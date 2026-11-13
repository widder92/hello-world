package server

import (
	"fmt"
	"net/http"
	"regexp"
	"strings"

	"marktrechnung/internal/codes"
	"marktrechnung/internal/modell"
	"marktrechnung/internal/partner"
	"marktrechnung/internal/post"
)

// Prüfung der Stammdaten: Sind die eigenen Firmendaten, die Kunden, ihre
// Märkte und die Artikel vollständig und in sich stimmig? Geprüft wird, was
// sich ohne Nachfrage beim Handel feststellen lässt - Vollständigkeit,
// Prüfziffern, Schreibweisen, Doppeltes und Widersprüche. Ob eine GLN
// wirklich zu einem Markt gehört, weiß nur der Handel.

type stammBefund struct {
	Schwere string `json:"schwere"` // "fehler" oder "warnung"
	Bereich string `json:"bereich"` // "firma", "kunde", "markt" oder "artikel"
	KundeID string `json:"kundeId,omitempty"`
	Kunde   string `json:"kunde,omitempty"`
	Markt   string `json:"markt,omitempty"`
	Artikel string `json:"artikel,omitempty"`
	Text    string `json:"text"`
}

type stammPruefung struct {
	Kunden  int           `json:"kunden"`
	Maerkte int           `json:"maerkte"`
	Artikel int           `json:"artikel"`
	Befunde []stammBefund `json:"befunde"`
}

var (
	ustDE     = regexp.MustCompile(`^DE[0-9]{9}$`)
	ustAndere = regexp.MustCompile(`^[A-Z]{2}[0-9A-Z+*]{2,12}$`)
	plzDE     = regexp.MustCompile(`^[0-9]{5}$`)
	hausnr    = regexp.MustCompile(`[0-9]`)
)

// ustFalsch meldet, ob eine USt-IdNr. schon der Form nach nicht stimmen kann.
func ustFalsch(s string) bool {
	s = strings.ToUpper(strings.ReplaceAll(strings.TrimSpace(s), " ", ""))
	if s == "" {
		return false
	}
	if strings.HasPrefix(s, "DE") {
		return !ustDE.MatchString(s)
	}
	return !ustAndere.MatchString(s)
}

// testpostfach erkennt Adressen, die der Handel nur für die Testphase nennt.
func testpostfach(adresse string) bool {
	n := strings.ToLower(adresse)
	return strings.Contains(n, "onboarding@") || strings.Contains(n, "-test@") || strings.Contains(n, ".test@")
}

// anschrift prüft Straße, PLZ und Ort. Zurück kommen die Beanstandungen
// und, was nur auffällt: Eine Straße ohne Hausnummer kann stimmen (Markt in
// einem Einkaufszentrum), ist aber öfter ein Versehen.
func anschrift(strasse, plz, ort, land string) (l, auffaellig []string) {
	var fehlt []string
	if strings.TrimSpace(strasse) == "" {
		fehlt = append(fehlt, "Straße")
	}
	if strings.TrimSpace(plz) == "" {
		fehlt = append(fehlt, "PLZ")
	}
	if strings.TrimSpace(ort) == "" {
		fehlt = append(fehlt, "Ort")
	}
	if len(fehlt) > 0 {
		l = append(l, "Die Anschrift ist unvollständig: "+strings.Join(fehlt, ", ")+" fehlt.")
	}
	if p := strings.TrimSpace(plz); p != "" && (land == "" || land == "DE") && !plzDE.MatchString(p) {
		l = append(l, "Die PLZ „"+p+"“ hat nicht fünf Ziffern.")
	}
	if s := strings.TrimSpace(strasse); s != "" && !hausnr.MatchString(s) {
		auffaellig = append(auffaellig, "Bei der Straße „"+s+"“ steht keine Hausnummer. Bitte prüfen, ob sie fehlt.")
	}
	return l, auffaellig
}

func (s *Server) stammdatenPruefen(w http.ResponseWriter, r *http.Request) {
	e, err := s.Speicher.Einstellungen()
	if err != nil {
		fehler(w, 500, err)
		return
	}
	kunden, err := s.Speicher.Kunden()
	if err != nil {
		fehler(w, 500, err)
		return
	}
	artikel, err := s.Speicher.Artikel()
	if err != nil {
		fehler(w, 500, err)
		return
	}
	antworte(w, 200, pruefeStammdaten(e, kunden, artikel))
}

