package main

import (
	"encoding/json"
	"log"
	"os"
	"path/filepath"
	"runtime"
	"sort"
	"strconv"
	"strings"
	"syscall"
	"unsafe"
)

// Das eigene Programmfenster unter Windows.
//
// Marktrechnung öffnet ein gewöhnliches Windows-Fenster mit eigenem Titel
// und Symbol und lässt darin die Oberfläche von "WebView2" zeichnen - der
// Komponente, mit der Windows Programmen das Anzeigen von Webinhalten
// erlaubt (sie gehört zu Windows 11 und ist auf Windows 10 praktisch immer
// vorhanden). Vom Browser ist dabei nichts zu sehen: keine Adresszeile,
// kein Browsermenü, keine Tastenkürzel des Browsers.
//
// Das Fenster läuft als eigener Prozess desselben Programms (Schalter
// -fenster-eigen). Geht dabei etwas schief, bleibt das Programm selbst
// unberührt und zeigt die Oberfläche auf dem bisherigen Weg.
//
// Die Schnittstelle zu WebView2 ist COM. Die Nummern der Methoden
// entsprechen ihrer Reihenfolge in der Kopfdatei WebView2.h von Microsoft.

const (
	fensterklasse   = "MarktrechnungFenster"
	fenstertitel    = "Marktrechnung"
	endeKeinWebView = 3 // das Fenster kam nicht zustande: bitte den anderen Weg nehmen
)

var (
	gdi32                 = syscall.NewLazyDLL("gdi32.dll")
	procRegisterClassEx   = user32.NewProc("RegisterClassExW")
	procCreateWindowEx    = user32.NewProc("CreateWindowExW")
	procDefWindowProc     = user32.NewProc("DefWindowProcW")
	procDestroyWindow     = user32.NewProc("DestroyWindow")
	procShowWindow        = user32.NewProc("ShowWindow")
	procUpdateWindow      = user32.NewProc("UpdateWindow")
	procGetMessage        = user32.NewProc("GetMessageW")
	procTranslateMessage  = user32.NewProc("TranslateMessage")
	procDispatchMessage   = user32.NewProc("DispatchMessageW")
	procPostQuitMessage   = user32.NewProc("PostQuitMessage")
	procGetClientRect     = user32.NewProc("GetClientRect")
	procLoadIcon          = user32.NewProc("LoadIconW")
	procLoadCursor        = user32.NewProc("LoadCursorW")
	procSetTimer          = user32.NewProc("SetTimer")
	procKillTimer         = user32.NewProc("KillTimer")
	procFindWindow        = user32.NewProc("FindWindowW")
	procSetForeground     = user32.NewProc("SetForegroundWindow")
	procIsIconic          = user32.NewProc("IsIconic")
	procSystemParameters  = user32.NewProc("SystemParametersInfoW")
	procGetDpiForSystem   = user32.NewProc("GetDpiForSystem")
	procSetDpiAwareness   = user32.NewProc("SetProcessDpiAwarenessContext")
	procBeginPaint        = user32.NewProc("BeginPaint")
	procEndPaint          = user32.NewProc("EndPaint")
	procDrawText          = user32.NewProc("DrawTextW")
	procGetPlacement      = user32.NewProc("GetWindowPlacement")
	procSetPlacement      = user32.NewProc("SetWindowPlacement")
	procSetBkMode         = gdi32.NewProc("SetBkMode")
	procGetStockObject    = gdi32.NewProc("GetStockObject")
	procSelectObject      = gdi32.NewProc("SelectObject")
	procGetModuleHandle   = kernel32.NewProc("GetModuleHandleW")
	procCoInitializeEx    = ole32.NewProc("CoInitializeEx")
	procCoTaskMemFreeText = ole32.NewProc("CoTaskMemFree")
)

type rechteck struct{ links, oben, rechts, unten int32 }

type fensterklasseEx struct {
	groesse     uint32
	stil        uint32
	prozedur    uintptr
	klasseExtra int32
	fensterEx   int32
	instanz     uintptr
	symbol      uintptr
	zeiger      uintptr
	hintergrund uintptr
	menue       *uint16
	name        *uint16
	symbolKlein uintptr
}

type nachricht struct {
	fenster uintptr
	nummer  uint32
	w       uintptr
	l       uintptr
	zeit    uint32
	x, y    int32
	privat  uint32
}

