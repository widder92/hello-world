package server

import (
	"bytes"
	"encoding/json"
	"io"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"testing/fstest"
	"time"

	"marktrechnung/internal/cii"
	"marktrechnung/internal/geheim"
	"marktrechnung/internal/modell"
	"marktrechnung/internal/partner"
	"marktrechnung/internal/post"
	"marktrechnung/internal/probe"
	"marktrechnung/internal/rechnen"
	"marktrechnung/internal/speicher"
	"marktrechnung/internal/zugferd"
)

var timeNull = time.Date(2026, 11, 13, 9, 0, 0, 0, time.UTC)

type umgebung struct {
	server   *Server
	t        *testing.T
	ts       *httptest.Server
	gesendet []post.Nachricht
}

func neu(t *testing.T) *umgebung {
	sp, err := speicher.Oeffne(t.TempDir())
	if err != nil {
		t.Fatal(err)
	}
	u := &umgebung{t: t}
	s := &Server{Speicher: sp, Passwort: geheim.Neu(t.TempDir(), "p"), Lexware: geheim.Neu(t.TempDir(), "l"), Token: "geheim", Version: "test",
		Web: fstest.MapFS{"index.html": {Data: []byte("<html>Marktrechnung</html>")}},
		Sende: func(n post.Nachricht, p modell.Postausgang, pw string) ([]byte, error) {
			u.gesendet = append(u.gesendet, n)
			return post.Baue(n, p, timeNull), nil
		}}
	u.server = s
	u.ts = httptest.NewServer(s.Handler())
	t.Cleanup(u.ts.Close)
	return u
}

// ruf schickt eine Anfrage und liest die Antwort als JSON in ziel.
func (u *umgebung) ruf(methode, pfad string, ein any, ziel any) int {
	u.t.Helper()
	var koerper io.Reader
	switch v := ein.(type) {
	case nil:
	case []byte:
		koerper = bytes.NewReader(v)
	default:
		b, _ := json.Marshal(v)
		koerper = bytes.NewReader(b)
	}
	a, _ := http.NewRequest(methode, u.ts.URL+pfad, koerper)
	a.AddCookie(&http.Cookie{Name: cookieName, Value: "geheim"})
	a.Header.Set("X-Marktrechnung", "1")
	antwort, err := http.DefaultClient.Do(a)
	if err != nil {
		u.t.Fatal(err)
	}
	defer antwort.Body.Close()
	roh, _ := io.ReadAll(antwort.Body)
	if ziel != nil {
		if b, ok := ziel.(*[]byte); ok {
			*b = roh
		} else if err := json.Unmarshal(roh, ziel); err != nil {
			u.t.Fatalf("%s %s: Antwort nicht lesbar: %v\n%s", methode, pfad, err, roh)
		}
	}
	return antwort.StatusCode
}

func (u *umgebung) einrichten() modell.Kunde {
	var e map[string]any
	u.ruf("GET", "/api/einstellungen", nil, &e)
	firma := probe.Verkaeufer()
	firma.Kennung = ""
	e["firma"] = firma
	e["iban"], e["bic"], e["bank"] = "DE89 3704 0044 0532 0130 00", "COBADEFFXXX", "Commerzbank Köln"
	e["post"] = modell.Postausgang{Server: "smtp.example", Port: 465, Sicherheit: "tls", Absender: "maria@sonnenwiese.example", KopieAnMich: true}
	e["passwort"] = "streng geheim"
	if code := u.ruf("PUT", "/api/einstellungen", e, &e); code != 200 {
		u.t.Fatalf("Einstellungen: %d %v", code, e)
	}
	vorlage := probe.Handel()
	k := modell.Kunde{Partei: vorlage.Kaeufer, Lieferantennummer: "700123", Profil: partner.Rewe, Format: modell.FormatZugferd,
		RechnungsEmail: "rechnung@mustermarkt.example", Zieltage: 30, Lieferorte: []modell.Lieferort{*vorlage.Lieferort}}
	k.Kennung = ""
	if code := u.ruf("POST", "/api/kunden", k, &k); code != 200 || k.ID == "" {
		u.t.Fatalf("Kunde: %d", code)
	}
	return k
}

