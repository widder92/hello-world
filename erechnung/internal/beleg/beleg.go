// Package beleg setzt eine Rechnung als PDF: Anschriftfeld nach DIN 5008,
// Positionstabelle, Summen, Zahlungshinweise und Fußzeile. Auf Wunsch wird
// die maschinenlesbare XML eingebettet - dann ist das Ergebnis eine
// ZUGFeRD-Rechnung.
package beleg

import (
	"fmt"
	"strings"
	"time"

	"marktrechnung/internal/codes"
	"marktrechnung/internal/dec"
	"marktrechnung/internal/modell"
	"marktrechnung/internal/pdf"
	"marktrechnung/internal/rechnen"
)

// Optionen für die PDF-Ausgabe.
type Optionen struct {
	Einst   modell.Einstellungen
	Logo    []byte    // PNG oder JPEG, darf fehlen
	XML     []byte    // eingebettete E-Rechnung, darf fehlen
	Entwurf bool      // deutlicher Hinweis, dass das Dokument noch nicht gilt
	Zeit    time.Time // Erstellungszeit; leer = jetzt
	Version string    // Programmversion für die Metadaten
}

// Maße in Millimetern.
const (
	links     = 20.0
	rechts    = 190.0
	unten     = 262.0 // tiefer wird nicht geschrieben, dann folgt die Fußzeile
	fussOben  = 271.0
	spInfo    = 122.0 // linker Rand des Infoblocks
	spMenge   = 122.0 // rechter Rand der Mengenspalte
	spEinheit = 124.0
	spPreis   = 156.0
	spSteuer  = 169.0
)

var (
	grau     = pdf.Grau(0.42)
	hellgrau = pdf.Grau(0.80)
	flaeche  = pdf.Grau(0.955)
	rot      = pdf.Farbe{R: 0.75, G: 0.1, B: 0.1}
)

type setzer struct {
	r     modell.Rechnung
	o     Optionen
	dok   *pdf.Dokument
	n, f  *pdf.Schrift
	seite *pdf.Seite
	y     float64
	titel string
	// inTabelle: Bei einem Seitenwechsel wird der Tabellenkopf wiederholt.
	inTabelle bool
	// briefDatum: Datum eines Schreibens zu einer Rechnung (Mahnung).
	briefDatum string
}

// belegMahnung kennzeichnet intern das Schreiben zu einer Rechnung.
const belegMahnung = "mahnung"

// Mahnschreiben: Angaben für eine Zahlungserinnerung oder Mahnung.
type Mahnschreiben struct {
	Stufe   int    // 0 Zahlungserinnerung, 1 erste Mahnung, 2 zweite Mahnung ...
	Datum   string // Datum des Schreibens
	Frist   string // zahlbar bis
	Gebuehr dec.D  // Mahngebühr, darf 0 sein
	Text    string // Anschreiben
}

// MahnTitel nennt die Überschrift zur Stufe.
func MahnTitel(stufe int) string {
	switch {
	case stufe <= 0:
		return "Zahlungserinnerung"
	case stufe == 1:
		return "Mahnung"
	}
	return fmt.Sprintf("%d. Mahnung", stufe)
}

