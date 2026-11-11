/*
 * Mehrzeiliges Textfeld: der Kern des Editors, auch für Formularfelder im
 * Browser. Der Text ist ein zusammenhängender UTF-8-Puffer, die Schreibmarke
 * eine Byte-Position darin; Zeilen werden nicht umbrochen.
 */
#pragma once

#include "gfx.h"

#define TEXTEDIT_LINE_H (FONT_H + 1)
#define TEXTEDIT_MAX    (4u * 1024 * 1024)

typedef struct {
    char *text;
    size_t len, capacity;
    size_t cursor;
    int wanted_column;                  /* Spalte, die Pfeil hoch/runter anpeilen */
    size_t top;                         /* Anfang der ersten sichtbaren Zeile */
    int left;                           /* erste sichtbare Spalte */
    bool modified;
    int columns, rows;                  /* Größe des sichtbaren Ausschnitts in Zeichen */
} textedit_t;

void textedit_init(textedit_t *t, int columns, int rows);
void textedit_free(textedit_t *t);
/* Neue Größe des sichtbaren Ausschnitts; die Schreibmarke bleibt im Blick. */
void textedit_resize(textedit_t *t, int columns, int rows);

/* Übernimmt einen Puffer (kmalloc, capacity Bytes) als neuen Inhalt. */
void textedit_adopt(textedit_t *t, char *text, size_t len, size_t capacity);
/* Wie textedit_adopt(), aber mit einer eigenen Kopie; false bei Speichermangel. */
bool textedit_set(textedit_t *t, const char *text, size_t len);

/* Verarbeitet eine Taste; false, wenn sie mit dem Text nichts zu tun hat. */
bool textedit_key(textedit_t *t, int key, int mods);
/* Setzt die Schreibmarke; x und y in Pixeln relativ zum Textbereich. */
void textedit_click(textedit_t *t, int x, int y);

/* Zeile und Spalte der Schreibmarke, ab 1 gezählt. */
void textedit_position(const textedit_t *t, int *line, int *column);

/* Breite und Höhe der gezeichneten Fläche in Pixeln. */
int textedit_width(const textedit_t *t, bool numbers);
int textedit_height(const textedit_t *t);
/* Zeichnet den Ausschnitt ab (x, y); numbers: mit Zeilennummern am linken Rand. */
void textedit_draw(const textedit_t *t, surface_t *s, int x, int y, bool numbers);
