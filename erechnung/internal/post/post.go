// Package post verschickt Rechnungen per E-Mail über das eigene Postfach
// (SMTP). Die Verbindung ist immer verschlüsselt; ohne Verschlüsselung wird
// kein Passwort übertragen.
package post

import (
	"bytes"
	"crypto/rand"
	"crypto/tls"
	"encoding/base64"
	"encoding/hex"
	"errors"
	"fmt"
	"mime"
	"net"
	"net/mail"
	"net/smtp"
	"strings"
	"time"

	"marktrechnung/internal/modell"
)

// Anhang ist eine Datei an der E-Mail.
type Anhang struct {
	Name  string
	Mime  string
	Daten []byte
}

// Nachricht ist eine E-Mail.
type Nachricht struct {
	An      []string
	Kopie   []string
	Blind   []string
	Betreff string
	Text    string
	Anhang  []Anhang
}

// Adressen zerlegt eine Eingabe wie "a@x.de, b@y.de" und prüft jede Adresse.
func Adressen(eingabe string) ([]string, error) {
	var l []string
	for _, teil := range strings.FieldsFunc(eingabe, func(r rune) bool { return r == ',' || r == ';' || r == ' ' || r == '\n' }) {
		a, err := mail.ParseAddress(teil)
		if err != nil || !strings.Contains(a.Address, ".") {
			return nil, fmt.Errorf("„%s“ ist keine gültige E-Mail-Adresse", teil)
		}
		l = append(l, a.Address)
	}
	return l, nil
}

func kopfwort(s string) string {
	for _, r := range s {
		if r > 126 || r < 32 {
			return mime.BEncoding.Encode("UTF-8", s)
		}
	}
	return s
}

func zeilen76(b *bytes.Buffer, daten []byte) {
	s := base64.StdEncoding.EncodeToString(daten)
	for len(s) > 76 {
		b.WriteString(s[:76])
		b.WriteString("\r\n")
		s = s[76:]
	}
	b.WriteString(s)
	b.WriteString("\r\n")
}

// Baue setzt die E-Mail im Internetformat zusammen.
func Baue(n Nachricht, p modell.Postausgang, zeit time.Time) []byte {
	zufall := make([]byte, 12)
	rand.Read(zufall)
	marke := hex.EncodeToString(zufall)
	domain := "localhost"
	if _, d, ok := strings.Cut(p.Absender, "@"); ok {
		domain = d
	}
	absender := (&mail.Address{Name: p.AbsenderName, Address: p.Absender}).String()

	var b bytes.Buffer
	kopf := func(name, wert string) {
		if wert != "" {
			b.WriteString(name + ": " + wert + "\r\n")
		}
	}
	kopf("From", absender)
	kopf("To", strings.Join(n.An, ", "))
	kopf("Cc", strings.Join(n.Kopie, ", "))
	kopf("Subject", kopfwort(n.Betreff))
	kopf("Date", zeit.Format(time.RFC1123Z))
	kopf("Message-ID", "<"+marke+"@"+domain+">")
	kopf("MIME-Version", "1.0")
	kopf("X-Mailer", "Marktrechnung")
	grenze := "=_marktrechnung_" + marke
	kopf("Content-Type", `multipart/mixed; boundary="`+grenze+`"`)
	b.WriteString("\r\n")

	b.WriteString("--" + grenze + "\r\n")
	b.WriteString("Content-Type: text/plain; charset=UTF-8\r\nContent-Transfer-Encoding: base64\r\n\r\n")
	text := strings.ReplaceAll(strings.ReplaceAll(n.Text, "\r\n", "\n"), "\n", "\r\n")
	zeilen76(&b, []byte(text))
	for _, a := range n.Anhang {
		art := a.Mime
		if art == "" {
			art = "application/octet-stream"
		}
		name := mime.FormatMediaType("x", map[string]string{"name": a.Name})
		name = strings.TrimPrefix(name, "x; ")
		b.WriteString("--" + grenze + "\r\n")
		b.WriteString("Content-Type: " + art + "; " + name + "\r\n")
		b.WriteString("Content-Transfer-Encoding: base64\r\n")
		b.WriteString("Content-Disposition: attachment; " + strings.Replace(name, "name", "filename", 1) + "\r\n\r\n")
		zeilen76(&b, a.Daten)
	}
	b.WriteString("--" + grenze + "--\r\n")
	return b.Bytes()
}

// loginAuth ist das Anmeldeverfahren "LOGIN", das manche Anbieter statt
// "PLAIN" verlangen.
type loginAuth struct{ benutzer, passwort string }

func (a *loginAuth) Start(server *smtp.ServerInfo) (string, []byte, error) {
	if !server.TLS {
		return "", nil, errors.New("unverschlüsselte Verbindung")
	}
	return "LOGIN", nil, nil
}

func (a *loginAuth) Next(vomServer []byte, mehr bool) ([]byte, error) {
	if !mehr {
		return nil, nil
	}
	frage := strings.ToLower(string(vomServer))
	if strings.Contains(frage, "user") {
		return []byte(a.benutzer), nil
	}
	if strings.Contains(frage, "pass") {
		return []byte(a.passwort), nil
	}
	return nil, errors.New("unerwartete Frage des Mailservers: " + string(vomServer))
}

