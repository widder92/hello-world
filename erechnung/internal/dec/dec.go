// Package dec ist eine kleine Festkomma-Arithmetik für Geldbeträge, Mengen
// und Prozentsätze. Rechnungen dürfen nicht mit Gleitkommazahlen gerechnet
// werden: 0,1 + 0,2 ist dort nicht 0,3, und ein Cent Abweichung genügt, damit
// ein Rechnungseingangsportal die Rechnung ablehnt.
//
// Ein D speichert den Wert als ganze Zahl in Millionsteln (sechs
// Nachkommastellen). Multiplikation und Division rechnen exakt und runden
// erst am Ende kaufmännisch (halbe aufwärts, vom Nullpunkt weg).
package dec

import (
	"errors"
	"math/big"
	"strings"
)

// D ist eine Dezimalzahl mit sechs Nachkommastellen.
type D int64

const (
	stellen = 6
	eins    = 1000000
)

// Null und Eins als Konstanten.
const (
	Null D = 0
	Eins D = eins
	// Hundert wird für Prozentrechnungen gebraucht.
	Hundert D = 100 * eins
)

// Int macht aus einer ganzen Zahl ein D.
func Int(n int64) D { return D(n * eins) }

// Cent macht aus einem Betrag in Cent ein D.
func Cent(n int64) D { return D(n * (eins / 100)) }

// Parse liest eine Dezimalzahl. Erlaubt sind ein Vorzeichen, Ziffern und ein
// Dezimaltrenner (Punkt oder Komma). Tausenderpunkte werden nur verstanden,
// wenn zusätzlich ein Komma vorkommt ("1.234,56").
func Parse(s string) (D, error) {
	s = strings.TrimSpace(s)
	if s == "" {
		return 0, errors.New("leere Zahl")
	}
	if strings.Contains(s, ",") {
		s = strings.ReplaceAll(s, ".", "")
		s = strings.Replace(s, ",", ".", 1)
	}
	neg := false
	switch s[0] {
	case '-':
		neg = true
		s = s[1:]
	case '+':
		s = s[1:]
	}
	if s == "" {
		return 0, errors.New("keine Ziffern")
	}
	ganz, bruch, _ := strings.Cut(s, ".")
	if ganz == "" && bruch == "" {
		return 0, errors.New("keine Ziffern")
	}
	if len(ganz) > 12 {
		return 0, errors.New("Zahl zu groß")
	}
	var v int64
	for _, c := range ganz {
		if c < '0' || c > '9' {
			return 0, errors.New("ungültiges Zeichen in Zahl")
		}
		v = v*10 + int64(c-'0')
	}
	v *= eins
	// Nachkommastellen: sechs werden übernommen, die siebte rundet.
	var f int64
	n := 0
	aufrunden := false
	for _, c := range bruch {
		if c < '0' || c > '9' {
			return 0, errors.New("ungültiges Zeichen in Zahl")
		}
		if n < stellen {
			f = f*10 + int64(c-'0')
			n++
		} else if n == stellen {
			aufrunden = c >= '5'
			n++
		}
	}
	for i := n; i < stellen; i++ {
		f *= 10
	}
	v += f
	if aufrunden {
		v++
	}
	if neg {
		v = -v
	}
	return D(v), nil
}

// Muss liest eine Zahl und bricht bei Fehlern ab. Nur für Konstanten im
// Programmtext und für Tests gedacht.
func Muss(s string) D {
	d, err := Parse(s)
	if err != nil {
		panic("dec.Muss(" + s + "): " + err.Error())
	}
	return d
}

var zehnerpotenz = [...]int64{1, 10, 100, 1000, 10000, 100000, 1000000}

// Runde rundet kaufmännisch auf n Nachkommastellen (0 bis 6).
func (d D) Runde(n int) D {
	if n >= stellen {
		return d
	}
	if n < 0 {
		n = 0
	}
	teiler := zehnerpotenz[stellen-n]
	q := int64(d) / teiler
	r := int64(d) % teiler
	if r < 0 {
		r = -r
	}
	if r*2 >= teiler {
		if d < 0 {
			q--
		} else {
			q++
		}
	}
	return D(q * teiler)
}

// rundeGross teilt z durch n und rundet kaufmännisch.
func rundeGross(z, n *big.Int) *big.Int {
	neg := (z.Sign() < 0) != (n.Sign() < 0)
	za := new(big.Int).Abs(z)
	na := new(big.Int).Abs(n)
	q, r := new(big.Int).QuoRem(za, na, new(big.Int))
	r.Lsh(r, 1)
	if r.Cmp(na) >= 0 {
		q.Add(q, big.NewInt(1))
	}
	if neg {
		q.Neg(q)
	}
	return q
}

// Mal multipliziert exakt und rundet das Ergebnis in einem Schritt
// kaufmännisch auf n Nachkommastellen.
func (d D) Mal(o D, n int) D {
	if n > stellen {
		n = stellen
	}
	if n < 0 {
		n = 0
	}
	p := new(big.Int).Mul(big.NewInt(int64(d)), big.NewInt(int64(o)))
	// p hat zwölf Nachkommastellen, gewünscht sind n.
	teiler := new(big.Int).Exp(big.NewInt(10), big.NewInt(int64(2*stellen-n)), nil)
	q := rundeGross(p, teiler)
	return D(q.Int64() * zehnerpotenz[stellen-n])
}

