package server

import (
	"fmt"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"

	"marktrechnung/internal/modell"
	"marktrechnung/internal/probe"
	"marktrechnung/internal/zugferd"
)

// Beispiel einer Rechnung, wörtlich aus der Beschreibung der Schnittstelle
// von Lexware Office (developers.lexware.io, "Invoices Properties").
const lexwareRechnung = `{
   "id":"e9066f04-8cc7-4616-93f8-ac9ecc8479c8",
   "organizationId":"aa93e8a8-2aa3-470b-b914-caad8a255dd8",
   "createdDate":"2023-04-24T08:20:22.528+02:00",
   "updatedDate":"2023-04-24T08:20:22.528+02:00",
   "version":0,
   "language":"de",
   "archived":false,
   "voucherStatus":"draft",
   "voucherNumber":"RE1019",
   "voucherDate":"2023-02-22T00:00:00.000+01:00",
   "dueDate":null,
   "address":{"contactId":null,"name":"Bike & Ride GmbH & Co. KG","supplement":"Gebäude 10","street":"Musterstraße 42","city":"Freiburg","zip":"79112","countryCode":"DE"},
   "xRechnung":null,
   "electronicDocumentProfile":"NONE",
   "lineItems":[
      {"id":"97b98491-e953-4dc9-97a9-ae437a8052b4","type":"material","name":"Abus Kabelschloss Primo 590 ","description":"- 9,5 mm starkes Spiralkabel","quantity":2,"unitName":"Stück",
       "unitPrice":{"currency":"EUR","netAmount":13.4,"grossAmount":15.95,"taxRatePercentage":19},"discountPercentage":50,"lineItemAmount":13.4},
      {"id":"dc4c805b-7df1-4310-a548-22be4499eb04","type":"service","name":"Aufwändige Montage","description":"Aufwand für arbeitsintensive Montagetätigkeit","quantity":1,"unitName":"Stunde",
       "unitPrice":{"currency":"EUR","netAmount":8.32,"grossAmount":8.9,"taxRatePercentage":7},"discountPercentage":0,"lineItemAmount":8.32},
      {"id":null,"type":"custom","name":"Energieriegel Testpaket","description":null,"quantity":1,"unitName":"Stück",
       "unitPrice":{"currency":"EUR","netAmount":5,"grossAmount":5,"taxRatePercentage":0},"discountPercentage":0,"lineItemAmount":5},
      {"type":"text","name":"Freitextposition","description":"This item type can contain either a name or a description or both."}
   ],
   "totalPrice":{"currency":"EUR","totalNetAmount":26.72,"totalGrossAmount":29.85,"totalTaxAmount":3.13,"totalDiscountAbsolute":null,"totalDiscountPercentage":null},
   "taxAmounts":[{"taxRatePercentage":0,"taxAmount":0,"netAmount":5},{"taxRatePercentage":7,"taxAmount":0.58,"netAmount":8.32},{"taxRatePercentage":19,"taxAmount":2.55,"netAmount":13.4}],
   "taxConditions":{"taxType":"net","taxTypeNote":null},
   "paymentConditions":{"paymentTermLabel":"10 Tage - 3 %, 30 Tage netto","paymentTermLabelTemplate":"{discountRange} Tage -{discount}, {paymentRange} Tage netto","paymentTermDuration":30,
      "paymentDiscountConditions":{"discountPercentage":3,"discountRange":10}},
   "shippingConditions":{"shippingDate":"2023-04-22T00:00:00.000+02:00","shippingEndDate":null,"shippingType":"delivery"},
   "closingInvoice":false,"claimedGrossAmount":null,"downPaymentDeductions":null,"recurringTemplateId":null,"relatedVouchers":[],
   "printLayoutId":"28c212c4-b6dd-11ee-b80a-dbc65f4ceccf",
   "title":"Rechnung",
   "introduction":"Ihre bestellten Positionen stellen wir Ihnen hiermit in Rechnung",
   "remark":"Vielen Dank für Ihren Einkauf",
   "files":{"documentFileId":"75295db7-7e69-4630-befd-a7f4ddfdaa83"}
}`

