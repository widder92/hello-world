// Package rechnen berechnet die Beträge einer Rechnung nach den Regeln der
// EN 16931: Positionsbeträge, Nachlässe, Umsatzsteuer je Steuersatz und die
// Gesamtsummen. Gerundet wird genau dort, wo die Norm es verlangt - sonst
// weicht die Summe um einen Cent ab und die Rechnung wird abgelehnt.
package rechnen

import (
	"fmt"
	"strings"
	"time"

	"marktrechnung/internal/dec"
	"marktrechnung/internal/modell"
)

// Datumsformat in den Daten des Programms.
const Datumsformat = "2006-01-02"

// Datum liest ein Datum im Format JJJJ-MM-TT.
func Datum(s string) (time.Time, bool) {
	t, err := time.Parse(Datumsformat, strings.TrimSpace(s))
	return t, err == nil
}

// DatumDeutsch wandelt JJJJ-MM-TT in TT.MM.JJJJ um.
func DatumDeutsch(s string) string {
	t, ok := Datum(s)
	if !ok {
		return s
	}
	return t.Format("02.01.2006")
}

// Begründungen für Umsätze ohne Steuer, je Steuerkategorie.
var befreiung = map[string][2]string{
	modell.KatReverseCharge: {"Steuerschuldnerschaft des Leistungsempfängers (Reverse Charge)", "VATEX-EU-AE"},
	modell.KatEULieferung:   {"Steuerfreie innergemeinschaftliche Lieferung", "VATEX-EU-IC"},
	modell.KatAusfuhr:       {"Steuerfreie Ausfuhrlieferung", "VATEX-EU-G"},
	modell.KatNichtSteuerb:  {"Nicht steuerbarer Umsatz", "VATEX-EU-O"},
}

// TextKleinunternehmer ist der Hinweis, den § 19 UStG auf der Rechnung verlangt.
const TextKleinunternehmer = "Kein Ausweis von Umsatzsteuer, da Kleinunternehmer gemäß § 19 UStG."

// Zeilenabschlaege liefert alle Nach- und Zuschläge einer Position. Der im
// Programm eingegebene Rabatt in Prozent wird dabei zu einem Nachlass.
func Zeilenabschlaege(p modell.Position) []modell.ZuAbschlag {
	var l []modell.ZuAbschlag
	basis := Zeilenbasis(p)
	if !p.RabattProzent.IstNull() {
		grund := p.RabattGrund
		if grund == "" {
			grund = "Rabatt"
		}
		l = append(l, modell.ZuAbschlag{
			Prozent: p.RabattProzent,
			Basis:   basis,
			Betrag:  basis.Prozent(p.RabattProzent, 2),
			Grund:   grund,
			Code:    "95",
		})
	}
	for _, z := range p.ZuAbschlaege {
		if !z.Prozent.IstNull() && z.Betrag.IstNull() {
			if z.Basis.IstNull() {
				z.Basis = basis
			}
			z.Betrag = z.Basis.Prozent(z.Prozent, 2)
		}
		l = append(l, z)
	}
	return l
}

// Zeilenbasis ist Menge mal Preis, noch ohne Nachlässe.
func Zeilenbasis(p modell.Position) dec.D {
	basis := p.Preisbasis
	if basis.IstNull() {
		basis = dec.Eins
	}
	// Menge x Preis / Preisbasis in einem Schritt, erst danach runden.
	roh := p.Menge.Mal(p.Einzelpreis, 6)
	return roh.Durch(basis, 2)
}

// Zeilennetto ist der Nettobetrag einer Position (BT-131).
func Zeilennetto(p modell.Position) dec.D {
	n := Zeilenbasis(p)
	for _, z := range Zeilenabschlaege(p) {
		if z.Zuschlag {
			n += z.Betrag
		} else {
			n -= z.Betrag
		}
	}
	return n
}

type steuergruppe struct {
	kat  string
	satz dec.D
}

