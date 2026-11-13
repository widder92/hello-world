package datev

import (
	"archive/zip"
	"bytes"
	"encoding/xml"
	"fmt"
	"io"
	"path/filepath"
	"strings"
	"time"

	"marktrechnung/internal/modell"
)

// Beleg ist eine Rechnung mit ihren Dateien für das Belegpaket.
type Beleg struct {
	Rechnung modell.Rechnung
	Kunde    *modell.Kunde
	Dateien  []Datei
}

// Datei: Name und Inhalt einer Rechnungsdatei (PDF oder XML).
type Datei struct {
	Name  string
	Daten []byte
}

const (
	nsDokument = "http://xml.datev.de/bedi/tps/document/v06.0"
	nsLedger   = "http://xml.datev.de/bedi/tps/ledger/v060"
)

// Belegpaket schreibt die ZIP-Datei nach der DATEV XML-Schnittstelle
// online: document.xml, je Rechnung eine Datei mit den Rechnungsdaten und
// die Rechnung selbst. Unterordner sind in der ZIP-Datei nicht erlaubt.
func Belegpaket(w io.Writer, d modell.Datev, belege []Beleg, erzeugt time.Time) (hinweise []string, err error) {
	d = Vorgabe(d)
	z := zip.NewWriter(w)
	vergeben := map[string]bool{"document.xml": true}
	// frei liefert einen Dateinamen aus reinen ASCII-Zeichen, den es im
	// Paket noch nicht gibt.
	frei := func(name string) string {
		endung := filepath.Ext(name)
		stamm := strings.Map(func(r rune) rune {
			if r >= 'a' && r <= 'z' || r >= 'A' && r <= 'Z' || r >= '0' && r <= '9' || r == '-' || r == '_' || r == '.' {
				return r
			}
			return '_'
		}, strings.TrimSuffix(name, endung))
		endung = strings.ToLower(endung)
		kandidat := stamm + endung
		for i := 2; vergeben[strings.ToLower(kandidat)]; i++ {
			kandidat = fmt.Sprintf("%s_%d%s", stamm, i, endung)
		}
		vergeben[strings.ToLower(kandidat)] = true
		return kandidat
	}
	lege := func(name string, daten []byte) error {
		f, err := z.Create(name)
		if err != nil {
			return err
		}
		_, err = f.Write(daten)
		return err
	}

	var dok bytes.Buffer
	dok.WriteString(`<?xml version="1.0" encoding="utf-8"?>` + "\n")
	fmt.Fprintf(&dok, `<archive xmlns="%s" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:schemaLocation="%s Document_v060.xsd" version="6.0" generatingSystem="%s">`+"\n",
		nsDokument, nsDokument, Programm)
	fmt.Fprintf(&dok, "  <header>\n    <date>%s</date>\n    <description>%s</description>\n  </header>\n  <content>\n",
		erzeugt.Format("2006-01-02T15:04:05"), x(kuerze("Ausgangsrechnungen aus "+Programm, 255)))

	anzahl := 0
	for _, b := range belege {
		re := b.Rechnung
		if re.Summen == nil || len(re.Datum) < 7 {
			continue
		}
		buchungen, h := Buchungen(re, b.Kunde, d)
		hinweise = append(hinweise, h...)
		if len(buchungen) == 0 {
			hinweise = append(hinweise, "Rechnung "+re.Nummer+" hat keinen Betrag und wurde ausgelassen.")
			continue
		}
		if len(b.Dateien) == 0 {
			hinweise = append(hinweise, "Zu Rechnung "+re.Nummer+" liegt keine Datei mehr im Ordner Rechnungen; übergeben werden nur die Rechnungsdaten.")
		}
		datenname := frei("Rechnungsdaten_" + Belegnummer(re.Nummer) + ".xml")
		if err := lege(datenname, rechnungsdaten(buchungen)); err != nil {
			return hinweise, err
		}
		guid := ""
		if len(b.Dateien) > 0 {
			guid = fmt.Sprintf(` guid="%s"`, GUID(re)) // die Kennung braucht einen Beleg
		}
		fmt.Fprintf(&dok, "    <document%s>\n", guid)
		fmt.Fprintf(&dok, "      <extension xsi:type=\"accountsReceivableLedger\" datafile=\"%s\">\n        <property value=\"%s\" key=\"1\" />\n        <property value=\"%s\" key=\"3\" />\n      </extension>\n",
			x(datenname), re.Datum[:7], x(kuerze(sauber(d.Ordner), 255)))
		for _, f := range b.Dateien {
			name := frei(f.Name)
			if err := lege(name, f.Daten); err != nil {
				return hinweise, err
			}
			fmt.Fprintf(&dok, "      <extension xsi:type=\"File\" name=\"%s\" />\n", x(name))
		}
		dok.WriteString("    </document>\n")
		anzahl++
	}
	dok.WriteString("  </content>\n</archive>\n")
	if anzahl == 0 {
		return hinweise, fmt.Errorf("im gewählten Zeitraum gibt es keine fertige Rechnung")
	}
	if anzahl > 5000 {
		return hinweise, fmt.Errorf("ein Belegpaket fasst höchstens 5000 Rechnungen - bitte einen kürzeren Zeitraum wählen")
	}
	if err := lege("document.xml", dok.Bytes()); err != nil {
		return hinweise, err
	}
	return hinweise, z.Close()
}

