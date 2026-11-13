package server

import (
	"bytes"
	"errors"
	"fmt"
	"io"
	"net/http"
	"strings"

	"marktrechnung/internal/cii"
	"marktrechnung/internal/modell"
	"marktrechnung/internal/partner"
	"marktrechnung/internal/pruefung"
	"marktrechnung/internal/rechnen"
	"marktrechnung/internal/xmlbaum"
	"marktrechnung/internal/zugferd"
)

// Rechnungen aus einem anderen Programm einlesen (zum Beispiel aus Lexware
// Office): prüfen, was fehlt, und als Entwurf übernehmen. Beim Übernehmen
// ergänzt das Programm, was es aus den eigenen Stammdaten kennt - die GLN
// des Kunden, den Markt, die Kostenstelle, die GTIN der Artikel.

type importErgebnis struct {
	Dateiart    string           `json:"dateiart"`
	Rechnung    modell.Rechnung  `json:"rechnung"`
	Meldungen   []modell.Meldung `json:"meldungen"`
	Auffaellig  []string         `json:"auffaellig"`
	Lesbar      bool             `json:"lesbar"`
	Fehler      string           `json:"fehler,omitempty"`
	Uebernehmen bool             `json:"uebernehmen"`
	// Stand: "neu", "vorhanden" (als fertige Rechnung) oder "entwurf"
	// (schon einmal übernommen, noch nicht fertiggestellt).
	Stand       string `json:"stand"`
	VorhandenID string `json:"vorhandenId,omitempty"`
	// FirmaLeer: Die eigenen Firmendaten sind noch nicht eingetragen.
	FirmaLeer bool `json:"firmaLeer"`
	// Kunde: Name des passenden Kunden in den Stammdaten, sonst leer.
	Kunde string `json:"kunde"`
}

// liesRechnungsdatei liest ein ZUGFeRD-PDF oder eine XML-Datei.
func liesRechnungsdatei(daten []byte) (re modell.Rechnung, dateiart string, auffaellig []string, err error) {
	xml := daten
	if bytes.HasPrefix(bytes.TrimLeft(daten[:min(len(daten), 64)], "\xef\xbb\xbf \r\n\t"), []byte("%PDF")) {
		dateiart = "PDF mit eingebetteter E-Rechnung (ZUGFeRD / Factur-X)"
		if xml, err = zugferd.XMLAusPDF(daten); err != nil {
			return re, "PDF", nil, err
		}
		if !bytes.Contains(daten, []byte("pdfaid:part")) {
			auffaellig = append(auffaellig, "Das PDF weist sich nicht als PDF/A aus. ZUGFeRD verlangt PDF/A-3; strenge Empfänger wie EDEKA lehnen andere PDFs ab.")
		} else if !bytes.Contains(daten, []byte("<pdfaid:part>3")) && !bytes.Contains(daten, []byte(`pdfaid:part="3"`)) {
			auffaellig = append(auffaellig, "Das PDF ist kein PDF/A-3. ZUGFeRD verlangt genau diese Fassung.")
		}
	} else {
		dateiart = "XML-Datei"
	}
	baum, err := xmlbaum.Lies(xml)
	if err != nil {
		return re, dateiart, auffaellig, errors.New("Die Datei ist weder ein PDF noch gültiges XML: " + err.Error())
	}
	if baum.Name == "Invoice" || baum.Name == "CreditNote" {
		return re, dateiart, auffaellig, errors.New("Die Datei ist eine E-Rechnung in der Syntax UBL. REWE und EDEKA nennen als Format ZUGFeRD oder XRechnung in der Syntax CII - eine UBL-Rechnung kann schon deshalb abgelehnt werden. Dieses Programm prüft und erzeugt CII.")
	}
	re, mehr, err := cii.LiesBaum(baum)
	return re, dateiart, append(auffaellig, mehr...), err
}

