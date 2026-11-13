package speicher

import (
	"archive/zip"
	"bytes"
	"os"
	"path/filepath"
	"testing"
	"time"

	"marktrechnung/internal/modell"
)

func TestNummer(t *testing.T) {
	tag := time.Date(2026, 3, 5, 0, 0, 0, 0, time.UTC)
	faelle := map[string]string{
		"RE-{JJJJ}-{NNNN}": "RE-2026-0042", "{JJ}{MM}-{NNN}": "2603-042", "R{N}": "R42", "Rechnung-": "Rechnung-0042",
	}
	for muster, soll := range faelle {
		if ist := Nummer(muster, tag, 42); ist != soll {
			t.Errorf("Nummer(%q) = %q, erwartet %q", muster, ist, soll)
		}
	}
}

func TestFertigstellen(t *testing.T) {
	s, err := Oeffne(t.TempDir())
	if err != nil {
		t.Fatal(err)
	}
	erzeuge := func(r modell.Rechnung) ([]Datei, error) {
		return []Datei{{Name: "Rechnung_" + r.Nummer + ".pdf", Daten: []byte("pdf " + r.Nummer)}}, nil
	}
	entwurf, err := s.SpeichereEntwurf(modell.Rechnung{Datum: "2026-11-13"})
	if err != nil || entwurf.ID == "" || entwurf.Status != modell.StatusEntwurf {
		t.Fatalf("Entwurf: %+v %v", entwurf, err)
	}
	r1, err := s.Fertigstellen(entwurf, erzeuge)
	if err != nil {
		t.Fatal(err)
	}
	if r1.Nummer != "RE-2026-0001" || r1.Status != modell.StatusFertig || len(r1.Dateien) != 1 || r1.Zahlung.Verwendungszweck != r1.Nummer {
		t.Errorf("erste Rechnung: %+v", r1)
	}
	daten, err := s.LiesDatei(r1.Dateien[0])
	if err != nil || string(daten) != "pdf RE-2026-0001" {
		t.Errorf("Datei: %q %v", daten, err)
	}
	// Fertige Rechnungen sind nicht mehr änderbar und nicht löschbar.
	if _, err := s.SpeichereEntwurf(r1); err != ErrFestgeschrieben {
		t.Errorf("Überschreiben: %v", err)
	}
	if err := s.LoescheEntwurf(r1.ID); err != ErrFestgeschrieben {
		t.Errorf("Löschen: %v", err)
	}
	if _, err := s.Fertigstellen(r1, erzeuge); err != ErrFestgeschrieben {
		t.Errorf("doppeltes Fertigstellen: %v", err)
	}
	// Die nächste Rechnung bekommt die nächste Nummer; eine von Hand
	// vergebene Nummer darf nicht doppelt vorkommen.
	r2, err := s.Fertigstellen(modell.Rechnung{Datum: "2026-11-14"}, erzeuge)
	if err != nil || r2.Nummer != "RE-2026-0002" {
		t.Errorf("zweite Rechnung: %s %v", r2.Nummer, err)
	}
	if _, err := s.Fertigstellen(modell.Rechnung{Datum: "2026-11-14", Nummer: "re-2026-0002"}, erzeuge); err == nil {
		t.Error("doppelte Nummer wurde angenommen")
	}
	r3, err := s.Fertigstellen(modell.Rechnung{Datum: "2026-11-14", Nummer: "LX-77"}, erzeuge)
	if err != nil || r3.Nummer != "LX-77" {
		t.Errorf("eigene Nummer: %s %v", r3.Nummer, err)
	}
	// Im neuen Jahr beginnt die Zählung wieder bei 1.
	r4, err := s.Fertigstellen(modell.Rechnung{Datum: "2027-01-02"}, erzeuge)
	if err != nil || r4.Nummer != "RE-2027-0001" {
		t.Errorf("neues Jahr: %s %v", r4.Nummer, err)
	}
	if _, err := os.Stat(filepath.Join(s.AusgangOrdner(), "2027", "Rechnung_RE-2027-0001.pdf")); err != nil {
		t.Error(err)
	}
	alle, _ := s.Rechnungen()
	if len(alle) != 4 || alle[0].Nummer != "RE-2027-0001" {
		t.Errorf("Liste: %d Einträge", len(alle))
	}
	if _, err := s.LiesDatei("../daten/einstellungen.json"); err == nil {
		t.Error("Zugriff außerhalb des Rechnungsordners war möglich")
	}
	if _, err := s.Rechnung("../einstellungen"); err == nil {
		t.Error("ungültige Kennung wurde angenommen")
	}

	var b bytes.Buffer
	if err := s.Sicherung(&b); err != nil {
		t.Fatal(err)
	}
	z, err := zip.NewReader(bytes.NewReader(b.Bytes()), int64(b.Len()))
	if err != nil || len(z.File) < 9 {
		t.Errorf("Sicherung: %v, %d Dateien", err, len(z.File))
	}
}

func TestKundenUndArtikel(t *testing.T) {
	s, _ := Oeffne(t.TempDir())
	k, err := s.SpeichereKunde(modell.Kunde{Partei: modell.Partei{Name: "Zebra Markt"}, Lieferorte: []modell.Lieferort{{Name: "Filiale 1"}}})
	if err != nil || k.ID == "" || k.Lieferorte[0].ID == "" {
		t.Fatalf("Kunde: %+v %v", k, err)
	}
	s.SpeichereKunde(modell.Kunde{Partei: modell.Partei{Name: "Apfel Markt"}})
	k.Name = "Zebra Markt GmbH"
	s.SpeichereKunde(k)
	l, _ := s.Kunden()
	if len(l) != 2 || l[0].Name != "Apfel Markt" || l[1].Name != "Zebra Markt GmbH" {
		t.Errorf("Kunden: %+v", l)
	}
	if err := s.LoescheKunde(k.ID); err != nil {
		t.Fatal(err)
	}
	if l, _ = s.Kunden(); len(l) != 1 {
		t.Errorf("nach dem Löschen: %d", len(l))
	}
	a, _ := s.SpeichereArtikel(modell.Artikel{Bezeichnung: "Honig"})
	if al, _ := s.Artikel(); len(al) != 1 || al[0].ID != a.ID {
		t.Errorf("Artikel: %+v", al)
	}
	e, _ := s.Einstellungen()
	if e.Nummernmuster == "" || e.Firma.Land != "DE" {
		t.Errorf("Vorgaben fehlen: %+v", e)
	}
}
