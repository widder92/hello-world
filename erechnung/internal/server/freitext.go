package server

import (
	"regexp"
	"strings"

	"marktrechnung/internal/codes"
	"marktrechnung/internal/modell"
)

// Angaben aus Freitexten holen.
//
// Programme wie Lexware Office kennen für manche Angaben, die der Handel
// verlangt, kein eigenes Feld. Wer dort Rechnungen schreibt, behilft sich
// und tippt sie als Text ein: "GLN: 4012345000009" in die Anschrift,
// "Lieferschein-Nr.: 4711" und "WE-GLN: ..." in die Bemerkung. Beim Einlesen
// holt Marktrechnung diese Angaben in ihre Felder und nimmt die Zeilen, die
// nur daraus bestanden, aus dem Text heraus - der Handel will Angaben, für
// die es ein Feld gibt, nicht zusätzlich im Freitext sehen.

var (
	// GLN mit Vorsatz: "GLN 123...", "ILN: 123...", "WE-GLN: 123...", "RE-GLN 123..."
	glnImText = regexp.MustCompile(`(?i)(WE-|RE-)?(?:GLN|ILN)\s*[:.\-]?\s*(\d{13})\b`)
	// Lieferschein mit Nummer: "Lieferschein 4711", "Lieferschein-Nr.: LS-12/3"
	lieferscheinImText = regexp.MustCompile(`(?i)\bLieferschein(?:\s*-?\s*(?:Nr\.?|Nummer))?\s*[:.]?\s*([A-Za-z0-9][A-Za-z0-9\-/]*)`)
	// Bestellung mit Nummer: "Bestellnummer: 4500123", "Bestell-Nr. 77", "Bestellung 123"
	bestellungImText = regexp.MustCompile(`(?i)\bBestell(?:ung|nummer|-?\s*Nr\.?)\s*[:.]?\s*([A-Za-z0-9][A-Za-z0-9\-/]*)`)
	// Zeichen, die zwischen solchen Angaben stehen dürfen
	trenner = regexp.MustCompile(`[\s,;|·/]+`)
)

// nummerAus liefert die Nummer hinter einem Stichwort, wenn sie eine ist:
// Sie muss eine Ziffer enthalten ("Vielen Dank für Ihre Bestellung. Wir ..."
// nennt keine Bestellnummer).
func nummerAus(muster *regexp.Regexp, text string) string {
	for _, m := range muster.FindAllStringSubmatch(text, -1) {
		wert := m[1]
		// Folgt die nächste Angabe ohne Leerzeichen, endet die Nummer davor.
		for _, anhang := range []string{"WE-GLN", "RE-GLN", "GLN", "ILN"} {
			if i := strings.Index(strings.ToUpper(wert), anhang); i > 0 {
				wert = wert[:i]
			}
		}
		wert = strings.TrimRight(wert, "-/")
		if strings.ContainsAny(wert, "0123456789") {
			return wert
		}
	}
	return ""
}

// nurAngaben meldet, ob eine Textzeile ausschließlich aus Angaben besteht,
// die jetzt in eigenen Feldern stehen.
func nurAngaben(zeile string, uebernommen map[string]bool) bool {
	rest := zeile
	gefunden := false
	// Erst die Nummern, dann die GLN: Hinter einer Nummer kann ohne
	// Leerzeichen gleich "GLN" folgen.
	for _, muster := range []*regexp.Regexp{lieferscheinImText, bestellungImText, glnImText} {
		rest = muster.ReplaceAllStringFunc(rest, func(treffer string) string {
			m := muster.FindStringSubmatch(treffer)
			wert := m[len(m)-1]
			for w := range uebernommen {
				if w != "" && strings.HasPrefix(wert, w) {
					gefunden = true
					return strings.TrimPrefix(treffer[strings.Index(treffer, wert):], w)
				}
			}
			return treffer
		})
	}
	return gefunden && trenner.ReplaceAllString(rest, "") == ""
}

