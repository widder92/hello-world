// Package cii schreibt und liest E-Rechnungen in der Syntax "UN/CEFACT Cross
// Industry Invoice" (CII). Das ist die XML, die in einem ZUGFeRD-PDF steckt
// und die REWE und EDEKA auch als XRechnung erwarten.
package cii

import (
	"strings"

	"marktrechnung/internal/dec"
	"marktrechnung/internal/modell"
	"marktrechnung/internal/rechnen"
	"marktrechnung/internal/xmlbaum"
)

// Kennungen des Standards (BT-24).
const (
	LeitlinieEN16931   = "urn:cen.eu:en16931:2017"
	LeitlinieXRechnung = "urn:cen.eu:en16931:2017#compliant#urn:xeinkauf.de:kosit:xrechnung_3.0"
	// Geschäftsprozess (BT-23), den die XRechnung verlangt.
	ProzessPeppol = "urn:fdc:peppol.eu:2017:poacc:billing:01:1.0"
)

// IstXRechnung meldet, ob ein Ausgabeformat den XRechnung-Regeln folgt.
func IstXRechnung(format string) bool {
	return format == modell.FormatZugferdXR || format == modell.FormatXRechnungCII || format == modell.FormatXRechnungUBL
}

func datum102(s string) string { return strings.ReplaceAll(strings.TrimSpace(s), "-", "") }

func betrag(d dec.D) string { return d.Fest(2) }