// Mahnung erzeugt das Schreiben zu einer Rechnung: Anschrift, Text, der
// offene Betrag mit Gebühr, die neue Frist und die Bankverbindung.
func Mahnung(r modell.Rechnung, m Mahnschreiben, o Optionen) ([]byte, error) {
	if r.Summen == nil {
		r = rechnen.Ausrechnen(r)
	}
	if o.Zeit.IsZero() {
		o.Zeit = time.Now()
	}
	brief := r
	brief.Belegart = belegMahnung
	brief.Leistungsdatum, brief.ZeitraumVon, brief.ZeitraumBis = "", "", ""
	brief.Bestellnummer, brief.Lieferscheinnummer, brief.Auftragsnummer, brief.Vertragsnummer = "", "", "", ""
	brief.Kostenstelle, brief.Kaeuferreferenz = "", ""
	brief.Lieferort, brief.Vorgaenger, brief.Hinweise = nil, nil, nil
	brief.Einleitung, brief.Schluss = m.Text, "Sollte sich Ihre Zahlung mit diesem Schreiben überschnitten haben, betrachten Sie es bitte als gegenstandslos."
	brief.Zahlung.Bedingungen, brief.Zahlung.SkontoTage = "", 0

	s := &setzer{r: brief, o: o, dok: pdf.Neu(), briefDatum: m.Datum}
	s.n, s.f = pdf.Standardschriften()
	s.dok.Schrift(s.n)
	s.dok.Schrift(s.f)
	s.dok.Zeit = o.Zeit
	s.titel = MahnTitel(m.Stufe) + " zur Rechnung"
	s.dok.Titel = s.titel + " " + r.Nummer
	s.dok.Autor = r.Verkaeufer.Name
	s.dok.Thema = s.dok.Titel + " an " + r.Kaeufer.Name
	s.dok.Ersteller = strings.TrimSpace("Marktrechnung " + o.Version)
	s.neueSeite()
	if err := s.kopf(); err != nil {
		return nil, err
	}
	s.platz(34)
	s.y += 2
	zeile := func(titel string, betrag dec.D, fett bool) {
		f, gr := s.n, 10.0
		if fett {
			f, gr = s.f, 10.5
		}
		s.seite.Text(links, s.y+3.8, titel, f, gr, pdf.Schwarz)
		s.seite.TextRechts(rechts-1.5, s.y+3.8, Euro(betrag, r.Waehrung), f, gr, pdf.Schwarz)
		s.y += 5.6
	}
	offen := r.Summen.Zahlbetrag
	text := "Rechnung " + r.Nummer + " vom " + rechnen.DatumDeutsch(r.Datum)
	if r.Faellig != "" {
		text += ", fällig am " + rechnen.DatumDeutsch(r.Faellig)
	}
	zeile(text, offen, false)
	if !m.Gebuehr.IstNull() {
		zeile("Mahngebühr", m.Gebuehr, false)
	}
	s.seite.Linie(links, s.y+0.6, rechts, s.y+0.6, 0.4, pdf.Schwarz)
	s.y += 1.6
	gesamt := "Offener Betrag"
	if m.Frist != "" {
		gesamt = "Bitte überweisen Sie bis zum " + rechnen.DatumDeutsch(m.Frist)
	}
	zeile(gesamt, offen+m.Gebuehr, true)
	s.y += 4
	s.zahlung()
	s.schlusstexte()
	s.fusszeilen()
	return s.dok.Bytes(), nil
}

// Euro formatiert einen Geldbetrag: "1.234,56 €".
func Euro(d dec.D, waehrung string) string {
	z := "€"
	if waehrung != "" && waehrung != "EUR" {
		z = waehrung
	}
	return d.Deutsch(2, 2) + " " + z
}

// Titel liefert die Überschrift des Belegs, z. B. "Rechnung".
func Titel(r modell.Rechnung) string {
	switch r.Belegart {
	case modell.BelegAngebot:
		return "Angebot"
	case modell.BelegLieferschein:
		return "Lieferschein"
	}
	switch r.Art {
	case modell.ArtGutschrift:
		return "Rechnungskorrektur (Gutschrift)"
	case modell.ArtKorrektur:
		return "Korrigierte Rechnung"
	case modell.ArtAbschlag:
		return "Abschlagsrechnung"
	}
	return "Rechnung"
}

