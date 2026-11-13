//go:build windows

package geheim

import (
	"errors"
	"syscall"
	"unsafe"
)

// Windows verschlüsselt mit der "Data Protection API": Der Schlüssel hängt
// am Windows-Benutzerkonto, das Programm selbst kennt ihn nicht.

var (
	crypt32       = syscall.NewLazyDLL("crypt32.dll")
	kernel32      = syscall.NewLazyDLL("kernel32.dll")
	protectData   = crypt32.NewProc("CryptProtectData")
	unprotectData = crypt32.NewProc("CryptUnprotectData")
	localFree     = kernel32.NewProc("LocalFree")
)

type blob struct {
	laenge uint32
	daten  *byte
}

func neuerBlob(d []byte) *blob {
	if len(d) == 0 {
		return &blob{}
	}
	return &blob{laenge: uint32(len(d)), daten: &d[0]}
}

func (b *blob) kopie() []byte {
	if b.daten == nil || b.laenge == 0 {
		return nil
	}
	aus := make([]byte, b.laenge)
	copy(aus, unsafe.Slice(b.daten, b.laenge))
	return aus
}

const ohneDialog = 0x1 // CRYPTPROTECT_UI_FORBIDDEN

func schuetze(klar []byte) ([]byte, string, error) {
	var aus blob
	r, _, err := protectData.Call(uintptr(unsafe.Pointer(neuerBlob(klar))), 0, 0, 0, 0, ohneDialog, uintptr(unsafe.Pointer(&aus)))
	if r == 0 {
		return nil, "", err
	}
	defer localFree.Call(uintptr(unsafe.Pointer(aus.daten)))
	return aus.kopie(), "dpapi", nil
}

func oeffne(daten []byte, art string) ([]byte, error) {
	if art != "dpapi" {
		return nil, errors.New("unbekannte Ablageart")
	}
	var aus blob
	r, _, err := unprotectData.Call(uintptr(unsafe.Pointer(neuerBlob(daten))), 0, 0, 0, 0, ohneDialog, uintptr(unsafe.Pointer(&aus)))
	if r == 0 {
		return nil, err
	}
	defer localFree.Call(uintptr(unsafe.Pointer(aus.daten)))
	return aus.kopie(), nil
}
