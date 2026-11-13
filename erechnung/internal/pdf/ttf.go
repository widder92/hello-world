package pdf

import (
	"encoding/binary"
	"errors"
	"sort"
)

// Schrift ist eine geladene TrueType-Schrift. Sie merkt sich, welche Zeichen
// benutzt wurden, damit nur diese ins PDF eingebettet werden.
type Schrift struct {
	daten    []byte
	tabellen map[string][2]uint32 // Name -> Anfang, Länge

	einheiten   int // Einheiten je Geviert
	bbox        [4]int
	oberlaenge  int
	unterlaenge int
	versalhoehe int
	glyphen     int
	breiten     []uint16
	zeichen     map[rune]uint16
	langesLoca  bool
	Name        string // PostScript-Name, z. B. "LiberationSans"

	benutzt map[uint16]rune
	ersatz  uint16 // Glyphe für Zeichen, die die Schrift nicht kennt
}

func u16(b []byte, o int) int    { return int(binary.BigEndian.Uint16(b[o:])) }
func i16(b []byte, o int) int    { return int(int16(binary.BigEndian.Uint16(b[o:]))) }
func u32(b []byte, o int) uint32 { return binary.BigEndian.Uint32(b[o:]) }

// LadeSchrift liest eine TrueType-Datei (.ttf mit glyf-Umrissen).
func LadeSchrift(daten []byte, name string) (*Schrift, error) {
	if len(daten) < 12 {
		return nil, errors.New("Schriftdatei zu kurz")
	}
	s := &Schrift{daten: daten, tabellen: map[string][2]uint32{}, zeichen: map[rune]uint16{},
		benutzt: map[uint16]rune{}, Name: name}
	n := u16(daten, 4)
	for i := 0; i < n; i++ {
		o := 12 + i*16
		if o+16 > len(daten) {
			return nil, errors.New("Tabellenverzeichnis der Schrift beschädigt")
		}
		anfang, laenge := u32(daten, o+8), u32(daten, o+12)
		if uint64(anfang)+uint64(laenge) > uint64(len(daten)) {
			return nil, errors.New("Schrifttabelle reicht über das Dateiende hinaus")
		}
		s.tabellen[string(daten[o:o+4])] = [2]uint32{anfang, laenge}
	}
	for _, t := range []string{"head", "hhea", "maxp", "hmtx", "cmap", "loca", "glyf"} {
		if _, ok := s.tabellen[t]; !ok {
			return nil, errors.New("Schrift ohne Tabelle " + t + " (nur TrueType-Schriften werden unterstützt)")
		}
	}
	head := s.tabelle("head")
	s.einheiten = u16(head, 18)
	for i := 0; i < 4; i++ {
		s.bbox[i] = i16(head, 36+2*i)
	}
	s.langesLoca = i16(head, 50) != 0
	hhea := s.tabelle("hhea")
	s.oberlaenge, s.unterlaenge = i16(hhea, 4), i16(hhea, 6)
	anzahlBreiten := u16(hhea, 34)
	s.glyphen = u16(s.tabelle("maxp"), 4)
	s.versalhoehe = s.oberlaenge * 7 / 10
	if os2 := s.tabelle("OS/2"); len(os2) >= 90 && u16(os2, 0) >= 2 {
		s.versalhoehe = i16(os2, 88)
	}
	hmtx := s.tabelle("hmtx")
	s.breiten = make([]uint16, s.glyphen)
	letzte := uint16(0)
	for g := 0; g < s.glyphen; g++ {
		if g < anzahlBreiten && g*4+2 <= len(hmtx) {
			letzte = uint16(u16(hmtx, g*4))
		}
		s.breiten[g] = letzte
	}
	if err := s.liesCmap(); err != nil {
		return nil, err
	}
	s.ersatz = s.zeichen['?']
	return s, nil
}

func (s *Schrift) tabelle(name string) []byte {
	t, ok := s.tabellen[name]
	if !ok {
		return nil
	}
	return s.daten[t[0] : t[0]+t[1]]
}

