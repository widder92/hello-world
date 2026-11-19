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
 *   Attribute    [name], [name=wert] sowie ~= |= ^= $= *= (auch mit " i")
 *   Kästen       float, clear und width - sowie die Kniffe, mit denen Seiten
 *                Text nur für Vorleseprogramme bereithalten (clip, top)
 * Regeln mit Zuständen (:hover) oder Nachbarn (a + b) werden ausgelassen
 * statt falsch angewandt; ":not(:focus)" und Ähnliches gilt als erfüllt.
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
    const char *attrs;                  /* Text des Start-Tags hinter dem Namen: alle Attribute */
    size_t attrs_len;
    const struct css_node *parent;      /* umgebendes Element oder NULL */
    /*
     * Wer seine Attribute nicht als Text vorliegen hat, gibt stattdessen eine
     * Auskunft mit: liefert den Wert (NULL: fehlt) und seine Länge.
     */
    const char *(*attr)(const struct css_node *node, const char *name, size_t name_len,
                        size_t *value_len);
} css_node_t;

enum { CSS_COLOR = 1, CSS_BACKGROUND = 2, CSS_WEIGHT = 4, CSS_SLANT = 8, CSS_SIZE = 16,
       CSS_ALIGN = 32, CSS_UNDERLINE = 64, CSS_DISPLAY = 128, CSS_VISIBILITY = 256,
       CSS_LIST = 512, CSS_FLOAT = 1024, CSS_CLEAR = 2048, CSS_WIDTH = 4096, CSS_CLIP = 8192,
       CSS_DIRECTION = 16384, CSS_WRAP = 32768, CSS_TRACKS = 65536, CSS_BASIS = 131072,
       CSS_GROW = 262144, CSS_JUSTIFY = 524288, CSS_AREAS = 1048576 };

enum { CSS_BLOCK = 1, CSS_INLINE, CSS_NONE };
enum { CSS_LEFT = 1, CSS_CENTER, CSS_RIGHT };
enum { CSS_SIDE_NONE = 0, CSS_SIDE_LEFT = 1, CSS_SIDE_RIGHT = 2, CSS_SIDE_BOTH = 3 };
/* Wie ein Element seine Kinder anordnet (aus display: flex / grid) */
enum { CSS_FLOW = 0, CSS_FLEX, CSS_GRID };
/* Wohin freier Platz in einer Reihe geht (justify-content) */
enum { CSS_PACK_START = 0, CSS_PACK_CENTER, CSS_PACK_END, CSS_PACK_BETWEEN };

/*
 * --- Für den Seitenaufbau mit Kastenmodell (layout.h) ---
 *
 * Eine Länge: Pixel + Anteil der Bezugsgröße (Prozent) + Vielfache der
 * Schriftgröße (em), je mal 1000 - so lässt sich auch "calc(100% - 2em)"
 * festhalten. Was gilt, wenn nichts festgelegt ist, weiß der Seitenaufbau.
 */
enum { CSS_LEN_UNSET = 0, CSS_LEN_VALUE, CSS_LEN_AUTO, CSS_LEN_NONE,
       CSS_LEN_CONTENT };               /* CONTENT: so breit wie der Inhalt (fit-content ...) */

typedef struct {
    uint8_t kind;
    int32_t px, percent, em;
} css_len_t;

/* Bei allen folgenden Aufzählungen heißt 0: nicht festgelegt */
enum { CSS_D_NONE = 1, CSS_D_BLOCK, CSS_D_INLINE, CSS_D_INLINE_BLOCK, CSS_D_LIST_ITEM,
       CSS_D_FLEX, CSS_D_INLINE_FLEX, CSS_D_GRID, CSS_D_INLINE_GRID, CSS_D_TABLE,
       CSS_D_INLINE_TABLE, CSS_D_TABLE_ROW, CSS_D_TABLE_CELL, CSS_D_TABLE_GROUP,
       CSS_D_TABLE_CAPTION, CSS_D_TABLE_COLUMN, CSS_D_CONTENTS,
       CSS_D_FLOW_ROOT };               /* ein Block, der einen eigenen Abschnitt bildet */
