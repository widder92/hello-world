// Package web enthält die Dateien der Bedienoberfläche. Sie werden in das
// Programm eingebaut, damit es aus einer einzigen Datei besteht.
package web

import (
	"embed"
	"io/fs"
)

//go:embed dateien
var eingebaut embed.FS

// Dateien liefert die Oberfläche als Dateisystem.
func Dateien() fs.FS {
	unter, err := fs.Sub(eingebaut, "dateien")
	if err != nil {
		panic(err)
	}
	return unter
}
