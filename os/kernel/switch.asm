; =============================================================================
; void context_switch(uint32_t *old_esp, uint32_t new_esp);
;
; Sichert die "callee-saved" Register des laufenden Tasks auf dessen Stack,
; merkt sich den Stackpointer und wechselt auf den Stack des nächsten Tasks.
; Das abschließende `ret` kehrt dann im neuen Task dorthin zurück, wo dieser
; zuletzt selbst context_switch() aufgerufen hat.
; =============================================================================
bits 32

section .text
global context_switch

context_switch:
    mov eax, [esp + 4]              ; old_esp
    mov edx, [esp + 8]              ; new_esp
    push ebp
    push ebx
    push esi
    push edi
    mov [eax], esp
    mov esp, edx
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret
