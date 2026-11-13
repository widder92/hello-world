// Package zugferd fügt die Teile zusammen: Aus einer Rechnung entstehen die
// Dateien, die verschickt und aufbewahrt werden. Umgekehrt holt es die
// Rechnungs-XML wieder aus einem ZUGFeRD-PDF heraus.
package zugferd

import (
	"bytes"
	"compress/zlib"
	"errors"
	"io"
	"regexp"
	"strconv"
	"strings"
	"time"

	"marktrechnung/internal/beleg"
	"marktrechnung/internal/cii"
	"marktrechnung/internal/modell"
	"marktrechnung/internal/rechnen"
)

// Datei ist eine erzeugte Ausgabedatei.
type Datei struct {
	Name  string
	Mime  string
	Daten []byte
	// Versand: Diese Datei ist die E-Rechnung und wird verschickt. Die
	// andere Datei ist nur eine Ansicht für die eigenen Unterlagen.
	Versand bool
}

// Optionen für das Erzeugen der Dateien.
type Optionen struct {
	Einst   modell.Einstellungen
	Logo    []byte
	Zeit    time.Time
	Version string
}

var unerlaubt = regexp.MustCompile(`[^A-Za-z0-9ÄÖÜäöüß._-]+`)

// Dateiname bildet aus der Rechnungsnummer einen Dateinamen ohne Endung. Das
// Wort "Rechnung" steht vorn, weil manche Empfänger Dateien danach sortieren.
func Dateiname(r modell.Rechnung) string {
	nr := strings.Trim(unerlaubt.ReplaceAllString(r.Nummer, "-"), "-.")
	if nr == "" {
		nr = "Entwurf"
	}
	if r.Art == modell.ArtGutschrift {
		return "Rechnungskorrektur_" + nr
	}
	return "Rechnung_" + nr
}

// Erzeuge baut die Dateien einer Rechnung im gewählten Format.
func Erzeuge(r modell.Rechnung, o Optionen) ([]Datei, error) {
	a := rechnen.Ausrechnen(r)
	if a.Format == "" {
		a.Format = modell.FormatZugferd
	}
	xml := cii.Schreibe(a)
	name := Dateiname(a)
	bo := beleg.Optionen{Einst: o.Einst, Logo: o.Logo, Zeit: o.Zeit, Version: o.Version}
	switch a.Format {
	case modell.FormatZugferd, modell.FormatZugferdXR:
		bo.XML = xml
		pdf, err := beleg.PDF(a, bo)
		if err != nil {
			return nil, err
		}
		return []Datei{{Name: name + ".pdf", Mime: "application/pdf", Daten: pdf, Versand: true}}, nil
	case modell.FormatXRechnungCII:
		pdf, err := beleg.PDF(a, bo)
		if err != nil {
			return nil, err
		}
		return []Datei{
			{Name: name + ".xml", Mime: "application/xml", Daten: xml, Versand: true},
			{Name: name + "_Ansicht.pdf", Mime: "application/pdf", Daten: pdf},
		}, nil
	}
	return nil, errors.New("unbekanntes Ausgabeformat: " + a.Format)
}

// Vorschau liefert das PDF eines Entwurfs, ohne eingebettete XML und mit
// deutlichem Entwurfsvermerk.
func Vorschau(r modell.Rechnung, o Optionen) ([]byte, error) {
	a := rechnen.Ausrechnen(r)
	return beleg.PDF(a, beleg.Optionen{Einst: o.Einst, Logo: o.Logo, Zeit: o.Zeit, Version: o.Version, Entwurf: r.Status == modell.StatusEntwurf || r.Status == ""})
}

var laengeMuster = regexp.MustCompile(`/Length\s+(\d+)(\s+\d+\s+R)?`)

// XMLAusPDF sucht in einem PDF nach einer eingebetteten Rechnungs-XML.
func XMLAusPDF(pdf []byte) ([]byte, error) {
	if !bytes.HasPrefix(bytes.TrimLeft(pdf[:min(len(pdf), 1024)], "\xef\xbb\xbf \r\n\t"), []byte("%PDF")) {
		return nil, errors.New("die Datei ist kein PDF")
	}
	verschluesselt := bytes.Contains(pdf, []byte("/Encrypt"))
	ort := 0
	for {
		i := bytes.Index(pdf[ort:], []byte("stream"))
		if i < 0 {
			break
		}
		anfang := ort + i + len("stream")
		ort = anfang
		// "endstream" enthält das Wort ebenfalls.
		if anfang >= len("endstream") && string(pdf[anfang-len("endstream"):anfang]) == "endstream" {
			continue
		}
		if anfang+2 > len(pdf) {
			break
		}
		if pdf[anfang] == '\r' && pdf[anfang+1] == '\n' {
			anfang += 2
		} else if pdf[anfang] == '\n' || pdf[anfang] == '\r' {
			anfang++
		} else {
			continue
		}
		// Das Wörterbuch steht vor dem Wort "stream".
		kopfAnfang := bytes.LastIndex(pdf[:anfang], []byte("obj"))
		if kopfAnfang < 0 || anfang-kopfAnfang > 4000 {
			kopfAnfang = max(0, anfang-4000)
		}
		kopf := pdf[kopfAnfang:anfang]
		ende := -1
		if m := laengeMuster.FindSubmatch(kopf); m != nil && len(m[2]) == 0 {
			if n, err := strconv.Atoi(string(m[1])); err == nil && anfang+n <= len(pdf) {
				ende = anfang + n
			}
		}
		if ende < 0 {
			j := bytes.Index(pdf[anfang:], []byte("endstream"))
			if j < 0 {
				break
			}
			ende = anfang + j
		}
		inhalt := pdf[anfang:ende]
		ort = ende
		if bytes.Contains(kopf, []byte("/FlateDecode")) {
			z, err := zlib.NewReader(bytes.NewReader(inhalt))
			if err != nil {
				continue
			}
			entpackt, _ := io.ReadAll(io.LimitReader(z, 20<<20))
			inhalt = entpackt
		}
		if istRechnungsXML(inhalt) {
			return inhalt, nil
		}
	}
	if verschluesselt {
		return nil, errors.New("das PDF ist verschlüsselt; die eingebettete Rechnung kann nicht gelesen werden")
	}
	return nil, errors.New("im PDF steckt keine E-Rechnung (keine eingebettete XML-Datei gefunden). Es ist ein gewöhnliches PDF, das der Handel nicht als E-Rechnung annimmt")
}

func istRechnungsXML(d []byte) bool {
	kopf := d[:min(len(d), 3000)]
	if !bytes.Contains(kopf, []byte("<")) || bytes.Contains(kopf, []byte("xmpmeta")) {
		return false
	}
	for _, w := range []string{"CrossIndustryInvoice", "CrossIndustryDocument", "Invoice-2", "CreditNote-2"} {
		if bytes.Contains(kopf, []byte(w)) {
			return true
		}
	}
	return false
}
