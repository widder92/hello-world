package speicher

import (
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"time"

	"marktrechnung/internal/modell"
)

// Ablage der Zusatzbelege (Angebote, Lieferscheine) und der
// Serienrechnungen. Sie liegen getrennt von den Rechnungen, damit nichts,
// was mit Rechnungen arbeitet - Nummernkreis, Übergabe an die Buchhaltung,
// Auswertungen -, sie versehentlich mitnimmt.

// Belege liefert die Zusatzbelege einer Art, die neuesten zuerst.
func (s *Speicher) Belege(art string) ([]modell.Rechnung, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.belege(art)
}

func (s *Speicher) belege(art string) ([]modell.Rechnung, error) {
	l := []modell.Rechnung{}
	eintraege, err := os.ReadDir(s.pfad("belege"))
	if errors.Is(err, os.ErrNotExist) {
		return l, nil
	}
	if err != nil {
		return nil, err
	}
	for _, e := range eintraege {
		if e.IsDir() || !strings.HasSuffix(e.Name(), ".json") {
			continue
		}
		var r modell.Rechnung
		if err := lies(s.pfad("belege", e.Name()), &r); err != nil {
			return nil, err
		}
		if art == "" || r.Belegart == art {
			l = append(l, r)
		}
	}
	sort.SliceStable(l, func(i, j int) bool {
		if l[i].Datum != l[j].Datum {
			return l[i].Datum > l[j].Datum
		}
		return l[i].Erstellt > l[j].Erstellt
	})
	return l, nil
}

// Beleg liefert einen Zusatzbeleg.
func (s *Speicher) Beleg(id string) (modell.Rechnung, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.beleg(id)
}

func (s *Speicher) beleg(id string) (modell.Rechnung, error) {
	var r modell.Rechnung
	if !gueltigeID(id) {
		return r, ErrNichtGefunden
	}
	err := lies(s.pfad("belege", id+".json"), &r)
	if errors.Is(err, os.ErrNotExist) {
		return r, ErrNichtGefunden
	}
	return r, err
}

// SpeichereBeleg legt einen Zusatzbeleg an oder ändert ihn. Beim ersten
// Speichern bekommt er seine Nummer aus dem eigenen Nummernkreis. Anders
// als eine fertige Rechnung bleibt ein Angebot oder Lieferschein änderbar.
func (s *Speicher) SpeichereBeleg(r modell.Rechnung) (modell.Rechnung, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if r.Belegart != modell.BelegAngebot && r.Belegart != modell.BelegLieferschein {
		return r, errors.New("unbekannte Belegart")
	}
	if r.ID == "" {
		r.ID = NeueID()
		r.Erstellt = Jetzt()
	} else if !gueltigeID(r.ID) {
		return r, errors.New("ungültige Kennung")
	} else if alt, err := s.beleg(r.ID); err == nil {
		r.Erstellt = alt.Erstellt
		if r.Nummer == "" {
			r.Nummer = alt.Nummer
		}
	}
	if r.Status == "" || r.Status == modell.StatusEntwurf {
		r.Status = modell.StatusOffen
	}
	r.Nummer = strings.TrimSpace(r.Nummer)
	alle, err := s.belege(r.Belegart)
	if err != nil {
		return r, err
	}
	vergeben := map[string]bool{}
	for _, a := range alle {
		if a.ID != r.ID {
			vergeben[strings.ToLower(a.Nummer)] = true
		}
	}
	if r.Nummer == "" {
		e, err := s.einstellungen()
		if err != nil {
			return r, err
		}
		datum, err := time.Parse("2006-01-02", r.Datum)
		if err != nil {
			datum = time.Now()
		}
		muster, zaehler := &e.Zusatz.AngebotMuster, &e.Zusatz.AngebotNaechste
		vorgabe := "AN-{JJJJ}-{NNNN}"
		if r.Belegart == modell.BelegLieferschein {
			muster, zaehler, vorgabe = &e.Zusatz.LieferscheinMuster, &e.Zusatz.LieferscheinNaechste, "LS-{JJJJ}-{NNNN}"
		}
		if strings.TrimSpace(*muster) == "" {
			*muster = vorgabe
		}
		if *zaehler < 1 {
			*zaehler = 1
		}
		for {
			r.Nummer = Nummer(*muster, datum, *zaehler)
			*zaehler++
			if !vergeben[strings.ToLower(r.Nummer)] {
				break
			}
		}
		if err := schreibeJSON(s.pfad("einstellungen.json"), e); err != nil {
			return r, err
		}
	} else if vergeben[strings.ToLower(r.Nummer)] {
		return r, fmt.Errorf("die Nummer %s ist bereits vergeben", r.Nummer)
	}
	r.Geaendert = Jetzt()
	r.Dateien, r.Versand, r.Mahnungen = nil, nil, nil
	if err := os.MkdirAll(s.pfad("belege"), 0o755); err != nil {
		return r, err
	}
	return r, schreibeJSON(s.pfad("belege", r.ID+".json"), r)
}

