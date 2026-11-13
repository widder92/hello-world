package server

import (
	"encoding/json"
	"io"
	"io/fs"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"

	"marktrechnung/internal/geheim"
	"marktrechnung/web"
)

// claudeAttrappe stellt die Claude-Schnittstelle nach: Sie nimmt nur den
// Schlüssel "sk-ant-richtig" an und merkt sich die letzte Frage.
type claudeAttrappe struct {
	roh     string
	anfrage map[string]any
	fragen  int
}

func (c *claudeAttrappe) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	if r.Header.Get("Authorization") != "Bearer sk-ant-richtig" {
		w.WriteHeader(401)
		io.WriteString(w, `{"type":"error","error":{"type":"authentication_error","message":"invalid x-api-key"}}`)
		return
	}
	if strings.HasPrefix(r.URL.Path, "/v1/models/") {
		io.WriteString(w, `{"id":"claude-opus-5-5","type":"model","display_name":"Claude Opus 5.5","created_at":"2026-09-01T00:00:00Z"}`)
		return
	}
	daten, _ := io.ReadAll(r.Body)
	c.roh, c.anfrage = string(daten), nil
	json.Unmarshal(daten, &c.anfrage)
	c.fragen++
	io.WriteString(w, `{"id":"msg_01","type":"message","role":"assistant","model":"claude-opus-5-5","content":[{"type":"text","text":"1. Links auf „Kunden“ klicken. [[#/kunden|Kunden öffnen]]"}],"stop_reason":"end_turn","stop_sequence":null,"usage":{"input_tokens":5000,"output_tokens":30}}`)
}

func TestAssistent(t *testing.T) {
	claude := &claudeAttrappe{}
	ki := httptest.NewServer(claude)
	defer ki.Close()
	u := neu(t)
	u.server.Web, u.server.KI, u.server.KIBasis = web.Dateien(), geheim.Neu(t.TempDir(), "k"), ki.URL

	type stand struct {
		KI          bool   `json:"ki"`
		Modell      string `json:"modell"`
		Einrichtung []struct {
			ID       string `json:"id"`
			Titel    string `json:"titel"`
			Erledigt bool   `json:"erledigt"`
			Ziel     string `json:"ziel"`
			Kapitel  string `json:"kapitel"`
		} `json:"einrichtung"`
		Antwort string `json:"antwort"`
		Fehler  string `json:"fehler"`
	}
	var s stand
	if code := u.ruf("GET", "/api/assistent", nil, &s); code != 200 || s.KI || len(s.Einrichtung) != 7 {
		t.Fatalf("Stand: %d %+v", code, s)
	}
	// Jeder Schritt verweist auf ein Kapitel, das es in der Anleitung gibt.
	kapitel := map[string]bool{}
	for _, k := range u.server.anleitung().Kapitel {
		kapitel[k.ID] = true
	}
	if len(kapitel) < 10 {
		t.Fatalf("Anleitung nicht gelesen: %d Kapitel", len(kapitel))
	}
	for _, p := range s.Einrichtung {
		if p.Erledigt || !kapitel[p.Kapitel] || !strings.HasPrefix(p.Ziel, "#/") {
			t.Errorf("Schritt %+v", p)
		}
	}

	// Ohne Schlüssel geht keine Frage hinaus.
	if code := u.ruf("POST", "/api/assistent/frage", map[string]any{"frage": "Wie lege ich einen Kunden an?"}, &s); code != 422 || !strings.Contains(s.Fehler, "nicht eingeschaltet") {
		t.Errorf("Frage ohne Schlüssel: %d %q", code, s.Fehler)
	}
	// Ein falscher Schlüssel wird nicht gespeichert.
	if code := u.ruf("POST", "/api/assistent/schluessel", map[string]string{"schluessel": "sk-ant-falsch"}, &s); code != 422 || !strings.Contains(s.Fehler, "Zugangsschlüssel wird nicht angenommen") {
		t.Errorf("falscher Schlüssel: %d %q", code, s.Fehler)
	}
	if u.server.KI.Vorhanden() {
		t.Fatal("falscher Schlüssel gespeichert")
	}
	if code := u.ruf("POST", "/api/assistent/schluessel", map[string]string{"schluessel": "  sk-ant-richtig "}, &s); code != 200 || !s.KI {
		t.Fatalf("richtiger Schlüssel: %d %+v", code, s)
	}

	// Nach dem Einrichten sind die ersten Schritte erledigt.
	k := u.einrichten()
	s = stand{}
	u.ruf("GET", "/api/assistent", nil, &s)
	erledigt := map[string]bool{}
	for _, p := range s.Einrichtung {
		erledigt[p.ID] = p.Erledigt
	}
	if !s.KI || !erledigt["firma"] || !erledigt["bank"] || !erledigt["email"] || !erledigt["kunden"] || erledigt["artikel"] || erledigt["rechnung"] || erledigt["senden"] {
		t.Errorf("Einrichtung: %+v", erledigt)
	}

	// Die Frage geht mit Anleitung und Lage hinaus - ohne Kunden- oder Firmendaten.
	var verlauf []map[string]string
	for i := 0; i < 10; i++ {
		verlauf = append(verlauf, map[string]string{"rolle": "frage", "text": "alte Frage"}, map[string]string{"rolle": "antwort", "text": "alte Antwort"})
	}
	s = stand{}
	if code := u.ruf("POST", "/api/assistent/frage", map[string]any{"frage": "Wie lege ich einen Markt an?", "seite": "kunden", "verlauf": verlauf}, &s); code != 200 {
		t.Fatalf("Frage: %d %q", code, s.Fehler)
	}
	if !strings.Contains(s.Antwort, "[[#/kunden|Kunden öffnen]]") {
		t.Errorf("Antwort: %q", s.Antwort)
	}
	system, _ := claude.anfrage["system"].([]any)
	if len(system) != 2 {
		t.Fatalf("system: %v", claude.anfrage["system"])
	}
	auftrag, lage := system[0].(map[string]any)["text"].(string), system[1].(map[string]any)["text"].(string)
	for _, muss := range []string{"Kapitel 4: Kunden und Märkte anlegen", "Seite im Programm: #/kunden", "BR-DE-15", "Vorgaben der Handelsunternehmen", "## REWE"} {
		if !strings.Contains(auftrag, muss) {
			t.Errorf("im Auftrag fehlt %q", muss)
		}
	}
	for _, muss := range []string{"Geöffnete Seite: Kunden", "Eigene Firmendaten eintragen: erledigt", "Artikel anlegen: noch offen", "Zusatzfunktionen: keine eingeschaltet", "Programmversion: test"} {
		if !strings.Contains(lage, muss) {
			t.Errorf("in der Lage fehlt %q:\n%s", muss, lage)
		}
	}
	e, _ := u.server.Speicher.Einstellungen()
	for _, darfNicht := range []string{k.Name, k.GLN, k.Lieferorte[0].Name, k.Lieferorte[0].GLN, k.RechnungsEmail, e.Firma.Name, e.Firma.Strasse, e.IBAN, e.Post.Absender, "streng geheim"} {
		if darfNicht != "" && strings.Contains(claude.roh, darfNicht) {
			t.Errorf("an die Schnittstelle ging %q", darfNicht)
		}
	}
	// Vom Gespräch geht nur das jüngste Stück mit: 12 Beiträge und die neue Frage.
	if n := len(claude.anfrage["messages"].([]any)); n != 13 {
		t.Errorf("%d Nachrichten", n)
	}

	// Zu lange Fragen und das Abschalten.
	vorher := claude.fragen
	if code := u.ruf("POST", "/api/assistent/frage", map[string]any{"frage": strings.Repeat("ä", 4001)}, &s); code != 422 || claude.fragen != vorher {
		t.Errorf("zu lange Frage: %d", code)
	}
	s = stand{}
	if code := u.ruf("POST", "/api/assistent/schluessel", map[string]string{"schluessel": ""}, &s); code != 200 || s.KI || u.server.KI.Vorhanden() {
		t.Errorf("Abschalten: %d %+v", code, s)
	}
}