const lexwareBelegliste = `{"content":[
  {"id":"aaaa","voucherType":"invoice","voucherStatus":"open","voucherNumber":"RE1012","voucherDate":"2026-11-13T00:00:00.000+01:00","contactId":"777c","contactName":"Muster Markt GmbH ZwNl. West","totalAmount":338.59,"openAmount":338.59,"currency":"EUR","archived":false},
  {"id":"bbbb","voucherType":"invoice","voucherStatus":"overdue","voucherNumber":"RE1019","voucherDate":"2023-02-22T00:00:00.000+01:00","contactId":null,"contactName":"Bike & Ride GmbH & Co. KG","totalAmount":29.85,"openAmount":null,"currency":"EUR","archived":false},
  {"id":"cccc","voucherType":"invoice","voucherStatus":"draft","voucherNumber":"","voucherDate":"2026-11-13T00:00:00.000+01:00","contactName":"Entwurf","totalAmount":1,"currency":"EUR","archived":false},
  {"id":"dddd","voucherType":"quotation","voucherStatus":"open","voucherNumber":"AG1","voucherDate":"2026-11-13T00:00:00.000+01:00","contactName":"Angebot","totalAmount":1,"currency":"EUR","archived":false}
 ],"first":true,"last":true,"totalPages":1,"totalElements":4,"numberOfElements":4,"size":250,"number":0}`

// Kontakt und Artikel ebenfalls nach den Beispielen der Beschreibung.
const lexwareKontakte = `{"content":[
  {"id":"be94","version":1,"roles":{"customer":{"number":10307},"vendor":{"number":70303}},
   "company":{"name":"Testfirma","taxNumber":"12345/12345","vatRegistrationId":"DE123456789","allowTaxFreeInvoices":true,
     "contactPersons":[{"salutation":"Herr","firstName":"Max","lastName":"Mustermann","primary":true,"emailAddress":"contactpersonmail@lexware.de","phoneNumber":"08000/11111"}]},
   "addresses":{"billing":[{"supplement":"Rechnungsadressenzusatz","street":"Hauptstr. 5","zip":"12345","city":"Musterort","countryCode":"DE"}],
                "shipping":[{"supplement":"Lieferadressenzusatz","street":"Schulstr. 13","zip":"76543","city":"MUsterstadt","countryCode":"DE"}]},
   "xRechnung":{"buyerReference":"04011000-1234512345-35","vendorNumberAtCustomer":"70123456"},
   "emailAddresses":{"business":["business@lexware.de"],"office":["office@lexware.de"]},
   "phoneNumbers":{"business":["08000/1231"]},"note":"Notizen","archived":false},
  {"id":"p1","version":1,"roles":{"customer":{"number":10308}},"person":{"salutation":"Frau","firstName":"Erika","lastName":"Musterfrau"},"archived":false},
  {"id":"k2","version":1,"roles":{"customer":{"number":10001}},"company":{"name":"Muster Markt GmbH ZwNl. West"},
   "addresses":{"billing":[{"street":"Handelsstraße 20","zip":"50668","city":"Köln","countryCode":"DE"}]},"archived":false}
 ],"first":true,"last":true}`

const lexwareArtikelliste = `{"content":[
  {"id":"eb46","archived":false,"title":"Lexware buchhaltung Premium 2024","description":"Monatsabonnement.","type":"PRODUCT","articleNumber":"LXW-BUHA-2024-001","gtin":"9783648170632","note":"Interne Notiz","unitName":"Download-Code",
   "price":{"netPrice":61.90,"grossPrice":73.66,"leadingPrice":"NET","taxRate":19},"version":2},
  {"id":"a2","archived":false,"title":"Blütenhonig 500 g","type":"PRODUCT","articleNumber":"H-001","gtin":"4012345000016","unitName":"Glas","price":{"netPrice":5.25,"grossPrice":5.62,"leadingPrice":"NET","taxRate":7},"version":1},
  {"id":"a3","archived":true,"title":"Alter Artikel","type":"PRODUCT","unitName":"Stück","price":{"netPrice":1,"taxRate":19}}
 ],"first":true,"last":true}`

// lexwareAttrappe stellt die Schnittstelle von Lexware Office nach.
func lexwareAttrappe(t *testing.T) *httptest.Server {
	rechnung := probe.Handel()
	rechnung.Nummer = "RE1012"
	dateien, err := zugferd.Erzeuge(rechnung, zugferd.Optionen{})
	if err != nil {
		t.Fatal(err)
	}
	gewoehnlich, _ := zugferd.Vorschau(probe.Handel(), zugferd.Optionen{})
	anfragen := 0
	ts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		anfragen++
		if r.Header.Get("Authorization") != "Bearer richtiger-schluessel" {
			w.WriteHeader(401)
			fmt.Fprint(w, `{"message":"Unauthorized"}`)
			return
		}
		// Jede siebte Anfrage kommt zu schnell - der Abruf muss das abfangen.
		if anfragen%7 == 0 {
			w.WriteHeader(429)
			return
		}
		switch {
		case r.URL.Path == "/v1/profile":
			fmt.Fprint(w, `{"organizationId":"aa93","companyName":"Testfirma GmbH","taxType":"net","smallBusiness":false,"businessFeatures":["INVOICING"]}`)
		case r.URL.Path == "/v1/voucherlist":
			q := r.URL.Query()
			if q.Get("voucherType") == "" || q.Get("voucherStatus") == "" {
				w.WriteHeader(400)
				return
			}
			fmt.Fprint(w, lexwareBelegliste)
		case r.URL.Path == "/v1/invoices/aaaa/file":
			w.Header().Set("Content-Type", "application/pdf")
			w.Write(dateien[0].Daten)
		case r.URL.Path == "/v1/invoices/bbbb/file":
			w.Header().Set("Content-Type", "application/pdf")
			w.Write(gewoehnlich)
		case r.URL.Path == "/v1/invoices/bbbb":
			fmt.Fprint(w, lexwareRechnung)
		case r.URL.Path == "/v1/contacts":
			fmt.Fprint(w, lexwareKontakte)
		case r.URL.Path == "/v1/articles":
			fmt.Fprint(w, lexwareArtikelliste)
		default:
			w.WriteHeader(404)
			fmt.Fprint(w, `{"status":404,"error":"Not Found","message":"gibt es nicht"}`)
		}
	}))
	t.Cleanup(ts.Close)
	return ts
}