func entwurf(k modell.Kunde) modell.Rechnung {
	v := probe.Handel()
	r := modell.Rechnung{KundeID: k.ID, Datum: v.Datum, Leistungsdatum: v.Leistungsdatum, Bestellnummer: v.Bestellnummer,
		Lieferscheinnummer: v.Lieferscheinnummer, Kostenstelle: v.Kostenstelle, Positionen: v.Positionen,
		Lieferort: &modell.Lieferort{ID: k.Lieferorte[0].ID}}
	r.Zahlung.Zieltage = 30
	return r
}

func TestSchutz(t *testing.T) {
	u := neu(t)
	// Ohne Cookie kommt man nicht an die Daten.
	antwort, _ := http.Get(u.ts.URL + "/api/einstellungen")
	if antwort.StatusCode != 401 {
		t.Errorf("ohne Cookie: %d", antwort.StatusCode)
	}
	// Mit Cookie, aber ohne Kennzeichen, wird nichts verändert.
	a, _ := http.NewRequest("POST", u.ts.URL+"/api/kunden", strings.NewReader("{}"))
	a.AddCookie(&http.Cookie{Name: cookieName, Value: "geheim"})
	antwort, _ = http.DefaultClient.Do(a)
	if antwort.StatusCode != 403 {
		t.Errorf("ohne Kennzeichen: %d", antwort.StatusCode)
	}
	// Ein fremder Host-Name (Angriff über umgeleitete Namensauflösung).
	a, _ = http.NewRequest("GET", u.ts.URL+"/api/stamm", nil)
	a.Host = "boese.example"
	a.AddCookie(&http.Cookie{Name: cookieName, Value: "geheim"})
	antwort, _ = http.DefaultClient.Do(a)
	if antwort.StatusCode != 403 {
		t.Errorf("fremder Host: %d", antwort.StatusCode)
	}
	// Der Start-Link setzt das Cookie nur mit dem richtigen Kennwort.
	ohneUmleitung := &http.Client{CheckRedirect: func(*http.Request, []*http.Request) error { return http.ErrUseLastResponse }}
	antwort, _ = ohneUmleitung.Get(u.ts.URL + "/?t=falsch")
	if len(antwort.Cookies()) != 0 {
		t.Error("Cookie trotz falschem Kennwort")
	}
	antwort, _ = ohneUmleitung.Get(u.ts.URL + "/?t=geheim")
	if len(antwort.Cookies()) != 1 || antwort.StatusCode != 303 {
		t.Errorf("Start-Link: %d, %d Cookies", antwort.StatusCode, len(antwort.Cookies()))
	}
}

