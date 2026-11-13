// Paket assistent beantwortet Fragen zur Bedienung des Programms.
//
// Die Grundlage ist die Anleitung, die im Programm steckt (anleitung.json).
// Ohne weiteres Zutun sucht der Assistent darin - das geschieht in der
// Oberfläche, ohne Internet. Wer einen eigenen Zugangsschlüssel für Claude
// hinterlegt, bekommt zusätzlich frei formulierte Antworten: Dann geht die
// Frage zusammen mit der Anleitung an die Claude-Schnittstelle von Anthropic.
// Kunden- und Rechnungsdaten werden dabei nicht mitgeschickt.
package assistent

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"strings"
	"time"

	"github.com/anthropics/anthropic-sdk-go/v2"
	"github.com/anthropics/anthropic-sdk-go/v2/option"
)

// Modell ist das Claude-Modell, das die Fragen beantwortet.
const Modell = "claude-opus-5-5"

// Kapitel der Anleitung, so wie es in anleitung.json steht.
type Kapitel struct {
	ID       string   `json:"id"`
	Titel    string   `json:"titel"`
	Kurz     string   `json:"kurz"`
	Ziel     string   `json:"ziel"`
	Schritte []string `json:"schritte"`
	Hinweise []string `json:"hinweise"`
}

// Meldung erklärt eine Fehlermeldung, wie sie von Empfängern zurückkommt.
type Meldung struct {
	Regel string `json:"regel"`
	Text  string `json:"text"`
}

// Begriff erklärt ein Fachwort.
type Begriff struct {
	Wort string `json:"wort"`
	Text string `json:"text"`
}

// Anleitung ist der Inhalt von anleitung.json.
type Anleitung struct {
	Titel     string    `json:"titel"`
	Vorwort   string    `json:"vorwort"`
	Kapitel   []Kapitel `json:"kapitel"`
	Meldungen []Meldung `json:"meldungen"`
	Begriffe  []Begriff `json:"begriffe"`
}

// LiesAnleitung liest anleitung.json.
func LiesAnleitung(daten []byte) (Anleitung, error) {
	var a Anleitung
	err := json.Unmarshal(daten, &a)
	return a, err
}

// Text gibt die Anleitung als fortlaufenden Text wieder - für das Modell.
func (a Anleitung) Text() string {
	var b strings.Builder
	for i, k := range a.Kapitel {
		fmt.Fprintf(&b, "## Kapitel %d: %s\n%s\n", i+1, k.Titel, k.Kurz)
		if k.Ziel != "" {
			fmt.Fprintf(&b, "Seite im Programm: %s\n", k.Ziel)
		}
		for j, s := range k.Schritte {
			fmt.Fprintf(&b, "%d. %s\n", j+1, s)
		}
		for _, h := range k.Hinweise {
			fmt.Fprintf(&b, "Hinweis: %s\n", h)
		}
		b.WriteString("\n")
	}
	if len(a.Meldungen) > 0 {
		b.WriteString("## Was Fehlermeldungen der Empfänger bedeuten\n")
		for _, m := range a.Meldungen {
			fmt.Fprintf(&b, "%s: %s\n", m.Regel, m.Text)
		}
		b.WriteString("\n")
	}
	if len(a.Begriffe) > 0 {
		b.WriteString("## Begriffe\n")
		for _, w := range a.Begriffe {
			fmt.Fprintf(&b, "%s: %s\n", w.Wort, w.Text)
		}
		b.WriteString("\n")
	}
	return b.String()
}

// Nachricht ist ein Beitrag im Gespräch mit dem Assistenten.
type Nachricht struct {
	Rolle string `json:"rolle"` // "frage" oder "antwort"
	Text  string `json:"text"`
}

// Anfrage: die neue Frage mit dem bisherigen Gespräch und dem, was das
// Programm über die Lage weiß.
type Anfrage struct {
	Frage   string
	Verlauf []Nachricht
	Lage    string // z. B. geöffnete Seite, Stand der Einrichtung; keine Kundendaten
}

// Dienst spricht mit der Claude-Schnittstelle.
type Dienst struct {
	Schluessel string
	Basis      string // andere Adresse der Schnittstelle, nur für Tests
	Anleitung  Anleitung
	// Wissen ist weiterer Nachschlagetext, der sich nicht ändert, etwa die
	// Vorgaben der Handelsunternehmen.
	Wissen string
}