// Schreibe erzeugt die XML einer Rechnung. Die Rechnung muss vorher mit
// rechnen.Ausrechnen durchgerechnet worden sein.
func Schreibe(r modell.Rechnung) []byte {
	xr := IstXRechnung(r.Format)
	w := xmlbaum.Neu("rsm:CrossIndustryInvoice")
	w.A("xmlns:rsm", "urn:un:unece:uncefact:data:standard:CrossIndustryInvoice:100")
	w.A("xmlns:qdt", "urn:un:unece:uncefact:data:standard:QualifiedDataType:100")
	w.A("xmlns:ram", "urn:un:unece:uncefact:data:standard:ReusableAggregateBusinessInformationEntity:100")
	w.A("xmlns:udt", "urn:un:unece:uncefact:data:standard:UnqualifiedDataType:100")

	// Kopf: welcher Standard, welcher Prozess.
	kontext := w.Kind("rsm:ExchangedDocumentContext")
	prozess := r.Prozess
	if prozess == "" && xr {
		prozess = ProzessPeppol
	}
	kontext.Kind("ram:BusinessProcessSpecifiedDocumentContextParameter").T("ram:ID", prozess)
	leitlinie := LeitlinieEN16931
	if xr {
		leitlinie = LeitlinieXRechnung
	}
	kontext.Kind("ram:GuidelineSpecifiedDocumentContextParameter").T("ram:ID", leitlinie)

	dok := w.Kind("rsm:ExchangedDocument")
	dok.T("ram:ID", r.Nummer)
	dok.T("ram:TypeCode", r.Art)
	zeit(dok.Kind("ram:IssueDateTime"), "udt", r.Datum)
	for _, h := range hinweise(r) {
		n := dok.Kind("ram:IncludedNote")
		n.T("ram:Content", h.Text)
		n.T("ram:SubjectCode", h.Code)
	}

	tx := w.Kind("rsm:SupplyChainTradeTransaction")
	for _, p := range r.Positionen {
		position(tx, p, r)
	}

	// Vereinbarung: wer verkauft an wen, auf welche Bestellung.
	ver := tx.Kind("ram:ApplicableHeaderTradeAgreement")
	ver.T("ram:BuyerReference", r.Kaeuferreferenz)
	partei(ver.Kind("ram:SellerTradeParty"), r.Verkaeufer, true)
	partei(ver.Kind("ram:BuyerTradeParty"), r.Kaeufer, false)
	ver.Kind("ram:SellerOrderReferencedDocument").T("ram:IssuerAssignedID", r.Auftragsnummer)
	ver.Kind("ram:BuyerOrderReferencedDocument").T("ram:IssuerAssignedID", r.Bestellnummer)
	ver.Kind("ram:ContractReferencedDocument").T("ram:IssuerAssignedID", r.Vertragsnummer)
	if r.Objekt != "" {
		o := ver.Kind("ram:AdditionalReferencedDocument")
		o.T("ram:IssuerAssignedID", r.Objekt)
		o.T("ram:TypeCode", "130")
	}
	if r.Projekt != "" {
		pr := ver.Kind("ram:SpecifiedProcuringProject")
		pr.T("ram:ID", r.Projekt)
		pr.T("ram:Name", "Projekt")
	}

	// Lieferung: wohin, wann, mit welchem Lieferschein.
	lief := tx.Kind("ram:ApplicableHeaderTradeDelivery")
	if !r.Lieferort.Leer() {
		l := r.Lieferort
		ziel := lief.Kind("ram:ShipToTradeParty")
		// Entweder GLN oder freie Kennung - beides zugleich lehnen die
		// Prüfregeln ab.
		if l.GLN != "" {
			ziel.T("ram:GlobalID", l.GLN).A("schemeID", "0088")
		} else {
			ziel.T("ram:ID", l.Kennung)
		}
		ziel.T("ram:Name", l.Name)
		if l.HatAdresse() {
			a := ziel.Kind("ram:PostalTradeAddress")
			a.T("ram:PostcodeCode", l.PLZ)
			a.T("ram:LineOne", l.Strasse)
			a.T("ram:LineTwo", l.Zusatz)
			a.T("ram:CityName", l.Ort)
			a.T("ram:CountryID", oder(l.Land, "DE"))
		}
	}
	if r.Leistungsdatum != "" {
		zeit(lief.Kind("ram:ActualDeliverySupplyChainEvent").Kind("ram:OccurrenceDateTime"), "udt", r.Leistungsdatum)
	}
	lief.Kind("ram:DespatchAdviceReferencedDocument").T("ram:IssuerAssignedID", r.Lieferscheinnummer)
	lief.Kind("ram:ReceivingAdviceReferencedDocument").T("ram:IssuerAssignedID", r.Wareneingang)

	// Abrechnung: Zahlung, Steuer, Summen.
	z := r.Zahlung
	abr := tx.Kind("ram:ApplicableHeaderTradeSettlement")
	if z.Art == "59" {
		abr.T("ram:CreditorReferenceID", z.GlaeubigerID)
	}
	abr.T("ram:PaymentReference", z.Verwendungszweck)
	abr.T("ram:InvoiceCurrencyCode", r.Waehrung)
	zm := abr.Kind("ram:SpecifiedTradeSettlementPaymentMeans")
	zm.T("ram:TypeCode", z.Art)
	switch z.Art {
	case "59":
		zm.Kind("ram:PayerPartyDebtorFinancialAccount").T("ram:IBANID", iban(z.LastschriftIBAN))
	case "30", "58", "42", "31":
		konto := zm.Kind("ram:PayeePartyCreditorFinancialAccount")
		konto.T("ram:IBANID", iban(z.IBAN))
		konto.T("ram:AccountName", z.Kontoinhaber)
		if z.IBAN != "" {
			zm.Kind("ram:PayeeSpecifiedCreditorFinancialInstitution").T("ram:BICID", strings.ReplaceAll(z.BIC, " ", ""))
		}
	}

	s := r.Summen
	if s == nil {
		s = &modell.Summen{}
	}
	for _, st := range s.Steuern {
		t := abr.Kind("ram:ApplicableTradeTax")
		t.T("ram:CalculatedAmount", betrag(st.Betrag))
		t.T("ram:TypeCode", "VAT")
		t.T("ram:ExemptionReason", st.Grund)
		t.T("ram:BasisAmount", betrag(st.Basis))
		t.T("ram:CategoryCode", st.Kat)
		t.T("ram:ExemptionReasonCode", st.GrundCode)
		if st.Kat != modell.KatNichtSteuerb {
			t.T("ram:RateApplicablePercent", st.Satz.Text(0, 2))
		}
	}
	if r.ZeitraumVon != "" || r.ZeitraumBis != "" {
		zr := abr.Kind("ram:BillingSpecifiedPeriod")
		if r.ZeitraumVon != "" {
			zeit(zr.Kind("ram:StartDateTime"), "udt", r.ZeitraumVon)
		}
		if r.ZeitraumBis != "" {
			zeit(zr.Kind("ram:EndDateTime"), "udt", r.ZeitraumBis)
		}
	}
	for _, a := range r.ZuAbschlaege {
		n := abr.Kind("ram:SpecifiedTradeAllowanceCharge")
		n.Kind("ram:ChargeIndicator").T("udt:Indicator", wahr(a.Zuschlag))
		if !a.Prozent.IstNull() {
			n.T("ram:CalculationPercent", a.Prozent.Text(0, 4))
			n.T("ram:BasisAmount", betrag(a.Basis))
		}
		n.T("ram:ActualAmount", betrag(a.Betrag))
		n.T("ram:ReasonCode", a.Code)
		n.T("ram:Reason", a.Grund)
		t := n.Kind("ram:CategoryTradeTax")
		t.T("ram:TypeCode", "VAT")
		t.T("ram:CategoryCode", a.SteuerKat)
		if a.SteuerKat != modell.KatNichtSteuerb {
			t.T("ram:RateApplicablePercent", a.SteuerSatz.Text(0, 2))
		}
	}
	zb := abr.Kind("ram:SpecifiedTradePaymentTerms")
	zb.Roh("ram:Description", Zahlungsbedingungen(r, xr))
	if r.Faellig != "" {
		zeit(zb.Kind("ram:DueDateDateTime"), "udt", r.Faellig)
	}
	if z.Art == "59" {
		zb.T("ram:DirectDebitMandateID", z.Mandat)
	}

	sum := abr.Kind("ram:SpecifiedTradeSettlementHeaderMonetarySummation")
	sum.T("ram:LineTotalAmount", betrag(s.Positionen))
	if !s.Zuschlaege.IstNull() {
		sum.T("ram:ChargeTotalAmount", betrag(s.Zuschlaege))
	}
	if !s.Abschlaege.IstNull() {
		sum.T("ram:AllowanceTotalAmount", betrag(s.Abschlaege))
	}
	sum.T("ram:TaxBasisTotalAmount", betrag(s.Netto))
	sum.T("ram:TaxTotalAmount", betrag(s.Steuer)).A("currencyID", r.Waehrung)
	if !s.Rundung.IstNull() {
		sum.T("ram:RoundingAmount", betrag(s.Rundung))
	}
	sum.T("ram:GrandTotalAmount", betrag(s.Brutto))
	if !s.Vorauszahlung.IstNull() {
		sum.T("ram:TotalPrepaidAmount", betrag(s.Vorauszahlung))
	}
	sum.T("ram:DuePayableAmount", betrag(s.Zahlbetrag))

	for _, v := range r.Vorgaenger {
		if v.Nummer == "" {
			continue
		}
		n := abr.Kind("ram:InvoiceReferencedDocument")
		n.T("ram:IssuerAssignedID", v.Nummer)
		if v.Datum != "" {
			zeit(n.Kind("ram:FormattedIssueDateTime"), "qdt", v.Datum)
		}
	}
	abr.Kind("ram:ReceivableSpecifiedTradeAccountingAccount").T("ram:ID", r.Kostenstelle)

	// ApplicableHeaderTradeDelivery ist Pflicht, auch wenn es leer ist. Der
	// Baum lässt leere Elemente sonst weg, deshalb hier ein Sonderweg.
	return erzwingeLieferung(w.Bytes())
}

