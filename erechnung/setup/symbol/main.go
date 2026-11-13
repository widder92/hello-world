// Das Hilfsprogramm symbol macht aus einer Symboldatei (.ico) eine
// Ressourcendatei (.syso), die der Go-Übersetzer in die Windows-Programmdatei
// einbindet. So zeigt Marktrechnung.exe im Explorer ihr eigenes Symbol.
//
// Aufruf, wenn sich das Symbol ändert:
//
//	go run ./setup/symbol setup/marktrechnung.ico cmd/marktrechnung/symbol_windows_amd64.syso
//
// Die Ausgabe ist eine Objektdatei im Format COFF mit einem einzigen
// Abschnitt ".rsrc". Darin steht das Verzeichnis der Ressourcen in drei
// Ebenen (Art, Nummer, Sprache), danach die Bilddaten.
package main

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"os"
)

const (
	artSymbol       = 3      // RT_ICON: ein einzelnes Bild
	artSymbolgruppe = 14     // RT_GROUP_ICON: die Liste der Bilder
	sprache         = 0x0409 // Kennung der Sprache; für Symbole ohne Belang
	maschineAMD64   = 0x8664
	verweisRVA      = 3 // IMAGE_REL_AMD64_ADDR32NB: Adresse relativ zum Programmanfang
)

type bild struct {
	breite, hoehe, farben, reserviert byte
	ebenen, bits                      uint16
	daten                             []byte
}

func main() {
	if len(os.Args) != 3 {
		fmt.Fprintln(os.Stderr, "Aufruf: symbol EINGABE.ico AUSGABE.syso")
		os.Exit(2)
	}
	ico, err := os.ReadFile(os.Args[1])
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	bilder, err := liesIco(ico)
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	if err := os.WriteFile(os.Args[2], baue(bilder), 0o644); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	fmt.Printf("%s: %d Bilder\n", os.Args[2], len(bilder))
}

func liesIco(d []byte) ([]bild, error) {
	if len(d) < 6 || binary.LittleEndian.Uint16(d[2:]) != 1 {
		return nil, fmt.Errorf("keine Symboldatei")
	}
	n := int(binary.LittleEndian.Uint16(d[4:]))
	var l []bild
	for i := 0; i < n; i++ {
		e := d[6+16*i:]
		groesse, ort := binary.LittleEndian.Uint32(e[8:]), binary.LittleEndian.Uint32(e[12:])
		if int(ort+groesse) > len(d) {
			return nil, fmt.Errorf("Symboldatei beschädigt")
		}
		l = append(l, bild{breite: e[0], hoehe: e[1], farben: e[2], reserviert: e[3],
			ebenen: binary.LittleEndian.Uint16(e[4:]), bits: binary.LittleEndian.Uint16(e[6:]), daten: d[ort : ort+groesse]})
	}
	return l, nil
}

