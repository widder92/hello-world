// Package xmlbaum ist ein kleiner XML-Baum zum Schreiben und Lesen von
// E-Rechnungen. Beim Schreiben werden leere Elemente weggelassen, denn die
// Prüfregeln verbieten sie.
package xmlbaum

import (
	"bytes"
	"encoding/xml"
	"errors"
	"io"
	"strings"
)

// Attr ist ein Attribut.
type Attr struct{ Name, Wert string }

// Knoten ist ein XML-Element. Beim Schreiben trägt Name das Präfix
// ("ram:ID"), beim Lesen steht der Namensraum in Raum und Name ist der
// lokale Name.
type Knoten struct {
	Name   string
	Raum   string
	Attr   []Attr
	Kinder []*Knoten
	Text   string
}

// Neu legt ein Wurzelelement an.
func Neu(name string) *Knoten { return &Knoten{Name: name} }

// Kind hängt ein Kindelement an und gibt es zurück.
func (k *Knoten) Kind(name string) *Knoten {
	n := &Knoten{Name: name}
	k.Kinder = append(k.Kinder, n)
	return n
}

// T hängt ein Kindelement mit Text an. Ist der Text leer, entsteht beim
// Schreiben nichts.
func (k *Knoten) T(name, text string) *Knoten {
	n := k.Kind(name)
	n.Text = strings.TrimSpace(text)
	return n
}

// Roh hängt ein Kindelement an, dessen Text unverändert bleibt - auch ein
// Zeilenende am Schluss, das die XRechnung bei Skonto-Angaben verlangt.
func (k *Knoten) Roh(name, text string) *Knoten {
	n := k.Kind(name)
	if strings.TrimSpace(text) != "" {
		n.Text = text
	}
	return n
}

// A setzt ein Attribut und gibt den Knoten selbst zurück.
func (k *Knoten) A(name, wert string) *Knoten {
	if wert != "" {
		k.Attr = append(k.Attr, Attr{name, wert})
	}
	return k
}

func (k *Knoten) leer() bool {
	if k.Text != "" {
		return false
	}
	for _, c := range k.Kinder {
		if !c.leer() {
			return false
		}
	}
	return true
}

// Bytes gibt das Dokument als UTF-8 mit XML-Kopf aus.
func (k *Knoten) Bytes() []byte {
	var b bytes.Buffer
	b.WriteString("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n")
	k.schreibe(&b, 0)
	return b.Bytes()
}

func (k *Knoten) schreibe(b *bytes.Buffer, tiefe int) {
	if k.leer() {
		return
	}
	ein := strings.Repeat("  ", tiefe)
	b.WriteString(ein)
	b.WriteByte('<')
	b.WriteString(k.Name)
	for _, a := range k.Attr {
		b.WriteByte(' ')
		b.WriteString(a.Name)
		b.WriteString(`="`)
		maskiere(b, a.Wert, true)
		b.WriteByte('"')
	}
	b.WriteByte('>')
	if k.Text != "" {
		maskiere(b, k.Text, false)
	} else {
		b.WriteByte('\n')
		for _, c := range k.Kinder {
			c.schreibe(b, tiefe+1)
		}
		b.WriteString(ein)
	}
	b.WriteString("</")
	b.WriteString(k.Name)
	b.WriteString(">\n")
}

func maskiere(b *bytes.Buffer, s string, attribut bool) {
	for _, r := range s {
		switch {
		case r == '&':
			b.WriteString("&amp;")
		case r == '<':
			b.WriteString("&lt;")
		case r == '>':
			b.WriteString("&gt;")
		case r == '"' && attribut:
			b.WriteString("&quot;")
		case r == '\r':
			// Zeilenenden einheitlich als \n
		case r == '\n' && attribut:
			b.WriteString("&#10;")
		case r == '\n' || r == '\t':
			b.WriteRune(r)
		case r < 0x20 || r == 0xFFFE || r == 0xFFFF:
			// in XML 1.0 nicht erlaubte Steuerzeichen entfallen
		default:
			b.WriteRune(r)
		}
	}
}

