package rechnen

import (
	"testing"

	"marktrechnung/internal/dec"
	"marktrechnung/internal/modell"
)

func pos(menge, preis, satz string) modell.Position {
	return modell.Position{Bezeichnung: "x", Menge: dec.Muss(menge), Einheit: "H87",
		Einzelpreis: dec.Muss(preis), SteuerKat: "S", SteuerSatz: dec.Muss(satz)}
}

func TestGemischteSteuersaetze(t *testing.T) {
	r := modell.Rechnung{Datum: "2026-11-13"}
	r.Zahlung.Zieltage = 30
	r.Positionen = []modell.Position{
		pos("12", "1.49", "7"),   // 17,88
		pos("3.5", "2.999", "7"), // 10,4965 -> 10,50
		pos("2", "19.95", "19"),  // 39,90
		pos("-1", "3.10", "19"),  // Leergut zurück: -3,10
	}
	r.Positionen[2].RabattProzent = dec.Muss("10") // 3,99 Rabatt -> 35,91
	a := Ausrechnen(r)
	s := a.Summen
	pruefe := func(name string, ist dec.D, soll string) {
		t.Helper()
		if ist.Fest(2) != soll {
			t.Errorf("%s = %s, erwartet %s", name, ist.Fest(2), soll)
		}
	}
	pruefe("Position 2", a.Positionen[1].Netto, "10.50")
	pruefe("Position 3", a.Positionen[2].Netto, "35.91")
	pruefe("Summe Positionen", s.Positionen, "61.19")
	pruefe("Netto", s.Netto, "61.19")
	if len(s.Steuern) != 2 {
		t.Fatalf("Steuerzeilen: %d", len(s.Steuern))
	}
	pruefe("Basis 7", s.Steuern[0].Basis, "28.38")
	pruefe("Steuer 7", s.Steuern[0].Betrag, "1.99") // 1,9866
	pruefe("Basis 19", s.Steuern[1].Basis, "32.81")
	pruefe("Steuer 19", s.Steuern[1].Betrag, "6.23") // 6,2339
	pruefe("Steuer", s.Steuer, "8.22")
	pruefe("Brutto", s.Brutto, "69.41")
	pruefe("Zahlbetrag", s.Zahlbetrag, "69.41")
	if a.Faellig != "2026-12-13" {
		t.Errorf("Fälligkeit %s", a.Faellig)
	}
	if a.Positionen[3].Nr != "4" {
		t.Errorf("Positionsnummer %q", a.Positionen[3].Nr)
	}
}

func TestRechnungsrabattUndSkonto(t *testing.T) {
	r := modell.Rechnung{Datum: "2026-01-31"}
	r.Zahlung = modell.Zahlung{Zieltage: 30, SkontoTage: 14, SkontoProzent: dec.Muss("2")}
	r.Positionen = []modell.Position{pos("10", "10", "7"), pos("1", "50", "19")}
	r.ZuAbschlaege = []modell.ZuAbschlag{
		{Prozent: dec.Muss("5"), Grund: "Aktionsrabatt", SteuerKat: "S", SteuerSatz: dec.Muss("7")},
		{Zuschlag: true, Betrag: dec.Muss("4.90"), Grund: "Fracht", SteuerKat: "S", SteuerSatz: dec.Muss("19")},
	}
	a := Ausrechnen(r)
	s := a.Summen
	if s.Abschlaege.Fest(2) != "5.00" || s.Zuschlaege.Fest(2) != "4.90" || s.Netto.Fest(2) != "149.90" {
		t.Errorf("Summen: -%s +%s = %s", s.Abschlaege.Fest(2), s.Zuschlaege.Fest(2), s.Netto.Fest(2))
	}
	// 7 % von 95,00 = 6,65; 19 % von 54,90 = 10,43
	if s.Steuer.Fest(2) != "17.08" || s.Brutto.Fest(2) != "166.98" {
		t.Errorf("Steuer %s, Brutto %s", s.Steuer.Fest(2), s.Brutto.Fest(2))
	}
	if a.Faellig != "2026-03-02" {
		t.Errorf("Fälligkeit %s", a.Faellig)
	}
	if SkontoZeile(a) != "#SKONTO#TAGE=14#PROZENT=2.00#" {
		t.Errorf("Skonto: %s", SkontoZeile(a))
	}
	want := "Zahlbar innerhalb von 14 Tagen abzüglich 2 % Skonto, innerhalb von 30 Tagen bis zum 02.03.2026 ohne Abzug."
	if a.Zahlung.Bedingungen != want {
		t.Errorf("Zahlungstext: %s", a.Zahlung.Bedingungen)
	}
}

func TestKleinunternehmer(t *testing.T) {
	r := modell.Rechnung{Datum: "2026-05-05", Kleinunternehmer: true}
	r.Positionen = []modell.Position{pos("4", "2.50", "19")}
	a := Ausrechnen(r)
	if a.Summen.Steuer != 0 || a.Summen.Brutto.Fest(2) != "10.00" {
		t.Errorf("Steuer %s, Brutto %s", a.Summen.Steuer, a.Summen.Brutto)
	}
	z := a.Summen.Steuern[0]
	if z.Kat != "E" || z.Grund != TextKleinunternehmer {
		t.Errorf("Steuerzeile %+v", z)
	}
}
