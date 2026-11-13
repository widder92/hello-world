// Marktrechnung erstellt, prüft und verschickt E-Rechnungen (ZUGFeRD und
// XRechnung) für Lieferanten des Lebensmittelhandels.
//
// Das Programm zeigt seine Oberfläche in einem eigenen Fenster. Dahinter
// läuft ein kleiner Server, der nur auf dem eigenen Rechner erreichbar ist.
// Alle Daten bleiben auf dem Rechner.
package main

import (
	"context"
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"log"
	"net"
	"net/http"
	"os"
	"os/signal"
	"path/filepath"
	"time"

	"marktrechnung/internal/geheim"
	"marktrechnung/internal/server"
	"marktrechnung/internal/speicher"
	"marktrechnung/internal/version"
	"marktrechnung/web"
)

// programm sind die Angaben, die das Programm sich zwischen zwei Starts
// merkt. Sie liegen im Einstellungsordner des Benutzerkontos.
type programm struct {
	Datenordner string `json:"datenordner"`
	Token       string `json:"token"`
	Port        int    `json:"port"`
}

const standardPort = 47411

// kiAdresse ersetzt in Tests die Adresse der Claude-Schnittstelle.
var kiAdresse string

// lexwareAdresse ersetzt in Tests die Adresse von Lexware Office.
var lexwareAdresse string

// imBrowser: die Oberfläche im gewohnten Browser öffnen.
var imBrowser bool

func main() {
	datenFlag := flag.String("daten", "", "Ordner für Rechnungen und Stammdaten (Standard: Dokumente/Marktrechnung)")
	portFlag := flag.Int("port", 0, "Port auf dem eigenen Rechner (Standard: 47411)")
	ohneBrowser := flag.Bool("kein-browser", false, "kein Fenster öffnen, nur die Adresse nennen")
	flag.BoolVar(&imBrowser, "browser", false, "die Oberfläche im gewohnten Browser öffnen statt in einem eigenen Fenster")
	zeigeVersion := flag.Bool("version", false, "Version anzeigen")
	beenden := flag.Bool("beenden", false, "ein laufendes Marktrechnung beenden (für das Setup)")
	flag.StringVar(&lexwareAdresse, "lexware-adresse", "", "andere Adresse der Lexware-Schnittstelle (nur für Tests)")
	flag.StringVar(&kiAdresse, "ki-adresse", "", "andere Adresse der Claude-Schnittstelle (nur für Tests)")
	fensterEigen := flag.String("fenster-eigen", "", "nur für den eigenen Gebrauch: das Programmfenster für diese Adresse zeigen")
	flag.Parse()
	if *fensterEigen != "" {
		// Der Fensterprozess: zeigt nur das Fenster. Das Protokoll geht an
		// das Hauptprogramm, das ihn gestartet hat.
		log.SetFlags(0)
		log.SetPrefix("Fenster: ")
		os.Exit(zeigeEigenesFenster(*fensterEigen, fensterOrdner()+"-eigen"))
	}
	if *zeigeVersion {
		fmt.Println("Marktrechnung", version.Nummer)
		return
	}
	if *beenden {
		beendeLaufendes()
		return
	}
	if err := lauf(*datenFlag, *portFlag, *ohneBrowser); err != nil {
		meldeFehler("Marktrechnung kann nicht starten:\n\n" + err.Error())
		os.Exit(1)
	}
}

