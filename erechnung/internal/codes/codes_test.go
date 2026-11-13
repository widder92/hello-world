package codes

import "testing"

func TestListen(t *testing.T) {
	for _, e := range Einheiten {
		if !Gueltig(Einheit, e.Code) {
			t.Errorf("Einheit %s steht nicht in der Codeliste", e.Code)
		}
	}
	for _, l := range Laender {
		if !Gueltig(Land, l[0]) {
			t.Errorf("Land %s unbekannt", l[0])
		}
	}
	if !Gueltig(Waehrung, "EUR") || !Gueltig(Zahlungsart, "58") || !Gueltig(EAS, "EM") || Gueltig(Einheit, "Stück") {
		t.Error("Codelisten unvollständig")
	}
}

func TestPruefziffern(t *testing.T) {
	if !GLNGueltig("4388093000005") || GLNGueltig("4388093000006") || GLNGueltig("438809300000") {
		t.Error("GLN-Prüfung falsch")
	}
	if !GTINGueltig("4006381333931") || !GTINGueltig("40170725") || GTINGueltig("4006381333932") {
		t.Error("GTIN-Prüfung falsch")
	}
	if !IBANGueltig("DE89 3704 0044 0532 0130 00") || IBANGueltig("DE89 3704 0044 0532 0130 01") || IBANGueltig("DE8937040044053201300") {
		t.Error("IBAN-Prüfung falsch")
	}
}

func TestEinheitAusName(t *testing.T) {
	faelle := map[string]string{"Stück": "H87", "Stk.": "H87", "Flasche": "XBO", " Karton ": "XCT", "kg": "KGM", "Liter": "LTR", "H87": "H87", "Kanister": "XCI"}
	for name, soll := range faelle {
		if ist, ok := EinheitAusName(name); ist != soll || !ok {
			t.Errorf("EinheitAusName(%q) = %s, %v; erwartet %s", name, ist, ok, soll)
		}
	}
	if c, ok := EinheitAusName("Download-Code"); c != "C62" || ok {
		t.Errorf("unbekannte Einheit: %s %v", c, ok)
	}
	for name, code := range einheitNamen {
		if !Gueltig(Einheit, code) {
			t.Errorf("%s -> %s ist kein gültiger Code", name, code)
		}
	}
}
