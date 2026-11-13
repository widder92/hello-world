package pruefung

import (
	"strings"
	"testing"

	"marktrechnung/internal/cii"
	"marktrechnung/internal/codes"
	"marktrechnung/internal/dec"
	"marktrechnung/internal/modell"
	"marktrechnung/internal/partner"
	"marktrechnung/internal/probe"
	"marktrechnung/internal/rechnen"
)

func regeln(l []modell.Meldung, schwere string) string {
	var s []string
	for _, m := range l {
		if schwere == "" || m.Schwere == schwere {
			s = append(s, m.Regel)
		}
	}
	return strings.Join(s, " ")
}

func hat(l []modell.Meldung, regel string) bool {
	for _, m := range l {
		if m.Regel == regel {
			return true
		}
	}
	return false
}

// Die Beispielrechnungen müssen in allen Formaten und mit allen
// Partnerprofilen ohne Fehler durchgehen.
func TestBeispieleOhneFehler(t *testing.T) {
	for _, r := range probe.Alle() {
		for _, f := range []string{modell.FormatZugferd, modell.FormatZugferdXR, modell.FormatXRechnungCII} {
			for _, profil := range []string{"", partner.Handel, partner.Rewe, partner.Edeka} {
				r.Format, r.Profil = f, profil
				r.Hinweise = nil
				if profil == partner.Rewe {
					// Diesen Pflichthinweis setzt das Programm bei REWE selbst.
					r.Hinweise = []modell.Hinweis{{Code: "AAK", Text: partner.ReweKonditionen}}
				}
				l, _ := Pruefe(r, Optionen{})
				if HatFehler(l) {
					t.Errorf("%s/%s/%s: Fehler %s", r.ID, f, profil, regeln(l, modell.Fehler))
				}
			}
		}
	}
}

// Hin und zurück: schreiben, lesen, als fremde Datei prüfen - es darf kein
// Fehler entstehen, und die gelesene Rechnung muss dieselbe XML ergeben.
func TestSchreibenUndLesen(t *testing.T) {
	for _, r := range probe.Alle() {
		for _, f := range []string{modell.FormatZugferd, modell.FormatXRechnungCII} {
			r.Format = f
			xml := cii.Schreibe(rechnen.Ausrechnen(r))
			gelesen, auffaellig, err := cii.Lies(xml)
			if err != nil {
				t.Fatalf("%s/%s: %v", r.ID, f, err)
			}
			if len(auffaellig) > 0 {
				t.Errorf("%s/%s: Auffälligkeiten %v", r.ID, f, auffaellig)
			}
			l, _ := Pruefe(gelesen, Optionen{Fremd: true})
			if HatFehler(l) {
				for _, m := range l {
					t.Logf("%s %s: %s", m.Schwere, m.Regel, m.Text)
				}
				t.Errorf("%s/%s: gelesene Rechnung hat Fehler", r.ID, f)
			}
			gelesen.Format = f
			nochmal := cii.Schreibe(rechnen.Ausrechnen(gelesen))
			if string(nochmal) != string(xml) {
				t.Errorf("%s/%s: nach Lesen und erneutem Schreiben weicht die XML ab", r.ID, f)
				a, b := strings.Split(string(xml), "\n"), strings.Split(string(nochmal), "\n")
				for i := 0; i < len(a) && i < len(b); i++ {
					if a[i] != b[i] {
						t.Logf("Zeile %d:\n  vorher:  %s\n  nachher: %s", i+1, a[i], b[i])
						break
					}
				}
			}
		}
	}
}

