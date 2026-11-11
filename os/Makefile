# HelloOS (x86-64, UEFI) - Build
#
#   make          baut build/BOOTX64.EFI und das Disk-Image build/helloos.img
#   make run      startet HelloOS in QEMU (Fenster)
#   make run-usb  wie run, aber mit USB-Tastatur und -Maus statt PS/2
#   make test     bootet ohne Bildschirm und prüft die wichtigsten Befehle
#   make check    prüft die Kryptografie gegen eine Referenzbibliothek
#   make update-key      erzeugt das Schlüsselpaar für signierte Updates
#   make update-package  baut build/update/helloos.upd und helloos.version
#   make clean    räumt auf

BUILD   := build
EFI     := $(BUILD)/BOOTX64.EFI
IMAGE   := $(BUILD)/helloos.img

CC      := clang
LD      := lld-link
AS      := nasm
PYTHON  := python3
QEMU    := qemu-system-x86_64

# UEFI-Firmware für QEMU (Paket "ovmf"); bei Bedarf überschreiben:
#   make run OVMF=/pfad/zu/OVMF.fd
OVMF    := $(firstword $(wildcard /usr/share/ovmf/OVMF.fd \
               /usr/share/OVMF/OVMF.fd /usr/share/edk2/ovmf/OVMF.fd \
               /usr/share/edk2/x64/OVMF.4m.fd /usr/share/qemu/OVMF.fd \
               /opt/homebrew/share/qemu/edk2-x86_64-code.fd \
               /usr/local/share/qemu/edk2-x86_64-code.fd))

# --- Updates ---------------------------------------------------------------
# Build-Nummer: Anzahl der Commits - wächst mit jeder Änderung von selbst.
OS_BUILD   := $(shell git rev-list --count HEAD 2>/dev/null || echo 0)
# Hier sucht HelloOS nach helloos.version und helloos.upd.
UPDATE_URL := https://raw.githubusercontent.com/widder92/hello-world/helloos-updates
# Öffentlicher Schlüssel; ohne ihn sind Updates abgeschaltet.
UPDATE_KEY := $(shell cat update-key.pub 2>/dev/null)
# "Was ist neu": wird vor der Installation eines Updates angezeigt.
NOTES      := NEUES.txt

# --- HTTPS -----------------------------------------------------------------
# Adresse der Claude-API; nur für Tests gegen einen Nachbau interessant.
CLAUDE_API_URL := https://api.anthropic.com

# Zusätzliche Zertifizierungsstellen (PEM-Datei), denen HelloOS neben den
# eingebauten vertrauen soll - etwa für einen eigenen Server oder einen
# Firmen-Proxy, der HTTPS-Verbindungen aufbricht. Normalerweise leer.
EXTRA_CA   :=

# UEFI-Programme sind PE-Dateien wie unter Windows - daher dieses Ziel.
CFLAGS  := --target=x86_64-unknown-windows -std=gnu11 -O2 -Wall -Wextra \
           -ffreestanding -fno-stack-protector -fshort-wchar -mno-red-zone \
           -mgeneral-regs-only -mno-stack-arg-probe -MMD -MP -I$(BUILD)
LDFLAGS := /subsystem:efi_application /entry:efi_main /nodefaultlib

C_SRCS   := $(wildcard kernel/*.c)
ASM_SRCS := $(wildcard kernel/*.asm)
OBJS     := $(patsubst kernel/%.c,$(BUILD)/%.obj,$(C_SRCS)) \
            $(patsubst kernel/%.asm,$(BUILD)/%.asm.obj,$(ASM_SRCS))

# QEMU: Das Image steckt als USB-Stick an einem xHCI-Controller (so kann
# HelloOS es selbst ansprechen und aktualisieren), dazu eine Netzwerkkarte.
QEMU_FLAGS := -machine q35 -m 512 -bios $(OVMF) -device qemu-xhci \
              -drive if=none,id=stick,format=raw,file=$(IMAGE) \
              -device usb-storage,drive=stick \
              -netdev user,id=net0 -device e1000e,netdev=net0

.PHONY: all run run-usb test check clean update-key update-package FORCE

all: $(IMAGE)

$(BUILD):
	mkdir -p $@

# Einstellungen für den Updater; die Datei ändert sich nur, wenn sich ein
# Wert ändert - sonst würde jedes "make" neu übersetzen.
$(BUILD)/update-config.h: FORCE | $(BUILD)
	@$(PYTHON) tools/update-tool.py config $@ '$(OS_BUILD)' '$(UPDATE_URL)' \
	    '$(UPDATE_KEY)' $(NOTES)

$(BUILD)/update.obj: $(BUILD)/update-config.h
$(BUILD)/claude.obj: CFLAGS += -DCLAUDE_API_URL='"$(CLAUDE_API_URL)"'

$(BUILD)/%.obj: kernel/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.asm.obj: kernel/%.asm | $(BUILD)
	$(AS) -f win64 -i$(BUILD)/ $< -o $@

$(BUILD)/extra-ca.bin: FORCE | $(BUILD)
	@$(PYTHON) tools/mkroots.py $@ $(EXTRA_CA)

$(BUILD)/tls_roots.asm.obj: kernel/tls_roots.bin $(BUILD)/extra-ca.bin

$(EFI): $(OBJS)
	$(LD) $(LDFLAGS) /out:$@ $(OBJS)

# Disk-Image mit GPT und EFI-Systempartition (FAT32), darin \EFI\BOOT\BOOTX64.EFI
$(IMAGE): $(EFI) tools/mkimage.py
	$(PYTHON) tools/mkimage.py $(EFI) $@

run: $(IMAGE)
	$(QEMU) $(QEMU_FLAGS) -serial stdio

run-usb: $(IMAGE)
	$(QEMU) $(QEMU_FLAGS) -serial stdio -device usb-kbd -device usb-mouse

update-key:
	$(PYTHON) tools/update-tool.py keygen

update-package: $(EFI)
	$(PYTHON) tools/update-tool.py pack $(EFI) $(OS_BUILD) $(BUILD)/update $(NOTES)

test: $(IMAGE)
	QEMU="$(QEMU)" OVMF="$(OVMF)" ./tools/smoke-test.sh $(IMAGE)

# Läuft auf dem Entwicklungsrechner; braucht das Python-Paket "cryptography".
check:
	$(PYTHON) tests/crypto_test.py

clean:
	rm -rf $(BUILD)

-include $(wildcard $(BUILD)/*.d)