func TestGanzerAblauf(t *testing.T) {
	u := neu(t)
	k := u.einrichten()

	// Entwurf prüfen: Stammdaten werden ergänzt, es bleibt kein Fehler.
	var erg pruefErgebnis
	if code := u.ruf("POST", "/api/pruefen", entwurf(k), &erg); code != 200 {
		t.Fatalf("pruefen: %d", code)
	}
	for _, m := range erg.Meldungen {
		if m.Schwere != modell.Info {
			t.Errorf("unerwartete Meldung: %s %s %s", m.Schwere, m.Regel, m.Text)
		}
	}
	if erg.NummerVorschau != "RE-2026-0001" || erg.Rechnung.Summen.Brutto.Fest(2) != "338.59" ||
		erg.Rechnung.Verkaeufer.Kennung != "700123" || erg.Rechnung.Lieferort.GLN != "4000001000012" || erg.Rechnung.Zahlung.IBAN == "" {
		t.Errorf("Ergebnis: %s %s %+v", erg.NummerVorschau, erg.Rechnung.Summen.Brutto, erg.Rechnung.Lieferort)
	}

	// Ohne Kostenstelle lehnt das REWE-Profil ab.
	ohne := entwurf(k)
	ohne.Kostenstelle = ""
	var gespeichert modell.Rechnung
	u.ruf("POST", "/api/rechnungen", ohne, &gespeichert)
	var abgelehnt fehlerAntwort
	if code := u.ruf("POST", "/api/rechnungen/"+gespeichert.ID+"/fertigstellen", map[string]bool{}, &abgelehnt); code != 422 || len(abgelehnt.Meldungen) == 0 {
		t.Fatalf("fertigstellen trotz Fehler: %d", code)
	}
	gespeichert.Kostenstelle = "1234567890"
	u.ruf("POST", "/api/rechnungen", gespeichert, &gespeichert)

	var vorschau []byte
	if code := u.ruf("POST", "/api/vorschau", gespeichert, &vorschau); code != 200 || !bytes.HasPrefix(vorschau, []byte("%PDF")) {
		t.Fatalf("Vorschau: %d", code)
	}

	var fertig modell.Rechnung
	if code := u.ruf("POST", "/api/rechnungen/"+gespeichert.ID+"/fertigstellen", map[string]bool{"trotzWarnungen": true}, &fertig); code != 200 {
		t.Fatalf("fertigstellen: %d", code)
	}
	if fertig.Nummer != "RE-2026-0001" || fertig.Status != modell.StatusFertig || len(fertig.Dateien) != 1 || fertig.Faellig != "2026-12-13" {
		t.Fatalf("fertig: %+v", fertig)
	}
	// Die abgelegte Datei ist eine ZUGFeRD-Rechnung mit genau diesen Daten.
	var pdf []byte
	u.ruf("GET", "/api/rechnungen/"+fertig.ID+"/datei/0", nil, &pdf)
	xml, err := zugferd.XMLAusPDF(pdf)
	if err != nil {
		t.Fatal(err)
	}
	gelesen, _, err := cii.Lies(xml)
	if err != nil || gelesen.Nummer != "RE-2026-0001" || gelesen.Kostenstelle != "1234567890" || gelesen.Kaeufer.GLN != "4000001000005" ||
		gelesen.Zahlung.Verwendungszweck != "RE-2026-0001" || gelesen.Summen.Brutto.Fest(2) != "338.59" {
		t.Errorf("eingebettete Rechnung: %+v %v", gelesen.Summen, err)
	}
	// Eine fertige Rechnung lässt sich nicht mehr überschreiben.
	if code := u.ruf("POST", "/api/rechnungen", fertig, nil); code != 409 {
		t.Errorf("Überschreiben: %d", code)
	}

	// Versand
	var vorschlag struct {
		An, Betreff, Text string
		Dateien, Hinweise []string
		Eingerichtet      bool
	}
	u.ruf("GET", "/api/rechnungen/"+fertig.ID+"/versand", nil, &vorschlag)
	if vorschlag.An != "rechnung@mustermarkt.example" || vorschlag.Betreff != "Rechnung RE-2026-0001" || !vorschlag.Eingerichtet ||
		len(vorschlag.Dateien) != 1 || len(vorschlag.Hinweise) == 0 || strings.Contains(vorschlag.Text, "{") {
		t.Errorf("Vorschlag: %+v", vorschlag)
	}
	var versendet modell.Rechnung
	if code := u.ruf("POST", "/api/rechnungen/"+fertig.ID+"/senden", map[string]string{"an": "kein e-mail"}, nil); code != 422 {
		t.Errorf("ungültige Adresse: %d", code)
	}
	if code := u.ruf("POST", "/api/rechnungen/"+fertig.ID+"/senden", map[string]string{"an": vorschlag.An, "betreff": vorschlag.Betreff, "text": vorschlag.Text}, &versendet); code != 200 {
		t.Fatalf("senden: %d", code)
	}
	if len(u.gesendet) != 1 || len(u.gesendet[0].Anhang) != 1 || u.gesendet[0].Anhang[0].Name != "Rechnung_RE-2026-0001.pdf" ||
		!bytes.Equal(u.gesendet[0].Anhang[0].Daten, pdf) || len(u.gesendet[0].Blind) != 1 {
		t.Errorf("gesendet: %+v", u.gesendet)
	}
	if versendet.Status != modell.StatusVersendet || len(versendet.Versand) != 1 {
		t.Errorf("Vermerk: %+v", versendet.Versand)
	}

	// Bezahlt, Liste, Export
	u.ruf("POST", "/api/rechnungen/"+fertig.ID+"/status", map[string]string{"status": "bezahlt", "datum": "2026-12-01"}, &versendet)
	var liste []zeile
	u.ruf("GET", "/api/rechnungen", nil, &liste)
	if len(liste) != 1 || liste[0].Status != modell.StatusBezahlt || liste[0].Brutto.Fest(2) != "338.59" || liste[0].Markt == "" {
		t.Errorf("Liste: %+v", liste)
	}
	var csv []byte
	u.ruf("GET", "/api/export.csv?jahr=2026", nil, &csv)
	if !strings.Contains(string(csv), "RE-2026-0001;Rechnung;13.11.2026") || !strings.Contains(string(csv), "338,59") {
		t.Errorf("Export:\n%s", csv)
	}

	// Storno: Gutschrift mit Bezug, danach ist die Rechnung storniert.
	u.ruf("POST", "/api/rechnungen/"+fertig.ID+"/status", map[string]string{"status": "offen"}, nil)
	var gutschrift modell.Rechnung
	if code := u.ruf("POST", "/api/rechnungen/"+fertig.ID+"/storno", nil, &gutschrift); code != 200 || gutschrift.Art != modell.ArtGutschrift {
		t.Fatalf("storno: %d", code)
	}
	if code := u.ruf("POST", "/api/rechnungen/"+gutschrift.ID+"/fertigstellen", map[string]bool{"trotzWarnungen": true}, &gutschrift); code != 200 {
		t.Fatalf("Gutschrift fertigstellen: %d", code)
	}
	var alt modell.Rechnung
	u.ruf("GET", "/api/rechnungen/"+fertig.ID, nil, &alt)
	if gutschrift.Nummer != "RE-2026-0002" || alt.Status != modell.StatusStorniert || alt.StornoDurch != gutschrift.ID {
		t.Errorf("nach Storno: %s %s", gutschrift.Nummer, alt.Status)
	}
	var sicherung []byte
	if code := u.ruf("GET", "/api/sicherung.zip", nil, &sicherung); code != 200 || !bytes.HasPrefix(sicherung, []byte("PK")) {
		t.Errorf("Sicherung: %d", code)
	}
}