// Zahlungsbedingungen liefert den Text für BT-20. Für die XRechnung kommt die
// maschinenlesbare Skonto-Zeile dazu; sie muss mit einem Zeilenende aufhören.
func Zahlungsbedingungen(r modell.Rechnung, xr bool) string {
	text := strings.TrimSpace(r.Zahlung.Bedingungen)
	if !xr {
		return text
	}
	// Freitext darf in der XRechnung nicht mit "#" beginnen.
	var zeilen []string
	for _, z := range strings.Split(text, "\n") {
		z = strings.TrimSpace(z)
		if z != "" && !strings.HasPrefix(z, "#") {
			zeilen = append(zeilen, z)
		}
	}
	text = strings.Join(zeilen, "\n")
	if sk := rechnen.SkontoZeile(r); sk != "" {
		if text != "" {
			text += "\n"
		}
		text += sk + "\n"
	}
	return text
}

func hinweise(r modell.Rechnung) []modell.Hinweis {
	var l []modell.Hinweis
	if t := strings.TrimSpace(r.Einleitung); t != "" {
		l = append(l, modell.Hinweis{Text: t})
	}
	for _, h := range r.Hinweise {
		if strings.TrimSpace(h.Text) != "" {
			l = append(l, h)
		}
	}
	if t := strings.TrimSpace(r.Schluss); t != "" {
		l = append(l, modell.Hinweis{Text: t})
	}
	return l
}