func TestLexwareAbruf(t *testing.T) {
	u := neu(t)
	u.einrichten()
	u.server.LexwareBasis = lexwareAttrappe(t).URL

	// Ohne Schlüssel und mit falschem Schlüssel: verständliche Absage.
	var f fehlerAntwort
	if code := u.ruf("GET", "/api/lexware/belege", nil, &f); code != 422 || f.Fehler == "" {
		t.Errorf("ohne Schlüssel: %d", code)
	}
	if code := u.ruf("POST", "/api/lexware/schluessel", map[string]string{"schluessel": "falsch"}, &f); code != 422 || !strings.Contains(f.Fehler, "Zugangsschlüssel") {
		t.Errorf("falscher Schlüssel: %d %s", code, f.Fehler)
	}
	var verbunden struct {
		Verbunden bool
		Firma     string
	}
	if code := u.ruf("POST", "/api/lexware/schluessel", map[string]string{"schluessel": " richtiger-schluessel "}, &verbunden); code != 200 || !verbunden.Verbunden || verbunden.Firma != "Testfirma GmbH" {
		t.Fatalf("Schlüssel: %d %+v", code, verbunden)
	}

	// Belegliste: Entwürfe und Angebote bleiben weg.
	var belege []lexwareBeleg
	if code := u.ruf("GET", "/api/lexware/belege?von=2023-01-01", nil, &belege); code != 200 {
		t.Fatalf("Belege: %d", code)
	}
	if len(belege) != 2 || belege[0].Nummer != "RE1012" || belege[0].Datum != "2026-11-13" || belege[0].Betrag.Fest(2) != "338.59" || belege[0].Stand != "neu" {
		t.Fatalf("Belegliste: %+v", belege)
	}

	// Einlesen: der erste Beleg aus seiner E-Rechnungs-Datei, der zweite aus
	// den Angaben der Schnittstelle.
	var ergebnisse []struct {
		Name string
		Erg  importErgebnis
	}
	auswahl := map[string]any{"profil": "auto", "belege": []map[string]string{
		{"id": "aaaa", "typ": "invoice", "nummer": "RE1012"}, {"id": "bbbb", "typ": "invoice", "nummer": "RE1019"}}}
	if code := u.ruf("POST", "/api/lexware/abrufen", auswahl, &ergebnisse); code != 200 || len(ergebnisse) != 2 {
		t.Fatalf("abrufen: %d, %d Ergebnisse", code, len(ergebnisse))
	}
	a, b := ergebnisse[0].Erg, ergebnisse[1].Erg
	if !a.Lesbar || !a.Uebernehmen || a.Rechnung.Nummer != "RE1012" || a.Kunde == "" || !strings.Contains(a.Dateiart, "ZUGFeRD") {
		t.Errorf("Beleg aus Datei: %+v %q %q", a.Lesbar, a.Kunde, a.Dateiart)
	}
	r := b.Rechnung
	if !b.Lesbar || !b.Uebernehmen || r.Nummer != "RE1019" || r.Datum != "2023-02-22" || r.Leistungsdatum != "2023-04-22" ||
		r.Kaeufer.Name != "Bike & Ride GmbH & Co. KG" || r.Kaeufer.PLZ != "79112" || len(r.Positionen) != 3 {
		t.Fatalf("Beleg aus Schnittstelle: %+v", r)
	}
	p := r.Positionen
	if p[0].Einheit != "H87" || p[0].RabattProzent.String() != "50" || p[0].Einzelpreis.String() != "13.4" || p[1].Einheit != "HUR" ||
		p[1].SteuerSatz.String() != "7" || p[2].SteuerKat != modell.KatBefreit || !strings.Contains(p[2].Hinweis, "Freitextposition") {
		t.Errorf("Positionen: %+v", p)
	}
	if r.Zahlung.Zieltage != 30 || r.Zahlung.SkontoTage != 10 || r.Zahlung.SkontoProzent.String() != "3" || r.Einleitung == "" {
		t.Errorf("Zahlung: %+v", r.Zahlung)
	}
	// Die Summe stimmt auf den Cent mit Lexware Office überein.
	if r.Summen == nil || r.Summen.Brutto.Fest(2) != "29.85" || r.Herkunftsbetrag.Fest(2) != "29.85" {
		t.Errorf("Summe: %+v", r.Summen)
	}
	for _, text := range b.Auffaellig {
		if strings.Contains(text, "ergeben sich") {
			t.Errorf("unerwartete Abweichung: %s", text)
		}
	}

	var entwurf modell.Rechnung
	if code := u.ruf("POST", "/api/import/uebernehmen", map[string]any{"rechnung": b.Rechnung, "artikelAnlegen": true}, &entwurf); code != 200 {
		t.Fatalf("übernehmen: %d", code)
	}
	if entwurf.Nummer != "RE1019" || entwurf.Herkunft != "Lexware Office RE1019" || entwurf.Positionen[0].RabattProzent.String() != "50" {
		t.Errorf("Entwurf: %+v", entwurf)
	}
	u.ruf("GET", "/api/lexware/belege", nil, &belege)
	if belege[1].Stand != "entwurf" {
		t.Errorf("Stand nach der Übernahme: %s", belege[1].Stand)
	}

	// Kunden abgleichen: eine Firma neu, eine bekannte bleibt, die
	// Privatperson wird übergangen.
	var kundenErg map[string]int
	if code := u.ruf("POST", "/api/lexware/kunden", nil, &kundenErg); code != 200 {
		t.Fatalf("Kunden: %d", code)
	}
	var kunden []modell.Kunde
	u.ruf("GET", "/api/kunden", nil, &kunden)
	var testfirma, markt *modell.Kunde
	for i := range kunden {
		switch kunden[i].Name {
		case "Testfirma":
			testfirma = &kunden[i]
		case "Muster Markt GmbH ZwNl. West":
			markt = &kunden[i]
		}
	}
	if kundenErg["gefunden"] != 2 || kundenErg["neu"] != 1 || testfirma == nil || markt == nil {
		t.Fatalf("Kundenabgleich: %v", kundenErg)
	}
	if testfirma.UStID != "DE123456789" || testfirma.Lieferantennummer != "70123456" || testfirma.Kennung != "10307" || testfirma.Strasse != "Hauptstr. 5" ||
		testfirma.RechnungsEmail != "business@lexware.de" || len(testfirma.Lieferorte) != 1 || testfirma.Lieferorte[0].PLZ != "76543" {
		t.Errorf("neuer Kunde: %+v", testfirma)
	}
	// Beim bekannten Kunden bleibt, was hier gepflegt wurde.
	if markt.GLN != "4000001000005" || markt.Lieferantennummer != "700123" || len(markt.Lieferorte) != 1 || markt.Kennung != "10001" {
		t.Errorf("bekannter Kunde wurde verändert: %+v", markt)
	}

	// Artikel abgleichen.
	u.ruf("POST", "/api/artikel", modell.Artikel{Bezeichnung: "Blütenhonig 500 g", GTIN: "", Einheit: "C62"}, nil)
	var artikelErg struct {
		Gefunden, Neu, Geaendert int
		Hinweise                 []string
	}
	if code := u.ruf("POST", "/api/lexware/artikel", nil, &artikelErg); code != 200 {
		t.Fatalf("Artikel: %d", code)
	}
	var artikel []modell.Artikel
	u.ruf("GET", "/api/artikel", nil, &artikel)
	var honig *modell.Artikel
	for i := range artikel {
		if artikel[i].Bezeichnung == "Blütenhonig 500 g" {
			honig = &artikel[i]
		}
	}
	if artikelErg.Gefunden != 2 || artikelErg.Geaendert != 1 || honig == nil || honig.GTIN != "4012345000016" || honig.Einheit != "XJR" ||
		honig.Preis.String() != "5.25" || honig.Artikelnummer != "H-001" || len(artikelErg.Hinweise) != 1 {
		t.Errorf("Artikelabgleich: %+v %+v", artikelErg, honig)
	}

	// Verbindung trennen.
	u.ruf("POST", "/api/lexware/schluessel", map[string]string{"schluessel": ""}, &verbunden)
	if verbunden.Verbunden || u.server.Lexware.Vorhanden() {
		t.Error("der Schlüssel wurde nicht gelöscht")
	}
}
