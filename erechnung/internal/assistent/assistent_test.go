package assistent

import (
	"context"
	"encoding/json"
	"io"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
)

// attrappe stellt die Claude-Schnittstelle nach und merkt sich, was ankam.
type attrappe struct {
	t        *testing.T
	anfrage  map[string]any
	kopf     http.Header
	pfad     string
	status   int
	antwort  string
	aufrufe  int
	schluess string
}

func (a *attrappe) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	a.aufrufe++
	a.kopf, a.pfad, a.schluess = r.Header.Clone(), r.URL.Path, r.Header.Get("Authorization")
	daten, _ := io.ReadAll(r.Body)
	if len(daten) > 0 {
		a.anfrage = nil
		if err := json.Unmarshal(daten, &a.anfrage); err != nil {
			a.t.Errorf("Anfrage ist kein JSON: %v", err)
		}
	}
	w.Header().Set("Content-Type", "application/json")
	if a.status != 0 {
		w.WriteHeader(a.status)
	}
	io.WriteString(w, a.antwort)
}

func nachricht(text, ende string) string {
	inhalt, _ := json.Marshal(text)
	return `{"id":"msg_01","type":"message","role":"assistant","model":"claude-opus-5-5","content":[{"type":"text","text":` + string(inhalt) +
		`}],"stop_reason":"` + ende + `","stop_sequence":null,"usage":{"input_tokens":1200,"output_tokens":40}}`
}

func TestAntworte(t *testing.T) {
	a := &attrappe{t: t, antwort: nachricht("1. Links auf „Kunden“ klicken.\n2. Auf „+ Neuer Kunde“ klicken. [[#/kunden|Kunden öffnen]]", "end_turn")}
	srv := httptest.NewServer(a)
	defer srv.Close()
	anleitung := Anleitung{Kapitel: []Kapitel{{ID: "kunden", Titel: "Kunden und Märkte anlegen", Kurz: "Wer die Rechnung bekommt.", Ziel: "#/kunden",
		Schritte: []string{"Links auf „Kunden“ klicken."}, Hinweise: []string{"Viele Märkte auf einmal: aus Datei einlesen."}}}}
	d := Dienst{Schluessel: "sk-ant-test", Basis: srv.URL, Anleitung: anleitung}

	antwort, err := d.Antworte(context.Background(), Anfrage{
		Frage:   "  Wie lege ich einen Kunden an?  ",
		Verlauf: []Nachricht{{Rolle: "antwort", Text: "verwaist"}, {Rolle: "frage", Text: "Hallo"}, {Rolle: "antwort", Text: "Guten Tag."}, {Rolle: "frage", Text: " "}},
		Lage:    "Geöffnete Seite: Kunden",
	})
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(antwort, "+ Neuer Kunde") || !strings.Contains(antwort, "[[#/kunden|Kunden öffnen]]") {
		t.Errorf("Antwort: %q", antwort)
	}
	if a.pfad != "/v1/messages" || a.schluess != "Bearer sk-ant-test" || !strings.Contains(a.kopf.Get("Anthropic-Beta"), "server-side-fallback-2026-07-01") {
		t.Errorf("Aufruf: %s, Schlüssel %q, Beta %q", a.pfad, a.schluess, a.kopf.Get("Anthropic-Beta"))
	}
	q := a.anfrage
	if q["model"] != Modell || q["fallbacks"] != "default" || q["max_tokens"] != float64(16000) {
		t.Errorf("Modell %v, Ersatz %v, Länge %v", q["model"], q["fallbacks"], q["max_tokens"])
	}
	if aus, _ := q["output_config"].(map[string]any); aus["effort"] != "low" {
		t.Errorf("output_config: %v", q["output_config"])
	}
	if _, da := q["thinking"]; da {
		t.Errorf("thinking soll nicht gesetzt sein: %v", q["thinking"])
	}
	// Auftrag mit Anleitung vorn und zwischengespeichert, die Lage dahinter.
	system, _ := q["system"].([]any)
	if len(system) != 2 {
		t.Fatalf("system: %v", q["system"])
	}
	erster, zweiter := system[0].(map[string]any), system[1].(map[string]any)
	text, _ := erster["text"].(string)
	if !strings.Contains(text, "## Kapitel 1: Kunden und Märkte anlegen") || !strings.Contains(text, "Seite im Programm: #/kunden") ||
		!strings.Contains(text, "Hinweis: Viele Märkte") || erster["cache_control"] == nil {
		t.Errorf("Auftrag: %.300q, cache_control %v", text, erster["cache_control"])
	}
	if zweiter["cache_control"] != nil || !strings.Contains(zweiter["text"].(string), "Geöffnete Seite: Kunden") {
		t.Errorf("Lage: %v", zweiter)
	}
	// Das Gespräch beginnt mit einer Frage und wechselt sich ab.
	nachrichten, _ := q["messages"].([]any)
	var rollen []string
	for _, n := range nachrichten {
		rollen = append(rollen, n.(map[string]any)["role"].(string))
	}
	if strings.Join(rollen, ",") != "user,assistant,user" {
		t.Errorf("Rollen: %v", rollen)
	}
	letzte := nachrichten[2].(map[string]any)["content"].([]any)[0].(map[string]any)
	if letzte["text"] != "Wie lege ich einen Kunden an?" {
		t.Errorf("Frage: %v", letzte)
	}

	// Ablehnung aus Sicherheitsgründen und abgeschnittene Antwort.
	a.antwort = nachricht("", "refusal")
	if _, err := d.Antworte(context.Background(), Anfrage{Frage: "x"}); err != ErrAbgelehnt {
		t.Errorf("Ablehnung: %v", err)
	}
	a.antwort = nachricht("Ein langer Text", "max_tokens")
	if antwort, err := d.Antworte(context.Background(), Anfrage{Frage: "x"}); err != nil || !strings.Contains(antwort, "abgeschnitten") {
		t.Errorf("abgeschnitten: %q %v", antwort, err)
	}
	// Ohne Schlüssel oder ohne Frage geht nichts hinaus.
	vorher := a.aufrufe
	if _, err := (Dienst{Basis: srv.URL}).Antworte(context.Background(), Anfrage{Frage: "x"}); err == nil {
		t.Error("ohne Schlüssel beantwortet")
	}
	if _, err := d.Antworte(context.Background(), Anfrage{Frage: "  "}); err == nil {
		t.Error("leere Frage beantwortet")
	}
	if a.aufrufe != vorher {
		t.Error("trotzdem wurde die Schnittstelle gerufen")
	}
}

