package server

import (
	"bytes"
	"strings"
	"testing"
	"time"

	"marktrechnung/internal/dec"
	"marktrechnung/internal/modell"
)

// Angebot -> Lieferschein -> Rechnung, jeweils mit eigenem Nummernkreis.
func TestAngebotLieferscheinRechnung(t *testing.T) {
	u := neu(t)
	k := u.einrichten()
	vorlage := entwurf(k)
	angebot := modell.Rechnung{Belegart: modell.BelegAngebot, KundeID: k.ID, Datum: "2026-11-02", GueltigBis: "2026-12-02",
		Positionen: vorlage.Positionen, Lieferort: vorlage.Lieferort, Einleitung: "Gern bieten wir an:"}
	if code := u.ruf("POST", "/api/belege", angebot, &angebot); code != 200 || angebot.Nummer != "AN-2026-0001" || angebot.Status != modell.StatusOffen {
		t.Fatalf("Angebot: %d %q %q", code, angebot.Nummer, angebot.Status)
	}
	// Ändern behält die Nummer; ein zweites Angebot bekommt die nächste.
	angebot.Einleitung = "Unser Angebot:"
	u.ruf("POST", "/api/belege", angebot, &angebot)
	zweites := modell.Rechnung{Belegart: modell.BelegAngebot, KundeID: k.ID, Datum: "2026-11-03", Positionen: vorlage.Positionen}
	u.ruf("POST", "/api/belege", zweites, &zweites)
	if angebot.Nummer != "AN-2026-0001" || zweites.Nummer != "AN-2026-0002" {
		t.Errorf("Nummern: %q %q", angebot.Nummer, zweites.Nummer)
	}
	var liste []modell.Rechnung
	u.ruf("GET", "/api/belege?art=angebot", nil, &liste)
	if len(liste) != 2 || liste[0].Summen == nil || liste[0].Kaeufer.Name != k.Name {
		t.Fatalf("Liste der Angebote: %+v", liste)
	}
	var pdf []byte
	if code := u.ruf("GET", "/api/belege/"+angebot.ID+"/pdf", nil, &pdf); code != 200 || !bytes.HasPrefix(pdf, []byte("%PDF")) {
		t.Fatalf("PDF des Angebots: %d", code)
	}
	if bytes.Contains(pdf, []byte("factur-x")) {
		t.Error("ein Angebot ist keine E-Rechnung und enthält keine Rechnungsdaten")
	}

	var schein modell.Rechnung
	if code := u.ruf("POST", "/api/belege/"+angebot.ID+"/weiter/lieferschein", nil, &schein); code != 200 || schein.Nummer != "LS-2026-0001" || schein.Belegart != modell.BelegLieferschein {
		t.Fatalf("Lieferschein: %d %+v", code, schein.Nummer)
	}
	u.ruf("GET", "/api/belege/"+angebot.ID, nil, &angebot)
	if angebot.Status != modell.StatusAngenommen || angebot.Folgebeleg != "Lieferschein LS-2026-0001" {
		t.Errorf("Angebot nach dem Lieferschein: %q %q", angebot.Status, angebot.Folgebeleg)
	}
	if code := u.ruf("GET", "/api/belege/"+schein.ID+"/pdf", nil, &pdf); code != 200 || !bytes.HasPrefix(pdf, []byte("%PDF")) {
		t.Fatalf("PDF des Lieferscheins: %d", code)
	}

	var rechnung modell.Rechnung
	if code := u.ruf("POST", "/api/belege/"+schein.ID+"/weiter/rechnung", nil, &rechnung); code != 200 {
		t.Fatalf("Rechnung aus dem Lieferschein: %d", code)
	}
	if rechnung.Status != modell.StatusEntwurf || rechnung.Lieferscheinnummer != "LS-2026-0001" || rechnung.KundeID != k.ID ||
		len(rechnung.Positionen) != len(vorlage.Positionen) || rechnung.Belegart != "" || rechnung.Leistungsdatum == "" {
		t.Errorf("Entwurf der Rechnung: %+v", rechnung)
	}
	u.ruf("GET", "/api/belege/"+schein.ID, nil, &schein)
	if schein.Status != modell.StatusBerechnet {
		t.Errorf("Lieferschein nach der Rechnung: %q", schein.Status)
	}
	// Die Zusatzbelege tauchen nicht unter den Rechnungen auf.
	var rechnungen []modell.Rechnung
	u.ruf("GET", "/api/rechnungen", nil, &rechnungen)
	if len(rechnungen) != 1 {
		t.Errorf("%d Rechnungen, erwartet nur der eine Entwurf", len(rechnungen))
	}
	if code := u.ruf("DELETE", "/api/belege/"+zweites.ID, nil, nil); code != 200 {
		t.Errorf("Löschen: %d", code)
	}
	if code := u.ruf("POST", "/api/belege", modell.Rechnung{Belegart: "quittung", KundeID: k.ID}, nil); code != 400 {
		t.Errorf("unbekannte Belegart: %d", code)
	}
}

