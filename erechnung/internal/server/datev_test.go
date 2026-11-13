package server

import (
	"archive/zip"
	"bytes"
	"strings"
	"testing"

	"marktrechnung/internal/modell"
)

// Übergabe an DATEV: Übersicht, Buchungsstapel und Belegpaket.
func TestDatevExport(t *testing.T) {
	u := neu(t)
	k := u.einrichten()
	k.Debitorenkonto = "12345"
	u.ruf("POST", "/api/kunden", k, &k)
	// Zwei Rechnungen in zwei Monaten und ein Entwurf, der nicht mitkommt.
	var fertig [2]modell.Rechnung
	for i, tag := range []string{"2026-10-28", "2026-11-03"} {
		r := entwurf(k)
		r.Datum, r.Leistungsdatum = tag, tag
		var gespeichert modell.Rechnung
		u.ruf("POST", "/api/rechnungen", r, &gespeichert)
		if code := u.ruf("POST", "/api/rechnungen/"+gespeichert.ID+"/fertigstellen", map[string]bool{"trotzWarnungen": true}, &fertig[i]); code != 200 {
			t.Fatalf("fertigstellen: %d", code)
		}
	}
	nurEntwurf := entwurf(k)
	nurEntwurf.Datum = "2026-11-05"
	u.ruf("POST", "/api/rechnungen", nurEntwurf, nil)

	var ueb datevUebersicht
	u.ruf("GET", "/api/datev/uebersicht?von=2026-10&bis=2026-11", nil, &ueb)
	if ueb.Anzahl != 2 || len(ueb.Monate) != 2 || ueb.Monate[0].Monat != "2026-10" || len(ueb.Angaben) != 3 || ueb.MitKonten ||
		ueb.Brutto != fertig[0].Summen.Brutto+fertig[1].Summen.Brutto {
		t.Fatalf("Übersicht: %+v", ueb)
	}
	// Ohne Berater- und Mandantennummer gibt es keinen Buchungsstapel, das Belegpaket schon.
	if code := u.ruf("GET", "/api/datev/buchungen?von=2026-11&bis=2026-11", nil, nil); code != 422 {
		t.Errorf("Buchungsstapel ohne Angaben: %d", code)
	}
	var paket []byte
	if code := u.ruf("GET", "/api/datev/belege.zip?von=2026-11&bis=2026-11", nil, &paket); code != 200 {
		t.Fatalf("Belegpaket: %d", code)
	}
	z, err := zip.NewReader(bytes.NewReader(paket), int64(len(paket)))
	if err != nil {
		t.Fatal(err)
	}
	namen := ""
	for _, f := range z.File {
		namen += f.Name + " "
	}
	nummer := fertig[1].Nummer
	if len(z.File) != 3 || !strings.Contains(namen, "document.xml") || !strings.Contains(namen, "Rechnungsdaten_"+nummer+".xml") || !strings.Contains(namen, ".pdf") {
		t.Errorf("Inhalt des Belegpakets: %s", namen)
	}

	var e map[string]any
	u.ruf("GET", "/api/einstellungen", nil, &e)
	e["datev"] = modell.Datev{Berater: " 29098", Mandant: "55003 ", Kontenrahmen: "04"}
	if code := u.ruf("PUT", "/api/einstellungen", e, &e); code != 200 {
		t.Fatalf("Einstellungen: %d", code)
	}
	u.ruf("GET", "/api/datev/uebersicht?von=2026-11&bis=2026-11", nil, &ueb)
	if ueb.Anzahl != 1 || len(ueb.Angaben) != 0 || !ueb.MitKonten {
		t.Fatalf("Übersicht mit Angaben: %+v", ueb)
	}
	var stapel []byte
	if code := u.ruf("GET", "/api/datev/buchungen?von=2026-11&bis=2026-11&beleglink=1", nil, &stapel); code != 200 {
		t.Fatalf("Buchungsstapel: %d %s", code, stapel)
	}
	text := string(stapel)
	if !strings.HasPrefix(text, `"EXTF";700;21;"Buchungsstapel";13;`) || !strings.Contains(text, `;29098;55003;20260101;4;20261101;20261130;`) ||
		!strings.Contains(text, `;12345;4300;"";0311;"`+nummer+`";`) || !strings.Contains(text, `"BEDI ""`) || strings.Count(text, "\r\n") != 4 {
		t.Errorf("Buchungsstapel:\n%s", text)
	}
	// Zwei Monate: eine ZIP-Datei mit einer CSV-Datei je Monat.
	if code := u.ruf("GET", "/api/datev/buchungen?von=2026-10&bis=2026-11", nil, &stapel); code != 200 {
		t.Fatalf("Buchungsstapel für zwei Monate: %d", code)
	}
	z, err = zip.NewReader(bytes.NewReader(stapel), int64(len(stapel)))
	if err != nil || len(z.File) != 2 || z.File[0].Name != "EXTF_Buchungsstapel_Rechnungsausgang_2026-10.csv" || z.File[1].Name != "EXTF_Buchungsstapel_Rechnungsausgang_2026-11.csv" {
		t.Errorf("ZIP mit Stapeln: %v %+v", err, z)
	}
	// Ein Zeitraum ohne Rechnung.
	if code := u.ruf("GET", "/api/datev/belege.zip?von=2025-01&bis=2025-01", nil, nil); code != 422 {
		t.Errorf("leerer Zeitraum: %d", code)
	}
}