// PDF erzeugt das Dokument. Die Rechnung muss ausgerechnet sein.
func PDF(r modell.Rechnung, o Optionen) ([]byte, error) {
	if r.Summen == nil {
		r = rechnen.Ausrechnen(r)
	}
	if o.Zeit.IsZero() {
		o.Zeit = time.Now()
	}
	s := &setzer{r: r, o: o, dok: pdf.Neu()}
	s.n, s.f = pdf.Standardschriften()
	s.dok.Schrift(s.n)
	s.dok.Schrift(s.f)
	s.dok.Zeit = o.Zeit
	s.titel = Titel(r)
	nummer := r.Nummer
	if nummer == "" {
		nummer = "(Entwurf)"
	}
	s.dok.Titel = s.titel + " " + nummer
	s.dok.Autor = r.Verkaeufer.Name
	s.dok.Thema = s.titel + " " + nummer + " an " + r.Kaeufer.Name
	s.dok.Ersteller = strings.TrimSpace("Marktrechnung " + o.Version)

	if len(o.XML) > 0 {
		name, version, profil := "factur-x.xml", "1.0", "EN 16931"
		if r.Format == modell.FormatZugferdXR {
			name, version, profil = "xrechnung.xml", "3.0", "XRECHNUNG"
		}
		s.dok.Anhaengen(pdf.Anhang{Name: name, Beschreibung: "Rechnungsdaten im Format ZUGFeRD / Factur-X",
			Mime: "text/xml", Beziehung: "Alternative", Daten: o.XML})
		s.dok.XMPZusatz = pdf.ZugferdXMP(name, version, profil)
	}

	s.neueSeite()
	if err := s.kopf(); err != nil {
		return nil, err
	}
	s.tabelle()
	switch r.Belegart {
	case modell.BelegLieferschein:
		// Ohne Preise und Summen; dafür Platz für die Empfangsbestätigung.
		s.platz(12)
		s.y += 6
		s.schlusstexte()
		s.platz(30)
		s.y += 20
		s.seite.Linie(links, s.y, links+70, s.y, 0.4, pdf.Schwarz)
		s.seite.Linie(links+90, s.y, rechts, s.y, 0.4, pdf.Schwarz)
		s.seite.Text(links, s.y+4, "Ort, Datum", s.n, 8, grau)
		s.seite.Text(links+90, s.y+4, "Ware vollständig erhalten – Unterschrift, Stempel", s.n, 8, grau)
	case modell.BelegAngebot:
		s.summen()
		if t, ok := rechnen.Datum(r.GueltigBis); ok {
			s.platz(10)
			s.absatz(links, rechts-links, "Dieses Angebot ist gültig bis zum "+t.Format("02.01.2006")+".", s.n, 9.5, pdf.Schwarz)
			s.y += 2
		}
		s.schlusstexte()
	default:
		s.summen()
		s.zahlung()
		s.schlusstexte()
	}
	s.fusszeilen()
	return s.dok.Bytes(), nil
}

func (s *setzer) neueSeite() {
	s.seite = s.dok.NeueSeite()
	s.y = 25
	if s.o.Entwurf {
		s.seite.Rechteck(0, 0, 210, 9, pdf.Farbe{R: 1, G: 0.93, B: 0.93})
		s.seite.TextMitte(105, 6, "ENTWURF – diese Rechnung ist noch nicht fertiggestellt und nicht gültig", s.f, 9, rot)
	}
	if s.dok.Seitenzahl() > 1 {
		s.seite.Text(links, 18, s.titel+" "+s.r.Nummer, s.n, 8, grau)
		if s.inTabelle {
			s.tabellenkopf()
		}
	}
}

// platz sorgt dafür, dass noch h Millimeter auf die Seite passen.
func (s *setzer) platz(h float64) {
	if s.y+h > unten {
		s.neueSeite()
	}
}

// absatz schreibt umbrochenen Text und rückt y weiter.
func (s *setzer) absatz(x, breite float64, text string, f *pdf.Schrift, groesse float64, farbe pdf.Farbe) {
	hoehe := groesse * 0.44 // Zeilenabstand in Millimetern
	for _, z := range pdf.Umbruch(f, groesse, text, breite) {
		s.platz(hoehe)
		s.seite.Text(x, s.y, z, f, groesse, farbe)
		s.y += hoehe
	}
}

