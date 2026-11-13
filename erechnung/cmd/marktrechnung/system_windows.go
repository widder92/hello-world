//go:build windows

package main

import (
	"os"
	"os/exec"
	"path/filepath"
	"syscall"
	"unsafe"
)

var (
	shell32              = syscall.NewLazyDLL("shell32.dll")
	ole32                = syscall.NewLazyDLL("ole32.dll")
	user32               = syscall.NewLazyDLL("user32.dll")
	kernel32             = syscall.NewLazyDLL("kernel32.dll")
	shGetKnownFolderPath = shell32.NewProc("SHGetKnownFolderPath")
	shellExecute         = shell32.NewProc("ShellExecuteW")
	coTaskMemFree        = ole32.NewProc("CoTaskMemFree")
	messageBox           = user32.NewProc("MessageBoxW")
	enumWindows          = user32.NewProc("EnumWindows")
	getWindowProcessID   = user32.NewProc("GetWindowThreadProcessId")
	isWindowVisible      = user32.NewProc("IsWindowVisible")
	postMessage          = user32.NewProc("PostMessageW")
	getConsoleWindow     = kernel32.NewProc("GetConsoleWindow")
)

// oeffne übergibt eine Adresse oder einen Ordner an Windows, das dafür das
// zuständige Programm startet (Browser bzw. Explorer).
func oeffne(ziel string) error {
	z, err := syscall.UTF16PtrFromString(ziel)
	if err != nil {
		return err
	}
	verb, _ := syscall.UTF16PtrFromString("open")
	r, _, _ := shellExecute.Call(0, uintptr(unsafe.Pointer(verb)), uintptr(unsafe.Pointer(z)), 0, 0, 1)
	if r <= 32 {
		// Ersatzweg über die Eingabeaufforderung.
		return exec.Command("rundll32", "url.dll,FileProtocolHandler", ziel).Start()
	}
	return nil
}

func oeffneBrowser(adresse string) error { return oeffne(adresse) }

func oeffneOrdner(pfad string) error { return oeffne(pfad) }

// dokumente fragt Windows nach dem Dokumente-Ordner. Der liegt nicht immer
// unter dem Benutzerordner, zum Beispiel wenn OneDrive ihn sichert.
func dokumente() string {
	// FOLDERID_Documents {FDD39AD0-238F-46AF-ADB4-6C85480369C7}
	kennung := [16]byte{0xD0, 0x9A, 0xD3, 0xFD, 0x8F, 0x23, 0xAF, 0x46, 0xAD, 0xB4, 0x6C, 0x85, 0x48, 0x03, 0x69, 0xC7}
	var zeiger *uint16
	r, _, _ := shGetKnownFolderPath.Call(uintptr(unsafe.Pointer(&kennung)), 0, 0, uintptr(unsafe.Pointer(&zeiger)))
	if r == 0 && zeiger != nil {
		defer coTaskMemFree.Call(uintptr(unsafe.Pointer(zeiger)))
		var zeichen []uint16
		for p := unsafe.Pointer(zeiger); *(*uint16)(p) != 0; p = unsafe.Add(p, 2) {
			zeichen = append(zeichen, *(*uint16)(p))
		}
		if pfad := syscall.UTF16ToString(zeichen); pfad != "" {
			return pfad
		}
	}
	heim, err := os.UserHomeDir()
	if err != nil {
		return "."
	}
	return filepath.Join(heim, "Documents")
}

// hatKonsole meldet, ob das Programm in einem Konsolenfenster läuft. Die
// Fassung für Windows wird ohne Konsole gebaut; dann geht das Protokoll in
// eine Datei.
func hatKonsole() bool {
	r, _, _ := getConsoleWindow.Call()
	return r != 0
}

// meldeFehler zeigt ein Hinweisfenster, weil es keine Konsole gibt.
func meldeFehler(text string) {
	t, _ := syscall.UTF16PtrFromString(text)
	titel, _ := syscall.UTF16PtrFromString("Marktrechnung")
	messageBox.Call(0, uintptr(unsafe.Pointer(t)), uintptr(unsafe.Pointer(titel)), 0x10)
}

// browserKandidaten nennt Browser, die App-Fenster beherrschen. Microsoft
// Edge gehört zu Windows und steht deshalb vorn.
func browserKandidaten() []string {
	var l []string
	orte := []string{os.Getenv("ProgramFiles(x86)"), os.Getenv("ProgramFiles"), os.Getenv("LocalAppData")}
	for _, teil := range []string{
		`Microsoft\Edge\Application\msedge.exe`,
		`Google\Chrome\Application\chrome.exe`,
		`BraveSoftware\Brave-Browser\Application\brave.exe`,
		`Vivaldi\Application\vivaldi.exe`,
	} {
		for _, ort := range orte {
			if ort != "" {
				l = append(l, filepath.Join(ort, teil))
			}
		}
	}
	return l
}

// schliesseFenster schließt die Fenster des Browsers so, als hätte jemand
// auf das Kreuz geklickt. So endet er ordentlich und zeigt beim nächsten
// Start keine Meldung über einen Absturz.
func schliesseFenster(p *os.Process) {
	const wmClose = 0x0010
	ziel := uint32(p.Pid)
	rueckruf := syscall.NewCallback(func(fenster, _ uintptr) uintptr {
		var pid uint32
		getWindowProcessID.Call(fenster, uintptr(unsafe.Pointer(&pid)))
		if sichtbar, _, _ := isWindowVisible.Call(fenster); pid == ziel && sichtbar != 0 {
			postMessage.Call(fenster, wmClose, 0, 0)
		}
		return 1 // weiter mit dem nächsten Fenster
	})
	enumWindows.Call(rueckruf, 0)
}

// fensterOrdner ist der Ort für das Profil des Programmfensters. Er liegt
// bei den lokalen Programmdaten, weil ein Browserprofil groß werden kann und
// nicht von Rechner zu Rechner mitwandern soll.
func fensterOrdner() string {
	ordner, err := os.UserCacheDir()
	if err != nil {
		ordner = os.TempDir()
	}
	return filepath.Join(ordner, "Marktrechnung", "fenster")
}