func (s *Server) importPruefen(w http.ResponseWriter, r *http.Request) {
	daten, err := io.ReadAll(io.LimitReader(r.Body, 40<<20))
	if err != nil || len(daten) == 0 {
		fehler(w, 400, errors.New("es wurde keine Datei übertragen"))
		return
	}
	erg := importErgebnis{Meldungen: []modell.Meldung{}, Auffaellig: []string{}, Stand: "neu"}
	re, dateiart, auffaellig, err := liesRechnungsdatei(daten)
	erg.Dateiart = dateiart
	erg.Auffaellig = append(erg.Auffaellig, auffaellig...)
	if err != nil {
		erg.Fehler = err.Error()
		antworte(w, 200, erg)
		return
	}
	re.Herkunft = r.URL.Query().Get("name")
	s.beurteile(&erg, re, r.URL.Query().Get("profil"), true)
	antworte(w, 200, erg)
}

// beurteile prüft eine eingelesene Rechnung und stellt fest, ob und wie sie
// sich übernehmen lässt. ausDatei: Die Rechnung stammt aus einer
// E-Rechnungs-Datei; dann werden auch deren Summen nachgerechnet.
func (s *Server) beurteile(erg *importErgebnis, re modell.Rechnung, profil string, ausDatei bool) {
	kunden, _ := s.Speicher.Kunden()
	kunde := findeKunde(kunden, re.Kaeufer)
	if kunde != nil {
		erg.Kunde = kunde.Name
	}
	// Ohne ausdrückliche Vorgabe gelten die Regeln, die zum Kunden gehören.
	switch {
	case profil != "" && profil != "auto":
		if profil == "keine" {
			profil = ""
		}
	case kunde != nil:
		profil = kunde.Profil
	default:
		profil = rateProfil(re.Kaeufer.Name)
	}
	re.Profil = profil
	if re.Summen != nil {
		re.Herkunftsbetrag = re.Summen.Brutto
	}
	var aus modell.Rechnung
	erg.Meldungen, aus = pruefung.Pruefe(re, pruefung.Optionen{Fremd: ausDatei})
	if !ausDatei {
		// Ohne Datei gibt es keine Summen zum Nachrechnen; für die Anzeige
		// gelten die hier berechneten.
		re.Summen = aus.Summen
		if !re.Herkunftsbetrag.IstNull() && aus.Summen.Brutto != re.Herkunftsbetrag {
			erg.Auffaellig = append(erg.Auffaellig, "Lexware Office nennt als Rechnungsbetrag "+re.Herkunftsbetrag.Deutsch(2, 2)+
				" €, nach den Regeln der E-Rechnung ergeben sich "+aus.Summen.Brutto.Deutsch(2, 2)+" €. Bitte im Entwurf prüfen.")
		}
	}
	if erg.Meldungen == nil {
		erg.Meldungen = []modell.Meldung{}
	}
	erg.Rechnung, erg.Lesbar = re, true

	// Übernehmen ist nur sinnvoll, wenn die Rechnung von der eigenen Firma
	// stammt.
	e, _ := s.Speicher.Einstellungen()
	erg.FirmaLeer = strings.TrimSpace(e.Firma.Name) == ""
	erg.Uebernehmen = gleicheFirma(e.Firma, re.Verkaeufer)
	if alle, err := s.Speicher.Rechnungen(); err == nil && re.Nummer != "" {
		for _, a := range alle {
			if !strings.EqualFold(a.Nummer, re.Nummer) {
				continue
			}
			if a.Status != modell.StatusEntwurf {
				erg.Stand, erg.VorhandenID = "vorhanden", a.ID
				break
			}
			if a.Herkunft != "" {
				erg.Stand, erg.VorhandenID = "entwurf", a.ID
			}
		}
	}
}

func normal(s string) string {
	return strings.ToLower(strings.Join(strings.Fields(strings.NewReplacer(".", " ", ",", " ", "-", " ").Replace(s)), " "))
}

func gleicheFirma(a, b modell.Partei) bool {
	if a.Name == "" {
		return false
	}
	if a.UStID != "" && strings.EqualFold(strings.ReplaceAll(a.UStID, " ", ""), strings.ReplaceAll(b.UStID, " ", "")) {
		return true
	}
	if a.Steuernummer != "" && normal(a.Steuernummer) == normal(b.Steuernummer) {
		return true
	}
	return normal(a.Name) == normal(b.Name)
}

