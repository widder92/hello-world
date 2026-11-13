// Package pruefung prüft eine Rechnung, bevor sie das Haus verlässt: gegen
// die Regeln der europäischen Norm EN 16931 (Regelnummern "BR-..."), gegen
// die deutschen XRechnung-Regeln ("BR-DE-...") und gegen die Vorgaben des
// Handelspartners. Jede Meldung nennt die Regel, das betroffene Feld und in
// einfachen Worten, was zu tun ist.
//
// Die Prüfung deckt die Regeln ab, an denen Rechnungen in der Praxis
// scheitern. Sie ersetzt keinen amtlichen Validator, wird aber in den Tests
// laufend mit einem solchen abgeglichen.
package pruefung

import (
	"fmt"
	"regexp"
	"sort"
	"strings"

	"marktrechnung/internal/cii"
	"marktrechnung/internal/codes"
	"marktrechnung/internal/dec"
	"marktrechnung/internal/modell"
	"marktrechnung/internal/partner"
	"marktrechnung/internal/rechnen"
)

// Optionen steuern die Prüfung.
type Optionen struct {
	// Fremd: Die Rechnung wurde aus einer Datei eingelesen. Dann werden auch
	// die in der Datei stehenden Summen nachgerechnet, und fehlende Angaben,
	// die das Programm bei eigenen Rechnungen selbst ergänzt, werden gemeldet.
	Fremd bool
	// NummerOffen: Die Rechnungsnummer wird erst beim Fertigstellen vergeben.
	NummerOffen bool
}

type pruefer struct {
	l []modell.Meldung
}

func (p *pruefer) melde(regel, schwere, feld, text string, a ...any) {
	if len(a) > 0 {
		text = fmt.Sprintf(text, a...)
	}
	p.l = append(p.l, modell.Meldung{Regel: regel, Schwere: schwere, Feld: feld, Text: text})
}

func (p *pruefer) fehler(regel, feld, text string, a ...any) {
	p.melde(regel, modell.Fehler, feld, text, a...)
}

func (p *pruefer) warnung(regel, feld, text string, a ...any) {
	p.melde(regel, modell.Warnung, feld, text, a...)
}

var (
	emailMuster = regexp.MustCompile(`^[^@\s]{1,}[^@\s.]@[^@\s.][^@\s]*\.[^@\s.]{2,}$`)
	ustMuster   = regexp.MustCompile(`^[A-Z]{2}[A-Z0-9+*]{2,13}$`)
)

func leer(s string) bool { return strings.TrimSpace(s) == "" }

// Pruefe prüft eine Rechnung und liefert die Meldungen, die schwersten
// zuerst. Zurück kommt außerdem die ausgerechnete Fassung der Rechnung.
func Pruefe(r modell.Rechnung, o Optionen) ([]modell.Meldung, modell.Rechnung) {
	p := &pruefer{}
	a := rechnen.Ausrechnen(r)
	xr := cii.IstXRechnung(r.Format) || strings.Contains(strings.ToLower(r.Spezifikation), "xrechnung")

	p.kopf(r, a, o)
	p.parteien(a, xr, o)
	p.positionen(a)
	p.zuAbschlaege(a)
	p.steuer(a)
	p.zahlung(a, xr, o)
	if xr {
		p.xrechnung(a, o)
	}
	if o.Fremd {
		p.summen(r, a)
		p.fremdHinweise(r, xr)
	}
	p.l = append(p.l, partner.Regeln(r.Profil, a)...)

	rang := map[string]int{modell.Fehler: 0, modell.Warnung: 1, modell.Info: 2}
	sort.SliceStable(p.l, func(i, j int) bool { return rang[p.l[i].Schwere] < rang[p.l[j].Schwere] })
	return p.l, a
}

// HatFehler meldet, ob unter den Meldungen ein Fehler ist.
func HatFehler(l []modell.Meldung) bool {
	for _, m := range l {
		if m.Schwere == modell.Fehler {
			return true
		}
	}
	return false
}

