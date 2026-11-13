// Package server ist die Schaltstelle des Programms: Es stellt die
// Bedienoberfläche für das Programmfenster bereit und nimmt deren Aufträge entgegen. Der
// Server hört nur auf dem eigenen Rechner (127.0.0.1) und verlangt ein
// Kennwort-Cookie, das beim Start des Programms gesetzt wird.
package server

import (
	"bytes"
	"crypto/subtle"
	"encoding/csv"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"io/fs"
	"net"
	"net/http"
	"path/filepath"
	"strings"
	"time"

	"marktrechnung/internal/beleg"
	"marktrechnung/internal/cii"
	"marktrechnung/internal/codes"
	"marktrechnung/internal/dec"
	"marktrechnung/internal/geheim"
	"marktrechnung/internal/modell"
	"marktrechnung/internal/partner"
	"marktrechnung/internal/post"
	"marktrechnung/internal/pruefung"
	"marktrechnung/internal/rechnen"
	"marktrechnung/internal/speicher"
	"marktrechnung/internal/zugferd"
)

// Server hält alles zusammen, was die Oberfläche braucht.
type Server struct {
	Speicher *speicher.Speicher
	Passwort *geheim.Ablage
	// Lexware bewahrt den Zugangsschlüssel für Lexware Office auf.
	Lexware *geheim.Ablage
	// LexwareBasis ersetzt in Tests die Adresse der Lexware-Schnittstelle.
	LexwareBasis string
	Web          fs.FS  // Dateien der Oberfläche
	Token        string // Zugangskennwort für diesen Rechner
	Version      string
	// Oeffne öffnet einen Ordner im Dateimanager des Betriebssystems.
	Oeffne func(pfad string) error
	// Beende fährt das Programm herunter.
	Beende func()
	// Zeige bringt das Programmfenster (wieder) auf den Bildschirm.
	Zeige func() error
	// Sende verschickt eine E-Mail; in Tests austauschbar.
	Sende func(n post.Nachricht, p modell.Postausgang, passwort string) ([]byte, error)
}

const cookieName = "marktrechnung"

type fehlerAntwort struct {
	Fehler    string           `json:"fehler"`
	Meldungen []modell.Meldung `json:"meldungen,omitempty"`
}

func antworte(w http.ResponseWriter, status int, wert any) {
	w.Header().Set("Content-Type", "application/json; charset=utf-8")
	w.Header().Set("Cache-Control", "no-store")
	w.WriteHeader(status)
	json.NewEncoder(w).Encode(wert)
}

func fehler(w http.ResponseWriter, status int, err error) {
	if errors.Is(err, speicher.ErrNichtGefunden) {
		status = http.StatusNotFound
	}
	antworte(w, status, fehlerAntwort{Fehler: err.Error()})
}

func liesJSON(r *http.Request, ziel any) error {
	daten, err := io.ReadAll(io.LimitReader(r.Body, 20<<20))
	if err != nil {
		return err
	}
	if err := json.Unmarshal(daten, ziel); err != nil {
		return errors.New("die Anfrage ist nicht lesbar: " + err.Error())
	}
	return nil
}

// Handler liefert den fertigen HTTP-Handler.
func (s *Server) Handler() http.Handler {
	if s.Sende == nil {
		s.Sende = post.Sende
	}
	mux := http.NewServeMux()
	api := func(muster string, h func(http.ResponseWriter, *http.Request)) {
		mux.HandleFunc(muster, h)
	}
	api("GET /api/stamm", s.stamm)
	api("GET /api/einstellungen", s.einstellungenLesen)
	api("PUT /api/einstellungen", s.einstellungenSchreiben)
	api("POST /api/post/test", s.postTest)
	api("GET /api/logo", s.logoLesen)
	api("POST /api/logo", s.logoSchreiben)
	api("DELETE /api/logo", s.logoSchreiben)

	api("GET /api/kunden", s.kundenListe)
	api("POST /api/kunden", s.kundeSpeichern)
	api("DELETE /api/kunden/{id}", s.kundeLoeschen)
	api("GET /api/artikel", s.artikelListe)
	api("POST /api/artikel", s.artikelSpeichern)
	api("DELETE /api/artikel/{id}", s.artikelLoeschen)

	api("GET /api/rechnungen", s.rechnungenListe)
	api("POST /api/rechnungen", s.entwurfSpeichern)
	api("GET /api/rechnungen/{id}", s.rechnungLesen)
	api("DELETE /api/rechnungen/{id}", s.entwurfLoeschen)
	api("POST /api/pruefen", s.pruefen)
	api("POST /api/vorschau", s.vorschau)
	api("POST /api/rechnungen/{id}/fertigstellen", s.fertigstellen)
	api("GET /api/rechnungen/{id}/datei/{nr}", s.datei)
	api("GET /api/rechnungen/{id}/versand", s.versandVorschlag)
	api("POST /api/rechnungen/{id}/senden", s.senden)
	api("POST /api/rechnungen/{id}/status", s.status)
	api("POST /api/rechnungen/{id}/storno", s.storno)
	api("POST /api/rechnungen/{id}/kopie", s.kopie)

	api("POST /api/import", s.importPruefen)
	api("POST /api/import/uebernehmen", s.importUebernehmen)
	api("POST /api/import/firma", s.importFirma)
	api("POST /api/stammdaten", s.stammdatenEinlesen)
	api("GET /api/stammdaten/pruefung", s.stammdatenPruefen)
	api("GET /api/lexware", s.lexwareStand)
	api("POST /api/lexware/schluessel", s.lexwareSchluessel)
	api("GET /api/lexware/belege", s.lexwareBelege)
	api("POST /api/lexware/abrufen", s.lexwareAbrufen)
	api("POST /api/lexware/kunden", s.lexwareKunden)
	api("POST /api/lexware/artikel", s.lexwareArtikel)
	api("POST /api/ordner", s.ordner)
	api("GET /api/sicherung.zip", s.sicherung)
	api("GET /api/export.csv", s.export)
	api("POST /api/fenster", func(w http.ResponseWriter, r *http.Request) {
		if s.Zeige == nil {
			fehler(w, http.StatusNotImplemented, errors.New("dieses Programm hat kein Fenster"))
			return
		}
		if err := s.Zeige(); err != nil {
			fehler(w, 500, err)
			return
		}
		antworte(w, 200, map[string]bool{"ok": true})
	})
	api("POST /api/beenden", func(w http.ResponseWriter, r *http.Request) {
		antworte(w, 200, map[string]bool{"ok": true})
		if s.Beende != nil {
			go func() { time.Sleep(300 * time.Millisecond); s.Beende() }()
		}
	})
	mux.HandleFunc("/api/", func(w http.ResponseWriter, r *http.Request) {
		fehler(w, http.StatusNotFound, errors.New("unbekannte Anfrage"))
	})

	dateien := http.FileServerFS(s.Web)
	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		// Der Start-Link bringt das Kennwort mit; es wandert in ein Cookie.
		if t := r.URL.Query().Get("t"); t != "" && r.URL.Path == "/" {
			if subtle.ConstantTimeCompare([]byte(t), []byte(s.Token)) == 1 {
				http.SetCookie(w, &http.Cookie{Name: cookieName, Value: s.Token, Path: "/", HttpOnly: true,
					SameSite: http.SameSiteStrictMode, MaxAge: 400 * 24 * 3600})
			}
			http.Redirect(w, r, "/", http.StatusSeeOther)
			return
		}
		w.Header().Set("Cache-Control", "no-cache")
		dateien.ServeHTTP(w, r)
	})
	return s.schutz(mux)
}