// client nimmt nur, was im Programm hinterlegt ist – nichts aus der Umgebung
// des Rechners (dort gesetzte Schlüssel oder Adressen bleiben außen vor).
func (d Dienst) client() anthropic.Client {
	opt := []option.RequestOption{option.WithoutEnvironmentDefaults(), option.WithAPIKey(d.Schluessel), option.WithMaxRetries(1)}
	if d.Basis != "" {
		opt = append(opt, option.WithBaseURL(d.Basis))
	}
	return anthropic.NewClient(opt...)
}

// auftrag ist der gleichbleibende Teil der Anweisung: Rolle, Regeln und die
// ganze Anleitung. Er steht vorn und ändert sich nicht, damit die
// Schnittstelle ihn zwischenspeichern kann.
func (d Dienst) auftrag() string {
	return `Du bist der Hilfe-Assistent im Programm „Marktrechnung“. Mit dem Programm schreibt ein kleiner Betrieb Rechnungen an den Lebensmittelhandel (REWE, EDEKA) und verschickt sie als E-Rechnung. Wer dich fragt, sitzt gerade vor dem Programm und ist kein Computerfachmensch.

So hilfst du am besten:
- Antworte auf Deutsch, in der Sie-Form, freundlich und kurz. Wenn etwas zu tun ist, gib nummerierte Schritte und nenne Menüpunkte und Knöpfe genau so, wie sie in der Anleitung heißen, in „Anführungszeichen“.
- Stütze dich auf die Anleitung unten. Steht etwas nicht darin, sag das offen, statt zu raten, und nenne den nächstliegenden Weg (zum Beispiel die Seite „Hilfe“ im Programm oder die Nachfrage beim Kunden).
- Zu einer Seite im Programm kannst du verweisen, indem du [[#/pfad|Beschriftung]] schreibst, zum Beispiel [[#/kunden|Kunden öffnen]]. Verwende dafür nur Pfade, die in der Anleitung als „Seite im Programm“ stehen.
- Steuerliche und rechtliche Fragen (Steuersätze im Einzelfall, Buchungen, Fristen) beantwortest du nur allgemein und verweist für die Entscheidung auf die Steuerkanzlei.
- Du siehst weder Kunden noch Rechnungen. Du kannst im Programm nichts selbst anklicken oder ändern; du erklärst, wie es geht.
- Schreibe gewöhnlichen Text ohne Überschriften, Tabellen oder Sternchen.

# Anleitung zum Programm

` + d.Anleitung.Text() + d.Wissen
}

// ErrAbgelehnt: Die Schnittstelle hat die Frage aus Sicherheitsgründen
// nicht beantwortet.
var ErrAbgelehnt = errors.New("diese Frage kann der Assistent nicht beantworten. Bitte anders formulieren oder in der Anleitung nachsehen")