// rechnungsdaten schreibt die Belegsatzdatei einer Ausgangsrechnung: je
// Steuersatz ein Eintrag mit dem Bruttobetrag.
func rechnungsdaten(buchungen []Buchung) []byte {
	re := buchungen[0].Rechnung
	summe := buchungen[0].Brutto - buchungen[0].Brutto
	for _, bu := range buchungen {
		summe += bu.Brutto
	}
	lieferung := re.Leistungsdatum
	if lieferung == "" {
		lieferung = re.ZeitraumBis
	}
	nummer := Belegnummer(re.Nummer)

	var b bytes.Buffer
	b.WriteString(`<?xml version="1.0" encoding="utf-8"?>` + "\n")
	fmt.Fprintf(&b, `<LedgerImport xmlns="%s" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:schemaLocation="%s Belegverwaltung_online_ledger_import_v060.xsd" version="6.0" generator_info="%s" generating_system="%s" xml_data="Kopie nur zur Verbuchung berechtigt nicht zum Vorsteuerabzug">`+"\n",
		nsLedger, nsLedger, Programm, Programm)
	fmt.Fprintf(&b, `  <consolidate consolidatedAmount="%s" consolidatedDate="%s" consolidatedInvoiceId="%s" consolidatedCurrencyCode="EUR"`, summe.Fest(2), re.Datum, x(nummer))
	if datum(lieferung) {
		fmt.Fprintf(&b, ` consolidatedDeliveryDate="%s"`, lieferung)
	}
	b.WriteString(">\n")
	// el schreibt ein Element nur, wenn es einen Inhalt hat: Leere
	// Elemente lehnt DATEV ab.
	el := func(name, wert string) {
		if wert != "" {
			fmt.Fprintf(&b, "      <%s>%s</%s>\n", name, x(wert), name)
		}
	}
	markt := ""
	if re.Lieferort != nil {
		markt = re.Lieferort.Name
	}
	var info []string
	for _, teil := range []struct{ titel, wert string }{{"Markt", markt}, {"Lieferschein", re.Lieferscheinnummer}, {"Bestellung", re.Bestellnummer}} {
		if strings.TrimSpace(teil.wert) != "" {
			info = append(info, teil.titel+" "+strings.TrimSpace(teil.wert))
		}
	}
	for _, bu := range buchungen {
		b.WriteString("    <accountsReceivableLedger>\n")
		el("date", re.Datum)
		el("amount", bu.Brutto.Fest(2))
		el("accountNo", bu.Erloeskonto)
		el("tax", bu.Satz.Fest(2))
		el("information", kuerze(sauber(strings.Join(info, ", ")), 120))
		el("currencyCode", "EUR")
		el("invoiceId", nummer)
		el("bookingText", kuerze(sauber(buchungstext(re, 30)), 30))
		el("partyId", kuerze(sauber(re.Kaeufer.Kennung), 15))
		if datum(re.BezahltAm) {
			el("paidAt", re.BezahltAm)
		}
		el("vatId", ustID(re.Kaeufer.UStID))
		if datum(re.Faellig) {
			el("dueDate", re.Faellig)
		}
		el("bpAccountNo", bu.Debitor)
		if datum(lieferung) {
			el("deliveryDate", lieferung)
		}
		el("customerName", kuerze(sauber(re.Kaeufer.Name), 50))
		el("customerCity", kuerze(sauber(re.Kaeufer.Ort), 30))
		b.WriteString("    </accountsReceivableLedger>\n")
	}
	b.WriteString("  </consolidate>\n</LedgerImport>\n")
	return b.Bytes()
}

func datum(s string) bool {
	_, err := time.Parse("2006-01-02", s)
	return err == nil
}

// x maskiert Text für XML.
func x(s string) string {
	var b bytes.Buffer
	xml.EscapeText(&b, []byte(s))
	return b.String()
}

// ustID lässt von einer USt-IdNr. nur übrig, was DATEV dort annimmt.
func ustID(s string) string {
	s = strings.Map(func(r rune) rune {
		if r >= 'a' && r <= 'z' || r >= 'A' && r <= 'Z' || r >= '0' && r <= '9' {
			return r
		}
		return -1
	}, s)
	if len(s) > 15 {
		return ""
	}
	return strings.ToUpper(s)
}

// sauber ersetzt Zeichen, die die DATEV XML-Schnittstelle nicht zulässt:
// Erlaubt sind lateinische Buchstaben mit Umlauten und Akzenten, Ziffern
// und die üblichen Satzzeichen - typografische Anführungszeichen und lange
// Striche zum Beispiel nicht.
func sauber(s string) string {
	return strings.Map(func(r rune) rune {
		switch {
		case r >= 'a' && r <= 'z', r >= 'A' && r <= 'Z', r >= '0' && r <= '9':
			return r
		case strings.ContainsRune("§@€#|`´'^°%&*~:;.-/+=_ ?$!{}[],()<>\\\"", r):
			return r
		case r >= 0x00a0 && r <= 0x00ff:
			return r
		case r == '„', r == '“', r == '”', r == '«', r == '»':
			return '"'
		case r == '‚', r == '‘', r == '’':
			return '\''
		case r == '–', r == '—', r == '‑':
			return '-'
		case r == '\t', r == '\n', r == '\r', r == 0x202f, r == 0x2009:
			return ' '
		}
		return '?'
	}, s)
}