// schutz lässt nur Anfragen vom eigenen Rechner mit gültigem Cookie durch.
func (s *Server) schutz(weiter http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		// Fremde Webseiten können den Browser dazu bringen, Adressen im
		// eigenen Rechner aufzurufen. Der Host-Name muss deshalb stimmen.
		host, _, err := net.SplitHostPort(r.Host)
		if err != nil {
			host = r.Host
		}
		if host != "127.0.0.1" && host != "localhost" && host != "[::1]" && host != "::1" {
			http.Error(w, "Zugriff nur vom eigenen Rechner", http.StatusForbidden)
			return
		}
		w.Header().Set("X-Content-Type-Options", "nosniff")
		w.Header().Set("X-Frame-Options", "DENY")
		w.Header().Set("Referrer-Policy", "no-referrer")
		w.Header().Set("Content-Security-Policy", "default-src 'self'; img-src 'self' data: blob:; style-src 'self' 'unsafe-inline'; frame-src 'self' blob:; object-src 'self' blob:")
		if strings.HasPrefix(r.URL.Path, "/api/") {
			c, err := r.Cookie(cookieName)
			if err != nil || subtle.ConstantTimeCompare([]byte(c.Value), []byte(s.Token)) != 1 {
				antworte(w, http.StatusUnauthorized, fehlerAntwort{Fehler: "Nicht angemeldet. Bitte das Programm Marktrechnung über sein Symbol neu öffnen."})
				return
			}
			if r.Method != http.MethodGet && r.Header.Get("X-Marktrechnung") == "" {
				antworte(w, http.StatusForbidden, fehlerAntwort{Fehler: "Anfrage ohne Kennzeichen abgelehnt."})
				return
			}
			if o := r.Header.Get("Origin"); o != "" && !strings.HasSuffix(o, "://"+r.Host) {
				antworte(w, http.StatusForbidden, fehlerAntwort{Fehler: "Anfrage von fremder Seite abgelehnt."})
				return
			}
		}
		weiter.ServeHTTP(w, r)
	})
}

// ---------------------------------------------------------------------------
// Stammdaten und Einstellungen

func (s *Server) stamm(w http.ResponseWriter, r *http.Request) {
	e, _ := s.Speicher.Einstellungen()
	antworte(w, 200, map[string]any{
		"version":      s.Version,
		"datenordner":  s.Speicher.Ordner(),
		"ausgang":      s.Speicher.AusgangOrdner(),
		"einheiten":    codes.Einheiten,
		"laender":      codes.Laender,
		"profile":      partner.Profile(),
		"eingerichtet": strings.TrimSpace(e.Firma.Name) != "",
		"formate": []map[string]string{
			{"id": modell.FormatZugferd, "name": "ZUGFeRD-PDF (empfohlen)", "text": "PDF zum Lesen mit eingebetteter E-Rechnung, Profil EN 16931. Nehmen REWE und EDEKA an."},
			{"id": modell.FormatXRechnungCII, "name": "XRechnung (nur XML)", "text": "Reine XML-Datei nach XRechnung 3.0, Syntax CII. Für Empfänger, die kein PDF wollen."},
			{"id": modell.FormatZugferdXR, "name": "ZUGFeRD-PDF, Profil XRechnung", "text": "PDF mit eingebetteter XRechnung. Nur wählen, wenn der Empfänger es ausdrücklich verlangt."},
		},
	})
}

func (s *Server) einstellungenLesen(w http.ResponseWriter, r *http.Request) {
	e, err := s.Speicher.Einstellungen()
	if err != nil {
		fehler(w, 500, err)
		return
	}
	e.Post.PasswortDa = s.Passwort.Vorhanden()
	antworte(w, 200, e)
}

