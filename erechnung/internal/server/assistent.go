package server

import (
	"errors"
	"fmt"
	"io/fs"
	"net/http"
	"strings"

	"marktrechnung/internal/assistent"
	"marktrechnung/internal/modell"
	"marktrechnung/internal/partner"
)

// Der Assistent: eine eingebaute Anleitung, die ohne Internet auskommt, und
// - nur wenn ein eigener Zugangsschlüssel hinterlegt ist - Antworten von
// Claude. An Claude gehen die Frage, die Anleitung und der Stand der
// Einrichtung; Kunden, Artikel und Rechnungen bleiben auf dem Rechner.

// anleitung liest die Anleitung aus den Dateien der Oberfläche.
func (s *Server) anleitung() assistent.Anleitung {
	s.anleitungEinmal.Do(func() {
		if daten, err := fs.ReadFile(s.Web, "anleitung.json"); err == nil {
			s.anleitungInhalt, _ = assistent.LiesAnleitung(daten)
		}
	})
	return s.anleitungInhalt
}

// handelswissen fasst zusammen, was die Handelsunternehmen verlangen - dieselben
// Angaben, die auf der Seite "Hilfe" stehen.
func handelswissen() string {
	var b strings.Builder
	b.WriteString("# Vorgaben der Handelsunternehmen (stehen im Programm auf der Seite „Hilfe“)\n\n")
	for _, p := range partner.Profile() {
		if len(p.Hinweise) == 0 {
			continue
		}
		fmt.Fprintf(&b, "## %s\n", p.Name)
		for _, h := range p.Hinweise {
			fmt.Fprintf(&b, "- %s\n", h)
		}
		b.WriteString("\n")
	}
	return b.String()
}

// einrichtSchritt ist ein Punkt auf dem Weg zur ersten verschickten Rechnung.
type einrichtSchritt struct {
	ID       string `json:"id"`
	Titel    string `json:"titel"`
	Text     string `json:"text"`
	Erledigt bool   `json:"erledigt"`
	Ziel     string `json:"ziel"`    // Seite, auf der es zu erledigen ist
	Kapitel  string `json:"kapitel"` // Kapitel der Anleitung dazu
}

// einrichtung stellt fest, wie weit das Programm eingerichtet ist.
func (s *Server) einrichtung() []einrichtSchritt {
	e, _ := s.Speicher.Einstellungen()
	kunden, _ := s.Speicher.Kunden()
	artikel, _ := s.Speicher.Artikel()
	rechnungen, _ := s.Speicher.Rechnungen()
	f := e.Firma
	firma := strings.TrimSpace(f.Name) != "" && strings.TrimSpace(f.Strasse) != "" && strings.TrimSpace(f.PLZ) != "" && strings.TrimSpace(f.Ort) != "" &&
		(strings.TrimSpace(f.UStID) != "" || strings.TrimSpace(f.Steuernummer) != "")
	var maerkte, fertig, versendet int
	for _, k := range kunden {
		maerkte += len(k.Lieferorte)
	}
	for _, r := range rechnungen {
		if r.Status != modell.StatusEntwurf {
			fertig++
		}
		if r.Status == modell.StatusVersendet || len(r.Versand) > 0 {
			versendet++
		}
	}
	zahl := func(n int, eins, viele string) string {
		if n == 1 {
			return "1 " + eins
		}
		return fmt.Sprintf("%d %s", n, viele)
	}
	kundenText := "Wer die Rechnung bekommt, mit GLN, und die belieferten Märkte."
	if len(kunden) > 0 {
		kundenText = "Angelegt: " + zahl(len(kunden), "Kunde", "Kunden") + " mit " + zahl(maerkte, "Markt", "Märkten") + "."
	}
	artikelText := "Waren mit Preis, Steuersatz und GTIN."
	if len(artikel) > 0 {
		artikelText = "Angelegt: " + zahl(len(artikel), "Artikel", "Artikel") + "."
	}
	return []einrichtSchritt{
		{"firma", "Eigene Firmendaten eintragen", "Name, Anschrift, Steuernummer oder USt-IdNr. – das steht auf jeder Rechnung.", firma, "#/einstellungen", "firma"},
		{"bank", "Bankverbindung eintragen", "Die IBAN, auf die der Kunde zahlt.", strings.TrimSpace(e.IBAN) != "", "#/einstellungen", "firma"},
		{"email", "E-Mail-Versand einrichten", "Damit Rechnungen direkt aus dem Programm verschickt werden.", strings.TrimSpace(e.Post.Server) != "" && s.Passwort.Vorhanden(), "#/einstellungen", "email"},
		{"kunden", "Kunden und Märkte anlegen", kundenText, len(kunden) > 0, "#/kunden", "kunden"},
		{"artikel", "Artikel anlegen", artikelText, len(artikel) > 0, "#/artikel", "artikel"},
		{"rechnung", "Erste Rechnung schreiben und fertigstellen", "Das Programm prüft sie und sagt, was fehlt.", fertig > 0, "#/rechnung/neu", "rechnung"},
		{"senden", "Erste Rechnung versenden", "Per E-Mail an das Rechnungspostfach des Kunden.", versendet > 0, "#/", "senden"},
	}
}