// findeKunde sucht den Rechnungsempfänger in den Stammdaten: über die GLN,
// sonst über den Namen.
func findeKunde(kunden []modell.Kunde, kaeufer modell.Partei) *modell.Kunde {
	for i, k := range kunden {
		if k.GLN != "" && k.GLN == kaeufer.GLN {
			return &kunden[i]
		}
	}
	for i, k := range kunden {
		if normal(k.Name) != "" && normal(k.Name) == normal(kaeufer.Name) {
			return &kunden[i]
		}
	}
	return nil
}

// rateProfil schließt aus dem Namen eines neuen Kunden auf seine Vorgaben.
func rateProfil(name string) string {
	n := strings.ToLower(name)
	switch {
	case strings.Contains(n, "rewe") || strings.Contains(n, "penny"):
		return partner.Rewe
	case strings.Contains(n, "edeka") || strings.Contains(n, "marktkauf") || strings.Contains(n, "netto marken"):
		return partner.Edeka
	}
	return ""
}

func findeLieferort(k modell.Kunde, l modell.Lieferort) *modell.Lieferort {
	for i, x := range k.Lieferorte {
		gleicheAdresse := normal(x.Strasse) != "" && normal(x.Strasse) == normal(l.Strasse) && strings.TrimSpace(x.PLZ) == strings.TrimSpace(l.PLZ)
		if (x.GLN != "" && x.GLN == l.GLN) || (normal(x.Name) != "" && normal(x.Name) == normal(l.Name)) || gleicheAdresse {
			return &k.Lieferorte[i]
		}
	}
	return nil
}

type uebernahme struct {
	// ArtikelAnlegen: Positionen, die es in den Stammdaten noch nicht gibt,
	// werden dort als Artikel angelegt.
	ArtikelAnlegen bool
}

