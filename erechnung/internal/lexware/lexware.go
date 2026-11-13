// Package lexware holt Rechnungen, Kunden und Artikel aus Lexware Office
// (früher lexoffice) über dessen öffentliche Schnittstelle. Es wird nur
// gelesen, in Lexware Office wird nichts verändert.
//
// Beschreibung der Schnittstelle: https://developers.lexware.io/docs/
// Den Zugangsschlüssel erzeugt man in Lexware Office unter
// https://app.lexware.de/addons/public-api
package lexware

import (
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"strings"
	"sync"
	"time"

	"marktrechnung/internal/codes"
	"marktrechnung/internal/dec"
	"marktrechnung/internal/modell"
)

// Adresse der Schnittstelle.
const Adresse = "https://api.lexware.io"

// Client spricht mit Lexware Office.
type Client struct {
	Basis      string // Adresse der Schnittstelle; leer = Adresse
	Schluessel string
	HTTP       *http.Client
	// Pause zwischen zwei Anfragen. Lexware erlaubt zwei Anfragen je Sekunde.
	Pause time.Duration

	mu      sync.Mutex
	letzter time.Time
}

// Neu legt einen Client mit den üblichen Einstellungen an.
func Neu(schluessel string) *Client {
	return &Client{Schluessel: strings.TrimSpace(schluessel), HTTP: &http.Client{Timeout: 40 * time.Second}, Pause: 600 * time.Millisecond}
}

// hole ruft eine Adresse ab und liefert Inhalt und Kopfzeilen.
func (c *Client) hole(pfad, annehmen string) ([]byte, http.Header, error) {
	if c.Schluessel == "" {
		return nil, nil, errors.New("es ist noch kein Zugangsschlüssel für Lexware Office hinterlegt")
	}
	basis := c.Basis
	if basis == "" {
		basis = Adresse
	}
	c.mu.Lock()
	defer c.mu.Unlock()
	for versuch := 0; ; versuch++ {
		if warte := c.Pause - time.Since(c.letzter); warte > 0 {
			time.Sleep(warte)
		}
		anfrage, err := http.NewRequest("GET", basis+pfad, nil)
		if err != nil {
			return nil, nil, err
		}
		anfrage.Header.Set("Authorization", "Bearer "+c.Schluessel)
		anfrage.Header.Set("Accept", annehmen)
		antwort, err := c.HTTP.Do(anfrage)
		c.letzter = time.Now()
		if err != nil {
			return nil, nil, fmt.Errorf("Lexware Office ist nicht erreichbar. Besteht eine Internetverbindung? (%w)", err)
		}
		inhalt, err := io.ReadAll(io.LimitReader(antwort.Body, 60<<20))
		antwort.Body.Close()
		if err != nil {
			return nil, nil, err
		}
		switch {
		case antwort.StatusCode == http.StatusOK:
			return inhalt, antwort.Header, nil
		case antwort.StatusCode == http.StatusTooManyRequests && versuch < 4:
			// Zu schnell gefragt: kurz warten und wiederholen.
			time.Sleep(time.Duration(versuch+1) * c.Pause * 2)
			continue
		case antwort.StatusCode == http.StatusUnauthorized:
			return nil, nil, errors.New("Lexware Office lehnt den Zugangsschlüssel ab. Bitte den Schlüssel prüfen oder in Lexware Office einen neuen erzeugen")
		case antwort.StatusCode == http.StatusPaymentRequired:
			return nil, nil, errors.New("Lexware Office meldet ein Vertragsproblem: Mit dem aktuellen Tarif oder Vertragsstand ist dieser Zugriff nicht möglich")
		case antwort.StatusCode == http.StatusForbidden:
			return nil, nil, errors.New("der Zugangsschlüssel hat nicht die nötigen Rechte. Bitte in Lexware Office einen Schlüssel erzeugen, der Rechnungen, Kontakte und Artikel lesen darf")
		case antwort.StatusCode == http.StatusTooManyRequests:
			return nil, nil, errors.New("Lexware Office nimmt gerade keine weiteren Anfragen an (zu viele in kurzer Zeit). Bitte in einer Minute noch einmal versuchen")
		}
		return nil, antwort.Header, &Statusfehler{Status: antwort.StatusCode, Text: kurz(string(inhalt))}
	}
}

