// Package speicher legt alle Daten als lesbare Dateien in einem Ordner ab:
// Einstellungen, Kunden, Artikel und Rechnungen als JSON, die fertigen
// Rechnungsdateien nach Jahren sortiert. So lässt sich der Ordner einfach
// sichern, und die Rechnungen bleiben auch ohne das Programm zugänglich.
package speicher

import (
	"archive/zip"
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"io/fs"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"sync"
	"time"

	"marktrechnung/internal/dec"
	"marktrechnung/internal/modell"
)

// ErrNichtGefunden bedeutet: Den Datensatz gibt es nicht.
var ErrNichtGefunden = errors.New("nicht gefunden")

// Speicher ist der geöffnete Datenordner.
type Speicher struct {
	ordner string
	mu     sync.Mutex
}

// Datei ist eine Ausgabedatei, die beim Fertigstellen abgelegt wird.
type Datei struct {
	Name  string
	Daten []byte
}

// Oeffne öffnet den Datenordner und legt ihn bei Bedarf an.
func Oeffne(ordner string) (*Speicher, error) {
	for _, u := range []string{"daten/rechnungen", "daten/versand", "Rechnungen"} {
		if err := os.MkdirAll(filepath.Join(ordner, filepath.FromSlash(u)), 0o755); err != nil {
			return nil, fmt.Errorf("der Datenordner %s kann nicht angelegt werden: %w", ordner, err)
		}
	}
	return &Speicher{ordner: ordner}, nil
}

// Ordner liefert den Pfad des Datenordners.
func (s *Speicher) Ordner() string { return s.ordner }

// AusgangOrdner liefert den Ordner mit den fertigen Rechnungsdateien.
func (s *Speicher) AusgangOrdner() string { return filepath.Join(s.ordner, "Rechnungen") }

func (s *Speicher) pfad(teile ...string) string {
	return filepath.Join(append([]string{s.ordner, "daten"}, teile...)...)
}

// NeueID erzeugt eine zufällige Kennung.
func NeueID() string {
	b := make([]byte, 8)
	rand.Read(b)
	return hex.EncodeToString(b)
}

// Jetzt liefert den Zeitstempel für die Daten.
func Jetzt() string { return time.Now().Format("2006-01-02T15:04:05") }

func lies(pfad string, ziel any) error {
	daten, err := os.ReadFile(pfad)
	if errors.Is(err, fs.ErrNotExist) {
		return ErrNichtGefunden
	}
	if err != nil {
		return err
	}
	if err := json.Unmarshal(daten, ziel); err != nil {
		return fmt.Errorf("die Datei %s ist beschädigt: %w", filepath.Base(pfad), err)
	}
	return nil
}

// schreibe legt eine Datei sicher ab: erst vollständig unter anderem Namen,
// dann umbenennen. Bricht der Rechner dabei ab, bleibt die alte Fassung
// erhalten.
func schreibe(pfad string, daten []byte) error {
	vorlaeufig := pfad + ".neu"
	f, err := os.Create(vorlaeufig)
	if err != nil {
		return err
	}
	if _, err = f.Write(daten); err == nil {
		err = f.Sync()
	}
	if e := f.Close(); err == nil {
		err = e
	}
	if err != nil {
		os.Remove(vorlaeufig)
		return err
	}
	return os.Rename(vorlaeufig, pfad)
}

func schreibeJSON(pfad string, wert any) error {
	daten, err := json.MarshalIndent(wert, "", "  ")
	if err != nil {
		return err
	}
	return schreibe(pfad, daten)
}

// ---------------------------------------------------------------------------
// Einstellungen

// Einstellungen liest die Einstellungen; fehlen sie, kommen Vorgaben zurück.
func (s *Speicher) Einstellungen() (modell.Einstellungen, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.einstellungen()
}

