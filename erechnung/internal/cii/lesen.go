package cii

import (
	"errors"
	"regexp"
	"strconv"
	"strings"

	"marktrechnung/internal/dec"
	"marktrechnung/internal/modell"
	"marktrechnung/internal/xmlbaum"
)

// ErrKeinCII bedeutet: Die XML-Datei ist keine Rechnung in der Syntax CII.
var ErrKeinCII = errors.New("die Datei ist keine E-Rechnung in der Syntax CII")

func zahl(s string) dec.D {
	d, _ := dec.Parse(s)
	return d
}

// datumISO wandelt JJJJMMTT (Format 102) in JJJJ-MM-TT um.
func datumISO(s string) string {
	s = strings.TrimSpace(s)
	if len(s) == 8 && strings.Trim(s, "0123456789") == "" {
		return s[:4] + "-" + s[4:6] + "-" + s[6:]
	}
	return s
}

// Lies liest eine CII-Rechnung (ZUGFeRD, Factur-X, XRechnung) in das
// Datenmodell. Beträge und Summen werden so übernommen, wie sie in der Datei
// stehen, damit die Prüfung Rechenfehler findet.
func Lies(daten []byte) (modell.Rechnung, []string, error) {
	var r modell.Rechnung
	w, err := xmlbaum.Lies(daten)
	if err != nil {
		return r, nil, errors.New("die Datei ist kein gültiges XML: " + err.Error())
	}
	return LiesBaum(w)
}