// Verbinde baut die verschlüsselte, angemeldete Verbindung auf.
func verbinde(p modell.Postausgang, passwort string) (*smtp.Client, error) {
	if p.Server == "" || p.Absender == "" {
		return nil, errors.New("der E-Mail-Versand ist noch nicht eingerichtet (Einstellungen → E-Mail-Versand)")
	}
	port := p.Port
	if port == 0 {
		port = 465
	}
	adresse := net.JoinHostPort(p.Server, fmt.Sprint(port))
	tlsConf := &tls.Config{ServerName: p.Server, MinVersion: tls.VersionTLS12}
	waehler := &net.Dialer{Timeout: 20 * time.Second}
	var c *smtp.Client
	if p.Sicherheit == "starttls" {
		verb, err := waehler.Dial("tcp", adresse)
		if err != nil {
			return nil, fmt.Errorf("der Mailserver %s ist nicht erreichbar: %w", adresse, err)
		}
		verb.SetDeadline(time.Now().Add(90 * time.Second))
		if c, err = smtp.NewClient(verb, p.Server); err != nil {
			verb.Close()
			return nil, fmt.Errorf("der Mailserver antwortet nicht wie erwartet: %w", err)
		}
		if ok, _ := c.Extension("STARTTLS"); !ok {
			c.Close()
			return nil, errors.New("der Mailserver bietet keine Verschlüsselung an (STARTTLS). Bitte Port 465 mit „SSL/TLS“ versuchen")
		}
		if err = c.StartTLS(tlsConf); err != nil {
			c.Close()
			return nil, fmt.Errorf("die Verschlüsselung der Verbindung ist fehlgeschlagen: %w", err)
		}
	} else {
		verb, err := tls.DialWithDialer(waehler, "tcp", adresse, tlsConf)
		if err != nil {
			return nil, fmt.Errorf("keine verschlüsselte Verbindung zum Mailserver %s: %w. Stimmen Server und Port? (Port 465 für SSL/TLS, 587 für STARTTLS)", adresse, err)
		}
		verb.SetDeadline(time.Now().Add(90 * time.Second))
		if c, err = smtp.NewClient(verb, p.Server); err != nil {
			verb.Close()
			return nil, fmt.Errorf("der Mailserver antwortet nicht wie erwartet: %w", err)
		}
	}
	benutzer := p.Benutzer
	if benutzer == "" {
		benutzer = p.Absender
	}
	if passwort == "" {
		c.Close()
		return nil, errors.New("das E-Mail-Passwort fehlt (Einstellungen → E-Mail-Versand)")
	}
	var auth smtp.Auth = smtp.PlainAuth("", benutzer, passwort, p.Server)
	if _, verfahren := c.Extension("AUTH"); !strings.Contains(verfahren, "PLAIN") && strings.Contains(verfahren, "LOGIN") {
		auth = &loginAuth{benutzer, passwort}
	}
	if err := c.Auth(auth); err != nil {
		c.Close()
		return nil, fmt.Errorf("die Anmeldung am Mailserver wurde abgelehnt: %w. Stimmen Benutzername und Passwort? Viele Anbieter (Gmail, GMX, Web.de, iCloud) verlangen ein eigenes App-Passwort oder die Freischaltung von „SMTP/externen Programmen“ in den Einstellungen des Postfachs", err)
	}
	return c, nil
}

// Teste prüft nur, ob Verbindung und Anmeldung klappen.
func Teste(p modell.Postausgang, passwort string) error {
	c, err := verbinde(p, passwort)
	if err != nil {
		return err
	}
	return c.Quit()
}

// Sende verschickt die Nachricht und liefert die gesendete E-Mail zurück.
func Sende(n Nachricht, p modell.Postausgang, passwort string) ([]byte, error) {
	if len(n.An) == 0 {
		return nil, errors.New("es ist kein Empfänger angegeben")
	}
	roh := Baue(n, p, time.Now())
	c, err := verbinde(p, passwort)
	if err != nil {
		return nil, err
	}
	defer c.Close()
	if err := c.Mail(p.Absender); err != nil {
		return nil, fmt.Errorf("der Mailserver lehnt die Absenderadresse ab: %w", err)
	}
	empfaenger := append(append(append([]string{}, n.An...), n.Kopie...), n.Blind...)
	for _, e := range empfaenger {
		if err := c.Rcpt(e); err != nil {
			return nil, fmt.Errorf("der Mailserver lehnt den Empfänger %s ab: %w", e, err)
		}
	}
	w, err := c.Data()
	if err != nil {
		return nil, fmt.Errorf("der Mailserver nimmt die Nachricht nicht an: %w", err)
	}
	if _, err = w.Write(roh); err == nil {
		err = w.Close()
	}
	if err != nil {
		return nil, fmt.Errorf("die Übertragung der Nachricht ist fehlgeschlagen: %w", err)
	}
	c.Quit()
	return roh, nil
}