func (s *setzer) kopf() error {
	r, v := s.r, s.r.Verkaeufer
	p := s.seite

	// Briefkopf: Firmenname links, Logo rechts.
	logoBreite := 0.0
	if len(s.o.Logo) > 0 {
		bild, err := s.dok.LadeBild(s.o.Logo)
		if err != nil {
			return fmt.Errorf("das Logo kann nicht gelesen werden: %w", err)
		}
		b, h := 55.0, 55.0*float64(bild.H)/float64(bild.B)
		if h > 24 {
			h = 24
			b = h * float64(bild.B) / float64(bild.H)
		}
		p.Bild(bild, rechts-b, 12, b, h)
		logoBreite = b + 5
	}
	name := v.Handelsname
	if name == "" {
		name = v.Name
	}
	y := 21.0
	for _, z := range pdf.Umbruch(s.f, 15, name, rechts-links-logoBreite) {
		p.Text(links, y, z, s.f, 15, pdf.Schwarz)
		y += 6.5
	}
	if v.Handelsname != "" && v.Handelsname != v.Name {
		p.Text(links, y-1, v.Name, s.n, 9, grau)
	}

	// Anschriftfeld: Absenderzeile, darunter der Empfänger.
	absender := strings.Join(nichtLeer(v.Name, v.Strasse, strings.TrimSpace(v.PLZ+" "+v.Ort)), " · ")
	p.Text(links, 49, kuerze(s.n, 7, absender, 85), s.n, 7, grau)
	p.Linie(links, 50.2, links+85, 50.2, 0.3, hellgrau)
	k := r.Kaeufer
	ay := 56.5
	var zeilen []string
	zeilen = append(zeilen, pdf.Umbruch(s.n, 10.5, k.Name, 85)...)
	if k.Handelsname != "" && k.Handelsname != k.Name {
		zeilen = append(zeilen, k.Handelsname)
	}
	zeilen = append(zeilen, nichtLeer(k.Zusatz, k.Strasse, strings.TrimSpace(k.PLZ+" "+k.Ort))...)
	if k.Land != "" && k.Land != "DE" {
		zeilen = append(zeilen, strings.ToUpper(codes.LandName(k.Land)))
	}
	for i, z := range zeilen {
		if i >= 8 {
			break
		}
		p.Text(links, ay, kuerze(s.n, 10.5, z, 85), s.n, 10.5, pdf.Schwarz)
		ay += 4.9
	}

	// Infoblock rechts.
	iy := 49.0
	info := func(titel, wert string, fett bool) {
		if strings.TrimSpace(wert) == "" {
			return
		}
		f := s.n
		if fett {
			f = s.f
		}
		p.Text(spInfo, iy, titel, s.n, 9, grau)
		p.TextRechts(rechts, iy, kuerze(f, 9, wert, rechts-spInfo-30), f, 9, pdf.Schwarz)
		iy += 4.6
	}
	nummer := r.Nummer
	if nummer == "" {
		nummer = "wird vergeben"
	}
	switch r.Belegart {
	case modell.BelegAngebot:
		info("Angebotsnummer", nummer, true)
		info("Angebotsdatum", rechnen.DatumDeutsch(r.Datum), false)
		info("Gültig bis", rechnen.DatumDeutsch(r.GueltigBis), false)
	case modell.BelegLieferschein:
		info("Lieferscheinnummer", nummer, true)
		info("Datum", rechnen.DatumDeutsch(r.Datum), false)
	case belegMahnung:
		info("Datum", rechnen.DatumDeutsch(s.briefDatum), false)
		info("Rechnungsnummer", nummer, true)
		info("Rechnungsdatum", rechnen.DatumDeutsch(r.Datum), false)
	default:
		info("Rechnungsnummer", nummer, true)
		info("Rechnungsdatum", rechnen.DatumDeutsch(r.Datum), false)
	}
	switch {
	case r.Leistungsdatum != "":
		if r.Belegart == modell.BelegLieferschein {
			info("Lieferdatum", rechnen.DatumDeutsch(r.Leistungsdatum), false)
		} else {
			info("Liefer-/Leistungsdatum", rechnen.DatumDeutsch(r.Leistungsdatum), false)
		}
	case r.ZeitraumVon != "" || r.ZeitraumBis != "":
		info("Leistungszeitraum", rechnen.DatumDeutsch(r.ZeitraumVon)+" – "+rechnen.DatumDeutsch(r.ZeitraumBis), false)
	}
	info("Kundennummer", k.Kennung, false)
	info("Unsere Lieferantennr.", v.Kennung, false)
	info("Bestellnummer", r.Bestellnummer, false)
	info("Lieferschein", r.Lieferscheinnummer, false)
	info("Auftragsnummer", r.Auftragsnummer, false)
	info("Vertragsnummer", r.Vertragsnummer, false)
	info("Kostenstelle", r.Kostenstelle, false)
	if r.Kaeuferreferenz != "" && r.Kaeuferreferenz != k.GLN {
		info("Käuferreferenz", r.Kaeuferreferenz, false)
	}
	info("GLN Rechnungsempf.", k.GLN, false)
	info("USt-IdNr. Kunde", k.UStID, false)

	s.y = ay + 6
	if iy+4 > s.y {
		s.y = iy + 4
	}
	if s.y < 101 {
		s.y = 101
	}

	// Überschrift
	p.Text(links, s.y, s.titel+" "+r.Nummer, s.f, 15, pdf.Schwarz)
	s.y += 6
	for _, vg := range r.Vorgaenger {
		t := "zur Rechnung " + vg.Nummer
		if vg.Datum != "" {
			t += " vom " + rechnen.DatumDeutsch(vg.Datum)
		}
		p.Text(links, s.y, t, s.n, 9.5, pdf.Schwarz)
		s.y += 4.6
	}
	if l := r.Lieferort; !l.Leer() {
		t := "Warenempfänger: " + strings.Join(nichtLeer(l.Name, l.Strasse, strings.TrimSpace(l.PLZ+" "+l.Ort)), ", ")
		if l.GLN != "" {
			t += " · GLN " + l.GLN
		}
		if l.Kennung != "" {
			t += " · Markt-Nr. " + l.Kennung
		}
		s.absatz(links, rechts-links, t, s.n, 9, pdf.Schwarz)
	}
	s.y += 2.5
	if strings.TrimSpace(r.Einleitung) != "" {
		s.absatz(links, rechts-links, r.Einleitung, s.n, 10, pdf.Schwarz)
		s.y += 2
	}
	return nil
}