func (s *Server) einstellungenSchreiben(w http.ResponseWriter, r *http.Request) {
	var ein struct {
		modell.Einstellungen
		Passwort         string `json:"passwort"`
		PasswortLoeschen bool   `json:"passwortLoeschen"`
	}
	if err := liesJSON(r, &ein); err != nil {
		fehler(w, 400, err)
		return
	}
	alt, err := s.Speicher.Einstellungen()
	if err != nil {
		fehler(w, 500, err)
		return
	}
	e := ein.Einstellungen
	e.LogoDatei = alt.LogoDatei
	e.Post.PasswortDa = false
	e.Firma.Land = strings.ToUpper(strings.TrimSpace(e.Firma.Land))
	if e.Firma.Land == "" {
		e.Firma.Land = "DE"
	}
	if e.Firma.EAdresse == "" && e.Firma.Email != "" {
		e.Firma.EAdresse, e.Firma.EAdresseSchema = e.Firma.Email, "EM"
	}
	if strings.TrimSpace(e.Nummernmuster) == "" {
		e.Nummernmuster = "RE-{JJJJ}-{NNNN}"
	}
	if e.NaechsteNummer < 1 {
		e.NaechsteNummer = 1
	}
	// Wer die nächste Nummer von Hand setzt, meint das laufende Jahr - sonst
	// würde die jährliche Zählung sie beim nächsten Fertigstellen verwerfen.
	e.NummerJahr = alt.NummerJahr
	if e.NaechsteNummer != alt.NaechsteNummer {
		e.NummerJahr = time.Now().Year()
	}
	if err := s.Speicher.SetzeEinstellungen(e); err != nil {
		fehler(w, 500, err)
		return
	}
	if ein.PasswortLoeschen {
		err = s.Passwort.Setze("")
	} else if ein.Passwort != "" {
		err = s.Passwort.Setze(ein.Passwort)
	}
	if err != nil {
		fehler(w, 500, fmt.Errorf("die Einstellungen sind gespeichert, das Passwort aber nicht: %w", err))
		return
	}
	s.einstellungenLesen(w, r)
}

func (s *Server) postTest(w http.ResponseWriter, r *http.Request) {
	e, err := s.Speicher.Einstellungen()
	if err != nil {
		fehler(w, 500, err)
		return
	}
	pw, err := s.Passwort.Lies()
	if err != nil {
		fehler(w, 500, err)
		return
	}
	var ein struct {
		Probemail bool `json:"probemail"`
	}
	liesJSON(r, &ein)
	if ein.Probemail {
		n := post.Nachricht{An: []string{e.Post.Absender}, Betreff: "Marktrechnung: Probe-E-Mail",
			Text: "Diese E-Mail wurde vom Programm Marktrechnung verschickt.\n\nDer E-Mail-Versand ist richtig eingerichtet."}
		_, err = s.Sende(n, e.Post, pw)
	} else {
		err = post.Teste(e.Post, pw)
	}
	if err != nil {
		fehler(w, 422, err)
		return
	}
	antworte(w, 200, map[string]any{"ok": true, "an": e.Post.Absender})
}

func (s *Server) logoLesen(w http.ResponseWriter, r *http.Request) {
	daten := s.Speicher.Logo()
	if daten == nil {
		http.NotFound(w, r)
		return
	}
	w.Header().Set("Content-Type", http.DetectContentType(daten))
	w.Header().Set("Cache-Control", "no-store")
	w.Write(daten)
}

func (s *Server) logoSchreiben(w http.ResponseWriter, r *http.Request) {
	if r.Method == http.MethodDelete {
		if err := s.Speicher.SetzeLogo(nil, ""); err != nil {
			fehler(w, 500, err)
			return
		}
		antworte(w, 200, map[string]bool{"ok": true})
		return
	}
	daten, err := io.ReadAll(io.LimitReader(r.Body, 5<<20))
	if err != nil || len(daten) == 0 {
		fehler(w, 400, errors.New("es wurde kein Bild übertragen"))
		return
	}
	endung := map[string]string{"image/png": ".png", "image/jpeg": ".jpg"}[http.DetectContentType(daten)]
	if endung == "" {
		fehler(w, 422, errors.New("das Logo muss eine PNG- oder JPEG-Datei sein"))
		return
	}
	// Probehalber ein PDF damit bauen: So fällt ein unlesbares Bild sofort auf.
	if _, err := beleg.PDF(rechnen.Ausrechnen(modell.Rechnung{Datum: "2026-01-01"}), beleg.Optionen{Logo: daten}); err != nil {
		fehler(w, 422, err)
		return
	}
	if err := s.Speicher.SetzeLogo(daten, endung); err != nil {
		fehler(w, 500, err)
		return
	}
	antworte(w, 200, map[string]bool{"ok": true})
}

// ---------------------------------------------------------------------------
// Kunden und Artikel

func (s *Server) kundenListe(w http.ResponseWriter, r *http.Request) {
	l, err := s.Speicher.Kunden()
	if err != nil {
		fehler(w, 500, err)
		return
	}
	antworte(w, 200, l)
}

func saeubere(p *modell.Partei) {
	p.Name = strings.TrimSpace(p.Name)
	p.GLN = strings.ReplaceAll(strings.TrimSpace(p.GLN), " ", "")
	p.UStID = strings.ToUpper(strings.ReplaceAll(p.UStID, " ", ""))
	p.Land = strings.ToUpper(strings.TrimSpace(p.Land))
	if p.Land == "" {
		p.Land = "DE"
	}
	if p.EAdresse != "" && p.EAdresseSchema == "" {
		p.EAdresseSchema = "EM"
	}
}

func (s *Server) kundeSpeichern(w http.ResponseWriter, r *http.Request) {
	var k modell.Kunde
	if err := liesJSON(r, &k); err != nil {
		fehler(w, 400, err)
		return
	}
	saeubere(&k.Partei)
	if k.Name == "" {
		fehler(w, 422, errors.New("der Kunde braucht einen Namen"))
		return
	}
	for i := range k.Lieferorte {
		l := &k.Lieferorte[i]
		l.GLN = strings.ReplaceAll(strings.TrimSpace(l.GLN), " ", "")
		if l.Land == "" {
			l.Land = "DE"
		}
	}
	if k.RechnungsEmail != "" {
		if _, err := post.Adressen(k.RechnungsEmail); err != nil {
			fehler(w, 422, fmt.Errorf("E-Mail-Adresse für Rechnungen: %w", err))
			return
		}
	}
	// Die Rechnungsadresse dient zugleich als elektronische Adresse (BT-49).
	if k.EAdresse == "" && k.RechnungsEmail != "" {
		erste, _ := post.Adressen(k.RechnungsEmail)
		k.EAdresse, k.EAdresseSchema = erste[0], "EM"
	}
	k, err := s.Speicher.SpeichereKunde(k)
	if err != nil {
		fehler(w, 500, err)
		return
	}
	antworte(w, 200, k)
}

func (s *Server) kundeLoeschen(w http.ResponseWriter, r *http.Request) {
	if err := s.Speicher.LoescheKunde(r.PathValue("id")); err != nil {
		fehler(w, 500, err)
		return
	}
	antworte(w, 200, map[string]bool{"ok": true})
}

