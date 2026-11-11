# HelloOS - Build
#
#   make          baut build/helloos.img (bootfähiges Disk-Image)
#   make run      startet das Image in QEMU (Fenster)
#   make run-tty  startet es im Terminal über die serielle Konsole
#   make test     bootet ohne Bildschirm und prüft die wichtigsten Befehle
#   make clean    räumt auf

BUILD   := build
IMAGE   := $(BUILD)/helloos.img

CC      := gcc
AS      := nasm
LD      := ld
OBJCOPY := objcopy
QEMU    := qemu-system-i386

CFLAGS  := -m32 -march=i686 -std=gnu11 -O2 -g -Wall -Wextra \
           -ffreestanding -fno-pie -fno-pic -fno-stack-protector \
           -fno-asynchronous-unwind-tables -fno-tree-loop-distribute-patterns \
           -mgeneral-regs-only -MMD -MP
LDFLAGS := -m elf_i386 -T linker.ld -nostdlib -z noexecstack --build-id=none

# Neuere Linker warnen vor Segmenten, die zugleich schreib- und ausführbar
# sind - bei einem flachen Kernel-Image ohne Paging ist das Absicht.
LDFLAGS += $(shell $(LD) --help 2>/dev/null | grep -q no-warn-rwx-segments \
             && echo --no-warn-rwx-segments)

C_SRCS   := $(wildcard kernel/*.c)
ASM_SRCS := $(wildcard kernel/*.asm)
OBJS     := $(patsubst kernel/%.asm,$(BUILD)/%.asm.o,$(ASM_SRCS)) \
            $(patsubst kernel/%.c,$(BUILD)/%.o,$(C_SRCS))

# Der Kernel muss zwischen 0x10000 und dem Stack bei 0x80000 Platz finden.
MAX_KERNEL_SECTORS := 512

.PHONY: all run run-tty test clean

all: $(IMAGE)

$(BUILD):
	mkdir -p $@

$(BUILD)/%.o: kernel/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.asm.o: kernel/%.asm | $(BUILD)
	$(AS) -f elf32 $< -o $@

$(BUILD)/kernel.elf: $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

$(BUILD)/kernel.bin: $(BUILD)/kernel.elf
	$(OBJCOPY) -O binary $< $@

# Der Bootsektor muss wissen, wie viele Sektoren der Kernel belegt.
$(BUILD)/boot.bin: boot/boot.asm $(BUILD)/kernel.bin
	@sectors=$$(( ($$(wc -c < $(BUILD)/kernel.bin) + 511) / 512 )); \
	if [ $$sectors -gt $(MAX_KERNEL_SECTORS) ]; then \
		echo "Kernel zu groß: $$sectors Sektoren"; exit 1; \
	fi; \
	echo "$(AS) -f bin -DKERNEL_SECTORS=$$sectors $< -o $@"; \
	$(AS) -f bin -DKERNEL_SECTORS=$$sectors $< -o $@

# Image = Bootsektor + Kernel, aufgefüllt auf die Größe einer 1,44-MB-Diskette.
$(IMAGE): $(BUILD)/boot.bin $(BUILD)/kernel.bin
	cat $^ > $@
	truncate -s 1474560 $@

run: $(IMAGE)
	$(QEMU) -drive format=raw,file=$(IMAGE) -serial stdio

run-tty: $(IMAGE)
	@echo "Beenden mit 'shutdown' oder Strg+A, dann X"
	$(QEMU) -drive format=raw,file=$(IMAGE) -nographic

test: $(IMAGE)
	./tools/smoke-test.sh $(IMAGE)

clean:
	rm -rf $(BUILD)

-include $(wildcard $(BUILD)/*.d)