type malbereich struct {
	hdc      uintptr
	loeschen int32
	bereich  rechteck
	_        [48]byte
}

type fensterlage struct {
	laenge   uint32
	schalter uint32
	zeigen   uint32
	minX     int32
	minY     int32
	maxX     int32
	maxY     int32
	normal   rechteck
}

// com ist eine COM-Schnittstelle: ein Zeiger auf die Liste ihrer Methoden.
type com struct{ methoden *[64]uintptr }

// ruf ruft die Methode mit der genannten Nummer. Zeiger unter den
// Argumenten müssen auf Speicher zeigen, der nicht wandert - hier sind das
// die Felder von z.
func (o *com) ruf(nummer int, argumente ...uintptr) uintptr {
	alle := make([]uintptr, 0, 6)
	alle = append(alle, uintptr(unsafe.Pointer(o)))
	alle = append(alle, argumente...)
	r, _, _ := syscall.SyscallN(o.methoden[nummer], alle...)
	return r
}

// misslungen wertet das Ergebnis eines COM-Aufrufs (HRESULT).
func misslungen(hr uintptr) bool { return int32(uint32(hr)) < 0 }

// kennung wandelt die Schreibweise einer GUID in ihre Ablage im Speicher.
func kennung(text string) (g [16]byte) {
	hex := strings.ReplaceAll(text, "-", "")
	var roh [16]byte
	for i := range roh {
		n, _ := strconv.ParseUint(hex[2*i:2*i+2], 16, 8)
		roh[i] = byte(n)
	}
	// Die ersten drei Gruppen liegen mit dem niedrigsten Byte zuerst.
	g = [16]byte{roh[3], roh[2], roh[1], roh[0], roh[5], roh[4], roh[7], roh[6]}
	copy(g[8:], roh[8:])
	return g
}

var (
	iidUnknown       = kennung("00000000-0000-0000-c000-000000000046")
	iidEinstellungen = [...][16]byte{ // ICoreWebView2Settings6, 4 und 3 - je nach Alter von WebView2
		kennung("11cb3acd-9bc8-43b8-83bf-f40753714f87"),
		kennung("cb56846c-4168-4d53-b04f-03b6d6796ff2"),
		kennung("fdb5ab74-af33-4854-84f0-0a631deb5eba"),
	}
)

// rueckruf ist ein COM-Objekt aus diesem Programm: WebView2 ruft seine
// vierte Methode (Invoke), sobald etwas fertig ist oder geschieht.
type rueckruf struct {
	methoden *[4]uintptr
	iid      [16]byte
	tu       func(a uintptr, b *com) uintptr
}

// Die vier Methoden eines Rückrufs; angelegt erst im Fensterprozess.
var rueckrufMethoden [4]uintptr

func legeRueckrufeAn() {
	rueckrufMethoden = [4]uintptr{
		syscall.NewCallback(func(dies *rueckruf, iid *[16]byte, ziel *unsafe.Pointer) uintptr { // QueryInterface
			if *iid == iidUnknown || *iid == dies.iid {
				*ziel = unsafe.Pointer(dies)
				return 0
			}
			*ziel = nil
			return 0x80004002 // E_NOINTERFACE
		}),
		syscall.NewCallback(func(dies *rueckruf) uintptr { return 1 }), // AddRef: lebt, solange das Programm läuft
		syscall.NewCallback(func(dies *rueckruf) uintptr { return 1 }), // Release
		syscall.NewCallback(func(dies *rueckruf, a uintptr, b *com) uintptr { return dies.tu(a, b) }),
	}
}

// Zustand des Fensters. Er liegt an fester Stelle im Speicher, weil
// WebView2 Zeiger auf seine Felder bekommt.
var z struct {
	fenster          uintptr
	adresse          *uint16
	profil           *uint16
	umgebung         *com
	controller       *com
	webview          *com
	einstellungen    *com
	einstellungenNeu *com
	flaeche          rechteck
	arbeitsflaeche   rechteck
	malen            malbereich
	lage             fensterlage
	marke            int64
	ziel             *uint16
	text             *uint16
	rueckrufe        []*rueckruf
	lagedatei        string
	ende             uintptr
}

func neuerRueckruf(iid string, tu func(a uintptr, b *com) uintptr) *rueckruf {
	r := &rueckruf{methoden: &rueckrufMethoden, iid: kennung(iid), tu: tu}
	z.rueckrufe = append(z.rueckrufe, r) // festhalten: WebView2 behält nur die Adresse
	return r
}