// Statusfehler ist eine Antwort von Lexware Office, die kein Erfolg war.
type Statusfehler struct {
	Status int
	Text   string
}

func (f *Statusfehler) Error() string {
	return fmt.Sprintf("Lexware Office antwortet mit Fehler %d: %s", f.Status, f.Text)
}

func kurz(s string) string {
	s = strings.Join(strings.Fields(s), " ")
	if len(s) > 200 {
		s = s[:200] + " …"
	}
	return s
}

func (c *Client) holeJSON(pfad string, ziel any) error {
	inhalt, _, err := c.hole(pfad, "application/json")
	if err != nil {
		return err
	}
	if err := json.Unmarshal(inhalt, ziel); err != nil {
		return fmt.Errorf("die Antwort von Lexware Office ist nicht lesbar: %w", err)
	}
	return nil
}

// zahl liest einen Betrag, der als JSON-Zahl ankommt, ohne Umweg über
// Gleitkommazahlen.
type zahl struct{ dec.D }

func (z *zahl) UnmarshalJSON(b []byte) error {
	s := strings.Trim(string(b), `"`)
	if s == "null" || s == "" {
		return nil
	}
	d, err := dec.Parse(s)
	if err != nil {
		// Schreibweisen wie 1e-7 kommen bei Geldbeträgen nicht vor; im
		// Zweifel gilt der Wert als null.
		return nil
	}
	z.D = d
	return nil
}

// tag nimmt aus einem Zeitstempel wie 2023-02-22T00:00:00.000+01:00 den
// Tag. Der Stempel meint Mitternacht deutscher Zeit, deshalb wird nicht
// umgerechnet.
func tag(zeitstempel string) string {
	if len(zeitstempel) >= 10 {
		return zeitstempel[:10]
	}
	return ""
}

// ---------------------------------------------------------------------------
// Profil

// Profil sind die Angaben zum Lexware-Konto.
type Profil struct {
	Firma            string `json:"companyName"`
	Kleinunternehmer bool   `json:"smallBusiness"`
}

// Profil prüft den Zugang und liefert den Firmennamen des Kontos.
func (c *Client) Profil() (Profil, error) {
	var p Profil
	err := c.holeJSON("/v1/profile", &p)
	return p, err
}

// ---------------------------------------------------------------------------
// Belegliste

// Beleg ist eine Zeile der Belegliste.
type Beleg struct {
	ID     string `json:"id"`
	Typ    string `json:"typ"`    // "invoice" oder "creditnote"
	Status string `json:"status"` // open, paid, ...
	Nummer string `json:"nummer"`
	Datum  string `json:"datum"`
	Kunde  string `json:"kunde"`
	Betrag dec.D  `json:"betrag"`
}

type seite[T any] struct {
	Inhalt []T  `json:"content"`
	Letzte bool `json:"last"`
}

// Belege listet Rechnungen und Gutschriften ab einem Datum (JJJJ-MM-TT),
// die neuesten zuerst. Entwürfe und stornierte Belege bleiben weg.
func (c *Client) Belege(von string) ([]Beleg, error) {
	var alle []Beleg
	for nr := 0; nr < 40; nr++ {
		q := url.Values{}
		q.Set("voucherType", "invoice,creditnote")
		q.Set("voucherStatus", "any")
		q.Set("sort", "voucherDate,DESC")
		q.Set("size", "250")
		q.Set("page", fmt.Sprint(nr))
		if von != "" {
			q.Set("voucherDateFrom", von)
		}
		var s seite[struct {
			ID     string `json:"id"`
			Typ    string `json:"voucherType"`
			Status string `json:"voucherStatus"`
			Nummer string `json:"voucherNumber"`
			Datum  string `json:"voucherDate"`
			Name   string `json:"contactName"`
			Betrag zahl   `json:"totalAmount"`
		}]
		if err := c.holeJSON("/v1/voucherlist?"+q.Encode(), &s); err != nil {
			return nil, err
		}
		for _, b := range s.Inhalt {
			if b.Status == "draft" || b.Status == "voided" || (b.Typ != "invoice" && b.Typ != "creditnote") {
				continue
			}
			alle = append(alle, Beleg{ID: b.ID, Typ: b.Typ, Status: b.Status, Nummer: b.Nummer, Datum: tag(b.Datum), Kunde: b.Name, Betrag: b.Betrag.D})
		}
		if s.Letzte || len(s.Inhalt) == 0 {
			break
		}
	}
	return alle, nil
}