// liesCmap liest die Zuordnung von Zeichen zu Glyphen (Formate 4 und 12).
func (s *Schrift) liesCmap() error {
	cmap := s.tabelle("cmap")
	n := u16(cmap, 2)
	beste, guete := -1, 0
	for i := 0; i < n; i++ {
		o := 4 + i*8
		plattform, kodierung := u16(cmap, o), u16(cmap, o+2)
		g := 0
		switch {
		case plattform == 3 && kodierung == 10:
			g = 4
		case plattform == 0 && kodierung >= 4:
			g = 3
		case plattform == 3 && kodierung == 1:
			g = 2
		case plattform == 0:
			g = 1
		}
		if g > guete {
			guete, beste = g, int(u32(cmap, o+4))
		}
	}
	if beste < 0 {
		return errors.New("Schrift ohne Unicode-Zeichentabelle")
	}
	t := cmap[beste:]
	switch u16(t, 0) {
	case 4:
		segmente := u16(t, 6) / 2
		ende, anfang := 14, 16+segmente*2
		delta, bereich := anfang+segmente*2, anfang+segmente*4
		for i := 0; i < segmente; i++ {
			e, a := u16(t, ende+i*2), u16(t, anfang+i*2)
			d, ro := u16(t, delta+i*2), u16(t, bereich+i*2)
			for c := a; c <= e && c != 0xFFFF; c++ {
				var g int
				if ro == 0 {
					g = (c + d) & 0xFFFF
				} else {
					p := bereich + i*2 + ro + (c-a)*2
					if p+2 > len(t) {
						continue
					}
					g = u16(t, p)
					if g != 0 {
						g = (g + d) & 0xFFFF
					}
				}
				if g != 0 && g < s.glyphen {
					s.zeichen[rune(c)] = uint16(g)
				}
			}
		}
	case 12:
		gruppen := int(u32(t, 12))
		for i := 0; i < gruppen; i++ {
			o := 16 + i*12
			a, e, g := u32(t, o), u32(t, o+4), u32(t, o+8)
			for c := a; c <= e && c-a < 70000; c++ {
				if gi := g + c - a; gi != 0 && int(gi) < s.glyphen {
					s.zeichen[rune(c)] = uint16(gi)
				}
			}
		}
	default:
		return errors.New("Zeichentabelle der Schrift in unbekanntem Format")
	}
	return nil
}

// glyphe liefert die Glyphe zu einem Zeichen und merkt sie als benutzt vor.
func (s *Schrift) glyphe(r rune) uint16 {
	switch r {
	case '\t', ' ', ' ', ' ':
		r = ' '
	case '‑':
		r = '-'
	}
	g, ok := s.zeichen[r]
	if !ok {
		g, r = s.ersatz, '?'
	}
	if _, da := s.benutzt[g]; !da {
		s.benutzt[g] = r
	}
	return g
}

// breite1000 ist die Breite einer Glyphe in Tausendsteln der Schriftgröße.
func (s *Schrift) breite1000(g uint16) int {
	return (int(s.breiten[g])*1000 + s.einheiten/2) / s.einheiten
}

// Breite misst einen Text in Punkt.
func (s *Schrift) Breite(text string, groesse float64) float64 {
	summe := 0
	for _, r := range text {
		summe += int(s.breiten[s.glyphe(r)])
	}
	return float64(summe) * groesse / float64(s.einheiten)
}

