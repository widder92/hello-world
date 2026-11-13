package datev

import (
	"archive/zip"
	"bytes"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strings"
	"testing"
	"time"

	"marktrechnung/internal/modell"
	"marktrechnung/internal/probe"
	"marktrechnung/internal/rechnen"
)

var zeit = time.Date(2026, 11, 13, 20, 15, 30, 123e6, time.UTC)

func einstellungen() modell.Datev {
	return modell.Datev{Berater: "29098", Mandant: "55003", Kontenrahmen: "03"}
}

// rechnung liefert eine fertige Beispielrechnung mit zwei Steuersätzen.
func rechnung() modell.Rechnung {
	r := probe.Handel()
	r.ID, r.Nummer, r.Status = "r-1", "RE-2026-0042", modell.StatusFertig
	r.Kaeufer.Name = "Markt „West“ – Zentrale"
	r.Kaeufer.Kennung = "10042"
	return rechnen.Ausrechnen(r)
}

// Die Ausdrücke stammen aus der Formatbeschreibung im DATEV-Entwicklerportal
// (Kopfzeile und Buchungsstapel); jedes erzeugte Feld muss dazu passen.
var kopfRegeln = []string{
	`^["](EXTF|DTVF)["]$`, `^(700)$`, `^(16|20|21|46|48|65)$`,
	`^["](Buchungsstapel|Wiederkehrende Buchungen|Debitoren/Kreditoren|Kontenbeschriftungen|Zahlungsbedingungen|Diverse Adressen)["]$`,
	`^(2|4|5|13)$`,
	`^([2])([0])([0-9]{2})(0[1-9]|1[0-2])(0[1-9]|[1-2][0-9]|3[0-1])(2[0-3]|[01][0-9])([0-5][0-9])([0-5][0-9][0-9][0-9][0-9])$`,
	`^$`, `^["]\w{0,2}["]$`, `^["]\w{0,25}["]$`, `^["]\w{0,25}["]$`, `^(\d{4,6}|\d{7})$`, `^\d{1,5}$`,
	`^([2])([0])([0-9]{2})(0[1-9]|1[0-2])(0[1-9]|[1-2][0-9]|3[0-1])$`, `^[4-8]$`,
	`^([2])([0])([0-9]{2})(0[1-9]|1[0-2])(0[1-9]|[1-2][0-9]|3[0-1])$`,
	`^([2])([0])([0-9]{2})(0[1-9]|1[0-2])(0[1-9]|[1-2][0-9]|3[0-1])$`,
	`^["][\w.\-/ ]{0,30}["]$`, `^["]([A-Z]{2}){0,2}["]$`, `^[1-2]$`, `^(0|30|40|50|64)$`, `^(0|1)$`, `^["]([A-Z]{3})["]$`,
	`^$`, `^["]["]$`, `^$`, `^$`, `^["](\d{2}){0,2}["]$`, `^\d{0,4}$`, `^$`, `^["]["]$`, `^["].{0,16}["]$`,
}

var zeilenRegeln = map[int]string{
	1: `^\d{1,10},\d{2}$`, 2: `^["](S|H)["]$`, 7: `^(\d{1,9})$`, 8: `^(\d{1,9})$`, 9: `^(["]\d{4}["]|["]["])$`,
	10: `^(\d{4})$`, 11: `^(["][\w$&%*+\-/]{0,36}["])$`, 12: `^(["][\w$&%*+\-/]{0,36}["])$`, 14: `^(["].{0,60}["])$`,
	20: `^["]BEDI ["]["][0-9A-F]{8}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{12}["]["]["]$`, 114: `^(0|1)$`,
}

