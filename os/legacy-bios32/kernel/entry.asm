; =============================================================================
; Kernel-Einstieg: steht ganz am Anfang des Kernel-Images (0x10000).
; Der Bootloader springt hierher, bereits im 32-Bit Protected Mode.
; =============================================================================
bits 32

section .text.entry
global _start
extern kmain
extern __bss_start
extern __bss_end

_start:
    cli
    mov esp, 0x80000                ; Kernel-Stack (wächst nach unten)

    ; BSS nullen - im flachen Binärimage ist dieser Bereich nicht enthalten
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    xor eax, eax
    cld
    rep stosb

    call kmain

.hang:
    cli
    hlt
    jmp .hang
