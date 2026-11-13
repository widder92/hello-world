package datev

import (
	"bytes"
	"errors"
	"fmt"
	"strconv"
	"strings"
	"time"

	"marktrechnung/internal/dec"
	"marktrechnung/internal/modell"
)

// Stapel beschreibt einen Buchungsstapel. DATEV empfiehlt eine Datei je
// Buchungsperiode - hier ist das ein Kalendermonat.
type Stapel struct {
	Datev     modell.Datev
	Jahr      int
	Monat     int
	Erzeugt   time.Time
	Beleglink bool // Buchungen mit den Belegen in DATEV Unternehmen online verknüpfen
}

// Dateiname des Stapels. DATEV liest nur Dateien, deren Name mit EXTF_ beginnt.
func (s Stapel) Dateiname() string {
	return fmt.Sprintf("EXTF_Buchungsstapel_Rechnungsausgang_%04d-%02d.csv", s.Jahr, s.Monat)
}

// PruefeAngaben meldet, was für den Buchungsstapel in den Einstellungen
// noch fehlt oder nicht stimmen kann.
func PruefeAngaben(d modell.Datev) []string {
	var l []string
	ziffern := func(s string) bool { return s != "" && strings.Trim(s, "0123456789") == "" }
	if n, _ := strconv.Atoi(d.Berater); !ziffern(d.Berater) || n < 1001 || n > 9999999 {
		l = append(l, "Die Beraternummer fehlt oder stimmt nicht (4 bis 7 Ziffern). Sie steht auf den Auswertungen der Steuerkanzlei.")
	}
	if n, _ := strconv.Atoi(d.Mandant); !ziffern(d.Mandant) || n < 1 || n > 99999 {
		l = append(l, "Die Mandantennummer fehlt oder stimmt nicht (1 bis 5 Ziffern).")
	}
	if d.Kontenrahmen != "03" && d.Kontenrahmen != "04" {
		l = append(l, "Der Kontenrahmen ist nicht gewählt (SKR 03 oder SKR 04).")
		return l
	}
	d = Vorgabe(d)
	for _, k := range []struct{ name, konto string }{{"Erlöskonto 19 %", d.Erloes19}, {"Erlöskonto 7 %", d.Erloes7}, {"Erlöskonto ohne USt", d.ErloesOhne}} {
		if !ziffern(k.konto) || len(k.konto) != d.Kontenlaenge {
			l = append(l, fmt.Sprintf("%s: „%s“ passt nicht zur Kontenlänge %d.", k.name, k.konto, d.Kontenlaenge))
		}
	}
	if !ziffern(d.Debitor) || len(d.Debitor) != d.Kontenlaenge+1 {
		l = append(l, fmt.Sprintf("Sammelkonto für Kunden: „%s“ muss %d Ziffern haben (eine mehr als die Sachkonten).", d.Debitor, d.Kontenlaenge+1))
	}
	return l
}

