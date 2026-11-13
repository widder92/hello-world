package server

import (
	"errors"
	"fmt"
	"net/http"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"time"

	"marktrechnung/internal/beleg"
	"marktrechnung/internal/dec"
	"marktrechnung/internal/modell"
	"marktrechnung/internal/post"
	"marktrechnung/internal/rechnen"
	"marktrechnung/internal/speicher"
)

// Zusatzfunktionen neben dem Schreiben von Rechnungen: Angebote und
// Lieferscheine, Mahnungen, Serienrechnungen und eine Auswertung. Jede lässt
// sich in den Einstellungen einschalten; nichts davon ist nötig, um eine
// Rechnung zu schreiben.

const tag = "2006-01-02"

// ---------------------------------------------------------------------------
// Angebote und Lieferscheine

// belegFertig ergänzt einen Zusatzbeleg um Firma, Kunde und Summen - so, wie
// er gedruckt wird.
func (s *Server) belegFertig(b modell.Rechnung) (modell.Rechnung, modell.Einstellungen, error) {
	status := b.Status
	b.Status, b.Summen = "", nil
	voll, e, err := s.zusammenstellen(b)
	if err != nil {
		return b, e, err
	}
	voll.Status = status
	// Der Hinweis auf die Konditionen gehört nur in die Rechnung.
	var hinweise []modell.Hinweis
	for _, h := range voll.Hinweise {
		if h.Code != "AAK" {
			hinweise = append(hinweise, h)
		}
	}
	voll.Hinweise = hinweise
	return rechnen.Ausrechnen(voll), e, nil
}

func belegArt(art string) (string, bool) {
	switch art {
	case modell.BelegAngebot, modell.BelegLieferschein:
		return art, true
	}
	return "", false
}

func (s *Server) belegeListe(w http.ResponseWriter, r *http.Request) {
	art, ok := belegArt(r.URL.Query().Get("art"))
	if !ok {
		fehler(w, 400, errors.New("unbekannte Belegart"))
		return
	}
	l, err := s.Speicher.Belege(art)
	if err != nil {
		fehler(w, 500, err)
		return
	}
	for i := range l {
		if voll, _, err := s.belegFertig(l[i]); err == nil {
			l[i].Summen, l[i].Kaeufer = voll.Summen, voll.Kaeufer
		}
	}
	antworte(w, 200, l)
}

func (s *Server) belegLesen(w http.ResponseWriter, r *http.Request) {
	b, err := s.Speicher.Beleg(r.PathValue("id"))
	if err != nil {
		fehler(w, 404, err)
		return
	}
	antworte(w, 200, b)
}

func (s *Server) belegSpeichern(w http.ResponseWriter, r *http.Request) {
	var b modell.Rechnung
	if err := liesJSON(r, &b); err != nil {
		fehler(w, 400, err)
		return
	}
	if _, ok := belegArt(b.Belegart); !ok {
		fehler(w, 400, errors.New("unbekannte Belegart"))
		return
	}
	if b.KundeID == "" {
		fehler(w, 422, errors.New("bitte einen Kunden wählen"))
		return
	}
	if _, err := time.Parse(tag, b.Datum); err != nil {
		b.Datum = time.Now().Format(tag)
	}
	var positionen []modell.Position
	for _, p := range b.Positionen {
		if strings.TrimSpace(p.Bezeichnung) != "" {
			positionen = append(positionen, p)
		}
	}
	if len(positionen) == 0 {
		fehler(w, 422, errors.New("bitte mindestens eine Position mit Bezeichnung eintragen"))
		return
	}
	b.Positionen = positionen
	b.Summen, b.Art = nil, ""
	gespeichert, err := s.Speicher.SpeichereBeleg(b)
	if err != nil {
		fehler(w, 409, err)
		return
	}
	antworte(w, 200, gespeichert)
}

func (s *Server) belegLoeschen(w http.ResponseWriter, r *http.Request) {
	if err := s.Speicher.LoescheBeleg(r.PathValue("id")); err != nil {
		fehler(w, 404, err)
		return
	}
	antworte(w, 200, map[string]bool{"ok": true})
}

// belegDatei erzeugt das PDF eines Zusatzbelegs. Es wird nicht abgelegt,
// sondern bei jedem Aufruf frisch gesetzt.
func (s *Server) belegDatei(b modell.Rechnung) (name string, daten []byte, err error) {
	voll, e, err := s.belegFertig(b)
	if err != nil {
		return "", nil, err
	}
	daten, err = beleg.PDF(voll, beleg.Optionen{Einst: e, Logo: s.Speicher.Logo(), Zeit: time.Now(), Version: s.Version})
	return beleg.Titel(voll) + "_" + dateiname(voll.Nummer) + ".pdf", daten, err
}