func TestBuchungsstapel(t *testing.T) {
	re := rechnung()
	korrektur := re
	korrektur.ID, korrektur.Nummer, korrektur.Art, korrektur.Datum = "r-2", "RE-2026-0043", modell.ArtGutschrift, "2026-11-20"
	kunde := &modell.Kunde{Debitorenkonto: "12345"}
	var buchungen []Buchung
	for _, r := range []modell.Rechnung{re, korrektur} {
		l, hinweise := Buchungen(r, kunde, einstellungen())
		if len(hinweise) != 0 || len(l) != 2 {
			t.Fatalf("Buchungen: %d, Hinweise %v", len(l), hinweise)
		}
		buchungen = append(buchungen, l...)
	}
	st := Stapel{Datev: einstellungen(), Jahr: 2026, Monat: 11, Erzeugt: zeit, Beleglink: true}
	daten, err := Buchungsstapel(st, buchungen)
	if err != nil {
		t.Fatal(err)
	}
	if !strings.HasPrefix(st.Dateiname(), "EXTF_") || !strings.HasSuffix(st.Dateiname(), ".csv") {
		t.Errorf("Dateiname %q", st.Dateiname())
	}
	if bytes.Contains(daten, []byte("\xc3")) || !bytes.Contains(daten, []byte("BU-Schl\xfcssel")) {
		t.Error("die Datei ist nicht in der Windows-Zeichentabelle 1252 geschrieben")
	}
	zeilen := strings.Split(string(daten), "\r\n")
	if len(zeilen) != 7 || zeilen[6] != "" || strings.Contains(strings.Join(zeilen, ""), "\n") {
		t.Fatalf("%d Zeilen, erwartet Kopf, Überschriften, 4 Buchungen", len(zeilen)-1)
	}
	kopf := strings.Split(zeilen[0], ";")
	if len(kopf) != len(kopfRegeln) {
		t.Fatalf("Kopf hat %d Felder, verlangt sind %d", len(kopf), len(kopfRegeln))
	}
	for i, regel := range kopfRegeln {
		if !regexp.MustCompile(regel).MatchString(kopf[i]) {
			t.Errorf("Kopffeld %d: %q passt nicht zu %s", i+1, kopf[i], regel)
		}
	}
	if zeilen[0] != `"EXTF";700;21;"Buchungsstapel";13;20261113201530123;;"RE";"";"";29098;55003;20260101;4;20261101;20261130;"Rechnungsausgang 11/2026";"";1;0;0;"EUR";;"";;;"03";;;"";""` {
		t.Errorf("Kopf: %s", zeilen[0])
	}
	if n := len(strings.Split(zeilen[1], ";")); n != 125 {
		t.Errorf("Überschriften: %d Spalten", n)
	}
	summe := map[string]string{}
	for _, zeile := range zeilen[2:6] {
		f := strings.Split(zeile, ";")
		if len(f) != 125 {
			t.Fatalf("Buchungszeile hat %d Felder: %s", len(f), zeile)
		}
		for nr, regel := range zeilenRegeln {
			if !regexp.MustCompile(regel).MatchString(f[nr-1]) {
				t.Errorf("Feld %d: %q passt nicht zu %s", nr, f[nr-1], regel)
			}
		}
		for i, art := range spaltenart {
			if (art == 'T') != strings.HasPrefix(f[i], `"`) && f[i] != "" {
				t.Errorf("Feld %d: %q, Spaltenart %c", i+1, f[i], art)
			}
		}
		summe[f[10]+f[1]+f[7]] = f[0]
		if f[6] != "12345" || (f[9] != "1311" && f[9] != "2011") || f[11] != `"131226"` {
			t.Errorf("Konto, Belegdatum, Fälligkeit: %s", zeile[:80])
		}
	}
	// 7 % auf 8300, 19 % auf 8400; die Korrektur im Haben.
	sieben, neunzehn := re.Summen.Steuern[0], re.Summen.Steuern[1]
	if sieben.Satz.String() != "7" {
		sieben, neunzehn = neunzehn, sieben
	}
	komma := func(s modell.Steuerzeile) string { return strings.ReplaceAll((s.Basis + s.Betrag).Fest(2), ".", ",") }
	for schluessel, betrag := range map[string]string{
		`"RE-2026-0042""S"8300`: komma(sieben), `"RE-2026-0042""S"8400`: komma(neunzehn),
		`"RE-2026-0043""H"8300`: komma(sieben), `"RE-2026-0043""H"8400`: komma(neunzehn),
	} {
		if summe[schluessel] != betrag {
			t.Errorf("%s: %q, erwartet %q (%v)", schluessel, summe[schluessel], betrag, summe)
		}
	}
	// Typografische Zeichen des Kundennamens überstehen die Zeichentabelle.
	if !bytes.Contains(daten, []byte("Muster Markt Lindenthal / Markt \x84West\x93 \x96 Zentrale")) {
		t.Error("Buchungstext: Sonderzeichen falsch umgesetzt")
	}

	// Ohne Beraternummer oder mit einer Rechnung aus einem anderen Monat gibt es keine Datei.
	ohne := st
	ohne.Datev.Berater = ""
	if _, err := Buchungsstapel(ohne, buchungen); err == nil {
		t.Error("fehlende Beraternummer nicht bemerkt")
	}
	st.Monat = 10
	if _, err := Buchungsstapel(st, buchungen); err == nil {
		t.Error("falscher Monat nicht bemerkt")
	}
}