func utf16(s string) *uint16 {
	p, _ := syscall.UTF16PtrFromString(s)
	return p
}

// goText liest einen Text, den Windows geliefert hat.
func goText(p *uint16) string {
	if p == nil {
		return ""
	}
	n := 0
	for *(*uint16)(unsafe.Add(unsafe.Pointer(p), 2*n)) != 0 {
		n++
	}
	return syscall.UTF16ToString(unsafe.Slice(p, n))
}

// findeWebView sucht die Programmbibliothek von WebView2. Windows trägt
// ihren Ort in die Registrierung ein; ersatzweise wird der übliche Ordner
// durchsucht.
func findeWebView() string {
	const zweig = `Software\Microsoft\EdgeUpdate\ClientState\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}`
	const bibliothek = `EBWebView\x64\EmbeddedBrowserWebView.dll`
	da := func(ordner string) string {
		if ordner == "" {
			return ""
		}
		pfad := filepath.Join(ordner, bibliothek)
		if info, err := os.Stat(pfad); err == nil && !info.IsDir() {
			return pfad
		}
		return ""
	}
	for _, wurzel := range []syscall.Handle{syscall.HKEY_LOCAL_MACHINE, syscall.HKEY_CURRENT_USER} {
		for _, sicht := range []uint32{0x0200, 0x0100} { // 32- und 64-Bit-Sicht der Registrierung
			if pfad := da(registerText(wurzel, zweig, "EBWebView", sicht)); pfad != "" {
				return pfad
			}
		}
	}
	var kandidaten []string
	for _, ort := range []string{os.Getenv("ProgramFiles(x86)"), os.Getenv("ProgramFiles"), os.Getenv("LocalAppData")} {
		if ort == "" {
			continue
		}
		fassungen, _ := filepath.Glob(filepath.Join(ort, `Microsoft\EdgeWebView\Application`, "*"))
		sort.Sort(sort.Reverse(sort.StringSlice(fassungen)))
		kandidaten = append(kandidaten, fassungen...)
	}
	for _, ordner := range kandidaten {
		if pfad := da(ordner); pfad != "" {
			return pfad
		}
	}
	return ""
}

func registerText(wurzel syscall.Handle, zweig, name string, sicht uint32) string {
	var schluessel syscall.Handle
	if syscall.RegOpenKeyEx(wurzel, utf16(zweig), 0, syscall.KEY_READ|sicht, &schluessel) != nil {
		return ""
	}
	defer syscall.RegCloseKey(schluessel)
	var art, laenge uint32
	puffer := make([]uint16, 1024)
	laenge = uint32(len(puffer) * 2)
	if syscall.RegQueryValueEx(schluessel, utf16(name), nil, &art, (*byte)(unsafe.Pointer(&puffer[0])), &laenge) != nil || art != syscall.REG_SZ {
		return ""
	}
	return syscall.UTF16ToString(puffer)
}

// eigenesFensterVerfuegbar meldet, ob das eigene Fenster auf diesem Rechner
// möglich ist.
func eigenesFensterVerfuegbar() bool { return findeWebView() != "" }

// holeFensterNachVorn zeigt das Fenster eines laufenden Marktrechnung.
func holeFensterNachVorn() bool {
	fenster, _, _ := procFindWindow.Call(uintptr(unsafe.Pointer(utf16(fensterklasse))), 0)
	if fenster == 0 {
		return false
	}
	if klein, _, _ := procIsIconic.Call(fenster); klein != 0 {
		procShowWindow.Call(fenster, 9) // wiederherstellen
	}
	procSetForeground.Call(fenster)
	return true
}

// aufgeben schließt das Fenster mit dem Hinweis an das Hauptprogramm, die
// Oberfläche auf dem anderen Weg zu zeigen.
func aufgeben(grund string, hr uintptr) uintptr {
	log.Printf("Das eigene Fenster kommt nicht zustande: %s (Ergebnis 0x%08X)", grund, uint32(hr))
	z.ende = endeKeinWebView
	procDestroyWindow.Call(z.fenster)
	return 0
}