// Mahnungen zu einer überfälligen Rechnung, Serienrechnung und Auswertung.
func TestMahnungSerieAuswertung(t *testing.T) {
	u := neu(t)
	k := u.einrichten()
	var e map[string]any
	u.ruf("GET", "/api/einstellungen", nil, &e)
	e["zusatz"] = modell.Zusatz{Mahnungen: true, Mahnfrist: 7, Mahngebuehr: dec.Muss("5")}
	u.ruf("PUT", "/api/einstellungen", e, &e)

	r := entwurf(k)
	alt := time.Now().AddDate(0, 0, -45).Format(tag)
	r.Datum, r.Leistungsdatum = alt, alt
	r.Zahlung.Zieltage = 14
	var gespeichert, fertig modell.Rechnung
	u.ruf("POST", "/api/rechnungen", r, &gespeichert)
	if code := u.ruf("POST", "/api/rechnungen/"+gespeichert.ID+"/fertigstellen", map[string]bool{"trotzWarnungen": true}, &fertig); code != 200 {
		t.Fatalf("fertigstellen: %d", code)
	}

	var posten []offenerPosten
	u.ruf("GET", "/api/offene-posten", nil, &posten)
	if len(posten) != 1 || !posten[0].Ueberfaell || posten[0].Tage < 30 || posten[0].Stufe != -1 || posten[0].Betrag != fertig.Summen.Zahlbetrag {
		t.Fatalf("offene Posten: %+v", posten)
	}
	var vorschlag struct {
		Stufe   int
		Titel   string
		Frist   string
		Gebuehr dec.D
		Text    string
	}
	u.ruf("GET", "/api/rechnungen/"+fertig.ID+"/mahnung", nil, &vorschlag)
	if vorschlag.Stufe != 0 || vorschlag.Titel != "Zahlungserinnerung" || !vorschlag.Gebuehr.IstNull() || !strings.Contains(vorschlag.Text, fertig.Nummer) ||
		vorschlag.Frist != time.Now().AddDate(0, 0, 7).Format(tag) {
		t.Fatalf("Vorschlag: %+v", vorschlag)
	}
	var mit modell.Rechnung
	if code := u.ruf("POST", "/api/rechnungen/"+fertig.ID+"/mahnung", map[string]any{"stufe": 0, "frist": vorschlag.Frist, "gebuehr": "0", "text": vorschlag.Text}, &mit); code != 200 || len(mit.Mahnungen) != 1 {
		t.Fatalf("Zahlungserinnerung: %d %+v", code, mit.Mahnungen)
	}
	var pdf []byte
	if code := u.ruf("GET", "/api/rechnungen/"+fertig.ID+"/mahnung/0", nil, &pdf); code != 200 || !bytes.HasPrefix(pdf, []byte("%PDF")) {
		t.Fatalf("PDF der Zahlungserinnerung: %d", code)
	}
	// Die nächste Stufe ist die Mahnung mit Gebühr.
	u.ruf("GET", "/api/rechnungen/"+fertig.ID+"/mahnung", nil, &vorschlag)
	if vorschlag.Stufe != 1 || vorschlag.Titel != "Mahnung" || vorschlag.Gebuehr != dec.Muss("5") {
		t.Errorf("zweiter Vorschlag: %+v", vorschlag)
	}
	u.ruf("POST", "/api/rechnungen/"+fertig.ID+"/mahnung", map[string]any{"stufe": 1, "frist": vorschlag.Frist, "gebuehr": "5", "text": vorschlag.Text}, &mit)
	if len(mit.Mahnungen) != 2 || mit.Mahnungen[1].Datei == mit.Mahnungen[0].Datei || !strings.Contains(mit.Mahnungen[1].Datei, "/Mahnungen/") {
		t.Errorf("Mahnungen: %+v", mit.Mahnungen)
	}
	// Versand der Mahnung per E-Mail.
	if code := u.ruf("POST", "/api/rechnungen/"+fertig.ID+"/mahnung/1/senden", map[string]string{"an": "buchhaltung@mustermarkt.example", "betreff": "Mahnung", "text": "Siehe Anhang."}, &mit); code != 200 ||
		mit.Mahnungen[1].VersendetAn != "buchhaltung@mustermarkt.example" {
		t.Errorf("Versand der Mahnung: %d %+v", code, mit.Mahnungen)
	}
	if len(u.gesendet) == 0 || len(u.gesendet[len(u.gesendet)-1].Anhang) != 1 {
		t.Errorf("die E-Mail mit der Mahnung fehlt")
	}
	// Eine bezahlte Rechnung wird nicht gemahnt und ist kein offener Posten mehr.
	u.ruf("POST", "/api/rechnungen/"+fertig.ID+"/status", map[string]string{"status": "bezahlt"}, nil)
	if code := u.ruf("POST", "/api/rechnungen/"+fertig.ID+"/mahnung", map[string]any{"stufe": 2, "frist": vorschlag.Frist, "gebuehr": "5", "text": "x"}, nil); code != 409 {
		t.Errorf("Mahnung zu bezahlter Rechnung: %d", code)
	}
	u.ruf("GET", "/api/offene-posten", nil, &posten)
	if len(posten) != 0 {
		t.Errorf("offene Posten nach der Zahlung: %+v", posten)
	}

	// Serie aus der Rechnung, fällig seit gestern.
	gestern := time.Now().AddDate(0, 0, -1)
	var serie modell.Serie
	if code := u.ruf("POST", "/api/serien", map[string]any{"name": "Monatslieferung", "abstand": "monat", "naechste": gestern.Format(tag), "aktiv": true, "ausRechnung": fertig.ID}, &serie); code != 200 || serie.ID == "" {
		t.Fatalf("Serie: %d", code)
	}
	var serien []serieMitStand
	u.ruf("GET", "/api/serien", nil, &serien)
	if len(serien) != 1 || !serien[0].Faellig || serien[0].Kunde != k.Name || serien[0].Brutto != fertig.Summen.Brutto {
		t.Fatalf("Serien: %+v", serien)
	}
	var angelegt []modell.Rechnung
	if code := u.ruf("POST", "/api/serien/anlegen", map[string]string{}, &angelegt); code != 200 || len(angelegt) != 1 {
		t.Fatalf("Serienrechnung anlegen: %d %d", code, len(angelegt))
	}
	if a := angelegt[0]; a.Status != modell.StatusEntwurf || a.Datum != gestern.Format(tag) || a.KundeID != k.ID || a.Nummer != "" || len(a.Positionen) != len(fertig.Positionen) {
		t.Errorf("Entwurf aus der Serie: %+v", a)
	}
	u.ruf("GET", "/api/serien", nil, &serien)
	if serien[0].Faellig || serien[0].Naechste != weiter(gestern, "monat").Format(tag) || serien[0].Letzte != gestern.Format(tag) {
		t.Errorf("Serie nach dem Anlegen: %+v", serien[0].Serie)
	}
	u.ruf("POST", "/api/serien/anlegen", map[string]string{}, &angelegt)
	if len(angelegt) != 0 {
		t.Errorf("nicht fällige Serie hat eine Rechnung angelegt")
	}

	// Auswertung des Jahres der Rechnung.
	var aus struct {
		Summe  auswertungMonat
		Monate []auswertungMonat
		Kunden []auswertungKunde
	}
	u.ruf("GET", "/api/auswertung?jahr="+alt[:4], nil, &aus)
	if aus.Summe.Anzahl != 1 || aus.Summe.Brutto != fertig.Summen.Brutto || aus.Summe.Netto7+aus.Summe.Netto19+aus.Summe.NettoOhne != fertig.Summen.Netto ||
		aus.Summe.Steuer7+aus.Summe.Steuer19+aus.Summe.SteuerAnd != fertig.Summen.Steuer || len(aus.Monate) != 12 || len(aus.Kunden) != 1 || aus.Kunden[0].Kunde != k.Name {
		t.Errorf("Auswertung: %+v %+v", aus.Summe, aus.Kunden)
	}
}