func TestVorgabeUndAngaben(t *testing.T) {
	d := Vorgabe(modell.Datev{Kontenrahmen: "04", Kontenlaenge: 6})
	if d.Erloes19 != "440000" || d.Erloes7 != "430000" || d.ErloesOhne != "420000" || d.Debitor != "1000000" || d.Ordner != "Ausgangsrechnungen" || d.WJBeginn != 1 {
		t.Errorf("Vorgabe: %+v", d)
	}
	if l := PruefeAngaben(modell.Datev{Berater: "29098", Mandant: "1", Kontenrahmen: "04", Kontenlaenge: 6}); len(l) != 0 {
		t.Errorf("gültige Angaben beanstandet: %v", l)
	}
	if l := PruefeAngaben(modell.Datev{Berater: "12", Mandant: "", Kontenrahmen: "03", Erloes19: "84000"}); len(l) != 3 {
		t.Errorf("erwartet 3 Beanstandungen: %v", l)
	}
	if Belegnummer("RE 2026/0042_Ä.b") != "RE-2026/0042-Ae-b" {
		t.Errorf("Belegnummer: %q", Belegnummer("RE 2026/0042_Ä.b"))
	}
	if a, b := GUID(rechnung()), GUID(rechnung()); a != b || !regexp.MustCompile(`^[0-9A-F]{8}-[0-9A-F]{4}-5[0-9A-F]{3}-[89AB][0-9A-F]{3}-[0-9A-F]{12}$`).MatchString(a) {
		t.Errorf("GUID: %q %q", a, b)
	}
}

