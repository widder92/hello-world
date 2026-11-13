// Package modell beschreibt eine Rechnung so, wie die europäische Norm
// EN 16931 sie sieht. Die Kürzel in den Kommentaren (BT-1, BG-4 ...) sind die
// Feldnummern der Norm; unter ihnen findet man jedes Feld in den
// Fehlermeldungen der Rechnungseingangsportale wieder.
package modell

import "marktrechnung/internal/dec"

// Status einer Rechnung im Programm.
const (
	StatusEntwurf   = "entwurf"   // frei änderbar, noch keine Nummer vergeben
	StatusFertig    = "fertig"    // festgeschrieben, Dateien erzeugt
	StatusVersendet = "versendet" // mindestens einmal verschickt
	StatusBezahlt   = "bezahlt"
	StatusStorniert = "storniert" // durch eine Gutschrift aufgehoben
)

// Rechnungsarten (BT-3, Codeliste UNTDID 1001).
const (
	ArtRechnung   = "380"
	ArtGutschrift = "381" // Rechnungskorrektur / Stornorechnung
	ArtAbschlag   = "326" // Abschlagsrechnung (Teilrechnung)
	ArtKorrektur  = "384"
)

// Ausgabeformate.
const (
	FormatZugferd      = "zugferd"       // PDF mit eingebetteter XML, Profil EN 16931
	FormatZugferdXR    = "zugferd-xr"    // PDF mit eingebetteter XML, Profil XRechnung
	FormatXRechnungCII = "xrechnung-cii" // reine XML-Datei, Syntax CII
)

// Steuerkategorien (BT-118/BT-151, Codeliste UNCL 5305).
const (
	KatNormal        = "S"  // steuerpflichtig mit Steuersatz (19 %, 7 %)
	KatNull          = "Z"  // Nullsatz
	KatBefreit       = "E"  // steuerbefreit, z. B. Kleinunternehmer nach § 19 UStG
	KatReverseCharge = "AE" // Steuerschuld geht auf den Empfänger über
	KatEULieferung   = "K"  // innergemeinschaftliche Lieferung
	KatAusfuhr       = "G"  // Ausfuhr in ein Drittland
	KatNichtSteuerb  = "O"  // nicht steuerbar
)

// Partei ist Verkäufer (BG-4) oder Käufer (BG-7).
type Partei struct {
	Name        string `json:"name"`        // BT-27 / BT-44
	Handelsname string `json:"handelsname"` // BT-28 / BT-45
	Strasse     string `json:"strasse"`     // BT-35 / BT-50
	Zusatz      string `json:"zusatz"`      // BT-36 / BT-51
	PLZ         string `json:"plz"`         // BT-38 / BT-53
	Ort         string `json:"ort"`         // BT-37 / BT-52
	Land        string `json:"land"`        // BT-40 / BT-55, zwei Buchstaben

	UStID        string `json:"ustId"`        // BT-31 / BT-48
	Steuernummer string `json:"steuernummer"` // BT-32, nur Verkäufer
	Register     string `json:"register"`     // BT-30 / BT-47, z. B. "HRB 12345"
	Rechtliches  string `json:"rechtliches"`  // BT-33, nur Verkäufer

	// GLN ist die Global Location Number (früher ILN). Sie landet beim
	// Verkäufer in BT-29, beim Käufer in BT-46, jeweils mit Schema 0088.
	GLN string `json:"gln"`
	// Kennung ist eine frei vergebene Nummer: beim Verkäufer die
	// Lieferantennummer, die der Kunde vergeben hat (BT-29 ohne Schema), beim
	// Käufer die Kundennummer (BT-46).
	Kennung string `json:"kennung"`

	// Elektronische Adresse (BT-34 / BT-49). Schema "EM" bedeutet E-Mail,
	// "0088" GLN, "9930" deutsche USt-IdNr.
	EAdresse       string `json:"eAdresse"`
	EAdresseSchema string `json:"eAdresseSchema"`

	Ansprechpartner string `json:"ansprechpartner"` // BT-41 / BT-56
	Telefon         string `json:"telefon"`         // BT-42 / BT-57
	Email           string `json:"email"`           // BT-43 / BT-58
}

