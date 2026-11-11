; =============================================================================
; Interrupt-Einsprungpunkte (Vektoren 0-47: 32 CPU-Ausnahmen + 16 IRQs)
;
; Jeder Stub legt eine einheitliche Stackstruktur an (siehe regs_t in idt.h)
; und ruft dann den gemeinsamen C-Handler isr_handler() auf.
; =============================================================================
bits 32

extern isr_handler

section .text

%assign i 0
%rep 48
isr %+ i:
  ; Diese Ausnahmen legen selbst einen Fehlercode auf den Stack,
  ; für alle anderen schieben wir eine 0 nach.
  %if (i = 8) || ((i >= 10) && (i <= 14)) || (i = 17) || (i = 21) || (i = 29) || (i = 30)
  %else
    push dword 0
  %endif
    push dword i
    jmp isr_common
%assign i i+1
%endrep

isr_common:
    pusha
    push ds
    push es
    push fs
    push gs
    mov ax, 0x10                    ; Kernel-Datensegment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    cld
    push esp                        ; Zeiger auf regs_t
    call isr_handler
    add esp, 4
    pop gs
    pop fs
    pop es
    pop ds
    popa
    add esp, 8                      ; Vektornummer + Fehlercode
    iret

section .rodata
global isr_table
isr_table:
%assign i 0
%rep 48
    dd isr %+ i
%assign i i+1
%endrep