// Eine von Hand gesetzte nächste Nummer muss beim Fertigstellen gelten.
func TestNummerVonHandGesetzt(t *testing.T) {
	u := neu(t)
	k := u.einrichten()
	var e map[string]any
	u.ruf("GET", "/api/einstellungen", nil, &e)
	e["naechsteNummer"] = 50
	u.ruf("PUT", "/api/einstellungen", e, &e)
	r := entwurf(k)
	r.Datum, r.Leistungsdatum = time.Now().Format("2006-01-02"), time.Now().Format("2006-01-02")
	var gespeichert, fertig modell.Rechnung
	u.ruf("POST", "/api/rechnungen", r, &gespeichert)
	if code := u.ruf("POST", "/api/rechnungen/"+gespeichert.ID+"/fertigstellen", map[string]bool{"trotzWarnungen": true}, &fertig); code != 200 {
		t.Fatalf("fertigstellen: %d", code)
	}
	if !strings.HasSuffix(fertig.Nummer, "-0050") {
		t.Errorf("Nummer %s, erwartet ...-0050", fertig.Nummer)
	}
}

func TestFremdeRechnungPruefenUndUebernehmen(t *testing.T) {
	u := neu(t)
	u.einrichten()
	// Eine Rechnung, wie sie ein anderes Programm ohne Handelsangaben
	// erzeugt: keine GLN, keine Kostenstelle.
	r := probe.Handel()
	r.Kaeufer.GLN, r.Lieferort, r.Kostenstelle, r.Kaeufer.Name = "", nil, "", "REWE Neuer Markt GmbH"
	r.Nummer = "LX-2026-17"
	xml := cii.Schreibe(rechnen.Ausrechnen(r))
	var erg importErgebnis
	if code := u.ruf("POST", "/api/import?name=lexoffice.xml", xml, &erg); code != 200 || !erg.Lesbar {
		t.Fatalf("import: %d %s", code, erg.Fehler)
	}
	gefunden := map[string]bool{}
	for _, m := range erg.Meldungen {
		gefunden[m.Regel] = true
	}
	// Das REWE-Profil ergibt sich aus dem Namen des unbekannten Kunden.
	if !gefunden["REWE-RE-GLN"] || !gefunden["REWE-WE-GLN"] || !gefunden["REWE-KOSTENSTELLE"] || !erg.Uebernehmen || erg.Stand != "neu" || erg.Kunde != "" {
		t.Errorf("Meldungen: %v, übernehmen %v, Stand %s", gefunden, erg.Uebernehmen, erg.Stand)
	}
	// Dasselbe als PDF.
	dateien, _ := zugferd.Erzeuge(r, zugferd.Optionen{})
	var ergPDF importErgebnis
	u.ruf("POST", "/api/import", dateien[0].Daten, &ergPDF)
	if !ergPDF.Lesbar || ergPDF.Rechnung.Nummer != "LX-2026-17" || len(ergPDF.Auffaellig) != 0 {
		t.Errorf("PDF-Import: %+v %v", ergPDF.Auffaellig, ergPDF.Fehler)
	}
	// Unlesbares wird verständlich abgelehnt.
	var kaputt importErgebnis
	u.ruf("POST", "/api/import", []byte("Hallo"), &kaputt)
	if kaputt.Lesbar || kaputt.Fehler == "" {
		t.Errorf("kaputte Datei: %+v", kaputt)
	}

	var entwurf modell.Rechnung
	if code := u.ruf("POST", "/api/import/uebernehmen", map[string]any{"rechnung": erg.Rechnung, "artikelAnlegen": true}, &entwurf); code != 200 {
		t.Fatalf("übernehmen: %d", code)
	}
	var kunden []modell.Kunde
	u.ruf("GET", "/api/kunden", nil, &kunden)
	if entwurf.Einleitung == "" || len(entwurf.Hinweise) != 0 || entwurf.Positionen[2].RabattProzent.String() != "10" || len(entwurf.Positionen[2].ZuAbschlaege) != 0 {
		t.Errorf("Texte und Rabatt: %q %v %+v", entwurf.Einleitung, entwurf.Hinweise, entwurf.Positionen[2])
	}
	if entwurf.Status != modell.StatusEntwurf || entwurf.Nummer != "LX-2026-17" || entwurf.KundeID == "" || len(kunden) != 2 ||
		entwurf.Profil != partner.Rewe || entwurf.Herkunftsbetrag.Fest(2) != "338.59" {
		t.Errorf("Entwurf: %+v, %d Kunden, Profil %q, Herkunftsbetrag %s", entwurf.Status, len(kunden), entwurf.Profil, entwurf.Herkunftsbetrag)
	}
	var artikel []modell.Artikel
	u.ruf("GET", "/api/artikel", nil, &artikel)
	if len(artikel) != 4 {
		t.Errorf("aus den Positionen wurden %d Artikel angelegt, erwartet 4", len(artikel))
	}
	var geprueft pruefErgebnis
	u.ruf("POST", "/api/pruefen", entwurf, &geprueft)
	if geprueft.Rechnung.Summen.Brutto.Fest(2) != "338.59" || geprueft.Rechnung.Faellig != "2026-12-13" {
		t.Errorf("übernommener Entwurf: %s fällig %s", geprueft.Rechnung.Summen.Brutto, geprueft.Rechnung.Faellig)
	}

	// Ein zweites Einlesen derselben Rechnung ersetzt den Entwurf.
	var nochmal importErgebnis
	u.ruf("POST", "/api/import", xml, &nochmal)
	if nochmal.Stand != "entwurf" || nochmal.VorhandenID != entwurf.ID || nochmal.Kunde != "REWE Neuer Markt GmbH" {
		t.Errorf("zweites Einlesen: Stand %s, Kunde %q", nochmal.Stand, nochmal.Kunde)
	}
	var ersetzt modell.Rechnung
	u.ruf("POST", "/api/import/uebernehmen", map[string]any{"rechnung": nochmal.Rechnung}, &ersetzt)
	var liste []zeile
	u.ruf("GET", "/api/rechnungen", nil, &liste)
	if ersetzt.ID != entwurf.ID || len(liste) != 1 {
		t.Errorf("Entwurf doppelt: %d Einträge", len(liste))
	}
}