// dateiname lässt von einer Nummer nur Zeichen übrig, die in Dateinamen
// keinen Ärger machen.
func dateiname(s string) string {
	return strings.Map(func(r rune) rune {
		if r >= 'a' && r <= 'z' || r >= 'A' && r <= 'Z' || r >= '0' && r <= '9' || r == '-' || r == '_' || r == '.' {
			return r
		}
		return '-'
	}, s)
}

func (s *Server) belegPDF(w http.ResponseWriter, r *http.Request) {
	b, err := s.Speicher.Beleg(r.PathValue("id"))
	if err != nil {
		fehler(w, 404, err)
		return
	}
	name, daten, err := s.belegDatei(b)
	if err != nil {
		fehler(w, 500, err)
		return
	}
	anzeige := "inline"
	if r.URL.Query().Get("laden") != "" {
		anzeige = "attachment"
	}
	w.Header().Set("Content-Type", "application/pdf")
	w.Header().Set("Content-Disposition", fmt.Sprintf(`%s; filename="%s"`, anzeige, name))
	w.Write(daten)
}

// belegWeiter macht aus einem Angebot einen Lieferschein oder aus einem
// Angebot oder Lieferschein den Entwurf einer Rechnung.
func (s *Server) belegWeiter(w http.ResponseWriter, r *http.Request) {
	b, err := s.Speicher.Beleg(r.PathValue("id"))
	if err != nil {
		fehler(w, 404, err)
		return
	}
	ziel := r.PathValue("ziel")
	heute := time.Now().Format(tag)
	neu := modell.Rechnung{KundeID: b.KundeID, Lieferort: b.Lieferort, Positionen: b.Positionen, ZuAbschlaege: b.ZuAbschlaege,
		Bestellnummer: b.Bestellnummer, Einleitung: b.Einleitung, Schluss: b.Schluss, Datum: heute, Zahlung: b.Zahlung}
	bezug := beleg.Titel(b) + " " + b.Nummer + " vom " + rechnen.DatumDeutsch(b.Datum)
	var folge string
	switch {
	case ziel == "lieferschein" && b.Belegart == modell.BelegAngebot:
		neu.Belegart, neu.Leistungsdatum = modell.BelegLieferschein, heute
		neu.Einleitung, neu.Schluss = "Wir liefern gemäß unserem "+bezug+":", ""
		gespeichert, err := s.Speicher.SpeichereBeleg(neu)
		if err != nil {
			fehler(w, 500, err)
			return
		}
		neu, folge = gespeichert, "Lieferschein "+gespeichert.Nummer
	case ziel == "rechnung":
		if b.Belegart == modell.BelegLieferschein {
			neu.Lieferscheinnummer, neu.Leistungsdatum = b.Nummer, b.Leistungsdatum
			if neu.Leistungsdatum == "" {
				neu.Leistungsdatum = b.Datum
			}
			neu.Einleitung = ""
		} else {
			neu.Leistungsdatum = heute
			neu.Einleitung = "Wir berechnen gemäß unserem " + bezug + ":"
		}
		neu.Herkunft = bezug
		gespeichert, err := s.Speicher.SpeichereEntwurf(neu)
		if err != nil {
			fehler(w, 500, err)
			return
		}
		neu, folge = gespeichert, "Rechnung (Entwurf vom "+rechnen.DatumDeutsch(heute)+")"
		b.Status = modell.StatusBerechnet
	default:
		fehler(w, 400, errors.New("das geht mit diesem Beleg nicht"))
		return
	}
	if b.Status == modell.StatusOffen && b.Belegart == modell.BelegAngebot {
		b.Status = modell.StatusAngenommen
	}
	b.Folgebeleg = folge
	if _, err := s.Speicher.SpeichereBeleg(b); err != nil {
		fehler(w, 500, err)
		return
	}
	antworte(w, 200, neu)
}

