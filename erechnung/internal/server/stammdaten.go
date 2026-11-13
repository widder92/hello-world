package server

import (
	"errors"
	"net/http"
	"strings"

	"marktrechnung/internal/modell"
	"marktrechnung/internal/post"
)

// Stammdaten aus einer Datei einlesen: eigene Firmendaten, Kunden und ihre
// Märkte. Gedacht für den Umzug aus einem anderen Werkzeug oder von einem
// anderen Rechner. Vorhandenes wird nicht überschrieben - gefüllt wird nur,
// was noch leer ist; neue Kunden und Märkte kommen dazu.
//
// Aufbau der Datei (JSON):
//
//	{ "firma":  { "gln": "...", "name": "...", ... },
//	  "kunden": [ { "name": "...", "gln": "...", "profil": "edeka",
//	                "lieferorte": [ { "name": "...", "gln": "...", "strasse": "...", "plz": "...", "ort": "..." } ] } ] }
//
// Die Feldnamen sind dieselben wie in den Dateien kunden.json und
// einstellungen.json im Datenordner.
func (s *Server) stammdatenEinlesen(w http.ResponseWriter, r *http.Request) {
	var ein struct {
		Firma  *modell.Partei `json:"firma"`
		Kunden []modell.Kunde `json:"kunden"`
	}
	if err := liesJSON(r, &ein); err != nil {
		fehler(w, 400, errors.New("die Datei ist keine Stammdaten-Datei von Marktrechnung (JSON): "+err.Error()))
		return
	}
	if ein.Firma == nil && len(ein.Kunden) == 0 {
		fehler(w, 422, errors.New("die Datei enthält weder Firmendaten noch Kunden"))
		return
	}
	ergebnis := map[string]int{"kundenNeu": 0, "kundenErgaenzt": 0, "maerkteNeu": 0, "firma": 0}

	if f := ein.Firma; f != nil {
		e, err := s.Speicher.Einstellungen()
		if err != nil {
			fehler(w, 500, err)
			return
		}
		z := &e.Firma
		geaendert := false
		for _, paar := range []struct {
			ziel *string
			wert string
		}{{&z.Name, f.Name}, {&z.Handelsname, f.Handelsname}, {&z.Strasse, f.Strasse}, {&z.PLZ, f.PLZ}, {&z.Ort, f.Ort},
			{&z.UStID, f.UStID}, {&z.Steuernummer, f.Steuernummer}, {&z.GLN, strings.ReplaceAll(f.GLN, " ", "")}, {&z.Register, f.Register},
			{&z.Ansprechpartner, f.Ansprechpartner}, {&z.Telefon, f.Telefon}, {&z.Email, f.Email}} {
			if fuelleLeer(paar.ziel, paar.wert) {
				geaendert = true
			}
		}
		if geaendert {
			if err := s.Speicher.SetzeEinstellungen(e); err != nil {
				fehler(w, 500, err)
				return
			}
			ergebnis["firma"] = 1
		}
	}

	kunden, err := s.Speicher.Kunden()
	if err != nil {
		fehler(w, 500, err)
		return
	}
	for _, f := range ein.Kunden {
		f.ID = ""
		saeubere(&f.Partei)
		if f.Name == "" {
			continue
		}
		if f.RechnungsEmail != "" {
			if _, err := post.Adressen(f.RechnungsEmail); err != nil {
				f.RechnungsEmail = ""
			}
		}
		for i := range f.Lieferorte {
			l := &f.Lieferorte[i]
			l.ID = ""
			l.GLN = strings.ReplaceAll(strings.TrimSpace(l.GLN), " ", "")
			if l.Land == "" {
				l.Land = "DE"
			}
		}
		k := findeKunde(kunden, f.Partei)
		if k == nil {
			ergebnis["maerkteNeu"] += len(f.Lieferorte)
			neu, err := s.Speicher.SpeichereKunde(f)
			if err != nil {
				fehler(w, 500, err)
				return
			}
			kunden = append(kunden, neu)
			ergebnis["kundenNeu"]++
			continue
		}
		geaendert := false
		for _, paar := range []struct {
			ziel *string
			wert string
		}{{&k.Strasse, f.Strasse}, {&k.Zusatz, f.Zusatz}, {&k.PLZ, f.PLZ}, {&k.Ort, f.Ort}, {&k.UStID, f.UStID}, {&k.GLN, f.GLN},
			{&k.Kennung, f.Kennung}, {&k.Lieferantennummer, f.Lieferantennummer}, {&k.Kaeuferreferenz, f.Kaeuferreferenz},
			{&k.RechnungsEmail, f.RechnungsEmail}, {&k.Profil, f.Profil}, {&k.Format, f.Format}, {&k.Notiz, f.Notiz}, {&k.Alias, f.Alias}, {&k.Debitorenkonto, f.Debitorenkonto}} {
			if fuelleLeer(paar.ziel, paar.wert) {
				geaendert = true
			}
		}
		for _, l := range f.Lieferorte {
			vorhanden := findeLieferort(*k, l)
			if vorhanden == nil {
				k.Lieferorte = append(k.Lieferorte, l)
				ergebnis["maerkteNeu"]++
				geaendert = true
				continue
			}
			for _, paar := range []struct {
				ziel *string
				wert string
			}{{&vorhanden.GLN, l.GLN}, {&vorhanden.Kennung, l.Kennung}, {&vorhanden.Strasse, l.Strasse}, {&vorhanden.PLZ, l.PLZ},
				{&vorhanden.Ort, l.Ort}, {&vorhanden.Kostenstelle, l.Kostenstelle}, {&vorhanden.Kundennummer, l.Kundennummer}} {
				if fuelleLeer(paar.ziel, paar.wert) {
					geaendert = true
				}
			}
		}
		if geaendert {
			gespeichert, err := s.Speicher.SpeichereKunde(*k)
			if err != nil {
				fehler(w, 500, err)
				return
			}
			*k = gespeichert
			ergebnis["kundenErgaenzt"]++
		}
	}
	antworte(w, 200, ergebnis)
}