func belegPfad(typ string) string {
	if typ == "creditnote" {
		return "/v1/credit-notes/"
	}
	return "/v1/invoices/"
}

// Datei lädt die Datei eines Belegs: ein ZUGFeRD-PDF, eine XRechnung (XML)
// oder - wenn der Beleg keine E-Rechnung ist - ein gewöhnliches PDF.
func (c *Client) Datei(typ, id string) ([]byte, error) {
	inhalt, _, err := c.hole(belegPfad(typ)+url.PathEscape(id)+"/file", "*/*")
	return inhalt, err
}

// ---------------------------------------------------------------------------
// Beleg im Einzelnen

type belegJSON struct {
	Nummer  string `json:"voucherNumber"`
	Datum   string `json:"voucherDate"`
	Faellig string `json:"dueDate"`
	Adresse struct {
		Name    string `json:"name"`
		Zusatz  string `json:"supplement"`
		Strasse string `json:"street"`
		Ort     string `json:"city"`
		PLZ     string `json:"zip"`
		Land    string `json:"countryCode"`
	} `json:"address"`
	XRechnung *struct {
		Kaeuferreferenz string `json:"buyerReference"`
	} `json:"xRechnung"`
	Positionen []struct {
		Art          string `json:"type"`
		Name         string `json:"name"`
		Beschreibung string `json:"description"`
		Menge        zahl   `json:"quantity"`
		Einheit      string `json:"unitName"`
		Preis        struct {
			Netto zahl `json:"netAmount"`
			Satz  zahl `json:"taxRatePercentage"`
		} `json:"unitPrice"`
		Rabatt zahl `json:"discountPercentage"`
	} `json:"lineItems"`
	Summe struct {
		Brutto        zahl `json:"totalGrossAmount"`
		RabattProzent zahl `json:"totalDiscountPercentage"`
		RabattBetrag  zahl `json:"totalDiscountAbsolute"`
	} `json:"totalPrice"`
	Steuer struct {
		Art     string `json:"taxType"`
		Hinweis string `json:"taxTypeNote"`
	} `json:"taxConditions"`
	Zahlung struct {
		Tage   int `json:"paymentTermDuration"`
		Skonto *struct {
			Prozent zahl `json:"discountPercentage"`
			Tage    int  `json:"discountRange"`
		} `json:"paymentDiscountConditions"`
	} `json:"paymentConditions"`
	Lieferung struct {
		Datum string `json:"shippingDate"`
		Ende  string `json:"shippingEndDate"`
		Art   string `json:"shippingType"`
	} `json:"shippingConditions"`
	Bezug []struct {
		Nummer string `json:"voucherNumber"`
		Typ    string `json:"voucherType"`
	} `json:"relatedVouchers"`
	Einleitung string `json:"introduction"`
	Schluss    string `json:"remark"`
}

// Steuerarten von Lexware Office und ihre Kategorie in der E-Rechnung.
var steuerarten = map[string]string{
	"vatfree":                   modell.KatBefreit,
	"intraCommunitySupply":      modell.KatEULieferung,
	"constructionService13b":    modell.KatReverseCharge,
	"externalService13b":        modell.KatReverseCharge,
	"thirdPartyCountryService":  modell.KatNichtSteuerb,
	"thirdPartyCountryDelivery": modell.KatAusfuhr,
	"photovoltaicEquipment":     modell.KatNull,
}