// mailMitAnhang verschickt eine E-Mail mit einer Datei über das
// eingerichtete Postfach. Im Fehlerfall ist die Antwort schon geschrieben.
func (s *Server) mailMitAnhang(w http.ResponseWriter, anText, betreff, text, name string, daten []byte) (an []string, ok bool) {
	an, err := post.Adressen(anText)
	if err != nil || len(an) == 0 {
		if err == nil {
			err = errors.New("bitte die E-Mail-Adresse des Empfängers eintragen")
		}
		fehler(w, 422, err)
		return nil, false
	}
	if strings.TrimSpace(betreff) == "" {
		fehler(w, 422, errors.New("bitte einen Betreff eintragen"))
		return nil, false
	}
	e, err := s.Speicher.Einstellungen()
	if err != nil {
		fehler(w, 500, err)
		return nil, false
	}
	pw, err := s.Passwort.Lies()
	if err != nil {
		fehler(w, 500, err)
		return nil, false
	}
	n := post.Nachricht{An: an, Betreff: strings.TrimSpace(betreff), Text: text,
		Anhang: []post.Anhang{{Name: name, Mime: "application/pdf", Daten: daten}}}
	if e.Post.KopieAnMich && e.Post.Absender != "" {
		n.Blind = []string{e.Post.Absender}
	}
	if _, err := s.Sende(n, e.Post, pw); err != nil {
		fehler(w, 502, err)
		return nil, false
	}
	return an, true
}

func (s *Server) belegSenden(w http.ResponseWriter, r *http.Request) {
	var ein struct{ An, Betreff, Text string }
	if err := liesJSON(r, &ein); err != nil {
		fehler(w, 400, err)
		return
	}
	b, err := s.Speicher.Beleg(r.PathValue("id"))
	if err != nil {
		fehler(w, 404, err)
		return
	}
	name, daten, err := s.belegDatei(b)
	if err != nil {
		fehler(w, 500, err)
		return
	}
	an, ok := s.mailMitAnhang(w, ein.An, ein.Betreff, ein.Text, name, daten)
	if !ok {
		return
	}
	antworte(w, 200, map[string]string{"an": strings.Join(an, ", ")})
}

// ---------------------------------------------------------------------------
// Offene Posten und Mahnungen

type offenerPosten struct {
	ID         string `json:"id"`
	Nummer     string `json:"nummer"`
	Datum      string `json:"datum"`
	Faellig    string `json:"faellig"`
	Kunde      string `json:"kunde"`
	Profil     string `json:"profil"`
	Betrag     dec.D  `json:"betrag"`
	Tage       int    `json:"tage"`  // Tage seit Fälligkeit; negativ: noch nicht fällig
	Stufe      int    `json:"stufe"` // -1: noch nichts verschickt, 0 Erinnerung, 1 erste Mahnung ...
	LetzteVom  string `json:"letzteVom"`
	LetzteBis  string `json:"letzteBis"`
	Versendet  bool   `json:"versendet"`
	Ueberfaell bool   `json:"ueberfaellig"`
}

// offenePosten liefert die fertigen, noch nicht bezahlten Rechnungen, die
// am längsten fälligen zuerst.
func offenePosten(alle []modell.Rechnung, heute time.Time) []offenerPosten {
	l := []offenerPosten{}
	for _, re := range alle {
		if re.Summen == nil || re.Art == modell.ArtGutschrift || (re.Status != modell.StatusFertig && re.Status != modell.StatusVersendet) {
			continue
		}
		p := offenerPosten{ID: re.ID, Nummer: re.Nummer, Datum: re.Datum, Faellig: re.Faellig, Kunde: re.Kaeufer.Name, Profil: re.Profil,
			Betrag: re.Summen.Zahlbetrag, Stufe: -1, Versendet: re.Status == modell.StatusVersendet}
		if f, err := time.Parse(tag, re.Faellig); err == nil {
			p.Tage = int(heute.Sub(f).Hours() / 24)
			p.Ueberfaell = p.Tage > 0
		}
		if n := len(re.Mahnungen); n > 0 {
			p.Stufe, p.LetzteVom, p.LetzteBis = re.Mahnungen[n-1].Stufe, re.Mahnungen[n-1].Datum, re.Mahnungen[n-1].Frist
		}
		l = append(l, p)
	}
	sort.SliceStable(l, func(i, j int) bool { return l[i].Tage > l[j].Tage })
	return l
}

func (s *Server) offenePostenListe(w http.ResponseWriter, r *http.Request) {
	alle, err := s.Speicher.Rechnungen()
	if err != nil {
		fehler(w, 500, err)
		return
	}
	antworte(w, 200, offenePosten(alle, time.Now()))
}

