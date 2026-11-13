package server

import (
	"strings"
	"testing"

	"marktrechnung/internal/modell"
	"marktrechnung/internal/partner"
)

func TestStammdatenPruefung(t *testing.T) {
	firma := modell.Einstellungen{IBAN: "DE89 3704 0044 0532 0130 00"}
	firma.Firma = modell.Partei{Name: "Hofladen Sonnenwiese", Strasse: "Wiesenweg 3", PLZ: "50999", Ort: "Köln", Land: "DE", UStID: "DE123456789", GLN: "4012345000009"}
	gut := modell.Kunde{ID: "k1", Partei: modell.Partei{Name: "Muster Markt GmbH", Strasse: "Handelsstraße 20", PLZ: "50668", Ort: "Köln", Land: "DE", UStID: "DE811111111", GLN: "4000001000005"},
		Profil: partner.Rewe, RechnungsEmail: "rechnung@mustermarkt.example",
		Lieferorte: []modell.Lieferort{{ID: "m1", Name: "Markt Lindenthal", GLN: "4000001000012", Strasse: "Dürener Straße 100", PLZ: "50931", Ort: "Köln", Land: "DE"}}}
	artikel := []modell.Artikel{{Bezeichnung: "Blütenhonig 500 g", GTIN: "4012345000016"}}

	if erg := pruefeStammdaten(firma, []modell.Kunde{gut}, artikel); len(erg.Befunde) != 0 || erg.Kunden != 1 || erg.Maerkte != 1 || erg.Artikel != 1 {
		t.Fatalf("saubere Stammdaten beanstandet: %+v", erg)
	}

	// Lauter Fehler, wie sie beim Abtippen entstehen.
	schlecht := gut
	schlecht.ID, schlecht.Name = "k2", "Tippfehler GmbH"
	schlecht.GLN = "4000001000006"                                     // Prüfziffer falsch
	schlecht.PLZ = "5066"                                              // vier Ziffern
	schlecht.UStID = "DE81111111"                                      // acht Ziffern
	schlecht.RechnungsEmail = "e-rechnungen.onboarding@rewe-group.com" // Test-Postfach
	schlecht.Lieferorte = []modell.Lieferort{
		{Name: "Markt A", GLN: "4000001000029", Strasse: "Hauptstraße", PLZ: "50667", Ort: "Köln", Land: "DE"}, // Hausnummer fehlt
		{Name: "Markt B", GLN: "4000001000029", Strasse: "Ring 1", PLZ: "50667", Ort: "Köln", Land: "DE"},      // dieselbe GLN
		{Name: "Markt C", GLN: "4012345000009", Strasse: "Ring 2", PLZ: "50667", Ort: "", Land: "DE"},          // eigene GLN, Ort fehlt
		{Name: "Markt D", Strasse: "Ring 3", PLZ: "50667", Ort: "Köln", Land: "DE", Kostenstelle: "123"},       // GLN fehlt, Kostenstelle falsch
	}
	doppelt := gut
	doppelt.ID, doppelt.Name, doppelt.Lieferorte = "k3", "Muster Markt (zweiter Eintrag)", gut.Lieferorte
	firma.IBAN = "DE89 3704 0044 0532 0130 01"
	artikel = append(artikel, modell.Artikel{Bezeichnung: "Waldhonig", GTIN: "4012345000017"}, modell.Artikel{Bezeichnung: "Ohne alles"})

	erg := pruefeStammdaten(firma, []modell.Kunde{gut, schlecht, doppelt}, artikel)
	var texte []string
	for _, b := range erg.Befunde {
		texte = append(texte, b.Bereich+"|"+b.Schwere+"|"+b.Kunde+"|"+b.Markt+"|"+b.Text)
	}
	alle := strings.Join(texte, "\n")
	for _, erwartet := range []string{
		"firma|fehler|||Die IBAN",
		"kunde|fehler|Tippfehler GmbH||Die PLZ „5066“",
		"kunde|fehler|Tippfehler GmbH||Die GLN „4000001000006“ ist nicht gültig",
		"kunde|fehler|Tippfehler GmbH||Die USt-IdNr. „DE81111111“",
		"kunde|warnung|Tippfehler GmbH||„e-rechnungen.onboarding@rewe-group.com“ sieht nach einem Test-Postfach aus",
		"markt|warnung|Tippfehler GmbH|Markt A|Bei der Straße „Hauptstraße“ steht keine Hausnummer",
		"markt|warnung|Tippfehler GmbH|Markt B|Dieselbe GLN (4000001000029) hat auch der Markt „Markt A“",
		"markt|fehler|Tippfehler GmbH|Markt C|Als GLN des Markts ist Ihre eigene GLN eingetragen",
		"markt|fehler|Tippfehler GmbH|Markt C|Die Anschrift ist unvollständig: Ort fehlt",
		"markt|fehler|Tippfehler GmbH|Markt D|Die GLN des Markts fehlt",
		"markt|fehler|Tippfehler GmbH|Markt D|Die REWE-Kostenstelle „123“",
		"kunde|warnung|Muster Markt (zweiter Eintrag)||Dieselbe GLN (4000001000005) ist auch beim Kunden „Muster Markt GmbH“",
		"artikel|fehler|||Die GTIN „4012345000017“ ist nicht gültig",
		"artikel|warnung|||Ein Artikel hat weder GTIN noch Artikelnummer",
	} {
		if !strings.Contains(alle, erwartet) {
			t.Errorf("Befund fehlt: %s", erwartet)
		}
	}
	if len(erg.Befunde) != 14 {
		t.Errorf("%d Befunde, erwartet 14:\n%s", len(erg.Befunde), alle)
	}

	// Ohne Firmendaten gibt es genau einen Befund zur Firma.
	if erg := pruefeStammdaten(modell.Einstellungen{}, nil, nil); len(erg.Befunde) != 1 || erg.Befunde[0].Bereich != "firma" {
		t.Errorf("leere Firma: %+v", erg.Befunde)
	}
}
