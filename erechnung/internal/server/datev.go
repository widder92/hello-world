package server

import (
	"archive/zip"
	"bytes"
	"errors"
	"fmt"
	"net/http"
	"path/filepath"
	"sort"
	"strings"
	"time"

	"marktrechnung/internal/datev"
	"marktrechnung/internal/dec"
	"marktrechnung/internal/modell"
)

// Übergabe an DATEV: Buchungsstapel für die Steuerkanzlei (DATEV-Format)
// und Belegpaket für DATEV Unternehmen online. Beides sind Dateien zum
// Herunterladen - eine Verbindung zu DATEV baut das Programm nicht auf.

type datevMonat struct {
	Monat  string `json:"monat"` // JJJJ-MM
	Anzahl int    `json:"anzahl"`
	Brutto dec.D  `json:"brutto"`
}

type datevUebersicht struct {
	Von      string       `json:"von"`
	Bis      string       `json:"bis"`
	Anzahl   int          `json:"anzahl"`
	Brutto   dec.D        `json:"brutto"`
	Monate   []datevMonat `json:"monate"`
	Hinweise []string     `json:"hinweise"`
	// Angaben: Was in den Einstellungen für den Buchungsstapel noch fehlt.
	Angaben []string `json:"angaben"`
	// MitKonten: Ein Kontenrahmen ist gewählt; das Belegpaket nennt Konten.
	MitKonten bool `json:"mitKonten"`
}

// datevZeitraum liest von und bis (JJJJ-MM); ohne Angabe gilt der Vormonat.
func datevZeitraum(r *http.Request) (von, bis string, err error) {
	von, bis = r.URL.Query().Get("von"), r.URL.Query().Get("bis")
	if von == "" {
		von = time.Now().AddDate(0, -1, 1-time.Now().Day()).Format("2006-01")
	}
	if bis == "" {
		bis = von
	}
	for _, m := range []string{von, bis} {
		if _, e := time.Parse("2006-01", m); e != nil {
			return "", "", errors.New("der Zeitraum ist nicht lesbar (erwartet JJJJ-MM)")
		}
	}
	if bis < von {
		von, bis = bis, von
	}
	return von, bis, nil
}

// datevBelege sammelt die fertigen Rechnungen des Zeitraums, älteste zuerst.
func (s *Server) datevBelege(von, bis string, mitDateien bool) ([]datev.Beleg, error) {
	alle, err := s.Speicher.Rechnungen()
	if err != nil {
		return nil, err
	}
	kunden, err := s.Speicher.Kunden()
	if err != nil {
		return nil, err
	}
	var l []datev.Beleg
	for _, re := range alle {
		if re.Status == modell.StatusEntwurf || re.Summen == nil || len(re.Datum) < 7 || re.Datum[:7] < von || re.Datum[:7] > bis {
			continue
		}
		b := datev.Beleg{Rechnung: re}
		for i := range kunden {
			if kunden[i].ID == re.KundeID {
				b.Kunde = &kunden[i]
			}
		}
		if mitDateien {
			for _, rel := range re.Dateien {
				if daten, err := s.Speicher.LiesDatei(rel); err == nil {
					b.Dateien = append(b.Dateien, datev.Datei{Name: filepath.Base(rel), Daten: daten})
				}
			}
		}
		l = append(l, b)
	}
	sort.SliceStable(l, func(i, j int) bool {
		a, b := l[i].Rechnung, l[j].Rechnung
		if a.Datum != b.Datum {
			return a.Datum < b.Datum
		}
		return a.Nummer < b.Nummer
	})
	return l, nil
}

func (s *Server) datevUebersicht(w http.ResponseWriter, r *http.Request) {
	von, bis, err := datevZeitraum(r)
	if err != nil {
		fehler(w, 400, err)
		return
	}
	e, err := s.Speicher.Einstellungen()
	if err != nil {
		fehler(w, 500, err)
		return
	}
	belege, err := s.datevBelege(von, bis, false)
	if err != nil {
		fehler(w, 500, err)
		return
	}
	erg := datevUebersicht{Von: von, Bis: bis, Monate: []datevMonat{}, Hinweise: []string{}, Angaben: datev.PruefeAngaben(e.Datev),
		MitKonten: datev.Vorgabe(e.Datev).Kontenrahmen != ""}
	if erg.Angaben == nil {
		erg.Angaben = []string{}
	}
	abschlagGemeldet := false
	for _, b := range belege {
		buchungen, hinweise := datev.Buchungen(b.Rechnung, b.Kunde, e.Datev)
		erg.Hinweise = append(erg.Hinweise, hinweise...)
		if b.Rechnung.Art == modell.ArtAbschlag && !abschlagGemeldet {
			abschlagGemeldet = true
			erg.Hinweise = append(erg.Hinweise, "Im Zeitraum liegen Abschlagsrechnungen. Sie werden wie Rechnungen auf das Erlöskonto gebucht; ob stattdessen „erhaltene Anzahlungen“ zu buchen sind, bitte mit der Steuerkanzlei klären.")
		}
		if len(b.Rechnung.Dateien) == 0 {
			erg.Hinweise = append(erg.Hinweise, "Zu Rechnung "+b.Rechnung.Nummer+" liegt keine Datei vor; ins Belegpaket kommen nur die Rechnungsdaten.")
		}
		var brutto dec.D
		for _, bu := range buchungen {
			brutto += bu.Brutto
		}
		monat := b.Rechnung.Datum[:7]
		if n := len(erg.Monate); n == 0 || erg.Monate[n-1].Monat != monat {
			erg.Monate = append(erg.Monate, datevMonat{Monat: monat})
		}
		m := &erg.Monate[len(erg.Monate)-1]
		m.Anzahl, m.Brutto = m.Anzahl+1, m.Brutto+brutto
		erg.Anzahl, erg.Brutto = erg.Anzahl+1, erg.Brutto+brutto
	}
	antworte(w, 200, erg)
}