func position(tx *xmlbaum.Knoten, p modell.Position, r modell.Rechnung) {
	n := tx.Kind("ram:IncludedSupplyChainTradeLineItem")
	kopf := n.Kind("ram:AssociatedDocumentLineDocument")
	kopf.T("ram:LineID", p.Nr)
	kopf.Kind("ram:IncludedNote").T("ram:Content", p.Hinweis)

	prod := n.Kind("ram:SpecifiedTradeProduct")
	if p.GTIN != "" {
		prod.T("ram:GlobalID", p.GTIN).A("schemeID", "0160")
	}
	prod.T("ram:SellerAssignedID", p.Artikelnummer)
	prod.T("ram:BuyerAssignedID", p.KundenArtNr)
	prod.T("ram:Name", p.Bezeichnung)
	prod.T("ram:Description", p.Beschreibung)
	prod.Kind("ram:OriginTradeCountry").T("ram:ID", p.Herkunft)

	ver := n.Kind("ram:SpecifiedLineTradeAgreement")
	ver.Kind("ram:BuyerOrderReferencedDocument").T("ram:LineID", p.BestellPos)
	basis := ""
	if p.Preisbasis != dec.Eins && !p.Preisbasis.IstNull() {
		basis = p.Preisbasis.Text(0, 4)
	}
	if !p.Listenpreis.IstNull() {
		lp := ver.Kind("ram:GrossPriceProductTradePrice")
		lp.T("ram:ChargeAmount", p.Listenpreis.Text(2, 4))
		if basis != "" {
			lp.T("ram:BasisQuantity", basis).A("unitCode", p.Einheit)
		}
		if !p.Preisrabatt.IstNull() {
			ra := lp.Kind("ram:AppliedTradeAllowanceCharge")
			ra.Kind("ram:ChargeIndicator").T("udt:Indicator", "false")
			ra.T("ram:ActualAmount", p.Preisrabatt.Text(2, 4))
		}
	}
	np := ver.Kind("ram:NetPriceProductTradePrice")
	np.T("ram:ChargeAmount", p.Einzelpreis.Text(2, 4))
	if basis != "" {
		np.T("ram:BasisQuantity", basis).A("unitCode", p.Einheit)
	}

	n.Kind("ram:SpecifiedLineTradeDelivery").T("ram:BilledQuantity", p.Menge.Text(0, 4)).A("unitCode", p.Einheit)

	abr := n.Kind("ram:SpecifiedLineTradeSettlement")
	t := abr.Kind("ram:ApplicableTradeTax")
	t.T("ram:TypeCode", "VAT")
	t.T("ram:CategoryCode", p.SteuerKat)
	if p.SteuerKat != modell.KatNichtSteuerb {
		t.T("ram:RateApplicablePercent", p.SteuerSatz.Text(0, 2))
	}
	if p.ZeitraumVon != "" || p.ZeitraumBis != "" {
		zr := abr.Kind("ram:BillingSpecifiedPeriod")
		if p.ZeitraumVon != "" {
			zeit(zr.Kind("ram:StartDateTime"), "udt", p.ZeitraumVon)
		}
		if p.ZeitraumBis != "" {
			zeit(zr.Kind("ram:EndDateTime"), "udt", p.ZeitraumBis)
		}
	}
	for _, a := range p.ZuAbschlaege {
		za := abr.Kind("ram:SpecifiedTradeAllowanceCharge")
		za.Kind("ram:ChargeIndicator").T("udt:Indicator", wahr(a.Zuschlag))
		if !a.Prozent.IstNull() {
			za.T("ram:CalculationPercent", a.Prozent.Text(0, 4))
			za.T("ram:BasisAmount", betrag(a.Basis))
		}
		za.T("ram:ActualAmount", betrag(a.Betrag))
		za.T("ram:ReasonCode", a.Code)
		za.T("ram:Reason", a.Grund)
	}
	abr.Kind("ram:SpecifiedTradeSettlementLineMonetarySummation").T("ram:LineTotalAmount", betrag(p.Netto))
}

