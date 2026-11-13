// Package codes enthält die Codelisten, die in E-Rechnungen erlaubt sind:
// Mengeneinheiten, Länder, Währungen, Zahlungsarten und so weiter. Ein Wert,
// der nicht in der jeweiligen Liste steht, führt beim Empfänger zur Ablehnung.
package codes

import (
	_ "embed"
	"strings"
)

//go:embed listen.txt
var listenText string

var listen = map[string]map[string]bool{}

func init() {
	for _, zeile := range strings.Split(listenText, "\n") {
		name, werte, ok := strings.Cut(zeile, ":")
		if !ok || strings.HasPrefix(zeile, "#") {
			continue
		}
		m := map[string]bool{}
		for _, w := range strings.Fields(werte) {
			m[w] = true
		}
		listen[strings.TrimSpace(name)] = m
	}
}

// Namen der Listen.
const (
	Belegart     = "belegart"     // UNTDID 1001
	Land         = "land"         // ISO 3166-1
	Einheit      = "einheit"      // UN/ECE Rec. 20 und 21
	Steuerkat    = "steuerkat"    // UNCL 5305
	Waehrung     = "waehrung"     // ISO 4217
	Zahlungsart  = "zahlungsart"  // UNTDID 4461
	Vatex        = "vatex"        // Befreiungsgründe
	EAS          = "eas"          // Schemata elektronischer Adressen
	ICD          = "icd"          // Schemata für Kennungen (ISO 6523)
	Textcode     = "textcode"     // UNTDID 4451
	Abschlagcode = "abschlagcode" // UNTDID 5189
)

// Gueltig meldet, ob ein Wert in der genannten Liste steht.
func Gueltig(liste, wert string) bool { return listen[liste][wert] }

// EinheitInfo beschreibt eine Mengeneinheit für die Auswahl im Programm.
type EinheitInfo struct {
	Code string `json:"code"`
	Kurz string `json:"kurz"` // Abkürzung für das PDF
	Name string `json:"name"`
}

// Einheiten ist die Auswahl gebräuchlicher Mengeneinheiten. In der XML steht
// der Code, auf dem PDF die Abkürzung.
var Einheiten = []EinheitInfo{
	{"H87", "Stk.", "Stück"},
	{"KGM", "kg", "Kilogramm"},
	{"GRM", "g", "Gramm"},
	{"LTR", "l", "Liter"},
	{"MLT", "ml", "Milliliter"},
	{"XPK", "Pck.", "Packung"},
	{"XCT", "Krt.", "Karton"},
	{"XCR", "Kiste", "Kiste / Steige"},
	{"XBX", "Box", "Schachtel / Box"},
	{"XBO", "Fl.", "Flasche"},
	{"XJR", "Glas", "Glas"},
	{"XTN", "Dose", "Dose"},
	{"XCU", "Becher", "Becher"},
	{"XBG", "Beutel", "Beutel"},
	{"XSA", "Sack", "Sack"},
	{"XNT", "Netz", "Netz"},
	{"XBE", "Bund", "Bund"},
	{"XPU", "Schale", "Schale"},
	{"XBJ", "Eimer", "Eimer"},
	{"XBK", "Korb", "Korb"},
	{"XPX", "Pal.", "Palette"},
	{"XRO", "Rolle", "Rolle"},
	{"DZN", "Dtzd.", "Dutzend"},
	{"PR", "Paar", "Paar"},
	{"SET", "Set", "Set"},
	{"MTR", "m", "Meter"},
	{"MTK", "m²", "Quadratmeter"},
	{"HUR", "Std.", "Stunde"},
	{"DAY", "Tag", "Tag"},
	{"MON", "Monat", "Monat"},
	{"LS", "psch.", "Pauschale"},
	{"C62", "Einh.", "Einheit (allgemein)"},
}

// EinheitKurz liefert die Abkürzung zu einem Einheitencode.
func EinheitKurz(code string) string {
	for _, e := range Einheiten {
		if e.Code == code {
			return e.Kurz
		}
	}
	return code
}

// Laender für die Auswahl im Programm (Code und deutscher Name).
var Laender = [][2]string{
	{"DE", "Deutschland"}, {"AT", "Österreich"}, {"CH", "Schweiz"}, {"NL", "Niederlande"},
	{"BE", "Belgien"}, {"LU", "Luxemburg"}, {"FR", "Frankreich"}, {"IT", "Italien"},
	{"ES", "Spanien"}, {"PL", "Polen"}, {"CZ", "Tschechien"}, {"DK", "Dänemark"},
	{"SE", "Schweden"}, {"GB", "Vereinigtes Königreich"},
}

// LandName liefert den deutschen Namen eines Landes, sonst den Code.
func LandName(code string) string {
	for _, l := range Laender {
		if l[0] == code {
			return l[1]
		}
	}
	return code
}

// GLNGueltig prüft Länge und Prüfziffer einer GLN (13 Ziffern) oder GTIN
// (8, 12, 13 oder 14 Ziffern).
func GLNGueltig(s string) bool { return len(s) == 13 && pruefziffer(s) }

// GTINGueltig prüft eine Artikelnummer GTIN/EAN.
func GTINGueltig(s string) bool {
	switch len(s) {
	case 8, 12, 13, 14:
		return pruefziffer(s)
	}
	return false
}

func pruefziffer(s string) bool {
	summe := 0
	for i := 0; i < len(s); i++ {
		c := s[len(s)-1-i]
		if c < '0' || c > '9' {
			return false
		}
		z := int(c - '0')
		if i%2 == 1 {
			z *= 3
		}
		summe += z
	}
	return summe%10 == 0
}

// IBANGueltig prüft Aufbau und Prüfsumme einer IBAN (Leerzeichen erlaubt).
func IBANGueltig(s string) bool {
	s = strings.ToUpper(strings.ReplaceAll(s, " ", ""))
	if len(s) < 15 || len(s) > 34 {
		return false
	}
	if s[:2] == "DE" && len(s) != 22 {
		return false
	}
	// Die ersten vier Zeichen ans Ende, Buchstaben zu Zahlen, Rest mod 97.
	u := s[4:] + s[:4]
	rest := 0
	for _, c := range u {
		switch {
		case c >= '0' && c <= '9':
			rest = (rest*10 + int(c-'0')) % 97
		case c >= 'A' && c <= 'Z':
			rest = (rest*100 + int(c-'A') + 10) % 97
		default:
			return false
		}
	}
	return rest == 1
}
