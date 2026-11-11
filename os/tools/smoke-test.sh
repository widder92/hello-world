#!/bin/sh
# Bootet HelloOS ohne Bildschirm, bedient die Shell über die serielle
# Schnittstelle und prüft, ob die erwarteten Ausgaben erscheinen.
set -u

IMAGE="${1:-build/helloos.img}"
QEMU="${QEMU:-qemu-system-i386}"
LOG="$(mktemp)"
trap 'rm -f "$LOG"' EXIT

{
    sleep 3
    printf 'info\nmem\ndate\n'
    sleep 1
    printf 'write test.txt Hallo Welt\nappend test.txt Zeile zwei\ncat test.txt\nls\n'
    sleep 1
    printf 'spawn counter 2\nspawn burn\n'
    sleep 3
    printf 'ps\nkill 4\nrm test.txt\nls\nuptime\nshutdown\n'
    sleep 2
} | timeout 60 "$QEMU" -drive format=raw,file="$IMAGE" -display none \
        -serial stdio -no-reboot > "$LOG" 2>&1
status=$?

failed=0
expect() {
    if grep -q -- "$1" "$LOG"; then
        echo "  ok      $1"
    else
        echo "  FEHLT   $1"
        failed=1
    fi
}

echo "Prüfe Ausgaben:"
expect "Multitasking gestartet"
expect "HelloOS 0.1 (x86, 32 Bit, Protected Mode)"
expect "Speicherkarte des BIOS"
expect "Hallo Welt"
expect "Zeile zwei"
expect "22 Bytes  test.txt"
expect "PID 3\] 2 von 2"
expect "heizung"
expect "Task 4 beendet"
expect "^1 Datei(en)"
expect "Laufzeit: 00:00:"
expect "System wird heruntergefahren"

if [ "$status" -ne 0 ]; then
    echo "  FEHLT   sauberes Herunterfahren (QEMU-Status $status)"
    failed=1
fi

if [ "$failed" -ne 0 ]; then
    echo
    echo "--- Ausgabe der seriellen Konsole ---"
    cat "$LOG"
    echo "Test fehlgeschlagen."
    exit 1
fi
echo "Alle Tests bestanden."