// uebernimm macht aus einer eingelesenen eigenen Rechnung einen Entwurf. Die
// Rechnungsnummer bleibt erhalten. Kunde und Markt werden in den Stammdaten
// gesucht und sonst angelegt; bekannte Artikel liefern ihre GTIN.
func (s *Server) uebernimm(re modell.Rechnung, o uebernahme) (modell.Rechnung, error) {
	if strings.TrimSpace(re.Kaeufer.Name) == "" {
		return re, errors.New("die Rechnung nennt keinen Empfänger")
	}
	alle, err := s.Speicher.Rechnungen()
	if err != nil {
		return re, err
	}
	re.ID = ""
	for _, a := range alle {
		if re.Nummer == "" || !strings.EqualFold(a.Nummer, re.Nummer) {
			continue
		}
		if a.Status != modell.StatusEntwurf {
			return re, fmt.Errorf("die Rechnung %s ist in Marktrechnung bereits fertiggestellt", re.Nummer)
		}
		if a.Herkunft != "" {
			re.ID = a.ID // ein früher übernommener Entwurf wird ersetzt
		}
	}

	kunden, err := s.Speicher.Kunden()
	if err != nil {
		return re, err
	}
	var kunde modell.Kunde
	if k := findeKunde(kunden, re.Kaeufer); k != nil {
		kunde = *k
	} else {
		kunde = modell.Kunde{Partei: re.Kaeufer, Lieferantennummer: re.Verkaeufer.Kennung,
			Profil: rateProfil(re.Kaeufer.Name), Format: modell.FormatZugferd}
		saeubere(&kunde.Partei)
		if re.Kaeufer.Email != "" {
			kunde.RechnungsEmail = re.Kaeufer.Email
		}
	}
	// Markt: aus der Rechnung, sonst der einzige des Kunden.
	kundeGeaendert := kunde.ID == ""
	if !re.Lieferort.Leer() {
		if l := findeLieferort(kunde, *re.Lieferort); l != nil {
			re.Lieferort = l
		} else {
			neu := *re.Lieferort
			neu.ID = ""
			if neu.Name == "" {
				neu.Name = strings.TrimSpace(neu.Strasse + " " + neu.Ort)
			}
			kunde.Lieferorte = append(kunde.Lieferorte, neu)
			kundeGeaendert = true
		}
	}
	if kundeGeaendert {
		if kunde, err = s.Speicher.SpeichereKunde(kunde); err != nil {
			return re, err
		}
		if !re.Lieferort.Leer() {
			re.Lieferort = findeLieferort(kunde, *re.Lieferort)
		}
	}
	if re.Lieferort.Leer() && len(kunde.Lieferorte) == 1 {
		re.Lieferort = &kunde.Lieferorte[0]
	}
	if !re.Lieferort.Leer() {
		kopie := *re.Lieferort
		re.Lieferort = &kopie
		if re.Kostenstelle == "" {
			re.Kostenstelle = kopie.Kostenstelle
		}
	}
	re.KundeID, re.Profil = kunde.ID, kunde.Profil

	// Zahlungsziel und Skonto aus der Rechnung, sonst vom Kunden.
	if t, ok := rechnen.Datum(re.Datum); ok && re.Faellig != "" {
		if f, ok := rechnen.Datum(re.Faellig); ok && !f.Before(t) {
			re.Zahlung.Zieltage = int(f.Sub(t).Hours()/24 + 0.5)
		}
	} else if re.Zahlung.Zieltage == 0 {
		re.Zahlung.Zieltage = kunde.Zieltage
	}
	re.Faellig, re.Zahlung.Bedingungen = "", ""

	if re.Summen != nil && re.Herkunftsbetrag.IstNull() {
		re.Herkunftsbetrag = re.Summen.Brutto
	}
	re.Status, re.Summen = modell.StatusEntwurf, nil
	re.Format, re.Spezifikation, re.Prozess = "", "", ""

	artikel, err := s.Speicher.Artikel()
	if err != nil {
		return re, err
	}
	for i := range re.Positionen {
		p := &re.Positionen[i]
		p.Netto = 0
		// Ein einzelner Rabatt in Prozent erscheint im Eingabefeld "Rabatt %".
		if len(p.ZuAbschlaege) == 1 && !p.ZuAbschlaege[0].Zuschlag && !p.ZuAbschlaege[0].Prozent.IstNull() {
			p.RabattProzent, p.RabattGrund = p.ZuAbschlaege[0].Prozent, p.ZuAbschlaege[0].Grund
			p.ZuAbschlaege = nil
		}
		var treffer *modell.Artikel
		for j, a := range artikel {
			if (p.Artikelnummer != "" && a.Artikelnummer == p.Artikelnummer) || (normal(a.Bezeichnung) != "" && normal(a.Bezeichnung) == normal(p.Bezeichnung)) {
				treffer = &artikel[j]
				break
			}
		}
		switch {
		case treffer != nil:
			// Was die Datei nicht kennt, kommt aus den Stammdaten.
			if p.GTIN == "" {
				p.GTIN = treffer.GTIN
			}
			if p.Artikelnummer == "" {
				p.Artikelnummer = treffer.Artikelnummer
			}
			if p.KundenArtNr == "" {
				p.KundenArtNr = treffer.KundenArtNr[kunde.ID]
			}
			if p.Herkunft == "" {
				p.Herkunft = treffer.Herkunft
			}
			if (p.Einheit == "" || p.Einheit == "C62") && treffer.Einheit != "" {
				p.Einheit = treffer.Einheit
			}
		case o.ArtikelAnlegen && strings.TrimSpace(p.Bezeichnung) != "" && p.Menge > 0:
			neu, err := s.Speicher.SpeichereArtikel(modell.Artikel{Artikelnummer: p.Artikelnummer, GTIN: p.GTIN,
				Bezeichnung: strings.TrimSpace(p.Bezeichnung), Beschreibung: p.Beschreibung, Einheit: p.Einheit,
				Preis: p.Einzelpreis, SteuerSatz: p.SteuerSatz, Herkunft: p.Herkunft})
			if err != nil {
				return re, err
			}
			artikel = append(artikel, neu)
		}
	}

	// Freitexte ohne Kennzeichen werden zu Einleitung und Schlusstext, damit
	// sie in der Oberfläche sichtbar und änderbar sind.
	var uebrig []modell.Hinweis
	for _, h := range re.Hinweise {
		switch {
		case h.Code == "" && re.Einleitung == "" && len(uebrig) == 0:
			re.Einleitung = h.Text
		case h.Code == "" && re.Schluss == "":
			re.Schluss = h.Text
		default:
			uebrig = append(uebrig, h)
		}
	}
	re.Hinweise = uebrig
	if re.Herkunft == "" {
		re.Herkunft = "eingelesene Datei"
	}
	return s.Speicher.SpeichereEntwurf(re)
}