// Das Belegpaket wird gegen die Schema-Dateien geprüft, die DATEV im
// Entwicklerportal bereitstellt. Sie liegen nicht im Projekt; der Test
// läuft, wenn DATEV_XSD auf ihren Ordner zeigt und xmllint vorhanden ist.
func TestBelegpaket(t *testing.T) {
	re := rechnung()
	re.Faellig, re.BezahltAm = "2026-12-12", "2026-12-01"
	korrektur := re
	korrektur.ID, korrektur.Nummer, korrektur.Art, korrektur.BezahltAm = "r-2", "RE-2026-0043", modell.ArtGutschrift, ""
	ohneDatei := re
	ohneDatei.ID, ohneDatei.Nummer = "r-3", "RE-2026-0044"
	belege := []Beleg{
		{Rechnung: re, Dateien: []Datei{{Name: "RE-2026-0042.pdf", Daten: []byte("%PDF-1.7 Probe")}}},
		{Rechnung: korrektur, Dateien: []Datei{{Name: "Rechnungskorrektur RE-2026-0043 (Müller).xml", Daten: []byte("<a/>")}, {Name: "RE-2026-0042.pdf", Daten: []byte("%PDF")}}},
		{Rechnung: ohneDatei},
	}
	for _, d := range []modell.Datev{einstellungen(), {}} { // mit und ohne Konten
		var b bytes.Buffer
		hinweise, err := Belegpaket(&b, d, belege, zeit)
		if err != nil {
			t.Fatal(err)
		}
		if len(hinweise) != 1 || !strings.Contains(hinweise[0], "RE-2026-0044") {
			t.Errorf("Hinweise: %v", hinweise)
		}
		z, err := zip.NewReader(bytes.NewReader(b.Bytes()), int64(b.Len()))
		if err != nil {
			t.Fatal(err)
		}
		inhalt := map[string][]byte{}
		for _, f := range z.File {
			if strings.ContainsAny(f.Name, "/\\") || strings.TrimFunc(f.Name, func(r rune) bool { return r < 0x80 }) != "" {
				t.Errorf("Dateiname %q: nur ASCII, keine Unterordner", f.Name)
			}
			r, _ := f.Open()
			inhalt[f.Name], _ = io.ReadAll(r)
			r.Close()
		}
		for _, name := range []string{"document.xml", "Rechnungsdaten_RE-2026-0042.xml", "Rechnungsdaten_RE-2026-0043.xml", "Rechnungsdaten_RE-2026-0044.xml",
			"RE-2026-0042.pdf", "Rechnungskorrektur_RE-2026-0043__M_ller_.xml", "RE-2026-0042_2.pdf"} {
			if inhalt[name] == nil {
				t.Errorf("im Paket fehlt %s (vorhanden: %d Dateien)", name, len(inhalt))
			}
		}
		dok, daten, gutschrift := string(inhalt["document.xml"]), string(inhalt["Rechnungsdaten_RE-2026-0042.xml"]), string(inhalt["Rechnungsdaten_RE-2026-0043.xml"])
		if strings.Count(dok, "<document") != 3 || strings.Count(dok, `guid="`) != 2 || !strings.Contains(dok, `<property value="2026-11" key="1" />`) ||
			!strings.Contains(dok, `<property value="Ausgangsrechnungen" key="3" />`) || !strings.Contains(dok, `name="RE-2026-0042_2.pdf"`) {
			t.Errorf("document.xml:\n%s", dok)
		}
		if !strings.Contains(daten, `consolidatedAmount="`+re.Summen.Brutto.Fest(2)+`"`) || !strings.Contains(gutschrift, `consolidatedAmount="-`+re.Summen.Brutto.Fest(2)+`"`) ||
			!strings.Contains(daten, "<paidAt>2026-12-01</paidAt>") || strings.Contains(gutschrift, "paidAt") || !strings.Contains(daten, "<dueDate>2026-12-12</dueDate>") ||
			!strings.Contains(daten, `<customerName>Markt &#34;West&#34; - Zentrale</customerName>`) || !strings.Contains(daten, "<partyId>10042</partyId>") {
			t.Errorf("Rechnungsdaten:\n%s\n%s", daten, gutschrift)
		}
		mitKonten := d.Kontenrahmen != ""
		if strings.Contains(daten, "<accountNo>8300</accountNo>") != mitKonten || strings.Contains(daten, "<bpAccountNo>10000</bpAccountNo>") != mitKonten ||
			strings.Contains(daten, "accountNo>") != mitKonten {
			t.Errorf("Konten (mit: %v):\n%s", mitKonten, daten)
		}

		xsd := os.Getenv("DATEV_XSD")
		if _, err := exec.LookPath("xmllint"); xsd == "" || err != nil {
			t.Log("Schema-Prüfung ausgelassen (DATEV_XSD nicht gesetzt oder xmllint fehlt)")
			continue
		}
		ordner := t.TempDir()
		for name, daten := range inhalt {
			if !strings.HasPrefix(name, "Rechnungsdaten_") && name != "document.xml" {
				continue
			}
			schema := "Belegverwaltung_online_ledger_import_v060.xsd"
			if name == "document.xml" {
				schema = "Document_v060.xsd"
			}
			pfad := filepath.Join(ordner, name)
			os.WriteFile(pfad, daten, 0o600)
			if aus, err := exec.Command("xmllint", "--noout", "--schema", filepath.Join(xsd, schema), pfad).CombinedOutput(); err != nil {
				t.Errorf("%s entspricht nicht dem DATEV-Schema:\n%s\n%s", name, aus, daten)
			}
		}
	}
	// Ohne Rechnung gibt es kein Paket.
	if _, err := Belegpaket(io.Discard, einstellungen(), nil, zeit); err == nil {
		t.Error("leeres Paket nicht abgelehnt")
	}
}