// Der eigentliche Zweck des Imports: Was die Datei nicht enthält, kommt aus
// den Stammdaten - GLN des Kunden, Markt, Kostenstelle, GTIN der Artikel.
func TestImportErgaenztAusStammdaten(t *testing.T) {
	u := neu(t)
	k := u.einrichten() // Kunde mit GLN, einem Markt und REWE-Profil
	k.Lieferorte[0].Kostenstelle = "1234567890"
	u.ruf("POST", "/api/kunden", k, &k)
	u.ruf("POST", "/api/artikel", modell.Artikel{Bezeichnung: "Blütenhonig 500 g", GTIN: "4012345000016", Einheit: "XJR",
		KundenArtNr: map[string]string{k.ID: "880012"}}, nil)

	r := probe.Handel()
	r.Nummer = "LX-2026-18"
	r.Kaeufer.GLN, r.Lieferort, r.Kostenstelle, r.Verkaeufer.Kennung = "", nil, "", ""
	r.Positionen = r.Positionen[:1]
	r.Positionen[0].GTIN, r.Positionen[0].KundenArtNr, r.Positionen[0].Einheit = "", "", "C62"
	r.Zahlung.SkontoTage, r.Zahlung.SkontoProzent = 0, 0
	var erg importErgebnis
	u.ruf("POST", "/api/import?name=Rechnung.xml", cii.Schreibe(rechnen.Ausrechnen(r)), &erg)
	if erg.Kunde != k.Name || !erg.Uebernehmen {
		t.Fatalf("Kunde nicht erkannt: %q", erg.Kunde)
	}
	var entwurf modell.Rechnung
	if code := u.ruf("POST", "/api/import/uebernehmen", map[string]any{"rechnung": erg.Rechnung}, &entwurf); code != 200 {
		t.Fatalf("übernehmen: %d", code)
	}
	p := entwurf.Positionen[0]
	if entwurf.KundeID != k.ID || entwurf.Lieferort == nil || entwurf.Lieferort.GLN != "4000001000012" || entwurf.Kostenstelle != "1234567890" ||
		p.GTIN != "4012345000016" || p.KundenArtNr != "880012" || p.Einheit != "XJR" {
		t.Errorf("nicht ergänzt: Markt %+v Kostenstelle %q Position %+v", entwurf.Lieferort, entwurf.Kostenstelle, p)
	}
	// Mit den Stammdaten ist die Rechnung ohne weiteres Zutun versandfertig.
	var geprueft pruefErgebnis
	u.ruf("POST", "/api/pruefen", entwurf, &geprueft)
	for _, m := range geprueft.Meldungen {
		if m.Schwere == modell.Fehler {
			t.Errorf("nach der Übernahme noch ein Fehler: %s %s", m.Regel, m.Text)
		}
	}
	if geprueft.Rechnung.Kaeufer.GLN != "4000001000005" || geprueft.Rechnung.Verkaeufer.Kennung != "700123" {
		t.Errorf("Kundendaten nicht eingesetzt: %+v", geprueft.Rechnung.Kaeufer)
	}
	var fertig modell.Rechnung
	if code := u.ruf("POST", "/api/rechnungen/"+entwurf.ID+"/fertigstellen", map[string]bool{"trotzWarnungen": true}, &fertig); code != 200 || fertig.Nummer != "LX-2026-18" {
		t.Fatalf("fertigstellen: %d %s", code, fertig.Nummer)
	}
	// Eine fertige Rechnung wird nicht noch einmal übernommen.
	u.ruf("POST", "/api/import", cii.Schreibe(rechnen.Ausrechnen(r)), &erg)
	if erg.Stand != "vorhanden" {
		t.Errorf("Stand %s, erwartet vorhanden", erg.Stand)
	}
	if code := u.ruf("POST", "/api/import/uebernehmen", map[string]any{"rechnung": erg.Rechnung}, nil); code != 409 {
		t.Errorf("doppelte Übernahme: %d", code)
	}
}