func (p *pruefer) kopf(r, a modell.Rechnung, o Optionen) {
	if leer(r.Nummer) && !o.NummerOffen {
		p.fehler("BR-02", "nummer", "Die Rechnungsnummer fehlt.")
	}
	if strings.ContainsAny(r.Nummer, "\n\t") || len(r.Nummer) > 60 {
		p.fehler("BR-02", "nummer", "Die Rechnungsnummer enthält unzulässige Zeichen oder ist zu lang.")
	}
	datum, ok := rechnen.Datum(r.Datum)
	if !ok {
		p.fehler("BR-03", "datum", "Das Rechnungsdatum fehlt oder ist kein gültiges Datum.")
	}
	if !codes.Gueltig(codes.Belegart, a.Art) {
		p.fehler("BR-04", "art", "Die Belegart „%s“ ist kein gültiger Code.", a.Art)
	}
	if !codes.Gueltig(codes.Waehrung, a.Waehrung) {
		p.fehler("BR-05", "waehrung", "Die Währung „%s“ ist kein gültiger Währungscode (üblich: EUR).", a.Waehrung)
	}

	// Liefer- oder Leistungsdatum: Pflichtangabe nach § 14 UStG.
	hatZeitraum := r.ZeitraumVon != "" || r.ZeitraumBis != ""
	if leer(r.Leistungsdatum) && !hatZeitraum {
		schwere := modell.Fehler
		if o.Fremd {
			schwere = modell.Warnung
		}
		p.melde("UStG-14-LIEFERDATUM", schwere, "leistungsdatum",
			"Das Liefer- bzw. Leistungsdatum fehlt. Es ist eine Pflichtangabe jeder Rechnung (§ 14 UStG) und wird vom Handel zum Abgleich mit dem Wareneingang gebraucht.")
	}
	if !leer(r.Leistungsdatum) {
		if t, gut := rechnen.Datum(r.Leistungsdatum); !gut {
			p.fehler("BT-72", "leistungsdatum", "Das Lieferdatum ist kein gültiges Datum.")
		} else if ok && t.After(datum.AddDate(0, 0, 31)) {
			p.warnung("BT-72", "leistungsdatum", "Das Lieferdatum liegt mehr als einen Monat nach dem Rechnungsdatum. Bitte prüfen.")
		}
	}
	von, okVon := rechnen.Datum(r.ZeitraumVon)
	bis, okBis := rechnen.Datum(r.ZeitraumBis)
	if (r.ZeitraumVon != "" && !okVon) || (r.ZeitraumBis != "" && !okBis) {
		p.fehler("BR-CO-19", "zeitraumVon", "Der Leistungszeitraum enthält ein ungültiges Datum.")
	} else if okVon && okBis && bis.Before(von) {
		p.fehler("BR-29", "zeitraumBis", "Das Ende des Leistungszeitraums liegt vor seinem Beginn.")
	}
	if !leer(r.Faellig) {
		if f, gut := rechnen.Datum(r.Faellig); !gut {
			p.fehler("BT-9", "faellig", "Das Fälligkeitsdatum ist kein gültiges Datum.")
		} else if ok && f.Before(datum) {
			p.warnung("BT-9", "faellig", "Das Fälligkeitsdatum liegt vor dem Rechnungsdatum.")
		}
	}

	for i, v := range r.Vorgaenger {
		if leer(v.Nummer) {
			p.fehler("BR-55", fmt.Sprintf("vorgaenger.%d.nummer", i), "Beim Verweis auf die frühere Rechnung fehlt deren Nummer.")
		}
		if v.Datum != "" {
			if _, gut := rechnen.Datum(v.Datum); !gut {
				p.fehler("BT-26", fmt.Sprintf("vorgaenger.%d.datum", i), "Das Datum der früheren Rechnung ist ungültig.")
			}
		}
	}
	if (a.Art == modell.ArtGutschrift || a.Art == modell.ArtKorrektur) && len(r.Vorgaenger) == 0 {
		p.warnung("BR-DE-26", "vorgaenger.0.nummer",
			"Eine Gutschrift oder Rechnungskorrektur soll die Nummer der ursprünglichen Rechnung nennen, auf die sie sich bezieht.")
	}
	for i, h := range r.Hinweise {
		if h.Code != "" && !codes.Gueltig(codes.Textcode, h.Code) {
			p.fehler("BR-CL-08", fmt.Sprintf("hinweise.%d.code", i), "Der Code „%s“ eines Hinweistexts ist ungültig.", h.Code)
		}
	}
}

func (p *pruefer) gln(regel, feld, wert, wessen string) {
	if wert == "" {
		return
	}
	if strings.Trim(wert, "0123456789") != "" || len(wert) != 13 {
		p.fehler(regel, feld, "Die GLN %s muss aus genau 13 Ziffern bestehen - eingetragen ist „%s“.", wessen, wert)
	} else if !codes.GLNGueltig(wert) {
		p.fehler(regel, feld, "Die GLN %s („%s“) hat eine falsche Prüfziffer - vermutlich ein Tippfehler. Bitte mit der Vorlage vergleichen.", wessen, wert)
	}
}

