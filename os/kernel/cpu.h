/* Direkter Zugriff auf den Prozessor: Ports, Spezialregister, Interrupt-Flag */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline void outw(uint16_t port, uint16_t value)
{
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline void outl(uint16_t port, uint32_t value)
{
    __asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline uint32_t inl(uint16_t port)
{
    uint32_t value;
    __asm__ volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

/* Kurze Pause für langsame Hardware (Schreibzugriff auf ungenutzten Port). */
static inline void io_wait(void)
{
    outb(0x80, 0);
}

static inline uint64_t rdmsr(uint32_t msr)
{
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static inline void wrmsr(uint32_t msr, uint64_t value)
{
    __asm__ volatile("wrmsr"
                     : : "c"(msr), "a"((uint32_t)value),
                         "d"((uint32_t)(value >> 32)));
}

/* Zeitstempelzähler: zählt mit konstanter Frequenz seit dem Einschalten. */
static inline uint64_t rdtsc(void)
{
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static inline uint64_t read_cr3(void)
{
    uint64_t value;
    __asm__ volatile("mov %%cr3, %0" : "=r"(value));
    return value;
}

static inline void write_cr3(uint64_t value)
{
    __asm__ volatile("mov %0, %%cr3" : : "r"(value) : "memory");
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

static inline void cpu_pause(void)
{
    __asm__ volatile("pause");
}

/*
 * Interrupts sperren und den vorherigen Zustand zurückgeben. Zusammen mit
 * irq_restore() ist das die Sperre des Kernels: Auf einem Prozessor kann
 * zwischen den beiden Aufrufen kein anderer Task laufen.
 */
static inline uint64_t irq_save(void)
{
    uint64_t flags;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static inline void irq_restore(uint64_t flags)
{
    __asm__ volatile("push %0; popfq" : : "r"(flags) : "memory", "cc");
}
