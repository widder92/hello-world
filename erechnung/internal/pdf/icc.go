package pdf

import (
	"encoding/binary"
	"math"
)

// srgbProfil baut ein kleines Farbprofil für sRGB (ICC Version 2.1). PDF/A
// verlangt ein Profil, das sagt, wie die Farbwerte im Dokument gemeint sind.
// Das Profil wird hier errechnet statt als fremde Datei mitgeliefert.
func srgbProfil() []byte {
	s15 := func(f float64) []byte {
		b := make([]byte, 4)
		binary.BigEndian.PutUint32(b, uint32(int32(math.Round(f*65536))))
		return b
	}
	xyz := func(x, y, z float64) []byte {
		b := append([]byte("XYZ \x00\x00\x00\x00"), s15(x)...)
		b = append(b, s15(y)...)
		return append(b, s15(z)...)
	}
	beschreibung := func(t string) []byte {
		b := []byte("desc\x00\x00\x00\x00")
		n := make([]byte, 4)
		binary.BigEndian.PutUint32(n, uint32(len(t)+1))
		b = append(b, n...)
		b = append(b, t...)
		b = append(b, 0)
		// Unicode- und ScriptCode-Teil bleiben leer.
		return append(b, make([]byte, 4+4+2+1+67)...)
	}
	// Gammakurve 2,2 als Näherung der sRGB-Kennlinie (u8Fixed8: 2,2 = 0x0233).
	kurve := []byte("curv\x00\x00\x00\x00\x00\x00\x00\x01\x02\x33\x00\x00")
	recht := append([]byte("text\x00\x00\x00\x00"), "Frei verwendbar (CC0)\x00"...)

	type marke struct {
		name  string
		daten []byte
	}
	// Farbwerte der sRGB-Grundfarben, auf die Normlichtart D50 umgerechnet.
	marken := []marke{
		{"desc", beschreibung("sRGB")},
		{"cprt", recht},
		{"wtpt", xyz(0.9642, 1.0, 0.8249)},
		{"rXYZ", xyz(0.43607, 0.22249, 0.01392)},
		{"gXYZ", xyz(0.38515, 0.71687, 0.09708)},
		{"bXYZ", xyz(0.14307, 0.06061, 0.71410)},
		{"rTRC", kurve},
		{"gTRC", nil}, // teilen sich die Daten mit rTRC
		{"bTRC", nil},
	}

	kopf := make([]byte, 128)
	copy(kopf[4:], "none")                           // bevorzugtes Farbmodul
	binary.BigEndian.PutUint32(kopf[8:], 0x02100000) // Version 2.1
	copy(kopf[12:], "mntrRGB XYZ ")                  // Monitorprofil, RGB, Verbindungsraum XYZ
	binary.BigEndian.PutUint16(kopf[24:], 2026)      // Erstellungsdatum
	binary.BigEndian.PutUint16(kopf[26:], 1)
	binary.BigEndian.PutUint16(kopf[28:], 1)
	copy(kopf[36:], "acsp")
	copy(kopf[68:], s15(0.9642)) // Beleuchtung des Verbindungsraums: D50
	copy(kopf[72:], s15(1.0))
	copy(kopf[76:], s15(0.8249))

	verzeichnis := make([]byte, 4+12*len(marken))
	binary.BigEndian.PutUint32(verzeichnis, uint32(len(marken)))
	ort := 128 + len(verzeichnis)
	var daten []byte
	kurveOrt, kurveLaenge := 0, 0
	for i, m := range marken {
		e := verzeichnis[4+12*i:]
		copy(e, m.name)
		if m.daten == nil {
			binary.BigEndian.PutUint32(e[4:], uint32(kurveOrt))
			binary.BigEndian.PutUint32(e[8:], uint32(kurveLaenge))
			continue
		}
		if m.name == "rTRC" {
			kurveOrt, kurveLaenge = ort+len(daten), len(m.daten)
		}
		binary.BigEndian.PutUint32(e[4:], uint32(ort+len(daten)))
		binary.BigEndian.PutUint32(e[8:], uint32(len(m.daten)))
		daten = append(daten, m.daten...)
		for len(daten)%4 != 0 {
			daten = append(daten, 0)
		}
	}
	aus := append(kopf, verzeichnis...)
	aus = append(aus, daten...)
	binary.BigEndian.PutUint32(aus[0:], uint32(len(aus)))
	return aus
}