func (s *Server) artikelListe(w http.ResponseWriter, r *http.Request) {
	l, err := s.Speicher.Artikel()
	if err != nil {
		fehler(w, 500, err)
		return
	}
	antworte(w, 200, l)
}

func (s *Server) artikelSpeichern(w http.ResponseWriter, r *http.Request) {
	var a modell.Artikel
	if err := liesJSON(r, &a); err != nil {
		fehler(w, 400, err)
		return
	}
	a.Bezeichnung = strings.TrimSpace(a.Bezeichnung)
	a.GTIN = strings.ReplaceAll(strings.TrimSpace(a.GTIN), " ", "")
	if a.Bezeichnung == "" {
		fehler(w, 422, errors.New("der Artikel braucht eine Bezeichnung"))
		return
	}
	if a.GTIN != "" && !codes.GTINGueltig(a.GTIN) {
		fehler(w, 422, fmt.Errorf("die GTIN „%s“ hat eine falsche Länge oder Prüfziffer - bitte mit dem Etikett vergleichen", a.GTIN))
		return
	}
	a, err := s.Speicher.SpeichereArtikel(a)
	if err != nil {
		fehler(w, 500, err)
		return
	}
	antworte(w, 200, a)
}

func (s *Server) artikelLoeschen(w http.ResponseWriter, r *http.Request) {
	if err := s.Speicher.LoescheArtikel(r.PathValue("id")); err != nil {
		fehler(w, 500, err)
		return
	}
	antworte(w, 200, map[string]bool{"ok": true})
}

// ---------------------------------------------------------------------------
// Rechnungen

// zusammenstellen ergänzt einen Entwurf um alles, was aus den Stammdaten
// kommt: die eigenen Firmendaten, die Bankverbindung, die aktuellen Daten
// des Kunden und seines Markts.
func (s *Server) zusammenstellen(r modell.Rechnung) (modell.Rechnung, modell.Einstellungen, error) {
	e, err := s.Speicher.Einstellungen()
	if err != nil {
		return r, e, err
	}
	if r.Status != "" && r.Status != modell.StatusEntwurf {
		return r, e, nil
	}
	r.Verkaeufer = e.Firma
	r.Verkaeufer.Kennung = ""
	r.Kleinunternehmer = e.Kleinunternehmer
	if r.Waehrung == "" {
		r.Waehrung = "EUR"
	}
	if r.Art == "" {
		r.Art = modell.ArtRechnung
	}
	if r.KundeID != "" {
		k, err := s.Speicher.Kunde(r.KundeID)
		if err == nil {
			r.Kaeufer = k.Partei
			r.Verkaeufer.Kennung = k.Lieferantennummer
			r.Profil = k.Profil
			if r.Format == "" {
				r.Format = k.Format
			}
			if r.Kaeuferreferenz == "" {
				r.Kaeuferreferenz = k.Kaeuferreferenz
			}
			if r.Lieferort != nil && r.Lieferort.ID != "" {
				for _, l := range k.Lieferorte {
					if l.ID == r.Lieferort.ID {
						kopie := l
						r.Lieferort = &kopie
					}
				}
			}
		} else if !errors.Is(err, speicher.ErrNichtGefunden) {
			return r, e, err
		}
	}
	if r.Format == "" {
		r.Format = e.Format
	}
	if r.Format == "" {
		r.Format = modell.FormatZugferd
	}
	// REWE verlangt in jeder Rechnung einen festen Hinweis auf die
	// Konditionsvereinbarungen; das Programm setzt ihn selbst.
	if r.Profil == partner.Rewe {
		vorhanden := false
		for _, h := range r.Hinweise {
			vorhanden = vorhanden || h.Code == "AAK"
		}
		if !vorhanden {
			r.Hinweise = append(append([]modell.Hinweis{}, r.Hinweise...), modell.Hinweis{Code: "AAK", Text: partner.ReweKonditionen})
		}
	}
	if r.Lieferort.Leer() {
		r.Lieferort = nil
	}
	// In der XRechnung ist die Käuferreferenz Pflicht. Gibt der Kunde keine
	// vor, tritt an ihre Stelle, was ihn am ehesten zur Rechnung führt.
	if cii.IstXRechnung(r.Format) && r.Kaeuferreferenz == "" {
		for _, ersatz := range []string{r.Kaeufer.GLN, r.Bestellnummer, r.Kaeufer.Kennung} {
			if ersatz != "" {
				r.Kaeuferreferenz = ersatz
				break
			}
		}
	}
	z := &r.Zahlung
	if z.Art == "" {
		z.Art = "58"
	}
	if z.Art == "58" || z.Art == "30" {
		z.IBAN, z.BIC, z.Bank, z.Kontoinhaber = e.IBAN, e.BIC, e.Bank, e.Kontoinhaber
		if z.Kontoinhaber == "" {
			z.Kontoinhaber = e.Firma.Name
		}
	}
	for i := range r.Positionen {
		p := &r.Positionen[i]
		p.GTIN = strings.ReplaceAll(strings.TrimSpace(p.GTIN), " ", "")
		if p.SteuerKat == "" {
			p.SteuerKat = modell.KatNormal
		}
	}
	return r, e, nil
}

// naechsteNummer zeigt, welche Nummer die nächste Rechnung bekäme.
func naechsteNummer(e modell.Einstellungen, datum string) string {
	t, ok := rechnen.Datum(datum)
	if !ok {
		t = time.Now()
	}
	n := e.NaechsteNummer
	if e.JaehrlichNeu && e.NummerJahr != t.Year() {
		n = 1
	}
	if n < 1 {
		n = 1
	}
	return speicher.Nummer(e.Nummernmuster, t, n)
}

type zeile struct {
	ID          string `json:"id"`
	Status      string `json:"status"`
	Art         string `json:"art"`
	Nummer      string `json:"nummer"`
	Datum       string `json:"datum"`
	Faellig     string `json:"faellig"`
	Kunde       string `json:"kunde"`
	Markt       string `json:"markt"`
	Brutto      dec.D  `json:"brutto"`
	Format      string `json:"format"`
	Profil      string `json:"profil"`
	VersendetAm string `json:"versendetAm"`
	VersendetAn string `json:"versendetAn"`
	BezahltAm   string `json:"bezahltAm"`
	Herkunft    string `json:"herkunft"`
}