enum { CSS_POS_STATIC = 1, CSS_POS_RELATIVE, CSS_POS_ABSOLUTE, CSS_POS_FIXED, CSS_POS_STICKY };
enum { CSS_WS_NORMAL = 1, CSS_WS_NOWRAP, CSS_WS_PRE, CSS_WS_PRE_WRAP, CSS_WS_PRE_LINE };
enum { CSS_VA_BASELINE = 1, CSS_VA_TOP, CSS_VA_MIDDLE, CSS_VA_BOTTOM, CSS_VA_SUB, CSS_VA_SUPER };
enum { CSS_TT_NONE = 1, CSS_TT_UPPER, CSS_TT_LOWER, CSS_TT_CAPITALIZE };
enum { CSS_OV_VISIBLE = 1, CSS_OV_HIDDEN, CSS_OV_SCROLL };
enum { CSS_AL_STRETCH = 1, CSS_AL_START, CSS_AL_CENTER, CSS_AL_END, CSS_AL_BASELINE };
enum { CSS_JU_START = 1, CSS_JU_CENTER, CSS_JU_END, CSS_JU_BETWEEN, CSS_JU_AROUND, CSS_JU_EVENLY };
enum { CSS_FD_ROW = 1, CSS_FD_ROW_REVERSE, CSS_FD_COLUMN, CSS_FD_COLUMN_REVERSE };
enum { CSS_FW_NOWRAP = 1, CSS_FW_WRAP };
enum { CSS_LT_NONE = 1, CSS_LT_DISC, CSS_LT_CIRCLE, CSS_LT_SQUARE, CSS_LT_DECIMAL,
       CSS_LT_LOWER_ALPHA, CSS_LT_UPPER_ALPHA, CSS_LT_LOWER_ROMAN, CSS_LT_UPPER_ROMAN };
enum { CSS_BS_NONE = 1, CSS_BS_SOLID, CSS_BS_DASHED, CSS_BS_DOTTED };
enum { CSS_FF_SANS = 1, CSS_FF_MONO };
enum { CSS_BOX_CONTENT = 1, CSS_BOX_BORDER };
enum { CSS_NO = 1, CSS_YES };            /* für Ja/Nein-Angaben */
enum { CSS_COLOR_VALUE = 1, CSS_COLOR_CURRENT, CSS_COLOR_TRANSPARENT };