// mahntext schlägt das Anschreiben zur Stufe vor.
func mahntext(stufe int, re modell.Rechnung, frist string) string {
	bis := rechnen.DatumDeutsch(frist)
	anrede := "Sehr geehrte Damen und Herren,\n\n"
	switch {
	case stufe <= 0:
		return anrede + "sicher ist es Ihrer Aufmerksamkeit entgangen, dass unsere Rechnung " + re.Nummer + " noch nicht ausgeglichen ist. " +
			"Wir bitten Sie, den offenen Betrag bis zum " + bis + " zu überweisen."
	case stufe == 1:
		return anrede + "unsere Rechnung " + re.Nummer + " ist trotz Fälligkeit noch offen. " +
			"Wir bitten Sie, den unten genannten Betrag bis zum " + bis + " zu überweisen."
	}
	return anrede + "auf unsere bisherigen Schreiben zur Rechnung " + re.Nummer + " haben wir keinen Zahlungseingang feststellen können. " +
		"Wir fordern Sie auf, den unten genannten Betrag bis spätestens " + bis + " zu begleichen. Andernfalls behalten wir uns weitere Schritte vor."
}

// mahnVorschlag nennt für eine Rechnung die nächste Stufe mit Frist, Gebühr
// und Text.
func (s *Server) mahnVorschlag(w http.ResponseWriter, r *http.Request) {
	re, err := s.Speicher.Rechnung(r.PathValue("id"))
	if err != nil {
		fehler(w, 404, err)
		return
	}
	e, err := s.Speicher.Einstellungen()
	if err != nil {
		fehler(w, 500, err)
		return
	}
	stufe := 0
	if n := len(re.Mahnungen); n > 0 {
		stufe = re.Mahnungen[n-1].Stufe + 1
	}
	tage := e.Zusatz.Mahnfrist
	if tage < 1 {
		tage = 10
	}
	frist := time.Now().AddDate(0, 0, tage).Format(tag)
	gebuehr := dec.D(0)
	if stufe >= 1 {
		gebuehr = e.Zusatz.Mahngebuehr
	}
	var hinweise []string
	if re.Profil == "rewe" || re.Profil == "edeka" {
		hinweise = append(hinweise, "An das Rechnungspostfach des Handels gehören keine Mahnungen - dort werden sie nicht gelesen. Bitte an den Ansprechpartner in der Buchhaltung oder im Einkauf schicken.")
	}
	antworte(w, 200, map[string]any{"stufe": stufe, "titel": beleg.MahnTitel(stufe), "frist": frist, "gebuehr": gebuehr,
		"text": mahntext(stufe, re, frist), "hinweise": hinweise, "betrag": re.Summen.Zahlbetrag})
}

func (s *Server) mahnungAnlegen(w http.ResponseWriter, r *http.Request) {
	var ein struct {
		Stufe   int    `json:"stufe"`
		Frist   string `json:"frist"`
		Gebuehr dec.D  `json:"gebuehr"`
		Text    string `json:"text"`
	}
	if err := liesJSON(r, &ein); err != nil {
		fehler(w, 400, err)
		return
	}
	re, err := s.Speicher.Rechnung(r.PathValue("id"))
	if err != nil {
		fehler(w, 404, err)
		return
	}
	if re.Summen == nil || (re.Status != modell.StatusFertig && re.Status != modell.StatusVersendet) || re.Art == modell.ArtGutschrift {
		fehler(w, 409, errors.New("gemahnt werden kann nur eine fertige, noch nicht bezahlte Rechnung"))
		return
	}
	if _, err := time.Parse(tag, ein.Frist); err != nil {
		fehler(w, 422, errors.New("bitte die neue Zahlungsfrist eintragen"))
		return
	}
	if ein.Stufe < 0 || ein.Stufe > 9 || ein.Gebuehr < 0 || strings.TrimSpace(ein.Text) == "" {
		fehler(w, 422, errors.New("Stufe, Gebühr oder Text fehlen"))
		return
	}
	e, err := s.Speicher.Einstellungen()
	if err != nil {
		fehler(w, 500, err)
		return
	}
	heute := time.Now().Format(tag)
	m := beleg.Mahnschreiben{Stufe: ein.Stufe, Datum: heute, Frist: ein.Frist, Gebuehr: ein.Gebuehr, Text: strings.TrimSpace(ein.Text)}
	daten, err := beleg.Mahnung(re, m, beleg.Optionen{Einst: e, Logo: s.Speicher.Logo(), Zeit: time.Now(), Version: s.Version})
	if err != nil {
		fehler(w, 500, err)
		return
	}
	jahr := heute[:4]
	var rel string
	for n := 1; ; n++ {
		zusatz := ""
		if n > 1 {
			zusatz = fmt.Sprintf("_%d", n)
		}
		rel = filepath.ToSlash(filepath.Join(jahr, "Mahnungen", fmt.Sprintf("%s_%s_%s%s.pdf", dateiname(beleg.MahnTitel(ein.Stufe)), dateiname(re.Nummer), heute, zusatz)))
		err = s.Speicher.SchreibeDatei(rel, daten)
		if err == nil || n > 20 {
			break
		}
	}
	if err != nil {
		fehler(w, 500, err)
		return
	}
	re, err = s.Speicher.Aendere(re.ID, func(x *modell.Rechnung) error {
		x.Mahnungen = append(x.Mahnungen, modell.Mahnung{Stufe: ein.Stufe, Datum: heute, Frist: ein.Frist, Gebuehr: ein.Gebuehr, Datei: rel})
		return nil
	})
	if err != nil {
		fehler(w, 500, err)
		return
	}
	antworte(w, 200, re)
}