func baue(bilder []bild) []byte {
	n := len(bilder)
	le := binary.LittleEndian

	// Die Symbolgruppe: dieselbe Liste wie in der .ico-Datei, nur verweist
	// jeder Eintrag statt auf einen Ort in der Datei auf eine Nummer.
	var gruppe bytes.Buffer
	binary.Write(&gruppe, le, []uint16{0, 1, uint16(n)})
	for i, b := range bilder {
		gruppe.Write([]byte{b.breite, b.hoehe, b.farben, b.reserviert})
		binary.Write(&gruppe, le, []uint16{b.ebenen, b.bits})
		binary.Write(&gruppe, le, uint32(len(b.daten)))
		binary.Write(&gruppe, le, uint16(i+1))
	}
	inhalte := make([][]byte, 0, n+1)
	for _, b := range bilder {
		inhalte = append(inhalte, b.daten)
	}
	inhalte = append(inhalte, gruppe.Bytes())

	// Aufbau des Abschnitts: Wurzel, zwei Verzeichnisse der zweiten Ebene,
	// je Ressource ein Verzeichnis der dritten Ebene, die Dateneinträge und
	// schließlich die Daten selbst.
	const kopf, eintrag, datenEintrag = 16, 8, 16
	ortSymbole := kopf + 2*eintrag
	ortGruppe := ortSymbole + kopf + n*eintrag
	ortSprachen := ortGruppe + kopf + eintrag
	ortEintraege := ortSprachen + (n+1)*(kopf+eintrag)
	ortDaten := ortEintraege + (n+1)*datenEintrag

	var a bytes.Buffer
	verzeichnis := func(anzahl int) { // Kopf eines Verzeichnisses mit nummerierten Einträgen
		a.Write(make([]byte, 12))
		binary.Write(&a, le, []uint16{0, uint16(anzahl)})
	}
	const unterverzeichnis = 0x80000000
	verzeichnis(2)
	binary.Write(&a, le, []uint32{artSymbol, unterverzeichnis | uint32(ortSymbole), artSymbolgruppe, unterverzeichnis | uint32(ortGruppe)})
	verzeichnis(n)
	for i := 0; i < n; i++ {
		binary.Write(&a, le, []uint32{uint32(i + 1), unterverzeichnis | uint32(ortSprachen+i*(kopf+eintrag))})
	}
	verzeichnis(1)
	binary.Write(&a, le, []uint32{1, unterverzeichnis | uint32(ortSprachen+n*(kopf+eintrag))})
	for i := 0; i <= n; i++ {
		verzeichnis(1)
		binary.Write(&a, le, []uint32{sprache, uint32(ortEintraege + i*datenEintrag)})
	}
	// Dateneinträge. Der Ort der Daten ist erst beim Binden des Programms
	// bekannt; hier steht der Abstand vom Anfang des Abschnitts, und eine
	// Verweisliste sagt dem Binder, wo er den Rest addieren muss.
	var verweise []uint32
	ort := ortDaten
	for _, d := range inhalte {
		verweise = append(verweise, uint32(a.Len()))
		binary.Write(&a, le, []uint32{uint32(ort), uint32(len(d)), 0, 0})
		ort += (len(d) + 7) &^ 7
	}
	for _, d := range inhalte {
		a.Write(d)
		a.Write(make([]byte, (8-len(d)%8)%8))
	}
	abschnitt := a.Bytes()

	// Die Objektdatei: Kopf, Beschreibung des Abschnitts, der Abschnitt,
	// die Verweisliste, ein Symbol für den Abschnitt, eine leere Namensliste.
	const dateikopf, abschnittskopf = 20, 40
	ortAbschnitt := dateikopf + abschnittskopf
	ortVerweise := ortAbschnitt + len(abschnitt)
	ortSymbol := ortVerweise + len(verweise)*10

	var aus bytes.Buffer
	binary.Write(&aus, le, []uint16{maschineAMD64, 1})
	binary.Write(&aus, le, []uint32{0, uint32(ortSymbol), 1})
	binary.Write(&aus, le, []uint16{0, 0})

	aus.Write([]byte(".rsrc\x00\x00\x00"))
	binary.Write(&aus, le, []uint32{0, 0, uint32(len(abschnitt)), uint32(ortAbschnitt), uint32(ortVerweise), 0})
	binary.Write(&aus, le, []uint16{uint16(len(verweise)), 0})
	binary.Write(&aus, le, uint32(0x40000040)) // enthält Daten, nur lesbar

	aus.Write(abschnitt)
	for _, v := range verweise {
		binary.Write(&aus, le, []uint32{v, 0})
		binary.Write(&aus, le, uint16(verweisRVA))
	}
	aus.Write([]byte(".rsrc\x00\x00\x00"))
	binary.Write(&aus, le, uint32(0))
	binary.Write(&aus, le, []uint16{1, 0})
	aus.Write([]byte{3, 0}) // Symbol steht für den Abschnitt selbst, keine Zusätze
	binary.Write(&aus, le, uint32(4))
	return aus.Bytes()
}