// Buchungsstapel erzeugt die Datei im DATEV-Format: Kopfzeile, Überschriften
// und je Buchung eine Zeile. Gebucht wird der Bruttobetrag vom Kunden
// (Personenkonto) an das Erlöskonto; die Umsatzsteuer rechnet DATEV über
// das Erlöskonto selbst heraus (Automatikkonto).
func Buchungsstapel(st Stapel, buchungen []Buchung) ([]byte, error) {
	if l := PruefeAngaben(st.Datev); len(l) > 0 {
		return nil, errors.New(strings.Join(l, " "))
	}
	d := Vorgabe(st.Datev)
	if st.Monat < 1 || st.Monat > 12 || st.Jahr < 2000 || st.Jahr > 2099 {
		return nil, fmt.Errorf("ungültiger Monat %d/%d", st.Monat, st.Jahr)
	}
	von := time.Date(st.Jahr, time.Month(st.Monat), 1, 0, 0, 0, 0, time.UTC)
	bis := von.AddDate(0, 1, -1)
	wj := time.Date(st.Jahr, time.Month(d.WJBeginn), 1, 0, 0, 0, 0, time.UTC)
	if wj.After(von) {
		wj = wj.AddDate(-1, 0, 0)
	}
	text := func(s string) string { return `"` + strings.ReplaceAll(s, `"`, `""`) + `"` }

	var b bytes.Buffer
	// Zeile 1: Kopf mit 31 Feldern.
	kopf := []string{
		text("EXTF"), "700", "21", text("Buchungsstapel"), "13",
		st.Erzeugt.Format("20060102150405") + fmt.Sprintf("%03d", st.Erzeugt.Nanosecond()/1e6),
		"", text("RE"), text(""), text(""),
		d.Berater, d.Mandant, wj.Format("20060102"), strconv.Itoa(d.Kontenlaenge),
		von.Format("20060102"), bis.Format("20060102"),
		text(fmt.Sprintf("Rechnungsausgang %02d/%04d", st.Monat, st.Jahr)), text(""),
		"1", "0", "0", text("EUR"), "", text(""), "", "", text(d.Kontenrahmen), "", "", text(""), text(""),
	}
	b.WriteString(strings.Join(kopf, ";") + "\r\n")
	// Zeile 2: Überschriften.
	b.WriteString(ueberschriften + "\r\n")

	for _, bu := range buchungen {
		re := bu.Rechnung
		tag, err := time.Parse("2006-01-02", re.Datum)
		if err != nil || tag.Before(von) || tag.After(bis) {
			return nil, fmt.Errorf("Rechnung %s vom %s gehört nicht in den Stapel %02d/%04d", re.Nummer, re.Datum, st.Monat, st.Jahr)
		}
		if bu.Brutto.IstNull() {
			continue
		}
		if bu.Debitor == "" || bu.Erloeskonto == "" {
			return nil, fmt.Errorf("Rechnung %s: Konto fehlt", re.Nummer)
		}
		f := make([]string, len(spaltenart))
		f[0] = strings.ReplaceAll(bu.Brutto.Abs().Fest(2), ".", ",")
		f[1] = "S" // der Kunde schuldet den Betrag
		if bu.Brutto < dec.D(0) {
			f[1] = "H" // Rechnungskorrektur
		}
		f[6], f[7] = bu.Debitor, bu.Erloeskonto
		f[9] = tag.Format("0201")
		f[10] = Belegnummer(re.Nummer)
		if faellig, err := time.Parse("2006-01-02", re.Faellig); err == nil {
			f[11] = faellig.Format("020106")
		}
		f[13] = kuerze(buchungstext(re, 60), 60)
		if st.Beleglink {
			f[19] = `BEDI "` + GUID(re) + `"`
		}
		f[113] = "0" // nicht festschreiben: Die Kanzlei kann noch ändern
		for i, wert := range f {
			if i > 0 {
				b.WriteByte(';')
			}
			if spaltenart[i] == 'T' {
				b.WriteString(text(ohneSteuerzeichen(wert)))
			} else {
				b.WriteString(wert)
			}
		}
		b.WriteString("\r\n")
	}
	return nachCP1252(b.String()), nil
}

// ohneSteuerzeichen entfernt Zeilenumbrüche und andere Steuerzeichen, die
// in Textfeldern nicht erlaubt sind.
func ohneSteuerzeichen(s string) string {
	return strings.Map(func(r rune) rune {
		if r < 0x20 || r == 0x7f {
			return ' '
		}
		return r
	}, s)
}

// Zeichen, die in der Windows-Zeichentabelle 1252 an anderer Stelle liegen
// als in Unicode.
var cp1252 = map[rune]byte{'€': 0x80, '‚': 0x82, 'ƒ': 0x83, '„': 0x84, '…': 0x85, '†': 0x86, '‡': 0x87, 'ˆ': 0x88, '‰': 0x89,
	'Š': 0x8a, '‹': 0x8b, 'Œ': 0x8c, 'Ž': 0x8e, '‘': 0x91, '’': 0x92, '“': 0x93, '”': 0x94, '•': 0x95, '–': 0x96, '—': 0x97,
	'˜': 0x98, '™': 0x99, 'š': 0x9a, '›': 0x9b, 'œ': 0x9c, 'ž': 0x9e, 'Ÿ': 0x9f}

// nachCP1252 wandelt in die Zeichentabelle, die DATEV Rechnungswesen ohne
// weitere Angaben erwartet. Was es dort nicht gibt, wird zum Fragezeichen.
func nachCP1252(s string) []byte {
	aus := make([]byte, 0, len(s))
	for _, r := range s {
		switch {
		case r < 0x80, r >= 0xa0 && r <= 0xff:
			aus = append(aus, byte(r))
		default:
			if c, ok := cp1252[r]; ok {
				aus = append(aus, c)
			} else {
				aus = append(aus, '?')
			}
		}
	}
	return aus
}