func (s *Server) rechnungenListe(w http.ResponseWriter, r *http.Request) {
	alle, err := s.Speicher.Rechnungen()
	if err != nil {
		fehler(w, 500, err)
		return
	}
	liste := []zeile{}
	for _, re := range alle {
		if re.Status == modell.StatusEntwurf {
			re, _, _ = s.zusammenstellen(re)
			re = rechnen.Ausrechnen(re)
		}
		z := zeile{ID: re.ID, Status: re.Status, Art: re.Art, Nummer: re.Nummer, Datum: re.Datum, Faellig: re.Faellig,
			Kunde: re.Kaeufer.Name, Format: re.Format, Profil: re.Profil, BezahltAm: re.BezahltAm, Herkunft: re.Herkunft}
		if re.Lieferort != nil {
			z.Markt = re.Lieferort.Name
		}
		if re.Summen != nil {
			z.Brutto = re.Summen.Brutto
		}
		for _, v := range re.Versand {
			if v.Fehler == "" {
				z.VersendetAm, z.VersendetAn = v.Zeit, v.An
			}
		}
		liste = append(liste, z)
	}
	antworte(w, 200, liste)
}

func (s *Server) rechnungLesen(w http.ResponseWriter, r *http.Request) {
	re, err := s.Speicher.Rechnung(r.PathValue("id"))
	if err != nil {
		fehler(w, 500, err)
		return
	}
	antworte(w, 200, re)
}

func (s *Server) entwurfSpeichern(w http.ResponseWriter, r *http.Request) {
	var re modell.Rechnung
	if err := liesJSON(r, &re); err != nil {
		fehler(w, 400, err)
		return
	}
	re.Summen = nil
	re, err := s.Speicher.SpeichereEntwurf(re)
	if err != nil {
		fehler(w, 409, err)
		return
	}
	antworte(w, 200, re)
}

func (s *Server) entwurfLoeschen(w http.ResponseWriter, r *http.Request) {
	if err := s.Speicher.LoescheEntwurf(r.PathValue("id")); err != nil {
		fehler(w, 409, err)
		return
	}
	antworte(w, 200, map[string]bool{"ok": true})
}

type pruefErgebnis struct {
	Rechnung       modell.Rechnung  `json:"rechnung"`
	Meldungen      []modell.Meldung `json:"meldungen"`
	NummerVorschau string           `json:"nummerVorschau"`
	Zahlungstext   string           `json:"zahlungstext"`
}

func (s *Server) pruefeEntwurf(re modell.Rechnung) (pruefErgebnis, modell.Rechnung, modell.Einstellungen, error) {
	re.Summen = nil
	voll, e, err := s.zusammenstellen(re)
	if err != nil {
		return pruefErgebnis{}, voll, e, err
	}
	meldungen, aus := pruefung.Pruefe(voll, pruefung.Optionen{NummerOffen: voll.Nummer == ""})
	if meldungen == nil {
		meldungen = []modell.Meldung{}
	}
	if voll.KundeID == "" && voll.Kaeufer.Name == "" {
		// Ohne Kunde wäre die Liste voller Folgefehler.
		meldungen = []modell.Meldung{{Regel: "KUNDE", Schwere: modell.Fehler, Feld: "kundeId", Text: "Bitte zuerst einen Kunden auswählen."}}
	}
	return pruefErgebnis{Rechnung: aus, Meldungen: meldungen, NummerVorschau: naechsteNummer(e, voll.Datum),
		Zahlungstext: aus.Zahlung.Bedingungen}, voll, e, nil
}

func (s *Server) pruefen(w http.ResponseWriter, r *http.Request) {
	var re modell.Rechnung
	if err := liesJSON(r, &re); err != nil {
		fehler(w, 400, err)
		return
	}
	erg, _, _, err := s.pruefeEntwurf(re)
	if err != nil {
		fehler(w, 500, err)
		return
	}
	antworte(w, 200, erg)
}

func (s *Server) erzeugeOptionen(e modell.Einstellungen) zugferd.Optionen {
	return zugferd.Optionen{Einst: e, Logo: s.Speicher.Logo(), Zeit: time.Now(), Version: s.Version}
}

func (s *Server) vorschau(w http.ResponseWriter, r *http.Request) {
	var re modell.Rechnung
	if err := liesJSON(r, &re); err != nil {
		fehler(w, 400, err)
		return
	}
	re.Summen = nil
	voll, e, err := s.zusammenstellen(re)
	if err != nil {
		fehler(w, 500, err)
		return
	}
	voll.Status = modell.StatusEntwurf
	pdf, err := zugferd.Vorschau(voll, s.erzeugeOptionen(e))
	if err != nil {
		fehler(w, 500, err)
		return
	}
	w.Header().Set("Content-Type", "application/pdf")
	w.Header().Set("Cache-Control", "no-store")
	w.Write(pdf)
}

