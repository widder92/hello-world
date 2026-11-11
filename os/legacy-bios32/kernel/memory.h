/* Speicherverwaltung: BIOS-Speicherkarte und Kernel-Heap */
#pragma once

#include <stddef.h>
#include <stdint.h>

/* Ein Eintrag der BIOS-Speicherkarte (int 15h, E820). */
typedef struct {
    uint64_t base;
    uint64_t length;
    uint32_t type;                      /* 1 = nutzbarer RAM */
    uint32_t acpi;
} __attribute__((packed)) mmap_entry_t;

typedef struct {
    uint32_t total_kb;                  /* nutzbarer RAM laut BIOS */
    uint32_t heap_start;
    uint32_t heap_size;
    uint32_t heap_used;                 /* inklusive Verwaltungsdaten */
    uint32_t heap_blocks;
} memory_stats_t;

void memory_init(void);

void *kmalloc(size_t size);
void kfree(void *ptr);

void memory_stats(memory_stats_t *out);

int memory_map_count(void);
const mmap_entry_t *memory_map_entry(int index);
