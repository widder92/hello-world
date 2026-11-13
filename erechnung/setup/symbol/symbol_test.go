package main

import (
	"bytes"
	"debug/pe"
	"encoding/binary"
	"os"
	"os/exec"
	"path/filepath"
	"testing"
)

// Die Ressourcendatei muss eine gültige Objektdatei sein, und nach dem
// Übersetzen müssen in der Windows-Programmdatei genau die Bilder der
// Symboldatei stecken.
func TestSymbolInProgrammdatei(t *testing.T) {
	ico, err := os.ReadFile("../marktrechnung.ico")
	if err != nil {
		t.Fatal(err)
	}
	bilder, err := liesIco(ico)
	if err != nil || len(bilder) < 4 {
		t.Fatalf("Symboldatei: %d Bilder, %v", len(bilder), err)
	}
	syso := baue(bilder)
	objekt, err := pe.NewFile(bytes.NewReader(syso))
	if err != nil {
		t.Fatal(err)
	}
	if len(objekt.Sections) != 1 || objekt.Sections[0].Name != ".rsrc" || len(objekt.Sections[0].Relocs) != len(bilder)+1 {
		t.Fatalf("Aufbau der Objektdatei: %+v", objekt.Sections)
	}
	eingecheckt, err := os.ReadFile("../../cmd/marktrechnung/symbol_windows_amd64.syso")
	if err != nil || !bytes.Equal(eingecheckt, syso) {
		t.Error("cmd/marktrechnung/symbol_windows_amd64.syso passt nicht zur Symboldatei - bitte neu erzeugen (siehe main.go)")
	}

	if testing.Short() {
		t.Skip("ohne Übersetzen der Programmdatei")
	}
	exe := filepath.Join(t.TempDir(), "Marktrechnung.exe")
	bau := exec.Command("go", "build", "-o", exe, "./cmd/marktrechnung")
	bau.Dir = "../.."
	bau.Env = append(os.Environ(), "GOOS=windows", "GOARCH=amd64", "CGO_ENABLED=0")
	if aus, err := bau.CombinedOutput(); err != nil {
		t.Fatalf("Übersetzen: %v\n%s", err, aus)
	}
	f, err := pe.Open(exe)
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	rs := f.Section(".rsrc")
	if rs == nil {
		t.Fatal("die Programmdatei hat keinen Ressourcen-Abschnitt")
	}
	d, _ := rs.Data()
	le := binary.LittleEndian
	eintraege := func(ort uint32) map[uint32]uint32 {
		m := map[uint32]uint32{}
		n := int(le.Uint16(d[ort+12:])) + int(le.Uint16(d[ort+14:]))
		for i := 0; i < n; i++ {
			e := d[int(ort)+16+8*i:]
			m[le.Uint32(e)] = le.Uint32(e[4:])
		}
		return m
	}
	daten := func(art, nr uint32) []byte {
		nummern := eintraege(eintraege(0)[art] &^ 0x80000000)
		for _, o := range eintraege(nummern[nr] &^ 0x80000000) {
			rva, groesse := le.Uint32(d[o:]), le.Uint32(d[o+4:])
			return d[rva-rs.VirtualAddress : rva-rs.VirtualAddress+groesse]
		}
		return nil
	}
	gruppe := daten(artSymbolgruppe, 1)
	if len(gruppe) < 6 || int(le.Uint16(gruppe[4:])) != len(bilder) {
		t.Fatalf("Symbolgruppe fehlt oder ist unvollständig (%d Bytes)", len(gruppe))
	}
	for i, b := range bilder {
		if !bytes.Equal(daten(artSymbol, uint32(i+1)), b.daten) {
			t.Errorf("Bild %d in der Programmdatei weicht von der Symboldatei ab", i+1)
		}
	}
}