// LiesBaum liest eine bereits eingelesene XML. Neben der Rechnung liefert es
// Auffälligkeiten im Aufbau der Datei.
func LiesBaum(w *xmlbaum.Knoten) (modell.Rechnung, []string, error) {
	var r modell.Rechnung
	var auffaellig []string
	if w.Name != "CrossIndustryInvoice" {
		if w.Name == "CrossIndustryDocument" {
			return r, nil, errors.New("die Datei ist eine ZUGFeRD-1.0-Rechnung. Dieses veraltete Format nimmt der Handel nicht mehr an")
		}
		return r, nil, ErrKeinCII
	}
	for _, p := range w.LeereElemente() {
		if !strings.HasSuffix(p, "/ApplicableHeaderTradeDelivery") {
			auffaellig = append(auffaellig, "Leeres Element "+p)
		}
	}
	kontext := w.Finde("ExchangedDocumentContext")
	r.Spezifikation = kontext.Wert("GuidelineSpecifiedDocumentContextParameter", "ID")
	r.Prozess = kontext.Wert("BusinessProcessSpecifiedDocumentContextParameter", "ID")

	dok := w.Finde("ExchangedDocument")
	r.Nummer = dok.Wert("ID")
	r.Art = dok.Wert("TypeCode")
	r.Datum = datumISO(dok.Wert("IssueDateTime", "DateTimeString"))
	for _, n := range dok.Alle("IncludedNote") {
		r.Hinweise = append(r.Hinweise, modell.Hinweis{Code: n.Wert("SubjectCode"), Text: n.Wert("Content")})
	}

	tx := w.Finde("SupplyChainTradeTransaction")
	ver := tx.Finde("ApplicableHeaderTradeAgreement")
	lief := tx.Finde("ApplicableHeaderTradeDelivery")
	abr := tx.Finde("ApplicableHeaderTradeSettlement")
	if tx == nil || ver == nil || abr == nil {
		return r, auffaellig, errors.New("die Rechnung ist unvollständig: Die Abschnitte zu Vereinbarung und Abrechnung fehlen")
	}

	r.Kaeuferreferenz = ver.Wert("BuyerReference")
	r.Verkaeufer = liesPartei(ver.Finde("SellerTradeParty"))
	r.Kaeufer = liesPartei(ver.Finde("BuyerTradeParty"))
	r.Auftragsnummer = ver.Wert("SellerOrderReferencedDocument", "IssuerAssignedID")
	r.Bestellnummer = ver.Wert("BuyerOrderReferencedDocument", "IssuerAssignedID")
	r.Vertragsnummer = ver.Wert("ContractReferencedDocument", "IssuerAssignedID")
	r.Projekt = ver.Wert("SpecifiedProcuringProject", "ID")
	for _, d := range ver.Alle("AdditionalReferencedDocument") {
		if d.Wert("TypeCode") == "130" {
			r.Objekt = d.Wert("IssuerAssignedID")
		}
	}

	if ziel := lief.Finde("ShipToTradeParty"); ziel != nil {
		l := &modell.Lieferort{Name: ziel.Wert("Name")}
		for _, g := range ziel.Alle("GlobalID") {
			if g.Attribut("schemeID") == "0088" {
				l.GLN = g.Text
			} else if l.Kennung == "" {
				l.Kennung = g.Text
			}
		}
		if id := ziel.Wert("ID"); id != "" {
			l.Kennung = id
		}
		if a := ziel.Finde("PostalTradeAddress"); a != nil {
			l.Strasse, l.Zusatz = a.Wert("LineOne"), zusatz(a)
			l.PLZ, l.Ort, l.Land = a.Wert("PostcodeCode"), a.Wert("CityName"), a.Wert("CountryID")
		}
		r.Lieferort = l
	}
	r.Leistungsdatum = datumISO(lief.Wert("ActualDeliverySupplyChainEvent", "OccurrenceDateTime", "DateTimeString"))
	r.Lieferscheinnummer = lief.Wert("DespatchAdviceReferencedDocument", "IssuerAssignedID")
	r.Wareneingang = lief.Wert("ReceivingAdviceReferencedDocument", "IssuerAssignedID")

	r.Waehrung = abr.Wert("InvoiceCurrencyCode")
	z := &r.Zahlung
	z.Verwendungszweck = abr.Wert("PaymentReference")
	z.GlaeubigerID = abr.Wert("CreditorReferenceID")
	if zm := abr.Finde("SpecifiedTradeSettlementPaymentMeans"); zm != nil {
		z.Art = zm.Wert("TypeCode")
		z.IBAN = zm.Wert("PayeePartyCreditorFinancialAccount", "IBANID")
		if z.IBAN == "" {
			z.IBAN = zm.Wert("PayeePartyCreditorFinancialAccount", "ProprietaryID")
		}
		z.Kontoinhaber = zm.Wert("PayeePartyCreditorFinancialAccount", "AccountName")
		z.BIC = zm.Wert("PayeeSpecifiedCreditorFinancialInstitution", "BICID")
		z.LastschriftIBAN = zm.Wert("PayerPartyDebtorFinancialAccount", "IBANID")
		if len(abr.Alle("SpecifiedTradeSettlementPaymentMeans")) > 1 {
			auffaellig = append(auffaellig, "Die Datei nennt mehrere Zahlungswege; ausgewertet wird der erste.")
		}
	} else {
		auffaellig = append(auffaellig, "Die Datei enthält keine Zahlungsangaben (BG-16).")
	}
	if zb := abr.Finde("SpecifiedTradePaymentTerms"); zb != nil {
		z.Bedingungen = liesSkonto(zb.Wert("Description"), z)
		r.Faellig = datumISO(zb.Wert("DueDateDateTime", "DateTimeString"))
		z.Mandat = zb.Wert("DirectDebitMandateID")
	}
	if zr := abr.Finde("BillingSpecifiedPeriod"); zr != nil {
		r.ZeitraumVon = datumISO(zr.Wert("StartDateTime", "DateTimeString"))
		r.ZeitraumBis = datumISO(zr.Wert("EndDateTime", "DateTimeString"))
	}
	// Der Schreiber setzt bei reinen Zeitraumrechnungen das Ende des Zeitraums
	// als Liefertag ein; beim Lesen wird das wieder zurückgenommen.
	if r.Leistungsdatum != "" && r.Leistungsdatum == r.ZeitraumBis && r.Lieferort == nil && r.Lieferscheinnummer == "" && r.Wareneingang == "" {
		r.Leistungsdatum = ""
	}
	r.Kostenstelle = abr.Wert("ReceivableSpecifiedTradeAccountingAccount", "ID")
	for _, v := range abr.Alle("InvoiceReferencedDocument") {
		r.Vorgaenger = append(r.Vorgaenger, modell.Vorgaenger{Nummer: v.Wert("IssuerAssignedID"),
			Datum: datumISO(v.Wert("FormattedIssueDateTime", "DateTimeString"))})
	}
	for _, a := range abr.Alle("SpecifiedTradeAllowanceCharge") {
		za := liesZuAbschlag(a)
		za.SteuerKat = a.Wert("CategoryTradeTax", "CategoryCode")
		za.SteuerSatz = zahl(a.Wert("CategoryTradeTax", "RateApplicablePercent"))
		r.ZuAbschlaege = append(r.ZuAbschlaege, za)
	}

	s := &modell.Summen{}
	if sum := abr.Finde("SpecifiedTradeSettlementHeaderMonetarySummation"); sum != nil {
		s.Positionen = zahl(sum.Wert("LineTotalAmount"))
		s.Zuschlaege = zahl(sum.Wert("ChargeTotalAmount"))
		s.Abschlaege = zahl(sum.Wert("AllowanceTotalAmount"))
		s.Netto = zahl(sum.Wert("TaxBasisTotalAmount"))
		s.Steuer = zahl(sum.Wert("TaxTotalAmount"))
		s.Rundung = zahl(sum.Wert("RoundingAmount"))
		s.Brutto = zahl(sum.Wert("GrandTotalAmount"))
		s.Vorauszahlung = zahl(sum.Wert("TotalPrepaidAmount"))
		s.Zahlbetrag = zahl(sum.Wert("DuePayableAmount"))
		z.Vorauszahlung = s.Vorauszahlung
	}
	for _, t := range abr.Alle("ApplicableTradeTax") {
		s.Steuern = append(s.Steuern, modell.Steuerzeile{
			Kat: t.Wert("CategoryCode"), Satz: zahl(t.Wert("RateApplicablePercent")),
			Basis: zahl(t.Wert("BasisAmount")), Betrag: zahl(t.Wert("CalculatedAmount")),
			Grund: t.Wert("ExemptionReason"), GrundCode: t.Wert("ExemptionReasonCode"),
		})
		if t.Wert("CategoryCode") == modell.KatBefreit && r.Befreiungsgrund == "" {
			r.Befreiungsgrund = t.Wert("ExemptionReason")
		}
	}
	r.Summen = s

	for _, li := range tx.Alle("IncludedSupplyChainTradeLineItem") {
		var p modell.Position
		p.Nr = li.Wert("AssociatedDocumentLineDocument", "LineID")
		p.Hinweis = li.Wert("AssociatedDocumentLineDocument", "IncludedNote", "Content")
		prod := li.Finde("SpecifiedTradeProduct")
		p.GTIN = prod.Wert("GlobalID")
		p.Artikelnummer = prod.Wert("SellerAssignedID")
		p.KundenArtNr = prod.Wert("BuyerAssignedID")
		p.Bezeichnung = prod.Wert("Name")
		p.Beschreibung = prod.Wert("Description")
		p.Herkunft = prod.Wert("OriginTradeCountry", "ID")
		v := li.Finde("SpecifiedLineTradeAgreement")
		p.BestellPos = v.Wert("BuyerOrderReferencedDocument", "LineID")
		p.Einzelpreis = zahl(v.Wert("NetPriceProductTradePrice", "ChargeAmount"))
		if b := v.Wert("NetPriceProductTradePrice", "BasisQuantity"); b != "" {
			p.Preisbasis = zahl(b)
		}
		p.Listenpreis = zahl(v.Wert("GrossPriceProductTradePrice", "ChargeAmount"))
		p.Preisrabatt = zahl(v.Wert("GrossPriceProductTradePrice", "AppliedTradeAllowanceCharge", "ActualAmount"))
		menge := li.Finde("SpecifiedLineTradeDelivery", "BilledQuantity")
		p.Menge = zahl(menge.Wert())
		p.Einheit = menge.Attribut("unitCode")
		a := li.Finde("SpecifiedLineTradeSettlement")
		p.SteuerKat = a.Wert("ApplicableTradeTax", "CategoryCode")
		p.SteuerSatz = zahl(a.Wert("ApplicableTradeTax", "RateApplicablePercent"))
		if zr := a.Finde("BillingSpecifiedPeriod"); zr != nil {
			p.ZeitraumVon = datumISO(zr.Wert("StartDateTime", "DateTimeString"))
			p.ZeitraumBis = datumISO(zr.Wert("EndDateTime", "DateTimeString"))
		}
		for _, n := range a.Alle("SpecifiedTradeAllowanceCharge") {
			p.ZuAbschlaege = append(p.ZuAbschlaege, liesZuAbschlag(n))
		}
		p.Netto = zahl(a.Wert("SpecifiedTradeSettlementLineMonetarySummation", "LineTotalAmount"))
		r.Positionen = append(r.Positionen, p)
	}

	// Kleinunternehmer erkennt man am Befreiungsgrund.
	if g := strings.ToLower(r.Befreiungsgrund); strings.Contains(g, "§ 19") || strings.Contains(g, "§19") || strings.Contains(g, "kleinunternehmer") {
		r.Kleinunternehmer = true
	}
	return r, auffaellig, nil
}