func datevName(vorsatz, von, bis, endung string) string {
	if von == bis {
		return fmt.Sprintf("%s_%s.%s", vorsatz, von, endung)
	}
	return fmt.Sprintf("%s_%s_bis_%s.%s", vorsatz, von, bis, endung)
}

// datevBelegpaket liefert die ZIP-Datei für DATEV Belegtransfer.
func (s *Server) datevBelegpaket(w http.ResponseWriter, r *http.Request) {
	von, bis, err := datevZeitraum(r)
	if err != nil {
		fehler(w, 400, err)
		return
	}
	e, err := s.Speicher.Einstellungen()
	if err != nil {
		fehler(w, 500, err)
		return
	}
	belege, err := s.datevBelege(von, bis, true)
	if err != nil {
		fehler(w, 500, err)
		return
	}
	var b bytes.Buffer
	if _, err := datev.Belegpaket(&b, e.Datev, belege, time.Now()); err != nil {
		fehler(w, 422, err)
		return
	}
	w.Header().Set("Content-Type", "application/zip")
	w.Header().Set("Content-Disposition", `attachment; filename="`+datevName("DATEV-Belege", von, bis, "zip")+`"`)
	w.Write(b.Bytes())
}

// datevBuchungen liefert den Buchungsstapel: für einen Monat die CSV-Datei
// selbst, für mehrere Monate eine ZIP-Datei mit einer CSV-Datei je Monat.
func (s *Server) datevBuchungen(w http.ResponseWriter, r *http.Request) {
	von, bis, err := datevZeitraum(r)
	if err != nil {
		fehler(w, 400, err)
		return
	}
	e, err := s.Speicher.Einstellungen()
	if err != nil {
		fehler(w, 500, err)
		return
	}
	if l := datev.PruefeAngaben(e.Datev); len(l) > 0 {
		fehler(w, 422, errors.New("für den Buchungsstapel fehlen Angaben unter Einstellungen, DATEV: "+strings.Join(l, " ")))
		return
	}
	belege, err := s.datevBelege(von, bis, false)
	if err != nil {
		fehler(w, 500, err)
		return
	}
	jeMonat := map[string][]datev.Buchung{}
	var monate []string
	for _, b := range belege {
		buchungen, _ := datev.Buchungen(b.Rechnung, b.Kunde, e.Datev)
		if len(buchungen) == 0 {
			continue
		}
		monat := b.Rechnung.Datum[:7]
		if jeMonat[monat] == nil {
			monate = append(monate, monat)
		}
		jeMonat[monat] = append(jeMonat[monat], buchungen...)
	}
	if len(monate) == 0 {
		fehler(w, 422, errors.New("im gewählten Zeitraum gibt es keine fertige Rechnung"))
		return
	}
	type datei struct {
		name  string
		daten []byte
	}
	var dateien []datei
	jetzt := time.Now()
	for _, monat := range monate {
		t, _ := time.Parse("2006-01", monat)
		st := datev.Stapel{Datev: e.Datev, Jahr: t.Year(), Monat: int(t.Month()), Erzeugt: jetzt, Beleglink: r.URL.Query().Get("beleglink") == "1"}
		daten, err := datev.Buchungsstapel(st, jeMonat[monat])
		if err != nil {
			fehler(w, 422, err)
			return
		}
		dateien = append(dateien, datei{st.Dateiname(), daten})
	}
	if len(dateien) == 1 {
		w.Header().Set("Content-Type", "text/csv; charset=windows-1252")
		w.Header().Set("Content-Disposition", `attachment; filename="`+dateien[0].name+`"`)
		w.Write(dateien[0].daten)
		return
	}
	var b bytes.Buffer
	z := zip.NewWriter(&b)
	for _, d := range dateien {
		f, err := z.Create(d.name)
		if err == nil {
			_, err = f.Write(d.daten)
		}
		if err != nil {
			fehler(w, 500, err)
			return
		}
	}
	if err := z.Close(); err != nil {
		fehler(w, 500, err)
		return
	}
	w.Header().Set("Content-Type", "application/zip")
	w.Header().Set("Content-Disposition", `attachment; filename="`+datevName("DATEV-Buchungsstapel", von, bis, "zip")+`"`)
	w.Write(b.Bytes())
}
