// Package partner kennt die Zusatzanforderungen einzelner Handelspartner.
// Eine E-Rechnung kann nach der Norm fehlerfrei sein und trotzdem
// zurückkommen, weil dem Empfänger Angaben fehlen, die er für seine
// automatische Verarbeitung braucht - etwa die GLN des Markts oder die
// Kostenstelle.
//
// Stand der Angaben: November 2026. Quellen stehen bei jedem Profil. Was die
// Unternehmen nicht öffentlich dokumentieren, ist als Warnung statt als
// Fehler eingestuft und im Text als "üblich" gekennzeichnet.
package partner

import (
	"strings"

	"marktrechnung/internal/codes"
	"marktrechnung/internal/modell"
)

// Profil beschreibt einen Handelspartner oder eine Gruppe.
type Profil struct {
	ID   string `json:"id"`
	Name string `json:"name"`
	Kurz string `json:"kurz"` // ein Satz für die Auswahl
	// Hinweise erklären, was vor der ersten Rechnung zu erledigen ist und
	// worauf beim Versand zu achten ist.
	Hinweise []string `json:"hinweise"`
	Quellen  []string `json:"quellen"`
	// Formate, die der Partner laut eigener Angabe annimmt (leer = alle).
	Formate []string `json:"formate"`
	// MaxMB ist die größte erlaubte E-Mail in Megabyte (0 = unbekannt).
	MaxMB int `json:"maxMB"`
	// Nur der Rechnungsanhang, kein rechnungsrelevanter Text in der E-Mail.
	NeutralerText bool `json:"neutralerText"`
	// Vorlagen sind Rechnungsempfänger mit öffentlich bekannten Stammdaten.
	Vorlagen []Vorlage `json:"vorlagen"`
}

// Vorlage ist ein vorbereiteter Kunde.
type Vorlage struct {
	Titel   string       `json:"titel"`
	Hinweis string       `json:"hinweis"`
	Kunde   modell.Kunde `json:"kunde"`
}

const (
	// IDs der Profile.
	Handel = "handel"
	Rewe   = "rewe"
	Edeka  = "edeka"
)

const quelleVeR = "Verband elektronische Rechnung: Mindeststandard „E-Rechnung bereit für den Handel“ (09/2026)"

// Profile liefert alle bekannten Profile.
func Profile() []Profil {
	l := profile()
	for i := range l {
		// Leere Listen statt "nichts", damit die Oberfläche nicht stolpert.
		if l[i].Vorlagen == nil {
			l[i].Vorlagen = []Vorlage{}
		}
		if l[i].Formate == nil {
			l[i].Formate = []string{}
		}
	}
	return l
}

