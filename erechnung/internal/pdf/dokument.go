// Package pdf schreibt PDF-Dateien nach PDF/A-3b - dem Archivformat, das
// ZUGFeRD verlangt: alle Schriften eingebettet, Farbprofil und Metadaten
// vorhanden, die Rechnungs-XML als Anhang. Das Paket kommt ohne fremde
// Bibliotheken aus, damit jedes Detail der Norm in eigener Hand bleibt.
package pdf

import (
	"bytes"
	"compress/zlib"
	"crypto/md5"
	"encoding/hex"
	"fmt"
	"image"
	"image/draw"
	_ "image/jpeg" // Logos als JPEG
	_ "image/png"  // Logos als PNG
	"sort"
	"strings"
	"time"
	"unicode/utf16"
)

// Seitenmaße A4 in Punkt (1 Punkt = 1/72 Zoll).
const (
	a4Breite = 595.276
	a4Hoehe  = 841.890
	// MM rechnet Millimeter in Punkt um.
	MM = 72.0 / 25.4
)

// Farbe in Rot, Grün, Blau von 0 bis 1.
type Farbe struct{ R, G, B float64 }

// Häufige Farben.
var (
	Schwarz = Farbe{0, 0, 0}
	Weiss   = Farbe{1, 1, 1}
)

// Grau liefert einen Grauton (0 = schwarz, 1 = weiß).
func Grau(w float64) Farbe { return Farbe{w, w, w} }

// Anhang ist eine eingebettete Datei.
type Anhang struct {
	Name         string // Dateiname, z. B. "factur-x.xml"
	Beschreibung string
	Mime         string // z. B. "text/xml"
	Beziehung    string // "Alternative", "Data", "Source", "Supplement"
	Daten        []byte
}

// Bild ist ein eingebettetes Rasterbild.
type Bild struct {
	B, H  int
	daten []byte // RGB, komprimiert
	nr    int
}

// Dokument ist ein PDF im Aufbau.
type Dokument struct {
	Titel, Autor, Thema, Stichworte string
	Ersteller                       string // erzeugendes Programm
	Zeit                            time.Time
	// XMPZusatz sind weitere rdf:Description-Blöcke für die Metadaten, zum
	// Beispiel die ZUGFeRD-Angaben.
	XMPZusatz string

	schriften []*Schrift
	seiten    []*Seite
	bilder    []*Bild
	anhaenge  []Anhang

	objekte [][]byte
}

// Seite ist eine A4-Seite. Gezeichnet wird in Millimetern, gemessen von der
// linken oberen Ecke.
type Seite struct {
	dok    *Dokument
	inhalt bytes.Buffer
}

// Neu legt ein leeres Dokument an.
func Neu() *Dokument { return &Dokument{Zeit: time.Now()} }

// Schrift meldet eine Schrift an. Die Reihenfolge bestimmt den Namen im PDF.
func (d *Dokument) Schrift(s *Schrift) *Schrift {
	d.schriften = append(d.schriften, s)
	return s
}

// Anhaengen bettet eine Datei ein.
func (d *Dokument) Anhaengen(a Anhang) { d.anhaenge = append(d.anhaenge, a) }

// NeueSeite beginnt eine neue Seite.
func (d *Dokument) NeueSeite() *Seite {
	s := &Seite{dok: d}
	d.seiten = append(d.seiten, s)
	return s
}

// Seitenzahl liefert die Anzahl der bisher angelegten Seiten.
func (d *Dokument) Seitenzahl() int { return len(d.seiten) }

// Seiten liefert alle Seiten, etwa um nachträglich Seitenzahlen einzutragen.
func (d *Dokument) Seiten() []*Seite { return d.seiten }

// LadeBild liest ein PNG oder JPEG. Durchsichtige Stellen werden weiß.
func (d *Dokument) LadeBild(daten []byte) (*Bild, error) {
	quelle, _, err := image.Decode(bytes.NewReader(daten))
	if err != nil {
		return nil, err
	}
	r := quelle.Bounds()
	if r.Dx() == 0 || r.Dy() == 0 || r.Dx() > 6000 || r.Dy() > 6000 {
		return nil, fmt.Errorf("Bildgröße %dx%d wird nicht unterstützt", r.Dx(), r.Dy())
	}
	flach := image.NewRGBA(r)
	draw.Draw(flach, r, image.White, image.Point{}, draw.Src)
	draw.Draw(flach, r, quelle, r.Min, draw.Over)
	rgb := make([]byte, 0, r.Dx()*r.Dy()*3)
	for y := 0; y < r.Dy(); y++ {
		zeile := flach.Pix[y*flach.Stride : y*flach.Stride+r.Dx()*4]
		for x := 0; x < len(zeile); x += 4 {
			rgb = append(rgb, zeile[x], zeile[x+1], zeile[x+2])
		}
	}
	b := &Bild{B: r.Dx(), H: r.Dy(), daten: packe(rgb)}
	d.bilder = append(d.bilder, b)
	return b, nil
}