// mahnung sucht die Mahnung mit der Nummer aus der Adresse.
func (s *Server) mahnung(w http.ResponseWriter, r *http.Request) (re modell.Rechnung, nr int, daten []byte, ok bool) {
	re, err := s.Speicher.Rechnung(r.PathValue("id"))
	if err != nil {
		fehler(w, 404, err)
		return re, 0, nil, false
	}
	fmt.Sscan(r.PathValue("nr"), &nr)
	if nr < 0 || nr >= len(re.Mahnungen) {
		fehler(w, 404, speicher.ErrNichtGefunden)
		return re, 0, nil, false
	}
	daten, err = s.Speicher.LiesDatei(re.Mahnungen[nr].Datei)
	if err != nil {
		fehler(w, 404, fmt.Errorf("die Datei %s liegt nicht mehr im Ordner Rechnungen", re.Mahnungen[nr].Datei))
		return re, 0, nil, false
	}
	return re, nr, daten, true
}

func (s *Server) mahnungPDF(w http.ResponseWriter, r *http.Request) {
	re, nr, daten, ok := s.mahnung(w, r)
	if !ok {
		return
	}
	w.Header().Set("Content-Type", "application/pdf")
	w.Header().Set("Content-Disposition", fmt.Sprintf(`inline; filename="%s"`, filepath.Base(re.Mahnungen[nr].Datei)))
	w.Write(daten)
}

func (s *Server) mahnungOeffnen(w http.ResponseWriter, r *http.Request) {
	re, nr, _, ok := s.mahnung(w, r)
	if !ok {
		return
	}
	pfad := filepath.Join(s.Speicher.AusgangOrdner(), filepath.FromSlash(re.Mahnungen[nr].Datei))
	if s.Oeffne == nil {
		fehler(w, 501, errors.New("Datei: "+pfad))
		return
	}
	if err := s.Oeffne(pfad); err != nil {
		fehler(w, 500, err)
		return
	}
	antworte(w, 200, map[string]string{"pfad": pfad})
}

func (s *Server) mahnungSenden(w http.ResponseWriter, r *http.Request) {
	var ein struct{ An, Betreff, Text string }
	if err := liesJSON(r, &ein); err != nil {
		fehler(w, 400, err)
		return
	}
	re, nr, daten, ok := s.mahnung(w, r)
	if !ok {
		return
	}
	an, ok := s.mailMitAnhang(w, ein.An, ein.Betreff, ein.Text, filepath.Base(re.Mahnungen[nr].Datei), daten)
	if !ok {
		return
	}
	re, err := s.Speicher.Aendere(re.ID, func(x *modell.Rechnung) error {
		if nr < len(x.Mahnungen) {
			x.Mahnungen[nr].VersendetAn, x.Mahnungen[nr].VersendetAm = strings.Join(an, ", "), speicher.Jetzt()
		}
		return nil
	})
	if err != nil {
		fehler(w, 500, fmt.Errorf("die E-Mail ist verschickt, der Vermerk konnte aber nicht gespeichert werden: %w", err))
		return
	}
	antworte(w, 200, re)
}