// seitenname nennt die Seite so, wie sie im Menü heißt.
var seitenname = map[string]string{
	"": "Rechnungen (Übersicht)", "rechnung": "eine geöffnete Rechnung", "kunden": "Kunden", "artikel": "Artikel",
	"pruefen": "Import aus Lexoffice", "einstellungen": "Einstellungen", "hilfe": "Hilfe", "anleitung": "Anleitung",
	"angebote": "Angebote", "lieferscheine": "Lieferscheine", "serien": "Serienrechnungen", "offen": "Offene Posten", "auswertung": "Auswertung",
}

// lage beschreibt für das Modell, wo die Fragende gerade steht. Darin stehen
// nur Zustände und Zahlen, keine Namen, Anschriften oder Beträge.
func (s *Server) lage(seite string) string {
	var b strings.Builder
	if name, ok := seitenname[seite]; ok {
		fmt.Fprintf(&b, "Geöffnete Seite: %s\n", name)
	}
	b.WriteString("Stand der Einrichtung:\n")
	for _, p := range s.einrichtung() {
		stand := "noch offen"
		if p.Erledigt {
			stand = "erledigt"
		}
		fmt.Fprintf(&b, "- %s: %s\n", p.Titel, stand)
	}
	e, _ := s.Speicher.Einstellungen()
	var an []string
	for name, ein := range map[string]bool{"Angebote": e.Zusatz.Angebote, "Lieferscheine": e.Zusatz.Lieferscheine, "Serienrechnungen": e.Zusatz.Serien,
		"Abschlagsrechnungen": e.Zusatz.Abschlaege, "Offene Posten und Mahnungen": e.Zusatz.Mahnungen, "Auswertung": e.Zusatz.Auswertung} {
		if ein {
			an = append(an, name)
		}
	}
	if len(an) == 0 {
		b.WriteString("Zusatzfunktionen: keine eingeschaltet\n")
	} else {
		sortiere(an)
		fmt.Fprintf(&b, "Eingeschaltete Zusatzfunktionen: %s\n", strings.Join(an, ", "))
	}
	if e.Kleinunternehmer {
		b.WriteString("Der Betrieb ist als Kleinunternehmer eingetragen.\n")
	}
	fmt.Fprintf(&b, "Programmversion: %s\n", s.Version)
	return b.String()
}

func sortiere(l []string) {
	for i := 1; i < len(l); i++ {
		for j := i; j > 0 && l[j] < l[j-1]; j-- {
			l[j], l[j-1] = l[j-1], l[j]
		}
	}
}

func (s *Server) kiDienst(schluessel string) assistent.Dienst {
	return assistent.Dienst{Schluessel: schluessel, Basis: s.KIBasis, Anleitung: s.anleitung(), Wissen: handelswissen()}
}