var skontoMuster = regexp.MustCompile(`^#SKONTO#TAGE=(\d+)#PROZENT=(\d+\.\d+)(#BASISBETRAG=-?[0-9.]+)?#$`)

// liesSkonto holt die maschinenlesbaren Skonto-Zeilen der XRechnung aus dem
// Text der Zahlungsbedingungen und liefert den übrigen Text zurück.
func liesSkonto(text string, z *modell.Zahlung) string {
	var rest []string
	for _, zeile := range strings.Split(text, "\n") {
		zeile = strings.TrimSpace(zeile)
		if m := skontoMuster.FindStringSubmatch(zeile); m != nil {
			if z.SkontoTage == 0 {
				z.SkontoTage, _ = strconv.Atoi(m[1])
				z.SkontoProzent = zahl(m[2])
			}
			continue
		}
		if zeile != "" {
			rest = append(rest, zeile)
		}
	}
	return strings.Join(rest, "\n")
}

// zusatz fasst die zweite und dritte Anschriftzeile zusammen, durch einen
// Zeilenumbruch getrennt.
func zusatz(a *xmlbaum.Knoten) string {
	return strings.TrimSpace(a.Wert("LineTwo") + "\n" + a.Wert("LineThree"))
}

func liesZuAbschlag(a *xmlbaum.Knoten) modell.ZuAbschlag {
	return modell.ZuAbschlag{
		Zuschlag: strings.EqualFold(a.Wert("ChargeIndicator", "Indicator"), "true"),
		Prozent:  zahl(a.Wert("CalculationPercent")),
		Basis:    zahl(a.Wert("BasisAmount")),
		Betrag:   zahl(a.Wert("ActualAmount")),
		Code:     a.Wert("ReasonCode"),
		Grund:    a.Wert("Reason"),
	}
}

