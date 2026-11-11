#include "memory.h"
#include "bootinfo.h"
#include "cpu.h"
#include "interrupts.h"
#include "string.h"

#define MIB         (1024ull * 1024)
#define GIB         (1024 * MIB)
#define PAGE_SIZE   4096ull
#define HEAP_MAX    GIB
#define BLOCK_MAGIC 0x48454C4Fu         /* "HELO" */
#define ALIGNMENT   16ull

/* Jeder Heap-Block beginnt mit diesem Kopf; die Blöcke bilden eine Liste. */
typedef struct block {
    uint64_t size;                      /* Nutzgröße ohne Kopf */
    struct block *next;
    uint32_t free;
    uint32_t magic;
    uint64_t pad;                       /* Kopf auf 32 Bytes auffüllen */
} block_t;

static block_t *heap;
static uint64_t heap_start, heap_size;
static uint64_t page_bottom, page_top;  /* Vorrat für alloc_pages() */
static uint64_t total_bytes;

void memory_init(void)
{
    uint64_t best_base = 0, best_len = 0;
    uint8_t *p = (uint8_t *)boot_info.mmap;
    uint8_t *end = p + boot_info.mmap_size;

    for (; p < end; p += boot_info.mmap_desc_size) {
        const efi_memory_descriptor_t *d = (const efi_memory_descriptor_t *)p;
        uint64_t base = d->physical_start;
        uint64_t len = d->number_of_pages * EFI_PAGE_SIZE;

        if (d->type >= EFI_LOADER_CODE && d->type <= EFI_CONVENTIONAL_MEMORY)
            total_bytes += len;
        if (d->type != EFI_CONVENTIONAL_MEMORY)
            continue;

        /* Für den Kernel: der größte freie Bereich oberhalb von 1 MiB */
        uint64_t limit = base + len;
        if (base < MIB)
            base = MIB;
        if (limit > 512 * GIB)
            limit = 512 * GIB;          /* so weit reichen die Seitentabellen */
        if (limit > base && limit - base > best_len) {
            best_base = base;
            best_len = limit - base;
        }
    }
    if (best_len < 16 * MIB)
        panic("Zu wenig freier Arbeitsspeicher (%llu KiB am Stück).",
              best_len / 1024);
    if (best_len > HEAP_MAX)
        best_len = HEAP_MAX;

    /* Das obere Achtel (höchstens 16 MiB) ist der Vorrat für ganze Seiten. */
    uint64_t pool = best_len / 8;
    if (pool > 16 * MIB)
        pool = 16 * MIB;
    page_top = (best_base + best_len) & ~(PAGE_SIZE - 1);
    page_bottom = (page_top - pool) & ~(PAGE_SIZE - 1);

    heap_start = (best_base + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
    heap_size = page_bottom - heap_start;

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
    uint64_t flags = irq_save();

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

void *kzalloc(size_t size)
{
    void *ptr = kmalloc(size);
    if (ptr)
        memset(ptr, 0, size);
    return ptr;
}

void kfree(void *ptr)
{
    if (!ptr)
        return;
    block_t *block = (block_t *)ptr - 1;
    if (block->magic != BLOCK_MAGIC || block->free)
        return;                         /* ungültiger Zeiger oder doppeltes Free */

    uint64_t flags = irq_save();
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

void *alloc_pages(size_t count)
{
    uint64_t flags = irq_save();
    uint64_t bytes = count * PAGE_SIZE;
    if (page_top - page_bottom < bytes)
        panic("Der Seitenvorrat ist erschöpft.");
    page_top -= bytes;
    void *pages = (void *)page_top;
    irq_restore(flags);

    memset(pages, 0, bytes);
    return pages;
}

/* Liefert die Tabelle der nächsten Ebene und legt sie bei Bedarf an. */
static uint64_t *next_table(uint64_t *table, unsigned index)
{
    if (!(table[index] & 1))
        table[index] = (uint64_t)alloc_pages(1) | 0x03;
    return (uint64_t *)(table[index] & 0x000FFFFFFFFFF000ull);
}

void vmm_map(uint64_t address, uint64_t size)
{
    const uint64_t huge = 2 * MIB;
    uint64_t first = address & ~(huge - 1);
    uint64_t last = (address + size - 1) & ~(huge - 1);

    uint64_t flags = irq_save();
    for (uint64_t a = first; ; a += huge) {
        uint64_t *pdpt = next_table(boot_info.pml4, (a >> 39) & 0x1FF);
        uint64_t *pd = next_table(pdpt, (a >> 30) & 0x1FF);
        unsigned i = (a >> 21) & 0x1FF;
        if (!(pd[i] & 1))
            pd[i] = a | 0x83;           /* vorhanden, beschreibbar, 2 MiB */
        if (a == last)
            break;
    }
    write_cr3(read_cr3());              /* Adresspuffer (TLB) leeren */
    irq_restore(flags);
}

void memory_stats(memory_stats_t *out)
{
    uint64_t flags = irq_save();
    out->total_bytes = total_bytes;
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