// Lies liest ein XML-Dokument in einen Baum.
func Lies(daten []byte) (*Knoten, error) {
	d := xml.NewDecoder(bytes.NewReader(daten))
	d.Strict = true
	// E-Rechnungen sind fast immer UTF-8; andere Kodierungen mit reinen
	// Ein-Byte-Zeichen werden als Latin-1 gelesen.
	d.CharsetReader = func(charset string, input io.Reader) (io.Reader, error) {
		roh, err := io.ReadAll(input)
		if err != nil {
			return nil, err
		}
		runen := make([]rune, len(roh))
		for i, c := range roh {
			runen[i] = rune(c)
		}
		return strings.NewReader(string(runen)), nil
	}
	var stapel []*Knoten
	var wurzel *Knoten
	for {
		tok, err := d.Token()
		if err == io.EOF {
			break
		}
		if err != nil {
			return nil, err
		}
		switch t := tok.(type) {
		case xml.StartElement:
			n := &Knoten{Name: t.Name.Local, Raum: t.Name.Space}
			for _, a := range t.Attr {
				if a.Name.Space == "xmlns" || a.Name.Local == "xmlns" {
					continue
				}
				n.Attr = append(n.Attr, Attr{a.Name.Local, a.Value})
			}
			if len(stapel) > 0 {
				eltern := stapel[len(stapel)-1]
				eltern.Kinder = append(eltern.Kinder, n)
			} else {
				wurzel = n
			}
			stapel = append(stapel, n)
		case xml.EndElement:
			if len(stapel) > 0 {
				n := stapel[len(stapel)-1]
				n.Text = strings.TrimSpace(n.Text)
				stapel = stapel[:len(stapel)-1]
			}
		case xml.CharData:
			if len(stapel) > 0 {
				stapel[len(stapel)-1].Text += string(t)
			}
		}
	}
	if wurzel == nil {
		return nil, errors.New("die Datei enthält kein XML-Dokument")
	}
	return wurzel, nil
}

// Finde folgt einem Pfad lokaler Elementnamen und liefert das erste
// passende Element oder nil. Auf nil aufgerufen, liefert es nil.
func (k *Knoten) Finde(pfad ...string) *Knoten {
	n := k
	for _, name := range pfad {
		if n == nil {
			return nil
		}
		var treffer *Knoten
		for _, c := range n.Kinder {
			if c.Name == name {
				treffer = c
				break
			}
		}
		n = treffer
	}
	return n
}

// Alle liefert alle direkten Kinder mit dem Namen.
func (k *Knoten) Alle(name string) []*Knoten {
	if k == nil {
		return nil
	}
	var l []*Knoten
	for _, c := range k.Kinder {
		if c.Name == name {
			l = append(l, c)
		}
	}
	return l
}

// Wert liefert den Text des Elements am Pfad, oder "".
func (k *Knoten) Wert(pfad ...string) string {
	n := k.Finde(pfad...)
	if n == nil {
		return ""
	}
	return n.Text
}

// Attribut liefert den Wert eines Attributs, oder "".
func (k *Knoten) Attribut(name string) string {
	if k == nil {
		return ""
	}
	for _, a := range k.Attr {
		if a.Name == name {
			return a.Wert
		}
	}
	return ""
}

// LeereElemente sammelt die Pfade aller Elemente ohne Inhalt.
func (k *Knoten) LeereElemente() []string {
	var l []string
	var lauf func(n *Knoten, pfad string)
	lauf = func(n *Knoten, pfad string) {
		p := pfad + "/" + n.Name
		if n.Text == "" && len(n.Kinder) == 0 {
			l = append(l, p)
		}
		for _, c := range n.Kinder {
			lauf(c, p)
		}
	}
	lauf(k, "")
	return l
}