func (p *pruefer) parteien(a modell.Rechnung, xr bool, o Optionen) {
	v, k := a.Verkaeufer, a.Kaeufer
	wo := "Sie wird unter Einstellungen bei den Firmendaten eingetragen."
	if o.Fremd {
		wo = ""
	}
	if leer(v.Name) {
		p.fehler("BR-06", "verkaeufer.name", "Der Name des Rechnungsstellers fehlt. "+wo)
	}
	if leer(v.Land) && o.Fremd {
		p.fehler("BR-09", "verkaeufer.land", "Beim Rechnungssteller fehlt das Land.")
	} else if !leer(v.Land) && !codes.Gueltig(codes.Land, v.Land) {
		p.fehler("BR-CL-14", "verkaeufer.land", "Das Land des Rechnungsstellers („%s“) ist kein gültiger Ländercode.", v.Land)
	}
	if leer(v.Strasse) || leer(v.PLZ) || leer(v.Ort) {
		schwere := modell.Fehler
		regel := "UStG-14-ANSCHRIFT"
		if xr && (leer(v.PLZ) || leer(v.Ort)) {
			regel = "BR-DE-3"
			if leer(v.PLZ) {
				regel = "BR-DE-4"
			}
		}
		p.melde(regel, schwere, "verkaeufer.strasse", "Die Anschrift des Rechnungsstellers ist unvollständig (Straße, PLZ und Ort). "+wo)
	}
	if leer(k.Name) {
		p.fehler("BR-07", "kaeufer.name", "Der Name des Rechnungsempfängers fehlt. Bitte einen Kunden auswählen.")
	}
	if leer(k.Land) && o.Fremd {
		p.fehler("BR-11", "kaeufer.land", "Beim Rechnungsempfänger fehlt das Land.")
	} else if !leer(k.Land) && !codes.Gueltig(codes.Land, k.Land) {
		p.fehler("BR-CL-14", "kaeufer.land", "Das Land des Rechnungsempfängers („%s“) ist kein gültiger Ländercode.", k.Land)
	}
	if !leer(k.Name) && (leer(k.Strasse) || leer(k.PLZ) || leer(k.Ort)) {
		regel := "UStG-14-ANSCHRIFT"
		if xr && leer(k.Ort) {
			regel = "BR-DE-8"
		} else if xr && leer(k.PLZ) {
			regel = "BR-DE-9"
		}
		p.fehler(regel, "kaeufer.strasse", "Die Anschrift des Rechnungsempfängers ist unvollständig (Straße, PLZ und Ort). Sie wird beim Kunden gepflegt.")
	}

	// Steuerliche Kennung des Rechnungsstellers.
	if leer(v.UStID) && leer(v.Steuernummer) {
		p.fehler("BR-CO-26", "verkaeufer.ustId", "Beim Rechnungssteller fehlen Steuernummer und USt-IdNr. Eine von beiden ist Pflicht (§ 14 UStG). "+wo)
	}
	for _, e := range []struct{ feld, wert, wer string }{{"verkaeufer.ustId", v.UStID, "des Rechnungsstellers"}, {"kaeufer.ustId", k.UStID, "des Rechnungsempfängers"}} {
		id := strings.ToUpper(strings.ReplaceAll(e.wert, " ", ""))
		if id != "" && !ustMuster.MatchString(id) {
			p.fehler("BR-CO-09", e.feld, "Die USt-IdNr. %s („%s“) muss mit dem Länderkürzel beginnen, zum Beispiel DE123456789.", e.wer, e.wert)
		} else if strings.HasPrefix(id, "DE") && (len(id) != 11 || strings.Trim(id[2:], "0123456789") != "") {
			p.fehler("BR-CO-09", e.feld, "Eine deutsche USt-IdNr. besteht aus „DE“ und neun Ziffern - eingetragen ist „%s“.", e.wert)
		}
	}

	p.gln("GLN-VERKAEUFER", "verkaeufer.gln", v.GLN, "des Rechnungsstellers")
	p.gln("GLN-KAEUFER", "kaeufer.gln", k.GLN, "des Rechnungsempfängers")
	if l := a.Lieferort; !l.Leer() {
		p.gln("GLN-LIEFERORT", "lieferort.gln", l.GLN, "des Warenempfängers")
		if l.Land != "" && !codes.Gueltig(codes.Land, l.Land) {
			p.fehler("BR-CL-14", "lieferort.land", "Das Land des Warenempfängers („%s“) ist kein gültiger Ländercode.", l.Land)
		}
		if l.HatAdresse() && xr {
			if leer(l.Ort) {
				p.fehler("BR-DE-10", "lieferort.ort", "Bei der Lieferanschrift fehlt der Ort.")
			}
			if leer(l.PLZ) {
				p.fehler("BR-DE-11", "lieferort.plz", "Bei der Lieferanschrift fehlt die Postleitzahl.")
			}
		}
	}
	for _, e := range []struct{ feld, adr, schema string }{{"verkaeufer.eAdresse", v.EAdresse, v.EAdresseSchema}, {"kaeufer.eAdresse", k.EAdresse, k.EAdresseSchema}} {
		if e.adr == "" {
			continue
		}
		schema := e.schema
		if schema == "" {
			schema = "EM"
		}
		if !codes.Gueltig(codes.EAS, schema) {
			p.fehler("BR-CL-25", e.feld, "Die Art der elektronischen Adresse („%s“) ist kein gültiger Code.", schema)
		} else if schema == "EM" && !emailMuster.MatchString(e.adr) {
			p.fehler("BR-62", e.feld, "Die elektronische Adresse „%s“ ist keine gültige E-Mail-Adresse.", e.adr)
		}
	}
}