func (s *Server) fertigstellen(w http.ResponseWriter, r *http.Request) {
	var ein struct {
		TrotzWarnungen bool `json:"trotzWarnungen"`
	}
	liesJSON(r, &ein)
	re, err := s.Speicher.Rechnung(r.PathValue("id"))
	if err != nil {
		fehler(w, 500, err)
		return
	}
	if re.Status != modell.StatusEntwurf {
		fehler(w, 409, speicher.ErrFestgeschrieben)
		return
	}
	erg, voll, e, err := s.pruefeEntwurf(re)
	if err != nil {
		fehler(w, 500, err)
		return
	}
	if pruefung.HatFehler(erg.Meldungen) {
		antworte(w, 422, fehlerAntwort{Fehler: "Die Rechnung enthält noch Fehler und kann so nicht fertiggestellt werden.", Meldungen: erg.Meldungen})
		return
	}
	warnungen := 0
	for _, m := range erg.Meldungen {
		if m.Schwere == modell.Warnung {
			warnungen++
		}
	}
	if warnungen > 0 && !ein.TrotzWarnungen {
		antworte(w, 422, fehlerAntwort{Fehler: "Zur Rechnung gibt es noch Warnungen.", Meldungen: erg.Meldungen})
		return
	}
	// Abgeleitete Angaben einfrieren, damit die Rechnung später genauso
	// angezeigt wird, wie sie verschickt wurde.
	voll.Faellig = erg.Rechnung.Faellig
	voll.Zahlung.Bedingungen = erg.Rechnung.Zahlung.Bedingungen
	voll.Summen = erg.Rechnung.Summen
	// Das Logo vorab lesen: Während des Fertigstellens ist der Datenordner
	// gesperrt.
	optionen := s.erzeugeOptionen(e)
	fertig, err := s.Speicher.Fertigstellen(voll, func(nr modell.Rechnung) ([]speicher.Datei, error) {
		nr.Summen = nil
		dateien, err := zugferd.Erzeuge(nr, optionen)
		if err != nil {
			return nil, err
		}
		var l []speicher.Datei
		for _, d := range dateien {
			l = append(l, speicher.Datei{Name: d.Name, Daten: d.Daten})
		}
		return l, nil
	})
	if err != nil {
		fehler(w, 409, err)
		return
	}
	if fertig.StornoVon != "" {
		s.Speicher.Aendere(fertig.StornoVon, func(alt *modell.Rechnung) error {
			alt.Status, alt.StornoDurch = modell.StatusStorniert, fertig.ID
			return nil
		})
	}
	antworte(w, 200, fertig)
}

func (s *Server) datei(w http.ResponseWriter, r *http.Request) {
	re, err := s.Speicher.Rechnung(r.PathValue("id"))
	if err != nil {
		fehler(w, 500, err)
		return
	}
	var nr int
	fmt.Sscan(r.PathValue("nr"), &nr)
	if nr < 0 || nr >= len(re.Dateien) {
		fehler(w, 404, speicher.ErrNichtGefunden)
		return
	}
	daten, err := s.Speicher.LiesDatei(re.Dateien[nr])
	if err != nil {
		fehler(w, 404, fmt.Errorf("die Datei %s liegt nicht mehr im Ordner Rechnungen", re.Dateien[nr]))
		return
	}
	name := filepath.Base(re.Dateien[nr])
	art := "application/pdf"
	if strings.HasSuffix(name, ".xml") {
		art = "application/xml"
	}
	anzeige := "inline"
	if r.URL.Query().Get("laden") != "" {
		anzeige = "attachment"
	}
	w.Header().Set("Content-Type", art)
	w.Header().Set("Content-Disposition", fmt.Sprintf(`%s; filename="%s"`, anzeige, name))
	w.Write(daten)
}

// versanddateien liefert die Dateien, die verschickt werden: die E-Rechnung
// selbst, nicht die Ansichtskopie.
func versanddateien(re modell.Rechnung) []string {
	var l []string
	for _, d := range re.Dateien {
		if !strings.HasSuffix(d, "_Ansicht.pdf") {
			l = append(l, d)
		}
	}
	return l
}

func fuelle(vorlage string, re modell.Rechnung) string {
	betrag := ""
	if re.Summen != nil {
		betrag = beleg.Euro(re.Summen.Brutto, re.Waehrung)
	}
	return strings.NewReplacer("{NUMMER}", re.Nummer, "{DATUM}", rechnen.DatumDeutsch(re.Datum),
		"{FIRMA}", re.Verkaeufer.Name, "{BETRAG}", betrag, "{KUNDE}", re.Kaeufer.Name,
		"{BELEG}", beleg.Titel(re)).Replace(vorlage)
}

func (s *Server) versandVorschlag(w http.ResponseWriter, r *http.Request) {
	re, err := s.Speicher.Rechnung(r.PathValue("id"))
	if err != nil {
		fehler(w, 500, err)
		return
	}
	e, _ := s.Speicher.Einstellungen()
	an, betreff, text := "", "{BELEG} {NUMMER}", "Guten Tag,\n\nim Anhang erhalten Sie unsere {BELEG} {NUMMER} vom {DATUM} als E-Rechnung.\n\nFreundliche Grüße\n{FIRMA}"
	if k, err := s.Speicher.Kunde(re.KundeID); err == nil {
		an = k.RechnungsEmail
		if strings.TrimSpace(k.EmailBetreff) != "" {
			betreff = k.EmailBetreff
		}
		if strings.TrimSpace(k.EmailText) != "" {
			text = k.EmailText
		}
	}
	var hinweise []string
	groesse := 0
	var namen []string
	for _, d := range versanddateien(re) {
		daten, err := s.Speicher.LiesDatei(d)
		if err != nil {
			hinweise = append(hinweise, "Die Datei "+d+" liegt nicht mehr im Ordner Rechnungen.")
			continue
		}
		groesse += len(daten)
		namen = append(namen, filepath.Base(d))
	}
	if testpostfach(an) {
		hinweise = append(hinweise, "Achtung: "+an+" sieht nach einem Test-Postfach aus. Dorthin gehören nur Testrechnungen. Für echte Rechnungen bitte beim Kunden die Adresse eintragen, die er dafür genannt hat.")
	}
	if p, ok := partner.Finde(re.Profil); ok {
		if p.OhneText {
			text = ""
		}
		switch re.Profil {
		case partner.Rewe:
			h := "REWE nimmt Rechnungen nur von angemeldeten Absenderadressen und erst ab dem bestätigten Starttermin an."
			if e.Post.Absender != "" {
				h += " Absender ist " + e.Post.Absender + "."
			}
			hinweise = append(hinweise, h, "REWE möchte keinen Text und keine Signatur in der E-Mail - nur die Rechnung als Anhang. Das Textfeld ist deshalb leer.")
		case partner.Edeka:
			hinweise = append(hinweise, "EDEKA: eine Rechnung und ein Anhang je E-Mail, die Rechnungsnummer im Betreff.")
		}
		if p.MaxMB > 0 && groesse > p.MaxMB*1000*1000*3/4 {
			hinweise = append(hinweise, fmt.Sprintf("Die E-Mail wird größer als die erlaubten %d MB.", p.MaxMB))
		}
	}
	antworte(w, 200, map[string]any{
		"an": an, "betreff": fuelle(betreff, re), "text": fuelle(text, re), "dateien": namen, "hinweise": hinweise,
		"eingerichtet": e.Post.Server != "" && e.Post.Absender != "" && s.Passwort.Vorhanden(),
		"absender":     e.Post.Absender, "kopieAnMich": e.Post.KopieAnMich,
	})
}