// Ausrechnen liefert eine Kopie der Rechnung, in der alle abgeleiteten Werte
// eingetragen sind: Positionsnummern und -beträge, aufgelöste Rabatte,
// Fälligkeit, Zahlungsbedingungen und die Summen.
func Ausrechnen(r modell.Rechnung) modell.Rechnung {
	alt := r.Summen
	if r.Waehrung == "" {
		r.Waehrung = "EUR"
	}
	if r.Art == "" {
		r.Art = modell.ArtRechnung
	}

	var reihenfolge []steuergruppe
	basis := map[steuergruppe]dec.D{}
	merke := func(g steuergruppe, betrag dec.D) {
		if _, da := basis[g]; !da {
			reihenfolge = append(reihenfolge, g)
		}
		basis[g] += betrag
	}

	var s modell.Summen
	pos := make([]modell.Position, len(r.Positionen))
	for i, p := range r.Positionen {
		if strings.TrimSpace(p.Nr) == "" {
			p.Nr = fmt.Sprint(i + 1)
		}
		if p.Preisbasis.IstNull() {
			p.Preisbasis = dec.Eins
		}
		if r.Kleinunternehmer {
			p.SteuerKat, p.SteuerSatz = modell.KatBefreit, 0
		}
		if p.SteuerKat == "" {
			p.SteuerKat = modell.KatNormal
		}
		p.ZuAbschlaege = Zeilenabschlaege(p)
		p.Netto = Zeilenbasis(p)
		for _, z := range p.ZuAbschlaege {
			if z.Zuschlag {
				p.Netto += z.Betrag
			} else {
				p.Netto -= z.Betrag
			}
		}
		p.RabattProzent, p.RabattGrund = 0, ""
		s.Positionen += p.Netto
		merke(steuergruppe{p.SteuerKat, p.SteuerSatz}, p.Netto)
		pos[i] = p
	}
	r.Positionen = pos

	// Nachlässe und Zuschläge auf die ganze Rechnung. Ein Prozentsatz ohne
	// Betrag bezieht sich auf die Positionen mit demselben Steuersatz.
	za := make([]modell.ZuAbschlag, len(r.ZuAbschlaege))
	for i, z := range r.ZuAbschlaege {
		if r.Kleinunternehmer {
			z.SteuerKat, z.SteuerSatz = modell.KatBefreit, 0
		}
		if z.SteuerKat == "" {
			z.SteuerKat = modell.KatNormal
		}
		g := steuergruppe{z.SteuerKat, z.SteuerSatz}
		if !z.Prozent.IstNull() && z.Betrag.IstNull() {
			if z.Basis.IstNull() {
				z.Basis = basis[g]
			}
			z.Betrag = z.Basis.Prozent(z.Prozent, 2)
		}
		za[i] = z
	}
	// Erst nach der Schleife verbuchen, damit ein zweiter Prozent-Rabatt
	// dieselbe Grundlage hat wie der erste.
	for _, z := range za {
		g := steuergruppe{z.SteuerKat, z.SteuerSatz}
		if z.Zuschlag {
			s.Zuschlaege += z.Betrag
			merke(g, z.Betrag)
		} else {
			s.Abschlaege += z.Betrag
			merke(g, -z.Betrag)
		}
	}
	r.ZuAbschlaege = za

	s.Netto = s.Positionen - s.Abschlaege + s.Zuschlaege
	for _, g := range reihenfolge {
		z := modell.Steuerzeile{Kat: g.kat, Satz: g.satz, Basis: basis[g]}
		z.Betrag = z.Basis.Prozent(g.satz, 2)
		switch {
		case g.kat == modell.KatBefreit:
			z.Grund = r.Befreiungsgrund
			if z.Grund == "" && r.Kleinunternehmer {
				z.Grund = TextKleinunternehmer
			}
		case befreiung[g.kat][0] != "":
			z.Grund, z.GrundCode = befreiung[g.kat][0], befreiung[g.kat][1]
		}
		// Eine eingelesene Rechnung behält ihre eigene Begründung.
		if alt != nil {
			for _, a := range alt.Steuern {
				if a.Kat == g.kat && a.Satz == g.satz && (a.Grund != "" || a.GrundCode != "") {
					z.Grund, z.GrundCode = a.Grund, a.GrundCode
				}
			}
		}
		s.Steuer += z.Betrag
		s.Steuern = append(s.Steuern, z)
	}
	s.Brutto = s.Netto + s.Steuer
	s.Vorauszahlung = r.Zahlung.Vorauszahlung.Runde(2)
	if alt != nil {
		s.Rundung = alt.Rundung
		if s.Vorauszahlung.IstNull() {
			s.Vorauszahlung = alt.Vorauszahlung
		}
	}
	s.Zahlbetrag = s.Brutto - s.Vorauszahlung + s.Rundung
	r.Summen = &s

	if r.Faellig == "" && r.Zahlung.Art != "59" {
		if t, ok := Datum(r.Datum); ok {
			r.Faellig = t.AddDate(0, 0, r.Zahlung.Zieltage).Format(Datumsformat)
		}
	}
	if strings.TrimSpace(r.Zahlung.Bedingungen) == "" {
		r.Zahlung.Bedingungen = Zahlungstext(r)
	}
	if r.Zahlung.Art == "" {
		r.Zahlung.Art = "58"
	}
	return r
}

// Zahlungstext formuliert die Zahlungsbedingungen aus Zahlungsziel und Skonto.
func Zahlungstext(r modell.Rechnung) string {
	z := r.Zahlung
	if r.Art == modell.ArtGutschrift {
		return "Der Betrag wird gutgeschrieben bzw. mit offenen Forderungen verrechnet."
	}
	if z.Art == "59" {
		return "Der Rechnungsbetrag wird per SEPA-Lastschrift eingezogen."
	}
	ziel := "sofort"
	if z.Zieltage > 0 {
		ziel = fmt.Sprintf("innerhalb von %d Tagen", z.Zieltage)
	}
	if r.Faellig != "" {
		ziel += " bis zum " + DatumDeutsch(r.Faellig)
	}
	if z.SkontoTage > 0 && !z.SkontoProzent.IstNull() {
		return fmt.Sprintf("Zahlbar innerhalb von %d Tagen abzüglich %s %% Skonto, %s ohne Abzug.",
			z.SkontoTage, z.SkontoProzent.Deutsch(0, 2), ziel)
	}
	return "Zahlbar " + ziel + " ohne Abzug."
}

// SkontoZeile ist die maschinenlesbare Skonto-Angabe, wie die XRechnung sie
// in den Zahlungsbedingungen (BT-20) erwartet. Leer, wenn kein Skonto gilt.
func SkontoZeile(r modell.Rechnung) string {
	z := r.Zahlung
	if z.SkontoTage <= 0 || z.SkontoProzent.IstNull() || r.Art == modell.ArtGutschrift {
		return ""
	}
	return fmt.Sprintf("#SKONTO#TAGE=%d#PROZENT=%s#", z.SkontoTage, z.SkontoProzent.Fest(2))
}