func (s *Speicher) einstellungen() (modell.Einstellungen, error) {
	e := modell.Einstellungen{
		Nummernmuster: "RE-{JJJJ}-{NNNN}", NaechsteNummer: 1, JaehrlichNeu: true, Zieltage: 14,
		StandardSteuer: dec.Int(7), StandardEinheit: "H87", Format: modell.FormatZugferd,
		Einleitung: "Wir berechnen Ihnen die folgende Lieferung:",
	}
	e.Firma.Land = "DE"
	e.Firma.EAdresseSchema = "EM"
	e.Post.Port, e.Post.Sicherheit, e.Post.KopieAnMich = 465, "tls", true
	err := lies(s.pfad("einstellungen.json"), &e)
	if errors.Is(err, ErrNichtGefunden) {
		err = nil
	}
	return e, err
}

// SetzeEinstellungen speichert die Einstellungen.
func (s *Speicher) SetzeEinstellungen(e modell.Einstellungen) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	return schreibeJSON(s.pfad("einstellungen.json"), e)
}

// Logo liefert das hinterlegte Firmenlogo oder nil.
func (s *Speicher) Logo() []byte {
	e, _ := s.Einstellungen()
	if e.LogoDatei == "" {
		return nil
	}
	daten, _ := os.ReadFile(s.pfad(filepath.Base(e.LogoDatei)))
	return daten
}

// SetzeLogo speichert das Logo; leere Daten löschen es.
func (s *Speicher) SetzeLogo(daten []byte, endung string) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	e, err := s.einstellungen()
	if err != nil {
		return err
	}
	if e.LogoDatei != "" {
		os.Remove(s.pfad(filepath.Base(e.LogoDatei)))
		e.LogoDatei = ""
	}
	if len(daten) > 0 {
		e.LogoDatei = "logo" + endung
		if err := schreibe(s.pfad(e.LogoDatei), daten); err != nil {
			return err
		}
	}
	return schreibeJSON(s.pfad("einstellungen.json"), e)
}

// ---------------------------------------------------------------------------
// Kunden und Artikel

// Kunden liefert alle Kunden, nach Namen sortiert.
func (s *Speicher) Kunden() ([]modell.Kunde, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.kunden()
}

func (s *Speicher) kunden() ([]modell.Kunde, error) {
	l := []modell.Kunde{}
	err := lies(s.pfad("kunden.json"), &l)
	if errors.Is(err, ErrNichtGefunden) {
		err = nil
	}
	sort.SliceStable(l, func(i, j int) bool { return strings.ToLower(l[i].Name) < strings.ToLower(l[j].Name) })
	return l, err
}

// Kunde liefert einen Kunden.
func (s *Speicher) Kunde(id string) (modell.Kunde, error) {
	l, err := s.Kunden()
	if err != nil {
		return modell.Kunde{}, err
	}
	for _, k := range l {
		if k.ID == id {
			return k, nil
		}
	}
	return modell.Kunde{}, ErrNichtGefunden
}

// SpeichereKunde legt einen Kunden an oder ersetzt ihn.
func (s *Speicher) SpeichereKunde(k modell.Kunde) (modell.Kunde, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	l, err := s.kunden()
	if err != nil {
		return k, err
	}
	if k.ID == "" {
		k.ID = NeueID()
	}
	for i := range k.Lieferorte {
		if k.Lieferorte[i].ID == "" {
			k.Lieferorte[i].ID = NeueID()
		}
	}
	if k.Lieferorte == nil {
		k.Lieferorte = []modell.Lieferort{}
	}
	ersetzt := false
	for i := range l {
		if l[i].ID == k.ID {
			l[i], ersetzt = k, true
		}
	}
	if !ersetzt {
		l = append(l, k)
	}
	return k, schreibeJSON(s.pfad("kunden.json"), l)
}

// LoescheKunde entfernt einen Kunden. Bestehende Rechnungen behalten ihre
// Kopie der Anschrift.
func (s *Speicher) LoescheKunde(id string) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	l, err := s.kunden()
	if err != nil {
		return err
	}
	neu := l[:0]
	for _, k := range l {
		if k.ID != id {
			neu = append(neu, k)
		}
	}
	return schreibeJSON(s.pfad("kunden.json"), neu)
}

