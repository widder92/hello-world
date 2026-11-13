package main

import (
	"errors"
	"io/fs"
	"log"
	"os"
	"os/exec"
	"path/filepath"
	"sync"
	"time"
)

// Das Programmfenster.
//
// Die Oberfläche ist mit Webtechnik gebaut. Damit sie trotzdem als eigenes
// Programm erscheint - ohne Adresszeile, ohne Tabs, mit eigenem Symbol in der
// Taskleiste -, zeigt Marktrechnung sie in einem "App-Fenster" des auf dem
// Rechner vorhandenen Browsers (unter Windows Microsoft Edge, das zum System
// gehört). Das Fenster bekommt ein eigenes Profil, getrennt vom Surfen im
// Internet: keine Erweiterungen, kein Verlauf, keine fremden Cookies.
//
// Solange das Fenster offen ist, läuft das Programm; wird es geschlossen,
// beendet sich Marktrechnung.

// fensterVerwalter öffnet das Fenster und merkt, wann es geschlossen wird.
type fensterVerwalter struct {
	adresse   string // Start-Adresse der Oberfläche samt Kennwort
	profil    string // Ordner für das Profil des Fensters
	browser   string // Programmdatei des Browsers; leer = keiner gefunden
	imBrowser bool   // im gewohnten Browser öffnen statt im eigenen Fenster
	zu        func() // wird gerufen, wenn das Fenster geschlossen wurde

	mu        sync.Mutex
	prozess   *exec.Cmd
	ende      chan struct{} // geschlossen, sobald prozess beendet ist
	schliesst bool
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
	if f.imBrowser || f.browser == "" {
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
	f.prozess, f.ende = cmd, ende
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
