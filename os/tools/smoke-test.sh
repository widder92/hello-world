#!/bin/sh
# Bootet HelloOS in QEMU (UEFI, ohne Bildschirmfenster), bedient die Shell über
# die serielle Schnittstelle und prüft, ob die erwarteten Ausgaben erscheinen.
#
# Aufruf: smoke-test.sh [image] [weitere QEMU-Optionen ...]
set -u

IMAGE="${1:-build/helloos.img}"
[ $# -gt 0 ] && shift
QEMU="${QEMU:-qemu-system-x86_64}"
OVMF="${OVMF:-/usr/share/ovmf/OVMF.fd}"

if [ ! -f "$OVMF" ]; then
    echo "UEFI-Firmware nicht gefunden: $OVMF (Paket 'ovmf' installieren" \
         "oder OVMF=/pfad/zu/OVMF.fd setzen)"
    exit 1
fi

DIR="$(mktemp -d)"
LOG="$DIR/serial.log"
FIFO="$DIR/input"
mkfifo "$FIFO"
: > "$LOG"

# Das Image steckt als USB-Stick am xHCI-Controller; QEMUs eingebautes Netz
# ("user") liefert DHCP und einen anpingbaren Router - auch ohne Internet.
# Gearbeitet wird auf einer Kopie, damit das Original unverändert bleibt.
cp "$IMAGE" "$DIR/stick.img"
start_qemu() {
    : > "$LOG"
    timeout 180 "$QEMU" -machine q35 -m 256 -bios "$OVMF" -device qemu-xhci \
        -drive if=none,id=stick,format=raw,file="$DIR/stick.img" \
        -device usb-storage,drive=stick \
        -netdev user,id=net0 -device e1000e,netdev=net0 -display none \
        -serial stdio -no-reboot "$@" < "$FIFO" > "$LOG" 2>&1 &
    QEMU_PID=$!
    exec 3> "$FIFO"
}
start_qemu "$@"
trap 'exec 3>&-; kill "$QEMU_PID" 2>/dev/null; rm -rf "$DIR"' EXIT

# Wartet (höchstens $2 Sekunden), bis der Text in der Ausgabe auftaucht.
wait_for() {
    tries=$(( ${2:-20} * 10 ))
    until grep -q -- "$1" "$LOG"; do
        tries=$((tries - 1))
        [ "$tries" -le 0 ] && return 1
        sleep 0.1
    done
}

failed=0
expect() {
    if wait_for "$1" "${2:-20}"; then
        echo "  ok      $1"
    else
        echo "  FEHLT   $1"
        failed=1
    fi
}

echo "Starte HelloOS in QEMU ..."
expect "HelloOS: Start über UEFI" 90
expect "Multitasking gestartet" 30
expect "helloos> " 30

printf 'info\nmem\ndate\n' >&3
expect "HelloOS 0.6 (x86-64, UEFI"
expect "Speicherkarte der Firmware"

printf 'write test.txt Hallo Welt\nappend test.txt Zeile zwei\ncat test.txt\nls\n' >&3
expect "Zeile zwei"
expect "22 Bytes  test.txt"

printf 'spawn counter 2\n' >&3
expect "Task gestartet (PID"
expect "\] 2 von 2"

printf 'spawn burn\nstart monitor\n' >&3
sleep 2
printf 'ps\n' >&3
expect "heizung"
expect "monitor"
BURN_PID="$(grep heizung "$LOG" | tail -1 | awk '{print $1}')"
printf 'kill %s\n' "$BURN_PID" >&3
expect "Task $BURN_PID beendet"

printf 'rm test.txt\nls\nuptime\n' >&3
expect "^1 Datei(en)"
expect "Laufzeit: 00:0"

# Datenträger und Netzwerk
expect "Adresse 10.0.2.15 per DHCP erhalten" 30
printf 'disk\nping 10.0.2.2\n' >&3
expect "Startdatei EFI/BOOT/BOOTX64.EFI"
expect "4 von 4 Paketen beantwortet"

# Dateiübertragung: ohne Angaben erklärt sich der Befehl
printf 'sftp\n' >&3
expect "Aufruf: sftp <benutzer>@<server>"

# Claude: ohne Schlüssel gibt es eine klare Meldung und keinen Netzzugriff
printf 'claude\nclaude hallo\n' >&3
expect "Schlüssel: nicht gesetzt"
expect "Es ist kein API-Schlüssel hinterlegt"

# Eigene Dateien werden auf dem Stick gesichert ...
expect "Dateien: 0 vom USB-Stick geladen" 30
printf 'write bleibt.txt dauerhaft gespeichert\nls\n' >&3
expect "2 Datei(en), gesichert auf dem USB-Stick"

printf 'shutdown\n' >&3
expect "System wird heruntergefahren"
if wait "$QEMU_PID"; then
    echo "  ok      QEMU hat sich nach dem Herunterfahren beendet"
else
    echo "  FEHLT   sauberes Herunterfahren"
    failed=1
fi

# ... und sind nach einem Neustart wieder da.
echo "Starte HelloOS ein zweites Mal ..."
exec 3>&-
start_qemu "$@"
expect "Dateien: 1 vom USB-Stick geladen" 120
printf 'cat bleibt.txt\nshutdown\n' >&3
expect "^dauerhaft gespeichert"
expect "System wird heruntergefahren"
wait "$QEMU_PID"

if [ "$failed" -ne 0 ]; then
    echo
    echo "--- Ausgabe der seriellen Konsole ---"
    cat "$LOG"
    echo "Test fehlgeschlagen."
    exit 1
fi
echo "Alle Tests bestanden."