// teilmenge baut eine verkleinerte Schriftdatei, die nur die benutzten
// Glyphen enthält. Die Glyphennummern bleiben gleich; nicht benutzte Glyphen
// werden zu leeren Einträgen.
func (s *Schrift) teilmenge() []byte {
	glyf, loca := s.tabelle("glyf"), s.tabelle("loca")
	ort := func(g int) (int, int) {
		if s.langesLoca {
			return int(u32(loca, g*4)), int(u32(loca, g*4+4))
		}
		return u16(loca, g*2) * 2, u16(loca, g*2+2) * 2
	}
	behalten := map[int]bool{0: true}
	var sammle func(g int, tiefe int)
	sammle = func(g int, tiefe int) {
		if g >= s.glyphen || behalten[g] && g != 0 || tiefe > 8 {
			return
		}
		behalten[g] = true
		a, e := ort(g)
		if e <= a || e > len(glyf) || e-a < 10 || i16(glyf, a) >= 0 {
			return
		}
		// Zusammengesetzte Glyphe: ihre Bestandteile werden auch gebraucht.
		p := a + 10
		for p+4 <= e {
			flags, teil := u16(glyf, p), u16(glyf, p+2)
			sammle(teil, tiefe+1)
			p += 4
			if flags&0x0001 != 0 {
				p += 4
			} else {
				p += 2
			}
			switch {
			case flags&0x0008 != 0:
				p += 2
			case flags&0x0040 != 0:
				p += 4
			case flags&0x0080 != 0:
				p += 8
			}
			if flags&0x0020 == 0 {
				break
			}
		}
	}
	for g := range s.benutzt {
		sammle(int(g), 0)
	}

	var neuGlyf []byte
	neuLoca := make([]byte, (s.glyphen+1)*4)
	for g := 0; g < s.glyphen; g++ {
		binary.BigEndian.PutUint32(neuLoca[g*4:], uint32(len(neuGlyf)))
		if behalten[g] {
			a, e := ort(g)
			if e > a && e <= len(glyf) {
				neuGlyf = append(neuGlyf, glyf[a:e]...)
				for len(neuGlyf)%4 != 0 {
					neuGlyf = append(neuGlyf, 0)
				}
			}
		}
	}
	binary.BigEndian.PutUint32(neuLoca[s.glyphen*4:], uint32(len(neuGlyf)))

	head := append([]byte(nil), s.tabelle("head")...)
	binary.BigEndian.PutUint32(head[8:], 0)  // Prüfsumme wird unten gesetzt
	binary.BigEndian.PutUint16(head[50:], 1) // langes loca-Format

	teile := map[string][]byte{"head": head, "glyf": neuGlyf, "loca": neuLoca}
	for _, t := range []string{"hhea", "maxp", "hmtx", "cvt ", "fpgm", "prep"} {
		if d := s.tabelle(t); d != nil {
			teile[t] = d
		}
	}
	namen := make([]string, 0, len(teile))
	for t := range teile {
		namen = append(namen, t)
	}
	sort.Strings(namen)

	n := len(namen)
	potenz, exponent := 1, 0
	for potenz*2 <= n {
		potenz *= 2
		exponent++
	}
	aus := make([]byte, 12+16*n)
	binary.BigEndian.PutUint32(aus[0:], 0x00010000)
	binary.BigEndian.PutUint16(aus[4:], uint16(n))
	binary.BigEndian.PutUint16(aus[6:], uint16(potenz*16))
	binary.BigEndian.PutUint16(aus[8:], uint16(exponent))
	binary.BigEndian.PutUint16(aus[10:], uint16(n*16-potenz*16))
	kopfHead := 0
	for i, t := range namen {
		d := teile[t]
		o := 12 + i*16
		copy(aus[o:], t)
		binary.BigEndian.PutUint32(aus[o+4:], pruefsumme(d))
		binary.BigEndian.PutUint32(aus[o+8:], uint32(len(aus)))
		binary.BigEndian.PutUint32(aus[o+12:], uint32(len(d)))
		if t == "head" {
			kopfHead = len(aus)
		}
		aus = append(aus, d...)
		for len(aus)%4 != 0 {
			aus = append(aus, 0)
		}
	}
	binary.BigEndian.PutUint32(aus[kopfHead+8:], 0xB1B0AFBA-pruefsumme(aus))
	return aus
}

func pruefsumme(d []byte) uint32 {
	var s uint32
	for i := 0; i+4 <= len(d); i += 4 {
		s += binary.BigEndian.Uint32(d[i:])
	}
	if r := len(d) % 4; r != 0 {
		var rest [4]byte
		copy(rest[:], d[len(d)-r:])
		s += binary.BigEndian.Uint32(rest[:])
	}
	return s
}
