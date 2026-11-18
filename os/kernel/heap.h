/*
 * Der Kernel-Heap: verwaltet einen zusammenhängenden Speicherbereich in
 * Blöcken beliebiger Größe.
 *
 * Freie Blöcke hängen nach Größe sortiert in Listen, und jeder Block kennt
 * seine beiden Nachbarn im Speicher: Anfordern und Freigeben dauern damit
 * gleich lang, egal wie viele Blöcke es gibt (früher wurde jedes Mal die
 * ganze Blockliste abgelaufen - bei Seiten mit Skripten sind das
 * Hunderttausende).
 *
 * Reine Rechenarbeit ohne Hardware; wer sie aufruft, sorgt für die Sperre.
 * Läuft auch im Test auf dem Entwicklungsrechner (tests/heap_test.c).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Richtet den Bereich ein; base und size sind Vielfache von 16. */
void heap_init(void *base, size_t size);

/* Liefert size Bytes (auf 16 ausgerichtet) oder NULL. */
void *heap_alloc(size_t size);

/* Gibt einen Block zurück; fremde Zeiger und doppeltes Freigeben werden übergangen. */
void heap_free(void *ptr);

/* Belegter Speicher samt Verwaltungsdaten und Zahl der Blöcke */
void heap_usage(uint64_t *used, uint64_t *blocks);

/* Prüft alle Verkettungen (für Tests); false, wenn etwas nicht stimmt. */
bool heap_check(void);