// Rechnung holt einen Beleg mit allen Angaben und übersetzt ihn in das
// Datenmodell des Programms. Dieser Weg dient als Ersatz, wenn der Beleg in
// Lexware Office keine E-Rechnung ist; sonst ist die Datei die bessere
// Quelle. Der Rechnungssteller bleibt leer und wird vom Aufrufer gesetzt.
func (c *Client) Rechnung(typ, id string) (modell.Rechnung, []string, error) {
	var b belegJSON
	if err := c.holeJSON(belegPfad(typ)+url.PathEscape(id), &b); err != nil {
		return modell.Rechnung{}, nil, err
	}
	r, hinweise := uebersetze(b, typ)
	return r, hinweise, nil
}

func uebersetze(b belegJSON, typ string) (modell.Rechnung, []string) {
	var hinweise []string
	r := modell.Rechnung{Art: modell.ArtRechnung, Nummer: b.Nummer, Datum: tag(b.Datum), Faellig: tag(b.Faellig), Waehrung: "EUR",
		Einleitung: klartext(b.Einleitung), Schluss: klartext(b.Schluss)}
	if typ == "creditnote" {
		r.Art = modell.ArtGutschrift
		for _, v := range b.Bezug {
			if v.Typ == "invoice" && v.Nummer != "" {
				r.Vorgaenger = append(r.Vorgaenger, modell.Vorgaenger{Nummer: v.Nummer})
			}
		}
	}
	r.Kaeufer = modell.Partei{Name: b.Adresse.Name, Zusatz: b.Adresse.Zusatz, Strasse: b.Adresse.Strasse, PLZ: b.Adresse.PLZ,
		Ort: b.Adresse.Ort, Land: land(b.Adresse.Land)}
	if b.XRechnung != nil {
		r.Kaeuferreferenz = b.XRechnung.Kaeuferreferenz
	}

	kat := steuerarten[b.Steuer.Art]
	if kat == modell.KatBefreit {
		r.Befreiungsgrund = b.Steuer.Hinweis
	}
	unbekannt := map[string]bool{}
	for _, p := range b.Positionen {
		if p.Art == "text" {
			// Freitext zwischen den Positionen: zur vorigen Position, sonst
			// in die Einleitung.
			text := strings.TrimSpace(klartext(p.Name + "\n" + p.Beschreibung))
			if n := len(r.Positionen); n > 0 {
				r.Positionen[n-1].Hinweis = strings.TrimSpace(r.Positionen[n-1].Hinweis + "\n" + text)
			} else {
				r.Einleitung = strings.TrimSpace(r.Einleitung + "\n" + text)
			}
			continue
		}
		einheit, bekannt := codes.EinheitAusName(p.Einheit)
		if !bekannt && strings.TrimSpace(p.Einheit) != "" {
			unbekannt[p.Einheit] = true
		}
		pos := modell.Position{Bezeichnung: strings.TrimSpace(p.Name), Beschreibung: klartext(p.Beschreibung), Menge: p.Menge.D,
			Einheit: einheit, Einzelpreis: p.Preis.Netto.D, RabattProzent: p.Rabatt.D, SteuerSatz: p.Preis.Satz.D, SteuerKat: modell.KatNormal}
		switch {
		case kat != "":
			pos.SteuerKat, pos.SteuerSatz = kat, 0
		case pos.SteuerSatz.IstNull():
			pos.SteuerKat = modell.KatBefreit
		}
		r.Positionen = append(r.Positionen, pos)
	}
	for name := range unbekannt {
		hinweise = append(hinweise, "Die Einheit „"+name+"“ ist dem Programm nicht bekannt und wurde als allgemeine Einheit übernommen. Bitte im Entwurf die passende Einheit wählen.")
	}

	// Rabatt auf die ganze Rechnung, verteilt auf die Steuersätze.
	if !b.Summe.RabattProzent.IstNull() {
		gesehen := map[dec.D]bool{}
		for _, p := range r.Positionen {
			if !gesehen[p.SteuerSatz] {
				gesehen[p.SteuerSatz] = true
				r.ZuAbschlaege = append(r.ZuAbschlaege, modell.ZuAbschlag{Prozent: b.Summe.RabattProzent.D, Grund: "Rabatt", Code: "95",
					SteuerKat: p.SteuerKat, SteuerSatz: p.SteuerSatz})
			}
		}
	} else if !b.Summe.RabattBetrag.IstNull() {
		hinweise = append(hinweise, "Die Rechnung enthält in Lexware Office einen Gesamtrabatt als festen Betrag ("+b.Summe.RabattBetrag.Deutsch(2, 2)+" €). Er wurde nicht übernommen - bitte im Entwurf als Rabatt auf die Rechnung nachtragen.")
	}

	switch b.Lieferung.Art {
	case "serviceperiod", "deliveryperiod":
		r.ZeitraumVon, r.ZeitraumBis = tag(b.Lieferung.Datum), tag(b.Lieferung.Ende)
	case "none":
	default:
		r.Leistungsdatum = tag(b.Lieferung.Datum)
	}
	r.Zahlung.Art = "58"
	r.Zahlung.Zieltage = b.Zahlung.Tage
	if s := b.Zahlung.Skonto; s != nil {
		r.Zahlung.SkontoTage, r.Zahlung.SkontoProzent = s.Tage, s.Prozent.D
	}
	r.Herkunftsbetrag = b.Summe.Brutto.D
	return r, hinweise
}