func pruefeStammdaten(e modell.Einstellungen, kunden []modell.Kunde, artikel []modell.Artikel) stammPruefung {
	erg := stammPruefung{Kunden: len(kunden), Artikel: len(artikel), Befunde: []stammBefund{}}
	melde := func(b stammBefund) { erg.Befunde = append(erg.Befunde, b) }

	// Handel im Kundenstamm? Dann wird die eigene GLN gebraucht.
	mitRewe, mitHandel := false, false
	for _, k := range kunden {
		mitRewe = mitRewe || k.Profil == partner.Rewe
		mitHandel = mitHandel || k.Profil != ""
	}

	// ---------------------------------------------------------- eigene Firma
	f := e.Firma
	firma := func(schwere, text string) { melde(stammBefund{Schwere: schwere, Bereich: "firma", Text: text}) }
	if strings.TrimSpace(f.Name) == "" {
		firma(modell.Fehler, "Die eigenen Firmendaten sind noch nicht eingetragen.")
	} else {
		falsch, auffaellig := anschrift(f.Strasse, f.PLZ, f.Ort, f.Land)
		for _, t := range falsch {
			firma(modell.Fehler, t)
		}
		for _, t := range auffaellig {
			firma(modell.Warnung, t)
		}
		if strings.TrimSpace(f.UStID) == "" && strings.TrimSpace(f.Steuernummer) == "" {
			firma(modell.Fehler, "Es fehlt die USt-IdNr. oder die Steuernummer - eine von beiden gehört in jede Rechnung.")
		}
		if ustFalsch(f.UStID) {
			firma(modell.Fehler, "Die eigene USt-IdNr. „"+f.UStID+"“ hat nicht die richtige Form (DE und neun Ziffern).")
		}
		switch {
		case f.GLN == "" && mitRewe:
			firma(modell.Fehler, "Die eigene GLN fehlt. REWE verlangt sie in jeder Rechnung; vergeben wird sie von GS1 Germany.")
		case f.GLN == "" && mitHandel:
			firma(modell.Warnung, "Die eigene GLN fehlt. Der Handel erwartet sie in der Regel; vergeben wird sie von GS1 Germany.")
		case f.GLN != "" && !codes.GLNGueltig(f.GLN):
			firma(modell.Fehler, "Die eigene GLN „"+f.GLN+"“ ist nicht gültig (13 Ziffern, die letzte ist eine Prüfziffer).")
		}
		switch iban := strings.ReplaceAll(e.IBAN, " ", ""); {
		case iban == "":
			firma(modell.Warnung, "Es ist keine Bankverbindung eingetragen.")
		case !codes.IBANGueltig(iban):
			firma(modell.Fehler, "Die IBAN „"+e.IBAN+"“ ist nicht gültig - bitte auf Tippfehler prüfen.")
		}
	}

	// ----------------------------------------------------- Kunden und Märkte
	kundeMitGLN := map[string]string{}
	for _, k := range kunden {
		k := k
		erg.Maerkte += len(k.Lieferorte)
		name := k.Name
		kunde := func(schwere, text string) {
			melde(stammBefund{Schwere: schwere, Bereich: "kunde", KundeID: k.ID, Kunde: name, Text: text})
		}
		streng := k.Profil == partner.Rewe || k.Profil == partner.Edeka
		wer := "Der Handel"
		if p, ok := partner.Finde(k.Profil); ok && streng {
			wer = p.Name
		}

		falsch, auffaellig := anschrift(k.Strasse, k.PLZ, k.Ort, k.Land)
		for _, t := range falsch {
			kunde(modell.Fehler, t)
		}
		for _, t := range auffaellig {
			kunde(modell.Warnung, t)
		}
		switch {
		case k.GLN == "" && streng:
			kunde(modell.Fehler, "Die GLN des Rechnungsempfängers fehlt. "+wer+" verlangt sie.")
		case k.GLN == "" && k.Profil != "":
			kunde(modell.Warnung, "Die GLN des Rechnungsempfängers fehlt. Der Handel erwartet sie in der Regel.")
		case k.GLN != "" && !codes.GLNGueltig(k.GLN):
			kunde(modell.Fehler, "Die GLN „"+k.GLN+"“ ist nicht gültig (13 Ziffern, die letzte ist eine Prüfziffer).")
		case k.GLN != "" && k.GLN == f.GLN:
			kunde(modell.Fehler, "Als GLN des Kunden ist Ihre eigene GLN eingetragen.")
		case k.GLN != "":
			if anderer, doppelt := kundeMitGLN[k.GLN]; doppelt {
				kunde(modell.Warnung, "Dieselbe GLN ("+k.GLN+") ist auch beim Kunden „"+anderer+"“ eingetragen. Ist das derselbe Kunde zweimal?")
			} else {
				kundeMitGLN[k.GLN] = name
			}
		}
		switch {
		case ustFalsch(k.UStID):
			kunde(modell.Fehler, "Die USt-IdNr. „"+k.UStID+"“ hat nicht die richtige Form (für Deutschland: DE und neun Ziffern).")
		case strings.TrimSpace(k.UStID) == "" && k.Profil == partner.Rewe:
			kunde(modell.Fehler, "Die USt-IdNr. des Kunden fehlt. REWE verlangt sie in jeder Rechnung.")
		}
		switch adresse := strings.TrimSpace(k.RechnungsEmail); {
		case adresse == "":
			kunde(modell.Warnung, "Es ist keine E-Mail-Adresse für Rechnungen eingetragen - so lässt sich nichts aus dem Programm versenden.")
		default:
			if _, err := post.Adressen(adresse); err != nil {
				kunde(modell.Fehler, "Die E-Mail-Adresse „"+adresse+"“ ist nicht gültig.")
			} else if testpostfach(adresse) {
				kunde(modell.Warnung, "„"+adresse+"“ sieht nach einem Test-Postfach aus. Echte Rechnungen gehen an die Adresse, die der Kunde nach der Freigabe nennt.")
			}
		}
		if streng && len(k.Lieferorte) == 0 {
			kunde(modell.Warnung, "Es ist kein belieferter Markt hinterlegt. "+wer+" verlangt in der Rechnung den Markt mit GLN und Anschrift.")
		}

		marktMitGLN := map[string]string{}
		for _, l := range k.Lieferorte {
			marktname := l.Name
			if marktname == "" {
				marktname = "Markt ohne Namen"
			}
			markt := func(schwere, text string) {
				melde(stammBefund{Schwere: schwere, Bereich: "markt", KundeID: k.ID, Kunde: name, Markt: marktname, Text: text})
			}
			if strings.TrimSpace(l.Name) == "" {
				markt(modell.Fehler, "Der Markt hat keinen Namen.")
			}
			schwere := modell.Warnung
			if streng {
				schwere = modell.Fehler
			}
			falsch, auffaellig := anschrift(l.Strasse, l.PLZ, l.Ort, l.Land)
			for _, t := range falsch {
				markt(schwere, t)
			}
			for _, t := range auffaellig {
				markt(modell.Warnung, t)
			}
			switch {
			case l.GLN == "":
				markt(schwere, "Die GLN des Markts fehlt.")
			case !codes.GLNGueltig(l.GLN):
				markt(modell.Fehler, "Die GLN „"+l.GLN+"“ ist nicht gültig (13 Ziffern, die letzte ist eine Prüfziffer).")
			case l.GLN == f.GLN:
				markt(modell.Fehler, "Als GLN des Markts ist Ihre eigene GLN eingetragen.")
			default:
				if anderer, doppelt := marktMitGLN[l.GLN]; doppelt {
					markt(modell.Warnung, "Dieselbe GLN ("+l.GLN+") hat auch der Markt „"+anderer+"“. Jeder Markt hat eine eigene GLN - bitte prüfen.")
				} else {
					marktMitGLN[l.GLN] = marktname
				}
			}
			if ks := strings.TrimSpace(l.Kostenstelle); ks != "" && k.Profil == partner.Rewe && (len(ks) != 10 || strings.Trim(ks, "0123456789") != "") {
				markt(modell.Fehler, "Die REWE-Kostenstelle „"+ks+"“ besteht nicht aus genau 10 Ziffern.")
			}
		}
	}

	// ---------------------------------------------------------------- Artikel
	artikelMitGTIN := map[string]string{}
	ohneKennung := 0
	for _, a := range artikel {
		a := a
		art := func(schwere, text string) {
			melde(stammBefund{Schwere: schwere, Bereich: "artikel", Artikel: a.Bezeichnung, Text: text})
		}
		switch {
		case a.GTIN == "" && a.Artikelnummer == "":
			ohneKennung++
		case a.GTIN != "" && !codes.GTINGueltig(a.GTIN):
			art(modell.Fehler, "Die GTIN „"+a.GTIN+"“ ist nicht gültig - bitte mit dem Strichcode auf der Ware vergleichen.")
		case a.GTIN != "":
			if anderer, doppelt := artikelMitGTIN[a.GTIN]; doppelt {
				art(modell.Warnung, "Dieselbe GTIN ("+a.GTIN+") hat auch der Artikel „"+anderer+"“.")
			} else {
				artikelMitGTIN[a.GTIN] = a.Bezeichnung
			}
		}
	}
	if ohneKennung > 0 && mitHandel {
		text := fmt.Sprintf("%d Artikel haben weder GTIN noch Artikelnummer. Der Handel verlangt je Position eine von beiden.", ohneKennung)
		if ohneKennung == 1 {
			text = "Ein Artikel hat weder GTIN noch Artikelnummer. Der Handel verlangt je Position eine von beiden."
		}
		melde(stammBefund{Schwere: modell.Warnung, Bereich: "artikel", Text: text})
	}
	return erg
}