func (s *setzer) tabellenkopf() {
	p := s.seite
	p.Rechteck(links, s.y, rechts-links, 6.5, flaeche)
	ty := s.y + 4.4
	p.Text(links+1.5, ty, "Pos.", s.f, 8, pdf.Schwarz)
	p.Text(links+11, ty, "Bezeichnung", s.f, 8, pdf.Schwarz)
	p.TextRechts(spMenge, ty, "Menge", s.f, 8, pdf.Schwarz)
	p.Text(spEinheit, ty, "Einheit", s.f, 8, pdf.Schwarz)
	if s.r.Belegart != modell.BelegLieferschein {
		p.TextRechts(spPreis, ty, "Einzelpreis", s.f, 8, pdf.Schwarz)
		p.TextRechts(spSteuer, ty, "USt", s.f, 8, pdf.Schwarz)
		p.TextRechts(rechts-1.5, ty, "Netto", s.f, 8, pdf.Schwarz)
	}
	s.y += 6.5
}

func (s *setzer) tabelle() {
	r := s.r
	s.platz(30)
	s.tabellenkopf()
	s.inTabelle = true
	breiteText := spMenge - 14 - (links + 11) // Platz für die Bezeichnung
	for _, pos := range r.Positionen {
		titel := pdf.Umbruch(s.n, 9.5, pos.Bezeichnung, breiteText)
		var klein []string
		kennungen := nichtLeer(mit("Art.-Nr. ", pos.Artikelnummer), mit("GTIN ", pos.GTIN), mit("Ihre Art.-Nr. ", pos.KundenArtNr))
		if len(kennungen) > 0 {
			klein = append(klein, pdf.Umbruch(s.n, 7.5, strings.Join(kennungen, " · "), breiteText+25)...)
		}
		for _, t := range nichtLeer(pos.Beschreibung, pos.Hinweis) {
			klein = append(klein, pdf.Umbruch(s.n, 7.5, t, breiteText+25)...)
		}
		if r.Belegart == modell.BelegLieferschein {
			// Der Lieferschein nennt Ware und Menge, keine Preise.
			hoehe := 2.2 + float64(len(titel))*4.2 + float64(len(klein))*3.3 + 1.6
			s.platz(hoehe)
			p := s.seite
			y := s.y + 4.6
			p.Text(links+1.5, y, pos.Nr, s.n, 9.5, pdf.Schwarz)
			p.TextRechts(spMenge, y, pos.Menge.Deutsch(0, 3), s.n, 9.5, pdf.Schwarz)
			p.Text(spEinheit, y, codes.EinheitKurz(pos.Einheit), s.n, 9.5, pdf.Schwarz)
			for _, z := range titel {
				p.Text(links+11, y, z, s.n, 9.5, pdf.Schwarz)
				y += 4.2
			}
			y -= 0.6
			for _, z := range klein {
				p.Text(links+11, y, z, s.n, 7.5, grau)
				y += 3.3
			}
			s.y += hoehe
			p.Linie(links, s.y, rechts, s.y, 0.3, hellgrau)
			continue
		}
		hoehe := 2.2 + float64(len(titel))*4.2 + float64(len(klein))*3.3 + float64(len(pos.ZuAbschlaege))*3.8 + 1.6
		s.platz(hoehe)
		p := s.seite
		y := s.y + 4.6
		p.Text(links+1.5, y, pos.Nr, s.n, 9.5, pdf.Schwarz)
		p.TextRechts(spMenge, y, pos.Menge.Deutsch(0, 3), s.n, 9.5, pdf.Schwarz)
		p.Text(spEinheit, y, codes.EinheitKurz(pos.Einheit), s.n, 9.5, pdf.Schwarz)
		preis := pos.Einzelpreis.Deutsch(2, 4)
		if pos.Preisbasis != dec.Eins && !pos.Preisbasis.IstNull() {
			preis += " / " + pos.Preisbasis.Deutsch(0, 3)
		}
		p.TextRechts(spPreis, y, preis, s.n, 9.5, pdf.Schwarz)
		steuer := pos.SteuerSatz.Deutsch(0, 2) + " %"
		if pos.SteuerKat != modell.KatNormal {
			steuer = "–"
		}
		p.TextRechts(spSteuer, y, steuer, s.n, 9.5, pdf.Schwarz)
		// Hat die Position Nachlässe, steht rechts zunächst der Betrag davor.
		if len(pos.ZuAbschlaege) > 0 {
			p.TextRechts(rechts-1.5, y, rechnen.Zeilenbasis(pos).Deutsch(2, 2), s.n, 9.5, grau)
		} else {
			p.TextRechts(rechts-1.5, y, pos.Netto.Deutsch(2, 2), s.n, 9.5, pdf.Schwarz)
		}
		for _, z := range titel {
			p.Text(links+11, y, z, s.n, 9.5, pdf.Schwarz)
			y += 4.2
		}
		y -= 0.6
		for _, z := range klein {
			p.Text(links+11, y, z, s.n, 7.5, grau)
			y += 3.3
		}
		for i, za := range pos.ZuAbschlaege {
			t, vz := "abzüglich ", "-"
			if za.Zuschlag {
				t, vz = "zuzüglich ", ""
			}
			if !za.Prozent.IstNull() {
				t += za.Prozent.Deutsch(0, 2) + " % "
			}
			t += za.Grund
			y += 0.5
			p.Text(links+11, y, t, s.n, 8.5, pdf.Schwarz)
			p.TextRechts(spPreis, y, vz+za.Betrag.Deutsch(2, 2), s.n, 8.5, pdf.Schwarz)
			if i == len(pos.ZuAbschlaege)-1 {
				p.TextRechts(rechts-1.5, y, pos.Netto.Deutsch(2, 2), s.n, 9.5, pdf.Schwarz)
			}
			y += 3.3
		}
		s.y += hoehe
		p.Linie(links, s.y, rechts, s.y, 0.3, hellgrau)
	}
	s.inTabelle = false
}