// ---------------------------------------------------------------------------
// Abschlagsrechnungen
//
// Eine Abschlagsrechnung ist eine Rechnung der Art 326 (Teilrechnung). Die
// Schlussrechnung nennt die ganze Leistung und zieht die Abschläge wieder
// ab - je Steuersatz als Position mit der Menge -1 und dem Nettobetrag des
// Abschlags. So bleibt in der Schlussrechnung nur die Umsatzsteuer auf den
// Rest, und über "Vorgänger" (BG-3) verweist sie auf die Abschlagsrechnungen.

type offenerAbschlag struct {
	ID      string               `json:"id"`
	Nummer  string               `json:"nummer"`
	Datum   string               `json:"datum"`
	Brutto  dec.D                `json:"brutto"`
	Steuern []modell.Steuerzeile `json:"steuern"`
}

// abschlaegeListe nennt die fertigen Abschlagsrechnungen eines Kunden, die
// noch in keiner Schlussrechnung abgezogen sind.
func (s *Server) abschlaegeListe(w http.ResponseWriter, r *http.Request) {
	kunde := r.URL.Query().Get("kunde")
	alle, err := s.Speicher.Rechnungen()
	if err != nil {
		fehler(w, 500, err)
		return
	}
	l := []offenerAbschlag{}
	for i := len(alle) - 1; i >= 0; i-- {
		re := alle[i]
		if re.Art != modell.ArtAbschlag || re.KundeID != kunde || re.Summen == nil || re.Folgebeleg != "" ||
			re.Status == modell.StatusEntwurf || re.Status == modell.StatusStorniert {
			continue
		}
		l = append(l, offenerAbschlag{ID: re.ID, Nummer: re.Nummer, Datum: re.Datum, Brutto: re.Summen.Brutto, Steuern: re.Summen.Steuern})
	}
	antworte(w, 200, l)
}

// vermerkeAbschlaege hält an den Abschlagsrechnungen fest, in welcher
// Rechnung sie abgezogen wurden - damit sie kein zweites Mal abgezogen werden.
func (s *Server) vermerkeAbschlaege(fertig modell.Rechnung) {
	if fertig.Art != modell.ArtRechnung || len(fertig.Vorgaenger) == 0 {
		return
	}
	alle, err := s.Speicher.Rechnungen()
	if err != nil {
		return
	}
	for _, v := range fertig.Vorgaenger {
		for _, re := range alle {
			if re.Art == modell.ArtAbschlag && re.Nummer == v.Nummer && re.KundeID == fertig.KundeID && re.Folgebeleg == "" && re.Status != modell.StatusEntwurf {
				s.Speicher.Aendere(re.ID, func(x *modell.Rechnung) error {
					x.Folgebeleg = "abgezogen in Rechnung " + fertig.Nummer
					return nil
				})
			}
		}
	}
}

// ---------------------------------------------------------------------------
// Serienrechnungen

var abstaende = map[string][2]int{"woche": {0, 7}, "monat": {1, 0}, "quartal": {3, 0}, "halbjahr": {6, 0}, "jahr": {12, 0}}

// weiter rückt ein Datum um den Abstand der Serie vor.
func weiter(datum time.Time, abstand string) time.Time {
	a, ok := abstaende[abstand]
	if !ok {
		a = abstaende["monat"]
	}
	if a[0] == 0 {
		return datum.AddDate(0, 0, a[1])
	}
	// Monatsweise, ohne über das Monatsende hinauszuspringen (31.01. -> 28.02.).
	ziel := time.Date(datum.Year(), datum.Month()+time.Month(a[0]), 1, 0, 0, 0, 0, time.UTC)
	letzter := ziel.AddDate(0, 1, -1).Day()
	t := datum.Day()
	if t > letzter {
		t = letzter
	}
	return time.Date(ziel.Year(), ziel.Month(), t, 0, 0, 0, 0, time.UTC)
}

type serieMitStand struct {
	modell.Serie
	Faellig bool   `json:"faellig"`
	Kunde   string `json:"kunde"`
	Brutto  dec.D  `json:"brutto"`
}

func (s *Server) serienListe(w http.ResponseWriter, r *http.Request) {
	l, err := s.Speicher.Serien()
	if err != nil {
		fehler(w, 500, err)
		return
	}
	heute := time.Now().Format(tag)
	aus := []serieMitStand{}
	for _, x := range l {
		m := serieMitStand{Serie: x, Faellig: x.Aktiv && x.Naechste != "" && x.Naechste <= heute}
		if voll, _, err := s.belegFertig(x.Vorlage); err == nil {
			m.Kunde, m.Brutto = voll.Kaeufer.Name, voll.Summen.Brutto
		}
		aus = append(aus, m)
	}
	antworte(w, 200, aus)
}

