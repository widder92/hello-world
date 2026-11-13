#!/bin/sh
# Stellt die Lizenztexte der Bibliotheken zusammen, die im Programm stecken,
# und gibt sie aus. bauen.sh legt das Ergebnis als LIZENZEN.txt zu jedem Paket.
# Aufruf: ./lizenzen.sh > LIZENZEN.txt
set -eu
cd "$(dirname "$0")"
cat <<'KOPF'
Marktrechnung - Lizenzen der enthaltenen Bibliotheken
=====================================================

Das Programm enthält die folgenden Bibliotheken. Ihre Lizenzen verlangen,
dass der jeweilige Lizenztext mitgegeben wird.

Schrift im PDF: Liberation Sans, SIL Open Font License 1.1
Oberfläche: Preact (MIT-Lizenz) und htm (Apache-Lizenz 2.0)

Für den KI-Teil des Assistenten (Anbindung an Claude von Anthropic):
KOPF
# Module, aus denen tatsächlich Pakete im Programm landen.
module="$(GOOS=windows GOARCH=amd64 go list -deps -f '{{with .Module}}{{if not .Main}}{{.Path}} {{.Version}} {{.Dir}}{{end}}{{end}}' ./cmd/marktrechnung | sort -u)"
while read -r pfad version ordner; do
  printf '%s %s\n' "$pfad" "$version"
done <<LISTE
$module
LISTE
strich="------------------------------------------------------------------------"
while read -r pfad version ordner; do
  printf '\n\n%s\n%s %s\n%s\n\n' "$strich" "$pfad" "$version" "$strich"
  gefunden=""
  for name in LICENSE LICENSE.txt LICENSE.md LICENCE COPYING; do
    if [ -f "$ordner/$name" ]; then cat "$ordner/$name"; gefunden=1; break; fi
  done
  if [ -z "$gefunden" ]; then echo "Fehler: keine Lizenzdatei für $pfad in $ordner" >&2; exit 1; fi
done <<LISTE
$module
LISTE
printf '\n\n%s\nPreact und htm\n%s\n\n' "$strich" "$strich"
cat web/dateien/fremd/LIZENZEN.txt
printf '\n\n%s\nLiberation Sans\n%s\n\n' "$strich" "$strich"
cat internal/pdf/schriften/LIZENZ-Liberation.txt
