package post

import (
	"bytes"
	"io"
	"mime"
	"mime/multipart"
	"net/mail"
	"strings"
	"testing"
	"time"

	"marktrechnung/internal/modell"
)

func TestAdressen(t *testing.T) {
	l, err := Adressen("a@markt.example; b@markt.example,\n c@markt.example")
	if err != nil || len(l) != 3 || l[2] != "c@markt.example" {
		t.Errorf("%v %v", l, err)
	}
	for _, s := range []string{"keine-adresse", "a@b", "a@@b.de"} {
		if _, err := Adressen(s); err == nil {
			t.Errorf("%q hätte abgelehnt werden müssen", s)
		}
	}
}

// Die gebaute E-Mail muss sich mit der Standardbibliothek wieder lesen
// lassen: Kopfzeilen, Text und Anhang unverändert.
func TestBaue(t *testing.T) {
	anhang := bytes.Repeat([]byte("%PDF-Beispiel äöü\n"), 500)
	n := Nachricht{An: []string{"rechnung@markt.example"}, Blind: []string{"geheim@sonnenwiese.example"},
		Betreff: "Rechnung RE-2026-0042 – Hofladen Sonnenwiese", Text: "Guten Tag,\n\nanbei die Rechnung.\n\nFreundliche Grüße",
		Anhang: []Anhang{{Name: "Rechnung_RE-2026-0042.pdf", Mime: "application/pdf", Daten: anhang}}}
	p := modell.Postausgang{Absender: "maria@sonnenwiese.example", AbsenderName: "Hofladen Sonnenwiese – Büro"}
	roh := Baue(n, p, time.Date(2026, 11, 13, 10, 0, 0, 0, time.UTC))
	if bytes.Contains(roh, []byte("geheim@")) {
		t.Error("die Blindkopie steht in der Nachricht")
	}
	m, err := mail.ReadMessage(bytes.NewReader(roh))
	if err != nil {
		t.Fatal(err)
	}
	dec := new(mime.WordDecoder)
	betreff, _ := dec.DecodeHeader(m.Header.Get("Subject"))
	if betreff != n.Betreff {
		t.Errorf("Betreff %q", betreff)
	}
	von, err := m.Header.AddressList("From")
	if err != nil || von[0].Address != p.Absender || von[0].Name != p.AbsenderName {
		t.Errorf("Absender %v %v", von, err)
	}
	_, param, err := mime.ParseMediaType(m.Header.Get("Content-Type"))
	if err != nil {
		t.Fatal(err)
	}
	teile := multipart.NewReader(m.Body, param["boundary"])
	text, err := teile.NextPart()
	if err != nil {
		t.Fatal(err)
	}
	inhalt, _ := io.ReadAll(text) // multipart dekodiert base64 nicht selbst
	if !strings.Contains(text.Header.Get("Content-Type"), "UTF-8") || len(inhalt) == 0 {
		t.Error("Textteil fehlt")
	}
	datei, err := teile.NextPart()
	if err != nil {
		t.Fatal(err)
	}
	if datei.FileName() != "Rechnung_RE-2026-0042.pdf" || !strings.HasPrefix(datei.Header.Get("Content-Type"), "application/pdf") {
		t.Errorf("Anhang: %q %q", datei.FileName(), datei.Header.Get("Content-Type"))
	}
	for _, z := range strings.Split(string(roh), "\r\n") {
		if len(z) > 998 {
			t.Error("Zeile länger als im Internetformat erlaubt")
		}
	}
}