// zeigeEigenesFenster öffnet das Fenster und kehrt zurück, wenn es
// geschlossen wurde. Das Ergebnis ist die Endekennzahl des Fensterprozesses:
// 0 nach dem Schließen durch den Benutzer.
func zeigeEigenesFenster(adresse, profil string) int {
	runtime.LockOSThread()
	bibliothek := findeWebView()
	if bibliothek == "" {
		log.Print("WebView2 ist auf diesem Rechner nicht vorhanden.")
		return endeKeinWebView
	}
	dll, err := syscall.LoadDLL(bibliothek)
	if err != nil {
		log.Printf("WebView2 lässt sich nicht laden: %v", err)
		return endeKeinWebView
	}
	erzeuge, err := dll.FindProc("CreateWebViewEnvironmentWithOptionsInternal")
	if err != nil {
		log.Printf("WebView2 hat nicht die erwartete Schnittstelle: %v", err)
		return endeKeinWebView
	}
	if os.MkdirAll(profil, 0o700) != nil {
		return endeKeinWebView
	}
	// Scharfe Darstellung auf vergrößerten Bildschirmen.
	if procSetDpiAwareness.Find() == nil {
		procSetDpiAwareness.Call(^uintptr(3)) // je Bildschirm, zweite Fassung
	}
	procCoInitializeEx.Call(0, 2) // ein Faden mit Nachrichtenschleife
	legeRueckrufeAn()

	z.adresse, z.profil = utf16(adresse), utf16(profil)
	z.text = utf16("Marktrechnung wird geladen …")
	z.lagedatei = filepath.Join(filepath.Dir(profil), "fensterlage.json")

	instanz, _, _ := procGetModuleHandle.Call(0)
	symbol, _, _ := procLoadIcon.Call(instanz, 1) // das Programmsymbol aus der Programmdatei
	zeiger, _, _ := procLoadCursor.Call(0, 32512)
	klasse := fensterklasseEx{stil: 0x0003, prozedur: syscall.NewCallback(fensterProzedur), instanz: instanz, symbol: symbol,
		zeiger: zeiger, hintergrund: 6, name: utf16(fensterklasse), symbolKlein: symbol}
	klasse.groesse = uint32(unsafe.Sizeof(klasse))
	if r, _, err := procRegisterClassEx.Call(uintptr(unsafe.Pointer(&klasse))); r == 0 {
		log.Printf("Fensterklasse: %v", err)
		return endeKeinWebView
	}

	// Größe: 1340 x 920 in der Vergrößerung des Bildschirms, höchstens der
	// freie Platz; in die Mitte gesetzt.
	dpi := uintptr(96)
	if procGetDpiForSystem.Find() == nil {
		if d, _, _ := procGetDpiForSystem.Call(); d >= 96 {
			dpi = d
		}
	}
	procSystemParameters.Call(0x0030, 0, uintptr(unsafe.Pointer(&z.arbeitsflaeche)), 0)
	platzB, platzH := z.arbeitsflaeche.rechts-z.arbeitsflaeche.links, z.arbeitsflaeche.unten-z.arbeitsflaeche.oben
	breite, hoehe := int32(1340*dpi/96), int32(920*dpi/96)
	if platzB > 0 && breite > platzB {
		breite = platzB
	}
	if platzH > 0 && hoehe > platzH {
		hoehe = platzH
	}
	x, y := z.arbeitsflaeche.links+(platzB-breite)/2, z.arbeitsflaeche.oben+(platzH-hoehe)/2
	if platzB <= 0 || platzH <= 0 {
		x, y = 80, 60
	}
	z.fenster, _, err = procCreateWindowEx.Call(0, uintptr(unsafe.Pointer(klasse.name)), uintptr(unsafe.Pointer(utf16(fenstertitel))),
		0x02CF0000, // gewöhnliches Fenster mit Rahmen, Titelzeile und Knöpfen
		uintptr(x), uintptr(y), uintptr(breite), uintptr(hoehe), 0, 0, instanz, 0)
	if z.fenster == 0 {
		log.Printf("Fenster: %v", err)
		return endeKeinWebView
	}
	zeigen := uintptr(1)
	if stelleLageHer() {
		zeigen = uintptr(z.lage.zeigen)
	}
	procShowWindow.Call(z.fenster, zeigen)
	procUpdateWindow.Call(z.fenster)

	controllerFertig := neuerRueckruf("6c4819f3-c9b7-4260-8127-c9f5bde7f68c", func(hr uintptr, controller *com) uintptr {
		if misslungen(hr) || controller == nil {
			return aufgeben("Anzeige", hr)
		}
		controller.ruf(1) // behalten
		z.controller = controller
		if hr := controller.ruf(25, uintptr(unsafe.Pointer(&z.webview))); misslungen(hr) || z.webview == nil {
			return aufgeben("Inhalt", hr)
		}
		richteEin()
		procGetClientRect.Call(z.fenster, uintptr(unsafe.Pointer(&z.flaeche)))
		controller.ruf(6, uintptr(unsafe.Pointer(&z.flaeche))) // Größe
		controller.ruf(4, 1)                                   // sichtbar
		if hr := z.webview.ruf(5, uintptr(unsafe.Pointer(z.adresse))); misslungen(hr) {
			return aufgeben("Aufruf der Oberfläche", hr)
		}
		controller.ruf(12, 0) // Tastatur in die Oberfläche
		procKillTimer.Call(z.fenster, 1)
		log.Print("Das eigene Fenster zeigt die Oberfläche.")
		return 0
	})
	umgebungFertig := neuerRueckruf("4e8a3389-c9d8-4bd2-b6b5-124fee6cc14d", func(hr uintptr, umgebung *com) uintptr {
		if misslungen(hr) || umgebung == nil {
			return aufgeben("Umgebung", hr)
		}
		umgebung.ruf(1)
		z.umgebung = umgebung
		if hr := umgebung.ruf(3, z.fenster, uintptr(unsafe.Pointer(controllerFertig))); misslungen(hr) {
			return aufgeben("Anzeige anfordern", hr)
		}
		return 0
	})
	// Kommt die Oberfläche nicht in einer halben Minute, gilt der Versuch als gescheitert.
	procSetTimer.Call(z.fenster, 1, 30000, 0)
	if hr, _, _ := erzeuge.Call(1, 0, uintptr(unsafe.Pointer(z.profil)), 0, uintptr(unsafe.Pointer(umgebungFertig))); misslungen(hr) {
		aufgeben("Start von WebView2", hr)
	}

	var n nachricht
	for {
		r, _, _ := procGetMessage.Call(uintptr(unsafe.Pointer(&n)), 0, 0, 0)
		if int32(r) <= 0 {
			break
		}
		procTranslateMessage.Call(uintptr(unsafe.Pointer(&n)))
		procDispatchMessage.Call(uintptr(unsafe.Pointer(&n)))
	}
	return int(n.w)
}

