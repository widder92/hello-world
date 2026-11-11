#include "memory.h"
#include "io.h"

#include <stdbool.h>

/* Vom Bootloader hinterlegt: Anzahl bei 0x8000, Einträge ab 0x8004. */
#define MMAP_COUNT   (*(const uint16_t *)0x8000)
#define MMAP_ENTRIES ((const mmap_entry_t *)0x8004)
#define MMAP_MAX     64

#define MIB          (1024u * 1024u)
#define HEAP_MAX     (32u * MIB)
#define BLOCK_MAGIC  0x48454C4Fu        /* "HELO" */
#define ALIGNMENT    16u

/* Jeder Heap-Block beginnt mit diesem Kopf; die Blöcke bilden eine Liste. */
typedef struct block {
    uint32_t size;                      /* Nutzgröße ohne Kopf */
    uint32_t free;
    struct block *next;
    uint32_t magic;
} block_t;

static block_t *heap;
static uint32_t heap_start, heap_size;
static uint32_t total_kb;

int memory_map_count(void)
{
    return MMAP_COUNT > MMAP_MAX ? MMAP_MAX : MMAP_COUNT;
}

const mmap_entry_t *memory_map_entry(int index)
{
    return &MMAP_ENTRIES[index];
}

void memory_init(void)
{
    uint64_t best_base = 0, best_len = 0;

    for (int i = 0; i < memory_map_count(); i++) {
        const mmap_entry_t *e = memory_map_entry(i);
        if (e->type != 1)
            continue;
        total_kb += (uint32_t)(e->length >> 10);

        /* Für den Heap: größter freier Bereich zwischen 1 MiB und 4 GiB */
        uint64_t base = e->base, end = e->base + e->length;
        if (base < MIB)
            base = MIB;
        if (end > 0x100000000ull)
            end = 0x100000000ull;
        if (end > base && end - base > best_len) {
            best_base = base;
            best_len = end - base;
        }
    }

    if (best_len < MIB) {
        /* Keine brauchbare Karte: konservativ 4 MiB oberhalb von 1 MiB annehmen */
        best_base = MIB;
        best_len = 4 * MIB;
        if (total_kb == 0)
            total_kb = 5 * 1024;
    }
    if (best_len > HEAP_MAX)
        best_len = HEAP_MAX;

    heap_start = ((uint32_t)best_base + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
    heap_size = ((uint32_t)best_len - (heap_start - (uint32_t)best_base))
                & ~(ALIGNMENT - 1);

    heap = (block_t *)heap_start;
    heap->size = heap_size - sizeof(block_t);
    heap->free = 1;
    heap->next = NULL;
    heap->magic = BLOCK_MAGIC;
}

void *kmalloc(size_t size)
{
    if (size == 0 || size > heap_size)
        return NULL;
    size = (size + ALIGNMENT - 1) & ~(ALIGNMENT - 1);

    void *result = NULL;
    uint32_t flags = irq_save();

    /* First Fit: den ersten freien Block nehmen, der groß genug ist */
    for (block_t *b = heap; b; b = b->next) {
        if (!b->free || b->size < size)
            continue;
        /* Rest abspalten, wenn er für einen eigenen Block reicht */
        if (b->size >= size + sizeof(block_t) + ALIGNMENT) {
            block_t *rest = (block_t *)((uint8_t *)(b + 1) + size);
            rest->size = b->size - size - sizeof(block_t);
            rest->free = 1;
            rest->next = b->next;
            rest->magic = BLOCK_MAGIC;
            b->size = size;
            b->next = rest;
        }
        b->free = 0;
        result = b + 1;
        break;
    }

    irq_restore(flags);
    return result;
}

void kfree(void *ptr)
{
    if (!ptr)
        return;
    block_t *block = (block_t *)ptr - 1;
    if (block->magic != BLOCK_MAGIC || block->free)
        return;                         /* ungültiger Zeiger oder doppeltes Free */

    uint32_t flags = irq_save();
    block->free = 1;

    /* Benachbarte freie Blöcke wieder verschmelzen */
    for (block_t *b = heap; b; ) {
        if (b->free && b->next && b->next->free) {
            b->size += sizeof(block_t) + b->next->size;
            b->next->magic = 0;
            b->next = b->next->next;
        } else {
            b = b->next;
        }
    }
    irq_restore(flags);
}

void memory_stats(memory_stats_t *out)
{
    uint32_t flags = irq_save();
    out->total_kb = total_kb;
    out->heap_start = heap_start;
    out->heap_size = heap_size;
    out->heap_used = 0;
    out->heap_blocks = 0;
    for (block_t *b = heap; b; b = b->next) {
        out->heap_blocks++;
        out->heap_used += sizeof(block_t);
        if (!b->free)
            out->heap_used += b->size;
    }
    irq_restore(flags);
}
