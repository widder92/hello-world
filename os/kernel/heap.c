/* Der Kernel-Heap - siehe heap.h */
#include "heap.h"

#define BLOCK_MAGIC 0x48454C4Fu         /* "HELO" */
#define ALIGNMENT   16u
#define SMALL_BINS  64                  /* Größen 16, 32, ... 1024: je eine eigene Liste */
#define SMALL_MAX   (SMALL_BINS * ALIGNMENT)
#define LARGE_SPLIT 4                   /* darüber: je Verdopplung vier Listen */
#define BINS        (SMALL_BINS + 54 * LARGE_SPLIT)
#define SCAN_MAX    24                  /* so viele Blöcke einer Liste werden höchstens geprüft */

/*
 * Jeder Block beginnt mit diesem Kopf (32 Bytes). Der nächste Block folgt
 * unmittelbar hinter den Nutzdaten; den davor nennt prev.
 */
typedef struct block {
    uint64_t size;                      /* Nutzgröße ohne Kopf, Vielfaches von 16 */
    struct block *prev;                 /* Block unmittelbar davor im Speicher, oder NULL */
    uint32_t free;
    uint32_t magic;
    uint64_t pad;
} block_t;

/* Freie Blöcke tragen in ihren Nutzdaten die Verkettung ihrer Liste. */
typedef struct {
    block_t *next, *prev;
} links_t;

static uint8_t *heap_base, *heap_end;
static block_t *bins[BINS];
static uint64_t bin_map[(BINS + 63) / 64];      /* welche Listen nicht leer sind */
static uint64_t used_bytes, block_count;

static links_t *links(block_t *b)
{
    return (links_t *)(b + 1);
}

static block_t *after(block_t *b)
{
    uint8_t *next = (uint8_t *)(b + 1) + b->size;
    return next < heap_end ? (block_t *)next : NULL;
}

/* Die Liste, in die ein freier Block dieser Größe gehört */
static unsigned bin_of(uint64_t size)
{
    if (size <= SMALL_MAX)
        return (unsigned)(size / ALIGNMENT) - 1;
    /* size liegt in [2^k, 2^(k+1)); k >= 10. Vier gleich breite Stufen je Verdopplung. */
    unsigned k = 63 - (unsigned)__builtin_clzll(size);
    unsigned step = (unsigned)((size >> (k - 2)) & (LARGE_SPLIT - 1));
    unsigned bin = SMALL_BINS + (k - 10) * LARGE_SPLIT + step;
    return bin < BINS ? bin : BINS - 1;
}

static void bin_insert(block_t *b)
{
    unsigned bin = bin_of(b->size);
    links(b)->prev = NULL;
    links(b)->next = bins[bin];
    if (bins[bin])
        links(bins[bin])->prev = b;
    bins[bin] = b;
    bin_map[bin / 64] |= 1ull << (bin % 64);
}

static void bin_remove(block_t *b)
{
    unsigned bin = bin_of(b->size);
    links_t *l = links(b);
    if (l->prev)
        links(l->prev)->next = l->next;
    else
        bins[bin] = l->next;
    if (l->next)
        links(l->next)->prev = l->prev;
    if (!bins[bin])
        bin_map[bin / 64] &= ~(1ull << (bin % 64));
}

/* Die erste nicht leere Liste ab from, oder BINS */
static unsigned next_bin(unsigned from)
{
    for (unsigned word = from / 64; word < sizeof(bin_map) / sizeof(bin_map[0]); word++) {
        uint64_t bits = bin_map[word];
        if (word == from / 64)
            bits &= ~0ull << (from % 64);
        if (bits)
            return word * 64 + (unsigned)__builtin_ctzll(bits);
    }
    return BINS;
}

void heap_init(void *base, size_t size)
{
    heap_base = base;
    heap_end = heap_base + size;
    for (unsigned i = 0; i < BINS; i++)
        bins[i] = NULL;
    for (unsigned i = 0; i < sizeof(bin_map) / sizeof(bin_map[0]); i++)
        bin_map[i] = 0;
    block_t *first = base;
    first->size = size - sizeof(block_t);
    first->prev = NULL;
    first->free = 1;
    first->magic = BLOCK_MAGIC;
    first->pad = 0;
    used_bytes = sizeof(block_t);
    block_count = 1;
    bin_insert(first);
}

