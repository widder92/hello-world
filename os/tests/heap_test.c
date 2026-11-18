/*
 * Test für den Kernel-Heap (kernel/heap.c) auf dem Entwicklungsrechner:
 * zufälliges Anfordern und Freigeben mit Prüfung der Inhalte und aller
 * Verkettungen - und die Fälle, die nichts anrichten dürfen (doppeltes
 * Freigeben, fremde Zeiger, voller Speicher).
 */
#include "../kernel/heap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int checks, failures;

static void check(int ok, const char *what)
{
    checks++;
    if (!ok) {
        failures++;
        printf("FEHLER: %s\n", what);
    }
}

#define ARENA (24u * 1024 * 1024)
#define LIVE  30000

static struct {
    unsigned char *p;
    size_t size;
    unsigned char fill;
} live[LIVE];

static unsigned seed = 4711;

static unsigned rnd(void)
{
    seed = seed * 1103515245u + 12345u;
    return seed >> 8;
}

static int intact(int i)
{
    for (size_t k = 0; k < live[i].size; k++) {
        if (live[i].p[k] != live[i].fill)
            return 0;
    }
    return 1;
}

int main(void)
{
    unsigned char *arena = aligned_alloc(16, ARENA);
    uint64_t used, blocks;
    int count = 0, bad = 0, misaligned = 0, outside = 0;

    heap_init(arena, ARENA);
    heap_usage(&used, &blocks);
    check(heap_check() && blocks == 1 && used == 32, "frisch eingerichtet: ein freier Block");
    check(heap_alloc(0) == NULL, "Größe 0: nichts");
    check(heap_alloc(ARENA) == NULL && heap_alloc((size_t)-1) == NULL, "zu groß: nichts");

    void *a = heap_alloc(1), *b = heap_alloc(17), *c = heap_alloc(5000);
    check(a && b && c && ((uintptr_t)a & 15) == 0 && ((uintptr_t)b & 15) == 0 && ((uintptr_t)c & 15) == 0,
          "kleine und große Blöcke, auf 16 ausgerichtet");
    check(heap_check(), "Verkettungen stimmen nach den ersten Anforderungen");
    heap_free(b);
    heap_free(b);                       /* doppelt */
    heap_free(NULL);
    heap_free((char *)c + 16);          /* mitten in einem Block */
    heap_free((char *)c + 8);           /* schief */
    heap_free(arena - 64);              /* außerhalb */
    heap_free(arena + ARENA + 64);
    int local = 0;
    heap_free(&local);
    check(heap_check(), "doppeltes Freigeben und fremde Zeiger richten nichts an");
    memset(c, 0xEE, 5000);
    void *b2 = heap_alloc(17);
    check(b2 == b, "ein freigegebener Block wird wiederverwendet");
    heap_free(a);
    heap_free(b2);
    heap_free(c);
    heap_usage(&used, &blocks);
    check(heap_check() && blocks == 1 && used == 32, "alles freigegeben: wieder ein einziger Block");

    /* voller Speicher: Es gibt NULL - und danach geht es weiter */
    int n = 0;
    void *big[64];
    while (n < 64 && (big[n] = heap_alloc(1024 * 1024)) != NULL)
        n++;
    check(n >= 20 && n < 24 && heap_alloc(2 * 1024 * 1024) == NULL, "Speicher voll: NULL statt Unsinn");
    check(heap_alloc(64) != NULL, "kleine Blöcke gehen noch");
    for (int i = 0; i < n; i += 2)
        heap_free(big[i]);
    check(heap_alloc(2 * 1024 * 1024) == NULL, "zerstückelt: Zwei Megabyte am Stück gibt es nicht");
    for (int i = 1; i < n; i += 2)
        heap_free(big[i]);
    check(heap_check() && heap_alloc(20 * 1024 * 1024) != NULL, "Nachbarn verschmelzen wieder zu einem großen Stück");
    heap_init(arena, ARENA);

    /* Zufall: anfordern, füllen, prüfen, freigeben */
    for (int round = 0; round < 600000; round++) {
        unsigned r = rnd();
        if ((r % 100 < 55 || count == 0) && count < LIVE) {
            unsigned kind = rnd() % 100;
            size_t size = kind < 70 ? 1 + rnd() % 200 : kind < 95 ? 1 + rnd() % 6000
                        : kind < 99 ? 1 + rnd() % 200000 : 1 + rnd() % 2000000;
            unsigned char *p = heap_alloc(size);
            if (!p)
                continue;               /* voll: kommt vor */
            misaligned += ((uintptr_t)p & 15) != 0;
            outside += p < arena || p + size > arena + ARENA;
            live[count].p = p;
            live[count].size = size;
            live[count].fill = (unsigned char)rnd();
            memset(p, live[count].fill, size);
            count++;
        } else {
            int i = (int)(rnd() % (unsigned)count);
            bad += !intact(i);
            heap_free(live[i].p);
            live[i] = live[--count];
        }
        if (round % 20000 == 0 && !heap_check()) {
            bad++;
            break;
        }
    }
    check(bad == 0, "600000 zufällige Schritte: Kein Block überschneidet einen anderen");
    check(misaligned == 0 && outside == 0, "alle Blöcke ausgerichtet und im Bereich");
    check(heap_check(), "Verkettungen stimmen danach");
    for (int i = 0; i < count; i++) {
        bad += !intact(i);
        heap_free(live[i].p);
    }
    heap_usage(&used, &blocks);
    check(bad == 0 && heap_check() && blocks == 1 && used == 32, "am Ende alles zurück: ein einziger freier Block");

    /* Tempo: Hunderttausende kleine Blöcke - früher wurde dafür jedes Mal die ganze Liste abgelaufen */
    static void *many[300000];
    clock_t start = clock();
    for (int i = 0; i < 300000; i++)
        many[i] = heap_alloc(16 + (size_t)(i % 5) * 16);
    for (int i = 0; i < 300000; i += 2)
        heap_free(many[i]);
    for (int i = 0; i < 300000; i += 2)
        many[i] = heap_alloc(48);
    for (int i = 0; i < 300000; i++)
        heap_free(many[i]);
    double seconds = (double)(clock() - start) / CLOCKS_PER_SEC;
    heap_usage(&used, &blocks);
    check(blocks == 1 && seconds < 2.0, "900000 Schritte mit 300000 lebenden Blöcken in unter zwei Sekunden");

    free(arena);
    printf("%d Prüfungen am Kernel-Heap, %d Fehler\n", checks, failures);
    return failures != 0;
}