func (s *Server) importUebernehmen(w http.ResponseWriter, r *http.Request) {
	var ein struct {
		Rechnung       modell.Rechnung `json:"rechnung"`
		ArtikelAnlegen bool            `json:"artikelAnlegen"`
	}
	if err := liesJSON(r, &ein); err != nil {
		fehler(w, 400, err)
		return
	}
	e, err := s.Speicher.Einstellungen()
	if err != nil {
		fehler(w, 500, err)
		return
	}
	if !gleicheFirma(e.Firma, ein.Rechnung.Verkaeufer) {
		fehler(w, 422, errors.New("übernehmen lässt sich nur eine Rechnung der eigenen Firma"))
		return
	}
	re, err := s.uebernimm(ein.Rechnung, uebernahme{ArtikelAnlegen: ein.ArtikelAnlegen})
	if err != nil {
		fehler(w, 409, err)
		return
	}
	antworte(w, 200, re)
}

// importFirma trägt die Angaben des Rechnungsstellers aus einer eingelesenen
// Rechnung als eigene Firmendaten ein - nur dort, wo noch nichts steht. So
// füllt die erste Rechnung aus dem alten Programm die Einstellungen.
func (s *Server) importFirma(w http.ResponseWriter, r *http.Request) {
	var ein struct {
		Verkaeufer modell.Partei  `json:"verkaeufer"`
		Zahlung    modell.Zahlung `json:"zahlung"`
	}
	if err := liesJSON(r, &ein); err != nil {
		fehler(w, 400, err)
		return
	}
	e, err := s.Speicher.Einstellungen()
	if err != nil {
		fehler(w, 500, err)
		return
	}
	v := ein.Verkaeufer
	if strings.TrimSpace(v.Name) == "" {
		fehler(w, 422, errors.New("die Rechnung nennt keinen Rechnungssteller"))
		return
	}
	fuelle := func(ziel *string, wert string) {
		if strings.TrimSpace(*ziel) == "" {
			*ziel = strings.TrimSpace(wert)
		}
	}
	f := &e.Firma
	fuelle(&f.Name, v.Name)
	fuelle(&f.Handelsname, v.Handelsname)
	fuelle(&f.Strasse, v.Strasse)
	fuelle(&f.Zusatz, v.Zusatz)
	fuelle(&f.PLZ, v.PLZ)
	fuelle(&f.Ort, v.Ort)
	fuelle(&f.UStID, strings.ToUpper(strings.ReplaceAll(v.UStID, " ", "")))
	fuelle(&f.Steuernummer, v.Steuernummer)
	fuelle(&f.GLN, v.GLN)
	fuelle(&f.Register, v.Register)
	fuelle(&f.Ansprechpartner, v.Ansprechpartner)
	fuelle(&f.Telefon, v.Telefon)
	fuelle(&f.Email, v.Email)
	if f.EAdresse == "" {
		f.EAdresse, f.EAdresseSchema = v.EAdresse, v.EAdresseSchema
		if f.EAdresse == "" && f.Email != "" {
			f.EAdresse, f.EAdresseSchema = f.Email, "EM"
		}
	}
	if v.Land != "" {
		f.Land = strings.ToUpper(v.Land)
	}
	fuelle(&e.IBAN, ein.Zahlung.IBAN)
	fuelle(&e.BIC, ein.Zahlung.BIC)
	fuelle(&e.Kontoinhaber, ein.Zahlung.Kontoinhaber)
	if err := s.Speicher.SetzeEinstellungen(e); err != nil {
		fehler(w, 500, err)
		return
	}
	s.einstellungenLesen(w, r)
}
