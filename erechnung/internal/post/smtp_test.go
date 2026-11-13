package post

import (
	"bufio"
	"bytes"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/tls"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/base64"
	"math/big"
	"net"
	"strings"
	"sync"
	"testing"
	"time"

	"marktrechnung/internal/modell"
)

// probeserver ist ein kleiner Mailserver für Tests. Er versteht gerade so
// viel SMTP, wie zum Annehmen einer Nachricht nötig ist.
type probeserver struct {
	t          *testing.T
	horcher    net.Listener
	zertifikat tls.Certificate
	starttls   bool   // STARTTLS statt sofortiger Verschlüsselung
	verfahren  string // angebotene Anmeldeverfahren
	passwort   string

	mu        sync.Mutex
	von       string
	an        []string
	nachricht []byte
}

func neuerProbeserver(t *testing.T, starttls bool, verfahren string) *probeserver {
	schluessel, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	vorlage := &x509.Certificate{SerialNumber: big.NewInt(1), Subject: pkix.Name{CommonName: "localhost"},
		NotBefore: time.Now().Add(-time.Hour), NotAfter: time.Now().Add(time.Hour),
		DNSNames: []string{"localhost"}, KeyUsage: x509.KeyUsageDigitalSignature, ExtKeyUsage: []x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth},
		IsCA: true, BasicConstraintsValid: true}
	der, err := x509.CreateCertificate(rand.Reader, vorlage, vorlage, &schluessel.PublicKey, schluessel)
	if err != nil {
		t.Fatal(err)
	}
	zert, _ := x509.ParseCertificate(der)
	vertrauen = x509.NewCertPool()
	vertrauen.AddCert(zert)
	t.Cleanup(func() { vertrauen = nil })

	s := &probeserver{t: t, starttls: starttls, verfahren: verfahren, passwort: "richtig",
		zertifikat: tls.Certificate{Certificate: [][]byte{der}, PrivateKey: schluessel}}
	s.horcher, err = net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { s.horcher.Close() })
	go func() {
		for {
			verb, err := s.horcher.Accept()
			if err != nil {
				return
			}
			go s.bediene(verb)
		}
	}()
	return s
}

func (s *probeserver) einstellungen() modell.Postausgang {
	_, port, _ := net.SplitHostPort(s.horcher.Addr().String())
	var p int
	for _, c := range port {
		p = p*10 + int(c-'0')
	}
	sicherheit := "tls"
	if s.starttls {
		sicherheit = "starttls"
	}
	return modell.Postausgang{Server: "localhost", Port: p, Sicherheit: sicherheit, Absender: "maria@sonnenwiese.example", AbsenderName: "Hofladen"}
}

func (s *probeserver) bediene(verb net.Conn) {
	defer verb.Close()
	verb.SetDeadline(time.Now().Add(10 * time.Second))
	konf := &tls.Config{Certificates: []tls.Certificate{s.zertifikat}}
	sicher := false
	if !s.starttls {
		verb, sicher = tls.Server(verb, konf), true
	}
	lesen := bufio.NewReader(verb)
	sag := func(text string) { verb.Write([]byte(text + "\r\n")) }
	sag("220 probeserver bereit")
	for {
		zeile, err := lesen.ReadString('\n')
		if err != nil {
			return
		}
		zeile = strings.TrimRight(zeile, "\r\n")
		befehl := strings.ToUpper(zeile)
		switch {
		case strings.HasPrefix(befehl, "EHLO"):
			if sicher {
				sag("250-probeserver\r\n250 AUTH " + s.verfahren)
			} else {
				sag("250-probeserver\r\n250 STARTTLS")
			}
		case befehl == "STARTTLS":
			sag("220 los")
			verb, sicher = tls.Server(verb, konf), true
			lesen = bufio.NewReader(verb)
		case strings.HasPrefix(befehl, "AUTH PLAIN"):
			roh, _ := base64.StdEncoding.DecodeString(strings.TrimSpace(zeile[len("AUTH PLAIN"):]))
			teile := bytes.Split(roh, []byte{0})
			if !sicher || len(teile) != 3 || string(teile[2]) != s.passwort {
				sag("535 Anmeldung abgelehnt")
				continue
			}
			sag("235 angemeldet")
		case befehl == "AUTH LOGIN":
			sag("334 " + base64.StdEncoding.EncodeToString([]byte("Username:")))
			lesen.ReadString('\n')
			sag("334 " + base64.StdEncoding.EncodeToString([]byte("Password:")))
			pw, _ := lesen.ReadString('\n')
			klar, _ := base64.StdEncoding.DecodeString(strings.TrimSpace(pw))
			if string(klar) != s.passwort {
				sag("535 Anmeldung abgelehnt")
				continue
			}
			sag("235 angemeldet")
		case strings.HasPrefix(befehl, "MAIL FROM:"):
			s.mu.Lock()
			s.von = zeile[len("MAIL FROM:"):]
			s.mu.Unlock()
			sag("250 ok")
		case strings.HasPrefix(befehl, "RCPT TO:"):
			s.mu.Lock()
			s.an = append(s.an, strings.Trim(zeile[len("RCPT TO:"):], "<> "))
			s.mu.Unlock()
			sag("250 ok")
		case befehl == "DATA":
			sag("354 bitte")
			var b bytes.Buffer
			for {
				z, err := lesen.ReadString('\n')
				if err != nil {
					return
				}
				if z == ".\r\n" {
					break
				}
				b.WriteString(strings.TrimPrefix(z, "."))
			}
			s.mu.Lock()
			s.nachricht = b.Bytes()
			s.mu.Unlock()
			sag("250 angenommen")
		case befehl == "QUIT":
			sag("221 tschüss")
			return
		default:
			sag("500 unbekannt")
		}
	}
}

