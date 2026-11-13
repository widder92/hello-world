package zugferd

import (
	"bytes"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"

	"marktrechnung/internal/cii"
	"marktrechnung/internal/dec"
	"marktrechnung/internal/modell"
	"marktrechnung/internal/probe"
	"marktrechnung/internal/rechnen"
)

var formate = []string{modell.FormatZugferd, modell.FormatZugferdXR, modell.FormatXRechnungCII}

// knapp ist eine Rechnung mit so wenig Angaben wie möglich: keine GLN, kein
// Lieferort, Leistungszeitraum statt Lieferdatum.
func knapp() modell.Rechnung {
	r := probe.Handel()
	r.ID, r.Nummer = "probe-knapp", "2026/7"
	r.Lieferort, r.Leistungsdatum = nil, ""
	r.ZeitraumVon, r.ZeitraumBis = "2026-10-01", "2026-10-31"
	r.Bestellnummer, r.Lieferscheinnummer, r.Kostenstelle = "", "", ""
	r.Kaeufer.GLN, r.Verkaeufer.GLN, r.Verkaeufer.Kennung = "", "", ""
	r.Positionen = r.Positionen[:1]
	r.Positionen[0].GTIN, r.Positionen[0].Artikelnummer, r.Positionen[0].KundenArtNr = "", "", ""
	r.Zahlung.SkontoTage, r.Zahlung.SkontoProzent = 0, 0
	return r
}

// lang hat so viele Positionen, dass das PDF mehrere Seiten braucht.
func lang() modell.Rechnung {
	r := probe.Handel()
	r.ID, r.Nummer = "probe-lang", "RE-2026-0099"
	vorlage := r.Positionen
	r.Positionen = nil
	for i := 0; i < 70; i++ {
		p := vorlage[i%len(vorlage)]
		p.Menge = dec.Int(int64(i%7 + 1))
		p.Beschreibung = "Sorte Nr. " + dec.Int(int64(i+1)).String() + " mit einem etwas längeren Beschreibungstext, der im PDF umbrochen werden muss, damit er in die Spalte passt."
		r.Positionen = append(r.Positionen, p)
	}
	return r
}

func beispiele() []modell.Rechnung {
	return append(probe.Alle(), knapp(), lang())
}

func TestErzeugeUndLiesZurueck(t *testing.T) {
	for _, r := range beispiele() {
		for _, f := range formate {
			r.Format = f
			dateien, err := Erzeuge(r, Optionen{Version: "test"})
			if err != nil {
				t.Fatalf("%s/%s: %v", r.ID, f, err)
			}
			soll := cii.Schreibe(rechnen.Ausrechnen(r))
			for _, d := range dateien {
				if !d.Versand {
					continue
				}
				ist := d.Daten
				if strings.HasSuffix(d.Name, ".pdf") {
					if ist, err = XMLAusPDF(d.Daten); err != nil {
						t.Fatalf("%s/%s: %v", r.ID, f, err)
					}
				}
				if !bytes.Equal(ist, soll) {
					t.Errorf("%s/%s: die eingebettete XML weicht ab", r.ID, f)
				}
			}
		}
	}
	if _, err := XMLAusPDF([]byte("kein PDF")); err == nil {
		t.Error("ein Nicht-PDF hätte abgelehnt werden müssen")
	}
	ohne, _ := Vorschau(probe.Handel(), Optionen{})
	if _, err := XMLAusPDF(ohne); err == nil {
		t.Error("die Vorschau darf keine E-Rechnung enthalten")
	}
}

func TestDateiname(t *testing.T) {
	r := knapp()
	if n := Dateiname(r); n != "Rechnung_2026-7" {
		t.Errorf("Dateiname %q", n)
	}
}

// TestAmtlichePruefung lässt alle Beispiele von Mustang prüfen, einem freien
// Prüfprogramm für ZUGFeRD und XRechnung (inklusive PDF/A-Prüfung). Der Test
// läuft nur, wenn MUSTANG_JAR auf die Datei Mustang-CLI-x.y.z.jar zeigt:
//
//	MUSTANG_JAR=/pfad/Mustang-CLI.jar go test ./internal/zugferd/ -run Amtlich -v
func TestAmtlichePruefung(t *testing.T) {
	jar := os.Getenv("MUSTANG_JAR")
	if jar == "" {
		t.Skip("MUSTANG_JAR ist nicht gesetzt")
	}
	ordner := t.TempDir()
	for _, r := range beispiele() {
		for _, f := range formate {
			r.Format = f
			dateien, err := Erzeuge(r, Optionen{Version: "test"})
			if err != nil {
				t.Fatal(err)
			}
			for _, d := range dateien {
				if !d.Versand {
					continue
				}
				pfad := filepath.Join(ordner, r.ID+"-"+f+filepath.Ext(d.Name))
				if err := os.WriteFile(pfad, d.Daten, 0o644); err != nil {
					t.Fatal(err)
				}
				aus, _ := exec.Command("java", "-Xmx1g", "-jar", jar, "--action", "validate", "--source", pfad, "--no-notices", "--disable-file-logging").Output()
				bericht := string(aus)
				i := strings.LastIndex(bericht, "<summary status=")
				if i < 0 || !strings.HasPrefix(bericht[i:], `<summary status="valid"`) || strings.Contains(bericht, "<error") || strings.Contains(bericht, "<warning") {
					t.Errorf("%s/%s: nicht gültig\n%s", r.ID, f, bericht)
				} else {
					t.Logf("%s/%s: gültig", r.ID, f)
				}
			}
		}
	}
}