// Antworte schickt die Frage an Claude und liefert die Antwort als Text.
func (d Dienst) Antworte(ctx context.Context, a Anfrage) (string, error) {
	if strings.TrimSpace(d.Schluessel) == "" {
		return "", errors.New("für den KI-Assistenten ist kein Zugangsschlüssel hinterlegt")
	}
	frage := strings.TrimSpace(a.Frage)
	if frage == "" {
		return "", errors.New("bitte eine Frage eintragen")
	}
	// Das Gespräch: Frage und Antwort im Wechsel, beginnend mit einer Frage.
	var nachrichten []anthropic.BetaMessageParam
	for _, n := range a.Verlauf {
		text := strings.TrimSpace(n.Text)
		if text == "" {
			continue
		}
		block := anthropic.NewBetaTextBlock(text)
		if n.Rolle == "antwort" {
			if len(nachrichten) == 0 {
				continue
			}
			nachrichten = append(nachrichten, anthropic.BetaMessageParam{OfAssistant: &anthropic.BetaAssistantMessageParam{Content: []anthropic.BetaContentBlockParamUnion{block}}})
		} else {
			nachrichten = append(nachrichten, anthropic.NewBetaUserMessage(block))
		}
	}
	nachrichten = append(nachrichten, anthropic.NewBetaUserMessage(anthropic.NewBetaTextBlock(frage)))

	// Vorn der gleichbleibende Auftrag (zwischengespeichert), dahinter die
	// Lage, die sich von Frage zu Frage ändert.
	system := []anthropic.BetaTextBlockParam{{Text: d.auftrag(), CacheControl: anthropic.NewBetaCacheControlEphemeralParam()}}
	if lage := strings.TrimSpace(a.Lage); lage != "" {
		system = append(system, anthropic.BetaTextBlockParam{Text: "Lage im Programm, so wie sie das Programm selbst meldet:\n" + lage})
	}

	ctx, abbruch := context.WithTimeout(ctx, 2*time.Minute)
	defer abbruch()
	client := d.client()
	antwort, err := client.Beta.Messages.New(ctx, anthropic.BetaMessageNewParams{
		Model:     Modell,
		MaxTokens: 16000,
		System:    system,
		Messages:  nachrichten,
		// Fragen zur Bedienung brauchen kein langes Nachdenken.
		OutputConfig: anthropic.BetaOutputConfigParam{Effort: anthropic.BetaOutputConfigEffortLow},
		// Lehnt das Modell eine Frage aus Sicherheitsgründen ab, darf die
		// Schnittstelle sie von einem Ersatzmodell beantworten lassen.
		Betas:     []anthropic.AnthropicBeta{anthropic.AnthropicBetaServerSideFallback2026_07_01},
		Fallbacks: anthropic.BetaFallbacksParamOfDefault(),
	})
	if err != nil {
		return "", verstaendlich(err)
	}
	if antwort.StopReason == anthropic.BetaStopReasonRefusal {
		return "", ErrAbgelehnt
	}
	var text strings.Builder
	for _, block := range antwort.Content {
		if t, ok := block.AsAny().(anthropic.BetaTextBlock); ok {
			text.WriteString(t.Text)
		}
	}
	aus := strings.TrimSpace(text.String())
	if aus == "" {
		return "", errors.New("der Assistent hat keine Antwort geliefert. Bitte noch einmal versuchen")
	}
	if antwort.StopReason == anthropic.BetaStopReasonMaxTokens {
		aus += "\n\n(Die Antwort wurde wegen ihrer Länge abgeschnitten.)"
	}
	return aus, nil
}

// PruefeSchluessel fragt die Schnittstelle, ob sie den Schlüssel annimmt.
// Das kostet nichts: Es wird nur das Modell nachgeschlagen.
func (d Dienst) PruefeSchluessel(ctx context.Context) error {
	ctx, abbruch := context.WithTimeout(ctx, 30*time.Second)
	defer abbruch()
	client := d.client()
	if _, err := client.Models.Get(ctx, Modell, anthropic.ModelGetParams{}); err != nil {
		return verstaendlich(err)
	}
	return nil
}

// verstaendlich übersetzt Fehler der Schnittstelle in Sätze, mit denen man
// etwas anfangen kann.
func verstaendlich(err error) error {
	var fehler *anthropic.Error
	if errors.As(err, &fehler) {
		switch fehler.StatusCode {
		case 401:
			return errors.New("der Zugangsschlüssel wird nicht angenommen. Bitte unter Einstellungen prüfen, ob er vollständig eingetragen ist")
		case 403:
			return errors.New("mit diesem Zugangsschlüssel ist die Nutzung nicht erlaubt. Bitte in der Claude Console die Berechtigungen des Schlüssels prüfen")
		case 404:
			return errors.New("das Modell ist für diesen Zugangsschlüssel nicht verfügbar")
		case 429:
			return errors.New("im Augenblick sind zu viele Anfragen unterwegs oder das Guthaben ist aufgebraucht. Bitte kurz warten oder in der Claude Console das Guthaben prüfen")
		case 400:
			return fmt.Errorf("die Schnittstelle hat die Anfrage abgelehnt (%s)", strings.TrimSpace(fehler.Error()))
		}
		if fehler.StatusCode >= 500 {
			return errors.New("der KI-Dienst ist gerade nicht erreichbar oder überlastet. Bitte später noch einmal versuchen")
		}
		return fmt.Errorf("der KI-Dienst meldet einen Fehler (%d)", fehler.StatusCode)
	}
	if errors.Is(err, context.DeadlineExceeded) {
		return errors.New("der KI-Dienst hat nicht rechtzeitig geantwortet. Bitte noch einmal versuchen")
	}
	return errors.New("keine Verbindung zum KI-Dienst. Besteht eine Verbindung zum Internet?")
}
