/* Port-I/O und Interrupt-Steuerung (x86) */
#pragma once

#include <stdint.h>

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline void outw(uint16_t port, uint16_t value)
{
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

/* Kurze Pause für langsame Hardware (Schreibzugriff auf ungenutzten Port). */
static inline void io_wait(void)
{
    outb(0x80, 0);
}

static inline void irq_enable(void)
{
    __asm__ volatile("sti");
}

static inline void irq_disable(void)
{
    __asm__ volatile("cli");
}

static inline void cpu_halt(void)
{
    __asm__ volatile("hlt");
}

/* Interrupts sperren und den vorherigen Zustand zurückgeben ... */
static inline uint32_t irq_save(void)
{
    uint32_t flags;
    __asm__ volatile("pushf; pop %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

/* ... und ihn später wiederherstellen. Dient als einfache Kernel-Sperre. */
static inline void irq_restore(uint32_t flags)
{
    __asm__ volatile("push %0; popf" : : "r"(flags) : "memory", "cc");
}
