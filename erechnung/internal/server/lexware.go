package server

import (
	"errors"
	"net/http"
	"strings"

	"marktrechnung/internal/lexware"
	"marktrechnung/internal/modell"
	"marktrechnung/internal/post"
	"marktrechnung/internal/rechnen"
)

// Direkter Abruf aus Lexware Office: Rechnungen, Kunden und Artikel kommen
// über dessen Schnittstelle, ohne dass Dateien heruntergeladen werden
// müssen. Gelesen wird nur; in Lexware Office ändert sich nichts.

func (s *Server) lexwareClient() (*lexware.Client, error) {
	schluessel, err := s.Lexware.Lies()
	if err != nil {
		return nil, err
	}
	if schluessel == "" {
		return nil, errors.New("es ist noch kein Zugangsschlüssel für Lexware Office hinterlegt")
	}
	c := lexware.Neu(schluessel)
	if s.LexwareBasis != "" {
		c.Basis, c.Pause = s.LexwareBasis, 0
	}
	return c, nil
}

func (s *Server) lexwareStand(w http.ResponseWriter, r *http.Request) {
	antworte(w, 200, map[string]any{"verbunden": s.Lexware.Vorhanden()})
}

// lexwareSchluessel speichert den Zugangsschlüssel, nachdem er sich bei
// Lexware Office bewährt hat. Ein leerer Schlüssel trennt die Verbindung.
func (s *Server) lexwareSchluessel(w http.ResponseWriter, r *http.Request) {
	var ein struct {
		Schluessel string `json:"schluessel"`
	}
	if err := liesJSON(r, &ein); err != nil {
		fehler(w, 400, err)
		return
	}
	ein.Schluessel = strings.TrimSpace(ein.Schluessel)
	if ein.Schluessel == "" {
		if err := s.Lexware.Setze(""); err != nil {
			fehler(w, 500, err)
			return
		}
		antworte(w, 200, map[string]any{"verbunden": false})
		return
	}
	c := lexware.Neu(ein.Schluessel)
	if s.LexwareBasis != "" {
		c.Basis, c.Pause = s.LexwareBasis, 0
	}
	profil, err := c.Profil()
	if err != nil {
		fehler(w, 422, err)
		return
	}
	if err := s.Lexware.Setze(ein.Schluessel); err != nil {
		fehler(w, 500, err)
		return
	}
	antworte(w, 200, map[string]any{"verbunden": true, "firma": profil.Firma})
}

type lexwareBeleg struct {
	lexware.Beleg
	Stand string `json:"stand"` // neu, entwurf, vorhanden
}

func (s *Server) lexwareBelege(w http.ResponseWriter, r *http.Request) {
	c, err := s.lexwareClient()
	if err != nil {
		fehler(w, 422, err)
		return
	}
	von := r.URL.Query().Get("von")
	if _, ok := rechnen.Datum(von); von != "" && !ok {
		fehler(w, 422, errors.New("das Datum ist ungültig"))
		return
	}
	belege, err := c.Belege(von)
	if err != nil {
		fehler(w, 502, err)
		return
	}
	stand := map[string]string{}
	if alle, err := s.Speicher.Rechnungen(); err == nil {
		for _, a := range alle {
			n := strings.ToLower(a.Nummer)
			switch {
			case n == "":
			case a.Status != modell.StatusEntwurf:
				stand[n] = "vorhanden"
			case a.Herkunft != "" && stand[n] == "":
				stand[n] = "entwurf"
			}
		}
	}
	liste := []lexwareBeleg{}
	for _, b := range belege {
		st := stand[strings.ToLower(b.Nummer)]
		if st == "" {
			st = "neu"
		}
		liste = append(liste, lexwareBeleg{Beleg: b, Stand: st})
	}
	antworte(w, 200, liste)
}