// Artikel liefert alle Artikel, nach Bezeichnung sortiert.
func (s *Speicher) Artikel() ([]modell.Artikel, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.artikel()
}

func (s *Speicher) artikel() ([]modell.Artikel, error) {
	l := []modell.Artikel{}
	err := lies(s.pfad("artikel.json"), &l)
	if errors.Is(err, ErrNichtGefunden) {
		err = nil
	}
	sort.SliceStable(l, func(i, j int) bool { return strings.ToLower(l[i].Bezeichnung) < strings.ToLower(l[j].Bezeichnung) })
	return l, err
}

// SpeichereArtikel legt einen Artikel an oder ersetzt ihn.
func (s *Speicher) SpeichereArtikel(a modell.Artikel) (modell.Artikel, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	l, err := s.artikel()
	if err != nil {
		return a, err
	}
	if a.ID == "" {
		a.ID = NeueID()
	}
	ersetzt := false
	for i := range l {
		if l[i].ID == a.ID {
			l[i], ersetzt = a, true
		}
	}
	if !ersetzt {
		l = append(l, a)
	}
	return a, schreibeJSON(s.pfad("artikel.json"), l)
}

// LoescheArtikel entfernt einen Artikel.
func (s *Speicher) LoescheArtikel(id string) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	l, err := s.artikel()
	if err != nil {
		return err
	}
	neu := l[:0]
	for _, a := range l {
		if a.ID != id {
			neu = append(neu, a)
		}
	}
	return schreibeJSON(s.pfad("artikel.json"), neu)
}

// ---------------------------------------------------------------------------
// Rechnungen

func gueltigeID(id string) bool {
	if id == "" || len(id) > 40 {
		return false
	}
	for _, c := range id {
		if !(c >= 'a' && c <= 'z' || c >= '0' && c <= '9' || c == '-') {
			return false
		}
	}
	return true
}

// Rechnungen liefert alle Rechnungen, die neuesten zuerst.
func (s *Speicher) Rechnungen() ([]modell.Rechnung, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.rechnungen()
}

func (s *Speicher) rechnungen() ([]modell.Rechnung, error) {
	eintraege, err := os.ReadDir(s.pfad("rechnungen"))
	if err != nil {
		return nil, err
	}
	l := []modell.Rechnung{}
	for _, e := range eintraege {
		if e.IsDir() || !strings.HasSuffix(e.Name(), ".json") {
			continue
		}
		var r modell.Rechnung
		if err := lies(s.pfad("rechnungen", e.Name()), &r); err != nil {
			return nil, err
		}
		l = append(l, r)
	}
	sort.SliceStable(l, func(i, j int) bool {
		if l[i].Datum != l[j].Datum {
			return l[i].Datum > l[j].Datum
		}
		return l[i].Erstellt > l[j].Erstellt
	})
	return l, nil
}

// Rechnung liefert eine Rechnung.
func (s *Speicher) Rechnung(id string) (modell.Rechnung, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.rechnung(id)
}

func (s *Speicher) rechnung(id string) (modell.Rechnung, error) {
	var r modell.Rechnung
	if !gueltigeID(id) {
		return r, ErrNichtGefunden
	}
	err := lies(s.pfad("rechnungen", id+".json"), &r)
	return r, err
}

// ErrFestgeschrieben bedeutet: Die Rechnung ist fertiggestellt und darf
// nicht mehr verändert werden.
var ErrFestgeschrieben = errors.New("die Rechnung ist fertiggestellt und kann nicht mehr geändert werden. Für eine Berichtigung bitte eine Rechnungskorrektur ausstellen")