// Lieferort ist der Warenempfänger (BG-13), im Handel der einzelne Markt.
type Lieferort struct {
	ID      string `json:"id,omitempty"` // nur in den Kundenstammdaten
	Name    string `json:"name"`         // BT-70
	GLN     string `json:"gln"`          // BT-71 mit Schema 0088
	Kennung string `json:"kennung"`      // BT-71 ohne Schema, z. B. Marktnummer
	Strasse string `json:"strasse"`      // BT-75
	Zusatz  string `json:"zusatz"`       // BT-76
	PLZ     string `json:"plz"`          // BT-78
	Ort     string `json:"ort"`          // BT-77
	Land    string `json:"land"`         // BT-80
	// Kostenstelle des Markts; wird beim Auswählen in die Rechnung übernommen.
	Kostenstelle string `json:"kostenstelle,omitempty"`
	// Kundennummer, unter der dieser Markt im eigenen Rechnungsprogramm
	// geführt wird. Sie hilft beim Einlesen, den Markt wiederzuerkennen.
	Kundennummer string `json:"kundennummer,omitempty"`
}

// Leer meldet, ob gar kein Lieferort angegeben ist.
func (l *Lieferort) Leer() bool {
	return l == nil || (l.Name == "" && l.GLN == "" && l.Kennung == "" && l.Strasse == "" && l.PLZ == "" && l.Ort == "")
}

// HatAdresse meldet, ob eine Anschrift (BG-15) angegeben ist.
func (l *Lieferort) HatAdresse() bool {
	return l != nil && (l.Strasse != "" || l.PLZ != "" || l.Ort != "" || l.Zusatz != "")
}

// ZuAbschlag ist ein Nachlass oder Zuschlag, entweder für die ganze Rechnung
// (BG-20 / BG-21) oder für eine Position (BG-27 / BG-28).
type ZuAbschlag struct {
	Zuschlag bool   `json:"zuschlag"` // false = Nachlass, true = Zuschlag
	Betrag   dec.D  `json:"betrag"`   // BT-92 / BT-99 / BT-136 / BT-141
	Basis    dec.D  `json:"basis"`    // BT-93 / BT-100 / BT-137 / BT-142
	Prozent  dec.D  `json:"prozent"`  // BT-94 / BT-101 / BT-138 / BT-143
	Grund    string `json:"grund"`    // BT-97 / BT-104 / BT-139 / BT-144
	Code     string `json:"code"`     // BT-98 / BT-105 / BT-140 / BT-145
	// Nur auf Rechnungsebene: Steuer, die auf den Nachlass entfällt.
	SteuerKat  string `json:"steuerKat"`  // BT-95 / BT-102
	SteuerSatz dec.D  `json:"steuerSatz"` // BT-96 / BT-103
}

// Position ist eine Rechnungszeile (BG-25).
type Position struct {
	Nr            string `json:"nr"`            // BT-126
	Artikelnummer string `json:"artikelnummer"` // BT-155, eigene Nummer
	KundenArtNr   string `json:"kundenArtNr"`   // BT-156, Nummer beim Kunden
	GTIN          string `json:"gtin"`          // BT-157 mit Schema 0160 (EAN)
	Bezeichnung   string `json:"bezeichnung"`   // BT-153
	Beschreibung  string `json:"beschreibung"`  // BT-154
	Hinweis       string `json:"hinweis"`       // BT-127
	BestellPos    string `json:"bestellPos"`    // BT-132
	Herkunft      string `json:"herkunft"`      // BT-159, Ursprungsland

	Menge       dec.D  `json:"menge"`       // BT-129
	Einheit     string `json:"einheit"`     // BT-130, Code nach UN/ECE Rec. 20
	Einzelpreis dec.D  `json:"einzelpreis"` // BT-146, netto
	Preisbasis  dec.D  `json:"preisbasis"`  // BT-149, Preis gilt je so viele Einheiten
	Listenpreis dec.D  `json:"listenpreis"` // BT-148, optional
	Preisrabatt dec.D  `json:"preisrabatt"` // BT-147, optional

	// RabattProzent ist die einfache Eingabe im Programm: ein Rabatt in
	// Prozent auf die Position. Beim Rechnen wird daraus ein Nachlass.
	RabattProzent dec.D        `json:"rabattProzent"`
	RabattGrund   string       `json:"rabattGrund"`
	ZuAbschlaege  []ZuAbschlag `json:"zuAbschlaege,omitempty"` // BG-27 / BG-28

	SteuerKat  string `json:"steuerKat"`  // BT-151
	SteuerSatz dec.D  `json:"steuerSatz"` // BT-152

	ZeitraumVon string `json:"zeitraumVon,omitempty"` // BT-134
	ZeitraumBis string `json:"zeitraumBis,omitempty"` // BT-135

	Netto dec.D `json:"netto"` // BT-131, wird berechnet
}