// lexwareAbrufen liest die gewählten Belege ein und prüft sie - wie
// hineingezogene Dateien. Ist ein Beleg in Lexware Office eine E-Rechnung,
// dient deren Datei als Quelle; sonst die Angaben aus der Schnittstelle.
func (s *Server) lexwareAbrufen(w http.ResponseWriter, r *http.Request) {
	var ein struct {
		Belege []struct {
			ID     string `json:"id"`
			Typ    string `json:"typ"`
			Nummer string `json:"nummer"`
		} `json:"belege"`
		Profil string `json:"profil"`
	}
	if err := liesJSON(r, &ein); err != nil {
		fehler(w, 400, err)
		return
	}
	if len(ein.Belege) == 0 {
		fehler(w, 422, errors.New("es ist kein Beleg ausgewählt"))
		return
	}
	if len(ein.Belege) > 100 {
		fehler(w, 422, errors.New("bitte höchstens 100 Belege auf einmal einlesen"))
		return
	}
	c, err := s.lexwareClient()
	if err != nil {
		fehler(w, 422, err)
		return
	}
	e, err := s.Speicher.Einstellungen()
	if err != nil {
		fehler(w, 500, err)
		return
	}
	type eintrag struct {
		Name string         `json:"name"`
		Erg  importErgebnis `json:"erg"`
	}
	liste := []eintrag{}
	for _, b := range ein.Belege {
		name := "Lexware Office " + b.Nummer
		erg := importErgebnis{Meldungen: []modell.Meldung{}, Auffaellig: []string{}, Stand: "neu"}
		var re modell.Rechnung
		ausDatei := false
		datei, err := c.Datei(b.Typ, b.ID)
		var status *lexware.Statusfehler
		if err != nil && !errors.As(err, &status) {
			// Kein Zugang oder keine Verbindung: Weitere Versuche wären sinnlos.
			fehler(w, 502, err)
			return
		}
		if err == nil {
			var auffaellig []string
			if re, erg.Dateiart, auffaellig, err = liesRechnungsdatei(datei); err == nil {
				ausDatei = true
				erg.Auffaellig = append(erg.Auffaellig, auffaellig...)
			}
		}
		if !ausDatei {
			var hinweise []string
			if re, hinweise, err = c.Rechnung(b.Typ, b.ID); err != nil {
				if !errors.As(err, &status) {
					fehler(w, 502, err)
					return
				}
				erg.Fehler = err.Error()
				liste = append(liste, eintrag{name, erg})
				continue
			}
			erg.Dateiart = "Angaben aus Lexware Office (dort keine E-Rechnung, sondern ein gewöhnliches PDF)"
			erg.Auffaellig = append(erg.Auffaellig, hinweise...)
			// Der Rechnungssteller ist die eigene Firma; ihre Angaben und die
			// Bankverbindung stehen in den Einstellungen.
			re.Verkaeufer = e.Firma
			re.Kleinunternehmer = e.Kleinunternehmer
			re.Zahlung.IBAN, re.Zahlung.BIC, re.Zahlung.Kontoinhaber = e.IBAN, e.BIC, e.Kontoinhaber
		}
		re.Herkunft = name
		s.beurteile(&erg, re, ein.Profil, ausDatei)
		liste = append(liste, eintrag{name, erg})
	}
	antworte(w, 200, liste)
}

func fuelleLeer(ziel *string, wert string) bool {
	if strings.TrimSpace(*ziel) == "" && strings.TrimSpace(wert) != "" {
		*ziel = strings.TrimSpace(wert)
		return true
	}
	return false
}