// Durch teilt exakt und rundet auf n Nachkommastellen. Division durch null
// ergibt null.
func (d D) Durch(o D, n int) D {
	if o == 0 {
		return 0
	}
	if n > stellen {
		n = stellen
	}
	if n < 0 {
		n = 0
	}
	// d/o mit n Nachkommastellen: (d * 10^n) / o, kaufmännisch gerundet.
	z := new(big.Int).Mul(big.NewInt(int64(d)), big.NewInt(zehnerpotenz[n]))
	q := rundeGross(z, big.NewInt(int64(o)))
	return D(q.Int64() * zehnerpotenz[stellen-n])
}

// Prozent berechnet d * p / 100, gerundet auf n Stellen.
func (d D) Prozent(p D, n int) D {
	if n > stellen {
		n = stellen
	}
	z := new(big.Int).Mul(big.NewInt(int64(d)), big.NewInt(int64(p)))
	teiler := new(big.Int).Exp(big.NewInt(10), big.NewInt(int64(2*stellen-n+2)), nil)
	q := rundeGross(z, teiler)
	return D(q.Int64() * zehnerpotenz[stellen-n])
}

// Neg kehrt das Vorzeichen um.
func (d D) Neg() D { return -d }

// Abs liefert den Betrag.
func (d D) Abs() D {
	if d < 0 {
		return -d
	}
	return d
}

// IstNull meldet, ob der Wert null ist.
func (d D) IstNull() bool { return d == 0 }

// Nachkommastellen zählt die tatsächlich benutzten Nachkommastellen.
func (d D) Nachkommastellen() int {
	v := int64(d)
	if v < 0 {
		v = -v
	}
	n := stellen
	for n > 0 && v%10 == 0 {
		v /= 10
		n--
	}
	return n
}

// Text gibt die Zahl mit Punkt als Dezimaltrenner aus, mit mindestens min
// und höchstens max Nachkommastellen (überzählige werden gerundet).
func (d D) Text(min, max int) string {
	if max > stellen {
		max = stellen
	}
	if min > max {
		min = max
	}
	d = d.Runde(max)
	neg := d < 0
	v := int64(d)
	if neg {
		v = -v
	}
	ganz := v / eins
	bruch := v % eins
	var b strings.Builder
	if neg {
		b.WriteByte('-')
	}
	b.WriteString(itoa(ganz))
	// Bruch als sechs Ziffern, dann hinten kürzen.
	z := itoa(bruch)
	z = strings.Repeat("0", stellen-len(z)) + z
	z = z[:max]
	for len(z) > min && z[len(z)-1] == '0' {
		z = z[:len(z)-1]
	}
	if z != "" {
		b.WriteByte('.')
		b.WriteString(z)
	}
	return b.String()
}

// Fest gibt die Zahl mit genau n Nachkommastellen aus ("12.50").
func (d D) Fest(n int) string { return d.Text(n, n) }

// String gibt die Zahl ohne überflüssige Nullen aus.
func (d D) String() string { return d.Text(0, stellen) }

// Deutsch gibt die Zahl in deutscher Schreibweise aus: Tausenderpunkte,
// Komma als Dezimaltrenner, mindestens min und höchstens max Stellen.
func (d D) Deutsch(min, max int) string {
	s := d.Text(min, max)
	neg := strings.HasPrefix(s, "-")
	if neg {
		s = s[1:]
	}
	ganz, bruch, hat := strings.Cut(s, ".")
	var b strings.Builder
	if neg {
		b.WriteByte('-')
	}
	for i, c := range ganz {
		if i > 0 && (len(ganz)-i)%3 == 0 {
			b.WriteByte('.')
		}
		b.WriteRune(c)
	}
	if hat {
		b.WriteByte(',')
		b.WriteString(bruch)
	}
	return b.String()
}

func itoa(v int64) string {
	if v == 0 {
		return "0"
	}
	var buf [20]byte
	i := len(buf)
	for v > 0 {
		i--
		buf[i] = byte('0' + v%10)
		v /= 10
	}
	return string(buf[i:])
}

// MarshalJSON schreibt die Zahl als Zeichenkette, damit unterwegs niemand
// eine Gleitkommazahl daraus macht.
func (d D) MarshalJSON() ([]byte, error) {
	return []byte(`"` + d.String() + `"`), nil
}

// UnmarshalJSON liest eine Zahl aus einer Zeichenkette oder einer JSON-Zahl.
// Ein leerer Text und null bedeuten null.
func (d *D) UnmarshalJSON(b []byte) error {
	s := strings.Trim(string(b), `"`)
	s = strings.TrimSpace(s)
	if s == "" || s == "null" {
		*d = 0
		return nil
	}
	v, err := Parse(s)
	if err != nil {
		return err
	}
	*d = v
	return nil
}