// Zahlung enthält Zahlungsweg und Zahlungsbedingungen (BG-16 bis BG-19, BT-20).
type Zahlung struct {
	Art              string `json:"art"`              // BT-81: 58 Überweisung, 59 Lastschrift
	Verwendungszweck string `json:"verwendungszweck"` // BT-83
	IBAN             string `json:"iban"`             // BT-84
	BIC              string `json:"bic"`              // BT-86
	Kontoinhaber     string `json:"kontoinhaber"`     // BT-85
	Bank             string `json:"bank"`             // nur für das PDF

	Mandat          string `json:"mandat"`          // BT-89
	GlaeubigerID    string `json:"glaeubigerId"`    // BT-90
	LastschriftIBAN string `json:"lastschriftIban"` // BT-91

	Zieltage      int    `json:"zieltage"`      // Zahlungsziel in Tagen
	SkontoTage    int    `json:"skontoTage"`    // 0 = kein Skonto
	SkontoProzent dec.D  `json:"skontoProzent"` //
	Bedingungen   string `json:"bedingungen"`   // BT-20; leer = wird erzeugt
	Vorauszahlung dec.D  `json:"vorauszahlung"` // BT-113
}

// Hinweis ist ein Freitext zur Rechnung (BG-1).
type Hinweis struct {
	Code string `json:"code"` // BT-21, z. B. "REG" für Pflichtangaben zur Firma
	Text string `json:"text"` // BT-22
}

// Vorgaenger verweist auf eine frühere Rechnung (BG-3), etwa bei Gutschriften.
type Vorgaenger struct {
	Nummer string `json:"nummer"` // BT-25
	Datum  string `json:"datum"`  // BT-26
}

// Steuerzeile ist eine Zeile der Umsatzsteueraufschlüsselung (BG-23).
type Steuerzeile struct {
	Kat       string `json:"kat"`       // BT-118
	Satz      dec.D  `json:"satz"`      // BT-119
	Basis     dec.D  `json:"basis"`     // BT-116
	Betrag    dec.D  `json:"betrag"`    // BT-117
	Grund     string `json:"grund"`     // BT-120
	GrundCode string `json:"grundCode"` // BT-121
}

// Summen sind die Gesamtbeträge der Rechnung (BG-22).
type Summen struct {
	Positionen    dec.D         `json:"positionen"`    // BT-106
	Abschlaege    dec.D         `json:"abschlaege"`    // BT-107
	Zuschlaege    dec.D         `json:"zuschlaege"`    // BT-108
	Netto         dec.D         `json:"netto"`         // BT-109
	Steuer        dec.D         `json:"steuer"`        // BT-110
	Brutto        dec.D         `json:"brutto"`        // BT-112
	Vorauszahlung dec.D         `json:"vorauszahlung"` // BT-113
	Rundung       dec.D         `json:"rundung"`       // BT-114
	Zahlbetrag    dec.D         `json:"zahlbetrag"`    // BT-115
	Steuern       []Steuerzeile `json:"steuern"`
}

// Versand hält fest, wann eine Rechnung an wen verschickt wurde.
type Versand struct {
	Zeit    string `json:"zeit"`
	An      string `json:"an"`
	Betreff string `json:"betreff"`
	Dateien string `json:"dateien"`
	Weg     string `json:"weg"` // "email" oder "manuell"
	Fehler  string `json:"fehler,omitempty"`
}