func (p *pruefer) positionen(a modell.Rechnung) {
	if len(a.Positionen) == 0 {
		p.fehler("BR-16", "positionen", "Die Rechnung hat keine Position. Mindestens eine ist nötig.")
	}
	nummern := map[string]bool{}
	for i, pos := range a.Positionen {
		f := func(feld string) string { return fmt.Sprintf("positionen.%d.%s", i, feld) }
		nr := pos.Nr
		if nummern[nr] {
			p.fehler("BR-21", f("nr"), "Die Positionsnummer %s kommt doppelt vor.", nr)
		}
		nummern[nr] = true
		if leer(pos.Bezeichnung) {
			p.fehler("BR-25", f("bezeichnung"), "Position %s hat keine Bezeichnung.", nr)
		}
		if pos.Menge.IstNull() {
			p.fehler("BR-22", f("menge"), "Position %s hat keine Menge.", nr)
		}
		if leer(pos.Einheit) {
			p.fehler("BR-23", f("einheit"), "Position %s hat keine Mengeneinheit.", nr)
		} else if !codes.Gueltig(codes.Einheit, pos.Einheit) {
			p.fehler("BR-CL-23", f("einheit"), "Position %s: Die Einheit „%s“ ist kein gültiger Code. In der E-Rechnung sind nur genormte Einheitencodes erlaubt, zum Beispiel H87 für Stück oder KGM für Kilogramm.", nr, pos.Einheit)
		}
		if pos.Einzelpreis < 0 {
			p.fehler("BR-27", f("einzelpreis"), "Position %s: Der Einzelpreis darf nicht negativ sein. Für eine Rücknahme bitte die Menge negativ eingeben.", nr)
		}
		if pos.Einzelpreis.IstNull() {
			p.warnung("BR-26", f("einzelpreis"), "Position %s hat den Einzelpreis 0. Ist das beabsichtigt?", nr)
		}
		if pos.Listenpreis < 0 {
			p.fehler("BR-28", f("listenpreis"), "Position %s: Der Listenpreis darf nicht negativ sein.", nr)
		}
		if pos.Preisbasis < 0 {
			p.fehler("PEPPOL-EN16931-R121", f("preisbasis"), "Position %s: Die Preisbasis muss größer als null sein.", nr)
		}
		if !codes.Gueltig(codes.Steuerkat, pos.SteuerKat) {
			p.fehler("BR-CO-04", f("steuerSatz"), "Position %s hat keine gültige Steuerkategorie.", nr)
		}
		if pos.SteuerSatz < 0 || pos.SteuerSatz > dec.Hundert {
			p.fehler("BR-CO-04", f("steuerSatz"), "Position %s: Der Steuersatz ist nicht plausibel.", nr)
		}
		if pos.GTIN != "" {
			if strings.Trim(pos.GTIN, "0123456789") != "" {
				p.fehler("GTIN", f("gtin"), "Position %s: Die GTIN darf nur Ziffern enthalten - eingetragen ist „%s“.", nr, pos.GTIN)
			} else if !codes.GTINGueltig(pos.GTIN) {
				p.fehler("GTIN", f("gtin"), "Position %s: Die GTIN „%s“ hat eine falsche Länge oder Prüfziffer - vermutlich ein Tippfehler.", nr, pos.GTIN)
			}
		}
		if pos.Herkunft != "" && !codes.Gueltig(codes.Land, pos.Herkunft) {
			p.fehler("BR-CL-15", f("herkunft"), "Position %s: Das Ursprungsland „%s“ ist kein gültiger Ländercode.", nr, pos.Herkunft)
		}
		for _, z := range pos.ZuAbschlaege {
			if leer(z.Grund) && leer(z.Code) {
				regel := "BR-42"
				if z.Zuschlag {
					regel = "BR-44"
				}
				p.fehler(regel, f("rabattProzent"), "Position %s: Ein Nachlass oder Zuschlag braucht eine Begründung, zum Beispiel „Rabatt“.", nr)
			}
			if z.Betrag < 0 {
				p.fehler("BR-41", f("rabattProzent"), "Position %s: Der Betrag eines Nachlasses oder Zuschlags darf nicht negativ sein.", nr)
			}
		}
		vonOK, bisOK := true, true
		von, bis := pos.ZeitraumVon, pos.ZeitraumBis
		if von != "" {
			_, vonOK = rechnen.Datum(von)
		}
		if bis != "" {
			_, bisOK = rechnen.Datum(bis)
		}
		if !vonOK || !bisOK {
			p.fehler("BR-CO-20", f("zeitraumVon"), "Position %s: Der Zeitraum enthält ein ungültiges Datum.", nr)
		} else if von != "" && bis != "" && bis < von {
			p.fehler("BR-30", f("zeitraumBis"), "Position %s: Das Ende des Zeitraums liegt vor seinem Beginn.", nr)
		}
	}
}