func liesPartei(n *xmlbaum.Knoten) modell.Partei {
	var p modell.Partei
	if n == nil {
		return p
	}
	p.Kennung = n.Wert("ID")
	for _, g := range n.Alle("GlobalID") {
		if g.Attribut("schemeID") == "0088" {
			p.GLN = g.Text
		} else if p.Kennung == "" {
			p.Kennung = g.Text
		}
	}
	p.Name = n.Wert("Name")
	p.Rechtliches = n.Wert("Description")
	p.Register = n.Wert("SpecifiedLegalOrganization", "ID")
	p.Handelsname = n.Wert("SpecifiedLegalOrganization", "TradingBusinessName")
	if k := n.Finde("DefinedTradeContact"); k != nil {
		p.Ansprechpartner = k.Wert("PersonName")
		if p.Ansprechpartner == "" {
			p.Ansprechpartner = k.Wert("DepartmentName")
		}
		p.Telefon = k.Wert("TelephoneUniversalCommunication", "CompleteNumber")
		p.Email = k.Wert("EmailURIUniversalCommunication", "URIID")
	}
	if a := n.Finde("PostalTradeAddress"); a != nil {
		p.Strasse, p.Zusatz = a.Wert("LineOne"), zusatz(a)
		p.PLZ, p.Ort, p.Land = a.Wert("PostcodeCode"), a.Wert("CityName"), a.Wert("CountryID")
	}
	if e := n.Finde("URIUniversalCommunication", "URIID"); e != nil {
		p.EAdresse, p.EAdresseSchema = e.Text, e.Attribut("schemeID")
	}
	for _, t := range n.Alle("SpecifiedTaxRegistration") {
		id := t.Finde("ID")
		switch id.Attribut("schemeID") {
		case "VA":
			p.UStID = id.Wert()
		case "FC":
			p.Steuernummer = id.Wert()
		}
	}
	return p
}