func TestFehlerUndSchluesselpruefung(t *testing.T) {
	a := &attrappe{t: t, status: 401, antwort: `{"type":"error","error":{"type":"authentication_error","message":"invalid x-api-key"}}`}
	srv := httptest.NewServer(a)
	defer srv.Close()
	d := Dienst{Schluessel: "falsch", Basis: srv.URL}
	if err := d.PruefeSchluessel(context.Background()); err == nil || !strings.Contains(err.Error(), "Zugangsschlüssel wird nicht angenommen") {
		t.Errorf("falscher Schlüssel: %v", err)
	}
	if a.pfad != "/v1/models/"+Modell {
		t.Errorf("Schlüsselprüfung ruft %s", a.pfad)
	}
	if _, err := d.Antworte(context.Background(), Anfrage{Frage: "x"}); err == nil || !strings.Contains(err.Error(), "Zugangsschlüssel") {
		t.Errorf("Antwort mit falschem Schlüssel: %v", err)
	}
	a.status, a.antwort = 429, `{"type":"error","error":{"type":"rate_limit_error","message":"slow down"}}`
	if _, err := d.Antworte(context.Background(), Anfrage{Frage: "x"}); err == nil || !strings.Contains(err.Error(), "zu viele Anfragen") {
		t.Errorf("zu viele Anfragen: %v", err)
	}
	a.status, a.antwort = 0, `{"id":"claude-opus-5-5","type":"model","display_name":"Claude Opus 5.5","created_at":"2026-09-01T00:00:00Z"}`
	if err := d.PruefeSchluessel(context.Background()); err != nil {
		t.Errorf("gültiger Schlüssel: %v", err)
	}
	srv.Close()
	if err := d.PruefeSchluessel(context.Background()); err == nil || !strings.Contains(err.Error(), "keine Verbindung") {
		t.Errorf("ohne Verbindung: %v", err)
	}
}
