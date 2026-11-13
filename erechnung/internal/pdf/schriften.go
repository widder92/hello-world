package pdf

import _ "embed"

// Die Schrift Liberation Sans (SIL Open Font License, siehe
// schriften/LIZENZ-Liberation.txt) wird ins Programm eingebaut, damit die
// Rechnungen auf jedem Rechner gleich aussehen.

//go:embed schriften/LiberationSans-Regular.ttf
var normalTTF []byte

//go:embed schriften/LiberationSans-Bold.ttf
var fettTTF []byte

// Standardschriften lädt die beiden eingebauten Schriften (normal und fett)
// frisch für ein neues Dokument.
func Standardschriften() (normal, fett *Schrift) {
	n, err := LadeSchrift(normalTTF, "LiberationSans")
	if err != nil {
		panic(err)
	}
	f, err := LadeSchrift(fettTTF, "LiberationSans-Bold")
	if err != nil {
		panic(err)
	}
	return n, f
}
