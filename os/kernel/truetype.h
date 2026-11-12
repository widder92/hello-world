/*
 * TrueType-Schriften lesen und Zeichen in jeder Größe rastern.
 *
 * Eine Schriftdatei beschreibt jedes Zeichen als Umriss aus Geraden und
 * Bögen. Dieser Teil sucht zu einem Zeichen den Umriss, bringt ihn auf die
 * gewünschte Größe und füllt ihn - mit weichen Kanten: Jeder Bildpunkt
 * bekommt den Anteil, zu dem der Umriss ihn bedeckt (0 bis 255).
 *
 * Reine Rechenarbeit mit ganzen Zahlen, ohne Hardware; läuft deshalb auch im
 * Test auf dem Entwicklungsrechner (tests/font_test.c). Hinting (das Anpassen
 * der Umrisse ans Pixelraster) gibt es nicht.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const uint8_t *data;
    size_t size;
    /* Stellen der benötigten Tabellen in der Datei */
    uint32_t glyf, glyf_size, loca, loca_size, hmtx, hmtx_size;
    uint32_t cmap4, cmap12;             /* Zuordnung Zeichen -> Umriss; 0: fehlt */
    int units_per_em;
    int ascent, descent, line_gap;      /* in Einheiten der Schrift; descent negativ */
    int glyph_count, metric_count;
    bool long_loca;
} ttf_t;

/* Ein gerastertes Zeichen */
typedef struct {
    int width, height;                  /* Größe der Deckkraft-Karte; 0: leer (z. B. Leerzeichen) */
    int left;                           /* Abstand der Karte von der Schreibposition nach rechts */
    int top;                            /* Abstand der Oberkante von der Grundlinie nach unten (meist negativ) */
    uint8_t *coverage;                  /* width * height Werte, mit kfree() freigeben */
} ttf_bitmap_t;

/* Prüft die Datei und merkt sich die Tabellen. Die Daten müssen erhalten bleiben. */
bool ttf_open(ttf_t *font, const uint8_t *data, size_t size);

/* Nummer des Umrisses für ein Zeichen; 0, wenn die Schrift es nicht kennt. */
int ttf_glyph(const ttf_t *font, uint32_t codepoint);

/* Vorschub nach dem Zeichen in Bildpunkten bei der Schriftgröße size (gerundet). */
int ttf_advance(const ttf_t *font, int glyph, int size);

/* Höhe über und unter der Grundlinie sowie Zeilenabstand, in Bildpunkten. */
void ttf_line_metrics(const ttf_t *font, int size, int *ascent, int *descent, int *line_height);

/*
 * Rastert ein Zeichen in der Größe size (Höhe des Schriftgevierts in
 * Bildpunkten). oblique neigt es nach rechts (für Kursivschrift ohne eigene
 * Schriftdatei). Liefert false bei Speichermangel oder beschädigter Schrift.
 */
bool ttf_render(const ttf_t *font, int glyph, int size, bool oblique, ttf_bitmap_t *out);
