// Paket datev übergibt Rechnungen an DATEV - auf den beiden Wegen, die
// DATEV für fremde Programme vorsieht:
//
//   - Buchungsstapel im DATEV-Format (CSV-Datei "EXTF_..."), den die
//     Steuerkanzlei in DATEV Rechnungswesen einliest, und
//   - Belegpaket nach der DATEV XML-Schnittstelle online (ZIP-Datei mit
//     document.xml, Rechnungsdaten und den Rechnungen selbst), das mit dem
//     Programm DATEV Belegtransfer nach DATEV Unternehmen online geht.
//
// Grundlage sind die Beschreibungen im DATEV-Entwicklerportal
// (developer.datev.de, Dateiformate): DATEV-Format mit Kopfzeile 700 und
// Buchungsstapel in Fassung 13, DATEV XML-Schnittstelle online 6.0.
package datev

import (
	"crypto/sha1"
	"fmt"
	"strings"

	"marktrechnung/internal/dec"
	"marktrechnung/internal/modell"
)

// Programm nennt sich in den Dateien als erzeugendes System.
const Programm = "Marktrechnung"

// Vorgabe ergänzt, was in den Einstellungen leer ist: Kontenlänge, Konten
// des gewählten Kontenrahmens, Sammelkonto, Ordner. Ohne Kontenrahmen
// bleiben die Konten leer.
func Vorgabe(d modell.Datev) modell.Datev {
	if d.Kontenlaenge < 4 || d.Kontenlaenge > 8 {
		d.Kontenlaenge = 4
	}
	if d.WJBeginn < 1 || d.WJBeginn > 12 {
		d.WJBeginn = 1
	}
	if strings.TrimSpace(d.Ordner) == "" {
		d.Ordner = "Ausgangsrechnungen"
	}
	// Standardkonten: Bei längeren Kontonummern hängt DATEV Nullen an.
	lang := func(konto string, stellen int) string {
		for len(konto) < stellen {
			konto += "0"
		}
		return konto
	}
	var k19, k7, ohne string
	switch d.Kontenrahmen {
	case "03":
		k19, k7, ohne = "8400", "8300", "8200"
	case "04":
		k19, k7, ohne = "4400", "4300", "4200"
	default:
		d.Kontenrahmen = ""
		return d
	}
	for _, paar := range []struct {
		ziel   *string
		konto  string
		laenge int
	}{{&d.Erloes19, k19, d.Kontenlaenge}, {&d.Erloes7, k7, d.Kontenlaenge}, {&d.ErloesOhne, ohne, d.Kontenlaenge}, {&d.Debitor, "10000", d.Kontenlaenge + 1}} {
		if *paar.ziel = strings.TrimSpace(*paar.ziel); *paar.ziel == "" {
			*paar.ziel = lang(paar.konto, paar.laenge)
		}
	}
	return d
}

// Buchung ist eine Zeile der Übergabe: der Bruttobetrag einer Rechnung zu
// einem Steuersatz.
type Buchung struct {
	Rechnung    modell.Rechnung
	Brutto      dec.D  // mit Vorzeichen: negativ bei Rechnungskorrekturen
	Satz        dec.D  // Steuersatz in Prozent
	Erloeskonto string // leer: Die Kanzlei ordnet zu
	Debitor     string // Personenkonto des Kunden, leer ohne Kontenrahmen
}