func profile() []Profil {
	return []Profil{
		{
			ID: Rewe, Name: "REWE Group", Kurz: "REWE Markt, PENNY, REWE-Zentralfinanz und weitere Gesellschaften der REWE Group",
			Hinweise: []string{
				"Vor der ersten E-Rechnung ist eine Anmeldung nötig: Format und Versandweg müssen mit REWE vereinbart und getestet werden. Die Anmeldung läuft über e-rechnungen.onboarding@rewe-group.com; dabei die eigene Kreditorennummer (Lieferantennummer bei REWE) nennen. An dieses Postfach keine echten Rechnungen schicken.",
				"REWE nimmt Rechnungen nur von angemeldeten Absenderadressen an (höchstens fünf). Eine Rechnung von einer nicht angemeldeten Adresse oder vor dem bestätigten Starttermin wird automatisch abgelehnt und nicht gebucht.",
				"Pflicht auf jeder Rechnung: die 10-stellige Kostenstelle, die GLN des Rechnungsempfängers und die GLN des Warenempfängers (Markt). Der Auftraggeber bei REWE teilt sie auf Anfrage mit.",
				"Der Namenszusatz der Zweigniederlassung gehört zum Empfängernamen, zum Beispiel „REWE Markt GmbH ZwNl. West“.",
				"Je E-Mail nur eine Rechnung, je Datei nur eine Rechnung. Der E-Mail-Text wird nicht gelesen - dort darf nichts Rechnungsrelevantes stehen.",
				"Der öffentlich beschriebene E-Mail-Weg gilt laut REWE derzeit für Kostenrechnungen. Für Warenrechnungen bitte beim Onboarding nachfragen, welcher Weg und welche Adresse gelten.",
				"Selbstständige REWE-Kaufleute sind eigene Firmen und stehen nicht in der REWE-Kundenliste: Rechnungsempfänger, GLN und Rechnungsadresse direkt beim Markt erfragen.",
			},
			Quellen: []string{
				"REWE Group: E-Rechnungen bei der REWE Group – https://service.rewe-group.com/de/rechnungsstellung/e-rechnungen-bei-der-rewe-group/",
				"REWE Group: Häufig gestellte Fragen zur Rechnungsstellung – https://service.rewe-group.com/de/rechnungsstellung/haufig-gestellte-fragen/",
				"REWE Group: Starterkit Zentraler Belegeingang und Kundenliste (auf denselben Seiten verlinkt)",
			},
			Formate:       []string{modell.FormatZugferd, modell.FormatXRechnungCII},
			MaxMB:         30,
			NeutralerText: true,
			Vorlagen: []Vorlage{
				{
					Titel:   "REWE Markt GmbH",
					Hinweis: "Bitte den Namenszusatz der Zweigniederlassung ergänzen und deren GLN eintragen (laut REWE-Kundenliste zum Beispiel ZwNl. West 4388093000005, ZwNl. Nord 4388091000007). Adresse und Rechnungspostfach vor der ersten Rechnung mit der aktuellen Kundenliste von REWE abgleichen.",
					Kunde: modell.Kunde{
						Partei: modell.Partei{Name: "REWE Markt GmbH ZwNl. ", Strasse: "Domstraße 20", PLZ: "50668", Ort: "Köln", Land: "DE", UStID: "DE812706034"},
						Profil: Rewe, Format: modell.FormatZugferd,
						RechnungsEmail: "rechnung.rewemarkt@rewe-group.com",
					},
				},
				{
					Titel:   "PENNY Markt GmbH",
					Hinweis: "Adresse, GLN und Rechnungspostfach vor der ersten Rechnung mit der aktuellen Kundenliste von REWE abgleichen.",
					Kunde: modell.Kunde{
						Partei: modell.Partei{Name: "PENNY Markt GmbH", Strasse: "Domstraße 20", PLZ: "50668", Ort: "Köln", Land: "DE", UStID: "DE202748117", GLN: "4388290000006"},
						Profil: Rewe, Format: modell.FormatZugferd,
						RechnungsEmail: "rechnung.pennymarkt@rewe-group.com",
					},
				},
				{
					Titel:   "Selbstständiger REWE-Kaufmann",
					Hinweis: "Name, Anschrift, USt-IdNr., GLN und Rechnungsadresse direkt beim Markt erfragen - jeder Kaufmann ist eine eigene Firma.",
					Kunde:   modell.Kunde{Partei: modell.Partei{Land: "DE"}, Profil: Handel, Format: modell.FormatZugferd},
				},
			},
		},
		{
			ID: Edeka, Name: "EDEKA-Verbund", Kurz: "EDEKA-Regionalgesellschaften, Netto Marken-Discount und selbstständige EDEKA-Kaufleute",
			Hinweise: []string{
				"EDEKA veröffentlicht die Rechnungsadressen nicht. Vor der ersten E-Rechnung bei der abrechnenden Gesellschaft (Regionalgesellschaft oder Markt) erfragen: die E-Mail-Adresse für E-Rechnungen, den Leitfaden zum elektronischen Rechnungsversand sowie die GLN des Rechnungsempfängers und der belieferten Märkte.",
				"Wer Rechnungsempfänger ist, hängt vom Vertrag ab: Bei Direktlieferung ist es meist der Markt selbst (der Kaufmann als eigene Firma), bei Streckenlieferung und Zentralregulierung die Regionalgesellschaft.",
				"EDEKA verlangt die GLN in eigenen Feldern der Rechnung: Rechnungsempfänger, Warenempfänger (Markt) und möglichst auch die eigene GLN als Lieferant. Eine GLN im Firmennamen oder im Freitext zählt nicht. Die eigene GLN und die GTIN der Artikel vergibt GS1 Germany.",
				"Üblich sind außerdem Bestellnummer, Lieferscheinnummer, Lieferdatum und GTIN je Position; in der Regel eine Rechnung je Lieferschein.",
				"Versand: eine Rechnung und ein Anhang je E-Mail, höchstens 10 MB, die Rechnungsnummer im Betreff. An das Rechnungspostfach keine Mahnungen oder andere Schreiben schicken.",
				"EDEKA prüft auch das PDF streng (PDF/A-3). Dieses Programm erzeugt gültiges PDF/A-3; alternativ kann die reine XML-Datei (XRechnung) gesendet werden.",
				"Laut Leitfaden von EDEKA Nordwest gehen ab 01.01.2027 nicht konforme Rechnungen automatisch mit Fehlerangabe zurück.",
			},
			Quellen: []string{
				"EDEKA Nordwest: Leitfaden zum elektronischen Rechnungsversand, V1.0 (06/2026) – bei EDEKA anfordern",
				"GS1 Germany: Anwendungsempfehlung EN 16931, Teil 1, V2.0 (12/2025)",
				quelleVeR,
			},
			Formate: []string{modell.FormatZugferd, modell.FormatXRechnungCII},
			MaxMB:   10,
			Vorlagen: []Vorlage{
				{
					Titel:   "EDEKA-Regionalgesellschaft",
					Hinweis: "Genaue Firmierung, Anschrift, USt-IdNr., GLN und Rechnungspostfach bei der Regionalgesellschaft erfragen.",
					Kunde:   modell.Kunde{Partei: modell.Partei{Name: "EDEKA ", Land: "DE"}, Profil: Edeka, Format: modell.FormatZugferd},
				},
				{
					Titel:   "Selbstständiger EDEKA-Kaufmann",
					Hinweis: "Name, Anschrift, USt-IdNr., GLN und Rechnungsadresse direkt beim Markt erfragen - jeder Kaufmann ist eine eigene Firma.",
					Kunde:   modell.Kunde{Partei: modell.Partei{Name: "EDEKA ", Land: "DE"}, Profil: Edeka, Format: modell.FormatZugferd},
				},
			},
		},
		{
			ID: Handel, Name: "Handel allgemein", Kurz: "Andere Händler und Märkte, die GLN, Bestell- und Lieferscheinnummer erwarten",
			Hinweise: []string{
				"Der Lebensmittelhandel verarbeitet Rechnungen automatisch und gleicht sie mit Bestellung und Wareneingang ab. Dafür braucht er die GLN von Rechnungsempfänger und Warenempfänger, die Bestell- und die Lieferscheinnummer sowie die GTIN der Artikel.",
				"Rechnungsadresse und GLN vor der ersten Rechnung beim Kunden erfragen.",
			},
			Quellen: []string{quelleVeR, "GS1 Germany: Anwendungsempfehlung EN 16931, Teil 1, V2.0 (12/2025)"},
		},
	}
}