// LoescheBeleg entfernt einen Zusatzbeleg.
func (s *Speicher) LoescheBeleg(id string) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	if !gueltigeID(id) {
		return ErrNichtGefunden
	}
	err := os.Remove(s.pfad("belege", id+".json"))
	if errors.Is(err, os.ErrNotExist) {
		return ErrNichtGefunden
	}
	return err
}

// Serien liefert die Serienrechnungen.
func (s *Speicher) Serien() ([]modell.Serie, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.serien()
}

func (s *Speicher) serien() ([]modell.Serie, error) {
	l := []modell.Serie{}
	err := lies(s.pfad("serien.json"), &l)
	if errors.Is(err, os.ErrNotExist) || errors.Is(err, ErrNichtGefunden) {
		return []modell.Serie{}, nil // noch keine Serie angelegt
	}
	return l, err
}

// SpeichereSerie legt eine Serienrechnung an oder ändert sie.
func (s *Speicher) SpeichereSerie(neu modell.Serie) (modell.Serie, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	l, err := s.serien()
	if err != nil {
		return neu, err
	}
	if neu.ID == "" {
		neu.ID = NeueID()
		l = append(l, neu)
	} else {
		gefunden := false
		for i := range l {
			if l[i].ID == neu.ID {
				l[i], gefunden = neu, true
			}
		}
		if !gefunden {
			return neu, ErrNichtGefunden
		}
	}
	return neu, schreibeJSON(s.pfad("serien.json"), l)
}

// LoescheSerie entfernt eine Serienrechnung. Schon angelegte Rechnungen
// bleiben.
func (s *Speicher) LoescheSerie(id string) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	l, err := s.serien()
	if err != nil {
		return err
	}
	rest := l[:0]
	for _, x := range l {
		if x.ID != id {
			rest = append(rest, x)
		}
	}
	return schreibeJSON(s.pfad("serien.json"), rest)
}

// SchreibeDatei legt eine Datei im Ordner Rechnungen ab, zum Beispiel eine
// Mahnung. Eine vorhandene Datei wird nicht überschrieben.
func (s *Speicher) SchreibeDatei(rel string, daten []byte) error {
	sauber := filepath.Clean(filepath.FromSlash(rel))
	if filepath.IsAbs(sauber) || strings.HasPrefix(sauber, "..") {
		return errors.New("ungültiger Dateiname")
	}
	ziel := filepath.Join(s.AusgangOrdner(), sauber)
	if _, err := os.Stat(ziel); err == nil {
		return fmt.Errorf("die Datei %s gibt es schon", rel)
	}
	if err := os.MkdirAll(filepath.Dir(ziel), 0o755); err != nil {
		return err
	}
	return schreibe(ziel, daten)
}