// richteEin stellt ab, was an einen Browser erinnert, und sorgt dafür, dass
// Verweise nach außen im gewohnten Browser aufgehen.
func richteEin() {
	if hr := z.webview.ruf(3, uintptr(unsafe.Pointer(&z.einstellungen))); !misslungen(hr) && z.einstellungen != nil {
		e := z.einstellungen
		e.ruf(10, 0) // keine Leiste mit der Adresse eines Verweises
		e.ruf(12, 0) // keine Entwicklerwerkzeuge
		for i := range iidEinstellungen {
			z.einstellungenNeu = nil
			if hr := e.ruf(0, uintptr(unsafe.Pointer(&iidEinstellungen[i])), uintptr(unsafe.Pointer(&z.einstellungenNeu))); misslungen(hr) || z.einstellungenNeu == nil {
				continue
			}
			neu := z.einstellungenNeu
			neu.ruf(24, 0) // keine Tastenkürzel des Browsers (Neu laden, Suchen, Drucken ...)
			if i <= 1 {
				neu.ruf(26, 0) // keine Frage nach dem Speichern von Passwörtern
				neu.ruf(28, 0) // keine Vorschläge aus früheren Eingaben
			}
			if i == 0 {
				neu.ruf(32, 0) // kein Vor und Zurück durch Wischen
			}
			break
		}
	}
	// Ein Verweis, der ein neues Fenster verlangt, führt nach draußen: Er
	// geht im gewohnten Browser auf, nicht in einem Fenster ohne Rahmen.
	neuesFenster := neuerRueckruf("d4c185fe-c81c-4989-97af-2d3fa7ab5651", func(_ uintptr, angaben *com) uintptr {
		if angaben == nil {
			return 0
		}
		z.ziel = nil
		if hr := angaben.ruf(3, uintptr(unsafe.Pointer(&z.ziel))); misslungen(hr) || z.ziel == nil {
			return 0
		}
		ziel := goText(z.ziel)
		procCoTaskMemFreeText.Call(uintptr(unsafe.Pointer(z.ziel)))
		z.ziel = nil
		angaben.ruf(6, 1) // erledigt: kein neues Fenster
		if strings.HasPrefix(ziel, "https://") || strings.HasPrefix(ziel, "http://") || strings.HasPrefix(ziel, "mailto:") {
			oeffne(ziel)
		}
		return 0
	})
	z.webview.ruf(44, uintptr(unsafe.Pointer(neuesFenster)), uintptr(unsafe.Pointer(&z.marke)))
}

