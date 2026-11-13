// Package geheim bewahrt das E-Mail-Passwort auf. Es liegt nicht im
// Datenordner (der vielleicht in eine Cloud gesichert wird), sondern im
// persönlichen Einstellungsordner des Benutzerkontos. Unter Windows wird es
// zusätzlich mit dem Windows-Konto verschlüsselt, so dass es nur dieser
// Benutzer auf diesem Rechner wieder lesen kann.
package geheim

import (
	"encoding/base64"
	"errors"
	"io/fs"
	"os"
	"path/filepath"
	"strings"
)

// Ablage ist eine Datei für ein Geheimnis.
type Ablage struct{ pfad string }

// Neu legt die Ablage im angegebenen Ordner an.
func Neu(ordner, name string) *Ablage {
	return &Ablage{pfad: filepath.Join(ordner, name)}
}

// Setze speichert das Geheimnis; ein leerer Text löscht es.
func (a *Ablage) Setze(geheimnis string) error {
	if geheimnis == "" {
		err := os.Remove(a.pfad)
		if errors.Is(err, fs.ErrNotExist) {
			return nil
		}
		return err
	}
	verpackt, art, err := schuetze([]byte(geheimnis))
	if err != nil {
		return err
	}
	if err := os.MkdirAll(filepath.Dir(a.pfad), 0o700); err != nil {
		return err
	}
	inhalt := art + ":" + base64.StdEncoding.EncodeToString(verpackt) + "\n"
	return os.WriteFile(a.pfad, []byte(inhalt), 0o600)
}

// Lies liefert das Geheimnis oder "" wenn keines gespeichert ist.
func (a *Ablage) Lies() (string, error) {
	inhalt, err := os.ReadFile(a.pfad)
	if errors.Is(err, fs.ErrNotExist) {
		return "", nil
	}
	if err != nil {
		return "", err
	}
	art, daten, ok := strings.Cut(strings.TrimSpace(string(inhalt)), ":")
	if !ok {
		return "", errors.New("die Passwortablage ist beschädigt")
	}
	roh, err := base64.StdEncoding.DecodeString(daten)
	if err != nil {
		return "", errors.New("die Passwortablage ist beschädigt")
	}
	klar, err := oeffne(roh, art)
	if err != nil {
		return "", errors.New("das gespeicherte E-Mail-Passwort kann nicht gelesen werden. Bitte unter Einstellungen neu eingeben")
	}
	return string(klar), nil
}

// Vorhanden meldet, ob ein Geheimnis gespeichert ist.
func (a *Ablage) Vorhanden() bool {
	_, err := os.Stat(a.pfad)
	return err == nil
}