func TestSerienabstand(t *testing.T) {
	d := func(s string) time.Time { x, _ := time.Parse(tag, s); return x }
	for _, f := range []struct{ von, abstand, nach string }{
		{"2026-01-31", "monat", "2026-02-28"}, {"2026-11-15", "monat", "2026-12-15"}, {"2026-12-15", "monat", "2027-01-15"},
		{"2026-11-30", "quartal", "2027-02-28"}, {"2026-11-13", "woche", "2026-11-20"}, {"2024-02-29", "jahr", "2025-02-28"},
		{"2026-08-31", "halbjahr", "2027-02-28"}, {"2026-05-10", "unbekannt", "2026-06-10"},
	} {
		if aus := weiter(d(f.von), f.abstand).Format(tag); aus != f.nach {
			t.Errorf("%s + %s = %s, erwartet %s", f.von, f.abstand, aus, f.nach)
		}
	}
}

// Abschlagsrechnung und Schlussrechnung, die den Abschlag wieder abzieht.
func TestAbschlagUndSchlussrechnung(t *testing.T) {
	u := neu(t)
	k := u.einrichten()
	heute := time.Now().Format(tag)
	fertigstellen := func(r modell.Rechnung) modell.Rechnung {
		t.Helper()
		r.Datum, r.Leistungsdatum = heute, heute
		var gespeichert, fertig modell.Rechnung
		u.ruf("POST", "/api/rechnungen", r, &gespeichert)
		var geprueft pruefErgebnis
		u.ruf("POST", "/api/pruefen", gespeichert, &geprueft)
		for _, m := range geprueft.Meldungen {
			if m.Schwere == modell.Fehler {
				t.Errorf("Fehler in der Prüfung: %s %s", m.Regel, m.Text)
			}
		}
		if code := u.ruf("POST", "/api/rechnungen/"+gespeichert.ID+"/fertigstellen", map[string]bool{"trotzWarnungen": true}, &fertig); code != 200 {
			t.Fatalf("fertigstellen: %d", code)
		}
		return fertig
	}
	abschlag := entwurf(k)
	abschlag.Art = modell.ArtAbschlag
	abschlag.Positionen = abschlag.Positionen[:1]
	abschlag = fertigstellen(abschlag)
	if abschlag.Art != modell.ArtAbschlag || len(abschlag.Summen.Steuern) != 1 {
		t.Fatalf("Abschlagsrechnung: Art %q, Steuern %+v", abschlag.Art, abschlag.Summen.Steuern)
	}
	var offen []offenerAbschlag
	u.ruf("GET", "/api/abschlaege?kunde="+k.ID, nil, &offen)
	if len(offen) != 1 || offen[0].Nummer != abschlag.Nummer || offen[0].Brutto != abschlag.Summen.Brutto {
		t.Fatalf("offene Abschläge: %+v", offen)
	}

	// Schlussrechnung: alle Positionen, der Abschlag mit der Menge -1 abgezogen.
	schluss := entwurf(k)
	st := abschlag.Summen.Steuern[0]
	abzug := schluss.Positionen[0]
	abzug.Bezeichnung, abzug.Menge, abzug.Einheit, abzug.Einzelpreis = "abzüglich Abschlagsrechnung "+abschlag.Nummer, dec.Muss("-1"), "C62", st.Basis
	abzug.SteuerSatz, abzug.RabattProzent, abzug.ZuAbschlaege = st.Satz, 0, nil
	ganz := entwurf(k)
	ganzFertig := rechnenBrutto(t, u, ganz)
	schluss.Positionen = append(schluss.Positionen, abzug)
	schluss.Vorgaenger = []modell.Vorgaenger{{Nummer: abschlag.Nummer, Datum: abschlag.Datum}}
	schluss = fertigstellen(schluss)
	if schluss.Summen.Brutto != ganzFertig-abschlag.Summen.Brutto {
		t.Errorf("Schlussrechnung: brutto %s, erwartet %s - %s", schluss.Summen.Brutto, ganzFertig, abschlag.Summen.Brutto)
	}
	// Der Abschlag gilt als abgezogen und wird nicht noch einmal angeboten.
	u.ruf("GET", "/api/abschlaege?kunde="+k.ID, nil, &offen)
	var nachher modell.Rechnung
	u.ruf("GET", "/api/rechnungen/"+abschlag.ID, nil, &nachher)
	if len(offen) != 0 || nachher.Folgebeleg != "abgezogen in Rechnung "+schluss.Nummer {
		t.Errorf("nach der Schlussrechnung: offen %+v, Vermerk %q", offen, nachher.Folgebeleg)
	}
}

// rechnenBrutto liefert den Bruttobetrag, den ein Entwurf ergäbe.
func rechnenBrutto(t *testing.T, u *umgebung, r modell.Rechnung) dec.D {
	t.Helper()
	var geprueft pruefErgebnis
	r.Datum, r.Leistungsdatum = time.Now().Format(tag), time.Now().Format(tag)
	u.ruf("POST", "/api/pruefen", r, &geprueft)
	if geprueft.Rechnung.Summen == nil {
		t.Fatal("keine Summen")
	}
	return geprueft.Rechnung.Summen.Brutto
}