func lauf(datenFlag string, portFlag int, ohneBrowser bool) error {
	konfig, err := os.UserConfigDir()
	if err != nil {
		return fmt.Errorf("der Einstellungsordner des Benutzerkontos ist nicht zu finden: %w", err)
	}
	konfig = filepath.Join(konfig, "Marktrechnung")
	if err := os.MkdirAll(konfig, 0o700); err != nil {
		return err
	}
	richteProtokollEin(konfig)

	var p programm
	merkdatei := filepath.Join(konfig, "programm.json")
	if daten, err := os.ReadFile(merkdatei); err == nil {
		json.Unmarshal(daten, &p)
	}
	geaendert := false
	if p.Token == "" {
		b := make([]byte, 24)
		if _, err := rand.Read(b); err != nil {
			return err
		}
		p.Token, geaendert = hex.EncodeToString(b), true
	}
	if datenFlag != "" {
		abs, err := filepath.Abs(datenFlag)
		if err != nil {
			return err
		}
		if p.Datenordner != abs {
			p.Datenordner, geaendert = abs, true
		}
	}
	if p.Datenordner == "" {
		p.Datenordner, geaendert = filepath.Join(dokumente(), "Marktrechnung"), true
	}
	if portFlag != 0 && p.Port != portFlag {
		p.Port, geaendert = portFlag, true
	}
	if p.Port == 0 {
		p.Port = standardPort
	}
	if geaendert {
		daten, _ := json.MarshalIndent(p, "", "  ")
		if err := os.WriteFile(merkdatei, daten, 0o600); err != nil {
			return err
		}
	}

	adresse := fmt.Sprintf("127.0.0.1:%d", p.Port)
	start := fmt.Sprintf("http://%s/?t=%s", adresse, p.Token)

	// Läuft das Programm schon, zeigt es auf Zuruf sein Fenster wieder.
	if laeuftSchon(adresse, p.Token) {
		if ohneBrowser {
			return nil
		}
		if !zeigeLaufendes(adresse, p.Token) {
			// Eine ältere Fassung kennt den Zuruf noch nicht.
			return oeffneBrowser(start)
		}
		return nil
	}

	sp, err := speicher.Oeffne(p.Datenordner)
	if err != nil {
		return err
	}
	horcher, err := net.Listen("tcp", adresse)
	if err != nil {
		// Der gewohnte Port ist von einem anderen Programm belegt: Dann
		// nimmt Marktrechnung für diesen Lauf einen freien.
		if horcher, err = net.Listen("tcp", "127.0.0.1:0"); err != nil {
			return fmt.Errorf("es lässt sich kein Anschluss auf dem eigenen Rechner öffnen: %w", err)
		}
		start = fmt.Sprintf("http://%s/?t=%s", horcher.Addr().String(), p.Token)
	}

	ctx, stopp := signal.NotifyContext(context.Background(), os.Interrupt)
	defer stopp()
	fenster := &fensterVerwalter{adresse: start, profil: fensterOrdner(), imBrowser: imBrowser, zu: stopp}
	if !imBrowser && !ohneBrowser {
		fenster.eigen = os.Getenv("MARKTRECHNUNG_FENSTER") != "app" && eigenesFensterVerfuegbar() && !eigenesFensterGesperrt(fenster.profil)
		if fenster.browser = findeBrowser(); fenster.browser == "" && !fenster.eigen {
			log.Print("Kein Browser mit App-Fenster gefunden (Edge, Chrome ...); die Oberfläche erscheint im gewohnten Browser.")
		}
	}
	s := &server.Server{
		Speicher: sp, Passwort: geheim.Neu(konfig, "postausgang.geheim"), Lexware: geheim.Neu(konfig, "lexware.geheim"),
		KI: geheim.Neu(konfig, "assistent.geheim"), KIBasis: kiAdresse,
		Web: web.Dateien(), Token: p.Token, Version: version.Nummer,
		Oeffne: oeffneOrdner, Beende: stopp, LexwareBasis: lexwareAdresse, Zeige: fenster.zeige,
	}
	// Die bebilderte Anleitung liegt neben dem Programm, wenn es über das
	// Setup oder aus dem Archiv kommt.
	if selbst, err := os.Executable(); err == nil {
		if pdf := filepath.Join(filepath.Dir(selbst), "Anleitung.pdf"); istDatei(pdf) {
			s.AnleitungPDF = pdf
		}
	}
	// Die Adresse merken, unter der das Programm gerade läuft. Das Setup
	// findet es darüber, um es vor einer Aktualisierung zu beenden.
	laufdatei := filepath.Join(konfig, laufdateiName)
	os.WriteFile(laufdatei, []byte(horcher.Addr().String()), 0o600)
	defer os.Remove(laufdatei)
	hs := &http.Server{Handler: s.Handler(), ReadHeaderTimeout: 10 * time.Second}
	fehler := make(chan error, 1)
	go func() { fehler <- hs.Serve(horcher) }()
	log.Printf("Marktrechnung %s läuft: http://%s  Daten: %s", version.Nummer, horcher.Addr(), p.Datenordner)
	if !ohneBrowser {
		if err := fenster.zeige(); err != nil {
			log.Printf("Das Fenster ließ sich nicht öffnen (%v). Bitte von Hand im Browser aufrufen: %s", err, start)
		}
	} else {
		log.Printf("Aufruf: %s", start)
	}

	select {
	case err := <-fehler:
		if !errors.Is(err, http.ErrServerClosed) {
			return err
		}
	case <-ctx.Done():
	}
	fenster.schliesse()
	ende, abbruch := context.WithTimeout(context.Background(), 5*time.Second)
	defer abbruch()
	return hs.Shutdown(ende)
}