func packe(daten []byte) []byte {
	var b bytes.Buffer
	z, _ := zlib.NewWriterLevel(&b, zlib.BestCompression)
	z.Write(daten)
	z.Close()
	return b.Bytes()
}

func zahl(f float64) string {
	s := fmt.Sprintf("%.3f", f)
	s = strings.TrimRight(s, "0")
	s = strings.TrimRight(s, ".")
	if s == "" || s == "-" || s == "-0" {
		return "0"
	}
	return s
}

func (s *Seite) schriftName(f *Schrift) string {
	for i, x := range s.dok.schriften {
		if x == f {
			return fmt.Sprintf("F%d", i+1)
		}
	}
	panic("pdf: Schrift wurde nicht am Dokument angemeldet")
}

// Text schreibt eine Zeile; x und y (Grundlinie) in Millimetern.
func (s *Seite) Text(x, y float64, text string, f *Schrift, groesse float64, farbe Farbe) {
	if text == "" {
		return
	}
	var h strings.Builder
	for _, r := range text {
		fmt.Fprintf(&h, "%04X", f.glyphe(r))
	}
	fmt.Fprintf(&s.inhalt, "BT /%s %s Tf %s %s %s rg %s %s Td <%s> Tj ET\n",
		s.schriftName(f), zahl(groesse), zahl(farbe.R), zahl(farbe.G), zahl(farbe.B),
		zahl(x*MM), zahl(a4Hoehe-y*MM), h.String())
}

// TextRechts schreibt eine Zeile, die bei x endet.
func (s *Seite) TextRechts(x, y float64, text string, f *Schrift, groesse float64, farbe Farbe) {
	s.Text(x-f.Breite(text, groesse)/MM, y, text, f, groesse, farbe)
}

// TextMitte schreibt eine Zeile, die um x zentriert ist.
func (s *Seite) TextMitte(x, y float64, text string, f *Schrift, groesse float64, farbe Farbe) {
	s.Text(x-f.Breite(text, groesse)/MM/2, y, text, f, groesse, farbe)
}

// Linie zeichnet eine Linie; die Dicke ist in Punkt angegeben.
func (s *Seite) Linie(x1, y1, x2, y2, dicke float64, farbe Farbe) {
	fmt.Fprintf(&s.inhalt, "%s %s %s RG %s w %s %s m %s %s l S\n",
		zahl(farbe.R), zahl(farbe.G), zahl(farbe.B), zahl(dicke),
		zahl(x1*MM), zahl(a4Hoehe-y1*MM), zahl(x2*MM), zahl(a4Hoehe-y2*MM))
}

// Rechteck füllt eine Fläche.
func (s *Seite) Rechteck(x, y, b, h float64, farbe Farbe) {
	fmt.Fprintf(&s.inhalt, "%s %s %s rg %s %s %s %s re f\n",
		zahl(farbe.R), zahl(farbe.G), zahl(farbe.B),
		zahl(x*MM), zahl(a4Hoehe-(y+h)*MM), zahl(b*MM), zahl(h*MM))
}

// Bild setzt ein Bild in das angegebene Rechteck.
func (s *Seite) Bild(b *Bild, x, y, breite, hoehe float64) {
	nr := 0
	for i, x := range s.dok.bilder {
		if x == b {
			nr = i + 1
		}
	}
	fmt.Fprintf(&s.inhalt, "q %s 0 0 %s %s %s cm /Im%d Do Q\n",
		zahl(breite*MM), zahl(hoehe*MM), zahl(x*MM), zahl(a4Hoehe-(y+hoehe)*MM), nr)
}

