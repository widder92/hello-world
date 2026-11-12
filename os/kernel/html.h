/*
 * HTML für den Browser: zerlegt eine Seite und setzt sie in Zeilen aus Text,
 * Links und Formularfeldern um - ein fester Zeichenraster, wie in einem
 * Textbrowser. Stylesheets und Skripte werden nicht ausgeführt.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { STYLE_BOLD = 1, STYLE_LINK = 2, STYLE_HEADING = 4, STYLE_MUTED = 8,
       STYLE_ITALIC = 16, STYLE_MONO = 32, STYLE_UNDERLINE = 64 };

/* Farbangaben in Stücken und Zeilen: 0 heißt "keine"; sonst dieses Bit plus 0xRRGGBB */
#define HTML_COLOR_SET 0x01000000u

/* Ein Stück einer Zeile: Text in einer Darstellung - oder ein Formularfeld. */
typedef struct {
    uint32_t text;                      /* Stelle im Textvorrat der Seite */
    uint16_t bytes;
    uint16_t column, width;             /* in Zeichen - mit Schriftauskunft in Pixeln */
    uint8_t style;
    uint8_t size;                       /* Schriftgröße in Pixeln; 0 im Zeichenraster */
    uint32_t color;                     /* Textfarbe laut Seite, oder 0: die übliche */
    uint32_t background;                /* Hintergrund nur dieses Stücks, oder 0 */
    int16_t link;                       /* Nummer des Links oder -1 */
    int16_t widget;                     /* Nummer des Formularfelds oder -1 */
    int16_t image;                      /* Bild an dieser Stelle (in Tabellen), oder -1 */
    uint16_t image_row;                 /* ... und welche seiner Zeilen */
} span_t;

typedef struct {
    uint32_t first, count;              /* Stücke dieser Zeile */
    int16_t image;                      /* Zeile gehört zu diesem Bild, oder -1 */
    uint16_t image_row;                 /* die wievielte Zeile des Bildes */
    uint8_t tall;                       /* große Schrift: belegt auch die nächste Zeile */
    uint8_t filler;                     /* diese Zeile ist der untere Teil einer solchen */
    uint8_t align;                      /* 0/1 links, 2 Mitte, 3 rechts (wie CSS_LEFT ...) */
    uint32_t background;                /* Hintergrund der ganzen Zeile, oder 0 */
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

/*
 * Ein Bild steht als Block für sich: rows Zeilen ab line. Ist es noch nicht
 * geladen, hält der Block den Platz frei (oder zeigt in einer Zeile den
 * Ersatztext).
 */
typedef struct {
    char *url;                          /* vollständige Adresse */
    char *alt;                          /* Ersatztext, kann fehlen (NULL) */
    int link;                           /* Link, in dem das Bild steht, oder -1 */
    int line, rows;
    int column;                         /* Einrückung in Zeichen */
    int width, height;                  /* Größe auf dem Bildschirm; 0: unbekannt */
    bool ready;                         /* war beim Setzen der Seite geladen */
} page_image_t;

/* Was der Seitenaufbau über ein Bild wissen kann */
typedef enum { PICTURE_UNKNOWN, PICTURE_READY, PICTURE_BROKEN } picture_state_t;

/*
 * Wer Bilder zeigen will, gibt dem Seitenaufbau das mit: die Maße des
 * Zeichenrasters und eine Auskunft, ob ein Bild schon geladen ist (und wie
 * groß es dann ist).
 */
typedef struct {
    int cell_width, line_height;        /* Pixel je Zeichen und je Zeile */
    int max_rows;                       /* höchstens so viele Zeilen je Bild */
    picture_state_t (*lookup)(void *ctx, const char *url, int *width, int *height);
    void *ctx;
} html_pictures_t;

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
    page_image_t *images;
    int image_count, image_cap;
    int heading_line;                   /* Zeile der ersten Hauptüberschrift oder -1 */
    bool pixels;                        /* Spalten und Breiten der Stücke sind Pixel, nicht Zeichen */
    uint32_t background;                /* Hintergrund der Seite laut ihrer Gestaltung, oder 0 */
    bool failed;                        /* Speicher ging aus - die Seite ist unvollständig */
} page_t;

/* Baut die Seite für eine Breite von columns Zeichen; NULL bei Speichermangel. */
page_t *html_layout(const char *html, size_t len, const char *base_url, int columns);
/*
 * Schriftauskunft: Wer in echter Schrift setzen will, sagt dem Seitenaufbau,
 * wie breit ein Stück Text wird. Alle Breiten und Spalten der Seite sind dann
 * Pixel statt Zeichen; `columns` beim Aufruf ist die Breite in Pixeln, und
 * Zeilen mit großer Schrift (ab 19 Pixel) belegen zwei Zeilen.
 *
 * In diesem Modus wertet der Seitenaufbau auch die Gestaltung der Seite aus:
 * style-Attribute und - falls mitgegeben - ihre Stylesheets (css.h).
 */
struct css_sheet;

typedef struct {
    int cell_width;                     /* Breite eines Zeichens der Rasterschrift (Felder, Einzüge) */
    int (*width)(void *ctx, const char *text, size_t len, uint8_t style, uint8_t size);
    void *ctx;
    uint8_t base_size;                  /* Schriftgröße des Fließtexts */
    uint8_t heading_size[3];            /* ... und der Überschriften der Stufen 1 bis 3 */
    /* Die üblichen Farben des Browsers: nötig, um unlesbare Kombinationen zu erkennen */
    uint32_t text_color, link_color, page_color;
    const struct css_sheet *css;        /* Stylesheets der Seite oder NULL */
} html_text_t;

/*
 * Sucht die Stylesheets einer Seite, in der Reihenfolge ihres Auftretens:
 * den Inhalt von <style> (linked false) und die Adressen verlinkter Dateien
 * (linked true, vollständige Adresse).
 */
typedef void (*html_style_found_t)(void *ctx, bool linked, const char *text, size_t len);
void html_find_styles(const char *html, size_t len, const char *base_url,
                      html_style_found_t found, void *ctx);

/* Wie html_layout(), aber mit Bildern als Blöcken und/oder in echter Schrift. */
page_t *html_layout_rich(const char *html, size_t len, const char *base_url, int columns,
                         const html_pictures_t *pictures, const html_text_t *text);

/* Wie html_layout(), plant aber Bilder als Blöcke ein. */
page_t *html_layout_pictures(const char *html, size_t len, const char *base_url,
                             int columns, const html_pictures_t *pictures);
/* Wie html_layout(), aber für reinen Text (alles wird wörtlich gezeigt). */
page_t *html_plain_text(const char *text, size_t len, int columns);
void page_free(page_t *page);

/* Ersetzt den Inhalt eines Felds; false bei Speichermangel. */
bool widget_set_value(widget_t *w, const char *text, size_t len);
