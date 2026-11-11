#include "gdt.h"

#include <stdint.h>

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} __attribute__((packed));

struct gdt_pointer {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct gdt_entry gdt[3];
static struct gdt_pointer gdtr;

static void set_entry(int i, uint32_t base, uint32_t limit, uint8_t access,
                      uint8_t flags)
{
    gdt[i].limit_low = limit & 0xFFFF;
    gdt[i].base_low = base & 0xFFFF;
    gdt[i].base_mid = (base >> 16) & 0xFF;
    gdt[i].access = access;
    gdt[i].granularity = (uint8_t)(((limit >> 16) & 0x0F) | (flags << 4));
    gdt[i].base_high = (base >> 24) & 0xFF;
}

void gdt_init(void)
{
    set_entry(0, 0, 0, 0, 0);               /* Null-Deskriptor */
    set_entry(1, 0, 0xFFFFF, 0x9A, 0xC);    /* 0x08: Kernel-Code */
    set_entry(2, 0, 0xFFFFF, 0x92, 0xC);    /* 0x10: Kernel-Daten */

    gdtr.limit = sizeof(gdt) - 1;
    gdtr.base = (uint32_t)gdt;

    /* Neue Tabelle laden und alle Segmentregister darauf umstellen */
    __asm__ volatile(
        "lgdt %0\n"
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        "ljmp $0x08, $1f\n"
        "1:\n"
        : : "m"(gdtr) : "eax", "memory");
}