// Rechnung ist ein Beleg im Programm: Entwurf oder festgeschriebene Rechnung.
type Rechnung struct {
	ID     string `json:"id"`
	Status string `json:"status"`

	Art      string `json:"art"`      // BT-3
	Nummer   string `json:"nummer"`   // BT-1
	Datum    string `json:"datum"`    // BT-2, JJJJ-MM-TT
	Faellig  string `json:"faellig"`  // BT-9
	Waehrung string `json:"waehrung"` // BT-5

	Leistungsdatum string `json:"leistungsdatum"` // BT-72
	ZeitraumVon    string `json:"zeitraumVon"`    // BT-73
	ZeitraumBis    string `json:"zeitraumBis"`    // BT-74

	KundeID    string     `json:"kundeId"`
	Verkaeufer Partei     `json:"verkaeufer"`
	Kaeufer    Partei     `json:"kaeufer"`
	Lieferort  *Lieferort `json:"lieferort,omitempty"`

	Kaeuferreferenz    string `json:"kaeuferreferenz"`    // BT-10 (Leitweg-ID)
	Bestellnummer      string `json:"bestellnummer"`      // BT-13
	Auftragsnummer     string `json:"auftragsnummer"`     // BT-14
	Vertragsnummer     string `json:"vertragsnummer"`     // BT-12
	Lieferscheinnummer string `json:"lieferscheinnummer"` // BT-16
	Wareneingang       string `json:"wareneingang"`       // BT-15
	Projekt            string `json:"projekt"`            // BT-11
	Kostenstelle       string `json:"kostenstelle"`       // BT-19
	Objekt             string `json:"objekt"`             // BT-18

	Vorgaenger []Vorgaenger `json:"vorgaenger,omitempty"`

	Positionen   []Position   `json:"positionen"`
	ZuAbschlaege []ZuAbschlag `json:"zuAbschlaege,omitempty"`
	Zahlung      Zahlung      `json:"zahlung"`
	Hinweise     []Hinweis    `json:"hinweise,omitempty"`

	// Texte nur für das PDF (und als Hinweis BT-22 in der XML).
	Einleitung string `json:"einleitung"`
	Schluss    string `json:"schluss"`

	// Kleinunternehmer: keine Umsatzsteuer nach § 19 UStG.
	Kleinunternehmer bool `json:"kleinunternehmer"`
	// Begründung für steuerfreie Umsätze (BT-120), wenn Positionen ohne
	// Umsatzsteuer berechnet werden.
	Befreiungsgrund string `json:"befreiungsgrund"`

	Format string `json:"format"`
	Profil string `json:"profil"` // Vorgaben des Handelspartners, z. B. "rewe"

	// Summen werden für eigene Rechnungen berechnet. Bei eingelesenen
	// Rechnungen stehen hier die Beträge aus der Datei, damit die Prüfung
	// Rechenfehler findet.
	Summen *Summen `json:"summen,omitempty"`

	// Kennung des Standards, wie sie in einer eingelesenen Datei stand (BT-24).
	Spezifikation string `json:"spezifikation,omitempty"`
	Prozess       string `json:"prozess,omitempty"` // BT-23
	Herkunft      string `json:"herkunft,omitempty"`
	// Bruttobetrag, wie er in der eingelesenen Datei stand. Weicht die neu
	// berechnete Summe davon ab, warnt das Programm.
	Herkunftsbetrag dec.D `json:"herkunftsbetrag,omitempty"`

	// Zusatzbelege (Angebot, Lieferschein) haben denselben Aufbau wie eine
	// Rechnung, liegen aber getrennt von den Rechnungen.
	Belegart   string    `json:"belegart,omitempty"`   // leer: Rechnung; sonst BelegAngebot oder BelegLieferschein
	GueltigBis string    `json:"gueltigBis,omitempty"` // Angebot: gültig bis
	Folgebeleg string    `json:"folgebeleg,omitempty"` // was aus dem Beleg wurde, z. B. "Rechnung RE-2026-0007"
	Mahnungen  []Mahnung `json:"mahnungen,omitempty"`  // Zahlungserinnerungen und Mahnungen zu dieser Rechnung

	Dateien        []string  `json:"dateien,omitempty"` // erzeugte Dateien im Ausgangsordner
	Versand        []Versand `json:"versand,omitempty"`
	StornoVon      string    `json:"stornoVon,omitempty"`   // ID der aufgehobenen Rechnung
	StornoDurch    string    `json:"stornoDurch,omitempty"` // ID der Gutschrift
	Erstellt       string    `json:"erstellt"`
	Geaendert      string    `json:"geaendert"`
	Fertiggestellt string    `json:"fertiggestellt,omitempty"`
	BezahltAm      string    `json:"bezahltAm,omitempty"`
}