// Die Anleitung muss zu dem passen, was das Programm kennt: Jede genannte
// Seite gibt es, jedes Ziel führt auf eine Seite, kein Kapitel ist leer.
func TestAnleitungStimmig(t *testing.T) {
	daten, err := fs.ReadFile(web.Dateien(), "anleitung.json")
	if err != nil {
		t.Fatal(err)
	}
	var a struct {
		Titel, Vorwort string
		Kapitel        []struct {
			ID, Titel, Kurz, Ziel, Bild string
			Seiten, Stichworte          []string
			Schritte, Hinweise          []string
		}
		Meldungen []struct{ Regel, Text string }
		Begriffe  []struct {
			Wort, Text string
			Auch       []string
		}
	}
	if err := json.Unmarshal(daten, &a); err != nil {
		t.Fatal(err)
	}
	if a.Titel == "" || a.Vorwort == "" || len(a.Kapitel) < 15 || len(a.Meldungen) < 5 || len(a.Begriffe) < 10 {
		t.Fatalf("Anleitung unvollständig: %d Kapitel, %d Meldungen, %d Begriffe", len(a.Kapitel), len(a.Meldungen), len(a.Begriffe))
	}
	ziele := map[string]bool{"": true, "#/": true, "#/rechnung/neu": true, "#/kunden": true, "#/artikel": true, "#/pruefen": true, "#/einstellungen": true, "#/hilfe": true}
	gesehen := map[string]bool{}
	for _, k := range a.Kapitel {
		if k.ID == "" || gesehen[k.ID] {
			t.Errorf("Kapitel %q: Kennung fehlt oder ist doppelt", k.Titel)
		}
		gesehen[k.ID] = true
		if k.Titel == "" || k.Kurz == "" || len(k.Schritte) < 3 || len(k.Stichworte) < 5 {
			t.Errorf("Kapitel %s ist zu dünn: %d Schritte, %d Stichworte", k.ID, len(k.Schritte), len(k.Stichworte))
		}
		if !ziele[k.Ziel] {
			t.Errorf("Kapitel %s führt auf die unbekannte Seite %q", k.ID, k.Ziel)
		}
		for _, seite := range k.Seiten {
			if _, ok := seitenname[seite]; !ok {
				t.Errorf("Kapitel %s nennt die unbekannte Seite %q", k.ID, seite)
			}
		}
		for _, text := range append(append([]string{}, k.Schritte...), k.Hinweise...) {
			if strings.TrimSpace(text) == "" || strings.Contains(text, "  ") {
				t.Errorf("Kapitel %s: leerer oder unsauberer Text %q", k.ID, text)
			}
		}
	}
	for _, b := range a.Begriffe {
		if b.Wort == "" || b.Text == "" || len(b.Auch) == 0 {
			t.Errorf("Begriff %+v unvollständig", b)
		}
	}
}