// lexwareKunden gleicht die Firmenkunden ab: Neue werden angelegt, bei
// bekannten werden nur leere Felder gefüllt. Was hier gepflegt wurde (GLN,
// Märkte, Kostenstellen), bleibt unangetastet.
func (s *Server) lexwareKunden(w http.ResponseWriter, r *http.Request) {
	c, err := s.lexwareClient()
	if err != nil {
		fehler(w, 422, err)
		return
	}
	fremde, err := c.Kunden()
	if err != nil {
		fehler(w, 502, err)
		return
	}
	kunden, err := s.Speicher.Kunden()
	if err != nil {
		fehler(w, 500, err)
		return
	}
	neu, ergaenzt := 0, 0
	for _, f := range fremde {
		if f.RechnungsEmail != "" {
			if _, err := post.Adressen(f.RechnungsEmail); err != nil {
				f.RechnungsEmail = ""
			}
		}
		k := findeKunde(kunden, f.Partei)
		if k == nil {
			f.Profil = rateProfil(f.Name)
			saeubere(&f.Partei)
			angelegt, err := s.Speicher.SpeichereKunde(f)
			if err != nil {
				fehler(w, 500, err)
				return
			}
			kunden = append(kunden, angelegt)
			neu++
			continue
		}
		geaendert := false
		for _, paar := range []struct {
			ziel *string
			wert string
		}{{&k.Strasse, f.Strasse}, {&k.Zusatz, f.Zusatz}, {&k.PLZ, f.PLZ}, {&k.Ort, f.Ort}, {&k.UStID, f.UStID}, {&k.Kennung, f.Kennung},
			{&k.Lieferantennummer, f.Lieferantennummer}, {&k.Kaeuferreferenz, f.Kaeuferreferenz}, {&k.RechnungsEmail, f.RechnungsEmail},
			{&k.Ansprechpartner, f.Ansprechpartner}, {&k.Telefon, f.Telefon}, {&k.Email, f.Email}} {
			if fuelleLeer(paar.ziel, paar.wert) {
				geaendert = true
			}
		}
		for _, l := range f.Lieferorte {
			if findeLieferort(*k, l) == nil {
				k.Lieferorte = append(k.Lieferorte, l)
				geaendert = true
			}
		}
		if geaendert {
			gespeichert, err := s.Speicher.SpeichereKunde(*k)
			if err != nil {
				fehler(w, 500, err)
				return
			}
			*k = gespeichert
			ergaenzt++
		}
	}
	antworte(w, 200, map[string]int{"gefunden": len(fremde), "neu": neu, "ergaenzt": ergaenzt})
}

// lexwareArtikel gleicht die Artikel ab. Preis und Steuersatz kommen aus
// Lexware Office; eine hier eingetragene GTIN bleibt erhalten.
func (s *Server) lexwareArtikel(w http.ResponseWriter, r *http.Request) {
	c, err := s.lexwareClient()
	if err != nil {
		fehler(w, 422, err)
		return
	}
	fremde, hinweise, err := c.Artikel()
	if err != nil {
		fehler(w, 502, err)
		return
	}
	artikel, err := s.Speicher.Artikel()
	if err != nil {
		fehler(w, 500, err)
		return
	}
	neu, geaendert := 0, 0
	for _, f := range fremde {
		var a *modell.Artikel
		for i, x := range artikel {
			if (f.Artikelnummer != "" && x.Artikelnummer == f.Artikelnummer) || normal(x.Bezeichnung) == normal(f.Bezeichnung) {
				a = &artikel[i]
				break
			}
		}
		if a == nil {
			angelegt, err := s.Speicher.SpeichereArtikel(f)
			if err != nil {
				fehler(w, 500, err)
				return
			}
			artikel = append(artikel, angelegt)
			neu++
			continue
		}
		vorher := *a
		a.Preis, a.SteuerSatz = f.Preis, f.SteuerSatz
		fuelleLeer(&a.GTIN, f.GTIN)
		fuelleLeer(&a.Artikelnummer, f.Artikelnummer)
		fuelleLeer(&a.Beschreibung, f.Beschreibung)
		if (a.Einheit == "" || a.Einheit == "C62") && f.Einheit != "" {
			a.Einheit = f.Einheit
		}
		if a.Preis != vorher.Preis || a.SteuerSatz != vorher.SteuerSatz || a.GTIN != vorher.GTIN || a.Artikelnummer != vorher.Artikelnummer ||
			a.Beschreibung != vorher.Beschreibung || a.Einheit != vorher.Einheit {
			if _, err := s.Speicher.SpeichereArtikel(*a); err != nil {
				fehler(w, 500, err)
				return
			}
			geaendert++
		}
	}
	if hinweise == nil {
		hinweise = []string{}
	}
	antworte(w, 200, map[string]any{"gefunden": len(fremde), "neu": neu, "geaendert": geaendert, "hinweise": hinweise})
}