// klartext entfernt die Auszeichnungen (**fett**, __kursiv__), die Lexware
// Office in Texten erlaubt.
func klartext(s string) string {
	s = strings.NewReplacer("**", "", "__", "").Replace(s)
	return strings.TrimSpace(s)
}

// land kürzt erweiterte Ländercodes wie "ES_CN" auf den Staat.
func land(code string) string {
	code = strings.ToUpper(strings.TrimSpace(code))
	if i := strings.IndexByte(code, '_'); i > 0 {
		code = code[:i]
	}
	if code == "" {
		return "DE"
	}
	return code
}

// ---------------------------------------------------------------------------
// Kunden und Artikel

type adresseJSON struct {
	Zusatz  string `json:"supplement"`
	Strasse string `json:"street"`
	PLZ     string `json:"zip"`
	Ort     string `json:"city"`
	Land    string `json:"countryCode"`
}

// Kunden liefert alle Firmenkunden aus Lexware Office. Privatpersonen
// bleiben weg: E-Rechnungen gehen an Firmen.
func (c *Client) Kunden() ([]modell.Kunde, error) {
	var alle []modell.Kunde
	for nr := 0; nr < 200; nr++ {
		var s seite[struct {
			Archiviert bool `json:"archived"`
			Rollen     struct {
				Kunde *struct {
					Nummer json.Number `json:"number"`
				} `json:"customer"`
			} `json:"roles"`
			Firma *struct {
				Name  string `json:"name"`
				UStID string `json:"vatRegistrationId"`
				Leute []struct {
					Vorname  string `json:"firstName"`
					Nachname string `json:"lastName"`
					Haupt    bool   `json:"primary"`
					Email    string `json:"emailAddress"`
					Telefon  string `json:"phoneNumber"`
				} `json:"contactPersons"`
			} `json:"company"`
			Adressen struct {
				Rechnung  []adresseJSON `json:"billing"`
				Lieferung []adresseJSON `json:"shipping"`
			} `json:"addresses"`
			XRechnung *struct {
				Kaeuferreferenz   string `json:"buyerReference"`
				Lieferantennummer string `json:"vendorNumberAtCustomer"`
			} `json:"xRechnung"`
			Email struct {
				Geschaeft []string `json:"business"`
				Buero     []string `json:"office"`
				Andere    []string `json:"other"`
			} `json:"emailAddresses"`
		}]
		if err := c.holeJSON(fmt.Sprintf("/v1/contacts?customer=true&size=250&page=%d", nr), &s); err != nil {
			return nil, err
		}
		for _, k := range s.Inhalt {
			if k.Archiviert || k.Firma == nil || k.Rollen.Kunde == nil || strings.TrimSpace(k.Firma.Name) == "" {
				continue
			}
			neu := modell.Kunde{Format: modell.FormatZugferd}
			neu.Name = strings.TrimSpace(k.Firma.Name)
			neu.UStID = strings.ToUpper(strings.ReplaceAll(k.Firma.UStID, " ", ""))
			neu.Kennung = k.Rollen.Kunde.Nummer.String()
			neu.Land = "DE"
			if len(k.Adressen.Rechnung) > 0 {
				a := k.Adressen.Rechnung[0]
				neu.Zusatz, neu.Strasse, neu.PLZ, neu.Ort, neu.Land = a.Zusatz, a.Strasse, a.PLZ, a.Ort, land(a.Land)
			}
			for _, a := range k.Adressen.Lieferung {
				if strings.TrimSpace(a.Strasse) == "" && strings.TrimSpace(a.Ort) == "" {
					continue
				}
				name := strings.TrimSpace(a.Zusatz)
				if name == "" {
					name = strings.TrimSpace(neu.Name + " " + a.Ort)
				}
				neu.Lieferorte = append(neu.Lieferorte, modell.Lieferort{Name: name, Strasse: a.Strasse, PLZ: a.PLZ, Ort: a.Ort, Land: land(a.Land)})
			}
			if k.XRechnung != nil {
				neu.Kaeuferreferenz, neu.Lieferantennummer = k.XRechnung.Kaeuferreferenz, k.XRechnung.Lieferantennummer
			}
			for _, liste := range [][]string{k.Email.Geschaeft, k.Email.Buero, k.Email.Andere} {
				if neu.RechnungsEmail == "" && len(liste) > 0 {
					neu.RechnungsEmail = strings.TrimSpace(liste[0])
				}
			}
			for _, p := range k.Firma.Leute {
				if neu.Ansprechpartner == "" || p.Haupt {
					neu.Ansprechpartner = strings.TrimSpace(p.Vorname + " " + p.Nachname)
					neu.Telefon, neu.Email = p.Telefon, p.Email
				}
			}
			alle = append(alle, neu)
		}
		if s.Letzte || len(s.Inhalt) == 0 {
			break
		}
	}
	return alle, nil
}