func (s *Server) senden(w http.ResponseWriter, r *http.Request) {
	var ein struct {
		An, Kopie, Betreff, Text string
	}
	if err := liesJSON(r, &ein); err != nil {
		fehler(w, 400, err)
		return
	}
	re, err := s.Speicher.Rechnung(r.PathValue("id"))
	if err != nil {
		fehler(w, 500, err)
		return
	}
	if re.Status == modell.StatusEntwurf {
		fehler(w, 409, errors.New("ein Entwurf kann nicht verschickt werden. Bitte die Rechnung erst fertigstellen"))
		return
	}
	an, err := post.Adressen(ein.An)
	if err != nil || len(an) == 0 {
		if err == nil {
			err = errors.New("bitte die E-Mail-Adresse des Empfängers eintragen")
		}
		fehler(w, 422, err)
		return
	}
	kopie, err := post.Adressen(ein.Kopie)
	if err != nil {
		fehler(w, 422, err)
		return
	}
	if strings.TrimSpace(ein.Betreff) == "" {
		fehler(w, 422, errors.New("bitte einen Betreff eintragen"))
		return
	}
	e, err := s.Speicher.Einstellungen()
	if err != nil {
		fehler(w, 500, err)
		return
	}
	pw, err := s.Passwort.Lies()
	if err != nil {
		fehler(w, 500, err)
		return
	}
	n := post.Nachricht{An: an, Kopie: kopie, Betreff: strings.TrimSpace(ein.Betreff), Text: ein.Text}
	if e.Post.KopieAnMich && e.Post.Absender != "" {
		n.Blind = []string{e.Post.Absender}
	}
	var namen []string
	for _, d := range versanddateien(re) {
		daten, err := s.Speicher.LiesDatei(d)
		if err != nil {
			fehler(w, 409, fmt.Errorf("die Datei %s liegt nicht mehr im Ordner Rechnungen", d))
			return
		}
		art := "application/pdf"
		if strings.HasSuffix(d, ".xml") {
			art = "application/xml"
		}
		n.Anhang = append(n.Anhang, post.Anhang{Name: filepath.Base(d), Mime: art, Daten: daten})
		namen = append(namen, filepath.Base(d))
	}
	if len(n.Anhang) == 0 {
		fehler(w, 409, errors.New("zu dieser Rechnung gibt es keine Datei zum Verschicken"))
		return
	}
	roh, err := s.Sende(n, e.Post, pw)
	if err != nil {
		fehler(w, 502, err)
		return
	}
	s.Speicher.MerkeVersand(re.ID, roh)
	re, err = s.Speicher.Aendere(re.ID, func(x *modell.Rechnung) error {
		x.Versand = append(x.Versand, modell.Versand{Zeit: speicher.Jetzt(), An: strings.Join(append(an, kopie...), ", "),
			Betreff: n.Betreff, Dateien: strings.Join(namen, ", "), Weg: "email"})
		if x.Status == modell.StatusFertig {
			x.Status = modell.StatusVersendet
		}
		return nil
	})
	if err != nil {
		fehler(w, 500, fmt.Errorf("die E-Mail ist verschickt, der Vermerk konnte aber nicht gespeichert werden: %w", err))
		return
	}
	antworte(w, 200, re)
}

func (s *Server) status(w http.ResponseWriter, r *http.Request) {
	var ein struct {
		Status string `json:"status"`
		Datum  string `json:"datum"`
		An     string `json:"an"`
	}
	if err := liesJSON(r, &ein); err != nil {
		fehler(w, 400, err)
		return
	}
	re, err := s.Speicher.Aendere(r.PathValue("id"), func(x *modell.Rechnung) error {
		if x.Status == modell.StatusEntwurf {
			return errors.New("ein Entwurf hat noch keinen Status")
		}
		if x.Status == modell.StatusStorniert {
			return errors.New("die Rechnung ist storniert")
		}
		switch ein.Status {
		case modell.StatusBezahlt:
			x.Status = modell.StatusBezahlt
			x.BezahltAm = ein.Datum
			if x.BezahltAm == "" {
				x.BezahltAm = time.Now().Format(rechnen.Datumsformat)
			}
		case modell.StatusVersendet:
			// Von Hand verschickt, zum Beispiel über ein Portal hochgeladen.
			x.Versand = append(x.Versand, modell.Versand{Zeit: speicher.Jetzt(), An: ein.An, Weg: "manuell"})
			if x.Status == modell.StatusFertig {
				x.Status = modell.StatusVersendet
			}
		case "offen":
			x.BezahltAm = ""
			x.Status = modell.StatusFertig
			for _, v := range x.Versand {
				if v.Fehler == "" {
					x.Status = modell.StatusVersendet
				}
			}
		default:
			return errors.New("unbekannter Status")
		}
		return nil
	})
	if err != nil {
		fehler(w, 409, err)
		return
	}
	antworte(w, 200, re)
}

// neuerEntwurfAus kopiert den Inhalt einer Rechnung in einen neuen Entwurf.
func neuerEntwurfAus(alt modell.Rechnung) modell.Rechnung {
	neu := alt
	neu.ID, neu.Nummer, neu.Status = "", "", modell.StatusEntwurf
	neu.Datum = time.Now().Format(rechnen.Datumsformat)
	neu.Faellig, neu.Summen, neu.Dateien, neu.Versand = "", nil, nil, nil
	neu.Fertiggestellt, neu.BezahltAm, neu.StornoVon, neu.StornoDurch, neu.Herkunft = "", "", "", "", ""
	neu.Zahlung.Verwendungszweck, neu.Zahlung.Bedingungen, neu.Zahlung.Vorauszahlung = "", "", 0
	neu.Vorgaenger = nil
	neu.Positionen = append([]modell.Position(nil), alt.Positionen...)
	return neu
}