// Buchungen zerlegt eine fertige Rechnung nach Steuersätzen. Die Hinweise
// nennen, was die Kanzlei wissen muss - etwa einen Steuersatz, für den es
// kein Erlöskonto gibt.
func Buchungen(re modell.Rechnung, kunde *modell.Kunde, d modell.Datev) (l []Buchung, hinweise []string) {
	if re.Summen == nil {
		return nil, nil
	}
	d = Vorgabe(d)
	vz := dec.Eins
	if re.Art == modell.ArtGutschrift {
		vz = -dec.Eins
	}
	debitor := ""
	if d.Kontenrahmen != "" {
		debitor = d.Debitor
		if kunde != nil && strings.TrimSpace(kunde.Debitorenkonto) != "" {
			debitor = strings.TrimSpace(kunde.Debitorenkonto)
		}
	}
	for _, st := range re.Summen.Steuern {
		brutto := (st.Basis + st.Betrag).Mal(vz, 2)
		if brutto.IstNull() {
			continue
		}
		konto := ""
		if d.Kontenrahmen != "" {
			switch {
			case st.Betrag.IstNull():
				konto = d.ErloesOhne
			case st.Satz == dec.Int(19):
				konto = d.Erloes19
			case st.Satz == dec.Int(7):
				konto = d.Erloes7
			default:
				konto = d.ErloesOhne
				hinweise = append(hinweise, fmt.Sprintf("Rechnung %s: Für den Steuersatz %s %% ist kein Erlöskonto hinterlegt; gebucht wird auf %s. Bitte mit der Steuerkanzlei klären.",
					re.Nummer, st.Satz.Deutsch(0, 2), konto))
			}
		}
		l = append(l, Buchung{Rechnung: re, Brutto: brutto, Satz: st.Satz, Erloeskonto: konto, Debitor: debitor})
	}
	return l, hinweise
}

// GUID liefert zu einer Rechnung eine feste Kennung. Unter ihr liegt der
// Beleg in DATEV Unternehmen online, und über sie verweist die Buchung auf
// den Beleg (Beleglink). Dieselbe Rechnung ergibt immer dieselbe Kennung.
func GUID(re modell.Rechnung) string {
	h := sha1.Sum([]byte("marktrechnung/rechnung/" + re.ID + "/" + re.Nummer))
	b := h[:16]
	b[6] = b[6]&0x0f | 0x50 // Fassung 5: aus einem Namen abgeleitet
	b[8] = b[8]&0x3f | 0x80
	return strings.ToUpper(fmt.Sprintf("%x-%x-%x-%x-%x", b[0:4], b[4:6], b[6:8], b[8:10], b[10:16]))
}

// Belegnummer bringt eine Rechnungsnummer in die Form, die DATEV als
// Belegnummer annimmt: Buchstaben, Ziffern und $ % & * + - /, höchstens 36
// Zeichen. Umlaute werden umschrieben, alles andere wird zum Bindestrich.
func Belegnummer(nummer string) string {
	var b strings.Builder
	for _, r := range strings.TrimSpace(nummer) {
		switch {
		case r >= 'a' && r <= 'z', r >= 'A' && r <= 'Z', r >= '0' && r <= '9', strings.ContainsRune("$%&*+-/", r):
			b.WriteRune(r)
		case r == 'ä':
			b.WriteString("ae")
		case r == 'ö':
			b.WriteString("oe")
		case r == 'ü':
			b.WriteString("ue")
		case r == 'Ä':
			b.WriteString("Ae")
		case r == 'Ö':
			b.WriteString("Oe")
		case r == 'Ü':
			b.WriteString("Ue")
		case r == 'ß':
			b.WriteString("ss")
		default:
			b.WriteByte('-')
		}
	}
	s := b.String()
	if len(s) > 36 {
		s = s[:36]
	}
	return s
}

// kuerze schneidet einen Text auf höchstens n Zeichen.
func kuerze(s string, n int) string {
	s = strings.Join(strings.Fields(s), " ")
	if r := []rune(s); len(r) > n {
		return strings.TrimSpace(string(r[:n]))
	}
	return s
}

// buchungstext nennt den Kunden und, wenn es passt, den Markt.
func buchungstext(re modell.Rechnung, n int) string {
	text := re.Kaeufer.Name
	if re.Lieferort != nil && re.Lieferort.Name != "" && re.Lieferort.Name != re.Kaeufer.Name {
		text = re.Lieferort.Name + " / " + re.Kaeufer.Name
	}
	if re.Art == modell.ArtGutschrift {
		text = "Korrektur " + text
	}
	return kuerze(text, n)
}
