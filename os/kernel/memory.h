/* Speicherverwaltung: Kernel-Heap, Seitenvergabe und Seitentabellen */
#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint64_t total_bytes;               /* nutzbarer RAM laut Firmware */
    uint64_t heap_start;
    uint64_t heap_size;
    uint64_t heap_used;                 /* inklusive Verwaltungsdaten */
    uint64_t heap_blocks;
} memory_stats_t;

void memory_init(void);

void *kmalloc(size_t size);
void *kzalloc(size_t size);             /* wie kmalloc, mit Nullen gefüllt */
void kfree(void *ptr);

/*
 * Ganze 4-KiB-Seiten, mit Nullen gefüllt. Für Hardware-Strukturen und
 * Seitentabellen; sie werden nie wieder freigegeben.
 */
void *alloc_pages(size_t count);

/*
 * Sorgt dafür, dass der physische Bereich ansprechbar ist (1:1 abgebildet).
 * Nötig für Geräte, deren Register außerhalb des RAMs liegen.
 */
void vmm_map(uint64_t address, uint64_t size);

/*
 * Wie vmm_map(), aber für den Bildspeicher: Schreibzugriffe werden vom
 * Prozessor gebündelt ("Write Combining"). Ohne das wäre jeder einzelne
 * Pixel auf echter Hardware quälend langsam.
 */
void vmm_map_framebuffer(uint64_t address, uint64_t size);

void memory_stats(memory_stats_t *out);
