# HelloOS (x86-64, UEFI) - Build
#
#   make          baut build/BOOTX64.EFI und das Disk-Image build/helloos.img
#   make run      startet HelloOS in QEMU (Fenster)
#   make run-usb  wie run, aber mit USB-Tastatur und -Maus statt PS/2
#   make test     bootet ohne Bildschirm und prüft die wichtigsten Befehle
#   make check    prüft die Kryptografie gegen eine Referenzbibliothek
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

# UEFI-Programme sind PE-Dateien wie unter Windows - daher dieses Ziel.
CFLAGS  := --target=x86_64-unknown-windows -std=gnu11 -O2 -Wall -Wextra \
           -ffreestanding -fno-stack-protector -fshort-wchar -mno-red-zone \
           -mgeneral-regs-only -mno-stack-arg-probe -MMD -MP
LDFLAGS := /subsystem:efi_application /entry:efi_main /nodefaultlib

C_SRCS   := $(wildcard kernel/*.c)
ASM_SRCS := $(wildcard kernel/*.asm)
OBJS     := $(patsubst kernel/%.c,$(BUILD)/%.obj,$(C_SRCS)) \
            $(patsubst kernel/%.asm,$(BUILD)/%.asm.obj,$(ASM_SRCS))

QEMU_FLAGS := -machine q35 -m 512 -bios $(OVMF) \
              -drive format=raw,file=$(IMAGE) -net none

.PHONY: all run run-usb test check clean

all: $(IMAGE)

$(BUILD):
	mkdir -p $@

$(BUILD)/%.obj: kernel/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.asm.obj: kernel/%.asm | $(BUILD)
	$(AS) -f win64 $< -o $@

$(EFI): $(OBJS)
	$(LD) $(LDFLAGS) /out:$@ $(OBJS)

# Disk-Image mit GPT und EFI-Systempartition (FAT32), darin \EFI\BOOT\BOOTX64.EFI
$(IMAGE): $(EFI) tools/mkimage.py
	$(PYTHON) tools/mkimage.py $(EFI) $@

run: $(IMAGE)
	$(QEMU) $(QEMU_FLAGS) -serial stdio

run-usb: $(IMAGE)
	$(QEMU) $(QEMU_FLAGS) -serial stdio \
	    -device qemu-xhci -device usb-kbd -device usb-mouse

test: $(IMAGE)
	QEMU="$(QEMU)" OVMF="$(OVMF)" ./tools/smoke-test.sh $(IMAGE)

# Läuft auf dem Entwicklungsrechner; braucht das Python-Paket "cryptography".
check:
	$(PYTHON) tests/crypto_test.py

clean:
	rm -rf $(BUILD)

-include $(wildcard $(BUILD)/*.d)