func (s *Server) serieSpeichern(w http.ResponseWriter, r *http.Request) {
	var ein struct {
		modell.Serie
		AusRechnung string `json:"ausRechnung"` // die Vorlage aus dieser Rechnung nehmen
	}
	if err := liesJSON(r, &ein); err != nil {
		fehler(w, 400, err)
		return
	}
	x := ein.Serie
	if x.ID != "" {
		alle, err := s.Speicher.Serien()
		if err != nil {
			fehler(w, 500, err)
			return
		}
		for _, a := range alle {
			if a.ID == x.ID {
				x.Vorlage, x.Letzte = a.Vorlage, a.Letzte // die Vorlage ändert man über eine neue Serie
			}
		}
	}
	if ein.AusRechnung != "" {
		re, err := s.Speicher.Rechnung(ein.AusRechnung)
		if err != nil {
			fehler(w, 404, err)
			return
		}
		// Übernommen wird, was sich wiederholt - nicht, was zur einzelnen Lieferung gehört.
		x.Vorlage = modell.Rechnung{KundeID: re.KundeID, Lieferort: re.Lieferort, Positionen: re.Positionen, ZuAbschlaege: re.ZuAbschlaege,
			Einleitung: re.Einleitung, Schluss: re.Schluss, Zahlung: re.Zahlung, Kostenstelle: re.Kostenstelle, Kaeuferreferenz: re.Kaeuferreferenz}
		x.Vorlage.Zahlung.Verwendungszweck = ""
	}
	if x.Vorlage.KundeID == "" || len(x.Vorlage.Positionen) == 0 {
		fehler(w, 422, errors.New("die Serie braucht eine Rechnung als Vorlage"))
		return
	}
	if strings.TrimSpace(x.Name) == "" {
		fehler(w, 422, errors.New("bitte der Serie einen Namen geben"))
		return
	}
	if _, ok := abstaende[x.Abstand]; !ok {
		x.Abstand = "monat"
	}
	if _, err := time.Parse(tag, x.Naechste); err != nil {
		fehler(w, 422, errors.New("bitte das Datum der nächsten Rechnung eintragen"))
		return
	}
	gespeichert, err := s.Speicher.SpeichereSerie(x)
	if err != nil {
		fehler(w, 500, err)
		return
	}
	antworte(w, 200, gespeichert)
}

func (s *Server) serieLoeschen(w http.ResponseWriter, r *http.Request) {
	if err := s.Speicher.LoescheSerie(r.PathValue("id")); err != nil {
		fehler(w, 500, err)
		return
	}
	antworte(w, 200, map[string]bool{"ok": true})
}

// serienAnlegen legt für jede fällige Serie den Entwurf der nächsten
// Rechnung an und rückt die Serie vor. Mit einer Kennung in der Anfrage
// gilt das nur für diese Serie - dann auch, wenn sie noch nicht fällig ist.
func (s *Server) serienAnlegen(w http.ResponseWriter, r *http.Request) {
	var ein struct {
		ID string `json:"id"`
	}
	liesJSON(r, &ein)
	l, err := s.Speicher.Serien()
	if err != nil {
		fehler(w, 500, err)
		return
	}
	heute := time.Now().Format(tag)
	angelegt := []modell.Rechnung{}
	for _, x := range l {
		faellig := x.Aktiv && x.Naechste != "" && x.Naechste <= heute
		if (ein.ID == "" && !faellig) || (ein.ID != "" && ein.ID != x.ID) {
			continue
		}
		datum, err := time.Parse(tag, x.Naechste)
		if err != nil {
			datum = time.Now()
		}
		neu := x.Vorlage
		neu.ID, neu.Nummer, neu.Status, neu.Summen = "", "", "", nil
		neu.Datum, neu.Leistungsdatum, neu.Faellig = datum.Format(tag), datum.Format(tag), ""
		neu.Herkunft = "Serie " + x.Name
		gespeichert, err := s.Speicher.SpeichereEntwurf(neu)
		if err != nil {
			fehler(w, 500, err)
			return
		}
		angelegt = append(angelegt, gespeichert)
		x.Letzte, x.Naechste = datum.Format(tag), weiter(datum, x.Abstand).Format(tag)
		if _, err := s.Speicher.SpeichereSerie(x); err != nil {
			fehler(w, 500, err)
			return
		}
	}
	antworte(w, 200, angelegt)
}