// assistentStand: Ist die KI eingeschaltet, und wie weit ist die Einrichtung?
func (s *Server) assistentStand(w http.ResponseWriter, r *http.Request) {
	antworte(w, 200, map[string]any{
		"ki":          s.KI != nil && s.KI.Vorhanden(),
		"modell":      "Claude Opus 5.5",
		"einrichtung": s.einrichtung(),
	})
}

// assistentSchluessel speichert den Zugangsschlüssel für Claude, nachdem die
// Schnittstelle ihn angenommen hat. Ein leerer Schlüssel schaltet die KI ab.
func (s *Server) assistentSchluessel(w http.ResponseWriter, r *http.Request) {
	var ein struct {
		Schluessel string `json:"schluessel"`
	}
	if err := liesJSON(r, &ein); err != nil {
		fehler(w, 400, err)
		return
	}
	if s.KI == nil {
		fehler(w, http.StatusNotImplemented, errors.New("der KI-Assistent steht in diesem Programm nicht zur Verfügung"))
		return
	}
	ein.Schluessel = strings.TrimSpace(ein.Schluessel)
	if ein.Schluessel == "" {
		if err := s.KI.Setze(""); err != nil {
			fehler(w, 500, err)
			return
		}
		antworte(w, 200, map[string]any{"ki": false})
		return
	}
	if err := s.kiDienst(ein.Schluessel).PruefeSchluessel(r.Context()); err != nil {
		fehler(w, 422, err)
		return
	}
	if err := s.KI.Setze(ein.Schluessel); err != nil {
		fehler(w, 500, err)
		return
	}
	antworte(w, 200, map[string]any{"ki": true})
}

// assistentFrage reicht eine Frage an Claude weiter.
func (s *Server) assistentFrage(w http.ResponseWriter, r *http.Request) {
	var ein struct {
		Frage   string                `json:"frage"`
		Seite   string                `json:"seite"`
		Verlauf []assistent.Nachricht `json:"verlauf"`
	}
	if err := liesJSON(r, &ein); err != nil {
		fehler(w, 400, err)
		return
	}
	if s.KI == nil || !s.KI.Vorhanden() {
		fehler(w, 422, errors.New("der KI-Assistent ist nicht eingeschaltet. Unter Einstellungen lässt sich ein Zugangsschlüssel hinterlegen"))
		return
	}
	schluessel, err := s.KI.Lies()
	if err != nil {
		fehler(w, 500, err)
		return
	}
	if len([]rune(ein.Frage)) > 4000 {
		fehler(w, 422, errors.New("die Frage ist zu lang. Bitte kürzer fassen"))
		return
	}
	// Vom Gespräch geht nur das jüngste Stück mit - das hält die Kosten klein.
	if n := len(ein.Verlauf); n > 12 {
		ein.Verlauf = ein.Verlauf[n-12:]
	}
	antwort, err := s.kiDienst(schluessel).Antworte(r.Context(), assistent.Anfrage{Frage: ein.Frage, Verlauf: ein.Verlauf, Lage: s.lage(ein.Seite)})
	if err != nil {
		status := http.StatusBadGateway
		if errors.Is(err, assistent.ErrAbgelehnt) {
			status = 422
		}
		fehler(w, status, err)
		return
	}
	antworte(w, 200, map[string]any{"antwort": antwort})
}

// anleitungOeffnen zeigt die bebilderte Anleitung im PDF-Programm des Rechners.
func (s *Server) anleitungOeffnen(w http.ResponseWriter, r *http.Request) {
	if s.AnleitungPDF == "" || s.Oeffne == nil {
		fehler(w, http.StatusNotFound, errors.New("die Anleitung als PDF liegt diesem Programm nicht bei. Sie steht im Programm unter „Anleitung“"))
		return
	}
	if err := s.Oeffne(s.AnleitungPDF); err != nil {
		fehler(w, 500, fmt.Errorf("die Anleitung %s lässt sich nicht öffnen: %w", s.AnleitungPDF, err))
		return
	}
	antworte(w, 200, map[string]bool{"ok": true})
}
