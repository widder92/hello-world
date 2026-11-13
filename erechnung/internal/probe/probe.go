// Package probe liefert Beispielrechnungen für Tests und für die Vorschau.
// Alle Namen, Nummern und Adressen sind erfunden.
package probe

import (
	"marktrechnung/internal/dec"
	"marktrechnung/internal/modell"
)

// Verkaeufer ist eine erfundene Lieferantin.
func Verkaeufer() modell.Partei {
	return modell.Partei{
		Name: "Hofladen Sonnenwiese Inh. Maria Muster", Strasse: "Wiesenweg 3", PLZ: "50999", Ort: "Köln", Land: "DE",
		UStID: "DE123456789", Steuernummer: "214/5123/4567", GLN: "4012345000009",
		EAdresse: "rechnung@sonnenwiese.example", EAdresseSchema: "EM",
		Ansprechpartner: "Maria Muster", Telefon: "+49 221 1234567", Email: "rechnung@sonnenwiese.example",
	}
}

func position(artnr, gtin, name, menge, einheit, preis, satz string) modell.Position {
	return modell.Position{Artikelnummer: artnr, GTIN: gtin, Bezeichnung: name, Menge: dec.Muss(menge),
		Einheit: einheit, Einzelpreis: dec.Muss(preis), SteuerKat: "S", SteuerSatz: dec.Muss(satz)}
}

// Handel ist eine typische Warenrechnung an einen Markt: GLN bei allen
// Beteiligten, Bestell- und Lieferscheinnummer, GTIN je Position, zwei
// Steuersätze, ein Positionsrabatt und Skonto.
func Handel() modell.Rechnung {
	r := modell.Rechnung{
		ID: "probe-handel", Status: modell.StatusEntwurf, Art: modell.ArtRechnung,
		Nummer: "RE-2026-0042", Datum: "2026-11-13", Waehrung: "EUR", Leistungsdatum: "2026-11-12",
		Verkaeufer: Verkaeufer(),
		Kaeufer: modell.Partei{
			Name: "Muster Markt GmbH ZwNl. West", Strasse: "Handelsstraße 20", PLZ: "50668", Ort: "Köln", Land: "DE",
			UStID: "DE811111111", GLN: "4000001000005", Kennung: "10023",
			EAdresse: "rechnungseingang@mustermarkt.example", EAdresseSchema: "EM",
		},
		Lieferort: &modell.Lieferort{Name: "Muster Markt Lindenthal", GLN: "4000001000012",
			Strasse: "Dürener Straße 100", PLZ: "50931", Ort: "Köln", Land: "DE"},
		Kaeuferreferenz: "4000001000005", Bestellnummer: "4500123456", Lieferscheinnummer: "LS-2026-0815",
		Kostenstelle: "1234567890",
		Format:       modell.FormatZugferd,
	}
	r.Verkaeufer.Kennung = "700123"
	r.Positionen = []modell.Position{
		position("H-001", "4012345000016", "Blütenhonig 500 g", "24", "H87", "4.95", "7"),
		position("H-002", "4012345000023", "Waldhonig 250 g", "12", "H87", "3.80", "7"),
		position("K-010", "4012345000030", "Bienenwachskerze", "10", "H87", "6.50", "19"),
		position("E-001", "4012345000047", "Bio-Eier Größe M, 10er-Packung", "30", "XPK", "2.899", "7"),
	}
	r.Positionen[2].RabattProzent = dec.Muss("10")
	r.Positionen[0].KundenArtNr = "880012"
	r.Zahlung = modell.Zahlung{Art: "58", IBAN: "DE89 3704 0044 0532 0130 00", BIC: "COBADEFFXXX",
		Kontoinhaber: "Maria Muster", Bank: "Commerzbank Köln", Verwendungszweck: "RE-2026-0042",
		Zieltage: 30, SkontoTage: 14, SkontoProzent: dec.Muss("2")}
	r.Einleitung = "Vielen Dank für Ihre Bestellung. Wir berechnen Ihnen die folgende Lieferung:"
	return r
}

// Klein ist die Rechnung einer Kleinunternehmerin ohne Umsatzsteuer.
func Klein() modell.Rechnung {
	r := Handel()
	r.ID, r.Nummer = "probe-klein", "RE-2026-0043"
	r.Kleinunternehmer = true
	r.Verkaeufer.UStID = ""
	r.Positionen[2].RabattProzent = 0
	r.Zahlung.SkontoTage, r.Zahlung.SkontoProzent = 0, 0
	r.Zahlung.Verwendungszweck = r.Nummer
	return r
}

// Gutschrift hebt die Beispielrechnung teilweise wieder auf.
func Gutschrift() modell.Rechnung {
	r := Handel()
	r.ID, r.Nummer, r.Art = "probe-gutschrift", "RE-2026-0044", modell.ArtGutschrift
	r.Vorgaenger = []modell.Vorgaenger{{Nummer: "RE-2026-0042", Datum: "2026-11-13"}}
	r.Positionen = r.Positionen[:1]
	r.Positionen[0].Menge = dec.Muss("2")
	r.Positionen[0].Hinweis = "Bruch bei Anlieferung"
	r.Zahlung.SkontoTage, r.Zahlung.SkontoProzent = 0, 0
	r.Zahlung.Verwendungszweck = r.Nummer
	r.Einleitung = ""
	return r
}

// MitRabatt enthält zusätzlich einen Rechnungsrabatt, eine Frachtpauschale
// und eine Anzahlung.
func MitRabatt() modell.Rechnung {
	r := Handel()
	r.ID, r.Nummer = "probe-rabatt", "RE-2026-0045"
	r.ZuAbschlaege = []modell.ZuAbschlag{
		{Prozent: dec.Muss("3"), Grund: "Aktionsrabatt", Code: "95", SteuerKat: "S", SteuerSatz: dec.Muss("7")},
		{Zuschlag: true, Betrag: dec.Muss("9.90"), Grund: "Fracht", Code: "FC", SteuerKat: "S", SteuerSatz: dec.Muss("19")},
	}
	r.Zahlung.Vorauszahlung = dec.Muss("50")
	r.Zahlung.Verwendungszweck = r.Nummer
	return r
}

// Alle liefert alle Beispielrechnungen.
func Alle() []modell.Rechnung {
	return []modell.Rechnung{Handel(), Klein(), Gutschrift(), MitRabatt()}
}