// Die erste eingelesene Rechnung kann die eigenen Firmendaten liefern.
func TestFirmendatenAusRechnung(t *testing.T) {
	u := neu(t)
	r := probe.Handel()
	var erg importErgebnis
	u.ruf("POST", "/api/import", cii.Schreibe(rechnen.Ausrechnen(r)), &erg)
	if !erg.FirmaLeer || erg.Uebernehmen {
		t.Fatalf("leere Firma nicht erkannt: %+v", erg.FirmaLeer)
	}
	var e modell.Einstellungen
	if code := u.ruf("POST", "/api/import/firma", map[string]any{"verkaeufer": erg.Rechnung.Verkaeufer, "zahlung": erg.Rechnung.Zahlung}, &e); code != 200 {
		t.Fatalf("Firma übernehmen: %d", code)
	}
	if e.Firma.Name != r.Verkaeufer.Name || e.Firma.UStID != "DE123456789" || e.Firma.GLN != r.Verkaeufer.GLN || e.IBAN == "" || e.Firma.Telefon == "" {
		t.Errorf("Firmendaten: %+v IBAN %q", e.Firma, e.IBAN)
	}
	u.ruf("POST", "/api/import", cii.Schreibe(rechnen.Ausrechnen(r)), &erg)
	if erg.FirmaLeer || !erg.Uebernehmen {
		t.Error("nach dem Eintragen der Firma muss sich die Rechnung übernehmen lassen")
	}
}