func TestSendeUeberProbeserver(t *testing.T) {
	anhang := bytes.Repeat([]byte{0x25, 0x50, 0x44, 0x46, 0x00, 0xff, '\n', '.'}, 3000)
	n := Nachricht{An: []string{"rechnung@markt.example"}, Blind: []string{"maria@sonnenwiese.example"},
		Betreff: "Rechnung RE-2026-0001", Text: "Guten Tag,\n.\nmit Punkt am Zeilenanfang.",
		Anhang: []Anhang{{Name: "Rechnung_RE-2026-0001.pdf", Mime: "application/pdf", Daten: anhang}}}

	faelle := []struct {
		name      string
		starttls  bool
		verfahren string
	}{{"SSL/TLS mit PLAIN", false, "PLAIN LOGIN"}, {"STARTTLS mit PLAIN", true, "PLAIN"}, {"SSL/TLS nur LOGIN", false, "LOGIN"}}
	for _, f := range faelle {
		t.Run(f.name, func(t *testing.T) {
			s := neuerProbeserver(t, f.starttls, f.verfahren)
			p := s.einstellungen()
			if err := Teste(p, "richtig"); err != nil {
				t.Fatalf("Teste: %v", err)
			}
			roh, err := Sende(n, p, "richtig")
			if err != nil {
				t.Fatalf("Sende: %v", err)
			}
			s.mu.Lock()
			defer s.mu.Unlock()
			if len(s.an) != 2 || s.an[0] != "rechnung@markt.example" || s.an[1] != "maria@sonnenwiese.example" {
				t.Errorf("Empfänger: %v", s.an)
			}
			if !bytes.Equal(s.nachricht, roh) {
				t.Error("die angekommene Nachricht weicht von der gesendeten ab")
			}
			if !bytes.Contains(s.nachricht, []byte(base64.StdEncoding.EncodeToString(anhang)[:60])) {
				t.Error("der Anhang fehlt")
			}
			// Falsches Passwort: verständliche Meldung, nichts wird gesendet.
			if err := Teste(p, "falsch"); err == nil || !strings.Contains(err.Error(), "Anmeldung") {
				t.Errorf("falsches Passwort: %v", err)
			}
		})
	}

	// Einem Server mit unbekanntem Zertifikat wird kein Passwort anvertraut.
	s := neuerProbeserver(t, false, "PLAIN")
	vertrauen = x509.NewCertPool()
	if err := Teste(s.einstellungen(), "richtig"); err == nil {
		t.Error("ein nicht vertrauenswürdiges Zertifikat wurde angenommen")
	}
	// Ein Server ohne Verschlüsselung wird abgelehnt.
	if err := Teste(modell.Postausgang{}, "x"); err == nil {
		t.Error("fehlende Einstellungen wurden nicht bemerkt")
	}
}
