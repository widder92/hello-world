//go:build !windows

package geheim

import "errors"

// Auf macOS und Linux schützt allein das Dateirecht (nur der eigene Benutzer
// darf lesen).
func schuetze(klar []byte) ([]byte, string, error) { return klar, "datei", nil }

func oeffne(daten []byte, art string) ([]byte, error) {
	if art != "datei" {
		return nil, errors.New("unbekannte Ablageart")
	}
	return daten, nil
}