func (p *pruefer) zuAbschlaege(a modell.Rechnung) {
	for i, z := range a.ZuAbschlaege {
		feld := fmt.Sprintf("zuAbschlaege.%d.betrag", i)
		art := "Nachlass"
		if z.Zuschlag {
			art = "Zuschlag"
		}
		if z.Betrag.IstNull() {
			p.fehler("BR-31", feld, "Ein %s auf die Rechnung hat keinen Betrag.", art)
		}
		if z.Betrag < 0 {
			p.fehler("BR-31", feld, "Der Betrag eines Nachlasses oder Zuschlags darf nicht negativ sein. Statt eines negativen Nachlasses bitte einen Zuschlag anlegen.")
		}
		if leer(z.Grund) && leer(z.Code) {
			regel := "BR-33"
			if z.Zuschlag {
				regel = "BR-38"
			}
			p.fehler(regel, fmt.Sprintf("zuAbschlaege.%d.grund", i), "Ein %s auf die Rechnung braucht eine Begründung, zum Beispiel „Aktionsrabatt“ oder „Fracht“.", art)
		}
		if !codes.Gueltig(codes.Steuerkat, z.SteuerKat) {
			p.fehler("BR-32", fmt.Sprintf("zuAbschlaege.%d.steuerSatz", i), "Ein %s auf die Rechnung hat keine gültige Steuerkategorie.", art)
		}
	}
}

func (p *pruefer) steuer(a modell.Rechnung) {
	v, k := a.Verkaeufer, a.Kaeufer
	for _, st := range a.Summen.Steuern {
		satz := st.Satz.Deutsch(0, 2)
		switch st.Kat {
		case modell.KatNormal:
			if st.Satz <= 0 {
				p.fehler("BR-S-05", "positionen.0.steuerSatz", "Steuerpflichtige Positionen brauchen einen Steuersatz größer als 0 (in Deutschland 19 % oder 7 %). Für steuerfreie Umsätze bitte die passende Steuerart wählen.")
			} else if (a.Verkaeufer.Land == "DE" || a.Verkaeufer.Land == "") && st.Satz != dec.Int(19) && st.Satz != dec.Int(7) {
				p.warnung("STEUERSATZ", "positionen.0.steuerSatz", "Der Steuersatz %s %% ist in Deutschland ungewöhnlich (üblich: 19 %% oder 7 %%; 10,7 %%, 5,5 %% und 7,8 %% nur bei Land- und Forstwirten mit Durchschnittssatz). Bitte prüfen.", satz)
			}
		case modell.KatBefreit:
			if !st.Satz.IstNull() {
				p.fehler("BR-E-05", "positionen.0.steuerSatz", "Steuerbefreite Positionen müssen den Steuersatz 0 haben.")
			}
			if leer(st.Grund) && leer(st.GrundCode) {
				p.fehler("BR-E-10", "befreiungsgrund", "Für steuerbefreite Umsätze fehlt die Begründung, zum Beispiel der Hinweis auf § 19 UStG (Kleinunternehmer) oder die Vorschrift der Steuerbefreiung.")
			}
		case modell.KatNull:
			if !st.Satz.IstNull() {
				p.fehler("BR-Z-05", "positionen.0.steuerSatz", "Positionen mit Nullsatz müssen den Steuersatz 0 haben.")
			}
		case modell.KatReverseCharge:
			if leer(v.UStID) && leer(v.Steuernummer) {
				p.fehler("BR-AE-02", "verkaeufer.ustId", "Bei Steuerschuldnerschaft des Leistungsempfängers ist die eigene USt-IdNr. oder Steuernummer Pflicht.")
			}
			if leer(k.UStID) && leer(k.Register) {
				p.fehler("BR-AE-02", "kaeufer.ustId", "Bei Steuerschuldnerschaft des Leistungsempfängers muss die USt-IdNr. des Kunden angegeben sein.")
			}
			if !st.Satz.IstNull() {
				p.fehler("BR-AE-05", "positionen.0.steuerSatz", "Bei Steuerschuldnerschaft des Leistungsempfängers ist der Steuersatz 0.")
			}
		case modell.KatEULieferung:
			if leer(v.UStID) || leer(k.UStID) {
				p.fehler("BR-IC-02", "kaeufer.ustId", "Für eine innergemeinschaftliche Lieferung müssen die eigene USt-IdNr. und die des Kunden angegeben sein.")
			}
			if !st.Satz.IstNull() {
				p.fehler("BR-IC-05", "positionen.0.steuerSatz", "Bei einer innergemeinschaftlichen Lieferung ist der Steuersatz 0.")
			}
			if leer(a.Leistungsdatum) && leer(a.ZeitraumVon) {
				p.fehler("BR-IC-11", "leistungsdatum", "Für eine innergemeinschaftliche Lieferung ist das Lieferdatum Pflicht.")
			}
			if a.Lieferort.Leer() || leer(a.Lieferort.Land) {
				p.fehler("BR-IC-12", "lieferort.land", "Für eine innergemeinschaftliche Lieferung muss das Land der Lieferanschrift angegeben sein.")
			}
		case modell.KatAusfuhr:
			if leer(v.UStID) {
				p.fehler("BR-G-02", "verkaeufer.ustId", "Für eine Ausfuhrlieferung muss die eigene USt-IdNr. angegeben sein.")
			}
			if !st.Satz.IstNull() {
				p.fehler("BR-G-05", "positionen.0.steuerSatz", "Bei einer Ausfuhrlieferung ist der Steuersatz 0.")
			}
		case modell.KatNichtSteuerb:
			if len(a.Summen.Steuern) > 1 {
				p.fehler("BR-O-11", "positionen.0.steuerSatz", "Nicht steuerbare Umsätze dürfen nicht mit anderen Steuerarten in einer Rechnung gemischt werden.")
			}
			if !leer(v.UStID) || !leer(k.UStID) {
				p.fehler("BR-O-02", "verkaeufer.ustId", "Bei nicht steuerbaren Umsätzen dürfen keine USt-IdNrn. angegeben werden.")
			}
		}
	}
}

