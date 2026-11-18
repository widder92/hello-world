; =============================================================================
; Die wenigen Dinge, die sich nicht in C ausdrücken lassen.
;
; Aufrufkonvention ist die von UEFI (Microsoft x64): Argumente in RCX, RDX,
; R8, R9; der Aufrufer reserviert 32 Bytes "Shadow Space" auf dem Stack.
; =============================================================================
bits 64
default rel

extern interrupt_handler

section .text

; -----------------------------------------------------------------------------
; void stack_switch(void *stack_top, void (*entry)(void *), void *arg)
; Wechselt auf einen neuen Stack und ruft entry(arg) auf. Kehrt nie zurück.
; -----------------------------------------------------------------------------
global stack_switch
stack_switch:
    mov rsp, rcx
    mov rcx, r8
    sub rsp, 32
    call rdx
.hang:
    cli
    hlt
    jmp .hang

; -----------------------------------------------------------------------------
; void gdt_load(void *gdt_pointer)
; Lädt die Segmenttabelle und stellt alle Segmentregister darauf um.
; -----------------------------------------------------------------------------
global gdt_load
gdt_load:
    lgdt [rcx]
    mov ax, 0x10                    ; Datensegment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    pop rax                         ; Rücksprungadresse
    push 0x08                       ; Codesegment
    push rax
    retfq                           ; "far return" lädt CS neu

; -----------------------------------------------------------------------------
; void context_switch(uint64_t *old_rsp, uint64_t new_rsp)
;
; Sichert die Register, die eine Funktion erhalten muss, auf dem Stack des
; laufenden Tasks, merkt sich dessen Stackpointer und wechselt auf den Stack
; des nächsten Tasks. Das `ret` kehrt dann im neuen Task dorthin zurück, wo
; dieser zuletzt selbst context_switch() aufgerufen hat.
; -----------------------------------------------------------------------------
global context_switch
context_switch:
    fxsave [r8]                 ; Kommazahl-Register des alten Tasks sichern
    push rbp
    push rbx
    push rdi
    push rsi
    push r12
    push r13
    push r14
    push r15
    mov [rcx], rsp
    mov rsp, rdx
    pop r15
    pop r14
    pop r13
    pop r12
    pop rsi
    pop rdi
    pop rbx
    pop rbp
    fxrstor [r9]                ; ... und die des neuen zurückholen
    ret

; -----------------------------------------------------------------------------
; void fpu_initial(void *area)
;
; Schaltet die Kommazahl-Befehle (x87, SSE) ein, bringt sie in den
; Grundzustand und legt diesen in area ab (512 Bytes, auf 16 ausgerichtet) -
; als Vorlage für neue Tasks. Der Kernel selbst rechnet nur mit ganzen
; Zahlen; allein die JavaScript-Maschine braucht Kommazahlen.
; -----------------------------------------------------------------------------
global fpu_initial
fpu_initial:
    mov rax, cr0
    and rax, ~0xC               ; EM und TS aus: Befehle erlaubt
    or rax, 0x2                 ; MP
    mov cr0, rax
    mov rax, cr4
    or rax, 0x600               ; OSFXSR, OSXMMEXCPT
    mov cr4, rax
    fninit
    sub rsp, 8
    mov dword [rsp], 0x1F80     ; alle Ausnahmen maskiert, runden zur nächsten Zahl
    ldmxcsr [rsp]
    add rsp, 8
    fxsave [rcx]
    ret

; -----------------------------------------------------------------------------
; void call_on_stack(void (*fn)(void *arg), void *arg, void *stack_top)
;
; Ruft fn(arg) auf einem anderen, größeren Stack auf und kehrt danach auf den
; bisherigen zurück. Für Arbeit, die tief verschachtelt rechnet (Skripte).
; -----------------------------------------------------------------------------
global call_on_stack
call_on_stack:
    push rbp
    mov rbp, rsp
    mov rsp, r8
    and rsp, -16
    sub rsp, 32                 ; "Shadow Space" der Aufrufkonvention
    mov rax, rcx
    mov rcx, rdx
    call rax
    mov rsp, rbp
    pop rbp
    ret

; -----------------------------------------------------------------------------
; Interrupt-Einsprungpunkte für alle 256 Vektoren. Jeder Stub sorgt für einen
; einheitlichen Stackaufbau (siehe regs_t in interrupts.h) und ruft dann
; interrupt_handler(regs) auf.
; -----------------------------------------------------------------------------
%assign i 0
%rep 256
isr %+ i:
  ; Diese Ausnahmen legen selbst einen Fehlercode auf den Stack,
  ; für alle anderen schieben wir eine 0 nach.
  %if (i = 8) || ((i >= 10) && (i <= 14)) || (i = 17) || (i = 21) || (i = 29) || (i = 30)
  %else
    push 0
  %endif
    push i
    jmp isr_common
%assign i i+1
%endrep

isr_common:
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov rcx, rsp                    ; Argument: Zeiger auf regs_t
    mov rbp, rsp
    and rsp, -16                    ; Stack ausrichten + Shadow Space
    sub rsp, 32
    cld
    call interrupt_handler
    mov rsp, rbp
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax
    add rsp, 16                     ; Vektornummer + Fehlercode
    iretq

section .rdata
global isr_table
isr_table:
%assign i 0
%rep 256
    dq isr %+ i
%assign i i+1
%endrep