// Kunde ist ein Rechnungsempfänger in den Stammdaten.
type Kunde struct {
	ID string `json:"id"`
	Partei
	// Debitorenkonto: eigenes Personenkonto dieses Kunden in der
	// Buchhaltung (DATEV). Leer: das Sammelkonto aus den Einstellungen.
	Debitorenkonto string `json:"debitorenkonto"`
	// Alias: abweichende Namen, unter denen der Kunde in einem anderen
	// Programm geführt wird (durch Semikolon getrennt). Daran erkennt der
	// Import den Kunden wieder; in der Rechnung erscheint der Alias nicht.
	Alias string `json:"alias"`
	// Unsere Lieferantennummer bei diesem Kunden (wandert in BT-29).
	Lieferantennummer string `json:"lieferantennummer"`
	// Feste Käuferreferenz / Leitweg-ID (BT-10), falls der Kunde eine vorgibt.
	Kaeuferreferenz string `json:"kaeuferreferenz"`

	Profil string `json:"profil"` // "rewe", "edeka", "handel" oder leer
	Format string `json:"format"`

	// Rechnungseingang des Kunden.
	RechnungsEmail string `json:"rechnungsEmail"`
	EmailBetreff   string `json:"emailBetreff"`
	EmailText      string `json:"emailText"`

	Zieltage      int   `json:"zieltage"`
	SkontoTage    int   `json:"skontoTage"`
	SkontoProzent dec.D `json:"skontoProzent"`

	Lieferorte []Lieferort `json:"lieferorte"`
	Notiz      string      `json:"notiz"`
}

// Artikel ist eine Ware oder Leistung in den Stammdaten.
type Artikel struct {
	ID            string `json:"id"`
	Artikelnummer string `json:"artikelnummer"`
	GTIN          string `json:"gtin"`
	Bezeichnung   string `json:"bezeichnung"`
	Beschreibung  string `json:"beschreibung"`
	Einheit       string `json:"einheit"`
	Preis         dec.D  `json:"preis"`
	SteuerSatz    dec.D  `json:"steuerSatz"`
	Herkunft      string `json:"herkunft"`
	// Artikelnummern beim Kunden, Schlüssel ist die Kunden-ID.
	KundenArtNr map[string]string `json:"kundenArtNr,omitempty"`
}

// Postausgang sind die Zugangsdaten zum E-Mail-Konto. Das Passwort steht nicht
// hier, sondern getrennt im geschützten Speicher des Benutzerkontos.
type Postausgang struct {
	Server       string `json:"server"`
	Port         int    `json:"port"`
	Sicherheit   string `json:"sicherheit"` // "tls" (465) oder "starttls" (587)
	Benutzer     string `json:"benutzer"`
	Absender     string `json:"absender"`     // E-Mail-Adresse
	AbsenderName string `json:"absenderName"` //
	KopieAnMich  bool   `json:"kopieAnMich"`  // Blindkopie an den Absender
	PasswortDa   bool   `json:"passwortDa"`   // nur Anzeige
}

// Einstellungen des Programms.
type Einstellungen struct {
	Firma             Partei `json:"firma"`
	Kleinunternehmer  bool   `json:"kleinunternehmer"`
	Geschaeftsleitung string `json:"geschaeftsleitung"` // für die Fußzeile
	Webseite          string `json:"webseite"`

	IBAN         string `json:"iban"`
	BIC          string `json:"bic"`
	Bank         string `json:"bank"`
	Kontoinhaber string `json:"kontoinhaber"`

	// Nummernkreis: Muster mit {JJJJ}, {JJ}, {MM} und {N...} für die
	// laufende Nummer, z. B. "RE-{JJJJ}-{NNNN}".
	Nummernmuster   string `json:"nummernmuster"`
	NaechsteNummer  int    `json:"naechsteNummer"`
	NummerJahr      int    `json:"nummerJahr"`   // Jahr, für das NaechsteNummer gilt
	JaehrlichNeu    bool   `json:"jaehrlichNeu"` // jedes Jahr wieder bei 1 anfangen
	Zieltage        int    `json:"zieltage"`     // Standard-Zahlungsziel
	Einleitung      string `json:"einleitung"`   // Standardtexte
	Schluss         string `json:"schluss"`
	StandardSteuer  dec.D  `json:"standardSteuer"`  // z. B. 7 für Lebensmittel
	StandardEinheit string `json:"standardEinheit"` // z. B. "H87"
	Format          string `json:"format"`
	LogoDatei       string `json:"logoDatei"`

	Post Postausgang `json:"post"`

	Datev  Datev  `json:"datev"`
	Zusatz Zusatz `json:"zusatz"`
}

// Zusatzbelege neben der Rechnung.
const (
	BelegAngebot      = "angebot"
	BelegLieferschein = "lieferschein"

	StatusOffen      = "offen"      // Angebot abgegeben, Lieferschein geschrieben
	StatusAngenommen = "angenommen" // Angebot angenommen
	StatusAbgelehnt  = "abgelehnt"  // Angebot abgelehnt
	StatusBerechnet  = "berechnet"  // aus dem Beleg ist eine Rechnung entstanden
)