func partei(n *xmlbaum.Knoten, p modell.Partei, verkaeufer bool) {
	if verkaeufer {
		// BT-29 darf mehrfach vorkommen: Lieferantennummer und GLN.
		n.T("ram:ID", p.Kennung)
		if p.GLN != "" {
			n.T("ram:GlobalID", p.GLN).A("schemeID", "0088")
		}
	} else if p.GLN != "" {
		// BT-46 nur einmal: die GLN hat Vorrang vor der Kundennummer.
		n.T("ram:GlobalID", p.GLN).A("schemeID", "0088")
	} else {
		n.T("ram:ID", p.Kennung)
	}
	n.T("ram:Name", p.Name)
	if verkaeufer {
		n.T("ram:Description", p.Rechtliches)
	}
	org := n.Kind("ram:SpecifiedLegalOrganization")
	org.T("ram:ID", p.Register)
	org.T("ram:TradingBusinessName", p.Handelsname)
	if p.Ansprechpartner != "" || p.Telefon != "" || p.Email != "" {
		k := n.Kind("ram:DefinedTradeContact")
		k.T("ram:PersonName", p.Ansprechpartner)
		k.Kind("ram:TelephoneUniversalCommunication").T("ram:CompleteNumber", p.Telefon)
		k.Kind("ram:EmailURIUniversalCommunication").T("ram:URIID", p.Email)
	}
	a := n.Kind("ram:PostalTradeAddress")
	a.T("ram:PostcodeCode", p.PLZ)
	a.T("ram:LineOne", p.Strasse)
	a.T("ram:LineTwo", p.Zusatz)
	a.T("ram:CityName", p.Ort)
	a.T("ram:CountryID", oder(p.Land, "DE"))
	if p.EAdresse != "" {
		n.Kind("ram:URIUniversalCommunication").T("ram:URIID", p.EAdresse).A("schemeID", oder(p.EAdresseSchema, "EM"))
	}
	if p.UStID != "" {
		n.Kind("ram:SpecifiedTaxRegistration").T("ram:ID", strings.ReplaceAll(p.UStID, " ", "")).A("schemeID", "VA")
	}
	if verkaeufer && p.Steuernummer != "" {
		n.Kind("ram:SpecifiedTaxRegistration").T("ram:ID", p.Steuernummer).A("schemeID", "FC")
	}
}

func zeit(n *xmlbaum.Knoten, praefix, datum string) {
	n.T(praefix+":DateTimeString", datum102(datum)).A("format", "102")
}

func wahr(b bool) string {
	if b {
		return "true"
	}
	return "false"
}

func oder(a, b string) string {
	if strings.TrimSpace(a) != "" {
		return a
	}
	return b
}

func iban(s string) string { return strings.ToUpper(strings.ReplaceAll(s, " ", "")) }

func alleLeer(k *xmlbaum.Knoten) bool {
	if k.Text != "" {
		return false
	}
	for _, c := range k.Kinder {
		if !alleLeer(c) {
			return false
		}
	}
	return true
}

// erzwingeLieferung fügt das Pflichtelement ApplicableHeaderTradeDelivery
// leer ein, wenn die Rechnung keine Lieferangaben enthält.
func erzwingeLieferung(x []byte) []byte {
	s := string(x)
	if strings.Contains(s, "<ram:ApplicableHeaderTradeDelivery>") {
		return x
	}
	marke := "    <ram:ApplicableHeaderTradeSettlement>"
	return []byte(strings.Replace(s, marke, "    <ram:ApplicableHeaderTradeDelivery/>\n"+marke, 1))
}