func (s *setzer) summen() {
	r, su := s.r, s.r.Summen
	zeilen := 3 + len(r.ZuAbschlaege) + len(su.Steuern)
	s.platz(float64(zeilen)*5 + 14)
	s.y += 3
	x := 112.0
	zeile := func(titel string, betrag dec.D, fett bool) {
		f := s.n
		gr := 9.5
		if fett {
			f, gr = s.f, 10.5
		}
		s.seite.Text(x, s.y+3.6, titel, f, gr, pdf.Schwarz)
		s.seite.TextRechts(rechts-1.5, s.y+3.6, Euro(betrag, r.Waehrung), f, gr, pdf.Schwarz)
		s.y += 5
	}
	if len(r.ZuAbschlaege) > 0 {
		zeile("Summe Positionen", su.Positionen, false)
		for _, za := range r.ZuAbschlaege {
			t, betrag := "abzüglich ", -za.Betrag
			if za.Zuschlag {
				t, betrag = "zuzüglich ", za.Betrag
			}
			if !za.Prozent.IstNull() {
				t += za.Prozent.Deutsch(0, 2) + " % "
			}
			zeile(kuerze(s.n, 9.5, t+za.Grund, 50), betrag, false)
		}
	}
	zeile("Nettobetrag", su.Netto, false)
	for _, st := range su.Steuern {
		if st.Kat == modell.KatNormal {
			zeile(fmt.Sprintf("zzgl. %s %% USt auf %s", st.Satz.Deutsch(0, 2), st.Basis.Deutsch(2, 2)), st.Betrag, false)
		}
	}
	s.seite.Linie(x, s.y+0.6, rechts, s.y+0.6, 0.8, pdf.Schwarz)
	s.y += 1.6
	gesamt := "Rechnungsbetrag"
	if r.Belegart == modell.BelegAngebot {
		gesamt = "Angebotsbetrag"
	}
	if r.Art == modell.ArtGutschrift {
		gesamt = "Gutschriftsbetrag"
	}
	zeile(gesamt, su.Brutto, true)
	if !su.Vorauszahlung.IstNull() || !su.Rundung.IstNull() {
		if !su.Vorauszahlung.IstNull() {
			zeile("bereits gezahlt", -su.Vorauszahlung, false)
		}
		if !su.Rundung.IstNull() {
			zeile("Rundung", su.Rundung, false)
		}
		zeile("Noch zu zahlen", su.Zahlbetrag, true)
	}
	s.y += 7

	// Begründung, wenn keine Umsatzsteuer berechnet wird.
	for _, st := range su.Steuern {
		if st.Kat != modell.KatNormal && st.Grund != "" {
			s.absatz(links, rechts-links, st.Grund, s.n, 9.5, pdf.Schwarz)
			s.y += 1.5
		}
	}
}

