/*
 * HTML für den Browser: zerlegt eine Seite und setzt sie in Zeilen aus Text,
 * Links und Formularfeldern um - ein fester Zeichenraster, wie in einem
 * Textbrowser. Stylesheets und Skripte werden nicht ausgeführt.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { STYLE_BOLD = 1, STYLE_LINK = 2, STYLE_HEADING = 4, STYLE_MUTED = 8 };

/* Ein Stück einer Zeile: Text in einer Darstellung - oder ein Formularfeld. */
typedef struct {
    uint32_t text;                      /* Stelle im Textvorrat der Seite */
    uint16_t bytes;
    uint16_t column, width;             /* in Zeichen */
    uint8_t style;
    int16_t link;                       /* Nummer des Links oder -1 */
    int16_t widget;                     /* Nummer des Formularfelds oder -1 */
} span_t;

typedef struct {
    uint32_t first, count;              /* Stücke dieser Zeile */
} line_t;

typedef enum {
    W_TEXT, W_PASSWORD, W_CHECKBOX, W_RADIO, W_SELECT, W_TEXTAREA, W_SUBMIT, W_HIDDEN,
} widget_kind_t;

typedef struct {
    char *label, *value;
} option_t;

typedef struct {
    widget_kind_t kind;
    int form;                           /* Nummer des Formulars oder -1 */
    char *name;                         /* kann fehlen (NULL) */
    char *value;                        /* Inhalt bzw. der Wert, der gesendet wird */
    char *label;                        /* Beschriftung eines Knopfs */
    bool checked;
    int first_option, option_count, selected;
    int width;                          /* Breite auf dem Bildschirm, in Zeichen */
    int line, column;                   /* Platz auf der Seite; line -1: unsichtbar */
} widget_t;

typedef struct {
    char *action;                       /* vollständige Adresse */
    bool post;
} form_t;

typedef struct {
    char *id;
    int line;
} anchor_t;

typedef struct {
    char *title;
    char *pool;
    size_t pool_len, pool_cap;
    span_t *spans;
    int span_count, span_cap;
    line_t *lines;
    int line_count, line_cap;
    char **links;                       /* vollständige Adressen oder "#sprungmarke" */
    int link_count, link_cap;
    widget_t *widgets;
    int widget_count, widget_cap;
    option_t *options;
    int option_count, option_cap;
    form_t *forms;
    int form_count, form_cap;
    anchor_t *anchors;
    int anchor_count, anchor_cap;
    int heading_line;                   /* Zeile der ersten Hauptüberschrift oder -1 */
    bool failed;                        /* Speicher ging aus - die Seite ist unvollständig */
} page_t;

/* Baut die Seite für eine Breite von columns Zeichen; NULL bei Speichermangel. */
page_t *html_layout(const char *html, size_t len, const char *base_url, int columns);
/* Wie html_layout(), aber für reinen Text (alles wird wörtlich gezeigt). */
page_t *html_plain_text(const char *text, size_t len, int columns);
void page_free(page_t *page);

/* Ersetzt den Inhalt eines Felds; false bei Speichermangel. */
bool widget_set_value(widget_t *w, const char *text, size_t len);
