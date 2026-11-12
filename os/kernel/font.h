/*
 * Schriften für die Oberfläche: Text in echter, unterschiedlich breiter
 * Schrift und in jeder Größe - gezeichnet aus den eingebauten Schriftdateien
 * (DejaVu Sans, normal und fett; kursiv wird daraus geneigt).
 *
 * Gerasterte Zeichen bleiben in einem Zwischenspeicher, damit jedes nur
 * einmal berechnet werden muss.
 */
#pragma once

#include "gfx.h"

#include <stdbool.h>
#include <stddef.h>

enum { FONT_BOLD = 1, FONT_ITALIC = 2 };

/* Übliche Größe für Beschriftungen der Oberfläche (passt in eine 16-Pixel-Zeile) */
#define FONT_UI_SIZE 13

/* Liest die eingebauten Schriften ein; ohne sie bleibt es bei der Rasterschrift. */
void font_init(void);
bool font_available(void);

/* Höhe über und unter der Grundlinie sowie der übliche Zeilenabstand, in Pixeln. */
void font_metrics(int size, int *ascent, int *descent, int *line_height);

/* Breite von len Bytes Text (UTF-8) in Pixeln. */
int font_width(const char *text, size_t len, int size, int style);

/*
 * Zeichnet Text; (x, baseline) ist der Anfang der Grundlinie. Liefert die
 * Breite des Gezeichneten. Beachtet den Ausschnitt (clip) der Fläche.
 */
int font_draw(surface_t *s, int x, int baseline, const char *text, size_t len,
              int size, int style, color_t color);

/*
 * Für Beschriftungen: wie gfx_text() mit (x, y) als linker oberer Ecke einer
 * 16 Pixel hohen Zeile, aber in der Schrift der Oberfläche. Ohne Schriften
 * zeichnen sie mit der Rasterschrift.
 */
int ui_text(surface_t *s, int x, int y, const char *text, color_t color, bool bold);
int ui_text_width(const char *text, bool bold);