// Artikel liefert alle Artikel aus Lexware Office.
func (c *Client) Artikel() ([]modell.Artikel, []string, error) {
	var alle []modell.Artikel
	unbekannt := map[string]bool{}
	for nr := 0; nr < 200; nr++ {
		var s seite[struct {
			Archiviert   bool   `json:"archived"`
			Titel        string `json:"title"`
			Beschreibung string `json:"description"`
			Nummer       string `json:"articleNumber"`
			GTIN         string `json:"gtin"`
			Einheit      string `json:"unitName"`
			Preis        struct {
				Netto zahl `json:"netPrice"`
				Satz  zahl `json:"taxRate"`
			} `json:"price"`
		}]
		if err := c.holeJSON(fmt.Sprintf("/v1/articles?size=250&page=%d", nr), &s); err != nil {
			return nil, nil, err
		}
		for _, a := range s.Inhalt {
			if a.Archiviert || strings.TrimSpace(a.Titel) == "" {
				continue
			}
			einheit, bekannt := codes.EinheitAusName(a.Einheit)
			if !bekannt && strings.TrimSpace(a.Einheit) != "" {
				unbekannt[a.Einheit] = true
			}
			gtin := strings.ReplaceAll(strings.TrimSpace(a.GTIN), " ", "")
			if gtin != "" && !codes.GTINGueltig(gtin) {
				gtin = "" // eine fehlerhafte Nummer würde die Rechnung ungültig machen
			}
			alle = append(alle, modell.Artikel{Artikelnummer: strings.TrimSpace(a.Nummer), GTIN: gtin, Bezeichnung: strings.TrimSpace(a.Titel),
				Beschreibung: klartext(a.Beschreibung), Einheit: einheit, Preis: a.Preis.Netto.D, SteuerSatz: a.Preis.Satz.D})
		}
		if s.Letzte || len(s.Inhalt) == 0 {
			break
		}
	}
	var hinweise []string
	for name := range unbekannt {
		hinweise = append(hinweise, "Einheit „"+name+"“ unbekannt – als allgemeine Einheit übernommen")
	}
	return alle, hinweise, nil
}
