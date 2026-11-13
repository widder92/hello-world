// Das Hilfsprogramm probe schreibt die Beispielrechnungen in einen Ordner,
// damit man sie mit einem externen Prüfprogramm testen kann.
package main

import (
	"fmt"
	"os"
	"path/filepath"

	"marktrechnung/internal/beleg"
	"marktrechnung/internal/cii"
	"marktrechnung/internal/modell"
	"marktrechnung/internal/probe"
	"marktrechnung/internal/rechnen"
)

func main() {
	ziel := "."
	if len(os.Args) > 1 {
		ziel = os.Args[1]
	}
	schreibe := func(name string, daten []byte) {
		if err := os.WriteFile(filepath.Join(ziel, name), daten, 0o644); err != nil {
			fmt.Fprintln(os.Stderr, err)
			os.Exit(1)
		}
		fmt.Println(name)
	}
	for _, r := range probe.Alle() {
		for _, f := range []string{modell.FormatZugferd, modell.FormatZugferdXR, modell.FormatXRechnungCII} {
			r.Format = f
			a := rechnen.Ausrechnen(r)
			xml := cii.Schreibe(a)
			if f == modell.FormatXRechnungCII {
				schreibe(r.ID+"-"+f+".xml", xml)
				continue
			}
			pdf, err := beleg.PDF(a, beleg.Optionen{XML: xml, Version: "probe"})
			if err != nil {
				fmt.Fprintln(os.Stderr, err)
				os.Exit(1)
			}
			schreibe(r.ID+"-"+f+".pdf", pdf)
		}
	}
}