// Zusatz: Funktionen neben dem Schreiben von Rechnungen. Jede lässt sich
// einzeln einschalten; ausgeschaltet erscheint sie im Programm nicht.
type Zusatz struct {
	Angebote      bool `json:"angebote"`
	Lieferscheine bool `json:"lieferscheine"`
	Mahnungen     bool `json:"mahnungen"`
	Serien        bool `json:"serien"`
	Auswertung    bool `json:"auswertung"`
	Abschlaege    bool `json:"abschlaege"`

	AngebotMuster        string `json:"angebotMuster"`        // z. B. "AN-{JJJJ}-{NNNN}"
	AngebotNaechste      int    `json:"angebotNaechste"`      // nächste laufende Nummer
	AngebotTage          int    `json:"angebotTage"`          // Gültigkeit eines Angebots in Tagen
	LieferscheinMuster   string `json:"lieferscheinMuster"`   // z. B. "LS-{JJJJ}-{NNNN}"
	LieferscheinNaechste int    `json:"lieferscheinNaechste"` // nächste laufende Nummer
	Mahnfrist            int    `json:"mahnfrist"`            // neue Zahlungsfrist in Tagen
	Mahngebuehr          dec.D  `json:"mahngebuehr"`          // Gebühr ab der ersten Mahnung
}

// Mahnung: eine Zahlungserinnerung (Stufe 0) oder Mahnung (Stufe 1, 2 ...)
// zu einer Rechnung.
type Mahnung struct {
	Stufe       int    `json:"stufe"`
	Datum       string `json:"datum"`
	Frist       string `json:"frist"` // zahlbar bis
	Gebuehr     dec.D  `json:"gebuehr"`
	Datei       string `json:"datei"` // im Ordner Rechnungen
	VersendetAn string `json:"versendetAn,omitempty"`
	VersendetAm string `json:"versendetAm,omitempty"`
}

// Serie: eine Rechnung, die sich in festem Abstand wiederholt. Ist sie
// fällig, legt das Programm aus der Vorlage einen Entwurf an.
type Serie struct {
	ID       string   `json:"id"`
	Name     string   `json:"name"`
	Vorlage  Rechnung `json:"vorlage"`
	Abstand  string   `json:"abstand"`  // "woche", "monat", "quartal", "halbjahr", "jahr"
	Naechste string   `json:"naechste"` // Datum der nächsten Rechnung
	Aktiv    bool     `json:"aktiv"`
	Letzte   string   `json:"letzte,omitempty"` // Datum der zuletzt angelegten Rechnung
}

// Datev: Angaben für die Übergabe an DATEV. Ohne Kontenrahmen gibt das
// Programm keine Konten mit - dann ordnet die Steuerkanzlei sie zu.
type Datev struct {
	Berater      string `json:"berater"`      // Beraternummer der Kanzlei
	Mandant      string `json:"mandant"`      // Mandantennummer
	Kontenrahmen string `json:"kontenrahmen"` // "03" (SKR 03), "04" (SKR 04) oder leer
	Kontenlaenge int    `json:"kontenlaenge"` // Länge der Sachkonten, 4 bis 8
	Erloes19     string `json:"erloes19"`     // Erlöskonto für 19 % USt
	Erloes7      string `json:"erloes7"`      // Erlöskonto für 7 % USt
	ErloesOhne   string `json:"erloesOhne"`   // Erlöskonto ohne USt
	Debitor      string `json:"debitor"`      // Sammelkonto für Kunden ohne eigenes Konto
	WJBeginn     int    `json:"wjBeginn"`     // Monat, in dem das Wirtschaftsjahr beginnt
	Ordner       string `json:"ordner"`       // Rechnungsordner in DATEV Unternehmen online
}

// Meldung ist ein Ergebnis der Prüfung.
type Meldung struct {
	Regel   string `json:"regel"`   // z. B. "BR-DE-15" oder "REWE-GLN"
	Schwere string `json:"schwere"` // "fehler", "warnung" oder "hinweis"
	Feld    string `json:"feld"`    // Pfad zum Eingabefeld, z. B. "kaeufer.gln"
	Text    string `json:"text"`    // verständliche Erklärung
}

// Schweregrade einer Meldung.
const (
	Fehler  = "fehler"
	Warnung = "warnung"
	Info    = "hinweis"
)