func fensterProzedur(fenster, nummer, w, l uintptr) uintptr {
	switch nummer {
	case 0x0005: // Größe geändert
		if z.controller != nil {
			procGetClientRect.Call(fenster, uintptr(unsafe.Pointer(&z.flaeche)))
			z.controller.ruf(6, uintptr(unsafe.Pointer(&z.flaeche)))
		}
	case 0x0003, 0x0216: // verschoben
		if z.controller != nil {
			z.controller.ruf(23)
		}
	case 0x0007: // das Fenster bekommt die Tastatur
		if z.controller != nil {
			z.controller.ruf(12, 0)
		}
	case 0x000F: // zeichnen: nur zu sehen, solange die Oberfläche lädt
		hdc, _, _ := procBeginPaint.Call(fenster, uintptr(unsafe.Pointer(&z.malen)))
		procGetClientRect.Call(fenster, uintptr(unsafe.Pointer(&z.flaeche)))
		procSetBkMode.Call(hdc, 1)
		if schrift, _, _ := procGetStockObject.Call(17); schrift != 0 { // die Schrift der Windows-Dialoge
			procSelectObject.Call(hdc, schrift)
		}
		procDrawText.Call(hdc, uintptr(unsafe.Pointer(z.text)), ^uintptr(0), uintptr(unsafe.Pointer(&z.flaeche)), 0x25) // mittig, eine Zeile
		procEndPaint.Call(fenster, uintptr(unsafe.Pointer(&z.malen)))
		return 0
	case 0x0113: // die halbe Minute ist um
		procKillTimer.Call(fenster, 1)
		if z.webview == nil {
			return aufgeben("keine Antwort von WebView2", 0)
		}
		return 0
	case 0x0010: // schließen
		merkeLage()
		procDestroyWindow.Call(fenster)
		return 0
	case 0x0002: // zerstört
		if z.controller != nil {
			z.controller.ruf(24)
		}
		procPostQuitMessage.Call(z.ende)
		return 0
	}
	r, _, _ := procDefWindowProc.Call(fenster, nummer, w, l)
	return r
}

// Lage und Größe des Fensters überdauern das Schließen.
type gemerkteLage struct {
	Links, Oben, Rechts, Unten int32
	Maximiert                  bool
}

func merkeLage() {
	z.lage.laenge = uint32(unsafe.Sizeof(z.lage))
	if r, _, _ := procGetPlacement.Call(z.fenster, uintptr(unsafe.Pointer(&z.lage))); r == 0 {
		return
	}
	n := z.lage.normal
	daten, _ := json.Marshal(gemerkteLage{n.links, n.oben, n.rechts, n.unten, z.lage.zeigen == 3})
	os.WriteFile(z.lagedatei, daten, 0o600)
}

// stelleLageHer setzt das Fenster dorthin, wo es zuletzt war. Windows rückt
// es selbst zurecht, falls dort kein Bildschirm mehr ist.
func stelleLageHer() bool {
	daten, err := os.ReadFile(z.lagedatei)
	var g gemerkteLage
	if err != nil || json.Unmarshal(daten, &g) != nil || g.Rechts-g.Links < 400 || g.Unten-g.Oben < 300 {
		return false
	}
	z.lage = fensterlage{zeigen: 1, minX: -1, minY: -1, maxX: -1, maxY: -1, normal: rechteck{g.Links, g.Oben, g.Rechts, g.Unten}}
	z.lage.laenge = uint32(unsafe.Sizeof(z.lage))
	if g.Maximiert {
		z.lage.zeigen = 3
	}
	r, _, _ := procSetPlacement.Call(z.fenster, uintptr(unsafe.Pointer(&z.lage)))
	return r != 0
}