// Finde liefert das Profil mit der ID.
func Finde(id string) (Profil, bool) {
	for _, p := range Profile() {
		if p.ID == id {
			return p, true
		}
	}
	return Profil{}, false
}

func m(regel, schwere, feld, text string) modell.Meldung {
	return modell.Meldung{Regel: regel, Schwere: schwere, Feld: feld, Text: text}
}

// Regeln prüft eine ausgerechnete Rechnung gegen die Anforderungen des
// Profils.
func Regeln(id string, r modell.Rechnung) []modell.Meldung {
	if _, ok := Finde(id); !ok {
		return nil
	}
	var l []modell.Meldung
	name := map[string]string{Rewe: "REWE", Edeka: "EDEKA", Handel: "Der Handel"}[id]
	kuerzel := strings.ToUpper(id)

	// Gemeinsame Anforderungen des Handels.
	if r.Kaeufer.GLN == "" {
		l = append(l, m(kuerzel+"-RE-GLN", modell.Fehler, "kaeufer.gln",
			name+" verlangt die GLN des Rechnungsempfängers (13 Ziffern, Feld BT-46). Sie wird beim Kunden hinterlegt; der Ansprechpartner dort nennt sie auf Anfrage."))
	}
	if r.Lieferort.Leer() || r.Lieferort.GLN == "" {
		l = append(l, m(kuerzel+"-WE-GLN", modell.Fehler, "lieferort.gln",
			name+" verlangt die GLN des Warenempfängers, also des belieferten Markts (13 Ziffern, Feld BT-71). Bitte den Markt mit seiner GLN auswählen oder eintragen."))
	}
	if r.Leistungsdatum == "" {
		l = append(l, m(kuerzel+"-LIEFERDATUM", modell.Fehler, "leistungsdatum",
			"Das Lieferdatum fehlt (Feld BT-72). "+name+" gleicht die Rechnung damit gegen den Wareneingang ab."))
	}
	if r.Waehrung != "" && r.Waehrung != "EUR" {
		l = append(l, m(kuerzel+"-EUR", modell.Fehler, "waehrung", "Rechnungen an den deutschen Handel müssen in Euro ausgestellt sein."))
	}

	stufe := modell.Warnung
	switch id {
	case Rewe:
		ks := strings.TrimSpace(r.Kostenstelle)
		if ks == "" {
			l = append(l, m("REWE-KOSTENSTELLE", modell.Fehler, "kostenstelle",
				"REWE verlangt auf jeder Rechnung die 10-stellige Kostenstelle (Feld BT-19). Der Auftraggeber bei REWE teilt sie auf Anfrage mit."))
		} else if len(ks) != 10 || strings.Trim(ks, "0123456789") != "" {
			l = append(l, m("REWE-KOSTENSTELLE", modell.Fehler, "kostenstelle",
				"Die REWE-Kostenstelle besteht aus genau 10 Ziffern - eingetragen ist „"+ks+"“."))
		}
		if r.Verkaeufer.Kennung == "" {
			l = append(l, m("REWE-KREDITOR", modell.Warnung, "verkaeufer.kennung",
				"Die Lieferantennummer (Kreditorennummer) bei REWE fehlt. REWE fragt sie bei der Anmeldung ab; mit ihr wird die Rechnung sicher zugeordnet. Sie wird beim Kunden hinterlegt."))
		}
		if !r.Lieferort.Leer() && !r.Lieferort.HatAdresse() {
			l = append(l, m("REWE-LEISTUNGSORT", modell.Warnung, "lieferort.strasse",
				"REWE möchte den Ort der Leistung auf der Rechnung sehen, zum Beispiel die Anschrift des Markts. Bitte beim Markt Straße, PLZ und Ort ergänzen."))
		}
		if r.Bestellnummer == "" && r.Lieferscheinnummer == "" {
			l = append(l, m("REWE-BEZUG", modell.Info, "bestellnummer",
				"Weder Bestell- noch Lieferscheinnummer sind angegeben. Wenn es eine gibt, hilft sie REWE bei der Zuordnung."))
		}
		if n := strings.ToLower(r.Kaeufer.Name); strings.Contains(n, "rewe markt") && !strings.Contains(n, "zwnl") && !strings.Contains(n, "zweigniederlassung") {
			l = append(l, m("REWE-ZWNL", modell.Info, "kaeufer.name",
				"Bei der REWE Markt GmbH soll der Zusatz der Zweigniederlassung im Namen stehen, zum Beispiel „REWE Markt GmbH ZwNl. West“."))
		}
	case Edeka:
		if r.Verkaeufer.GLN == "" {
			l = append(l, m("EDEKA-LIEFERANT-GLN", modell.Warnung, "verkaeufer.gln",
				"Die eigene GLN als Lieferant fehlt (Feld BT-29). EDEKA erwartet sie nach den uns bekannten Vorgaben; die GLN vergibt GS1 Germany. Sie wird unter Einstellungen bei den Firmendaten eingetragen."))
		}
		if r.Zahlung.Verwendungszweck == "" {
			l = append(l, m("EDEKA-ZWECK", modell.Warnung, "zahlung.verwendungszweck",
				"Der Verwendungszweck fehlt (Feld BT-83). Üblich ist die Rechnungsnummer."))
		}
		erlaubt := map[string]bool{"H87": true, "EA": true, "KGM": true, "MTR": true, "LTR": true}
		for i, p := range r.Positionen {
			if p.Einheit != "" && !erlaubt[p.Einheit] {
				l = append(l, m("EDEKA-EINHEIT", modell.Info, feldPos(i, "einheit"),
					"Position "+p.Nr+": EDEKA nennt als übliche Einheiten Stück, Kilogramm, Liter und Meter. Die Einheit „"+codes.EinheitKurz(p.Einheit)+"“ bitte mit EDEKA abstimmen."))
				break
			}
		}
	case Handel:
		if r.Verkaeufer.GLN == "" && r.Verkaeufer.Kennung == "" {
			l = append(l, m("HANDEL-LIEFERANT", modell.Warnung, "verkaeufer.kennung",
				"Weder die eigene GLN noch die Lieferantennummer beim Kunden ist angegeben (Feld BT-29). Der Handel ordnet die Rechnung darüber dem Lieferanten zu."))
		}
	}

	// Bezug auf Bestellung und Lieferschein, Artikelkennung.
	if id != Rewe {
		if r.Bestellnummer == "" {
			l = append(l, m(kuerzel+"-BESTELLNR", stufe, "bestellnummer",
				"Die Bestellnummer des Kunden fehlt (Feld BT-13). "+name+" schickt Rechnungen ohne Bestellbezug häufig zurück. Gibt es wirklich keine Bestellnummer, bitte mit dem Kunden klären, was einzutragen ist."))
		}
		if r.Lieferscheinnummer == "" {
			l = append(l, m(kuerzel+"-LIEFERSCHEIN", stufe, "lieferscheinnummer",
				"Die Lieferscheinnummer fehlt (Feld BT-16). Üblich ist eine Rechnung je Lieferschein mit dessen Nummer."))
		}
	}
	ohneGTIN := 0
	erste := -1
	for i, p := range r.Positionen {
		if p.GTIN == "" {
			ohneGTIN++
			if erste < 0 {
				erste = i
			}
		}
	}
	if ohneGTIN > 0 && r.Art != modell.ArtGutschrift {
		schwere := modell.Warnung
		if id == Rewe {
			schwere = modell.Info
		}
		text := "Bei einer Position fehlt die GTIN (EAN, Feld BT-157)."
		if ohneGTIN > 1 {
			text = "Bei mehreren Positionen fehlt die GTIN (EAN, Feld BT-157)."
		}
		l = append(l, m(kuerzel+"-GTIN", schwere, feldPos(erste, "gtin"),
			text+" Für Handelsware erwartet der Handel die GTIN je Artikel; ohne sie kann die Position nicht automatisch zugeordnet werden."))
	}

	if p, _ := Finde(id); len(p.Formate) > 0 && r.Format != "" {
		ok := false
		for _, f := range p.Formate {
			ok = ok || f == r.Format
		}
		if !ok {
			l = append(l, m(kuerzel+"-FORMAT", modell.Warnung, "format",
				name+" nennt als angenommene Formate ZUGFeRD (Profil EN 16931) und XRechnung in der Syntax CII. Bitte eines davon wählen."))
		}
	}
	return l
}

func feldPos(i int, feld string) string {
	return "positionen." + itoa(i) + "." + feld
}

func itoa(i int) string {
	if i < 0 {
		i = 0
	}
	if i < 10 {
		return string(rune('0' + i))
	}
	return itoa(i/10) + string(rune('0'+i%10))
}
