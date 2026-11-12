/*
 * Stylesheets (CSS): die Gestaltungsregeln einer Seite lesen und für ein
 * Element bestimmen, was gilt - Farbe, Hintergrund, Schrift, Ausrichtung und
 * ob es überhaupt zu sehen ist.
 *
 * Unterstützt wird ein Ausschnitt, der für lesbare Seiten am meisten bringt:
 *   Auswahl      Elementname, .klasse, #kennung, verbunden ("a.b#c"),
 *                Nachfahre ("a b") und Kind ("a > b"), Listen mit Komma
 *   Rangfolge    Genauigkeit der Auswahl, Reihenfolge, !important
 *   Werte        Farben in allen üblichen Schreibweisen, var(--name)
 *   @media       Bildschirm und Breitenangaben; alles andere wird übergangen
 * Regeln mit Zuständen (:hover), Attributen ([type=x]) oder Nachbarn (a + b)
 * werden ausgelassen statt falsch angewandt.
 *
 * Reine Rechenarbeit ohne Hardware; läuft auch im Test auf dem
 * Entwicklungsrechner (tests/css_test.c).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct css_sheet css_sheet_t;

/* Ein Element der Seite mit dem, woran Regeln es erkennen */
typedef struct css_node {
    const char *tag;                    /* Elementname, Groß-/Kleinschreibung egal */
    size_t tag_len;
    const char *id;
    size_t id_len;
    const char *classes;                /* Inhalt des class-Attributs */
    size_t classes_len;
    const struct css_node *parent;      /* umgebendes Element oder NULL */
} css_node_t;

enum { CSS_COLOR = 1, CSS_BACKGROUND = 2, CSS_WEIGHT = 4, CSS_SLANT = 8, CSS_SIZE = 16,
       CSS_ALIGN = 32, CSS_UNDERLINE = 64, CSS_DISPLAY = 128, CSS_VISIBILITY = 256,
       CSS_LIST = 512, CSS_FLOAT = 1024, CSS_CLEAR = 2048 };

enum { CSS_BLOCK = 1, CSS_INLINE, CSS_NONE };
enum { CSS_LEFT = 1, CSS_CENTER, CSS_RIGHT };
enum { CSS_SIDE_NONE = 0, CSS_SIDE_LEFT = 1, CSS_SIDE_RIGHT = 2, CSS_SIDE_BOTH = 3 };

/* Was die Regeln für ein Element festlegen; "has" sagt, welche Angaben gemacht wurden */
typedef struct {
    uint32_t has;
    uint32_t color;                     /* 0x00RRGGBB */
    uint32_t background;                /* 0x00RRGGBB */
    bool background_clear;              /* ausdrücklich kein Hintergrund */
    bool bold, italic, underline, hidden, list_plain;
    uint8_t display, align;
    uint8_t float_side;                 /* neben dem Text: CSS_SIDE_LEFT/RIGHT, oder NONE */
    uint8_t clear;                      /* erst unterhalb solcher Kästen beginnen */
    int size_px;                        /* Schriftgröße in Pixeln, oder 0 ... */
    int size_percent;                   /* ... dann relativ zur Umgebung in Prozent */
} css_style_t;

/* viewport_width: Breite der Anzeige in Pixeln, für @media-Bedingungen */
css_sheet_t *css_create(int viewport_width);
void css_free(css_sheet_t *sheet);

/* Liest ein Stylesheet ein; spätere Regeln gehen bei Gleichstand vor. */
bool css_add(css_sheet_t *sheet, const char *text, size_t length);

/*
 * Bestimmt, was für das Element gilt (nur ausdrücklich Festgelegtes). style
 * ist der Inhalt seines style-Attributs (oder NULL); er geht den Regeln vor,
 * außer denen mit !important.
 */
void css_match(const css_sheet_t *sheet, const css_node_t *node, const char *style,
               size_t style_length, css_style_t *out);

/* Liest eine Farbangabe; false bei "transparent" oder Unverständlichem. */
bool css_color(const char *text, size_t length, uint32_t *rgb);

/* Anzahl der eingelesenen Regeln (für Meldungen und Tests) */
int css_rule_count(const css_sheet_t *sheet);