// ---------------------------------------------------------------------------
// Auswertung

type auswertungMonat struct {
	Monat     string `json:"monat"` // JJJJ-MM
	Anzahl    int    `json:"anzahl"`
	Netto19   dec.D  `json:"netto19"`
	Steuer19  dec.D  `json:"steuer19"`
	Netto7    dec.D  `json:"netto7"`
	Steuer7   dec.D  `json:"steuer7"`
	NettoOhne dec.D  `json:"nettoOhne"` // ohne Umsatzsteuer oder mit anderem Satz
	SteuerAnd dec.D  `json:"steuerAndere"`
	Brutto    dec.D  `json:"brutto"`
}

type auswertungKunde struct {
	Kunde  string `json:"kunde"`
	Anzahl int    `json:"anzahl"`
	Netto  dec.D  `json:"netto"`
	Brutto dec.D  `json:"brutto"`
}

// auswertung fasst die fertigen Rechnungen eines Jahres zusammen: Umsätze je
// Monat nach Steuersatz, Umsatz je Kunde und die offenen Posten. Das ist
// eine Übersicht über die geschriebenen Rechnungen, keine Buchführung.
func (s *Server) auswertung(w http.ResponseWriter, r *http.Request) {
	jahr := r.URL.Query().Get("jahr")
	if len(jahr) != 4 {
		jahr = time.Now().Format("2006")
	}
	alle, err := s.Speicher.Rechnungen()
	if err != nil {
		fehler(w, 500, err)
		return
	}
	monate := make([]auswertungMonat, 12)
	for i := range monate {
		monate[i].Monat = fmt.Sprintf("%s-%02d", jahr, i+1)
	}
	var summe auswertungMonat
	summe.Monat = jahr
	kunden := map[string]*auswertungKunde{}
	jahre := map[string]bool{time.Now().Format("2006"): true}
	for _, re := range alle {
		if re.Status == modell.StatusEntwurf || re.Summen == nil || len(re.Datum) < 7 {
			continue
		}
		jahre[re.Datum[:4]] = true
		if re.Datum[:4] != jahr {
			continue
		}
		nr, _ := strconv.Atoi(re.Datum[5:7]) // nicht Sscan: das läse "09" als Oktalzahl
		if nr < 1 || nr > 12 {
			continue
		}
		vz := dec.Eins
		if re.Art == modell.ArtGutschrift {
			vz = -dec.Eins
		}
		m := &monate[nr-1]
		for _, ziel := range []*auswertungMonat{m, &summe} {
			ziel.Anzahl++
			ziel.Brutto += re.Summen.Brutto.Mal(vz, 2)
			for _, st := range re.Summen.Steuern {
				basis, betrag := st.Basis.Mal(vz, 2), st.Betrag.Mal(vz, 2)
				switch {
				case st.Kat == modell.KatNormal && st.Satz == dec.Int(19):
					ziel.Netto19, ziel.Steuer19 = ziel.Netto19+basis, ziel.Steuer19+betrag
				case st.Kat == modell.KatNormal && st.Satz == dec.Int(7):
					ziel.Netto7, ziel.Steuer7 = ziel.Netto7+basis, ziel.Steuer7+betrag
				default:
					ziel.NettoOhne, ziel.SteuerAnd = ziel.NettoOhne+basis, ziel.SteuerAnd+betrag
				}
			}
		}
		k := kunden[re.Kaeufer.Name]
		if k == nil {
			k = &auswertungKunde{Kunde: re.Kaeufer.Name}
			kunden[re.Kaeufer.Name] = k
		}
		k.Anzahl++
		k.Netto += re.Summen.Netto.Mal(vz, 2)
		k.Brutto += re.Summen.Brutto.Mal(vz, 2)
	}
	nachKunde := []auswertungKunde{}
	for _, k := range kunden {
		nachKunde = append(nachKunde, *k)
	}
	sort.Slice(nachKunde, func(i, j int) bool { return nachKunde[i].Brutto > nachKunde[j].Brutto })
	var jahresliste []string
	for j := range jahre {
		jahresliste = append(jahresliste, j)
	}
	sort.Sort(sort.Reverse(sort.StringSlice(jahresliste)))
	antworte(w, 200, map[string]any{"jahr": jahr, "jahre": jahresliste, "monate": monate, "summe": summe, "kunden": nachKunde,
		"offen": offenePosten(alle, time.Now())})
}