func (p *pruefer) zahlung(a modell.Rechnung, xr bool, o Optionen) {
	z := a.Zahlung
	wo := " Sie wird unter Einstellungen eingetragen."
	if o.Fremd {
		wo = ""
	}
	if !codes.Gueltig(codes.Zahlungsart, z.Art) {
		p.fehler("BR-49", "zahlung.art", "Die Zahlungsart „%s“ ist kein gültiger Code.", z.Art)
		return
	}
	gutschrift := a.Art == modell.ArtGutschrift
	switch z.Art {
	case "30", "58":
		if leer(z.IBAN) {
			if !gutschrift || xr {
				regel := "BR-61"
				if xr {
					regel = "BR-DE-23-a"
				}
				p.fehler(regel, "zahlung.iban", "Für die Zahlung per Überweisung fehlt die IBAN."+wo)
			}
		} else if !codes.IBANGueltig(z.IBAN) {
			p.fehler("BR-DE-19", "zahlung.iban", "Die IBAN „%s“ ist nicht gültig (Länge oder Prüfsumme stimmen nicht) - vermutlich ein Tippfehler.", z.IBAN)
		}
		if b := strings.ReplaceAll(z.BIC, " ", ""); b != "" && len(b) != 8 && len(b) != 11 {
			p.fehler("BIC", "zahlung.bic", "Der BIC „%s“ muss 8 oder 11 Zeichen lang sein.", z.BIC)
		}
	case "59":
		if leer(z.Mandat) {
			p.fehler("PEPPOL-EN16931-R061", "zahlung.mandat", "Für die SEPA-Lastschrift fehlt die Mandatsreferenz.")
		}
		if leer(z.GlaeubigerID) {
			p.fehler("BR-DE-30", "zahlung.glaeubigerId", "Für die SEPA-Lastschrift fehlt die Gläubiger-Identifikationsnummer.")
		}
		if leer(z.LastschriftIBAN) {
			p.fehler("BR-DE-31", "zahlung.lastschriftIban", "Für die SEPA-Lastschrift fehlt die IBAN des Kontos, von dem eingezogen wird.")
		} else if !codes.IBANGueltig(z.LastschriftIBAN) {
			p.fehler("BR-DE-20", "zahlung.lastschriftIban", "Die IBAN „%s“ für die Lastschrift ist nicht gültig.", z.LastschriftIBAN)
		}
	}
	if z.SkontoTage < 0 || z.SkontoProzent < 0 || z.SkontoProzent >= dec.Hundert {
		p.fehler("BR-DE-18", "zahlung.skontoProzent", "Die Skonto-Angaben sind nicht plausibel.")
	}
	if z.SkontoTage > 0 && z.Zieltage > 0 && z.SkontoTage > z.Zieltage {
		p.warnung("SKONTO", "zahlung.skontoTage", "Die Skontofrist ist länger als das Zahlungsziel.")
	}
	if (z.SkontoTage > 0) != !z.SkontoProzent.IstNull() {
		p.warnung("SKONTO", "zahlung.skontoTage", "Skonto ist unvollständig: Es braucht sowohl die Frist in Tagen als auch den Prozentsatz.")
	}
	if a.Summen.Zahlbetrag > 0 && leer(a.Faellig) && leer(z.Bedingungen) {
		p.fehler("BR-CO-25", "faellig", "Bei einem offenen Betrag muss ein Fälligkeitsdatum oder eine Zahlungsbedingung angegeben sein.")
	}
	if a.Summen.Brutto < 0 {
		p.warnung("NEGATIV", "positionen", "Der Rechnungsbetrag ist negativ. Für eine Erstattung bitte eine Gutschrift (Rechnungskorrektur) mit positiven Beträgen ausstellen - negative Rechnungen lehnen viele Empfänger ab.")
	}
	if z.Vorauszahlung < 0 {
		p.fehler("BT-113", "zahlung.vorauszahlung", "Der bereits gezahlte Betrag darf nicht negativ sein.")
	}
}

