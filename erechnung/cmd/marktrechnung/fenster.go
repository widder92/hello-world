package main

import (
	"errors"
	"io/fs"
	"log"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"sync"
	"time"

	"marktrechnung/internal/version"
)

// Das Programmfenster.
//
// Die Oberfläche ist mit Webtechnik gebaut. Unter Windows zeigt Marktrechnung
// sie in einem eigenen Fenster (siehe eigenfenster_windows_amd64.go): Titel,
// Symbol und Rahmen gehören dem Programm, von einem Browser ist nichts zu
// sehen.
//
// Wo das nicht geht - auf anderen Systemen, oder wenn dem Rechner die nötige
// Komponente fehlt -, erscheint die Oberfläche in einem "App-Fenster" des
// vorhandenen Browsers: ohne Adresszeile und Tabs, mit eigenem Profil,
// getrennt vom Surfen im Internet.
//
// Solange das Fenster offen ist, läuft das Programm; wird es geschlossen,
// beendet sich Marktrechnung.

// fensterVerwalter öffnet das Fenster und merkt, wann es geschlossen wird.
type fensterVerwalter struct {
	adresse   string // Start-Adresse der Oberfläche samt Kennwort
	profil    string // Ordner für das Profil des Fensters
	browser   string // Programmdatei des Browsers; leer = keiner gefunden
	imBrowser bool   // im gewohnten Browser öffnen statt im eigenen Fenster
	eigen     bool   // das eigene Fenster ist möglich (Windows)
	zu        func() // wird gerufen, wenn das Fenster geschlossen wurde

	mu        sync.Mutex
	prozess   *exec.Cmd
	ende      chan struct{} // geschlossen, sobald prozess beendet ist
	schliesst bool
	istEigen  bool // prozess ist das eigene Fenster
}

// Sperre: Ist das eigene Fenster abgestürzt, versucht es diese Fassung des
// Programms nicht noch einmal. Eine neue Fassung versucht es wieder.
func sperrdatei(profil string) string {
	return filepath.Join(filepath.Dir(profil), "eigenes-fenster-gesperrt.txt")
}

func eigenesFensterGesperrt(profil string) bool {
	daten, err := os.ReadFile(sperrdatei(profil))
	return err == nil && strings.TrimSpace(string(daten)) == version.Nummer
}

// zeigeEigen startet den Fensterprozess. Endet er ohne Fehler, hat der
// Benutzer das Fenster geschlossen; sonst zeigt Marktrechnung die Oberfläche
// auf dem anderen Weg. Der Aufrufer hält f.mu.
func (f *fensterVerwalter) zeigeEigen() bool {
	selbst, err := os.Executable()
	if err != nil {
		return false
	}
	cmd := exec.Command(selbst, "-fenster-eigen", f.adresse)
	cmd.Stderr = log.Writer()
	if err := cmd.Start(); err != nil {
		log.Printf("Das eigene Fenster ließ sich nicht starten: %v", err)
		return false
	}
	ende := make(chan struct{})
	f.prozess, f.ende, f.istEigen = cmd, ende, true
	gestartet := time.Now()
	go func() {
		cmd.Wait()
		close(ende)
		kennzahl := cmd.ProcessState.ExitCode()
		f.mu.Lock()
		schliesst := f.schliesst
		f.mu.Unlock()
		if schliesst {
			return
		}
		if kennzahl == 0 {
			log.Print("Das Programmfenster wurde geschlossen; Marktrechnung beendet sich.")
			f.zu()
			return
		}
		// Das Fenster kam nicht zustande oder ist abgestürzt.
		log.Printf("Das eigene Fenster endete mit der Kennzahl %d; die Oberfläche erscheint im Fenster des Browsers.", kennzahl)
		if kennzahl != endeKeinWebView && time.Since(gestartet) < time.Minute {
			os.MkdirAll(filepath.Dir(sperrdatei(f.profil)), 0o700)
			os.WriteFile(sperrdatei(f.profil), []byte(version.Nummer), 0o600)
		}
		f.mu.Lock()
		f.eigen, f.prozess, f.istEigen = false, nil, false
		f.mu.Unlock()
		if err := f.zeige(); err != nil {
			log.Printf("Das Fenster ließ sich nicht öffnen: %v", err)
		}
	}()
	return true
}

// findeBrowser sucht einen Browser, der App-Fenster beherrscht.
func findeBrowser() string {
	if pfad := os.Getenv("MARKTRECHNUNG_BROWSER"); pfad != "" {
		return pfad
	}
	for _, k := range browserKandidaten() {
		if k == "" {
			continue
		}
		if filepath.IsAbs(k) {
			if info, err := os.Stat(k); err == nil && !info.IsDir() {
				return k
			}
		} else if pfad, err := exec.LookPath(k); err == nil {
			return pfad
		}
	}
	return ""
}