func TestFehlendeHandelsangaben(t *testing.T) {
	r := probe.Handel()
	r.Profil = partner.Rewe
	r.Kaeufer.GLN, r.Lieferort.GLN, r.Kostenstelle = "", "", "12345"
	l, _ := Pruefe(r, Optionen{})
	r.Kaeufer.UStID, r.Positionen[0].GTIN, r.Positionen[0].Artikelnummer, r.Lieferscheinnummer = "", "", "", ""
	l, _ = Pruefe(r, Optionen{})
	for _, regel := range []string{"REWE-RE-GLN", "REWE-WE-GLN", "REWE-KOSTENSTELLE", "REWE-USTID", "REWE-ARTIKEL", "REWE-LIEFERSCHEIN", "REWE-KONDITIONEN"} {
		if !hat(l, regel) {
			t.Errorf("Regel %s hätte anschlagen müssen; gemeldet: %s", regel, regeln(l, ""))
		}
	}

	r = probe.Handel()
	r.Profil = partner.Edeka
	r.Lieferort = nil
	r.Bestellnummer, r.Positionen[1].GTIN, r.Verkaeufer.GLN = "", "", ""
	l, _ = Pruefe(r, Optionen{})
	for _, regel := range []string{"EDEKA-WE-GLN", "EDEKA-BESTELLNR", "EDEKA-GTIN", "EDEKA-LIEFERANT-GLN"} {
		if !hat(l, regel) {
			t.Errorf("Regel %s hätte anschlagen müssen; gemeldet: %s", regel, regeln(l, ""))
		}
	}
}

func TestTippfehlerUndPflichtfelder(t *testing.T) {
	r := probe.Handel()
	r.Format = modell.FormatXRechnungCII
	r.Kaeufer.GLN = "4000001000006"           // Prüfziffer falsch
	r.Positionen[0].GTIN = "4012345000017"    // Prüfziffer falsch
	r.Positionen[1].Einheit = "Stück"         // kein Code
	r.Zahlung.IBAN = "DE89370400440532013001" // Prüfsumme falsch
	r.Kaeuferreferenz = ""
	r.Verkaeufer.Telefon = ""
	r.Kaeufer.EAdresse = ""
	r.Leistungsdatum = ""
	r.Verkaeufer.UStID = "123456789"
	r.Positionen[2].Einzelpreis = dec.Muss("-1")
	l, _ := Pruefe(r, Optionen{})
	for _, regel := range []string{"GLN-KAEUFER", "GTIN", "BR-CL-23", "BR-DE-19", "BR-DE-15", "BR-DE-6",
		"PEPPOL-EN16931-R010", "UStG-14-LIEFERDATUM", "BR-CO-09", "BR-27"} {
		if !hat(l, regel) {
			t.Errorf("Regel %s hätte anschlagen müssen; gemeldet: %s", regel, regeln(l, ""))
		}
	}
	for _, m := range l {
		if m.Text == "" || m.Feld == "" || strings.Contains(m.Text, "%!") {
			t.Errorf("Meldung unvollständig: %+v", m)
		}
	}
}

func TestRechenfehlerInFremderDatei(t *testing.T) {
	r := rechnen.Ausrechnen(probe.Handel())
	xml := string(cii.Schreibe(r))
	// Ein Cent zu viel Steuer, wie es bei zeilenweiser Berechnung vorkommt.
	falsch := strings.Replace(xml, "<ram:CalculatedAmount>17.60</ram:CalculatedAmount>", "<ram:CalculatedAmount>17.61</ram:CalculatedAmount>", 1)
	if falsch == xml {
		t.Fatal("Testvorlage passt nicht mehr zur Beispielrechnung")
	}
	gelesen, _, err := cii.Lies([]byte(falsch))
	if err != nil {
		t.Fatal(err)
	}
	l, _ := Pruefe(gelesen, Optionen{Fremd: true})
	if !hat(l, "BR-CO-17") || !hat(l, "BR-CO-14") {
		t.Errorf("Rechenfehler nicht erkannt: %s", regeln(l, ""))
	}
}

func TestVorlagenDerPartner(t *testing.T) {
	for _, p := range partner.Profile() {
		if len(p.Hinweise) == 0 || p.Name == "" {
			t.Errorf("Profil %s unvollständig", p.ID)
		}
		for _, v := range p.Vorlagen {
			if g := v.Kunde.GLN; g != "" && !codes.GLNGueltig(g) {
				t.Errorf("Vorlage %s: GLN %s hat eine falsche Prüfziffer", v.Titel, g)
			}
		}
	}
	// Die im Hinweistext genannten Beispiel-GLN müssen ebenfalls stimmen.
	for _, g := range []string{"4388093000005", "4388091000007", "4388290000006"} {
		if !codes.GLNGueltig(g) {
			t.Errorf("GLN %s hat eine falsche Prüfziffer", g)
		}
	}
}