func (s *setzer) zahlung() {
	r, z := s.r, s.r.Zahlung
	if t := strings.TrimSpace(z.Bedingungen); t != "" {
		s.platz(12)
		s.absatz(links, rechts-links, t, s.n, 9.5, pdf.Schwarz)
	}
	if r.Art != modell.ArtGutschrift && z.SkontoTage > 0 && !z.SkontoProzent.IstNull() && r.Summen.Zahlbetrag > 0 {
		skonto := r.Summen.Zahlbetrag.Prozent(z.SkontoProzent, 2)
		bis := ""
		if t, ok := rechnen.Datum(r.Datum); ok {
			bis = " bis zum " + t.AddDate(0, 0, z.SkontoTage).Format("02.01.2006")
		}
		s.absatz(links, rechts-links, fmt.Sprintf("Bei Zahlung%s: %s Skonto, zu zahlen %s.",
			bis, Euro(skonto, r.Waehrung), Euro(r.Summen.Zahlbetrag-skonto, r.Waehrung)), s.n, 9.5, pdf.Schwarz)
	}
	if r.Art != modell.ArtGutschrift && z.Art != "59" && z.IBAN != "" {
		s.y += 1.5
		s.platz(10)
		t := "Bankverbindung: " + strings.Join(nichtLeer(mit("", z.Kontoinhaber), mit("IBAN ", ibanGruppen(z.IBAN)), mit("BIC ", z.BIC), z.Bank), " · ")
		s.absatz(links, rechts-links, t, s.n, 9.5, pdf.Schwarz)
		if z.Verwendungszweck != "" {
			s.absatz(links, rechts-links, "Verwendungszweck: "+z.Verwendungszweck, s.n, 9.5, pdf.Schwarz)
		}
	}
	if z.Art == "59" {
		s.absatz(links, rechts-links, strings.Join(nichtLeer(mit("Mandatsreferenz ", z.Mandat), mit("Gläubiger-ID ", z.GlaeubigerID), mit("IBAN ", ibanGruppen(z.LastschriftIBAN))), " · "), s.n, 9.5, pdf.Schwarz)
	}
	s.y += 3
}

