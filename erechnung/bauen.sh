#!/bin/sh
# Baut Marktrechnung für Windows, macOS und Linux und legt die Pakete in den
# Ordner dist/:
#   - für Windows ein Setup (wenn das Werkzeug "makensis" vorhanden ist,
#     unter Ubuntu: apt-get install nsis) und ein ZIP ohne Installation,
#   - für macOS und Linux je ein ZIP.
# Aufruf: ./bauen.sh [Versionsnummer]
set -eu
cd "$(dirname "$0")"
hier="$(pwd)"
version="${1:-$(sed -n 's/^var Nummer = "\(.*\)"/\1/p' internal/version/version.go)}"
merker="-s -w -X marktrechnung/internal/version.Nummer=$version"
rm -rf dist && mkdir -p dist

baue() { # System Architektur Dateiname Zusatz-Schalter Paketname
  ordner="dist/$5"
  mkdir -p "$ordner"
  GOOS="$1" GOARCH="$2" CGO_ENABLED=0 go build -trimpath -ldflags "$merker $4" -o "$ordner/$3" ./cmd/marktrechnung
  cp LIESMICH.txt docs/Anleitung.pdf dist/LIZENZEN.txt "$ordner/"
  if [ "$1" = windows ]; then setup "$ordner/$3"; fi
  (cd dist && zip -q -r "$5.zip" "$5" && rm -r "$5")
  echo "dist/$5.zip"
}

setup() { # Pfad der Programmdatei
  if ! command -v makensis >/dev/null 2>&1; then
    echo "Hinweis: makensis fehlt - das Windows-Setup wird nicht gebaut." >&2
    return
  fi
  # Die Dateiversion im Setup verlangt vier Zahlen; alles andere wird zu 0.
  vier="$(printf '%s' "$version" | sed 's/[^0-9.].*$//; s/^\.*//; s/\.*$//')"
  case "$vier" in
    *.*.*.*) ;; *.*.*) vier="$vier.0" ;; *.*) vier="$vier.0.0" ;; "") vier="0.0.0.0" ;; *) vier="$vier.0.0.0" ;;
  esac
  makensis -V2 -INPUTCHARSET UTF8 -DVERSION="$version" -DVIERSTELLIG="$vier" \
    -DEXE="$hier/$1" -DLIESMICH="$hier/LIESMICH.txt" -DANLEITUNG="$hier/docs/Anleitung.pdf" -DLIZENZEN="$hier/dist/LIZENZEN.txt" \
    -DAUSGABE="$hier/dist/Marktrechnung-Setup-$version.exe" setup/marktrechnung.nsi >/dev/null
  echo "dist/Marktrechnung-Setup-$version.exe"
}

# Lizenztexte der enthaltenen Bibliotheken; sie liegen jedem Paket bei.
./lizenzen.sh > dist/LIZENZEN.txt

# Windows ohne Konsolenfenster: Das Programm zeigt sich nur im Browser.
baue windows amd64 Marktrechnung.exe "-H=windowsgui" "Marktrechnung-$version-Windows"
baue darwin  arm64 Marktrechnung     ""              "Marktrechnung-$version-macOS-Apple-Chip"
baue darwin  amd64 Marktrechnung     ""              "Marktrechnung-$version-macOS-Intel"
baue linux   amd64 marktrechnung     ""              "Marktrechnung-$version-Linux"
rm -f dist/LIZENZEN.txt
