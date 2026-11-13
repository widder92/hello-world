//go:build !windows

package main

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"syscall"
)

func starte(programm string, argumente ...string) error {
	return exec.Command(programm, argumente...).Start()
}

func oeffneBrowser(adresse string) error {
	if runtime.GOOS == "darwin" {
		return starte("open", adresse)
	}
	return starte("xdg-open", adresse)
}

func oeffneOrdner(pfad string) error { return oeffneBrowser(pfad) }

// dokumente liefert den Dokumente-Ordner des Benutzers.
func dokumente() string {
	heim, err := os.UserHomeDir()
	if err != nil {
		return "."
	}
	for _, name := range []string{"Dokumente", "Documents"} {
		if info, err := os.Stat(filepath.Join(heim, name)); err == nil && info.IsDir() {
			return filepath.Join(heim, name)
		}
	}
	return heim
}

func hatKonsole() bool { return true }

func meldeFehler(text string) { fmt.Fprintln(os.Stderr, text) }

// browserKandidaten nennt Browser, die App-Fenster beherrschen.
func browserKandidaten() []string {
	if runtime.GOOS == "darwin" {
		return []string{
			"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
			"/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge",
			"/Applications/Chromium.app/Contents/MacOS/Chromium",
			"/Applications/Brave Browser.app/Contents/MacOS/Brave Browser",
		}
	}
	return []string{"google-chrome", "google-chrome-stable", "chromium", "chromium-browser", "microsoft-edge", "microsoft-edge-stable", "brave-browser"}
}

// schliesseFenster bittet den Browser, sein Fenster zu schließen.
func schliesseFenster(p *os.Process) { p.Signal(syscall.SIGTERM) }

// fensterOrdner ist der Ort für das Profil des Programmfensters.
func fensterOrdner() string {
	ordner, err := os.UserCacheDir()
	if err != nil {
		ordner = os.TempDir()
	}
	return filepath.Join(ordner, "Marktrechnung", "fenster")
}