func (s *setzer) schlusstexte() {
	for _, h := range s.r.Hinweise {
		if strings.TrimSpace(h.Text) != "" {
			s.absatz(links, rechts-links, h.Text, s.n, 9.5, pdf.Schwarz)
			s.y += 1.5
		}
	}
	if t := strings.TrimSpace(s.r.Schluss); t != "" {
		s.absatz(links, rechts-links, t, s.n, 9.5, pdf.Schwarz)
	}
}

func (s *setzer) fusszeilen() {
	v, e, z := s.r.Verkaeufer, s.o.Einst, s.r.Zahlung
	spalten := [][]string{
		nichtLeer(v.Name, v.Strasse, strings.TrimSpace(v.PLZ+" "+v.Ort)),
		nichtLeer(mit("Tel. ", v.Telefon), v.Email, e.Webseite),
		nichtLeer(z.Bank, mit("IBAN ", ibanGruppen(z.IBAN)), mit("BIC ", z.BIC)),
		nichtLeer(mit("USt-IdNr. ", v.UStID), mit("St.-Nr. ", v.Steuernummer), v.Register, e.Geschaeftsleitung, mit("GLN ", v.GLN)),
	}
	xs := []float64{links, 72, 110, 152}
	seiten := s.dok.Seiten()
	for nr, p := range seiten {
		p.Linie(links, fussOben, rechts, fussOben, 0.3, hellgrau)
		for i, sp := range spalten {
			breite := 40.0
			if i+1 < len(xs) {
				breite = xs[i+1] - xs[i] - 2
			} else {
				breite = rechts - xs[i]
			}
			y := fussOben + 3.6
			for j, t := range sp {
				if j >= 5 {
					break
				}
				p.Text(xs[i], y, kuerze(s.n, 7, t, breite), s.n, 7, grau)
				y += 3.1
			}
		}
		if len(seiten) > 1 {
			p.TextRechts(rechts, 267.5, fmt.Sprintf("Seite %d von %d", nr+1, len(seiten)), s.n, 8, grau)
		}
		if len(s.o.XML) > 0 {
			p.TextMitte(105, 291.5, "Diese Datei enthält die Rechnung zusätzlich in maschinenlesbarer Form (ZUGFeRD / Factur-X, EN 16931).", s.n, 6.5, grau)
		}
	}
}

func nichtLeer(teile ...string) []string {
	var l []string
	for _, t := range teile {
		if strings.TrimSpace(t) != "" {
			l = append(l, strings.TrimSpace(t))
		}
	}
	return l
}

func mit(vorsatz, wert string) string {
	if strings.TrimSpace(wert) == "" {
		return ""
	}
	return vorsatz + strings.TrimSpace(wert)
}

// kuerze schneidet einen Text ab, der breiter ist als der Platz.
func kuerze(f *pdf.Schrift, groesse float64, text string, breite float64) string {
	if f.Breite(text, groesse) <= breite*pdf.MM {
		return text
	}
	runen := []rune(text)
	for len(runen) > 1 && f.Breite(string(runen)+"…", groesse) > breite*pdf.MM {
		runen = runen[:len(runen)-1]
	}
	return strings.TrimSpace(string(runen)) + "…"
}

func ibanGruppen(s string) string {
	s = strings.ToUpper(strings.ReplaceAll(s, " ", ""))
	var b strings.Builder
	for i, c := range s {
		if i > 0 && i%4 == 0 {
			b.WriteByte(' ')
		}
		b.WriteRune(c)
	}
	return b.String()
}
