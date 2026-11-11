/* Interrupt Descriptor Table, CPU-Ausnahmen und Hardware-Interrupts (IRQs) */
#pragma once

#include <stdint.h>

/* Stackaufbau beim Eintritt in den C-Handler (siehe interrupts.asm). */
typedef struct {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags;
} regs_t;

typedef void (*irq_handler_t)(regs_t *regs);

void idt_init(void);

/* Handler für IRQ 0-15 anmelden und die Leitung am PIC freischalten. */
void irq_register(int irq, irq_handler_t handler);
