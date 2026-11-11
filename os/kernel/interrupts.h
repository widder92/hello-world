/* Segmenttabelle, Interrupt-Tabelle, CPU-Ausnahmen und der Systemtakt */
#pragma once

#include <stdint.h>

#define TICK_HZ 1000                    /* ein Tick pro Millisekunde */

/* Stackaufbau beim Eintritt in den C-Handler (siehe lowlevel.asm). */
typedef struct {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rbx, rdx, rcx, rax;
    uint64_t int_no, err_code;
    uint64_t rip, cs, rflags, rsp, ss;
} regs_t;

/* GDT und IDT laden, den alten Interrupt-Controller (PIC) stilllegen. */
void interrupts_init(void);

/* Den Timer des Local APIC als Systemtakt starten. */
void timer_start(void);

/* Schwerer Fehler: Meldung auf Bildschirm und serieller Konsole, dann Halt. */
void panic(const char *fmt, ...)
    __attribute__((format(printf, 1, 2), noreturn));
