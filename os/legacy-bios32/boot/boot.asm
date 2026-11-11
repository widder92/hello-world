; =============================================================================
; HelloOS - Bootsektor (512 Bytes, wird vom BIOS nach 0x7C00 geladen)
;
; Aufgaben:
;   1. Kernel per BIOS (int 13h) von der Platte nach 0x10000 laden
;   2. Speicherkarte per BIOS (int 15h, E820) nach 0x8000 schreiben
;   3. A20-Leitung einschalten
;   4. In den 32-Bit Protected Mode wechseln und zum Kernel springen
; =============================================================================

%ifndef KERNEL_SECTORS
%define KERNEL_SECTORS 64           ; wird vom Makefile passend gesetzt
%endif

KERNEL_SEG   equ 0x1000             ; Real-Mode-Segment -> physisch 0x10000
KERNEL_ADDR  equ 0x10000
MMAP_ADDR    equ 0x8000             ; [0x8000] = Anzahl, ab 0x8004 die Einträge
MMAP_MAX     equ 64
STACK_TOP    equ 0x80000

bits 16
org 0x7C00

start:
    jmp 0x0000:init                 ; CS auf 0 normalisieren

init:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00                  ; Stack wächst unterhalb des Bootsektors
    sti
    cld
    mov [drive], dl                 ; BIOS übergibt das Boot-Laufwerk in DL

    mov si, msg_boot
    call print

    ; --- Laufwerksgeometrie abfragen (für LBA -> CHS) ------------------------
    mov ah, 0x08
    mov dl, [drive]
    xor di, di
    int 0x13
    jc .geometry_done               ; Fehler: Diskettenwerte (18/2) behalten
    and cx, 0x3F
    jz .geometry_done
    mov [sectors_per_track], cx
    movzx ax, dh
    inc ax
    mov [heads], ax
.geometry_done:

    ; --- Kernel laden: Sektor für Sektor ab LBA 1 ----------------------------
    mov bp, KERNEL_SECTORS
.load:
    call read_sector
    add word [dest_seg], 0x20       ; 512 Bytes weiter
    inc word [lba]
    dec bp
    jnz .load

    ; --- Speicherkarte (E820) holen ------------------------------------------
    xor ax, ax
    mov es, ax
    mov di, MMAP_ADDR + 4
    xor ebx, ebx
    xor bp, bp                      ; Zähler der Einträge
.e820:
    mov eax, 0xE820
    mov edx, 0x534D4150             ; 'SMAP'
    mov ecx, 24
    mov dword [es:di + 20], 1
    int 0x15
    jc .e820_done
    cmp eax, 0x534D4150
    jne .e820_done
    inc bp
    add di, 24
    cmp bp, MMAP_MAX
    jae .e820_done
    test ebx, ebx
    jnz .e820
.e820_done:
    mov [MMAP_ADDR], bp

    ; --- A20 einschalten (BIOS + "Fast A20") ---------------------------------
    mov ax, 0x2401
    int 0x15
    in al, 0x92
    or al, 2
    and al, 0xFE
    out 0x92, al

    ; --- Protected Mode ------------------------------------------------------
    cli
    lgdt [gdt_desc]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:protected_mode

; -----------------------------------------------------------------------------
; read_sector: liest Sektor [lba] nach [dest_seg]:0000 (3 Versuche)
; -----------------------------------------------------------------------------
read_sector:
    mov di, 3
.retry:
    mov ax, [lba]
    xor dx, dx
    div word [sectors_per_track]
    mov cx, dx
    inc cx                          ; CL = Sektor (1-basiert)
    xor dx, dx
    div word [heads]                ; AX = Zylinder, DX = Kopf
    mov ch, al
    shl ah, 6
    or cl, ah
    mov dh, dl
    mov dl, [drive]
    mov es, [dest_seg]
    xor bx, bx
    mov ax, 0x0201                  ; AH=02 lesen, AL=1 Sektor
    int 0x13
    jnc .ok
    xor ax, ax                      ; Laufwerk zurücksetzen, nochmal versuchen
    mov dl, [drive]
    int 0x13
    dec di
    jnz .retry
    mov si, msg_error
    call print
.halt:
    hlt
    jmp .halt
.ok:
    ret

; -----------------------------------------------------------------------------
; print: gibt den nullterminierten String in DS:SI per BIOS aus
; -----------------------------------------------------------------------------
print:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp print
.done:
    ret

bits 32
protected_mode:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, STACK_TOP
    jmp 0x08:KERNEL_ADDR

; -----------------------------------------------------------------------------
; Daten
; -----------------------------------------------------------------------------
drive:              db 0
lba:                dw 1
dest_seg:           dw KERNEL_SEG
sectors_per_track:  dw 18
heads:              dw 2

msg_boot:           db "HelloOS wird geladen...", 13, 10, 0
msg_error:          db "Lesefehler!", 0

align 8
gdt:
    dq 0                            ; Null-Deskriptor
    dq 0x00CF9A000000FFFF           ; 0x08: Code, 4 GiB, 32 Bit
    dq 0x00CF92000000FFFF           ; 0x10: Daten, 4 GiB, 32 Bit
gdt_desc:
    dw gdt_desc - gdt - 1
    dd gdt

times 510 - ($ - $$) db 0
dw 0xAA55                           ; Boot-Signatur