// Umbruch teilt einen Text so in Zeilen, dass jede in die Breite (in
// Millimetern) passt. Zeilenumbrüche im Text bleiben erhalten.
func Umbruch(f *Schrift, groesse float64, text string, breite float64) []string {
	grenze := breite * MM
	var zeilen []string
	for _, absatz := range strings.Split(strings.ReplaceAll(text, "\r", ""), "\n") {
		woerter := strings.Fields(absatz)
		if len(woerter) == 0 {
			zeilen = append(zeilen, "")
			continue
		}
		zeile := ""
		for _, w := range woerter {
			// Ein einzelnes Wort, das breiter ist als die Zeile, wird geteilt.
			for f.Breite(w, groesse) > grenze {
				runen := []rune(w)
				n := len(runen) - 1
				for n > 1 && f.Breite(string(runen[:n]), groesse) > grenze {
					n--
				}
				if zeile != "" {
					zeilen = append(zeilen, zeile)
					zeile = ""
				}
				zeilen = append(zeilen, string(runen[:n]))
				w = string(runen[n:])
			}
			versuch := w
			if zeile != "" {
				versuch = zeile + " " + w
			}
			if f.Breite(versuch, groesse) <= grenze {
				zeile = versuch
			} else {
				zeilen = append(zeilen, zeile)
				zeile = w
			}
		}
		zeilen = append(zeilen, zeile)
	}
	return zeilen
}

// ---------------------------------------------------------------------------
// Ausgabe

func (d *Dokument) neu() int {
	d.objekte = append(d.objekte, nil)
	return len(d.objekte)
}

func (d *Dokument) setze(nr int, inhalt string) { d.objekte[nr-1] = []byte(inhalt) }

func (d *Dokument) strom(nr int, woerterbuch string, daten []byte) {
	var b bytes.Buffer
	fmt.Fprintf(&b, "<< %s /Length %d >>\nstream\n", woerterbuch, len(daten))
	b.Write(daten)
	b.WriteString("\nendstream")
	d.objekte[nr-1] = b.Bytes()
}