// xrechnung prüft die zusätzlichen deutschen Regeln.
func (p *pruefer) xrechnung(a modell.Rechnung, o Optionen) {
	v, k := a.Verkaeufer, a.Kaeufer
	wo := " Die Angabe steht unter Einstellungen bei den Firmendaten."
	if o.Fremd {
		wo = ""
	}
	if leer(a.Kaeuferreferenz) {
		p.fehler("BR-DE-15", "kaeuferreferenz",
			"Die Käuferreferenz fehlt (Feld BT-10, bei Behörden die Leitweg-ID). In einer XRechnung ist sie Pflicht. Gibt der Kunde keine vor, wird üblicherweise seine GLN, die Bestellnummer oder die Kundennummer eingetragen.")
	}
	if leer(v.Ansprechpartner) && leer(v.Telefon) && leer(v.Email) {
		p.fehler("BR-DE-2", "verkaeufer.ansprechpartner", "Die Kontaktdaten des Rechnungsstellers fehlen (Ansprechpartner, Telefon, E-Mail). In einer XRechnung sind sie Pflicht."+wo)
	} else {
		if leer(v.Ansprechpartner) {
			p.fehler("BR-DE-5", "verkaeufer.ansprechpartner", "Beim Rechnungssteller fehlt der Ansprechpartner."+wo)
		}
		if leer(v.Telefon) {
			p.fehler("BR-DE-6", "verkaeufer.telefon", "Beim Rechnungssteller fehlt die Telefonnummer."+wo)
		}
		if leer(v.Email) {
			p.fehler("BR-DE-7", "verkaeufer.email", "Beim Rechnungssteller fehlt die E-Mail-Adresse."+wo)
		}
	}
	if !leer(v.Telefon) {
		ziffern := 0
		for _, c := range v.Telefon {
			if c >= '0' && c <= '9' {
				ziffern++
			}
		}
		if ziffern < 3 {
			p.warnung("BR-DE-27", "verkaeufer.telefon", "Die Telefonnummer des Rechnungsstellers sollte mindestens drei Ziffern enthalten.")
		}
	}
	if !leer(v.Email) && !emailMuster.MatchString(v.Email) {
		p.warnung("BR-DE-28", "verkaeufer.email", "Die E-Mail-Adresse des Rechnungsstellers („%s“) sieht nicht gültig aus.", v.Email)
	}
	if leer(v.EAdresse) {
		p.fehler("PEPPOL-EN16931-R020", "verkaeufer.eAdresse", "Die elektronische Adresse des Rechnungsstellers fehlt (Feld BT-34, üblich ist die E-Mail-Adresse). In einer XRechnung ist sie Pflicht."+wo)
	}
	if leer(k.EAdresse) {
		p.fehler("PEPPOL-EN16931-R010", "kaeufer.eAdresse", "Die elektronische Adresse des Rechnungsempfängers fehlt (Feld BT-49, üblich ist die E-Mail-Adresse des Rechnungseingangs). In einer XRechnung ist sie Pflicht. Sie wird beim Kunden eingetragen.")
	}
	erlaubt := map[string]bool{"326": true, "380": true, "384": true, "389": true, "381": true, "875": true, "876": true, "877": true}
	if !erlaubt[a.Art] {
		p.warnung("BR-DE-17", "art", "Die Belegart „%s“ ist in einer XRechnung nicht vorgesehen.", a.Art)
	}
	for _, st := range a.Summen.Steuern {
		if st.Kat == modell.KatNichtSteuerb {
			p.warnung("BR-DE-14", "positionen.0.steuerSatz", "Die XRechnung verlangt bei jeder Steuerart einen Steuersatz; bei nicht steuerbaren Umsätzen ist das nicht möglich. Bitte das Format ZUGFeRD wählen.")
		}
	}
}