/* Eine Spalte eines Rasters (grid-template-columns) */
#define CSS_GRID_TRACKS 12
#define CSS_AREA_ROWS 8
#define CSS_AREA_COLUMNS 6
typedef struct {
    css_len_t size;                     /* feste Breite bzw. Mindestbreite (UNSET: keine) */
    int32_t fr;                         /* Anteil am freien Platz mal 1000, oder 0 */
    bool automatic;                     /* so breit wie der Inhalt */
} css_track_t;

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
    int width_px;                       /* Breite in Pixeln, oder 0 ... */
    int width_percent;                  /* ... dann in Prozent der Umgebung */
    /*
     * Kinder nebeneinander: display: flex oder grid (layout), bei flex die
     * Richtung und ob umbrochen wird; bei grid die Zahl der Spalten laut
     * grid-template-columns - oder die Mindestbreite einer Spalte, wenn die
     * Seite so viele Spalten will, wie passen (repeat(auto-fill, ...)).
     */
    uint8_t layout;
    bool stacked;                       /* flex-direction: column - untereinander */
    bool wraps;
    uint8_t tracks;
    int track_min_px;
    bool areas;                         /* Raster mit benannten Flächen: Die Kinder stehen nicht der Reihe nach */
    uint8_t justify;                    /* CSS_PACK_...: freier Platz in der Reihe */
    /* Wunschbreite eines Kinds in so einer Reihe (flex-basis, flex), wie width */
    int basis_px, basis_percent;
    bool grows;                         /* das Kind nimmt freien Platz der Reihe (flex-grow) */
    bool clipped;                       /* auf nichts beschnitten: nur für Vorleseprogramme da */
    bool off_top;                       /* weit nach oben aus dem Bild geschoben (top: -1000em) */

    /*
     * Nur von css_match_full() ausgefüllt - für den Seitenaufbau mit
     * Kastenmodell. Seiten jeweils in der Reihenfolge oben, rechts, unten, links.
     */
    uint8_t display_kind, position, box_sizing, white_space, vertical_align, text_transform;
    uint8_t overflow, font_weight, font_slant, font_family, list_type, border_collapse;
    uint8_t flex_direction, flex_wrap, justify_content, align_items, align_self, align_content;
    uint8_t break_words, transparent;   /* CSS_NO/CSS_YES: lange Wörter trennen; opacity: 0 */
    uint8_t background_image;           /* CSS_NO/CSS_YES: Bild oder Farbverlauf als Hintergrund */
    bool gradient_set;                  /* ... und dessen erste Farbe als Ersatz */
    uint32_t gradient_color;
    uint8_t border_style[4], border_color_kind[4];
    uint32_t border_color[4];
    css_len_t margin[4], padding[4], border_width[4], inset[4];
    css_len_t width, height, min_width, max_width, min_height, max_height;
    css_len_t font_size, line_height, text_indent, radius, gap_row, gap_column, flex_basis;
    css_len_t border_spacing;
    bool grow_set, shrink_set, z_set, order_set, grid_set;
    int32_t flex_grow, flex_shrink;     /* mal 1000 */
    int32_t z_index, order;
    css_track_t grid[CSS_GRID_TRACKS];  /* grid-template-columns */
    uint8_t grid_count;
    bool grid_auto_fill;                /* so viele Spalten wie passen: die erste Spalte wiederholt sich */
    uint8_t grid_span;                  /* grid-column: über so viele Spalten (0: eine) */
    bool grid_full;                     /* ... oder über alle (1 / -1) */
    uint8_t grid_column;                /* ... ab dieser Spalte (1 ist die erste; 0: der Reihe nach) */
    uint8_t grid_row, grid_row_span;    /* grid-row: ab dieser Zeile, über so viele (0: der Reihe nach, eine) */
    /*
     * Raster mit benannten Flächen (grid-template-areas): je Zelle eine
     * Kennzahl des Namens (0: frei). grid_area ist die Kennzahl der Fläche,
     * in der ein Kind stehen will.
     */
    uint8_t area_rows, area_columns;
    uint16_t area_map[CSS_AREA_ROWS][CSS_AREA_COLUMNS];
    uint16_t grid_area;
    uint8_t masked;                     /* CSS_NO/CSS_YES: Der Hintergrund scheint nur durch eine Maske (Symbole) */
    uint8_t caption_below;              /* CSS_NO/CSS_YES: Beschriftung einer Tabelle darunter (caption-side) */
    /*
     * "clear" an einem nachgestellten Scheinelement (::after) - der übliche
     * Kniff, damit ein Kasten einschließt, was in ihm nebeneinander steht:
     * CSS_SIDE_LEFT/RIGHT/BOTH, oder 0
     */
    uint8_t clear_after;
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

/*
 * Wie css_match(), aber vollständig: mit den Grundregeln des Browsers (jede
 * Seite beginnt mit ihnen - Absätze haben Abstand, Überschriften sind groß ...)
 * und allen Angaben für Kästen. hints sind Festlegungen aus Attributen des
 * Elements ("bgcolor", "width" ...) in der Schreibweise eines style-Attributs:
 * Sie gelten vor den Grundregeln, aber jede Regel der Seite geht ihnen vor.
 */
void css_match_full(const css_sheet_t *sheet, const css_node_t *node, const char *hints,
                    size_t hints_length, const char *style, size_t style_length,
                    css_style_t *out);

/* Höhe der Anzeige in Pixeln, für Angaben in vh (sonst wird 3/4 der Breite angenommen) */
void css_set_viewport_height(css_sheet_t *sheet, int height);

/* Liest eine Längenangabe ("12px", "1.5em", "50%", "calc(...)"); false bei Unverständlichem. */
bool css_length(const css_sheet_t *sheet, const char *text, size_t length, css_len_t *out);

/* Liest eine Farbangabe; false bei "transparent" oder Unverständlichem. */
bool css_color(const char *text, size_t length, uint32_t *rgb);

/* Anzahl der eingelesenen Regeln (für Meldungen und Tests) */
int css_rule_count(const css_sheet_t *sheet);

/*
 * Eine einzelne Auswahl ("ul.menu > li, #kopf") zum Prüfen von Elementen -
 * für querySelector und Verwandte. NULL, wenn sie ungültig ist oder etwas
 * enthält, das sich hier nicht prüfen lässt.
 */
typedef struct css_selector css_selector_t;
css_selector_t *css_selector_parse(const char *text, size_t length);
bool css_selector_matches(const css_selector_t *selector, const css_node_t *node);
void css_selector_free(css_selector_t *selector);
