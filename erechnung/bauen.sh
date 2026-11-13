#!/bin/sh
# Baut Marktrechnung für Windows, macOS und Linux und legt je ein ZIP-Paket
# in den Ordner dist/. Aufruf: ./bauen.sh [Versionsnummer]
set -eu
cd "$(dirname "$0")"
version="${1:-$(sed -n 's/^var Nummer = "\(.*\)"/\1/p' internal/version/version.go)}"
merker="-s -w -X marktrechnung/internal/version.Nummer=$version"
rm -rf dist && mkdir -p dist

baue() { # System Architektur Dateiname Zusatz-Schalter Paketname
  ordner="dist/$5"
  mkdir -p "$ordner"
  GOOS="$1" GOARCH="$2" CGO_ENABLED=0 go build -trimpath -ldflags "$merker $4" -o "$ordner/$3" ./cmd/marktrechnung
  cp LIESMICH.txt "$ordner/"
  (cd dist && zip -q -r "$5.zip" "$5" && rm -r "$5")
  echo "dist/$5.zip"
}

# Windows ohne Konsolenfenster: Das Programm zeigt sich nur im Browser.
baue windows amd64 Marktrechnung.exe "-H=windowsgui" "Marktrechnung-$version-Windows"
baue darwin  arm64 Marktrechnung     ""              "Marktrechnung-$version-macOS-Apple-Chip"
baue darwin  amd64 Marktrechnung     ""              "Marktrechnung-$version-macOS-Intel"
baue linux   amd64 marktrechnung     ""              "Marktrechnung-$version-Linux"