// summen rechnet die Beträge einer eingelesenen Rechnung nach.
func (p *pruefer) summen(r, a modell.Rechnung) {
	ist, soll := r.Summen, a.Summen
	if ist == nil {
		p.fehler("BR-12", "summen", "Die Datei enthält keine Rechnungssummen.")
		return
	}
	for i, pos := range r.Positionen {
		if i < len(a.Positionen) && pos.Netto != a.Positionen[i].Netto {
			p.fehler("BR-CO-LINE", fmt.Sprintf("positionen.%d.netto", i),
				"Position %s: Der Nettobetrag in der Datei ist %s, aus Menge, Preis und Nachlässen ergibt sich aber %s.",
				a.Positionen[i].Nr, pos.Netto.Deutsch(2, 2), a.Positionen[i].Netto.Deutsch(2, 2))
		}
		if pos.Netto.Nachkommastellen() > 2 {
			p.fehler("BR-DEC-23", fmt.Sprintf("positionen.%d.netto", i), "Position %s: Der Nettobetrag hat mehr als zwei Nachkommastellen.", a.Positionen[i].Nr)
		}
	}
	vergleich := func(regel, name string, x, y dec.D) {
		if x != y {
			p.fehler(regel, "summen", "%s: In der Datei steht %s, rechnerisch ergibt sich %s.", name, x.Deutsch(2, 2), y.Deutsch(2, 2))
		}
		if x.Nachkommastellen() > 2 {
			p.fehler("BR-DEC", "summen", "%s hat mehr als zwei Nachkommastellen.", name)
		}
	}
	vergleich("BR-CO-10", "Summe der Positionen (BT-106)", ist.Positionen, soll.Positionen)
	vergleich("BR-CO-11", "Summe der Nachlässe (BT-107)", ist.Abschlaege, soll.Abschlaege)
	vergleich("BR-CO-12", "Summe der Zuschläge (BT-108)", ist.Zuschlaege, soll.Zuschlaege)
	vergleich("BR-CO-13", "Nettobetrag (BT-109)", ist.Netto, soll.Netto)
	// Die Steuer wird je Steuersatz verglichen; ein Cent Abweichung ist bei
	// zeilenweiser Berechnung beim Aussteller üblich, aber nicht regelgerecht.
	if len(ist.Steuern) == 0 {
		p.fehler("BR-CO-18", "summen", "Die Datei enthält keine Aufschlüsselung der Umsatzsteuer.")
	}
	for _, s := range soll.Steuern {
		gefunden := false
		for _, i := range ist.Steuern {
			if i.Kat == s.Kat && i.Satz == s.Satz {
				gefunden = true
				bez := fmt.Sprintf("%s %%", s.Satz.Deutsch(0, 2))
				vergleich("BR-CO-17", "Steuerbetrag zu "+bez+" (BT-117)", i.Betrag, i.Basis.Prozent(i.Satz, 2))
				vergleich("BR-S-08", "Bemessungsgrundlage zu "+bez+" (BT-116)", i.Basis, s.Basis)
			}
		}
		if !gefunden {
			p.fehler("BR-CO-18", "summen", "In der Steueraufschlüsselung fehlt die Zeile für %s %% (Kategorie %s).", s.Satz.Deutsch(0, 2), s.Kat)
		}
	}
	var steuerIst dec.D
	for _, i := range ist.Steuern {
		steuerIst += i.Betrag
	}
	vergleich("BR-CO-14", "Umsatzsteuer gesamt (BT-110)", ist.Steuer, steuerIst)
	vergleich("BR-CO-15", "Bruttobetrag (BT-112)", ist.Brutto, ist.Netto+ist.Steuer)
	vergleich("BR-CO-16", "Zahlbetrag (BT-115)", ist.Zahlbetrag, ist.Brutto-ist.Vorauszahlung+ist.Rundung)
}

// fremdHinweise erklärt bei eingelesenen Rechnungen, was die Kennung des
// Standards bedeutet.
func (p *pruefer) fremdHinweise(r modell.Rechnung, xr bool) {
	spez := r.Spezifikation
	switch {
	case spez == "":
		p.fehler("BR-01", "spezifikation", "Die Datei nennt nicht, nach welchem Standard sie erstellt ist (Feld BT-24).")
	case xr:
		p.melde("FORMAT", modell.Info, "spezifikation", "Die Datei ist eine XRechnung (%s).", spez)
	case spez == cii.LeitlinieEN16931:
		p.melde("FORMAT", modell.Info, "spezifikation",
			"Die Datei folgt der Norm EN 16931 (ZUGFeRD-Profil „EN 16931“, früher „Comfort“). Prüft der Empfänger sie nach den strengeren XRechnung-Regeln, meldet er oft die Fehler BR-DE-15 (Käuferreferenz fehlt) oder BR-DE-2 bis BR-DE-7 (Kontaktdaten fehlen), obwohl die Datei nach EN 16931 in Ordnung ist.")
	case strings.Contains(spez, "basicwl") || strings.Contains(spez, "minimum"):
		p.fehler("FORMAT", "spezifikation",
			"Die Datei nutzt ein eingeschränktes ZUGFeRD-Profil (%s). Die Profile MINIMUM und BASIC WL sind keine vollwertigen E-Rechnungen; der Handel verlangt mindestens das Profil EN 16931.", spez)
	case strings.Contains(spez, "extended"):
		p.melde("FORMAT", modell.Info, "spezifikation", "Die Datei nutzt das ZUGFeRD-Profil EXTENDED. Geprüft werden hier die Angaben, die auch die Norm EN 16931 kennt.")
	default:
		p.warnung("FORMAT", "spezifikation", "Die Kennung des Standards (%s) ist diesem Programm nicht bekannt.", spez)
	}
}
