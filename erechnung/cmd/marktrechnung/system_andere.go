//go:build !windows

package main

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
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