// SpeichereEntwurf legt einen Entwurf an oder ersetzt ihn. Fertiggestellte
// Rechnungen lassen sich so nicht überschreiben.
func (s *Speicher) SpeichereEntwurf(r modell.Rechnung) (modell.Rechnung, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if r.ID == "" {
		r.ID = NeueID()
		r.Erstellt = Jetzt()
	} else if !gueltigeID(r.ID) {
		return r, errors.New("ungültige Kennung")
	} else if alt, err := s.rechnung(r.ID); err == nil {
		if alt.Status != modell.StatusEntwurf {
			return r, ErrFestgeschrieben
		}
		r.Erstellt = alt.Erstellt
	}
	r.Status = modell.StatusEntwurf
	r.Geaendert = Jetzt()
	r.Dateien, r.Versand, r.Fertiggestellt = nil, nil, ""
	return r, schreibeJSON(s.pfad("rechnungen", r.ID+".json"), r)
}

// LoescheEntwurf entfernt einen Entwurf.
func (s *Speicher) LoescheEntwurf(id string) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	r, err := s.rechnung(id)
	if err != nil {
		return err
	}
	if r.Status != modell.StatusEntwurf {
		return ErrFestgeschrieben
	}
	return os.Remove(s.pfad("rechnungen", id+".json"))
}

// Aendere wendet eine Änderung auf eine gespeicherte Rechnung an, etwa um
// den Versand zu vermerken. Der Inhalt einer fertigen Rechnung bleibt dabei
// Sache des Aufrufers - gedacht ist die Funktion für Status und Vermerke.
func (s *Speicher) Aendere(id string, f func(r *modell.Rechnung) error) (modell.Rechnung, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	r, err := s.rechnung(id)
	if err != nil {
		return r, err
	}
	if err := f(&r); err != nil {
		return r, err
	}
	r.Geaendert = Jetzt()
	return r, schreibeJSON(s.pfad("rechnungen", r.ID+".json"), r)
}

// Nummer bildet eine Rechnungsnummer aus Muster, Datum und laufender Zahl.
func Nummer(muster string, datum time.Time, laufend int) string {
	if !strings.Contains(muster, "{N") {
		muster += "{NNNN}"
	}
	var b strings.Builder
	for i := 0; i < len(muster); {
		if muster[i] != '{' {
			b.WriteByte(muster[i])
			i++
			continue
		}
		ende := strings.IndexByte(muster[i:], '}')
		if ende < 0 {
			b.WriteString(muster[i:])
			break
		}
		feld := muster[i+1 : i+ende]
		switch {
		case feld == "JJJJ":
			b.WriteString(datum.Format("2006"))
		case feld == "JJ":
			b.WriteString(datum.Format("06"))
		case feld == "MM":
			b.WriteString(datum.Format("01"))
		case feld != "" && strings.Trim(feld, "N") == "":
			fmt.Fprintf(&b, "%0*d", len(feld), laufend)
		default:
			b.WriteString(muster[i : i+ende+1])
		}
		i += ende + 1
	}
	return b.String()
}

