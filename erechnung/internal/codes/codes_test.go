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