func (s *Server) storno(w http.ResponseWriter, r *http.Request) {
	alt, err := s.Speicher.Rechnung(r.PathValue("id"))
	if err != nil {
		fehler(w, 500, err)
		return
	}
	if alt.Status == modell.StatusEntwurf || alt.Status == modell.StatusStorniert || alt.Art == modell.ArtGutschrift {
		fehler(w, 409, errors.New("diese Rechnung kann nicht storniert werden"))
		return
	}
	neu := neuerEntwurfAus(alt)
	neu.Art = modell.ArtGutschrift
	neu.Vorgaenger = []modell.Vorgaenger{{Nummer: alt.Nummer, Datum: alt.Datum}}
	neu.StornoVon = alt.ID
	neu.Einleitung = "Hiermit korrigieren wir unsere Rechnung " + alt.Nummer + " vom " + rechnen.DatumDeutsch(alt.Datum) + " und schreiben Ihnen den folgenden Betrag gut:"
	neu.Schluss = ""
	neu.Zahlung.SkontoTage, neu.Zahlung.SkontoProzent = 0, 0
	neu, err = s.Speicher.SpeichereEntwurf(neu)
	if err != nil {
		fehler(w, 500, err)
		return
	}
	antworte(w, 200, neu)
}

func (s *Server) kopie(w http.ResponseWriter, r *http.Request) {
	alt, err := s.Speicher.Rechnung(r.PathValue("id"))
	if err != nil {
		fehler(w, 500, err)
		return
	}
	neu := neuerEntwurfAus(alt)
	neu.Leistungsdatum, neu.Bestellnummer, neu.Lieferscheinnummer = "", "", ""
	neu, err = s.Speicher.SpeichereEntwurf(neu)
	if err != nil {
		fehler(w, 500, err)
		return
	}
	antworte(w, 200, neu)
}

// ---------------------------------------------------------------------------
// Ordner, Sicherung, Export

func (s *Server) ordner(w http.ResponseWriter, r *http.Request) {
	var ein struct {
		Ziel string `json:"ziel"`
	}
	liesJSON(r, &ein)
	pfad := s.Speicher.AusgangOrdner()
	if ein.Ziel == "daten" {
		pfad = s.Speicher.Ordner()
	}
	if s.Oeffne == nil {
		fehler(w, 501, errors.New("Ordner: "+pfad))
		return
	}
	if err := s.Oeffne(pfad); err != nil {
		fehler(w, 500, fmt.Errorf("der Ordner %s lässt sich nicht öffnen: %w", pfad, err))
		return
	}
	antworte(w, 200, map[string]string{"pfad": pfad})
}

func (s *Server) sicherung(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/zip")
	w.Header().Set("Content-Disposition", fmt.Sprintf(`attachment; filename="Marktrechnung-Sicherung-%s.zip"`, time.Now().Format("2006-01-02")))
	s.Speicher.Sicherung(w)
}

// export liefert die Rechnungsliste als CSV-Datei für die Buchhaltung.
func (s *Server) export(w http.ResponseWriter, r *http.Request) {
	alle, err := s.Speicher.Rechnungen()
	if err != nil {
		fehler(w, 500, err)
		return
	}
	jahr := r.URL.Query().Get("jahr")
	var b bytes.Buffer
	b.WriteString("\xef\xbb\xbf") // damit Excel die Umlaute richtig liest
	c := csv.NewWriter(&b)
	c.Comma = ';'
	c.UseCRLF = true
	c.Write([]string{"Rechnungsnummer", "Belegart", "Rechnungsdatum", "Lieferdatum", "Kunde", "USt-IdNr. Kunde", "Markt",
		"Netto", "Umsatzsteuer", "Brutto", "Netto 7 %", "USt 7 %", "Netto 19 %", "USt 19 %", "Netto ohne USt",
		"Fällig", "Status", "Versendet am", "Bezahlt am", "Bestellnummer", "Lieferschein"})
	for i := len(alle) - 1; i >= 0; i-- {
		re := alle[i]
		if re.Status == modell.StatusEntwurf || re.Summen == nil || (jahr != "" && !strings.HasPrefix(re.Datum, jahr)) {
			continue
		}
		vz := dec.Eins
		if re.Art == modell.ArtGutschrift {
			vz = -dec.Eins
		}
		geld := func(d dec.D) string { return d.Mal(vz, 2).Deutsch(2, 2) }
		var n7, s7, n19, s19, n0 dec.D
		for _, st := range re.Summen.Steuern {
			switch {
			case st.Kat == modell.KatNormal && st.Satz == dec.Int(7):
				n7, s7 = n7+st.Basis, s7+st.Betrag
			case st.Kat == modell.KatNormal && st.Satz == dec.Int(19):
				n19, s19 = n19+st.Basis, s19+st.Betrag
			case st.Betrag.IstNull():
				n0 += st.Basis
			}
		}
		markt, versendet := "", ""
		if re.Lieferort != nil {
			markt = re.Lieferort.Name
		}
		for _, v := range re.Versand {
			if v.Fehler == "" && len(v.Zeit) >= 10 {
				versendet = rechnen.DatumDeutsch(v.Zeit[:10])
			}
		}
		c.Write([]string{re.Nummer, beleg.Titel(re), rechnen.DatumDeutsch(re.Datum), rechnen.DatumDeutsch(re.Leistungsdatum),
			re.Kaeufer.Name, re.Kaeufer.UStID, markt, geld(re.Summen.Netto), geld(re.Summen.Steuer), geld(re.Summen.Brutto),
			geld(n7), geld(s7), geld(n19), geld(s19), geld(n0), rechnen.DatumDeutsch(re.Faellig), re.Status, versendet,
			rechnen.DatumDeutsch(re.BezahltAm), re.Bestellnummer, re.Lieferscheinnummer})
	}
	c.Flush()
	name := "Rechnungen.csv"
	if jahr != "" {
		name = "Rechnungen-" + jahr + ".csv"
	}
	w.Header().Set("Content-Type", "text/csv; charset=utf-8")
	w.Header().Set("Content-Disposition", `attachment; filename="`+name+`"`)
	w.Write(b.Bytes())
}