// Fertigstellen schreibt eine Rechnung fest: Sie bekommt ihre Nummer, die
// Dateien werden erzeugt und abgelegt, danach ist sie nicht mehr änderbar.
// Die Funktion erzeuge baut aus der nummerierten Rechnung die Dateien.
func (s *Speicher) Fertigstellen(r modell.Rechnung, erzeuge func(modell.Rechnung) ([]Datei, error)) (modell.Rechnung, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if alt, err := s.rechnung(r.ID); err == nil && alt.Status != modell.StatusEntwurf {
		return r, ErrFestgeschrieben
	}
	alle, err := s.rechnungen()
	if err != nil {
		return r, err
	}
	vergeben := map[string]bool{}
	for _, a := range alle {
		if a.ID != r.ID && a.Nummer != "" && a.Status != modell.StatusEntwurf {
			vergeben[strings.ToLower(a.Nummer)] = true
		}
	}
	e, err := s.einstellungen()
	if err != nil {
		return r, err
	}
	datum, err := time.Parse("2006-01-02", r.Datum)
	if err != nil {
		return r, errors.New("das Rechnungsdatum ist ungültig")
	}
	zaehlerNeu := false
	r.Nummer = strings.TrimSpace(r.Nummer)
	if r.Nummer == "" {
		if e.JaehrlichNeu && e.NummerJahr != datum.Year() {
			e.NummerJahr, e.NaechsteNummer = datum.Year(), 1
		}
		if e.NaechsteNummer < 1 {
			e.NaechsteNummer = 1
		}
		for {
			r.Nummer = Nummer(e.Nummernmuster, datum, e.NaechsteNummer)
			e.NaechsteNummer++
			if !vergeben[strings.ToLower(r.Nummer)] {
				break
			}
		}
		zaehlerNeu = true
	} else if vergeben[strings.ToLower(r.Nummer)] {
		return r, fmt.Errorf("die Rechnungsnummer %s ist bereits vergeben", r.Nummer)
	}
	if r.Zahlung.Verwendungszweck == "" {
		r.Zahlung.Verwendungszweck = r.Nummer
	}
	if r.ID == "" {
		r.ID = NeueID()
		r.Erstellt = Jetzt()
	}

	dateien, err := erzeuge(r)
	if err != nil {
		return r, err
	}
	jahr := datum.Format("2006")
	if err := os.MkdirAll(filepath.Join(s.AusgangOrdner(), jahr), 0o755); err != nil {
		return r, err
	}
	r.Dateien = nil
	for _, d := range dateien {
		rel := filepath.Join(jahr, filepath.Base(d.Name))
		ziel := filepath.Join(s.AusgangOrdner(), rel)
		if _, err := os.Stat(ziel); err == nil {
			return r, fmt.Errorf("im Ordner Rechnungen/%s liegt bereits eine Datei %s. Sie wird nicht überschrieben", jahr, filepath.Base(d.Name))
		}
		if err := schreibe(ziel, d.Daten); err != nil {
			return r, err
		}
		r.Dateien = append(r.Dateien, filepath.ToSlash(rel))
	}
	r.Status = modell.StatusFertig
	r.Fertiggestellt = Jetzt()
	r.Geaendert = r.Fertiggestellt
	if err := schreibeJSON(s.pfad("rechnungen", r.ID+".json"), r); err != nil {
		return r, err
	}
	if zaehlerNeu {
		if err := schreibeJSON(s.pfad("einstellungen.json"), e); err != nil {
			return r, err
		}
	}
	return r, nil
}

// LiesDatei liest eine fertige Rechnungsdatei; rel ist der Pfad relativ zum
// Ordner Rechnungen, wie er in Rechnung.Dateien steht.
func (s *Speicher) LiesDatei(rel string) ([]byte, error) {
	sauber := filepath.Clean(filepath.FromSlash(rel))
	if filepath.IsAbs(sauber) || strings.HasPrefix(sauber, "..") {
		return nil, ErrNichtGefunden
	}
	return os.ReadFile(filepath.Join(s.AusgangOrdner(), sauber))
}

// MerkeVersand legt eine Kopie der verschickten E-Mail ab.
func (s *Speicher) MerkeVersand(id string, eml []byte) {
	name := fmt.Sprintf("%s_%s.eml", time.Now().Format("20060102-150405"), id)
	schreibe(s.pfad("versand", name), eml)
}

// Sicherung schreibt den ganzen Datenordner als ZIP-Archiv.
func (s *Speicher) Sicherung(w io.Writer) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	z := zip.NewWriter(w)
	err := filepath.WalkDir(s.ordner, func(pfad string, d fs.DirEntry, err error) error {
		if err != nil || d.IsDir() || strings.HasSuffix(pfad, ".neu") {
			return err
		}
		rel, _ := filepath.Rel(s.ordner, pfad)
		info, err := d.Info()
		if err != nil {
			return err
		}
		kopf, _ := zip.FileInfoHeader(info)
		kopf.Name = filepath.ToSlash(rel)
		kopf.Method = zip.Deflate
		ziel, err := z.CreateHeader(kopf)
		if err != nil {
			return err
		}
		quelle, err := os.Open(pfad)
		if err != nil {
			return err
		}
		defer quelle.Close()
		_, err = io.Copy(ziel, quelle)
		return err
	})
	if err != nil {
		return err
	}
	return z.Close()
}