// entwirre holt GLN, Lieferschein- und Bestellnummer aus Anschrift und
// Freitexten einer eingelesenen Rechnung in ihre Felder. Zurück kommt, was
// dabei gefunden wurde, in Worten.
func entwirre(re *modell.Rechnung) []string {
	var bericht []string
	k := &re.Kaeufer

	// 1. Anschrift des Rechnungsempfängers: Zeilen wie "GLN: 4012345000009".
	zeilen := append([]string{k.Strasse}, strings.Split(k.Zusatz, "\n")...)
	var bleiben []string
	for _, z := range zeilen {
		z = strings.TrimSpace(z)
		if z == "" {
			continue
		}
		m := glnImText.FindStringSubmatch(z)
		if m == nil {
			bleiben = append(bleiben, z)
			continue
		}
		if k.GLN == "" && !strings.EqualFold(m[1], "WE-") && codes.GLNGueltig(m[2]) {
			k.GLN = m[2]
			bericht = append(bericht, "GLN des Rechnungsempfängers ("+m[2]+") aus der Anschrift")
		}
		// Bleibt neben der GLN noch Text, gehört er weiter zur Anschrift.
		if rest := strings.Trim(trenner.ReplaceAllString(glnImText.ReplaceAllString(z, " "), " "), " "); rest != "" {
			bleiben = append(bleiben, rest)
		}
	}
	if len(bleiben) > 0 || k.Strasse != "" || k.Zusatz != "" {
		k.Strasse, k.Zusatz = "", ""
		if len(bleiben) > 0 {
			k.Strasse = bleiben[0]
			k.Zusatz = strings.Join(bleiben[1:], "\n")
		}
	}
	if m := glnImText.FindStringSubmatch(k.Name); m != nil {
		if k.GLN == "" && codes.GLNGueltig(m[2]) {
			k.GLN = m[2]
			bericht = append(bericht, "GLN des Rechnungsempfängers ("+m[2]+") aus dem Namen")
		}
		k.Name = strings.Trim(trenner.ReplaceAllString(glnImText.ReplaceAllString(k.Name, " "), " "), " -,")
	}

	// 2. Freitexte: Lieferschein, Bestellung, GLN des Markts.
	var texte []string
	for _, h := range re.Hinweise {
		texte = append(texte, h.Text)
	}
	alles := strings.Join(append(texte, re.Einleitung, re.Schluss), "\n")
	uebernommen := map[string]bool{}
	if nr := nummerAus(lieferscheinImText, alles); nr != "" {
		if re.Lieferscheinnummer == "" {
			re.Lieferscheinnummer = nr
			bericht = append(bericht, "Lieferscheinnummer "+nr+" aus dem Text")
		}
		uebernommen[re.Lieferscheinnummer] = true
	}
	if nr := nummerAus(bestellungImText, alles); nr != "" {
		if re.Bestellnummer == "" {
			re.Bestellnummer = nr
			bericht = append(bericht, "Bestellnummer "+nr+" aus dem Text")
		}
		uebernommen[re.Bestellnummer] = true
	}
	for _, m := range glnImText.FindAllStringSubmatch(alles, -1) {
		gln := m[2]
		if !codes.GLNGueltig(gln) || gln == re.Verkaeufer.GLN {
			continue
		}
		switch {
		case strings.EqualFold(m[1], "RE-"):
			if k.GLN == "" {
				k.GLN = gln
				bericht = append(bericht, "GLN des Rechnungsempfängers ("+gln+") aus dem Text")
			}
			uebernommen[gln] = k.GLN == gln
		case gln == k.GLN && !strings.EqualFold(m[1], "WE-"):
			uebernommen[gln] = true
		default:
			if re.Lieferort == nil {
				re.Lieferort = &modell.Lieferort{}
			}
			if re.Lieferort.GLN == "" {
				re.Lieferort.GLN = gln
				bericht = append(bericht, "GLN des belieferten Markts ("+gln+") aus dem Text")
			}
			uebernommen[gln] = re.Lieferort.GLN == gln
		}
	}

	// 3. Zeilen, die nur aus diesen Angaben bestanden, entfallen im Text.
	bereinige := func(text string) string {
		var l []string
		for _, z := range strings.Split(text, "\n") {
			if strings.TrimSpace(z) != "" && nurAngaben(z, uebernommen) {
				continue
			}
			l = append(l, z)
		}
		return strings.TrimSpace(strings.Join(l, "\n"))
	}
	if len(uebernommen) > 0 {
		var hinweise []modell.Hinweis
		for _, h := range re.Hinweise {
			if h.Text = bereinige(h.Text); h.Text != "" {
				hinweise = append(hinweise, h)
			}
		}
		re.Hinweise = hinweise
		re.Einleitung, re.Schluss = bereinige(re.Einleitung), bereinige(re.Schluss)
	}
	return bericht
}