// text kodiert eine Zeichenkette für das PDF: reines ASCII in Klammern, alles
// andere als UTF-16.
func text(s string) string {
	ascii := true
	for _, r := range s {
		if r < 32 || r > 126 {
			ascii = false
			break
		}
	}
	if ascii {
		s = strings.NewReplacer(`\`, `\\`, `(`, `\(`, `)`, `\)`).Replace(s)
		return "(" + s + ")"
	}
	var b strings.Builder
	b.WriteString("<FEFF")
	for _, u := range utf16.Encode([]rune(s)) {
		fmt.Fprintf(&b, "%04X", u)
	}
	b.WriteString(">")
	return b.String()
}

func pdfName(s string) string {
	var b strings.Builder
	for i := 0; i < len(s); i++ {
		c := s[i]
		if c <= 32 || c >= 127 || strings.ContainsRune("#/()<>[]{}%", rune(c)) {
			fmt.Fprintf(&b, "#%02X", c)
		} else {
			b.WriteByte(c)
		}
	}
	return b.String()
}

func pdfDatum(t time.Time) string {
	_, versatz := t.Zone()
	vz := "+"
	if versatz < 0 {
		vz, versatz = "-", -versatz
	}
	return fmt.Sprintf("D:%s%s%02d'%02d'", t.Format("20060102150405"), vz, versatz/3600, versatz%3600/60)
}

// Bytes setzt das Dokument zusammen.
func (d *Dokument) Bytes() []byte {
	d.objekte = nil
	katalog, seitenbaum, info := d.neu(), d.neu(), d.neu()

	// Schriften: je Schrift fünf Objekte.
	schriftNr := make([]int, len(d.schriften))
	for i, f := range d.schriften {
		schriftNr[i] = d.schreibeSchrift(f)
	}
	for i, b := range d.bilder {
		b.nr = d.neu()
		d.strom(b.nr, fmt.Sprintf("/Type /XObject /Subtype /Image /Width %d /Height %d /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /FlateDecode", b.B, b.H), b.daten)
		_ = i
	}

	var mittel strings.Builder
	mittel.WriteString("<< /Font <<")
	for i, nr := range schriftNr {
		fmt.Fprintf(&mittel, " /F%d %d 0 R", i+1, nr)
	}
	mittel.WriteString(" >>")
	if len(d.bilder) > 0 {
		mittel.WriteString(" /XObject <<")
		for i, b := range d.bilder {
			fmt.Fprintf(&mittel, " /Im%d %d 0 R", i+1, b.nr)
		}
		mittel.WriteString(" >>")
	}
	mittel.WriteString(" >>")

	var kinder strings.Builder
	for _, s := range d.seiten {
		inhalt, seite := d.neu(), d.neu()
		d.strom(inhalt, "/Filter /FlateDecode", packe(s.inhalt.Bytes()))
		d.setze(seite, fmt.Sprintf("<< /Type /Page /Parent %d 0 R /MediaBox [0 0 %s %s] /Resources %s /Contents %d 0 R >>",
			seitenbaum, zahl(a4Breite), zahl(a4Hoehe), mittel.String(), inhalt))
		fmt.Fprintf(&kinder, "%d 0 R ", seite)
	}
	d.setze(seitenbaum, fmt.Sprintf("<< /Type /Pages /Kids [%s] /Count %d >>", kinder.String(), len(d.seiten)))

	// Farbprofil: PDF/A verlangt die Angabe, wie die Farben gemeint sind.
	profil := d.neu()
	d.strom(profil, "/N 3 /Filter /FlateDecode", packe(srgbProfil()))

	// Metadaten stehen unkomprimiert im PDF, damit Archivprogramme sie
	// finden, ohne die Datei zu entpacken.
	meta := d.neu()
	d.strom(meta, "/Type /Metadata /Subtype /XML", []byte(d.xmp()))

	// Anhänge
	var namen, bezuege strings.Builder
	sortiert := append([]Anhang(nil), d.anhaenge...)
	sort.Slice(sortiert, func(i, j int) bool { return sortiert[i].Name < sortiert[j].Name })
	for _, a := range sortiert {
		datei, eintrag := d.neu(), d.neu()
		summe := md5.Sum(a.Daten)
		d.strom(datei, fmt.Sprintf("/Type /EmbeddedFile /Subtype /%s /Params << /ModDate (%s) /Size %d /CheckSum <%s> >> /Filter /FlateDecode",
			pdfName(a.Mime), pdfDatum(d.Zeit), len(a.Daten), hex.EncodeToString(summe[:])), packe(a.Daten))
		beziehung := a.Beziehung
		if beziehung == "" {
			beziehung = "Alternative"
		}
		d.setze(eintrag, fmt.Sprintf("<< /Type /Filespec /F %s /UF %s /Desc %s /AFRelationship /%s /EF << /F %d 0 R /UF %d 0 R >> >>",
			text(a.Name), text(a.Name), text(a.Beschreibung), beziehung, datei, datei))
		fmt.Fprintf(&namen, "%s %d 0 R ", text(a.Name), eintrag)
		fmt.Fprintf(&bezuege, "%d 0 R ", eintrag)
	}

	var kat strings.Builder
	fmt.Fprintf(&kat, "<< /Type /Catalog /Pages %d 0 R /Metadata %d 0 R /Lang (de-DE)", seitenbaum, meta)
	fmt.Fprintf(&kat, " /ViewerPreferences << /DisplayDocTitle true >>")
	fmt.Fprintf(&kat, " /OutputIntents [<< /Type /OutputIntent /S /GTS_PDFA1 /OutputConditionIdentifier (sRGB) /Info (sRGB IEC61966-2.1) /DestOutputProfile %d 0 R >>]", profil)
	if len(d.anhaenge) > 0 {
		fmt.Fprintf(&kat, " /Names << /EmbeddedFiles << /Names [%s] >> >> /AF [%s] /PageMode /UseAttachments", namen.String(), bezuege.String())
	}
	kat.WriteString(" >>")
	d.setze(katalog, kat.String())

	var inf strings.Builder
	inf.WriteString("<<")
	for _, e := range [][2]string{{"Title", d.Titel}, {"Author", d.Autor}, {"Subject", d.Thema}, {"Keywords", d.Stichworte}, {"Creator", d.Ersteller}, {"Producer", d.Ersteller}} {
		if e[1] != "" {
			fmt.Fprintf(&inf, " /%s %s", e[0], text(e[1]))
		}
	}
	fmt.Fprintf(&inf, " /CreationDate (%s) /ModDate (%s) >>", pdfDatum(d.Zeit), pdfDatum(d.Zeit))
	d.setze(info, inf.String())

	// Datei schreiben
	var aus bytes.Buffer
	aus.WriteString("%PDF-1.7\n%\xE2\xE3\xCF\xD3\n")
	orte := make([]int, len(d.objekte))
	for i, o := range d.objekte {
		orte[i] = aus.Len()
		fmt.Fprintf(&aus, "%d 0 obj\n", i+1)
		aus.Write(o)
		aus.WriteString("\nendobj\n")
	}
	xref := aus.Len()
	fmt.Fprintf(&aus, "xref\n0 %d\n0000000000 65535 f \n", len(d.objekte)+1)
	for _, o := range orte {
		fmt.Fprintf(&aus, "%010d 00000 n \n", o)
	}
	kennung := md5.Sum(aus.Bytes())
	id := hex.EncodeToString(kennung[:])
	fmt.Fprintf(&aus, "trailer\n<< /Size %d /Root %d 0 R /Info %d 0 R /ID [<%s> <%s>] >>\nstartxref\n%d\n%%%%EOF\n",
		len(d.objekte)+1, katalog, info, id, id, xref)
	return aus.Bytes()
}

// schreibeSchrift legt die Objekte für eine eingebettete Schrift an und
// liefert die Nummer des Schriftobjekts.
func (d *Dokument) schreibeSchrift(f *Schrift) int {
	haupt, kind, beschr, datei, uni := d.neu(), d.neu(), d.neu(), d.neu(), d.neu()

	glyphen := make([]int, 0, len(f.benutzt))
	for g := range f.benutzt {
		glyphen = append(glyphen, int(g))
	}
	sort.Ints(glyphen)

	// Teilmengen bekommen sechs Großbuchstaben vor dem Namen.
	h := md5.Sum([]byte(fmt.Sprint(f.Name, glyphen)))
	marke := make([]byte, 6)
	for i := range marke {
		marke[i] = 'A' + h[i]%26
	}
	name := string(marke) + "+" + f.Name

	var w strings.Builder
	for _, g := range glyphen {
		fmt.Fprintf(&w, "%d [%d] ", g, f.breite1000(uint16(g)))
	}
	skala := func(v int) int { return v * 1000 / f.einheiten }

	d.setze(haupt, fmt.Sprintf("<< /Type /Font /Subtype /Type0 /BaseFont /%s /Encoding /Identity-H /DescendantFonts [%d 0 R] /ToUnicode %d 0 R >>", name, kind, uni))
	d.setze(kind, fmt.Sprintf("<< /Type /Font /Subtype /CIDFontType2 /BaseFont /%s /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> /FontDescriptor %d 0 R /DW 1000 /W [%s] /CIDToGIDMap /Identity >>", name, beschr, w.String()))
	d.setze(beschr, fmt.Sprintf("<< /Type /FontDescriptor /FontName /%s /Flags 4 /FontBBox [%d %d %d %d] /ItalicAngle 0 /Ascent %d /Descent %d /CapHeight %d /StemV 80 /FontFile2 %d 0 R >>",
		name, skala(f.bbox[0]), skala(f.bbox[1]), skala(f.bbox[2]), skala(f.bbox[3]),
		skala(f.oberlaenge), skala(f.unterlaenge), skala(f.versalhoehe), datei))
	roh := f.teilmenge()
	d.strom(datei, fmt.Sprintf("/Length1 %d /Filter /FlateDecode", len(roh)), packe(roh))

	// Rückübersetzung Glyphe -> Zeichen, damit man Text kopieren und
	// durchsuchen kann.
	var c strings.Builder
	c.WriteString("/CIDInit /ProcSet findresource begin\n12 dict begin\nbegincmap\n")
	c.WriteString("/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n")
	c.WriteString("/CMapName /Adobe-Identity-UCS def\n/CMapType 2 def\n")
	c.WriteString("1 begincodespacerange\n<0000> <FFFF>\nendcodespacerange\n")
	for i := 0; i < len(glyphen); i += 100 {
		ende := i + 100
		if ende > len(glyphen) {
			ende = len(glyphen)
		}
		fmt.Fprintf(&c, "%d beginbfchar\n", ende-i)
		for _, g := range glyphen[i:ende] {
			fmt.Fprintf(&c, "<%04X> <", g)
			for _, u := range utf16.Encode([]rune{f.benutzt[uint16(g)]}) {
				fmt.Fprintf(&c, "%04X", u)
			}
			c.WriteString(">\n")
		}
		c.WriteString("endbfchar\n")
	}
	c.WriteString("endcmap\nCMapName currentdict /CMap defineresource pop\nend\nend\n")
	d.strom(uni, "/Filter /FlateDecode", packe([]byte(c.String())))
	return haupt
}