void *heap_alloc(size_t size)
{
    if (size == 0 || !heap_base || size > (size_t)(heap_end - heap_base))
        return NULL;
    size = (size + ALIGNMENT - 1) & ~(size_t)(ALIGNMENT - 1);

    /*
     * In der eigenen Liste passen bei kleinen Größen alle Blöcke; bei großen
     * nur manche (die Liste fasst einen Größenbereich). In jeder höheren
     * Liste passt dann jeder.
     */
    block_t *found = NULL;
    unsigned bin = bin_of(size);
    if (size <= SMALL_MAX) {
        found = bins[bin];
    } else {
        int scanned = 0;
        for (block_t *b = bins[bin]; b && scanned < SCAN_MAX; b = links(b)->next, scanned++) {
            if (b->size >= size) {
                found = b;
                break;
            }
        }
    }
    if (!found) {
        unsigned higher = next_bin(bin + 1);
        if (higher >= BINS)
            return NULL;
        found = bins[higher];
        if (found->size < size)
            return NULL;                /* kann nur die letzte, offene Liste treffen */
    }
    bin_remove(found);

    /* Rest abspalten, wenn er für einen eigenen Block reicht */
    if (found->size >= size + sizeof(block_t) + ALIGNMENT) {
        block_t *rest = (block_t *)((uint8_t *)(found + 1) + size);
        rest->size = found->size - size - sizeof(block_t);
        rest->prev = found;
        rest->free = 1;
        rest->magic = BLOCK_MAGIC;
        rest->pad = 0;
        found->size = size;
        block_t *behind = after(rest);
        if (behind)
            behind->prev = rest;
        bin_insert(rest);
        block_count++;
        used_bytes += sizeof(block_t);
    }
    found->free = 0;
    used_bytes += found->size;
    return found + 1;
}

void heap_free(void *ptr)
{
    uint8_t *p = ptr;
    if (!ptr || !heap_base || p < heap_base + sizeof(block_t) || p >= heap_end ||
        ((uintptr_t)p & (ALIGNMENT - 1)))
        return;
    block_t *b = (block_t *)ptr - 1;
    if (b->magic != BLOCK_MAGIC || b->free)
        return;                         /* ungültiger Zeiger oder doppeltes Freigeben */

    used_bytes -= b->size;
    b->free = 1;

    /* Mit freien Nachbarn verschmelzen: erst dem dahinter, dann dem davor */
    block_t *next = after(b);
    if (next && next->free) {
        bin_remove(next);
        b->size += sizeof(block_t) + next->size;
        next->magic = 0;
        block_count--;
        used_bytes -= sizeof(block_t);
    }
    block_t *prev = b->prev;
    if (prev && prev->free) {
        bin_remove(prev);
        prev->size += sizeof(block_t) + b->size;
        b->magic = 0;
        b = prev;
        block_count--;
        used_bytes -= sizeof(block_t);
    }
    next = after(b);
    if (next)
        next->prev = b;
    bin_insert(b);
}

void heap_usage(uint64_t *used, uint64_t *blocks)
{
    *used = used_bytes;
    *blocks = block_count;
}

bool heap_check(void)
{
    uint64_t used = 0, count = 0, free_blocks = 0, listed = 0;
    block_t *prev = NULL;
    if (!heap_base)
        return false;
    for (block_t *b = (block_t *)heap_base; b; b = after(b)) {
        if (b->magic != BLOCK_MAGIC || b->prev != prev || (b->size & (ALIGNMENT - 1)) ||
            b->size < ALIGNMENT || (uint8_t *)(b + 1) + b->size > heap_end)
            return false;
        if (b->free && prev && prev->free)
            return false;               /* zwei freie Nachbarn: nicht verschmolzen */
        used += sizeof(block_t) + (b->free ? 0 : b->size);
        free_blocks += b->free != 0;
        count++;
        prev = b;
    }
    if ((uint8_t *)(prev + 1) + prev->size != heap_end || used != used_bytes || count != block_count)
        return false;
    for (unsigned bin = 0; bin < BINS; bin++) {
        bool marked = bin_map[bin / 64] >> (bin % 64) & 1;
        if (marked != (bins[bin] != NULL))
            return false;
        block_t *before = NULL;
        for (block_t *b = bins[bin]; b; b = links(b)->next) {
            if (!b->free || b->magic != BLOCK_MAGIC || bin_of(b->size) != bin ||
                links(b)->prev != before || ++listed > free_blocks)
                return false;
            before = b;
        }
    }
    return listed == free_blocks;
}