// laeuftSchon fragt, ob unter der Adresse bereits Marktrechnung antwortet.
func laeuftSchon(adresse, token string) bool {
	anfrage, err := http.NewRequest("GET", "http://"+adresse+"/api/stamm", nil)
	if err != nil {
		return false
	}
	anfrage.AddCookie(&http.Cookie{Name: "marktrechnung", Value: token})
	c := &http.Client{Timeout: 2 * time.Second}
	antwort, err := c.Do(anfrage)
	if err != nil {
		return false
	}
	defer antwort.Body.Close()
	return antwort.StatusCode == http.StatusOK
}

const laufdateiName = "laeuft.txt"

// beendeLaufendes bittet ein laufendes Marktrechnung, sich zu beenden, und
// wartet, bis es das getan hat. Das Setup ruft dies vor dem Überschreiben
// und vor dem Entfernen der Programmdatei auf.
func beendeLaufendes() {
	konfig, err := os.UserConfigDir()
	if err != nil {
		return
	}
	konfig = filepath.Join(konfig, "Marktrechnung")
	var p programm
	if daten, err := os.ReadFile(filepath.Join(konfig, "programm.json")); err == nil {
		json.Unmarshal(daten, &p)
	}
	if p.Token == "" {
		return
	}
	if p.Port == 0 {
		p.Port = standardPort
	}
	adressen := []string{fmt.Sprintf("127.0.0.1:%d", p.Port)}
	if daten, err := os.ReadFile(filepath.Join(konfig, laufdateiName)); err == nil && len(daten) > 0 {
		adressen = append([]string{string(daten)}, adressen...)
	}
	for _, adresse := range adressen {
		if !laeuftSchon(adresse, p.Token) {
			continue
		}
		anfrage, err := http.NewRequest("POST", "http://"+adresse+"/api/beenden", nil)
		if err != nil {
			continue
		}
		anfrage.AddCookie(&http.Cookie{Name: "marktrechnung", Value: p.Token})
		anfrage.Header.Set("X-Marktrechnung", "1")
		c := &http.Client{Timeout: 3 * time.Second}
		if antwort, err := c.Do(anfrage); err == nil {
			antwort.Body.Close()
		}
		// Warten, bis der Anschluss wirklich frei ist (höchstens acht Sekunden).
		for i := 0; i < 40; i++ {
			verb, err := net.DialTimeout("tcp", adresse, 300*time.Millisecond)
			if err != nil {
				break
			}
			verb.Close()
			time.Sleep(200 * time.Millisecond)
		}
		// Windows gibt die Programmdatei erst kurz nach dem Ende frei.
		time.Sleep(400 * time.Millisecond)
		return
	}
}

// zeigeLaufendes bittet ein schon laufendes Marktrechnung, sein Fenster zu
// zeigen. So bleibt das Fenster in der Hand des Programms, das es auch
// wieder schließen kann.
func zeigeLaufendes(adresse, token string) bool {
	// Das eigene Fenster holt dieser Aufruf selbst nach vorn: Windows
	// erlaubt das dem Programm, das der Benutzer gerade gestartet hat.
	if holeFensterNachVorn() {
		return true
	}
	anfrage, err := http.NewRequest("POST", "http://"+adresse+"/api/fenster", nil)
	if err != nil {
		return false
	}
	anfrage.AddCookie(&http.Cookie{Name: "marktrechnung", Value: token})
	anfrage.Header.Set("X-Marktrechnung", "1")
	c := &http.Client{Timeout: 5 * time.Second}
	antwort, err := c.Do(anfrage)
	if err != nil {
		return false
	}
	antwort.Body.Close()
	return antwort.StatusCode == http.StatusOK
}

func richteProtokollEin(konfig string) {
	f, err := os.OpenFile(filepath.Join(konfig, "protokoll.txt"), os.O_CREATE|os.O_WRONLY|os.O_TRUNC, 0o600)
	if err == nil && !hatKonsole() {
		log.SetOutput(f)
	}
}

func istDatei(pfad string) bool {
	info, err := os.Stat(pfad)
	return err == nil && info.Mode().IsRegular()
}
