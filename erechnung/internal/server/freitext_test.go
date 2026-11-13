package server

import (
	"strings"
	"testing"

	"marktrechnung/internal/modell"
)

func TestEntwirre(t *testing.T) {
	re := modell.Rechnung{}
	re.Verkaeufer.GLN = "4012345000009"
	re.Kaeufer = modell.Partei{Name: "Muster Handel GmbH", Strasse: "Handelsstraße 20", Zusatz: "GLN: 4000001000005"}
	re.Hinweise = []modell.Hinweis{
		{Text: "Vielen Dank für Ihre Bestellung. Wir berechnen wie folgt:"},
		{Text: "Lieferschein-Nr.: LS-2026/0815 WE-GLN: 4000001000012\nBestellnummer: 4500123456\nWare bleibt bis zur Bezahlung unser Eigentum."},
		{Code: "REG", Text: "Inhaberin Maria Muster"},
	}
	bericht := entwirre(&re)
	if re.Kaeufer.GLN != "4000001000005" || re.Kaeufer.Strasse != "Handelsstraße 20" || re.Kaeufer.Zusatz != "" {
		t.Errorf("Anschrift: %+v", re.Kaeufer)
	}
	if re.Lieferscheinnummer != "LS-2026/0815" || re.Bestellnummer != "4500123456" || re.Lieferort == nil || re.Lieferort.GLN != "4000001000012" {
		t.Errorf("Felder: Lieferschein %q, Bestellung %q, Markt %+v", re.Lieferscheinnummer, re.Bestellnummer, re.Lieferort)
	}
	if len(re.Hinweise) != 3 || re.Hinweise[1].Text != "Ware bleibt bis zur Bezahlung unser Eigentum." || !strings.HasPrefix(re.Hinweise[0].Text, "Vielen Dank") {
		t.Errorf("Texte: %+v", re.Hinweise)
	}
	if len(bericht) != 4 {
		t.Errorf("Bericht: %v", bericht)
	}
	// Ein zweiter Durchlauf ändert nichts mehr.
	vorher := re
	if b := entwirre(&re); len(b) != 0 || re.Lieferscheinnummer != vorher.Lieferscheinnummer || len(re.Hinweise) != 3 {
		t.Errorf("zweiter Durchlauf: %v", b)
	}
}

func TestEntwirreSonderfaelle(t *testing.T) {
	// GLN ohne Leerzeichen hinter der Lieferscheinnummer, Vorsatz ILN, GLN im Namen.
	re := modell.Rechnung{}
	re.Kaeufer = modell.Partei{Name: "Markt Beispiel oHG GLN 4000001000012", Strasse: "ILN 4000001000012", Zusatz: "Ringstraße 1a"}
	re.Hinweise = []modell.Hinweis{{Text: "Lieferschein 4711GLN: 4000001000012"}}
	entwirre(&re)
	if re.Kaeufer.Name != "Markt Beispiel oHG" || re.Kaeufer.GLN != "4000001000012" || re.Kaeufer.Strasse != "Ringstraße 1a" {
		t.Errorf("Käufer: %+v", re.Kaeufer)
	}
	if re.Lieferscheinnummer != "4711" || len(re.Hinweise) != 0 {
		t.Errorf("Lieferschein %q, Hinweise %+v", re.Lieferscheinnummer, re.Hinweise)
	}
	// Die GLN des Käufers im Text macht den Käufer nicht zum Markt.
	if re.Lieferort != nil {
		t.Errorf("Markt irrtümlich gesetzt: %+v", re.Lieferort)
	}

	// Falsche Prüfziffer und Wörter ohne Nummer werden nicht übernommen;
	// vorhandene Felder bleiben, der Text auch.
	re = modell.Rechnung{Lieferscheinnummer: "A-1"}
	re.Kaeufer = modell.Partei{Name: "X", Strasse: "Weg 1", Zusatz: "GLN: 4000001000006"}
	re.Hinweise = []modell.Hinweis{{Text: "Lieferschein vom Dienstag, Lieferschein-Nr. B-2 liegt bei."}}
	entwirre(&re)
	if re.Kaeufer.GLN != "" || re.Lieferscheinnummer != "A-1" || len(re.Hinweise) != 1 {
		t.Errorf("Sonderfall: GLN %q, Lieferschein %q, Hinweise %+v", re.Kaeufer.GLN, re.Lieferscheinnummer, re.Hinweise)
	}
}