// Voreinstellungen für das Profil des Fensters: Der Browser soll weder
// Passwörter speichern wollen noch eine Übersetzung anbieten.
const fensterVoreinstellungen = `{"credentials_enable_service":false,"profile":{"password_manager_enabled":false},` +
	`"translate":{"enabled":false},"browser":{"has_seen_welcome_page":true},"autofill":{"profile_enabled":false,"credit_card_enabled":false}}`

func (f *fensterVerwalter) bereiteProfilVor() {
	datei := filepath.Join(f.profil, "Default", "Preferences")
	if _, err := os.Stat(datei); err == nil {
		return
	}
	if os.MkdirAll(filepath.Dir(datei), 0o700) == nil {
		os.WriteFile(datei, []byte(fensterVoreinstellungen), 0o600)
	}
}

// profilBelegt meldet, ob ein Browser das Profil gerade benutzt, das Fenster
// also offen ist. Der Browser hält dafür eine Sperrdatei.
func (f *fensterVerwalter) profilBelegt() bool {
	for _, name := range []string{"lockfile", "SingletonLock"} {
		if _, err := os.Lstat(filepath.Join(f.profil, name)); err == nil || !errors.Is(err, fs.ErrNotExist) {
			return true
		}
	}
	return false
}

// zeige öffnet das Fenster. Ist schon eines offen, kommt ein weiteres dazu.
func (f *fensterVerwalter) zeige() error {
	f.mu.Lock()
	defer f.mu.Unlock()
	if f.imBrowser {
		return oeffneBrowser(f.adresse)
	}
	if f.eigen {
		if f.prozess != nil && f.istEigen {
			select {
			case <-f.ende:
			default:
				holeFensterNachVorn() // das Fenster ist offen: nur zeigen
				return nil
			}
		}
		if f.zeigeEigen() {
			return nil
		}
		f.eigen = false
	}
	if f.browser == "" {
		return oeffneBrowser(f.adresse)
	}
	f.bereiteProfilVor()
	cmd := exec.Command(f.browser,
		"--app="+f.adresse,
		"--user-data-dir="+f.profil,
		"--no-first-run",
		"--no-default-browser-check",
		"--disable-background-mode",
		"--hide-crash-restore-bubble",
		"--disable-session-crashed-bubble",
		"--window-size=1340,920",
		"--class=Marktrechnung",
	)
	if err := cmd.Start(); err != nil {
		log.Printf("Das Programmfenster ließ sich nicht öffnen (%v); die Oberfläche erscheint im Browser.", err)
		return oeffneBrowser(f.adresse)
	}
	if f.prozess != nil {
		select {
		case <-f.ende:
		default:
			// Unser Fenster ist noch offen: Der neue Aufruf reicht nur an
			// den laufenden Browser weiter und endet gleich wieder.
			go cmd.Wait()
			return nil
		}
	}
	ende := make(chan struct{})
	f.prozess, f.ende, f.istEigen = cmd, ende, false
	gestartet := time.Now()
	go func() {
		cmd.Wait()
		close(ende)
		f.mu.Lock()
		schliesst := f.schliesst
		f.mu.Unlock()
		if schliesst {
			return
		}
		// Endet der Aufruf sofort und das Profil ist trotzdem belegt, hat
		// ein schon laufender Browser das Fenster übernommen. Dann gilt
		// dessen Sperrdatei als Zeichen, dass das Fenster noch offen ist.
		if time.Since(gestartet) < 5*time.Second {
			time.Sleep(1500 * time.Millisecond)
			for f.profilBelegt() {
				time.Sleep(1500 * time.Millisecond)
			}
		}
		log.Print("Das Programmfenster wurde geschlossen; Marktrechnung beendet sich.")
		f.zu()
	}()
	return nil
}

// schliesse schließt das Fenster, wenn das Programm beendet wird.
func (f *fensterVerwalter) schliesse() {
	f.mu.Lock()
	f.schliesst = true
	cmd, ende := f.prozess, f.ende
	f.mu.Unlock()
	if cmd == nil || cmd.Process == nil {
		return
	}
	select {
	case <-ende:
		return
	default:
	}
	schliesseFenster(cmd.Process)
	select {
	case <-ende:
	case <-time.After(4 * time.Second):
		cmd.Process.Kill()
	}
}
