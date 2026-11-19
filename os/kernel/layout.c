/* Seitenaufbau mit Kastenmodell - siehe layout.h */
#include "layout.h"
#include "claude.h"
#include "console.h"
#include "memory.h"
#include "string.h"
#include "url.h"

/*
 * Der Ablauf:
 *   1. Aus dem Abbild der Seite entsteht ein Baum von Kästen; für jedes
 *      Element wird bestimmt, was seine Gestaltung festlegt (compute_style).
 *   2. Jeder Kasten erfährt, wie breit sein Inhalt mindestens und höchstens
 *      werden will (intrinsic) - das brauchen Tabellen, Kästen neben dem Text
 *      und alles, was sich nach seinem Inhalt richtet.
 *   3. Von oben nach unten bekommt jeder Kasten seine Breite, setzt seinen
 *      Inhalt und kennt danach seine Höhe (layout_block und Verwandte).
 *   4. Aus den fertigen Kästen entsteht die Seite: Kästen, Zeilen, Bilder,
 *      Felder in der Reihenfolge, in der sie gezeichnet werden (emit).
 *
 * Aller Speicher dafür kommt aus einem Vorrat, der am Ende als Ganzes
 * freigegeben wird.
 */

#define CHUNK_BYTES     (256 * 1024)
#define SPARE_BYTES     4096            /* Ausweichplatz, wenn der Speicher ausgeht */
#define BOXES_MAX       400000
#define DEPTH_MAX       180             /* so tief verschachtelt wird gesetzt */
#define IMAGES_MAX      300
#define WIDGETS_MAX     30000
#define LINKS_MAX       30000
#define ANCHORS_MAX     6000
#define FLOATS_MAX      64              /* Kästen neben dem Text je Abschnitt */
#define COLUMNS_MAX     64              /* Spalten einer Tabelle */
#define IMAGE_TALLEST   700             /* höher wird kein Bild gezeigt */
#define HUGE_WIDTH      100000

/* --- Datenstrukturen --------------------------------------------------------------------------- */

typedef struct {
    uint8_t kind;                       /* CSS_LEN_... */
    int32_t px;
    int32_t percent;                    /* mal 1000 */
} len_t;

typedef struct {
    len_t size;
    int32_t fr;
    bool automatic;
} track_t;

/* Angaben für flex und grid - nur für Elemente, die welche haben */
typedef struct {
    uint8_t direction, wrap, justify, align_items, align_self, align_content;
    bool grow_set, shrink_set, grid_auto_fill, grid_full;
    int32_t grow, shrink, order;
    len_t basis, gap_row, gap_column;
    uint8_t grid_count, grid_span;
    track_t grid[CSS_GRID_TRACKS];
    /* benannte Flächen des Rasters; und wo ein Kind darin stehen will */
    uint8_t area_rows, area_columns;
    uint16_t area_map[CSS_AREA_ROWS][CSS_AREA_COLUMNS];
    uint16_t grid_area;
    uint8_t grid_column, grid_row, grid_row_span;
} flexgrid_t;

typedef struct style {
    /* wird vererbt */
    uint32_t color;                     /* HTML_COLOR_SET plus Farbe, oder 0: die übliche */
    uint32_t back;                      /* was hinter dem Text zu sehen ist (0xRRGGBB) */
    uint8_t size;                       /* Schriftgröße in Pixeln */
    uint8_t text;                       /* STYLE_BOLD, _ITALIC, _MONO, _UNDERLINE, _LINK */
    uint8_t align, white_space, transform, list_type, break_words;
    bool invisible;                     /* visibility: hidden */
    int16_t line_px;                    /* Zeilenhöhe in Pixeln, oder 0 ... */
    int32_t line_factor;                /* ... dann als Vielfaches der Schrift mal 1000, oder 0: üblich */
    int16_t indent;
    int16_t link;                       /* Nummer des Links, in dem der Inhalt steht, oder -1 */
    uint8_t spacing;                    /* Tabellen: Abstand zwischen den Zellen */
    bool collapse;                      /* ... oder Zellen mit gemeinsamen Rahmen */
    bool caption_below;                 /* die Beschriftung steht unter der Tabelle */
    /* wird nicht vererbt */
    uint8_t display, position, float_side, clear, box_sizing, overflow, valign;
    uint8_t clear_after;                /* der Kasten reicht bis unter das, was in ihm nebeneinander steht */
    bool background_set, z_set;
    uint32_t background;
    uint8_t border[4];                  /* Breite des Rahmens in Pixeln: oben, rechts, unten, links */
    uint8_t border_style[4];            /* CSS_BS_..., oder 0: wird nicht gezeichnet */
    uint32_t border_color[4];
    uint8_t radius;
    len_t margin[4], padding[4], inset[4];
    len_t width, height, min_width, max_width, min_height, max_height;
    int32_t z;
    flexgrid_t *fg;
    struct style *anonymous;            /* die Gestaltung namenloser Kästen in diesem Element */
} style_t;

enum { K_BLOCK, K_INLINE, K_TEXT, K_BREAK };
enum { IN_FLOW, IN_FLEX, IN_GRID, IN_TABLE };
enum { F_ATOMIC = 1,                    /* steht als Ganzes in einer Textzeile (inline-block, Bild, Feld) */
       F_FLOAT = 2, F_ABS = 4,
       F_ANON = 8,                      /* namenloser Kasten um Text zwischen Kästen */
       F_LINES = 16,                    /* der Inhalt wird in Zeilen gesetzt */
       F_REPLACED = 32,                 /* Bild, Feld, Platzhalter: feste Maße, kein eigener Inhalt */
       F_MARKER = 64,                   /* Listenpunkt: steht links vor dem Kasten */
       F_ROOT = 128,                    /* schließt seine Kästen neben dem Text ein */
       F_FIXED = 256,                   /* position: fixed */
       F_OWN_TEXT = 512,                /* der Text gehört dem Vorrat, nicht dem Abbild */
       F_RELATIVE = 1024,
       F_LAYER = 2048 };                /* wird mit den frei platzierten Kästen gezeichnet (eigene Ebene) */

typedef struct piece {
    struct piece *next;
    int x, w;
    const style_t *st;
    const dom_node *node;               /* das Element, zu dem der Text gehört */
    char *text;
    uint16_t bytes, room;
    int16_t shift;                      /* hoch-/tiefgestellt: Grundlinie um so viel tiefer */
    struct lbox *atom;                  /* statt Text: ein Kasten in der Zeile */
} piece_t;

typedef struct deco {
    struct deco *next;
    const struct lbox *box;             /* das Element in der Zeile, dem Hintergrund und Rahmen gehören */
    int x, w, top, h;
    bool opens, closes;                 /* linkes bzw. rechtes Ende des Elements liegt in dieser Zeile */
} deco_t;

typedef struct lline {
    struct lline *next;
    int y, h, baseline;                 /* y ab Oberkante des Inhalts des Kastens */
    piece_t *first;
    deco_t *decos;
} lline_t;

typedef struct lbox {
    struct lbox *parent, *first, *last, *next;
    const dom_node *node;               /* Element oder Text; NULL bei namenlosen Kästen */
    style_t *st;
    uint8_t kind, inner;
    uint16_t flags;
    uint32_t seq;                       /* laufende Nummer: die Reihenfolge in der Seite */
    int16_t image, widget;              /* Bild bzw. Feld der Seite, oder -1 */
    int32_t ordinal;                    /* Nummer eines Listenpunkts */
    int x, y, w, h;                     /* Außenkante des Rahmens, ab der des umgebenden Kastens */
    int margin[4];
    int pad[4];
    uint8_t bor[4];                     /* Breite des Rahmens, wie sie für diesen Kasten gilt */
    int min_w, max_w;                   /* gewünschte Breite samt Rahmen: mindestens, höchstens (-1: unbekannt) */
    int baseline;                       /* erste Grundlinie ab Oberkante, oder -1 */
    int leak_top, leak_bottom;          /* Abstände, die oben und unten aus dem Kasten herausreichen */
    lline_t *lines;
    struct lbox *abs_list, *abs_next;   /* frei platzierte Nachfahren, die sich nach diesem Kasten richten */
    int static_x, static_y;             /* wo ein frei platzierter Kasten ohne Angaben stünde */
    int ax, ay;                         /* Lage auf der Seite */
    const char *text;                   /* K_TEXT */
    uint32_t text_len;
    uint8_t colspan, rowspan;
    int natural_w, natural_h;           /* F_REPLACED: Maße ohne Angaben der Seite */
    struct lbox *line_block;            /* K_INLINE: in diesem Kasten beginnt es, in der Zeile bei line_y */
    int line_y;
    struct lbox *layer_next;            /* alle frei platzierten Kästen der Seite, der Reihe nach */
    void *aux;                          /* Tabellen: ihre Aufteilung in Zeilen und Spalten */
} lbox_t;

/* Sprungziele: Elemente mit Namen - wo sie stehen, zeigt sich erst am Ende */
typedef struct target {
    struct target *next;
    char *id;
    lbox_t *box;
} target_t;

typedef struct chunk {
    struct chunk *next;
    size_t used, size;
} chunk_t;

/* Kästen neben dem Text eines Abschnitts (in dessen Koordinaten) */
typedef struct {
    struct {
        int x, y, w, h;
        bool right;
    } items[FLOATS_MAX];
    int count;
} floats_t;

typedef struct {
    const layout_options_t *opt;
    const dom_doc *doc;
    page_t *page;
    css_sheet_t *own_css;               /* nur die Grundregeln, wenn die Seite keine Stylesheets hat */
    const css_sheet_t *css;
    bool author_rules;                  /* die Seite hat eigene Regeln */
    chunk_t *chunks;
    char spare[SPARE_BYTES];
    bool failed;
    int boxes;
    char base[URL_SIZE];
    lbox_t *root;
    lbox_t *fixed_owner;                /* der Kasten der ganzen Seite: nach ihm richtet sich position: fixed */
    uint32_t canvas;                    /* Hintergrund der ganzen Seite */
    bool canvas_set;
    int form;                           /* Formular, in dem gerade gebaut wird, oder -1 */
    target_t *targets;
    int target_count;
    lbox_t *heading;                    /* die erste Hauptüberschrift */
    lbox_t *layers, *layers_tail;       /* frei platzierte Kästen: Sie werden zuletzt gezeichnet */
    int bottom;                         /* so weit reicht die Seite nach unten */
} lay_t;

/* --- Speicher ---------------------------------------------------------------------------------- */

static void *arena(lay_t *L, size_t n)
{
    n = (n + 7) & ~(size_t)7;
    chunk_t *c = L->chunks;
    if (!c || c->used + n > c->size) {
        size_t size = n > CHUNK_BYTES - sizeof(chunk_t) ? n : CHUNK_BYTES - sizeof(chunk_t);
        c = L->failed ? NULL : kmalloc(sizeof(chunk_t) + size);
        if (!c) {
            /* kein Speicher: Der Aufrufer bekommt Ausweichplatz und die Seite gilt als gescheitert */
            L->failed = true;
            memset(L->spare, 0, sizeof(L->spare));
            return n <= sizeof(L->spare) ? L->spare : NULL;
        }
        c->next = L->chunks;
        c->used = 0;
        c->size = size;
        L->chunks = c;
    }
    void *p = (char *)(c + 1) + c->used;
    c->used += n;
    memset(p, 0, n);
    return p;
}

static void arena_free(lay_t *L)
{
    while (L->chunks) {
        chunk_t *next = L->chunks->next;
        kfree(L->chunks);
        L->chunks = next;
    }
}

/* Die Felder einer Seite wachsen wie in html.c */
static bool grow(lay_t *L, void **array, int count, int *cap, size_t item)
{
    if (count < *cap)
        return true;
    int bigger = *cap ? *cap * 2 : 32;
    void *grown = kmalloc((size_t)bigger * item);
    if (!grown) {
        L->failed = true;
        return false;
    }
    if (*cap)
        memcpy(grown, *array, (size_t)*cap * item);
    kfree(*array);
    *array = grown;
    *cap = bigger;
    return true;
}

#define GROW(L, array, count, cap) grow(L, (void **)&(array), count, &(cap), sizeof(*(array)))

static char *copy_text(const char *text, size_t len)
{
    char *copy = kmalloc(len + 1);
    if (copy) {
        memcpy(copy, text, len);
        copy[len] = '\0';
    }
    return copy;
}

/* --- kleine Helfer ----------------------------------------------------------------------------- */

static bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

static char lower(char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c;
}

static bool same(const char *a, const char *b)
{
    while (*a && *b && lower(*a) == lower(*b)) {
        a++;
        b++;
    }
    return !*a && !*b;
}

static bool is_tag(const dom_node *n, const char *name)
{
    return n && n->type == DOM_ELEMENT && strcmp(n->name, name) == 0;
}

static bool one_of(const dom_node *n, const char *const *names)
{
    for (int i = 0; names[i]; i++) {
        if (is_tag(n, names[i]))
            return true;
    }
    return false;
}

/* Die Zahl am Anfang eines Attributs ("120", "50%"); percent sagt, ob ein % folgt. */
static bool attr_number(const dom_node *n, const char *name, int *value, bool *percent)
{
    const char *text = dom_attr_get(n, name);
    long v = 0;
    int digits = 0;
    if (!text)
        return false;
    while (is_space(*text))
        text++;
    for (; *text >= '0' && *text <= '9' && digits < 7; text++, digits++)
        v = v * 10 + (*text - '0');
    if (!digits)
        return false;
    while (*text >= '0' && *text <= '9')
        text++;
    if (*text == '.')
        for (text++; *text >= '0' && *text <= '9'; text++)
            ;
    *value = (int)v;
    if (percent)
        *percent = *text == '%';
    return true;
}

static int attr_int(const dom_node *n, const char *name, int fallback)
{
    int value;
    return attr_number(n, name, &value, NULL) ? value : fallback;
}

static int clamp(int v, int low, int high)
{
    return v < low ? low : v > high ? high : v;
}

static int max_int(int a, int b)
{
    return a > b ? a : b;
}

static int min_int(int a, int b)
{
    return a < b ? a : b;
}

/* Eine Länge in Pixeln; base ist die Bezugsgröße für Prozente (unter 0: unbekannt). */
static int len_px(len_t l, int base)
{
    if (l.kind != CSS_LEN_VALUE)
        return 0;
    return l.px + (l.percent && base > 0 ? (int)((int64_t)l.percent * base / 100000) : 0);
}

/* Steht die Länge fest - auch ohne die Bezugsgröße zu kennen? */
static bool len_fixed(len_t l)
{
    return l.kind == CSS_LEN_VALUE && l.percent == 0;
}

static bool len_set(len_t l, int base)
{
    return l.kind == CSS_LEN_VALUE && (l.percent == 0 || base >= 0);
}

static len_t make_len(const css_len_t *c, int font_size)
{
    len_t l = { c->kind, 0, 0 };
    if (c->kind == CSS_LEN_VALUE) {
        int64_t px = (int64_t)c->px + (int64_t)c->em * font_size;
        l.px = (int)((px + (px >= 0 ? 500 : -500)) / 1000);
        l.percent = c->percent;
    }
    return l;
}

static int brightness(uint32_t rgb)
{
    return (int)(((rgb >> 16) & 0xFF) * 299 + ((rgb >> 8) & 0xFF) * 587 + (rgb & 0xFF) * 114) / 1000;
}

/* Schriftmaße: Ober- und Unterlänge */
static void font_metrics_of(const lay_t *L, const style_t *st, int *ascent, int *descent)
{
    if (st->text & STYLE_MONO) {        /* die Rasterschrift ist immer gleich groß */
        *ascent = 12;
        *descent = 4;
    } else if (L->opt->metrics) {
        L->opt->metrics(L->opt->metrics_ctx, st->size, st->text, ascent, descent);
    } else {
        *ascent = (st->size * 93 + 50) / 100;
        *descent = (st->size * 24 + 50) / 100;
    }
}

/* Die Höhe einer Textzeile in dieser Gestaltung */
static int line_height_of(const lay_t *L, const style_t *st)
{
    int ascent, descent;
    font_metrics_of(L, st, &ascent, &descent);
    int size = st->text & STYLE_MONO ? 16 : st->size;
    int h = st->line_px > 0 ? st->line_px
          : st->line_factor > 0 ? (int)(((int64_t)st->line_factor * size + 500) / 1000)
          : (size * 125 + 50) / 100;
    return h < 1 ? 1 : h;
}

static int text_width(const lay_t *L, const style_t *st, const char *text, size_t len)
{
    const html_text_t *t = L->opt->text;
    return t->width(t->ctx, text, len, st->text & (STYLE_BOLD | STYLE_ITALIC | STYLE_MONO), st->size);
}

/* --- Gestaltung eines Elements ---------------------------------------------------------------- */

/* Hängt "name:wert;" an, wenn der Wert harmlos ist (keine Zeichen, die etwas anderes daraus machen). */
static void hint(char *out, size_t size, const char *name, const char *value, const char *unit)
{
    size_t n = strlen(out), len = 0;
    while (value[len] && value[len] != ';' && value[len] != '{' && value[len] != '}' && value[len] != '"')
        len++;
    while (len > 0 && is_space(value[len - 1]))
        len--;
    if (value[len] || len == 0 || n + strlen(name) + len + strlen(unit) + 3 >= size)
        return;
    ksnprintf(out + n, size - n, "%s:%.*s%s;", name, (int)len, value, unit);
}

static void hint_size(char *out, size_t size, const dom_node *e, const char *attr, const char *name)
{
    int value;
    bool percent;
    char number[16];
    if (!attr_number(e, attr, &value, &percent) || value > 20000)
        return;
    ksnprintf(number, sizeof(number), "%d", value);
    hint(out, size, name, number, percent ? "%" : "px");
}

/*
 * Was die Attribute eines Elements über sein Aussehen sagen (bgcolor, width,
 * align ...), in der Schreibweise eines style-Attributs. Jede Regel der Seite
 * geht dem vor.
 */
static void hints_for(const dom_node *e, char *out, size_t size)
{
    static const char *const sized[] = { "table", "td", "th", "img", "hr", "col", "iframe", "video",
                                         "canvas", "svg", "object", "embed", "input", NULL };
    static const char *const aligned[] = { "div", "p", "h1", "h2", "h3", "h4", "h5", "h6", "td", "th",
                                           "tr", "tbody", "thead", "tfoot", "caption", "center", NULL };
    static const char *const font_sizes[] = { "10px", "10px", "13px", "16px", "18px", "24px", "32px", "48px" };
    const char *v;
    out[0] = '\0';
    if (e->attr_count == 0)
        return;
    if ((v = dom_attr_get(e, "bgcolor")) != NULL)
        hint(out, size, "background-color", v, "");
    if (one_of(e, sized)) {
        if (!is_tag(e, "input"))
            hint_size(out, size, e, "width", "width");
        if (!is_tag(e, "table") && !is_tag(e, "hr") && !is_tag(e, "input"))
            hint_size(out, size, e, "height", "height");
    }
    if ((v = dom_attr_get(e, "align")) != NULL) {
        bool side = same(v, "left") || same(v, "right");
        if (is_tag(e, "table") || is_tag(e, "img")) {
            if (side)
                hint(out, size, "float", v, "");
            else if (same(v, "center") && is_tag(e, "table"))
                hint(out, size, "margin-left", "auto;margin-right:auto", "");
        } else if (one_of(e, aligned) && (side || same(v, "center") || same(v, "justify"))) {
            hint(out, size, "text-align", v, "");
        }
    }
    if ((v = dom_attr_get(e, "valign")) != NULL)
        hint(out, size, "vertical-align", v, "");
    if (is_tag(e, "td") || is_tag(e, "th")) {
        if (dom_attr_get(e, "nowrap"))
            hint(out, size, "white-space", "nowrap", "");
        /* Rahmen und Innenabstand der Zellen legt die Tabelle fest */
        const dom_node *table = e->parent;
        for (int up = 0; table && up < 3 && !is_tag(table, "table"); up++)
            table = table->parent;
        if (is_tag(table, "table")) {
            if (attr_int(table, "border", 0) > 0)
                hint(out, size, "border", "1px solid #808080", "");
            hint_size(out, size, table, "cellpadding", "padding");
        }
    } else if (is_tag(e, "table")) {
        int border = attr_int(e, "border", dom_attr_get(e, "border") ? 1 : 0);
        if (border > 0) {
            char text[40];
            ksnprintf(text, sizeof(text), "%dpx solid #808080", border > 20 ? 20 : border);
            hint(out, size, "border", text, "");
        }
        hint_size(out, size, e, "cellspacing", "border-spacing");
    } else if (is_tag(e, "font")) {
        if ((v = dom_attr_get(e, "color")) != NULL)
            hint(out, size, "color", v, "");
        if ((v = dom_attr_get(e, "face")) != NULL)
            hint(out, size, "font-family", v, "");
        if ((v = dom_attr_get(e, "size")) != NULL) {
            int n = v[0] == '+' ? 3 + (v[1] - '0') : v[0] == '-' ? 3 - (v[1] - '0') : v[0] - '0';
            hint(out, size, "font-size", font_sizes[clamp(n, 1, 7)], "");
        }
    } else if (is_tag(e, "body")) {
        if ((v = dom_attr_get(e, "text")) != NULL)
            hint(out, size, "color", v, "");
    } else if (is_tag(e, "ol") || is_tag(e, "ul") || is_tag(e, "li")) {
        if ((v = dom_attr_get(e, "type")) != NULL) {
            const char *type = strcmp(v, "1") == 0 ? "decimal" : strcmp(v, "a") == 0 ? "lower-alpha"
                             : strcmp(v, "A") == 0 ? "upper-alpha" : strcmp(v, "i") == 0 ? "lower-roman"
                             : strcmp(v, "I") == 0 ? "upper-roman" : v;
            hint(out, size, "list-style-type", type, "");
        }
    }
}

static style_t *root_style(lay_t *L)
{
    const html_text_t *t = L->opt->text;
    style_t *st = arena(L, sizeof(*st));
    st->back = t->page_color & 0xFFFFFF;
    st->size = t->base_size ? t->base_size : 15;
    st->align = CSS_LEFT;
    st->white_space = CSS_WS_NORMAL;
    st->transform = CSS_TT_NONE;
    st->list_type = CSS_LT_DISC;
    st->break_words = CSS_NO;
    st->link = -1;
    return st;
}

/* Setzt alles, was nicht vererbt wird, auf den Anfangswert. */
static void reset_box(style_t *st)
{
    static const len_t zero = { CSS_LEN_VALUE, 0, 0 }, automatic = { CSS_LEN_AUTO, 0, 0 };
    static const len_t unset = { CSS_LEN_UNSET, 0, 0 }, none = { CSS_LEN_NONE, 0, 0 };
    st->display = CSS_D_INLINE;
    st->position = CSS_POS_STATIC;
    st->float_side = CSS_SIDE_NONE;
    st->clear = CSS_SIDE_NONE;
    st->clear_after = CSS_SIDE_NONE;
    st->box_sizing = CSS_BOX_CONTENT;
    st->overflow = CSS_OV_VISIBLE;
    st->valign = CSS_VA_BASELINE;
    st->background_set = false;
    st->z_set = false;
    st->z = 0;
    st->radius = 0;
    for (int i = 0; i < 4; i++) {
        st->border[i] = 0;
        st->border_style[i] = 0;
        st->margin[i] = zero;
        st->padding[i] = zero;
        st->inset[i] = automatic;
    }
    st->width = st->height = automatic;
    st->min_width = st->min_height = unset;
    st->max_width = st->max_height = none;
    st->fg = NULL;
    st->anonymous = NULL;
}

/* Die Gestaltung namenloser Kästen: erbt alles Vererbbare, hat sonst nichts. */
static style_t *anonymous_style(lay_t *L, style_t *parent)
{
    if (!parent->anonymous) {
        style_t *st = arena(L, sizeof(*st));
        *st = *parent;
        reset_box(st);
        st->display = CSS_D_BLOCK;
        parent->anonymous = st;
    }
    return parent->anonymous;
}

static flexgrid_t *flexgrid(lay_t *L, style_t *st)
{
    if (!st->fg)
        st->fg = arena(L, sizeof(flexgrid_t));
    return st->fg;
}

/* Ist die Schrift auf diesem Grund zu lesen? */
static bool readable(uint32_t color, uint32_t back)
{
    int d = brightness(color) - brightness(back);
    return d > 70 || d < -70;
}

/*
 * Bestimmt, was für das Element gilt: die Regeln der Seite, seine Attribute,
 * die Grundregeln des Browsers - und was es von seiner Umgebung erbt.
 */
__attribute__((noinline))
static style_t *compute_style(lay_t *L, const dom_node *e, const style_t *ps, bool root)
{
    css_style_t cs;
    char hints[400];
    const char *inline_style = dom_attr_get(e, "style");
    hints_for(e, hints, sizeof(hints));
    css_match_full(L->css, &e->css, hints, strlen(hints), inline_style,
                   inline_style ? strlen(inline_style) : 0, &cs);

    style_t *st = arena(L, sizeof(*st));
    *st = *ps;
    reset_box(st);

    /* Schrift */
    if (cs.font_size.kind == CSS_LEN_VALUE) {
        /* Pixelangaben meinen eine Grundschrift von 16 Pixeln; em und Prozent die der Umgebung */
        int base = L->opt->text->base_size ? L->opt->text->base_size : 15;
        int64_t px = (int64_t)cs.font_size.px * base / 16 + (int64_t)cs.font_size.em * ps->size;
        st->size = (uint8_t)clamp((int)((px + 500) / 1000), 7, 120);
    }
    if (cs.font_weight)
        st->text = (uint8_t)(cs.font_weight == CSS_YES ? st->text | STYLE_BOLD : st->text & ~STYLE_BOLD);
    if (cs.font_slant)
        st->text = (uint8_t)(cs.font_slant == CSS_YES ? st->text | STYLE_ITALIC : st->text & ~STYLE_ITALIC);
    if (cs.font_family)
        st->text = (uint8_t)(cs.font_family == CSS_FF_MONO ? st->text | STYLE_MONO : st->text & ~STYLE_MONO);
    if (cs.line_height.kind == CSS_LEN_AUTO) {
        st->line_px = 0;
        st->line_factor = 0;
    } else if (cs.line_height.kind == CSS_LEN_VALUE) {
        if (cs.line_height.px == 0 && cs.line_height.em > 0) {
            st->line_px = 0;            /* als Vielfaches: gilt auch für größere Schrift darin */
            st->line_factor = cs.line_height.em;
        } else {
            st->line_px = (int16_t)clamp(make_len(&cs.line_height, st->size).px, 1, 2000);
            st->line_factor = 0;
        }
    }

    /* Farben */
    bool link = is_tag(e, "a") && dom_attr_get(e, "href");
    if (cs.has & CSS_COLOR) {
        st->color = HTML_COLOR_SET | cs.color;
        st->text &= (uint8_t)~STYLE_LINK;
    } else if (link) {
        st->color = 0;
        st->text |= STYLE_LINK;
    }
    if (cs.has & CSS_BACKGROUND) {
        st->background_set = true;
        st->background = cs.background;
    } else if (cs.gradient_set) {
        st->background_set = true;      /* Farbverlauf: ersatzweise seine erste Farbe */
        st->background = cs.gradient_color;
    }
    if (cs.masked == CSS_YES)
        st->background_set = false;     /* ein Symbol, das nur durch eine Maske zu sehen wäre: nichts zeichnen */
    if (st->background_set)
        st->back = st->background;
    if (cs.has & CSS_UNDERLINE)
        st->text = (uint8_t)(cs.underline ? st->text | STYLE_UNDERLINE : st->text & ~STYLE_UNDERLINE);
    else if (link)
        st->text |= STYLE_UNDERLINE;

    /* Text */
    if (cs.has & CSS_ALIGN)
        st->align = cs.align;
    if (cs.white_space)
        st->white_space = cs.white_space;
    if (cs.text_transform)
        st->transform = cs.text_transform;
    if (cs.list_type)
        st->list_type = cs.list_type;
    else if ((cs.has & CSS_LIST) && cs.list_plain)
        st->list_type = CSS_LT_NONE;
    if (cs.break_words)
        st->break_words = cs.break_words;
    if (cs.text_indent.kind == CSS_LEN_VALUE)
        st->indent = (int16_t)clamp(make_len(&cs.text_indent, st->size).px, -20000, 2000);
    if (cs.border_spacing.kind == CSS_LEN_VALUE)
        st->spacing = (uint8_t)clamp(make_len(&cs.border_spacing, st->size).px, 0, 100);
    if (cs.border_collapse)
        st->collapse = cs.border_collapse == CSS_YES;
    if (cs.caption_below)
        st->caption_below = cs.caption_below == CSS_YES;
    if (cs.has & CSS_VISIBILITY)
        st->invisible = cs.hidden;
    if (cs.transparent == CSS_YES)
        st->invisible = true;

    /* Art des Kastens */
    st->display = cs.display_kind ? cs.display_kind : CSS_D_INLINE;
    if ((cs.has & CSS_DISPLAY) && cs.display == CSS_NONE)
        st->display = CSS_D_NONE;
    if (((cs.has & CSS_CLIP) && cs.clipped) || cs.off_top)
        st->display = CSS_D_NONE;       /* nur für Vorleseprogramme gedacht */
    if (!L->author_rules) {
        /* ohne Stylesheets entscheiden ein paar Anzeichen, was verborgen sein soll */
        const char *classes = dom_attr_get(e, "class");
        if (classes && html_class_hides(classes, strlen(classes), L->opt->scripts))
            st->display = CSS_D_NONE;
    }
    if (cs.position)
        st->position = cs.position;
    if (cs.has & CSS_FLOAT)
        st->float_side = cs.float_side;
    if (cs.has & CSS_CLEAR)
        st->clear = cs.clear;
    st->clear_after = cs.clear_after;
    if (cs.box_sizing)
        st->box_sizing = cs.box_sizing;
    if (cs.overflow)
        st->overflow = cs.overflow;
    if (cs.vertical_align)
        st->valign = cs.vertical_align;
    else if (st->display == CSS_D_TABLE_CELL || st->display == CSS_D_TABLE_ROW ||
             st->display == CSS_D_TABLE_GROUP)
        st->valign = ps->display == CSS_D_TABLE_ROW || ps->display == CSS_D_TABLE_GROUP
                         ? ps->valign : CSS_VA_MIDDLE;
    if (cs.z_set) {
        st->z_set = true;
        st->z = cs.z_index;
    }
    if (st->position == CSS_POS_ABSOLUTE || st->position == CSS_POS_FIXED)
        st->float_side = CSS_SIDE_NONE;
    if (root && st->display != CSS_D_NONE)
        st->display = CSS_D_BLOCK;

    /* Abstände, Maße, Rahmen */
    for (int i = 0; i < 4; i++) {
        if (cs.margin[i].kind)
            st->margin[i] = make_len(&cs.margin[i], st->size);
        if (cs.padding[i].kind == CSS_LEN_VALUE)
            st->padding[i] = make_len(&cs.padding[i], st->size);
        if (cs.inset[i].kind)
            st->inset[i] = make_len(&cs.inset[i], st->size);
        uint8_t line = cs.border_style[i];
        if (line == CSS_BS_SOLID || line == CSS_BS_DASHED || line == CSS_BS_DOTTED) {
            int width = cs.border_width[i].kind == CSS_LEN_VALUE ? make_len(&cs.border_width[i], st->size).px : 3;
            if (cs.border_width[i].kind == CSS_LEN_VALUE && width == 0 && (cs.border_width[i].px > 0 || cs.border_width[i].em > 0))
                width = 1;              /* dünner als ein Pixel: trotzdem zu sehen */
            st->border[i] = (uint8_t)clamp(width, 0, 100);
            st->border_style[i] = cs.border_color_kind[i] == CSS_COLOR_TRANSPARENT ? 0 : line;
            st->border_color[i] = cs.border_color_kind[i] == CSS_COLOR_VALUE ? cs.border_color[i]
                                : st->color ? st->color & 0xFFFFFF : L->opt->text->text_color & 0xFFFFFF;
        }
    }
    if (cs.width.kind)
        st->width = make_len(&cs.width, st->size);
    if (cs.height.kind)
        st->height = make_len(&cs.height, st->size);
    if (cs.min_width.kind == CSS_LEN_VALUE)
        st->min_width = make_len(&cs.min_width, st->size);
    if (cs.max_width.kind == CSS_LEN_VALUE)
        st->max_width = make_len(&cs.max_width, st->size);
    if (cs.min_height.kind == CSS_LEN_VALUE)
        st->min_height = make_len(&cs.min_height, st->size);
    if (cs.max_height.kind == CSS_LEN_VALUE)
        st->max_height = make_len(&cs.max_height, st->size);
    if (cs.radius.kind == CSS_LEN_VALUE)
        st->radius = (uint8_t)(cs.radius.percent >= 40000 ? 255 : clamp(make_len(&cs.radius, st->size).px, 0, 250));

    /* flex und grid */
    bool container = st->display == CSS_D_FLEX || st->display == CSS_D_INLINE_FLEX ||
                     st->display == CSS_D_GRID || st->display == CSS_D_INLINE_GRID;
    if (container || cs.grow_set || cs.shrink_set || cs.flex_basis.kind || cs.align_self ||
        cs.order_set || cs.grid_span || cs.grid_full || cs.grid_area || cs.grid_column || cs.grid_row) {
        flexgrid_t *fg = flexgrid(L, st);
        fg->direction = cs.flex_direction ? cs.flex_direction : CSS_FD_ROW;
        fg->wrap = cs.flex_wrap ? cs.flex_wrap : CSS_FW_NOWRAP;
        fg->justify = cs.justify_content ? cs.justify_content : CSS_JU_START;
        fg->align_items = cs.align_items ? cs.align_items : CSS_AL_STRETCH;
        fg->align_self = cs.align_self;
        fg->align_content = cs.align_content;
        fg->grow_set = cs.grow_set;
        fg->shrink_set = cs.shrink_set;
        fg->grow = cs.flex_grow;
        fg->shrink = cs.flex_shrink;
        fg->order = cs.order_set ? cs.order : 0;
        fg->basis = make_len(&cs.flex_basis, st->size);
        fg->gap_row = make_len(&cs.gap_row, st->size);
        fg->gap_column = make_len(&cs.gap_column, st->size);
        fg->grid_count = cs.grid_set ? cs.grid_count : 0;
        fg->grid_auto_fill = cs.grid_auto_fill;
        fg->grid_span = cs.grid_span;
        fg->grid_full = cs.grid_full;
        fg->area_rows = cs.area_rows;
        fg->area_columns = cs.area_columns;
        memcpy(fg->area_map, cs.area_map, sizeof(fg->area_map));
        fg->grid_area = cs.grid_area;
        fg->grid_column = cs.grid_column;
        fg->grid_row = cs.grid_row;
        fg->grid_row_span = cs.grid_row_span;
        for (int i = 0; i < fg->grid_count; i++) {
            fg->grid[i].size = make_len(&cs.grid[i].size, st->size);
            fg->grid[i].fr = cs.grid[i].fr;
            fg->grid[i].automatic = cs.grid[i].automatic;
        }
    }
    /* zugeklappt (Höhe 0, Überstehendes abgeschnitten): wie nicht vorhanden */
    if (st->overflow == CSS_OV_HIDDEN &&
        ((len_fixed(st->height) && st->height.px == 0) || (len_fixed(st->max_height) && st->max_height.px == 0)))
        st->display = CSS_D_NONE;
    return st;
}

/* --- Der Baum der Kästen ----------------------------------------------------------------------- */

static lbox_t *new_box(lay_t *L, uint8_t kind, const dom_node *node, style_t *st)
{
    lbox_t *b = arena(L, sizeof(*b));
    if (++L->boxes > BOXES_MAX)
        L->failed = true;
    b->seq = (uint32_t)L->boxes;
    b->kind = kind;
    b->node = node;
    b->st = st;
    b->image = b->widget = -1;
    b->min_w = b->max_w = -1;
    b->baseline = -1;
    b->colspan = b->rowspan = 1;
    return b;
}

static void append(lbox_t *parent, lbox_t *child)
{
    child->parent = parent;
    child->next = NULL;
    if (parent->last)
        parent->last->next = child;
    else
        parent->first = child;
    parent->last = child;
}

/* Text, den der Seitenaufbau selbst beisteuert (Hinweise, Listenpunkte) */
static lbox_t *own_text(lay_t *L, lbox_t *parent, const dom_node *node, style_t *st, const char *text)
{
    size_t len = strlen(text);
    char *copy = arena(L, len + 1);
    lbox_t *b = new_box(L, K_TEXT, node, st);
    if (copy && len < SPARE_BYTES)
        memcpy(copy, text, len + 1);
    b->text = copy ? copy : "";
    b->text_len = copy ? (uint32_t)len : 0;
    b->flags = F_OWN_TEXT;
    append(parent, b);
    return b;
}

/* Steht der Kasten für sich (unter- oder nebeneinander) statt in einer Textzeile? */
static bool block_level(const lbox_t *b)
{
    return b->kind == K_BLOCK && !(b->flags & (F_ATOMIC | F_FLOAT | F_ABS));
}

static bool out_of_flow(const lbox_t *b)
{
    return (b->flags & (F_FLOAT | F_ABS)) != 0;
}

static bool blank_text(const lbox_t *b)
{
    if (b->kind != K_TEXT || b->st->white_space == CSS_WS_PRE || b->st->white_space == CSS_WS_PRE_WRAP)
        return false;
    for (uint32_t i = 0; i < b->text_len; i++) {
        if (!is_space(b->text[i]))
            return false;
    }
    return true;
}

/* Die vollständige Adresse zu einer Angabe der Seite; NULL, wenn sie nicht taugt. */
static char *resolve(lay_t *L, const char *reference, bool web_only)
{
    char *full = kmalloc(URL_SIZE), *out = NULL;
    if (full && reference && url_resolve(L->base, reference, full, URL_SIZE) &&
        (!web_only || memcmp(full, "http://", 7) == 0 || memcmp(full, "https://", 8) == 0))
        out = copy_text(full, strlen(full));
    kfree(full);
    return out;
}

static bool starts(const char *text, const char *prefix)
{
    while (*prefix) {
        if (lower(*text++) != *prefix++)
            return false;
    }
    return true;
}

/* Trägt das Ziel eines Links ein; liefert seine Nummer oder -1. */
static int add_link(lay_t *L, const char *href)
{
    page_t *page = L->page;
    char *target;
    if (!href || page->link_count >= LINKS_MAX)
        return -1;
    while (is_space(*href))
        href++;
    if ((href[0] == '#' && href[1]) || starts(href, "javascript:") ||
        (starts(href, "about:") && starts(L->base, "about:")))
        target = copy_text(href, strlen(href));     /* gilt nur in dieser Seite bzw. im Browser selbst */
    else
        target = resolve(L, href, false);
    if (!target || !GROW(L, page->links, page->link_count, page->link_cap)) {
        kfree(target);
        return -1;
    }
    page->links[page->link_count] = target;
    return page->link_count++;
}

static void add_target(lay_t *L, const char *id, lbox_t *box)
{
    if (!id || !id[0] || L->target_count >= ANCHORS_MAX)
        return;
    target_t *t = arena(L, sizeof(*t));
    t->id = copy_text(id, strlen(id));
    t->box = box;
    t->next = L->targets;
    L->targets = t;
    L->target_count++;
}

/* Der Text eines Elements samt allem darunter, Leerraum zusammengefasst (kmalloc). */
static char *flat_text(const dom_node *e, size_t limit)
{
    size_t len = 0, n = 0;
    char *raw = dom_text_content(e, &len);
    if (!raw)
        return NULL;
    bool gap = false;
    for (size_t i = 0; i < len && n < limit; i++) {
        if (is_space(raw[i])) {
            gap = n > 0;
        } else {
            if (gap)
                raw[n++] = ' ';
            gap = false;
            raw[n++] = raw[i];
        }
    }
    while (n > 0 && ((uint8_t)raw[n] & 0xC0) == 0x80)
        n--;                            /* nicht mitten in einem Zeichen enden */
    raw[n] = '\0';
    return raw;
}

static int count_chars(const char *text)
{
    int n = 0;
    for (; *text; text++) {
        if (((uint8_t)*text & 0xC0) != 0x80)
            n++;
    }
    return n;
}

/* --- Formularfelder ---------------------------------------------------------------------------- */

static int add_widget(lay_t *L, widget_kind_t kind, const dom_node *e)
{
    page_t *page = L->page;
    if (page->widget_count >= WIDGETS_MAX ||
        !GROW(L, page->widgets, page->widget_count, page->widget_cap))
        return -1;
    widget_t *w = &page->widgets[page->widget_count];
    const char *text;
    size_t len = 0;
    memset(w, 0, sizeof(*w));
    w->kind = kind;
    w->node = e->id;
    w->form = L->form;
    /* Abgeschaltete Felder werden nicht gesendet - am einfachsten: ohne Namen */
    if ((text = dom_attr_get(e, "name")) != NULL && !dom_attr_get(e, "disabled"))
        w->name = copy_text(text, strlen(text));
    w->value = dom_field_value(e, &len);
    if ((text = dom_attr_get(e, "placeholder")) != NULL) {
        w->placeholder = copy_text(text, strlen(text));
        if (w->placeholder)
            claude_plain_text(w->placeholder);
    }
    w->checked = e->checked >= 0 ? e->checked > 0 : dom_attr_get(e, "checked") != NULL;
    w->line = -1;
    return page->widget_count++;
}

static void set_label(widget_t *w, const char *label)
{
    w->label = copy_text(label, strlen(label));
    if (w->label)
        claude_plain_text(w->label);
}

/* Die Einträge einer Auswahlliste (auch die in Gruppen) */
static void add_options(lay_t *L, const dom_node *e, widget_t *w, const dom_node *select, int *widest)
{
    page_t *page = L->page;
    for (const dom_node *c = e->first; c; c = c->next) {
        if (is_tag(c, "optgroup")) {
            add_options(L, c, w, select, widest);
            continue;
        }
        if (!is_tag(c, "option") || !GROW(L, page->options, page->option_count, page->option_cap))
            continue;
        option_t *o = &page->options[page->option_count];
        const char *value = dom_attr_get(c, "value");
        o->label = flat_text(c, 200);
        if (!o->label)
            o->label = copy_text("", 0);
        if (o->label)
            claude_plain_text(o->label);
        /* Ohne eigenes value-Attribut wird die Beschriftung gesendet. */
        o->value = value ? copy_text(value, strlen(value)) : copy_text(o->label ? o->label : "", o->label ? strlen(o->label) : 0);
        if (select->selected >= 0 ? select->selected == w->option_count
                                  : dom_attr_get(c, "selected") != NULL)
            w->selected = w->option_count;
        int n = o->label ? count_chars(o->label) : 0;
        if (n > *widest)
            *widest = n;
        w->option_count++;
        page->option_count++;
    }
}

/* Ein Kasten fester Größe in der Zeile: Feld, Bild, Platzhalter */
static lbox_t *replaced_box(lay_t *L, lbox_t *parent, const dom_node *e, style_t *st, int w, int h)
{
    lbox_t *b = new_box(L, K_BLOCK, e, st);
    b->flags = F_REPLACED | F_ROOT;
    b->natural_w = w;
    b->natural_h = h;
    if (st->position == CSS_POS_ABSOLUTE || st->position == CSS_POS_FIXED)
        b->flags |= F_ABS | (st->position == CSS_POS_FIXED ? F_FIXED : 0);
    else if (st->float_side != CSS_SIDE_NONE)
        b->flags |= F_FLOAT;
    else if (st->display == CSS_D_BLOCK || st->display == CSS_D_FLEX || st->display == CSS_D_GRID ||
             st->display == CSS_D_TABLE || st->display == CSS_D_LIST_ITEM || st->display == CSS_D_FLOW_ROOT)
        ;                               /* steht als Block für sich */
    else
        b->flags |= F_ATOMIC;
    if (st->position == CSS_POS_RELATIVE)
        b->flags |= F_RELATIVE;
    append(parent, b);
    return b;
}

static void note_text(lay_t *L, lbox_t *parent, const dom_node *e, style_t *st, const char *text)
{
    style_t *muted = arena(L, sizeof(*muted));
    *muted = *st;
    reset_box(muted);
    muted->text |= STYLE_MUTED;
    muted->color = 0;
    own_text(L, parent, e, muted, text);
}

/* So breit wird ein Knopf: seine Beschriftung (in der Rasterschrift oder der der Seite) und Rand */
static int button_width(const lay_t *L, const style_t *st, const char *label)
{
    if (!label)
        label = "";
    int plain = count_chars(label) * 8;
    int styled = st->background_set ? text_width(L, st, label, strlen(label)) : 0;
    return max_int(plain, styled) + (st->background_set ? 8 : 32);
}

/* <input>: je nach Art ein Feld, ein Knopf, ein Kästchen - oder nichts */
static void build_input(lay_t *L, lbox_t *parent, const dom_node *e, style_t *st)
{
    const char *type = dom_attr_get(e, "type");
    bool scripts = L->opt->scripts;
    int index;
    if (!type)
        type = "text";
    if (same(type, "hidden")) {
        add_widget(L, W_HIDDEN, e);
    } else if ((same(type, "checkbox") || same(type, "radio")) && L->form < 0 &&
               !dom_attr_get(e, "name") && (!scripts || dom_attr_get(e, "role"))) {
        /*
         * Ohne Namen und Formular: ein Schalter für die Gestaltung, kein
         * Eingabefeld. Mit Skripten kann es doch eines sein - außer es gibt
         * sich selbst als etwas anderes aus (role="button": Aufklappmenüs).
         */
    } else if (same(type, "checkbox") || same(type, "radio")) {
        index = add_widget(L, lower(type[0]) == 'c' ? W_CHECKBOX : W_RADIO, e);
        if (index < 0)
            return;
        widget_t *w = &L->page->widgets[index];
        if (!w->value || !dom_attr_get(e, "value")) {
            kfree(w->value);
            w->value = copy_text("on", 2);
        }
        replaced_box(L, parent, e, st, 22, 19)->widget = (int16_t)index;
    } else if (same(type, "submit") || same(type, "image") || same(type, "button") || same(type, "reset")) {
        bool sends = same(type, "submit") || same(type, "image");
        if (!sends && !scripts)
            return;                     /* Knöpfe, die nur mit Skripten etwas tun */
        index = add_widget(L, sends ? W_SUBMIT : W_BUTTON, e);
        if (index < 0)
            return;
        widget_t *w = &L->page->widgets[index];
        set_label(w, w->value && w->value[0] ? w->value : sends ? "Senden"
                     : same(type, "reset") ? "Zurücksetzen" : "Knopf");
        replaced_box(L, parent, e, st, button_width(L, st, w->label), 24)->widget = (int16_t)index;
    } else if (same(type, "file")) {
        note_text(L, parent, e, st, "[Datei auswählen: hier nicht möglich]");
    } else {
        index = add_widget(L, same(type, "password") ? W_PASSWORD : W_TEXT, e);
        if (index < 0)
            return;
        if (!L->page->widgets[index].value)
            L->page->widgets[index].value = copy_text("", 0);
        int size = clamp(attr_int(e, "size", 24), 6, 44);
        replaced_box(L, parent, e, st, (size + 2) * 8, 24)->widget = (int16_t)index;
    }
}

static void build_select(lay_t *L, lbox_t *parent, const dom_node *e, style_t *st)
{
    int index = add_widget(L, W_SELECT, e), widest = 6;
    if (index < 0)
        return;
    widget_t *w = &L->page->widgets[index];
    w->first_option = L->page->option_count;
    add_options(L, e, w, e, &widest);
    w = &L->page->widgets[index];
    if (w->selected >= w->option_count)
        w->selected = 0;
    replaced_box(L, parent, e, st, (min_int(widest, 36) + 4) * 8, 24)->widget = (int16_t)index;
}

static void build_textarea(lay_t *L, lbox_t *parent, const dom_node *e, style_t *st)
{
    int index = add_widget(L, W_TEXTAREA, e);
    if (index < 0)
        return;
    if (!L->page->widgets[index].value)
        L->page->widgets[index].value = copy_text("", 0);
    int columns = clamp(attr_int(e, "cols", 40), 10, 60), rows = clamp(attr_int(e, "rows", 2), 1, 6);
    replaced_box(L, parent, e, st, (columns + 2) * 8, rows * 16 + 8)->widget = (int16_t)index;
}

static void build_button(lay_t *L, lbox_t *parent, const dom_node *e, style_t *st)
{
    const char *type = dom_attr_get(e, "type");
    bool sends = L->form >= 0 && (!type || same(type, "submit"));
    if (!sends && !L->opt->scripts)
        return;                         /* ohne Formular tut er nur mit Skripten etwas */
    int index = add_widget(L, sends ? W_SUBMIT : W_BUTTON, e);
    if (index < 0)
        return;
    widget_t *w = &L->page->widgets[index];
    char *label = flat_text(e, 120);
    set_label(w, label && label[0] ? label : w->value && w->value[0] ? w->value : sends ? "Senden" : "Knopf");
    kfree(label);
    replaced_box(L, parent, e, st, button_width(L, st, w->label), 24)->widget = (int16_t)index;
}

/* --- Bilder und was sich hier nicht zeigen lässt --------------------------------------------- */

/* Die Adresse des Bildes: "src", bei nachladenden Seiten auch deren Ersatzangaben. */
static char *picture_address(lay_t *L, const dom_node *e)
{
    static const char *const names[] = { "src", "data-src", "data-lazy-src", "data-original", NULL };
    for (int i = 0; names[i]; i++) {
        const char *value = dom_attr_get(e, names[i]);
        if (value && value[0] && !starts(value, "data:")) {
            char *full = resolve(L, value, true);
            if (full)
                return full;
        }
    }
    return NULL;
}

static void build_image(lay_t *L, lbox_t *parent, const dom_node *e, style_t *st)
{
    page_t *page = L->page;
    const html_pictures_t *pictures = L->opt->pictures;
    const char *alt = dom_attr_get(e, "alt");
    char text[160];
    char *url = pictures && page->image_count < IMAGES_MAX ? picture_address(L, e) : NULL;
    int hint_w = attr_int(e, "width", 0), hint_h = attr_int(e, "height", 0);
    int natural_w = 0, natural_h = 0;
    picture_state_t state = url ? pictures->lookup(pictures->ctx, url, &natural_w, &natural_h)
                                : PICTURE_BROKEN;
    bool ready = state == PICTURE_READY && natural_w > 0 && natural_h > 0;

    /* Zählpixel und Abstandhalter sind keine Bilder für den Leser */
    if ((hint_w > 0 && hint_w <= 2) || (hint_h > 0 && hint_h <= 2) ||
        (ready && (natural_w <= 2 || natural_h <= 2))) {
        kfree(url);
        return;
    }
    bool sized = len_fixed(st->width) && len_fixed(st->height);
    if (state == PICTURE_BROKEN || (!ready && !(hint_w > 0 && hint_h > 0) && !sized)) {
        /* nicht zu zeigen (oder noch nicht da und ohne Maße): der Ersatztext steht im Lauftext */
        if (alt && alt[0] && strlen(alt) <= 120) {
            ksnprintf(text, sizeof(text), state == PICTURE_BROKEN ? "%s" : "[Bild: %s]", alt);
            note_text(L, parent, e, st, text);
        } else if (state != PICTURE_BROKEN) {
            note_text(L, parent, e, st, "[Bild]");
        }
        if (state == PICTURE_BROKEN) {
            kfree(url);
            return;
        }
    }
    if (!GROW(L, page->images, page->image_count, page->image_cap)) {
        kfree(url);
        return;
    }
    int index = page->image_count++;
    page_image_t *image = &page->images[index];
    memset(image, 0, sizeof(*image));
    image->url = url;
    if (alt && alt[0] && strlen(alt) <= 120)
        image->alt = copy_text(alt, strlen(alt));
    image->link = st->link;
    image->node = e->id;
    image->ready = ready;
    image->y = -1;                      /* steht (noch) nirgends */
    if (!ready && !(hint_w > 0 && hint_h > 0) && !sized)
        return;                         /* wird geholt; gezeigt wird solange der Ersatztext */

    /* Maße ohne Zutun der Gestaltung: die Angaben der Seite, sonst die des Bildes selbst */
    int w = hint_w > 0 ? hint_w : natural_w, h = hint_h > 0 ? hint_h : natural_h;
    if (ready && hint_w > 0 && hint_h <= 0)
        h = (int)((int64_t)hint_w * natural_h / natural_w);
    else if (ready && hint_h > 0 && hint_w <= 0)
        w = (int)((int64_t)hint_h * natural_w / natural_h);
    lbox_t *b = replaced_box(L, parent, e, st, max_int(w, 1), max_int(h, 1));
    b->image = (int16_t)index;
}

/* Ein Hinweis für etwas, das sich hier nicht zeigen lässt - als Link auf die Datei oder Seite. */
static void build_media(lay_t *L, lbox_t *parent, const dom_node *e, style_t *st, const char *what)
{
    const char *address = dom_attr_get(e, is_tag(e, "object") ? "data" : "src");
    for (const dom_node *c = e->first; c && !address; c = c->next) {
        if (is_tag(c, "source"))
            address = dom_attr_get(c, "src");
    }
    char *full = address && address[0] ? resolve(L, address, true) : NULL;
    if (!full)
        return;
    /* benannt nach dem Titel - oder dem letzten Stück der Adresse */
    const char *title = dom_attr_get(e, "title"), *name = full, *stop = full;
    char text[160];
    while (*stop && *stop != '?' && *stop != '#')
        stop++;
    for (const char *p = full; p < stop; p++) {
        if (*p == '/' && p + 1 < stop)
            name = p + 1;
    }
    int n = (int)(stop - name);
    if (title && title[0])
        ksnprintf(text, sizeof(text), "[%s: %.60s]", what, title);
    else
        ksnprintf(text, sizeof(text), "[%s: %.*s]", what, n > 60 ? 60 : n, name);
    style_t *link = arena(L, sizeof(*link));
    *link = *st;
    reset_box(link);
    link->link = (int16_t)add_link(L, full);
    link->color = 0;
    link->text |= STYLE_LINK;
    kfree(full);
    own_text(L, parent, e, link, text);
}

/* <progress> und <meter>: ein Balken aus Zeichen */
static void build_bar(lay_t *L, lbox_t *parent, const dom_node *e, style_t *st, bool meter)
{
    int low = meter ? attr_int(e, "min", 0) : 0, high = attr_int(e, "max", 1), value = 0;
    const char *raw = dom_attr_get(e, "value");
    char text[64];
    size_t n = 0;
    if (!raw || high <= low) {
        if (!meter)
            note_text(L, parent, e, st, "[läuft ...]");
        return;
    }
    /* der Wert mit zwei Nachkommastellen */
    long hundredths = 0;
    int digits = -1;
    for (const char *p = raw; *p && digits < 2; p++) {
        if (*p >= '0' && *p <= '9') {
            hundredths = hundredths * 10 + (*p - '0');
            if (digits >= 0)
                digits++;
        } else if (*p == '.' && digits < 0) {
            digits = 0;
        }
    }
    for (int i = digits < 0 ? 0 : digits; i < 2; i++)
        hundredths *= 10;
    value = (int)((hundredths - low * 100L) / (high - low));
    value = clamp(value, 0, 100);
    for (int i = 0; i < 10; i++) {
        memcpy(text + n, i * 10 + 5 <= value ? "\xE2\x96\x88" : "\xE2\x96\x91", 3);
        n += 3;
    }
    ksnprintf(text + n, sizeof(text) - n, " %d %%", value);
    own_text(L, parent, e, st, text);
}

/* --- Elemente in Kästen umsetzen -------------------------------------------------------------- */

static void build_children(lay_t *L, const dom_node *e, lbox_t *parent, style_t *st, int depth);

/* Nimmt die Kinder von first bis vor stop aus dem Kasten und steckt sie in einen neuen. */
static lbox_t *wrap_run(lay_t *L, lbox_t *box, lbox_t *before, lbox_t *first, lbox_t *stop,
                        style_t *st)
{
    lbox_t *w = new_box(L, K_BLOCK, NULL, st);
    w->flags = F_ANON;
    w->parent = box;
    lbox_t *last = NULL;
    for (lbox_t *c = first; c != stop; c = c->next) {
        c->parent = w;
        last = c;
    }
    w->first = first;
    w->last = last;
    if (last)
        last->next = NULL;
    w->next = stop;
    if (before)
        before->next = w;
    else
        box->first = w;
    if (!stop)
        box->last = w;
    return w;
}

static bool table_part(const lbox_t *b)
{
    uint8_t d = b->kind == K_BLOCK ? b->st->display : 0;
    return d == CSS_D_TABLE_ROW || d == CSS_D_TABLE_GROUP || d == CSS_D_TABLE_CELL ||
           d == CSS_D_TABLE_CAPTION;
}

/* Die Gestaltung eines namenlosen Kastens einer bestimmten Art (Tabelle, Zeile) */
static style_t *anonymous_as(lay_t *L, style_t *parent, uint8_t display)
{
    style_t *st = arena(L, sizeof(*st));
    *st = *anonymous_style(L, parent);
    st->display = display;
    st->anonymous = NULL;
    return st;
}

/* Entfernt Kinder, die nur aus Leerraum bestehen (zwischen Kästen bedeutet er nichts). */
static void drop_blank(lbox_t *box)
{
    lbox_t *prev = NULL;
    for (lbox_t *c = box->first; c; c = c->next) {
        if (blank_text(c)) {
            if (prev)
                prev->next = c->next;
            else
                box->first = c->next;
            if (box->last == c)
                box->last = prev;
        } else {
            prev = c;
        }
    }
}

/*
 * Bringt die Kinder eines Kastens in Ordnung, nachdem sie gebaut sind: Text
 * zwischen Kästen bekommt eigene namenlose Kästen, Teile von Tabellen ohne
 * Tabelle bekommen eine, und was in einer Reihe (flex, grid) steht, wird zu
 * eigenständigen Kästen.
 */
static void fix_children(lay_t *L, lbox_t *box)
{
    uint8_t display = box->st->display;
    bool row = display == CSS_D_TABLE_ROW;
    bool table = box->inner == IN_TABLE || display == CSS_D_TABLE_GROUP;

    if (box->inner == IN_FLEX || box->inner == IN_GRID || table || row) {
        /* Jedes Kind ist ein Kasten für sich; loser Text bekommt einen namenlosen */
        drop_blank(box);
        lbox_t *prev = NULL;
        bool cells = table || row;
        for (lbox_t *c = box->first; c; prev = c, c = c->next) {
            if (c->flags & F_ABS)
                continue;
            if (c->kind == K_INLINE && !cells) {
                /* in einer Reihe oder einem Raster ist jedes Element ein Kasten für sich */
                style_t *own = arena(L, sizeof(*own));
                *own = *c->st;
                own->display = CSS_D_BLOCK;
                own->anonymous = NULL;
                c->st = own;
                c->kind = K_BLOCK;
                c->flags |= F_LINES;
            } else if (c->kind != K_BLOCK) {
                /* loser Text (in Tabellen: alles, was kein Kasten ist) bekommt einen namenlosen */
                lbox_t *stop = c;
                while (stop && stop->kind != K_BLOCK && (cells || stop->kind != K_INLINE))
                    stop = stop->next;
                c = wrap_run(L, box, prev, c, stop, anonymous_style(L, box->st));
                c->flags |= F_LINES;
            }
            if (!table && !row) {
                c->flags &= (uint16_t)~(F_ATOMIC | F_FLOAT);
                c->flags |= F_ROOT;
            } else if (row) {
                c->flags &= (uint16_t)~(F_ATOMIC | F_FLOAT);
                c->flags |= F_ROOT;     /* in einer Tabellenzeile gilt jedes Kind als Zelle */
            }
        }
        return;
    }

    /* Teile einer Tabelle ohne Tabelle darum: Sie bekommen eine namenlose */
    if (display != CSS_D_TABLE && display != CSS_D_INLINE_TABLE) {
        lbox_t *prev = NULL;
        for (lbox_t *c = box->first; c; prev = c, c = c->next) {
            if (!table_part(c) || c->st->display == CSS_D_TABLE_CAPTION)
                continue;
            bool cells = c->st->display == CSS_D_TABLE_CELL;
            lbox_t *stop = c;
            while (stop && ((table_part(stop) && (stop->st->display == CSS_D_TABLE_CELL) == cells &&
                             stop->st->display != CSS_D_TABLE_CAPTION) || blank_text(stop)))
                stop = stop->next;
            lbox_t *inner = wrap_run(L, box, prev, c, stop, anonymous_as(L, box->st, cells ? CSS_D_TABLE_ROW : CSS_D_TABLE));
            if (cells) {
                fix_children(L, inner);
                inner = wrap_run(L, box, prev, inner, inner->next, anonymous_as(L, box->st, CSS_D_TABLE));
            }
            inner->inner = IN_TABLE;
            inner->flags |= F_ROOT;
            fix_children(L, inner);
            c = inner;
        }
    }

    bool blocks = false;
    for (lbox_t *c = box->first; c && !blocks; c = c->next)
        blocks = block_level(c);
    if (!blocks) {
        box->flags |= F_LINES;
        return;
    }
    /* gemischt: Was in Zeilen steht, kommt zwischen den Kästen in namenlose Kästen */
    lbox_t *prev = NULL, *c = box->first;
    while (c) {
        if (block_level(c)) {
            prev = c;
            c = c->next;
            continue;
        }
        lbox_t *stop = c;
        bool content = false;
        for (; stop && !block_level(stop); stop = stop->next)
            content = content || (!blank_text(stop) && !out_of_flow(stop));
        if (!content) {
            /* nur Leerraum (der entfällt) und Kästen neben dem Text (die bleiben, wo sie sind) */
            for (lbox_t *x = c; x != stop;) {
                lbox_t *next = x->next;
                if (blank_text(x)) {
                    if (prev)
                        prev->next = next;
                    else
                        box->first = next;
                    if (box->last == x)
                        box->last = prev;
                } else {
                    prev = x;
                }
                x = next;
            }
            c = stop;
            continue;
        }
        lbox_t *w = wrap_run(L, box, prev, c, stop, anonymous_style(L, box->st));
        w->flags |= F_LINES;
        prev = w;
        c = stop;
    }
}

/* Baut den Kasten für ein Element und alles darin. */
static void build_element(lay_t *L, const dom_node *e, lbox_t *parent, style_t *pst, int depth,
                          int *counter)
{
    static const char *const media[] = { "video", "audio", "iframe", "object", "embed", NULL };
    if (depth > DEPTH_MAX || L->failed)
        return;
    if (is_tag(e, "noscript") && L->opt->scripts)
        return;                         /* nur für Browser ohne Skripte gedacht */
    style_t *st = compute_style(L, e, pst, false);
    if (st->display == CSS_D_NONE || st->display == CSS_D_TABLE_COLUMN) {
        /* unsichtbare Felder eines Formulars werden trotzdem gesendet */
        if (is_tag(e, "input") && L->form >= 0) {
            const char *type = dom_attr_get(e, "type");
            if (type && same(type, "hidden"))
                add_widget(L, W_HIDDEN, e);
        }
        return;
    }
    if (is_tag(e, "a") && pst->link < 0 && dom_attr_get(e, "href"))
        st->link = (int16_t)add_link(L, dom_attr_get(e, "href"));

    /* Elemente mit eigenem Inhalt statt Kindern */
    if (is_tag(e, "br")) {
        lbox_t *b = new_box(L, K_BREAK, e, st);
        append(parent, b);
        return;
    }
    if (is_tag(e, "wbr"))
        return;
    if (is_tag(e, "img")) {
        build_image(L, parent, e, st);
        return;
    }
    if (is_tag(e, "input")) {
        build_input(L, parent, e, st);
        return;
    }
    if (is_tag(e, "select")) {
        build_select(L, parent, e, st);
        return;
    }
    if (is_tag(e, "textarea")) {
        build_textarea(L, parent, e, st);
        return;
    }
    if (is_tag(e, "button")) {
        build_button(L, parent, e, st);
        return;
    }
    if (one_of(e, media)) {
        build_media(L, parent, e, st, is_tag(e, "video") ? "Video" : is_tag(e, "audio") ? "Ton"
                                      : is_tag(e, "iframe") ? "Eingebettete Seite" : "Eingebettetes Objekt");
        return;
    }
    if (is_tag(e, "progress") || is_tag(e, "meter")) {
        build_bar(L, parent, e, st, is_tag(e, "meter"));
        return;
    }
    if (is_tag(e, "canvas") || is_tag(e, "svg") || is_tag(e, "math")) {
        /* eine Fläche, auf der hier nichts erscheint: Sie hält nur ihren Platz frei */
        bool canvas = is_tag(e, "canvas");
        replaced_box(L, parent, e, st, attr_int(e, "width", canvas ? 300 : 16),
                     attr_int(e, "height", canvas ? 150 : 16));
        return;
    }
    if (st->display == CSS_D_CONTENTS) {
        build_children(L, e, parent, st, depth);
        return;
    }

    int form = L->form;
    if (is_tag(e, "form") && GROW(L, L->page->forms, L->page->form_count, L->page->form_cap)) {
        form_t *f = &L->page->forms[L->page->form_count];
        const char *action = dom_attr_get(e, "action"), *method = dom_attr_get(e, "method");
        /* Ohne Angabe geht das Formular an die Adresse der Seite selbst. */
        f->action = resolve(L, action ? action : "", false);
        f->post = method && same(method, "post");
        f->node = e->id;
        L->form = L->page->form_count++;
    }

    uint8_t d = st->display;
    bool absolute = st->position == CSS_POS_ABSOLUTE || st->position == CSS_POS_FIXED;
    bool floats = st->float_side != CSS_SIDE_NONE && !absolute;
    lbox_t *b = new_box(L, d == CSS_D_INLINE && !absolute && !floats ? K_INLINE : K_BLOCK, e, st);
    b->inner = d == CSS_D_FLEX || d == CSS_D_INLINE_FLEX ? IN_FLEX
             : d == CSS_D_GRID || d == CSS_D_INLINE_GRID ? IN_GRID
             : d == CSS_D_TABLE || d == CSS_D_INLINE_TABLE ? IN_TABLE : IN_FLOW;
    if (absolute)
        b->flags |= F_ABS | F_ROOT | (st->position == CSS_POS_FIXED ? F_FIXED : 0);
    else if (floats)
        b->flags |= F_FLOAT | F_ROOT;
    else if (d == CSS_D_INLINE_BLOCK || d == CSS_D_INLINE_FLEX || d == CSS_D_INLINE_GRID ||
             d == CSS_D_INLINE_TABLE)
        b->flags |= F_ATOMIC | F_ROOT;
    if (st->position == CSS_POS_RELATIVE)
        b->flags |= F_RELATIVE;
    if (b->kind == K_BLOCK && (b->inner != IN_FLOW || st->overflow != CSS_OV_VISIBLE ||
                               d == CSS_D_TABLE_CELL || d == CSS_D_TABLE_CAPTION || d == CSS_D_FLOW_ROOT))
        b->flags |= F_ROOT;
    if (d == CSS_D_LIST_ITEM) {
        if (counter) {
            int value;
            if (attr_number(e, "value", &value, NULL))
                *counter = value;
            b->ordinal = (*counter)++;
        }
        if (st->list_type != CSS_LT_NONE)
            b->flags |= F_MARKER;
    }
    if (d == CSS_D_TABLE_CELL) {
        b->colspan = (uint8_t)clamp(attr_int(e, "colspan", 1), 1, COLUMNS_MAX);
        b->rowspan = (uint8_t)clamp(attr_int(e, "rowspan", 1), 1, 200);
    }
    append(parent, b);
    add_target(L, dom_attr_get(e, "id"), b);
    if (is_tag(e, "a") && !dom_attr_get(e, "id"))
        add_target(L, dom_attr_get(e, "name"), b);
    if (is_tag(e, "h1") && !L->heading)
        L->heading = b;

    build_children(L, e, b, st, depth);
    L->form = form;

    if (b->kind == K_INLINE) {
        /* Ein Element im Text, das Kästen enthält, wird selbst zum Kasten */
        bool blocks = false;
        for (lbox_t *c = b->first; c && !blocks; c = c->next)
            blocks = block_level(c);
        if (!blocks)
            return;
        b->kind = K_BLOCK;
        style_t *own = arena(L, sizeof(*own));
        *own = *st;
        own->display = CSS_D_BLOCK;
        own->anonymous = NULL;
        b->st = own;
    }
    fix_children(L, b);
}

static void build_children(lay_t *L, const dom_node *e, lbox_t *parent, style_t *st, int depth)
{
    /* <details>: Zugeklappt ist nur seine Überschrift (<summary>) zu sehen. */
    bool closed = is_tag(e, "details") && L->opt->scripts && !dom_attr_get(e, "open");
    bool list = is_tag(e, "ol") || is_tag(e, "ul") || is_tag(e, "menu");
    int counter = list ? attr_int(e, "start", 1) : 0;
    if (is_tag(e, "summary") && is_tag(e->parent, "details")) {
        bool open = !L->opt->scripts || dom_attr_get(e->parent, "open");
        own_text(L, parent, e, st, open ? "\xE2\x96\xBE " : "\xE2\x96\xB8 ");
    }
    for (const dom_node *c = e->first; c && !L->failed; c = c->next) {
        if (c->type == DOM_ELEMENT) {
            if (closed && !is_tag(c, "summary"))
                continue;
            build_element(L, c, parent, st, depth + 1, list ? &counter : NULL);
        } else if (c->type == DOM_TEXT && c->text_len && !closed && !c->raw) {
            lbox_t *t = new_box(L, K_TEXT, e, st);
            t->text = c->text;
            t->text_len = c->text_len;
            append(parent, t);
        }
    }
}

/* --- Maße eines Kastens ------------------------------------------------------------------------ */

static void intrinsic(lay_t *L, lbox_t *b);
static void layout_block(lay_t *L, lbox_t *b, int width, floats_t *fc, int fx, int fy);

/* Die erste Zelle einer Tabelle (oder NULL) */
static const lbox_t *first_cell(const lbox_t *b, int depth)
{
    for (const lbox_t *c = b->first; c && depth < 3; c = c->next) {
        if (c->kind != K_BLOCK)
            continue;
        if (c->st->display == CSS_D_TABLE_CELL)
            return c;
        const lbox_t *inner = first_cell(c, depth + 1);
        if (inner)
            return inner;
    }
    return NULL;
}

/* Rahmen, Innen- und Außenabstand in Pixeln; base: Breite des umgebenden Inhalts (für Prozente) */
static void resolve_edges(lbox_t *b, int base)
{
    for (int i = 0; i < 4; i++) {
        b->pad[i] = max_int(len_px(b->st->padding[i], base), 0);
        b->margin[i] = len_px(b->st->margin[i], base);  /* "auto" zählt hier als 0 */
        b->bor[i] = b->st->border[i];
    }
    if (b->inner == IN_TABLE && b->st->collapse) {
        /* gemeinsame Rahmen: Haben die Zellen einen, bilden sie auch den Rand der Tabelle */
        const lbox_t *cell = first_cell(b, 0);
        if (cell && cell->st->border[3]) {
            memset(b->bor, 0, sizeof(b->bor));
            memset(b->pad, 0, sizeof(b->pad));
        }
    }
}

static int edge_w(const lbox_t *b)
{
    return b->pad[1] + b->pad[3] + b->bor[1] + b->bor[3];
}

static int edge_h(const lbox_t *b)
{
    return b->pad[0] + b->pad[2] + b->bor[0] + b->bor[2];
}

/* Die Breite samt Rahmen, die die Seite für den Kasten festlegt; false: keine Angabe */
static bool given_width(const lbox_t *b, int base, int *w)
{
    const style_t *st = b->st;
    if (!len_set(st->width, base))
        return false;
    int v = len_px(st->width, base);
    *w = st->box_sizing == CSS_BOX_BORDER ? max_int(v, edge_w(b)) : v + edge_w(b);
    return true;
}

static int limit_width(const lbox_t *b, int base, int w)
{
    const style_t *st = b->st;
    int extra = st->box_sizing == CSS_BOX_BORDER ? 0 : edge_w(b);
    if (len_set(st->max_width, base))
        w = min_int(w, len_px(st->max_width, base) + extra);
    if (len_set(st->min_width, base))
        w = max_int(w, len_px(st->min_width, base) + extra);
    return max_int(w, 0);
}

/* Die Größe eines Bildes, Felds oder Platzhalters (Außenkante Rahmen) */
static void replaced_size(lbox_t *b, int base, int *out_w, int *out_h)
{
    const style_t *st = b->st;
    int cw = len_set(st->width, base) ? len_px(st->width, base) : -1;
    int ch = len_fixed(st->height) ? st->height.px : -1;
    int w, h;
    if (b->widget >= 0) {
        /*
         * Ein Feld zeichnet seinen Rahmen selbst: Innenabstand und Rahmen der
         * Seite zählen nur zu seiner Größe.
         */
        int extra_w = edge_w(b), extra_h = edge_h(b);
        bool border_box = st->box_sizing == CSS_BOX_BORDER;
        w = cw >= 0 ? (border_box ? cw : cw + extra_w) : b->natural_w + b->pad[1] + b->pad[3];
        h = ch >= 0 ? (border_box ? ch : ch + extra_h) : min_int(b->natural_h + b->pad[0] + b->pad[2], 64);
        if (len_set(st->max_width, base))
            w = min_int(w, len_px(st->max_width, base) + (border_box ? 0 : extra_w));
        if (len_set(st->min_width, base))
            w = max_int(w, len_px(st->min_width, base) + (border_box ? 0 : extra_w));
        if (base >= 0)
            w = min_int(w, base);
        if (len_fixed(st->min_height))
            h = max_int(h, st->min_height.px + (border_box ? 0 : extra_h));
        w = max_int(w, 22);
        h = clamp(h, 17, 400);
        for (int i = 0; i < 4; i++) {
            b->pad[i] = 0;
            b->bor[i] = 0;
        }
        *out_w = w;
        *out_h = h;
        return;
    }
    int nw = max_int(b->natural_w, 1), nh = max_int(b->natural_h, 1);
    if (cw >= 0 && ch >= 0) {
        w = cw;
        h = ch;
    } else if (cw >= 0) {
        w = cw;
        h = (int)((int64_t)cw * nh / nw);
    } else if (ch >= 0) {
        h = ch;
        w = (int)((int64_t)ch * nw / nh);
    } else {
        w = nw;
        h = nh;
    }
    if (st->box_sizing == CSS_BOX_BORDER) {
        if (cw >= 0)
            w = max_int(w - edge_w(b), 0);
        if (ch >= 0)
            h = max_int(h - edge_h(b), 0);
    }
    /* nie breiter als erlaubt und als der Platz; das Seitenverhältnis bleibt */
    int most = len_set(st->max_width, base) ? len_px(st->max_width, base) : HUGE_WIDTH;
    if (base >= 0)
        most = min_int(most, max_int(base - edge_w(b), 1));
    if (w > most && w > 0) {
        if (ch < 0 || cw < 0)
            h = (int)((int64_t)h * most / w);
        w = most;
    }
    int tallest = len_fixed(st->max_height) ? min_int(st->max_height.px, IMAGE_TALLEST) : IMAGE_TALLEST;
    if (h > tallest && h > 0) {
        if (b->image >= 0)
            w = (int)((int64_t)w * tallest / h);
        h = tallest;
    }
    if (len_set(st->min_width, base))
        w = max_int(w, len_px(st->min_width, base));
    *out_w = max_int(w, 1) + edge_w(b);
    *out_h = max_int(h, 1) + edge_h(b);
}

/* Die Breite eines Kastens, der sich nach seinem Inhalt richtet; avail: Platz für ihn samt Rahmen */
static int fit_width(lay_t *L, lbox_t *b, int avail, int base)
{
    int w;
    if (b->flags & F_REPLACED) {
        int h;
        replaced_size(b, base, &w, &h);
        return w;
    }
    intrinsic(L, b);
    if (!given_width(b, base, &w))
        w = min_int(max_int(b->min_w, avail), b->max_w);
    else if (b->inner == IN_TABLE)
        w = max_int(w, b->min_w);
    return limit_width(b, base, w);
}

/* --- Kästen neben dem Text ------------------------------------------------------------------- */

/* Der freie Bereich in der Höhe y bis y+h zwischen den Kästen daneben; Ausgang: [*left, *right) */
static void float_range(const floats_t *f, int y, int h, int *left, int *right)
{
    if (h < 1)
        h = 1;
    for (int i = 0; f && i < f->count; i++) {
        if (f->items[i].y >= y + h || f->items[i].y + f->items[i].h <= y)
            continue;
        if (f->items[i].right)
            *right = min_int(*right, f->items[i].x);
        else
            *left = max_int(*left, f->items[i].x + f->items[i].w);
    }
}

/* Die nächste Höhe unter y, an der ein Kasten daneben endet - oder y, wenn dort keiner mehr ist */
static int float_next(const floats_t *f, int y)
{
    int next = -1;
    for (int i = 0; f && i < f->count; i++) {
        int bottom = f->items[i].y + f->items[i].h;
        if (bottom > y && (next < 0 || bottom < next))
            next = bottom;
    }
    return next < 0 ? y : next;
}

/* Die Höhe, ab der links, rechts oder beidseits (sides) nichts mehr daneben steht */
static int float_clear(const floats_t *f, int y, int sides)
{
    for (int i = 0; f && i < f->count; i++) {
        bool match = f->items[i].right ? (sides & CSS_SIDE_RIGHT) : (sides & CSS_SIDE_LEFT);
        if (match)
            y = max_int(y, f->items[i].y + f->items[i].h);
    }
    return y;
}

/*
 * Stellt einen fertig gesetzten Kasten neben den Text: so weit oben wie
 * möglich ab y, zwischen left und right. Liefert die Lage seiner Außenkante
 * (Rahmen) in den Koordinaten des Abschnitts.
 */
static void float_place(floats_t *f, lbox_t *b, int left, int right, int y, int *out_x, int *out_y)
{
    int w = b->w + b->margin[1] + b->margin[3], h = b->h + b->margin[0] + b->margin[2];
    bool at_right = b->st->float_side == CSS_SIDE_RIGHT;
    int l = left, r = right;
    for (int tries = 0; tries < FLOATS_MAX + 2; tries++) {
        l = left;
        r = right;
        float_range(f, y, h, &l, &r);
        if (r - l >= w || (l == left && r == right))
            break;
        int next = float_next(f, y);
        if (next == y)
            break;
        y = next;
    }
    int x = at_right ? max_int(r - w, l) : l;
    if (f->count < FLOATS_MAX && h > 0 && w > 0) {
        f->items[f->count].x = x;
        f->items[f->count].y = y;
        f->items[f->count].w = w;
        f->items[f->count].h = h;
        f->items[f->count].right = at_right;
        f->count++;
    }
    *out_x = x + b->margin[3];
    *out_y = y + b->margin[0];
}

/* Der Kasten wird zuletzt gezeichnet, nach seiner Ebene (z-index) geordnet. */
static void add_layer(lay_t *L, lbox_t *b)
{
    if (b->flags & F_LAYER)
        return;
    b->flags |= F_LAYER;
    b->layer_next = NULL;
    if (L->layers_tail)
        L->layers_tail->layer_next = b;
    else
        L->layers = b;
    L->layers_tail = b;
}

/* Meldet einen frei platzierten Kasten bei dem an, nach dem er sich richtet. */
static void register_abs(lay_t *L, lbox_t *b)
{
    lbox_t *owner = L->root;
    if (!(b->flags & F_FIXED)) {
        for (lbox_t *p = b->parent; p; p = p->parent) {
            if (p->st->position != CSS_POS_STATIC && p->kind == K_BLOCK) {
                owner = p;
                break;
            }
        }
    }
    lbox_t **tail = &owner->abs_list;
    while (*tail) {
        if (*tail == b)
            return;                     /* ist schon angemeldet */
        tail = &(*tail)->abs_next;
    }
    b->abs_next = NULL;
    *tail = b;
    add_layer(L, b);
}

/* --- Text in Zeilen setzen --------------------------------------------------------------------- */

#define OPEN_MAX 12

typedef struct {
    lay_t *L;
    lbox_t *block;
    bool measure;                       /* nur ausmessen: wie breit will der Inhalt werden? */
    int width;                          /* Breite des Inhalts */
    floats_t *fc;
    int fx, fy;                         /* linke obere Ecke des Inhalts in den Koordinaten des Abschnitts */
    int ox, oy;                         /* ... und ab der Außenkante des Kastens */
    int y, left, right, x;              /* laufende Zeile: Oberkante, freier Bereich, nächste freie Stelle */
    piece_t *first, *last;
    deco_t *decos;
    bool any;                           /* die Zeile hat Inhalt */
    bool space;                         /* vor dem nächsten Stück steht ein Leerraum */
    bool space_glued;                   /* ... an dem die Zeile nicht umbrechen darf (nowrap) */
    bool first_line;
    const style_t *space_style;
    const dom_node *space_node;
    int edge;                           /* Rand und Innenabstand von Elementen vor dem nächsten Stück */
    lline_t *lines, *tail;
    int min_w, max_w, run, unbroken, float_w;
    struct {
        const lbox_t *box;
        int start;
        bool fresh;                     /* sein linkes Ende ist noch nicht gezeichnet */
    } open[OPEN_MAX];
    int open_n;
    /*
     * Die letzte Stelle der Zeile, an der sie umbrechen darf: hinter den
     * ersten brk_bytes von brk_piece (NULL: am Zeilenanfang). Passt etwas
     * nicht mehr, das am Vorangehenden hängt, wandert alles ab hier mit.
     */
    piece_t *brk_piece;
    uint16_t brk_bytes;
    bool brk_set;
} inl_t;

static void begin_line(inl_t *I)
{
    I->left = 0;
    I->right = I->measure ? HUGE_WIDTH : I->width;
    if (!I->measure && I->fc) {
        int l = I->fx, r = I->fx + I->width;
        float_range(I->fc, I->fy + I->y, line_height_of(I->L, I->block->st), &l, &r);
        I->left = l - I->fx;
        I->right = r - I->fx;
    }
    I->x = I->left + (I->first_line && !I->measure ? I->block->st->indent : 0);
    I->first = I->last = NULL;
    I->decos = NULL;
    I->any = false;
    I->space = false;
    I->brk_set = false;
    for (int i = 0; i < I->open_n; i++)
        I->open[i].start = I->x;
}

static void add_deco(inl_t *I, const lbox_t *box, int from, int to, bool opens, bool closes)
{
    if (to <= from)
        return;
    deco_t *d = arena(I->L, sizeof(*d));
    d->box = box;
    d->x = from;
    d->w = to - from;
    d->opens = opens;
    d->closes = closes;
    d->next = I->decos;
    I->decos = d;
}

/* Schließt die laufende Zeile ab: Höhe, Grundlinie, Ausrichtung - und beginnt die nächste. */
static void finish_line(inl_t *I, bool forced)
{
    lay_t *L = I->L;
    if (I->measure) {
        I->max_w = max_int(I->max_w, I->run);
        I->run = 0;
        I->unbroken = 0;
        I->space = false;
        I->any = false;
        return;
    }
    for (int i = 0; i < I->open_n; i++) {
        /* offene Elemente reichen bis zum Zeilenende */
        if (I->x > I->open[i].start) {
            add_deco(I, I->open[i].box, I->open[i].start, I->x, I->open[i].fresh, false);
            I->open[i].fresh = false;
        }
    }
    if (!I->first && !forced && !I->decos)
        return;

    const style_t *bst = I->block->st;
    int a, d;
    font_metrics_of(L, bst, &a, &d);
    int lh = line_height_of(L, bst);
    int asc = a + (lh - (a + d)) / 2, desc = lh - asc, tallest = 0;
    for (piece_t *p = I->first; p; p = p->next) {
        if (p->atom) {
            const lbox_t *b = p->atom;
            int total = b->h + b->margin[0] + b->margin[2], up;
            uint8_t va = b->st->valign;
            if (va == CSS_VA_TOP || va == CSS_VA_BOTTOM) {
                tallest = max_int(tallest, total);
                continue;
            }
            if (va == CSS_VA_MIDDLE)
                up = total / 2 + bst->size / 4;
            else if (b->baseline >= 0)
                up = b->margin[0] + b->baseline;
            else
                up = total;
            p->shift = (int16_t)clamp(up, -30000, 30000);
            asc = max_int(asc, up);
            desc = max_int(desc, total - up);
        } else {
            int pa, pd;
            font_metrics_of(L, p->st, &pa, &pd);
            int plh = line_height_of(L, p->st);
            int top = pa + (plh - (pa + pd)) / 2;
            asc = max_int(asc, top - p->shift);
            desc = max_int(desc, plh - top + p->shift);
        }
    }
    if (asc < 0)
        asc = 0;
    int height = max_int(max_int(asc + desc, tallest), 1);

    int move = 0, room = I->right - I->left, used = I->x - I->left;
    if (bst->align == CSS_CENTER)
        move = (room - used) / 2;
    else if (bst->align == CSS_RIGHT)
        move = room - used;
    if (move < 0)
        move = 0;

    lline_t *line = arena(L, sizeof(*line));
    line->y = I->y;
    line->h = height;
    line->baseline = asc;
    line->first = I->first;
    line->decos = I->decos;
    for (piece_t *p = I->first; p; p = p->next) {
        p->x += move;
        if (p->atom) {
            lbox_t *b = p->atom;
            int total = b->h + b->margin[0] + b->margin[2];
            uint8_t va = b->st->valign;
            b->x = I->ox + p->x + b->margin[3];
            b->y = I->oy + b->margin[0] + (va == CSS_VA_TOP ? I->y : va == CSS_VA_BOTTOM ? I->y + height - total
                                                                    : I->y + asc - p->shift);
            p->shift = 0;
        }
    }
    for (deco_t *dc = I->decos; dc; dc = dc->next) {
        int da, dd;
        font_metrics_of(L, dc->box->st, &da, &dd);
        dc->x += move;
        dc->top = asc - da - dc->box->pad[0] - dc->box->bor[0];
        dc->h = da + dd + dc->box->pad[0] + dc->box->pad[2] + dc->box->bor[0] + dc->box->bor[2];
    }
    if (I->tail)
        I->tail->next = line;
    else
        I->lines = line;
    I->tail = line;
    I->y += height;
    I->first_line = false;
    begin_line(I);
}

/* Hängt ein Stück an die Zeile: Text (oder ein Kasten) der Breite w. */
static void add_piece(inl_t *I, const style_t *st, const dom_node *node, const char *text, size_t n,
                      int w, lbox_t *atom)
{
    lay_t *L = I->L;
    piece_t *last = I->last;
    int16_t shift = st->valign == CSS_VA_SUB ? (int16_t)(st->size / 4)
                  : st->valign == CSS_VA_SUPER ? (int16_t)-(st->size / 3) : 0;
    if (!atom && last && !last->atom && last->st == st && last->node == node &&
        last->x + last->w == I->x && last->bytes + n < 4000) {
        /* derselbe Text geht weiter: ein Stück */
        if (last->bytes + n > last->room) {
            size_t room = (last->bytes + n) * 2;
            char *grown = arena(L, room);
            if (!grown || room > SPARE_BYTES * 2)
                return;
            memcpy(grown, last->text, last->bytes);
            last->text = grown;
            last->room = (uint16_t)room;
        }
        memcpy(last->text + last->bytes, text, n);
        last->bytes = (uint16_t)(last->bytes + n);
        last->w += w;
        I->x += w;
        return;
    }
    piece_t *p = arena(L, sizeof(*p));
    p->x = I->x;
    p->w = w;
    p->st = st;
    p->node = node;
    p->atom = atom;
    if (!atom) {
        size_t room = n < 24 ? 48 : n * 2;
        if (room > SPARE_BYTES)
            room = n = SPARE_BYTES;
        p->text = arena(L, room);
        if (p->text)
            memcpy(p->text, text, n);
        p->bytes = (uint16_t)n;
        p->room = (uint16_t)room;
        p->shift = shift;
    }
    if (I->last)
        I->last->next = p;
    else
        I->first = p;
    I->last = p;
    I->x += w;
}

/* Vor dem nächsten Stück: der anstehende Leerraum und die Ränder offener Elemente */
static void add_gap(inl_t *I)
{
    if (I->space && I->any)
        add_piece(I, I->space_style, I->space_node, " ", 1,
                  text_width(I->L, I->space_style, " ", 1), NULL);
    I->space = false;
    I->x += I->edge;
    I->edge = 0;
}

/* Merkt sich die Stelle vor dem nächsten Stück als mögliche Umbruchstelle. */
static void mark_break(inl_t *I)
{
    if (!I->any)
        return;
    I->brk_piece = I->last;
    I->brk_bytes = I->last && !I->last->atom ? I->last->bytes : 0;
    I->brk_set = true;
}

/*
 * Etwas, das am Vorangehenden hängt ("8&nbsp;kg"), passt nicht mehr in die
 * Zeile: Sie endet an der letzten Umbruchstelle, und was danach kam, beginnt
 * die nächste. false: Es gibt keine solche Stelle in dieser Zeile.
 */
static bool carry_over(inl_t *I)
{
    lay_t *L = I->L;
    piece_t *at = I->brk_piece;
    if (!I->brk_set || !at)
        return false;
    piece_t *tail = at->next, cut;
    bool has_cut = !at->atom && at->bytes > I->brk_bytes;
    memset(&cut, 0, sizeof(cut));
    if (has_cut) {
        cut = *at;
        cut.text = at->text + I->brk_bytes;
        cut.bytes = (uint16_t)(at->bytes - I->brk_bytes);
        at->bytes = I->brk_bytes;
        at->w = text_width(L, at->st, at->text, at->bytes);
    }
    if (!has_cut && !tail)
        return false;
    at->next = NULL;
    I->last = at;
    I->x = at->x + at->w;
    finish_line(I, false);
    bool first = true;
    for (piece_t *p = has_cut ? &cut : tail; p;) {
        piece_t *next = p == &cut ? tail : p->next;
        if (p->atom) {
            add_piece(I, p->st, p->node, NULL, 0, p->w, p->atom);
            I->any = true;
        } else {
            const char *text = p->text;
            size_t n = p->bytes;
            if (first && n > 0 && text[0] == ' ') {     /* der Leerraum an der Umbruchstelle entfällt */
                text++;
                n--;
            }
            if (n > 0) {
                add_piece(I, p->st, p->node, text, n, text_width(L, p->st, text, n), NULL);
                I->any = true;
            }
        }
        first = false;
        p = next;
    }
    return true;
}

/* Die Zeile ist leer, aber wegen Kästen daneben zu schmal: weiter unten ist mehr Platz. */
static void make_room(inl_t *I, int need)
{
    for (int tries = 0; tries < FLOATS_MAX && !I->any && I->fc; tries++) {
        if (I->x + need <= I->right || (I->left <= 0 && I->right >= I->width))
            return;
        int here = I->fy + I->y, next = float_next(I->fc, here);
        if (next <= here)
            return;
        I->y = next - I->fy;
        begin_line(I);
    }
}

/* Die längste Vorsilbe (ganze Zeichen), die in room Pixel passt - mindestens ein Zeichen */
static size_t fitting(const lay_t *L, const style_t *st, const char *text, size_t n, int room)
{
    size_t best = 0, at = 0;
    while (at < n) {
        size_t next = at + 1;
        while (next < n && ((uint8_t)text[next] & 0xC0) == 0x80)
            next++;
        if (best > 0 && text_width(L, st, text, next) > room)
            break;
        best = next;
        at = next;
    }
    return best;
}

/* Setzt ein Wort (oder erhaltenen Leerraum). can_break: Davor darf die Zeile umbrechen. */
static void put_text(inl_t *I, const style_t *st, const dom_node *node, const char *text, size_t n,
                     bool can_break)
{
    lay_t *L = I->L;
    int w = text_width(L, st, text, n);
    int space_w = I->space && I->any ? text_width(L, I->space_style, " ", 1) : 0;
    if (I->measure) {
        if (can_break)
            I->unbroken = 0;
        else
            I->unbroken += space_w;
        I->unbroken += w + I->edge;
        if (st->break_words == CSS_YES)
            I->unbroken = min_int(I->unbroken, st->size * 2);
        I->min_w = max_int(I->min_w, I->unbroken);
        I->run += w + I->edge + space_w;
        I->edge = 0;
        I->space = false;
        I->any = true;
        return;
    }
    bool wraps = st->white_space != CSS_WS_NOWRAP;
    if (I->any && can_break && I->x + space_w + I->edge + w > I->right)
        finish_line(I, false);
    else if (I->any && !can_break && I->x + space_w + I->edge + w > I->right && carry_over(I))
        space_w = I->space && I->any ? text_width(L, I->space_style, " ", 1) : 0;
    if (can_break)
        mark_break(I);
    make_room(I, I->edge + w);
    /* ein Wort, das allein nicht in die Zeile passt, wird zerteilt */
    while (wraps && n > 1 && I->x + (I->any ? space_w : 0) + I->edge + w > I->right && I->right - I->left > 20) {
        int room = I->right - I->x - I->edge - (I->any ? space_w : 0);
        if (I->any && room < w / 4 + 8 && can_break) {
            finish_line(I, false);
            continue;
        }
        if (I->any && !can_break && room < 8)
            break;                      /* hängt an Vorangehendem: bleibt in dieser Zeile, ragt hinaus */
        size_t k = fitting(L, st, text, n, room);
        if (k >= n)
            break;
        add_gap(I);
        add_piece(I, st, node, text, k, text_width(L, st, text, k), NULL);
        I->any = true;
        finish_line(I, false);
        text += k;
        n -= k;
        w = text_width(L, st, text, n);
        space_w = 0;
    }
    add_gap(I);
    add_piece(I, st, node, text, n, w, NULL);
    I->any = true;
}

/* Ein Zeichen in die gewünschte Schreibung (text-transform) bringen: Lateinisch samt Umlauten */
static size_t transformed(char *out, const char *p, bool upper)
{
    uint8_t c = (uint8_t)p[0];
    if (c < 0x80) {
        out[0] = upper ? (c >= 'a' && c <= 'z' ? (char)(c - 32) : (char)c)
                       : (c >= 'A' && c <= 'Z' ? (char)(c + 32) : (char)c);
        return 1;
    }
    if (c == 0xC3 && ((uint8_t)p[1] & 0xC0) == 0x80) {
        uint8_t d = (uint8_t)p[1];
        out[0] = (char)c;
        if (upper && d >= 0xA0 && d <= 0xBE && d != 0xB7)
            d = (uint8_t)(d - 0x20);
        else if (!upper && d >= 0x80 && d <= 0x9E && d != 0x97)
            d = (uint8_t)(d + 0x20);
        out[1] = (char)d;
        return 2;
    }
    out[0] = (char)c;
    return 1;
}

static void inline_text(inl_t *I, const lbox_t *t)
{
    const style_t *st = t->st;
    uint8_t ws = st->white_space;
    bool collapse = ws == CSS_WS_NORMAL || ws == CSS_WS_NOWRAP || ws == CSS_WS_PRE_LINE;
    bool wraps = ws != CSS_WS_NOWRAP;
    bool newlines = ws == CSS_WS_PRE || ws == CSS_WS_PRE_WRAP || ws == CSS_WS_PRE_LINE;
    const char *p = t->text, *end = p + t->text_len;
    bool after_space = false;           /* erhaltener Leerraum ging voraus: Umbruch erlaubt */
    char buf[420];
    while (p < end && !I->L->failed) {
        if (*p == '\n' && newlines) {
            finish_line(I, true);
            p++;
            after_space = false;
            continue;
        }
        if (is_space(*p)) {
            if (collapse) {
                while (p < end && is_space(*p) && !(newlines && *p == '\n'))
                    p++;
                if (I->any) {
                    I->space = true;
                    I->space_glued = !wraps;
                    I->space_style = st;
                    I->space_node = t->node;
                }
                continue;
            }
            size_t n = 0;
            while (p < end && is_space(*p) && *p != '\n' && n + 8 < sizeof(buf)) {
                for (int k = *p == '\t' ? 4 : 1; k > 0; k--)
                    buf[n++] = ' ';
                p++;
            }
            put_text(I, st, t->node, buf, n, false);
            after_space = true;
            continue;
        }
        size_t n = 0;
        bool start = true;
        while (p < end && !is_space(*p) && n + 8 < sizeof(buf)) {
            if (st->transform == CSS_TT_UPPER || st->transform == CSS_TT_LOWER ||
                (st->transform == CSS_TT_CAPITALIZE && start)) {
                size_t k = transformed(buf + n, p, st->transform != CSS_TT_LOWER);
                n += k;
                p += k;
            } else {
                buf[n++] = *p++;
            }
            start = false;
            /* nach einem Bindestrich darf die Zeile umbrechen */
            if (p[-1] == '-' && n > 2 && p < end && !is_space(*p) && wraps)
                break;
        }
        /* nicht mitten in einem Zeichen aufhören */
        while (p < end && ((uint8_t)*p & 0xC0) == 0x80 && n + 1 < sizeof(buf))
            buf[n++] = *p++;
        bool hyphen = n > 2 && buf[n - 1] == '-';
        put_text(I, st, t->node, buf, n, wraps && ((I->space && !I->space_glued) || after_space));
        after_space = hyphen;           /* ... das nächste Stück darf dann auf die nächste Zeile */
    }
}

static void inline_children(inl_t *I, lbox_t *parent);

/* Ein Kasten in der Zeile: Bild, Feld oder ein Element, das als Ganzes mitläuft */
static void inline_atom(inl_t *I, lbox_t *b)
{
    lay_t *L = I->L;
    bool wraps = b->parent->st->white_space != CSS_WS_NOWRAP;
    resolve_edges(b, I->measure ? -1 : I->width);
    int margins = b->margin[1] + b->margin[3];
    int space_w = I->space && I->any ? text_width(L, I->space_style, " ", 1) : 0;
    if (I->measure) {
        intrinsic(L, b);
        I->unbroken = wraps ? 0 : I->unbroken + space_w;
        I->unbroken += b->min_w + margins + I->edge;
        I->min_w = max_int(I->min_w, I->unbroken);
        if (wraps)
            I->unbroken = 0;
        I->run += b->max_w + margins + I->edge + space_w;
        I->edge = 0;
        I->space = false;
        I->any = true;
        return;
    }
    int w = fit_width(L, b, I->width - margins, I->width);
    layout_block(L, b, w, NULL, 0, 0);
    int total = b->w + margins;
    if (I->any && wraps && I->x + space_w + I->edge + total > I->right)
        finish_line(I, false);
    else if (I->any && !wraps && I->x + space_w + I->edge + total > I->right)
        carry_over(I);
    if (wraps)
        mark_break(I);
    make_room(I, I->edge + total);
    add_gap(I);
    add_piece(I, b->st, b->node, NULL, 0, total, b);
    I->any = true;
}

/* Ein Kasten neben dem Text, der mitten im Text steht */
static void inline_float(inl_t *I, lbox_t *b)
{
    lay_t *L = I->L;
    resolve_edges(b, I->measure ? -1 : I->width);
    int margins = b->margin[1] + b->margin[3];
    if (I->measure) {
        intrinsic(L, b);
        I->min_w = max_int(I->min_w, b->min_w + margins);
        I->float_w += b->max_w + margins;
        return;
    }
    int w = fit_width(L, b, I->width - margins, I->width), x, y;
    layout_block(L, b, w, NULL, 0, 0);
    if (!I->fc) {
        b->x = I->ox;
        b->y = I->oy + I->y;
        return;
    }
    /* neben der laufenden Zeile, wenn sie noch leer ist - sonst darunter */
    int top = I->fy + I->y + (I->any ? line_height_of(L, I->block->st) : 0);
    if (b->st->clear != CSS_SIDE_NONE)
        top = float_clear(I->fc, top, b->st->clear);
    float_place(I->fc, b, I->fx, I->fx + I->width, top, &x, &y);
    b->x = I->ox + x - I->fx;
    b->y = I->oy + y - I->fy;
    if (!I->any) {
        bool first = I->first_line;
        begin_line(I);
        I->first_line = first;
    }
}

/* Ein Element im Text: Seine Ränder brauchen Platz, Hintergrund und Rahmen laufen mit. */
static void inline_span(inl_t *I, lbox_t *b)
{
    const style_t *st = b->st;
    resolve_edges(b, I->measure ? -1 : I->width);
    b->line_block = I->block;
    b->line_y = I->y;
    int left = b->margin[3] + b->bor[3] + b->pad[3], right = b->margin[1] + b->bor[1] + b->pad[1];
    bool deco = !I->measure && !st->invisible && I->open_n < OPEN_MAX &&
                (st->background_set || st->border_style[0] || st->border_style[1] ||
                 st->border_style[2] || st->border_style[3]);
    int slot = I->open_n;
    if (deco) {
        int space_w = I->space && I->any ? text_width(I->L, I->space_style, " ", 1) : 0;
        I->open[slot].box = b;
        I->open[slot].start = I->x + space_w + I->edge + b->margin[3];
        I->open[slot].fresh = true;
        I->open_n++;
    }
    I->edge += left;
    inline_children(I, b);
    if (I->edge > 0) {                  /* nichts darin: Die Ränder brauchen trotzdem ihren Platz */
        if (I->measure) {
            I->run += I->edge;
            I->unbroken += I->edge;
            I->min_w = max_int(I->min_w, I->unbroken);
        } else {
            I->x += I->edge;
        }
        I->edge = 0;
    }
    if (deco) {
        I->open_n = slot;
        add_deco(I, b, I->open[slot].start, I->x + b->bor[1] + b->pad[1], I->open[slot].fresh, true);
    }
    if (I->measure) {
        I->run += right;
        I->unbroken += right;
    } else {
        I->x += right;
    }
}

static void inline_children(inl_t *I, lbox_t *parent)
{
    for (lbox_t *c = parent->first; c && !I->L->failed; c = c->next) {
        if (c->flags & F_ABS) {
            if (!I->measure) {
                c->static_x = I->ox + I->x;
                c->static_y = I->oy + I->y;
                register_abs(I->L, c);
            }
        } else if (c->flags & F_FLOAT) {
            inline_float(I, c);
        } else if (c->kind == K_TEXT) {
            inline_text(I, c);
        } else if (c->kind == K_BREAK) {
            finish_line(I, true);
        } else if (c->kind == K_INLINE) {
            inline_span(I, c);
        } else {
            inline_atom(I, c);
        }
    }
}

/* Setzt den Inhalt eines Kastens in Zeilen; liefert die Höhe des Inhalts. */
static int layout_lines(lay_t *L, lbox_t *b, int cw, floats_t *fc, int fx, int fy)
{
    inl_t I;
    memset(&I, 0, sizeof(I));
    I.L = L;
    I.block = b;
    I.width = cw;
    I.fc = fc;
    I.ox = b->bor[3] + b->pad[3];
    I.oy = b->bor[0] + b->pad[0];
    I.fx = fx + I.ox;
    I.fy = fy + I.oy;
    I.first_line = true;
    begin_line(&I);
    inline_children(&I, b);
    finish_line(&I, false);
    b->lines = I.lines;
    b->baseline = I.lines ? I.oy + I.lines->baseline : -1;
    return I.y;
}

static void measure_lines(lay_t *L, lbox_t *b, int *min_w, int *max_w)
{
    inl_t I;
    memset(&I, 0, sizeof(I));
    I.L = L;
    I.block = b;
    I.measure = true;
    I.width = HUGE_WIDTH;
    begin_line(&I);
    inline_children(&I, b);
    finish_line(&I, false);
    *min_w = I.min_w;
    *max_w = I.max_w + I.float_w;
}

/* --- Wie breit will ein Kasten werden? -------------------------------------------------------- */

static void table_measure(lay_t *L, lbox_t *b, int *min_w, int *max_w);

static int gap_px(len_t gap, int base)
{
    return max_int(len_px(gap, base), 0);
}

static void intrinsic(lay_t *L, lbox_t *b)
{
    if (b->min_w >= 0)
        return;
    b->min_w = b->max_w = 0;
    resolve_edges(b, -1);
    const style_t *st = b->st;
    if (b->flags & F_REPLACED) {
        int w, h;
        replaced_size(b, -1, &w, &h);
        b->max_w = w;
        /* mit Angaben in Prozent passt es sich dem Platz an */
        bool flexible = st->width.percent || (st->max_width.kind == CSS_LEN_VALUE && st->max_width.percent);
        b->min_w = flexible && b->widget < 0 ? min_int(w, 24) : flexible ? min_int(w, 60) : w;
        return;
    }
    int cmin = 0, cmax = 0;
    const flexgrid_t *fg = st->fg;
    bool row = b->inner == IN_FLEX && fg && (fg->direction == CSS_FD_ROW || fg->direction == CSS_FD_ROW_REVERSE);
    if (b->inner == IN_TABLE) {
        table_measure(L, b, &cmin, &cmax);
    } else if (b->flags & F_LINES) {
        measure_lines(L, b, &cmin, &cmax);
    } else {
        int count = 0, widest_min = 0, widest_max = 0, sum_min = 0, sum_max = 0;
        for (lbox_t *c = b->first; c; c = c->next) {
            if (c->flags & F_ABS)
                continue;
            intrinsic(L, c);
            int m = c->margin[1] + c->margin[3];
            widest_min = max_int(widest_min, c->min_w + m);
            widest_max = max_int(widest_max, c->max_w + m);
            sum_min += c->min_w + m;
            sum_max += c->max_w + m;
            count++;
        }
        cmin = widest_min;
        cmax = widest_max;
        if (row && count > 0) {
            int gaps = gap_px(fg->gap_column, -1) * (count - 1);
            cmax = sum_max + gaps;
            if (fg->wrap != CSS_FW_WRAP)
                cmin = sum_min + gaps;
        } else if (b->inner == IN_GRID && fg && count > 0) {
            int n = fg->grid_auto_fill || fg->grid_count == 0 ? 1 : fg->grid_count;
            int gaps = gap_px(fg->gap_column, -1) * (n - 1), fixed = 0, loose = 0;
            for (int i = 0; i < n && !fg->grid_auto_fill && fg->grid_count; i++) {
                if (!fg->grid[i].fr && !fg->grid[i].automatic && len_fixed(fg->grid[i].size))
                    fixed += fg->grid[i].size.px;
                else
                    loose++;
            }
            if (fg->grid_auto_fill || !fg->grid_count)
                loose = 1;
            cmin = fixed + loose * widest_min + gaps;
            cmax = fg->grid_auto_fill ? sum_max + gap_px(fg->gap_column, -1) * (count - 1)
                                      : fixed + loose * widest_max + gaps;
        }
    }
    int e = edge_w(b), fixed;
    if (given_width(b, -1, &fixed)) {
        b->min_w = b->max_w = b->inner == IN_TABLE ? max_int(fixed, cmin + e) : fixed;
    } else {
        b->min_w = cmin + e;
        b->max_w = max_int(cmax, cmin) + e;
    }
    b->min_w = limit_width(b, -1, b->min_w);
    b->max_w = max_int(limit_width(b, -1, b->max_w), b->min_w);
}

/* --- Blöcke untereinander ---------------------------------------------------------------------- */

/* Zwei aneinanderstoßende Außenabstände verschmelzen: Es gilt der größere. */
static int collapse(int a, int b)
{
    if (a >= 0 && b >= 0)
        return max_int(a, b);
    if (a < 0 && b < 0)
        return min_int(a, b);
    return a + b;
}

/* Die Breite eines Blocks im Fluss und sein Abstand von der linken Innenkante des umgebenden Kastens */
static int block_width(lay_t *L, lbox_t *c, int avail, bool center, int *offset)
{
    const style_t *st = c->st;
    bool left_auto = st->margin[3].kind == CSS_LEN_AUTO, right_auto = st->margin[1].kind == CSS_LEN_AUTO;
    int ml = c->margin[3], mr = c->margin[1], w;
    bool shrinks = c->inner == IN_TABLE || (c->flags & F_REPLACED) || st->width.kind == CSS_LEN_CONTENT;
    if (c->flags & F_REPLACED) {
        int h;
        replaced_size(c, avail, &w, &h);
    } else if (shrinks) {
        w = fit_width(L, c, avail - ml - mr, avail);
    } else if (given_width(c, avail, &w)) {
        w = limit_width(c, avail, w);
    } else {
        w = limit_width(c, avail, avail - ml - mr);
        shrinks = w < avail - ml - mr;  /* max-width hat sie begrenzt: Dann zählen "auto"-Ränder */
    }
    bool sized = shrinks || len_set(st->width, avail);
    int free = avail - w - ml - mr;
    if (sized && free > 0) {
        if (left_auto && right_auto)
            ml += free / 2;
        else if (left_auto)
            ml += free;
        else if (center && !right_auto && (c->inner == IN_TABLE || (c->flags & F_REPLACED)))
            ml += free / 2;             /* Tabellen und Bilder in zentriertem Text: mittig */
    }
    *offset = ml;
    return w;
}

static int layout_flex(lay_t *L, lbox_t *b, int cw);
static int layout_grid(lay_t *L, lbox_t *b, int cw);
static int layout_table(lay_t *L, lbox_t *b, int cw);
static void layout_abs(lay_t *L, lbox_t *owner);

/* Setzt die Kinder eines Kastens untereinander; liefert die Höhe des Inhalts. */
static int layout_flow(lay_t *L, lbox_t *b, int cw, floats_t *fc, int fx, int fy)
{
    const style_t *st = b->st;
    int cx = b->bor[3] + b->pad[3], cy = b->bor[0] + b->pad[0];
    int cursor = 0, pending = 0;
    bool root = (b->flags & F_ROOT) != 0;
    bool at_start = !root && b->bor[0] == 0 && b->pad[0] == 0;     /* Abstände reichen oben hinaus */
    b->leak_top = b->leak_bottom = 0;

    for (lbox_t *c = b->first; c && !L->failed; c = c->next) {
        if (c->flags & F_ABS) {
            c->static_x = cx;
            c->static_y = cy + cursor + (at_start ? 0 : max_int(pending, 0));
            register_abs(L, c);
            continue;
        }
        resolve_edges(c, cw);
        if (c->flags & F_FLOAT) {
            int w = fit_width(L, c, cw - c->margin[1] - c->margin[3], cw), x, y;
            layout_block(L, c, w, NULL, 0, 0);
            int top = fy + cy + cursor + (at_start ? 0 : max_int(pending, 0));
            if (c->st->clear != CSS_SIDE_NONE)
                top = float_clear(fc, top, c->st->clear);   /* unter den früheren Kästen dieser Seite */
            float_place(fc, c, fx + cx, fx + cx + cw, top, &x, &y);
            c->x = x - fx;
            c->y = y - fy;
            continue;
        }
        if (c->kind != K_BLOCK)
            continue;                   /* loser Text zwischen Kästen kommt hier nicht vor */

        if (c->st->clear != CSS_SIDE_NONE) {
            int below = float_clear(fc, fy + cy + cursor, c->st->clear) - (fy + cy);
            if (below > cursor + (at_start ? 0 : max_int(pending, 0))) {
                cursor = below;
                pending = 0;
                at_start = false;
            }
        }
        int top = cursor + (at_start ? 0 : collapse(pending, c->margin[0]));
        /* Kästen mit eigenem Abschnitt weichen denen neben dem Text aus */
        int avail = cw, shift = 0;
        if ((c->flags & F_ROOT) && fc && fc->count > 0) {
            int l = fx + cx, r = fx + cx + cw;
            float_range(fc, fy + cy + top, 20, &l, &r);
            if (r - l < 80 && (l > fx + cx || r < fx + cx + cw)) {
                /* daneben ist zu wenig Platz: darunter */
                int below = float_clear(fc, fy + cy + top, CSS_SIDE_BOTH) - (fy + cy);
                cursor = max_int(cursor, below);
                pending = 0;
                at_start = false;
                top = cursor + max_int(c->margin[0], 0);
            } else {
                shift = l - (fx + cx);
                avail = r - l;
            }
        }
        int offset;
        int w = block_width(L, c, avail, st->align == CSS_CENTER, &offset);
        c->x = cx + shift + offset;
        layout_block(L, c, w, fc, fx + c->x, fy + cy + top);

        int mt = collapse(c->margin[0], c->leak_top);
        int mb = collapse(c->margin[2], c->leak_bottom);
        if (c->h == 0 && c->bor[0] + c->bor[2] + c->pad[0] + c->pad[2] == 0 && !(c->flags & F_ROOT)) {
            /* ein leerer Kasten: Seine Abstände verschmelzen miteinander und mit denen daneben */
            int through = collapse(mt, mb);
            c->y = cy + cursor;
            if (at_start)
                b->leak_top = collapse(b->leak_top, through);
            else
                pending = collapse(pending, through);
            continue;
        }
        if (at_start) {
            b->leak_top = collapse(b->leak_top, mt);
            c->y = cy + cursor;
        } else {
            c->y = cy + cursor + collapse(pending, mt);
        }
        cursor = c->y - cy + c->h;
        pending = mb;
        at_start = false;
    }
    bool open_bottom = !root && b->bor[2] == 0 && b->pad[2] == 0 && st->height.kind != CSS_LEN_VALUE &&
                       st->min_height.kind != CSS_LEN_VALUE;
    if (open_bottom)
        b->leak_bottom = pending;
    else
        cursor += pending;
    return max_int(cursor, 0);
}

/*
 * Setzt einen Kasten in der Breite width (Außenkante Rahmen). Ränder und
 * Innenabstände hat der Aufrufer bestimmt (resolve_edges). fc ist der
 * Abschnitt, zu dem der Kasten gehört, (fx, fy) seine Lage darin - oder NULL:
 * Dann (und bei Kästen mit F_ROOT) beginnt mit ihm ein eigener Abschnitt.
 */
static void layout_block(lay_t *L, lbox_t *b, int width, floats_t *fc, int fx, int fy)
{
    const style_t *st = b->st;
    b->w = max_int(width, 0);
    b->leak_top = b->leak_bottom = 0;
    b->lines = NULL;
    b->abs_list = NULL;
    if (b->flags & F_REPLACED) {
        int w, h;
        replaced_size(b, -1, &w, &h);
        /* die Breite steht fest; die Höhe folgt dem Seitenverhältnis */
        if (b->image >= 0 && w != b->w && w > 0 && !len_fixed(st->height))
            h = (int)((int64_t)(h - edge_h(b)) * max_int(b->w - edge_w(b), 1) / max_int(w - edge_w(b), 1)) + edge_h(b);
        b->h = max_int(h, 1);
        /* ein Feld steht mit seinem Text auf der Grundlinie der Zeile, ein Bild mit der Unterkante */
        b->baseline = b->widget >= 0 ? (b->h - 16) / 2 + 13 : -1;
        return;
    }
    if ((b->flags & F_ROOT) || !fc) {
        fc = arena(L, sizeof(floats_t));
        fx = fy = 0;
    }
    int cw = max_int(b->w - edge_w(b), 0), ch;
    if (b->inner == IN_FLEX)
        ch = layout_flex(L, b, cw);
    else if (b->inner == IN_GRID)
        ch = layout_grid(L, b, cw);
    else if (b->inner == IN_TABLE)
        ch = layout_table(L, b, cw);
    else if (b->flags & F_LINES)
        ch = layout_lines(L, b, cw, fc, fx, fy);
    else
        ch = layout_flow(L, b, cw, fc, fx, fy);

    /* "clear" am Ende des Kastens (clearfix): Er reicht bis unter die Kästen daneben */
    if (st->clear_after != CSS_SIDE_NONE && fc && b->inner == IN_FLOW) {
        int top = fy + b->bor[0] + b->pad[0];
        int below = float_clear(fc, top + ch, st->clear_after) - top;
        if (below > ch) {
            ch = below;
            b->leak_bottom = 0;
        }
    }
    /* ein eigener Abschnitt schließt ein, was in ihm neben dem Text steht */
    if ((b->flags & F_ROOT) && fc) {
        for (int i = 0; i < fc->count; i++)
            ch = max_int(ch, fc->items[i].y + fc->items[i].h - b->bor[0] - b->pad[0]);
    }
    /* Höhenangaben: Der Kasten wird nie kleiner als sein Inhalt (nichts wird abgeschnitten) */
    int extra = st->box_sizing == CSS_BOX_BORDER ? edge_h(b) : 0;
    if (len_fixed(st->height))
        ch = max_int(ch, st->height.px - extra);
    if (len_fixed(st->min_height))
        ch = max_int(ch, st->min_height.px - extra);
    b->h = ch + edge_h(b);
    if (st->position != CSS_POS_STATIC || b == L->root)
        layout_abs(L, b);
}

/* --- Frei platzierte Kästen ------------------------------------------------------------------- */

static void layout_abs(lay_t *L, lbox_t *owner)
{
    /* Bezug ist die Innenkante des Rahmens */
    int pw = owner->w - owner->bor[1] - owner->bor[3], ph = owner->h - owner->bor[0] - owner->bor[2];
    for (lbox_t *a = owner->abs_list; a && !L->failed; a = a->abs_next) {
        const style_t *st = a->st;
        resolve_edges(a, pw);
        bool has_left = len_set(st->inset[3], pw), has_right = len_set(st->inset[1], pw);
        bool has_top = len_set(st->inset[0], ph), has_bottom = len_set(st->inset[2], ph);
        int left = len_px(st->inset[3], pw), right = len_px(st->inset[1], pw);
        int top = len_px(st->inset[0], ph), bottom = len_px(st->inset[2], ph);
        int margins = a->margin[1] + a->margin[3], w;
        if (a->flags & F_REPLACED) {
            int h;
            replaced_size(a, pw, &w, &h);
        } else if (given_width(a, pw, &w)) {
            w = limit_width(a, pw, w);
        } else if (has_left && has_right) {
            w = limit_width(a, pw, pw - left - right - margins);
        } else {
            w = fit_width(L, a, pw - (has_left ? left : 0) - (has_right ? right : 0) - margins, pw);
        }
        layout_block(L, a, w, NULL, 0, 0);
        if (has_top && has_bottom && st->height.kind != CSS_LEN_VALUE)
            a->h = max_int(a->h, ph - top - bottom - a->margin[0] - a->margin[2]);

        /* wo der Kasten ohne Angaben stünde: an seiner Stelle im Fluss */
        int sx = a->static_x, sy = a->static_y;
        for (lbox_t *p = a->parent; p && p != owner; p = p->parent) {
            sx += p->x;
            sy += p->y;
        }
        if (a->flags & F_FIXED) {
            /* am Fenster befestigt: oben steht es am Seitenanfang, unten am Seitenende */
            sx = owner->bor[3];
            sy = has_bottom && !has_top ? ph : 0;
        }
        a->x = has_left ? owner->bor[3] + left + a->margin[3]
             : has_right ? owner->bor[3] + pw - right - a->margin[1] - a->w : sx + a->margin[3];
        a->y = has_top ? owner->bor[0] + top + a->margin[0]
             : (a->flags & F_FIXED) ? sy + a->margin[0]
             : has_bottom ? owner->bor[0] + ph - bottom - a->margin[2] - a->h : sy + a->margin[0];
    }
}

/* --- Kinder in einer Reihe: flex ---------------------------------------------------------------- */

typedef struct {
    lbox_t *box;
    int base, size, low, high;
    int32_t grow, shrink, order;
    bool auto_left, auto_right;
} flex_item_t;

/* Hebt alles im Kasten um dy an bzw. senkt es ab (Inhalt einer Zelle mittig oder unten). */
static void shift_content(lbox_t *b, int dy)
{
    if (dy == 0)
        return;
    for (lbox_t *c = b->first; c; c = c->next) {
        if (c->kind == K_INLINE)
            shift_content(c, dy);
        else
            c->y += dy;
    }
    for (lline_t *line = b->lines; line; line = line->next)
        line->y += dy;
    if (b->baseline >= 0)
        b->baseline += dy;
}

static uint8_t item_align(const lbox_t *c, const flexgrid_t *fg)
{
    return c->st->fg && c->st->fg->align_self ? c->st->fg->align_self : fg->align_items;
}

/* Die Höhe des Inhalts, die die Seite festlegt (oder -1) */
static int given_height(const lbox_t *b)
{
    const style_t *st = b->st;
    int extra = st->box_sizing == CSS_BOX_BORDER ? edge_h(b) : 0;
    if (len_fixed(st->height))
        return max_int(st->height.px - extra, 0);
    if (len_fixed(st->min_height))
        return max_int(st->min_height.px - extra, 0);
    return -1;
}

static int layout_flex(lay_t *L, lbox_t *b, int cw)
{
    const flexgrid_t *fg = b->st->fg;
    static const flexgrid_t plain = { .direction = CSS_FD_ROW, .wrap = CSS_FW_NOWRAP,
                                      .justify = CSS_JU_START, .align_items = CSS_AL_STRETCH };
    if (!fg)
        fg = &plain;
    bool row = fg->direction == CSS_FD_ROW || fg->direction == CSS_FD_ROW_REVERSE;
    bool reverse = fg->direction == CSS_FD_ROW_REVERSE || fg->direction == CSS_FD_COLUMN_REVERSE;
    int cx = b->bor[3] + b->pad[3], cy = b->bor[0] + b->pad[0];
    int gap_main = gap_px(row ? fg->gap_column : fg->gap_row, cw);
    int gap_cross = gap_px(row ? fg->gap_row : fg->gap_column, cw);
    int count = 0;
    for (lbox_t *c = b->first; c; c = c->next) {
        if (c->flags & F_ABS) {
            c->static_x = cx;
            c->static_y = cy;
            register_abs(L, c);
        } else if (c->kind == K_BLOCK) {
            count++;
        }
    }
    if (count == 0)
        return 0;
    flex_item_t *items = arena(L, (size_t)count * sizeof(flex_item_t));
    if (!items || (size_t)count * sizeof(flex_item_t) > SPARE_BYTES * 64)
        return 0;
    int n = 0;
    bool ordered = false;
    for (lbox_t *c = b->first; c && n < count; c = c->next) {
        if ((c->flags & F_ABS) || c->kind != K_BLOCK)
            continue;
        const flexgrid_t *f = c->st->fg;
        flex_item_t *it = &items[n++];
        it->box = c;
        it->grow = f && f->grow_set ? f->grow : 0;
        it->shrink = f && f->shrink_set ? f->shrink : 1000;
        it->order = f ? f->order : 0;
        it->auto_left = c->st->margin[row ? 3 : 0].kind == CSS_LEN_AUTO;
        it->auto_right = c->st->margin[row ? 1 : 2].kind == CSS_LEN_AUTO;
        ordered = ordered || it->order != 0;
        resolve_edges(c, cw);
    }
    if (ordered && n <= 2000) {         /* "order": stabil nach der gewünschten Reihenfolge sortieren */
        for (int i = 1; i < n; i++) {
            flex_item_t key = items[i];
            int k = i - 1;
            for (; k >= 0 && items[k].order > key.order; k--)
                items[k + 1] = items[k];
            items[k + 1] = key;
        }
    }
    if (reverse) {
        for (int i = 0; i < n / 2; i++) {
            flex_item_t swap = items[i];
            items[i] = items[n - 1 - i];
            items[n - 1 - i] = swap;
        }
    }

    if (!row) {
        /* untereinander */
        int y = 0, total_grow = 0;
        for (int i = 0; i < n; i++) {
            lbox_t *c = items[i].box;
            uint8_t align = item_align(c, fg);
            bool left_auto = c->st->margin[3].kind == CSS_LEN_AUTO, right_auto = c->st->margin[1].kind == CSS_LEN_AUTO;
            int avail = cw - c->margin[1] - c->margin[3], w;
            if ((c->flags & F_REPLACED) || given_width(c, cw, &w) || align != CSS_AL_STRETCH || left_auto || right_auto)
                w = fit_width(L, c, avail, cw);
            else
                w = limit_width(c, cw, avail);
            int free = max_int(avail - w, 0);
            int offset = (left_auto && right_auto) || (align == CSS_AL_CENTER && !left_auto && !right_auto) ? free / 2
                       : left_auto || (align == CSS_AL_END && !right_auto) ? free : 0;
            layout_block(L, c, w, NULL, 0, 0);
            c->x = cx + c->margin[3] + offset;
            c->y = cy + y + c->margin[0];
            y += c->margin[0] + c->h + c->margin[2] + gap_main;
            total_grow += items[i].grow;
        }
        int total = y - gap_main, wanted = given_height(b);
        if (wanted > total) {
            /* freie Höhe: an die Kinder, die wachsen wollen - sonst nach justify-content */
            int free = wanted - total, moved = 0;
            for (int i = 0; i < n; i++) {
                lbox_t *c = items[i].box;
                c->y += moved;
                if (total_grow > 0 && items[i].grow > 0) {
                    int add = (int)((int64_t)free * items[i].grow / total_grow);
                    c->h += add;
                    moved += add;
                } else if (total_grow == 0) {
                    uint8_t j = fg->justify;
                    c->y += j == CSS_JU_CENTER ? free / 2 : j == CSS_JU_END ? free
                          : j == CSS_JU_BETWEEN && n > 1 ? free * i / (n - 1)
                          : j == CSS_JU_AROUND ? free * (2 * i + 1) / (2 * n)
                          : j == CSS_JU_EVENLY ? free * (i + 1) / (n + 1) : 0;
                }
            }
            total = wanted;
        }
        return max_int(total, 0);
    }

    /* nebeneinander: zuerst die Wunschbreite jedes Kinds */
    for (int i = 0; i < n; i++) {
        lbox_t *c = items[i].box;
        const flexgrid_t *f = c->st->fg;
        const style_t *st = c->st;
        int extra = st->box_sizing == CSS_BOX_BORDER ? 0 : edge_w(c), base;
        if (c->flags & F_REPLACED) {
            int h;
            replaced_size(c, cw, &base, &h);
            intrinsic(L, c);
        } else {
            intrinsic(L, c);
            if (f && len_set(f->basis, cw))
                base = max_int(len_px(f->basis, cw) + extra, edge_w(c));
            else if (!given_width(c, cw, &base))
                base = c->max_w;
        }
        items[i].low = len_set(st->min_width, cw) ? len_px(st->min_width, cw) + extra
                     : st->overflow != CSS_OV_VISIBLE ? edge_w(c) : min_int(c->min_w, base);
        items[i].high = len_set(st->max_width, cw) ? len_px(st->max_width, cw) + extra : HUGE_WIDTH;
        if (items[i].high < items[i].low)
            items[i].high = items[i].low;
        items[i].base = clamp(base, items[i].low, items[i].high);
    }
    bool wraps = fg->wrap == CSS_FW_WRAP;
    int y = 0, first_baseline = -1;
    for (int start = 0; start < n && !L->failed;) {
        /* eine Reihe: so viele Kinder, wie hineinpassen */
        int end = start, used = 0;
        while (end < n) {
            int need = items[end].base + items[end].box->margin[1] + items[end].box->margin[3] +
                       (end > start ? gap_main : 0);
            if (wraps && end > start && used + need > cw)
                break;
            used += need;
            end++;
        }
        int free = cw - used;
        int64_t total_grow = 0, total_shrink = 0;
        for (int i = start; i < end; i++) {
            total_grow += items[i].grow;
            total_shrink += (int64_t)items[i].shrink * items[i].base;
        }
        used = 0;
        for (int i = start; i < end; i++) {
            int size = items[i].base;
            if (free > 0 && total_grow > 0)
                size += (int)((int64_t)free * items[i].grow / max_int((int)total_grow, 1000));
            else if (free < 0 && total_shrink > 0)
                size -= (int)((int64_t)-free * items[i].shrink * items[i].base / total_shrink);
            items[i].size = clamp(size, items[i].low, items[i].high);
            used += items[i].size + items[i].box->margin[1] + items[i].box->margin[3] + (i > start ? gap_main : 0);
        }
        /* was jetzt noch frei ist: an "auto"-Ränder, sonst nach justify-content */
        free = max_int(cw - used, 0);
        int autos = 0, items_here = end - start;
        for (int i = start; i < end; i++)
            autos += items[i].auto_left + items[i].auto_right;
        uint8_t justify = fg->justify;
        if (reverse && (justify == CSS_JU_START || justify == CSS_JU_END))
            justify = justify == CSS_JU_START ? CSS_JU_END : CSS_JU_START;
        int x = 0, between = 0;
        if (autos == 0) {
            if (justify == CSS_JU_CENTER) {
                x = free / 2;
            } else if (justify == CSS_JU_END) {
                x = free;
            } else if (justify == CSS_JU_BETWEEN && items_here > 1) {
                between = free / (items_here - 1);
            } else if (justify == CSS_JU_AROUND) {
                between = free / items_here;
                x = between / 2;
            } else if (justify == CSS_JU_EVENLY) {
                between = free / (items_here + 1);
                x = between;
            }
        }
        int line_h = 0;
        for (int i = start; i < end; i++) {
            lbox_t *c = items[i].box;
            layout_block(L, c, items[i].size, NULL, 0, 0);
            line_h = max_int(line_h, c->h + c->margin[0] + c->margin[2]);
        }
        if (!wraps && given_height(b) > line_h)
            line_h = given_height(b);
        for (int i = start; i < end; i++) {
            lbox_t *c = items[i].box;
            uint8_t align = item_align(c, fg);
            int outer = c->h + c->margin[0] + c->margin[2], down = 0;
            if (autos > 0 && items[i].auto_left)
                x += free / autos;
            if (c->st->margin[0].kind == CSS_LEN_AUTO || c->st->margin[2].kind == CSS_LEN_AUTO) {
                down = c->st->margin[0].kind != CSS_LEN_AUTO ? 0
                     : c->st->margin[2].kind == CSS_LEN_AUTO ? (line_h - outer) / 2 : line_h - outer;
            } else if (align == CSS_AL_STRETCH && !(c->flags & F_REPLACED) && !len_fixed(c->st->height)) {
                c->h = max_int(c->h, line_h - c->margin[0] - c->margin[2]);
            } else if (align == CSS_AL_CENTER) {
                down = (line_h - outer) / 2;
            } else if (align == CSS_AL_END) {
                down = line_h - outer;
            }
            c->x = cx + x + c->margin[3];
            c->y = cy + y + c->margin[0] + max_int(down, 0);
            if (first_baseline < 0 && c->baseline >= 0)
                first_baseline = c->y + c->baseline;
            x += c->margin[3] + items[i].size + c->margin[1] + gap_main + between;
            if (autos > 0 && items[i].auto_right)
                x += free / autos;
        }
        y += line_h + gap_cross;
        start = end;
    }
    b->baseline = first_baseline;
    return max_int(y - gap_cross, 0);
}

/* --- Kinder im Raster: grid --------------------------------------------------------------------- */

#define GRID_COLUMNS 24

static int layout_grid(lay_t *L, lbox_t *b, int cw)
{
    const flexgrid_t *fg = b->st->fg;
    int cx = b->bor[3] + b->pad[3], cy = b->bor[0] + b->pad[0];
    int gap_c = fg ? gap_px(fg->gap_column, cw) : 0, gap_r = fg ? gap_px(fg->gap_row, cw) : 0;
    track_t tracks[GRID_COLUMNS];
    int n = 1;
    memset(tracks, 0, sizeof(tracks));
    tracks[0].fr = 1000;
    if (fg && fg->grid_count && fg->grid_auto_fill) {
        /* so viele Spalten, wie mit der Mindestbreite passen */
        int least = len_px(fg->grid[0].size, cw);
        if (least < 40)
            least = 120;
        n = clamp((cw + gap_c) / (least + gap_c), 1, GRID_COLUMNS);
        for (int i = 0; i < n; i++)
            tracks[i] = fg->grid[0];
    } else if (fg && fg->grid_count) {
        n = min_int(fg->grid_count, GRID_COLUMNS);
        for (int i = 0; i < n; i++)
            tracks[i] = fg->grid[i];
    }

    if (fg && fg->area_columns > n && !fg->grid_auto_fill) {
        /* benannte Flächen ohne (genug) Spaltenangaben: Die übrigen Spalten teilen sich den Platz */
        int had = fg->grid_count ? n : 0;
        n = min_int(fg->area_columns, GRID_COLUMNS);
        for (int i = had; i < n; i++) {
            memset(&tracks[i], 0, sizeof(tracks[i]));
            tracks[i].fr = 1000;
        }
    }

    int count = 0;
    for (lbox_t *c = b->first; c; c = c->next) {
        if (c->flags & F_ABS) {
            c->static_x = cx;
            c->static_y = cy;
            register_abs(L, c);
        } else if (c->kind == K_BLOCK) {
            count++;
        }
    }
    if (count == 0)
        return 0;
    struct cell {
        lbox_t *box;
        int row, column, span, rows;
    } *cells = arena(L, (size_t)count * sizeof(*cells));
    int row_limit = count + CSS_AREA_ROWS + 4;
    uint32_t *taken = arena(L, (size_t)row_limit * sizeof(uint32_t));   /* je Zeile: belegte Spalten */
    int *height = arena(L, (size_t)row_limit * sizeof(int));
    int *row_y = arena(L, (size_t)row_limit * sizeof(int));
    if (!cells || !taken || !height || !row_y || (size_t)row_limit * sizeof(*cells) > SPARE_BYTES * 64)
        return 0;

    /*
     * Jedes Kind bekommt seine Zellen: nach dem Namen seiner Fläche, nach
     * ausdrücklichen Angaben - oder der Reihe nach in die nächste freie Stelle.
     */
    int k = 0, next_row = 0, next_column = 0, row_count = 0;
    for (lbox_t *c = b->first; c && k < count; c = c->next) {
        if ((c->flags & F_ABS) || c->kind != K_BLOCK)
            continue;
        const flexgrid_t *f = c->st->fg;
        int span = f && f->grid_full ? n : clamp(f && f->grid_span ? f->grid_span : 1, 1, n);
        int rows = clamp(f && f->grid_row_span ? f->grid_row_span : 1, 1, 50);
        int row = f && f->grid_row ? f->grid_row - 1 : -1;
        int column = f && f->grid_column ? min_int(f->grid_column - 1, n - 1) : f && f->grid_full ? 0 : -1;
        if (f && f->grid_area && fg && fg->area_rows) {
            int r0 = -1, r1 = -1, c0 = -1, c1 = -1;
            for (int r = 0; r < fg->area_rows; r++) {
                for (int col = 0; col < fg->area_columns && col < n; col++) {
                    if (fg->area_map[r][col] != f->grid_area)
                        continue;
                    if (r0 < 0)
                        r0 = r;
                    r1 = r;
                    c0 = c0 < 0 ? col : min_int(c0, col);
                    c1 = max_int(c1, col);
                }
            }
            if (r0 >= 0) {
                row = r0;
                column = c0;
                rows = r1 - r0 + 1;
                span = c1 - c0 + 1;
            }
        }
        if (column >= 0 && column + span > n)
            span = n - column;
        uint32_t mask = span >= 32 ? 0xFFFFFFFFu : (1u << span) - 1;
        if (row < 0 || column < 0) {
            /* die nächste freie Stelle - ab der letzten, in Leserichtung */
            int r = row >= 0 ? row : next_row, col = column >= 0 ? column : row >= 0 ? 0 : next_column;
            for (int tries = 0; tries < row_limit * GRID_COLUMNS; tries++) {
                bool free = r < row_limit && col + span <= n;
                for (int i = 0; free && i < rows && r + i < row_limit; i++)
                    free = !(taken[r + i] & (mask << col));
                if (free || r >= row_limit - 1)
                    break;
                if (column >= 0) {
                    r++;                /* die Spalte steht fest: weiter unten suchen */
                } else if (++col + span > n) {
                    col = 0;
                    if (row >= 0)
                        break;          /* die Zeile steht fest und ist voll */
                    r++;
                }
            }
            if (row < 0 && column < 0) {
                next_row = r;
                next_column = col + span;
                if (next_column >= n) {
                    next_row++;
                    next_column = 0;
                }
            }
            row = r;
            column = min_int(col, n - span);
        }
        row = clamp(row, 0, row_limit - 1);
        rows = min_int(rows, row_limit - row);
        for (int i = 0; i < rows; i++)
            taken[row + i] |= mask << column;
        cells[k].box = c;
        cells[k].row = row;
        cells[k].column = column;
        cells[k].span = span;
        cells[k].rows = rows;
        row_count = max_int(row_count, row + rows);
        k++;
    }

    /* Spaltenbreiten: feste zuerst, dann nach Inhalt, der Rest nach Anteilen */
    int width[GRID_COLUMNS], fixed = 0, total_fr = 0, loose = 0;
    for (int i = 0; i < n; i++) {
        width[i] = 0;
        if (tracks[i].fr) {
            total_fr += tracks[i].fr;
        } else if (tracks[i].automatic || !len_set(tracks[i].size, cw)) {
            for (int j = 0; j < k; j++) {
                if (cells[j].column == i && cells[j].span == 1) {
                    intrinsic(L, cells[j].box);
                    resolve_edges(cells[j].box, -1);
                    width[i] = max_int(width[i], cells[j].box->max_w + cells[j].box->margin[1] + cells[j].box->margin[3]);
                }
            }
            width[i] = min_int(width[i], cw);
            fixed += width[i];
            loose++;
        } else {
            width[i] = max_int(len_px(tracks[i].size, cw), 0);
            fixed += width[i];
        }
    }
    int rest = cw - gap_c * (n - 1) - fixed;
    if (total_fr > 0) {
        for (int i = 0; i < n; i++) {
            if (tracks[i].fr)
                width[i] = max_int((int)((int64_t)max_int(rest, 0) * tracks[i].fr / max_int(total_fr, 1000)),
                                   len_set(tracks[i].size, cw) ? len_px(tracks[i].size, cw) : 0);
        }
    } else if (rest > 0 && loose > 0) {
        for (int i = 0; i < n; i++) {
            if (!tracks[i].fr && (tracks[i].automatic || !len_set(tracks[i].size, cw)))
                width[i] += rest / loose;
        }
    } else if (rest < 0 && loose > 0) {
        /* zu breit: Die Spalten nach Inhalt geben nach */
        for (int i = 0; i < n; i++) {
            if (!tracks[i].fr && (tracks[i].automatic || !len_set(tracks[i].size, cw)))
                width[i] = max_int(width[i] + rest / loose, 20);
        }
    }

    /* jedes Kind in der Breite seiner Zellen setzen; daraus ergeben sich die Zeilenhöhen */
    for (int j = 0; j < k && !L->failed; j++) {
        lbox_t *c = cells[j].box;
        int x = 0, cell_w = 0, w;
        for (int i = 0; i < cells[j].column; i++)
            x += width[i] + gap_c;
        for (int i = 0; i < cells[j].span; i++)
            cell_w += width[cells[j].column + i] + (i ? gap_c : 0);
        resolve_edges(c, cell_w);
        int avail = cell_w - c->margin[1] - c->margin[3];
        if ((c->flags & F_REPLACED) || given_width(c, cell_w, &w))
            w = fit_width(L, c, avail, cell_w);
        else
            w = limit_width(c, cell_w, avail);
        layout_block(L, c, w, NULL, 0, 0);
        c->x = cx + x + c->margin[3];
        if (cells[j].rows == 1)
            height[cells[j].row] = max_int(height[cells[j].row], c->h + c->margin[0] + c->margin[2]);
    }
    for (int j = 0; j < k; j++) {
        /* über mehrere Zeilen: Reicht deren Höhe nicht, wächst die letzte */
        const lbox_t *c = cells[j].box;
        int have = 0;
        if (cells[j].rows < 2)
            continue;
        for (int i = 0; i < cells[j].rows; i++)
            have += height[cells[j].row + i] + (i && height[cells[j].row + i] ? gap_r : 0);
        int need = c->h + c->margin[0] + c->margin[2];
        if (need > have)
            height[cells[j].row + cells[j].rows - 1] += need - have;
    }
    int y = 0;
    for (int r = 0; r < row_count; r++) {
        row_y[r] = y;
        if (height[r] > 0)
            y += height[r] + gap_r;     /* leere Zeilen brauchen keinen Platz */
    }
    for (int j = 0; j < k; j++) {
        lbox_t *c = cells[j].box;
        uint8_t align = fg ? item_align(c, fg) : CSS_AL_STRETCH;
        int last = cells[j].row + cells[j].rows - 1;
        int room = row_y[last] + height[last] - row_y[cells[j].row];
        int outer = c->h + c->margin[0] + c->margin[2];
        c->y = cy + row_y[cells[j].row] + c->margin[0];
        if (align == CSS_AL_CENTER)
            c->y += max_int(room - outer, 0) / 2;
        else if (align == CSS_AL_END)
            c->y += max_int(room - outer, 0);
        else if (align == CSS_AL_STRETCH && !(c->flags & F_REPLACED) && !len_fixed(c->st->height))
            c->h = max_int(c->h, room - c->margin[0] - c->margin[2]);
    }
    return max_int(y - gap_r, 0);
}

/* --- Tabellen ---------------------------------------------------------------------------------- */

typedef struct {
    lbox_t *cell;
    int row, column;
} tcell_t;

typedef struct {
    tcell_t *cells;
    int cell_count;
    lbox_t **rows, **groups;            /* je Zeile: ihr Kasten (NULL: der Kasten ist selbst die Zelle) und ihre Gruppe */
    int row_count, columns;
    int low[COLUMNS_MAX], high[COLUMNS_MAX];
    int gap, edge;                      /* Abstand zwischen den Zellen und am Rand */
} table_t;

static bool table_row(const lbox_t *b)
{
    return b->st->display == CSS_D_TABLE_ROW;
}

/* Geht die Zeilen einer Tabelle durch; fill: Zeilen und Zellen eintragen (sonst nur zählen). */
static void table_walk(lbox_t *b, table_t *t, bool fill)
{
    int span_left[COLUMNS_MAX];
    int rows = 0, cells = 0;
    memset(span_left, 0, sizeof(span_left));
    t->columns = 0;
    for (lbox_t *g = b->first; g; g = g->next) {
        if ((g->flags & F_ABS) || g->kind != K_BLOCK || g->st->display == CSS_D_TABLE_CAPTION)
            continue;
        bool group = g->st->display == CSS_D_TABLE_GROUP;
        for (lbox_t *r = group ? g->first : g; r; r = group ? r->next : NULL) {
            if ((r->flags & F_ABS) || r->kind != K_BLOCK)
                continue;
            int column = 0;
            bool is_row = table_row(r);
            for (lbox_t *c = is_row ? r->first : r; c; c = is_row ? c->next : NULL) {
                if ((c->flags & F_ABS) || c->kind != K_BLOCK)
                    continue;
                while (column < COLUMNS_MAX - 1 && span_left[column] > 0)
                    column++;
                int span = min_int(c->colspan, COLUMNS_MAX - column);
                if (fill) {
                    t->cells[cells].cell = c;
                    t->cells[cells].row = rows;
                    t->cells[cells].column = column;
                }
                for (int i = 0; i < span; i++)
                    span_left[column + i] = c->rowspan;
                column += span;
                t->columns = max_int(t->columns, column);
                cells++;
            }
            if (fill) {
                t->rows[rows] = is_row ? r : NULL;
                t->groups[rows] = group ? g : NULL;
            }
            for (int i = 0; i < COLUMNS_MAX; i++) {
                if (span_left[i] > 0)
                    span_left[i]--;
            }
            rows++;
        }
    }
    t->row_count = rows;
    t->cell_count = cells;
}

/* Die Aufteilung einer Tabelle: Zeilen, Zellen, und wie breit jede Spalte werden will */
static table_t *table_of(lay_t *L, lbox_t *b)
{
    if (b->aux)
        return b->aux;
    table_t *t = arena(L, sizeof(*t));
    table_walk(b, t, false);
    size_t cells = (size_t)max_int(t->cell_count, 1), rows = (size_t)max_int(t->row_count, 1);
    t->cells = arena(L, cells * sizeof(tcell_t));
    t->rows = arena(L, rows * sizeof(lbox_t *));
    t->groups = arena(L, rows * sizeof(lbox_t *));
    if (!t->cells || !t->rows || !t->groups || cells * sizeof(tcell_t) > SPARE_BYTES * 256) {
        t->cell_count = t->row_count = t->columns = 0;
        b->aux = t;
        return t;
    }
    table_walk(b, t, true);

    /* Zellen mit gemeinsamen Rahmen: Sie überlappen sich um die Rahmenbreite */
    t->gap = t->edge = b->st->spacing;
    if (b->st->collapse) {
        t->edge = 0;
        t->gap = t->cell_count ? -(int)t->cells[0].cell->st->border[3] : 0;
    }
    for (int i = 0; i < t->cell_count; i++) {
        lbox_t *c = t->cells[i].cell;
        intrinsic(L, c);
        if (c->colspan == 1 && t->cells[i].column < COLUMNS_MAX) {
            int col = t->cells[i].column;
            t->low[col] = max_int(t->low[col], c->min_w);
            t->high[col] = max_int(t->high[col], c->max_w);
        }
    }
    /* Zellen über mehrere Spalten: Reicht deren Breite nicht, wachsen alle gleichmäßig */
    for (int i = 0; i < t->cell_count; i++) {
        lbox_t *c = t->cells[i].cell;
        int col = t->cells[i].column, span = min_int(c->colspan, t->columns - col);
        if (span < 2)
            continue;
        int low = t->gap * (span - 1), high = low;
        for (int k = 0; k < span; k++) {
            low += t->low[col + k];
            high += t->high[col + k];
        }
        for (int k = 0; k < span; k++) {
            if (c->min_w > low)
                t->low[col + k] += (c->min_w - low + span - 1) / span;
            if (c->max_w > high)
                t->high[col + k] += (c->max_w - high + span - 1) / span;
        }
    }
    for (int i = 0; i < t->columns; i++)
        t->high[i] = max_int(t->high[i], t->low[i]);
    b->aux = t;
    return t;
}

static void table_measure(lay_t *L, lbox_t *b, int *min_w, int *max_w)
{
    table_t *t = table_of(L, b);
    int low = 2 * t->edge + t->gap * max_int(t->columns - 1, 0), high = low;
    for (int i = 0; i < t->columns; i++) {
        low += t->low[i];
        high += t->high[i];
    }
    for (lbox_t *c = b->first; c; c = c->next) {
        if (c->kind == K_BLOCK && c->st->display == CSS_D_TABLE_CAPTION) {
            intrinsic(L, c);
            low = max_int(low, c->min_w);
        }
    }
    *min_w = max_int(low, 0);
    *max_w = max_int(high, low);
}

static int layout_table(lay_t *L, lbox_t *b, int cw)
{
    table_t *t = table_of(L, b);
    int cx = b->bor[3] + b->pad[3], cy = b->bor[0] + b->pad[0], top = 0;

    /* die Beschriftung steht über der Tabelle - oder, wenn die Seite es so will, darunter */
    int below = 0;
    for (lbox_t *c = b->first; c; c = c->next) {
        if (c->flags & F_ABS) {
            c->static_x = cx;
            c->static_y = cy;
            register_abs(L, c);
        } else if (c->kind == K_BLOCK && c->st->display == CSS_D_TABLE_CAPTION) {
            resolve_edges(c, cw);
            layout_block(L, c, max_int(cw - c->margin[1] - c->margin[3], 0), NULL, 0, 0);
            c->x = cx + c->margin[3];
            if (c->st->caption_below) {
                below += c->margin[0] + c->h + c->margin[2];
            } else {
                c->y = cy + top + c->margin[0];
                top += c->margin[0] + c->h + c->margin[2];
            }
        }
    }
    if (t->columns == 0 || t->row_count == 0) {
        int y = top;
        for (lbox_t *c = b->first; c; c = c->next) {
            if (!(c->flags & F_ABS) && c->kind == K_BLOCK && c->st->display == CSS_D_TABLE_CAPTION &&
                c->st->caption_below) {
                c->y = cy + y + c->margin[0];
                y += c->margin[0] + c->h + c->margin[2];
            }
        }
        return y;
    }

    /* Spaltenbreiten: zwischen Mindest- und Wunschbreite, je nach Platz */
    int width[COLUMNS_MAX], x[COLUMNS_MAX];
    int avail = cw - 2 * t->edge - t->gap * (t->columns - 1), low = 0, high = 0;
    for (int i = 0; i < t->columns; i++) {
        low += t->low[i];
        high += t->high[i];
    }
    for (int i = 0, at = t->edge; i < t->columns; i++) {
        if (avail >= high)
            width[i] = t->high[i] + (high > 0 ? (int)((int64_t)(avail - high) * t->high[i] / high)
                                              : (avail - high) / t->columns);
        else if (avail <= low)
            width[i] = t->low[i];
        else
            width[i] = t->low[i] + (int)((int64_t)(t->high[i] - t->low[i]) * (avail - low) / max_int(high - low, 1));
        x[i] = at;
        at += width[i] + t->gap;
    }
    int used = 0;
    for (int i = 0; i < t->columns; i++)
        used += width[i];
    if (avail >= high) {
        /* was beim Verteilen durch Runden übrig blieb, bekommt die letzte Spalte */
        width[t->columns - 1] += avail - used;
    } else if (avail > low) {
        /* ... oder die Spalten, denen noch ein Pixel zur Wunschbreite fehlt */
        for (int i = 0; i < t->columns && used < avail; i++) {
            if (width[i] < t->high[i]) {
                width[i]++;
                used++;
            }
        }
    }
    for (int i = 0, at = t->edge; i < t->columns; i++) {
        x[i] = at;
        at += width[i] + t->gap;
    }

    int *height = arena(L, (size_t)t->row_count * sizeof(int));
    int *row_y = arena(L, (size_t)t->row_count * sizeof(int));
    if (!height || !row_y || (size_t)t->row_count * sizeof(int) > SPARE_BYTES * 64)
        return top;
    for (int i = 0; i < t->cell_count && !L->failed; i++) {
        lbox_t *c = t->cells[i].cell;
        int col = t->cells[i].column, span = max_int(min_int(c->colspan, t->columns - col), 1), w = 0;
        for (int k = 0; k < span; k++)
            w += width[col + k] + (k ? t->gap : 0);
        resolve_edges(c, w);
        layout_block(L, c, w, NULL, 0, 0);
        if (c->rowspan == 1)
            height[t->cells[i].row] = max_int(height[t->cells[i].row], c->h);
    }
    for (int r = 0; r < t->row_count; r++) {
        if (t->rows[r] && len_fixed(t->rows[r]->st->height))
            height[r] = max_int(height[r], t->rows[r]->st->height.px);
    }
    /* Zellen über mehrere Zeilen: Die letzte davon wächst, wenn es nicht reicht */
    for (int i = 0; i < t->cell_count; i++) {
        lbox_t *c = t->cells[i].cell;
        int r = t->cells[i].row, span = min_int(c->rowspan, t->row_count - r), have = 0;
        if (span < 2)
            continue;
        for (int k = 0; k < span; k++)
            have += height[r + k] + (k ? t->gap : 0);
        if (c->h > have)
            height[r + span - 1] += c->h - have;
    }
    int y = t->edge;
    for (int r = 0; r < t->row_count; r++) {
        row_y[r] = y;
        y += height[r] + t->gap;
    }
    int total = y - t->gap + t->edge;

    /* Gruppen und Zeilen bekommen ihre Lage; Zellen stehen in ihrer Zeile */
    for (int r = 0; r < t->row_count; r++) {
        lbox_t *group = t->groups[r], *row = t->rows[r];
        if (group && (r == 0 || t->groups[r - 1] != group)) {
            int last = r;
            while (last + 1 < t->row_count && t->groups[last + 1] == group)
                last++;
            resolve_edges(group, cw);
            memset(group->pad, 0, sizeof(group->pad));
            memset(group->bor, 0, sizeof(group->bor));
            group->x = cx;
            group->y = cy + top + row_y[r];
            group->w = cw;
            group->h = row_y[last] + height[last] - row_y[r];
        }
        if (row) {
            resolve_edges(row, cw);
            memset(row->pad, 0, sizeof(row->pad));
            memset(row->bor, 0, sizeof(row->bor));
            row->x = group ? 0 : cx;
            row->y = group ? row_y[r] - (group->y - cy - top) : cy + top + row_y[r];
            row->w = cw;
            row->h = height[r];
        }
    }
    for (int i = 0; i < t->cell_count; i++) {
        lbox_t *c = t->cells[i].cell;
        int r = t->cells[i].row, span = max_int(min_int(c->rowspan, t->row_count - r), 1), h = 0;
        for (int k = 0; k < span; k++)
            h += height[r + k] + (k ? t->gap : 0);
        int spare = h - c->h;
        if (spare > 0) {
            c->h = h;
            shift_content(c, c->st->valign == CSS_VA_MIDDLE ? spare / 2
                             : c->st->valign == CSS_VA_BOTTOM ? spare : 0);
        }
        if (t->rows[r]) {
            c->x = x[t->cells[i].column];
            c->y = 0;
        } else {
            lbox_t *group = t->groups[r];
            c->x = (group ? 0 : cx) + x[t->cells[i].column];
            c->y = group ? row_y[r] - (group->y - cy - top) : cy + top + row_y[r];
        }
    }
    if (below > 0) {
        int y = top + total;
        for (lbox_t *c = b->first; c; c = c->next) {
            if (!(c->flags & F_ABS) && c->kind == K_BLOCK && c->st->display == CSS_D_TABLE_CAPTION &&
                c->st->caption_below) {
                c->y = cy + y + c->margin[0];
                y += c->margin[0] + c->h + c->margin[2];
            }
        }
    }
    return top + total + below;
}

/* --- Lage auf der Seite ------------------------------------------------------------------------ */

static void finalize(lay_t *L, lbox_t *b, int ax, int ay, int depth);

static void finalize_children(lay_t *L, lbox_t *parent, int ax, int ay, int depth)
{
    for (lbox_t *c = parent->first; c; c = c->next) {
        if (c->flags & F_ABS)
            continue;                   /* steht, wo sein Bezugskasten es sagt */
        if (c->kind == K_BLOCK) {
            finalize(L, c, ax + c->x, ay + c->y, depth + 1);
        } else if (c->kind == K_INLINE) {
            c->ax = ax;
            c->ay = ay;
            finalize_children(L, c, ax, ay, depth + 1);
        }
    }
}

static void finalize(lay_t *L, lbox_t *b, int ax, int ay, int depth)
{
    const style_t *st = b->st;
    if (depth > DEPTH_MAX * 4)
        return;
    if ((b->flags & F_RELATIVE) && b->parent) {
        /* verschoben gegenüber dem Platz im Fluss */
        int base = b->parent->w;
        ax += len_set(st->inset[3], base) ? len_px(st->inset[3], base)
            : len_set(st->inset[1], base) ? -len_px(st->inset[1], base) : 0;
        ay += len_fixed(st->inset[0]) ? st->inset[0].px : len_fixed(st->inset[2]) ? -st->inset[2].px : 0;
    }
    b->ax = ax;
    b->ay = ay;
    /* verschoben und mit eigener Ebene: wird wie ein frei platzierter Kasten gezeichnet */
    if ((b->flags & F_RELATIVE) && st->z_set && b != L->root)
        add_layer(L, b);
    finalize_children(L, b, ax, ay, depth);
    for (lbox_t *a = b->abs_list; a; a = a->abs_next)
        finalize(L, a, ax + a->x, ay + a->y, depth + 1);
}

/* --- Die fertige Seite -------------------------------------------------------------------------- */

static void emit_box(lay_t *L, lbox_t *b, int depth);

/* Die Farbe, in der Text dieser Gestaltung erscheint - notfalls eine, die auf dem Grund zu lesen ist */
static uint32_t text_color(const lay_t *L, const style_t *st)
{
    const html_text_t *t = L->opt->text;
    uint32_t usual = st->text & STYLE_LINK ? t->link_color : st->text & STYLE_MUTED ? 0x80848E : t->text_color;
    uint32_t color = st->color ? st->color & 0xFFFFFF : usual & 0xFFFFFF;
    if (readable(color, st->back))
        return st->color || st->back != (t->page_color & 0xFFFFFF) ? HTML_COLOR_SET | color : 0;
    if (readable(t->text_color & 0xFFFFFF, st->back))
        return HTML_COLOR_SET | (t->text_color & 0xFFFFFF);
    return HTML_COLOR_SET | (brightness(st->back) > 128 ? 0x000000 : 0xFFFFFF);
}

/* Hintergrund und Rahmen als Kasten der Seite */
static void emit_decoration(lay_t *L, const style_t *st, const dom_node *node, int x, int y, int w, int h,
                            const uint8_t *border, bool left_end, bool right_end)
{
    bool lines = false;
    for (int i = 0; i < 4; i++)
        lines = lines || (border[i] && st->border_style[i]);
    if (st->invisible || w <= 0 || h <= 0 || (!st->background_set && !lines && st->link < 0))
        return;
    if (!st->background_set && !lines)
        return;                         /* nichts zu sehen */
    box_t box;
    memset(&box, 0, sizeof(box));
    box.x = x;
    box.y = y;
    box.w = w;
    box.h = h;
    box.background = st->background_set ? HTML_COLOR_SET | st->background : 0;
    for (int i = 0; i < 4; i++) {
        bool shown = st->border_style[i] && border[i] && !(i == 3 && !left_end) && !(i == 1 && !right_end);
        box.border[i] = shown ? border[i] : 0;
        box.border_color[i] = st->border_color[i];
        if (shown && st->border_style[i] != CSS_BS_SOLID)
            box.dashed = st->border_style[i] == CSS_BS_DASHED ? 1 : 2;
    }
    box.radius = st->radius == 255 ? (uint8_t)min_int(min_int(w, h) / 2, 250) : st->radius;
    box.link = st->link;
    box.node = node ? node->id : 0;
    int index = page_add_box(L->page, &box);
    if (index >= 0)
        page_add_paint(L->page, PAINT_BOX, index);
    L->bottom = max_int(L->bottom, y + h);
}

static uint32_t pool_add(lay_t *L, const char *text, size_t n)
{
    page_t *page = L->page;
    if (page->pool_len + n + 1 > page->pool_cap) {
        size_t cap = page->pool_cap ? page->pool_cap * 2 : 16384;
        while (cap < page->pool_len + n + 1)
            cap *= 2;
        char *grown = kmalloc(cap);
        if (!grown) {
            L->failed = true;
            return 0;
        }
        if (page->pool_len)
            memcpy(grown, page->pool, page->pool_len);
        kfree(page->pool);
        page->pool = grown;
        page->pool_cap = cap;
    }
    uint32_t at = (uint32_t)page->pool_len;
    memcpy(page->pool + at, text, n);
    page->pool[at + n] = '\0';
    page->pool_len += n + 1;
    return at;
}

/* Beginnt eine Zeile der Seite; liefert sie oder NULL. */
static line_t *page_line(lay_t *L, int y, int height, int baseline)
{
    page_t *page = L->page;
    if (page->line_count >= 400000 || !GROW(L, page->lines, page->line_count, page->line_cap))
        return NULL;
    line_t *line = &page->lines[page->line_count];
    memset(line, 0, sizeof(*line));
    line->first = (uint32_t)page->span_count;
    line->image = -1;
    line->y = y;
    line->height = (uint16_t)clamp(height, 1, 60000);
    line->baseline = (uint16_t)clamp(baseline, 0, 60000);
    L->bottom = max_int(L->bottom, y + height);
    return line;
}

static void page_span(lay_t *L, line_t *line, const style_t *st, const dom_node *node, const char *text,
                      size_t n, int x, int w, int shift)
{
    page_t *page = L->page;
    if (st->invisible || n == 0 || x + w <= 0 || x > 60000 || !GROW(L, page->spans, page->span_count, page->span_cap))
        return;
    span_t *span = &page->spans[page->span_count];
    memset(span, 0, sizeof(*span));
    span->text = pool_add(L, text, n);
    span->bytes = (uint16_t)n;
    span->column = (uint16_t)max_int(x, 0);
    span->width = (uint16_t)clamp(w, 0, 60000);
    span->style = st->text;
    span->size = st->size;
    span->color = text_color(L, st);
    span->link = st->link;
    span->widget = -1;
    span->image = -1;
    span->shift = (int8_t)clamp(shift, -120, 120);
    span->node = node ? node->id : 0;
    page->span_count++;
    line->count++;
}

static void finish_page_line(lay_t *L, line_t *line)
{
    if (line && line->count > 0) {
        page_add_paint(L->page, PAINT_LINE, L->page->line_count);
        L->page->line_count++;
    }
}

/* Der Listenpunkt: Zeichen oder Nummer links vor dem Kasten */
static void ordinal_text(char *out, size_t size, uint8_t type, int n)
{
    static const char *const roman[] = { "m", "cm", "d", "cd", "c", "xc", "l", "xl", "x", "ix", "v", "iv", "i" };
    static const int value[] = { 1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1 };
    size_t len = 0;
    bool upper = type == CSS_LT_UPPER_ALPHA || type == CSS_LT_UPPER_ROMAN;
    if ((type == CSS_LT_LOWER_ALPHA || type == CSS_LT_UPPER_ALPHA) && n >= 1 && n <= 702) {
        if (n > 26)
            out[len++] = (char)('a' + (n - 1) / 26 - 1);
        out[len++] = (char)('a' + (n - 1) % 26);
    } else if ((type == CSS_LT_LOWER_ROMAN || type == CSS_LT_UPPER_ROMAN) && n >= 1 && n < 4000) {
        for (int i = 0; i < 13; i++) {
            while (n >= value[i] && len + 4 < size) {
                for (const char *p = roman[i]; *p; p++)
                    out[len++] = *p;
                n -= value[i];
            }
        }
    } else {
        len = (size_t)ksnprintf(out, size, "%d", n);
    }
    for (size_t i = 0; upper && i < len; i++)
        out[i] = (char)(out[i] - 32);
    out[len++] = '.';
    out[len] = '\0';
}

/* Die erste Grundlinie im Kasten, ab seiner Oberkante; -1: Er enthält keinen Text. */
static int first_baseline(const lbox_t *b, int depth)
{
    if (b->flags & F_LINES)
        return b->lines ? b->bor[0] + b->pad[0] + b->lines->y + b->lines->baseline : -1;
    for (const lbox_t *c = b->first; c && depth < 12; c = c->next) {
        if (c->kind != K_BLOCK || (c->flags & (F_ABS | F_FLOAT | F_REPLACED)))
            continue;
        int inner = first_baseline(c, depth + 1);
        if (inner >= 0)
            return c->y + inner;
    }
    return -1;
}

static void emit_marker(lay_t *L, lbox_t *b)
{
    const style_t *st = b->st;
    char text[24];
    switch (st->list_type) {
    case CSS_LT_DISC:   strlcpy(text, "\xE2\x80\xA2", sizeof(text)); break;
    case CSS_LT_CIRCLE: strlcpy(text, "\xE2\x97\xA6", sizeof(text)); break;
    case CSS_LT_SQUARE: strlcpy(text, "\xE2\x96\xAA", sizeof(text)); break;
    default:            ordinal_text(text, sizeof(text), st->list_type, b->ordinal); break;
    }
    int a, d, base = first_baseline(b, 0);
    font_metrics_of(L, st, &a, &d);
    if (base < 0)
        base = b->bor[0] + b->pad[0] + a + (line_height_of(L, st) - (a + d)) / 2;
    int w = text_width(L, st, text, strlen(text));
    line_t *line = page_line(L, b->ay + base - a, a + d, a);
    if (!line)
        return;
    /* ohne die Eigenschaften des Texts im Kasten: Der Punkt ist nie unterstrichen oder ein Link */
    style_t plain = *st;
    plain.text &= (uint8_t)~(STYLE_UNDERLINE | STYLE_LINK | STYLE_MONO);
    plain.link = -1;
    page_span(L, line, &plain, b->node, text, strlen(text), b->ax + b->bor[3] + b->pad[3] - w - max_int(st->size / 3, 5), w, 0);
    finish_page_line(L, line);
}

static void emit_children(lay_t *L, lbox_t *parent, int depth, bool floats_only);

static void emit_lines(lay_t *L, lbox_t *b, int depth)
{
    int ox = b->ax + b->bor[3] + b->pad[3], oy = b->ay + b->bor[0] + b->pad[0];
    for (lline_t *ll = b->lines; ll && !L->failed; ll = ll->next) {
        for (deco_t *d = ll->decos; d; d = d->next)
            emit_decoration(L, d->box->st, d->box->node, ox + d->x, oy + ll->y + d->top, d->w, d->h,
                            d->box->bor, d->opens, d->closes);
        line_t *line = page_line(L, oy + ll->y, ll->h, ll->baseline);
        if (!line)
            return;
        for (piece_t *p = ll->first; p; p = p->next) {
            if (!p->atom)
                page_span(L, line, p->st, p->node, p->text, p->bytes, ox + p->x, p->w, p->shift);
        }
        finish_page_line(L, line);
        for (piece_t *p = ll->first; p; p = p->next) {
            if (p->atom && !(p->atom->flags & F_LAYER))
                emit_box(L, p->atom, depth + 1);
        }
    }
}

/* Die Kinder eines Kastens - durch Elemente im Text hindurch; floats_only: nur Kästen neben dem Text */
static void emit_children(lay_t *L, lbox_t *parent, int depth, bool floats_only)
{
    for (lbox_t *c = parent->first; c && !L->failed; c = c->next) {
        if (c->flags & (F_ABS | F_LAYER))
            continue;                   /* kommen am Schluss, über allem anderen */
        if (c->kind == K_INLINE)
            emit_children(L, c, depth + 1, floats_only);
        else if (c->kind == K_BLOCK && (!floats_only || (c->flags & F_FLOAT)))
            emit_box(L, c, depth + 1);
    }
}

static void emit_layers(lay_t *L, bool behind);

static void emit_box(lay_t *L, lbox_t *b, int depth)
{
    page_t *page = L->page;
    const style_t *st = b->st;
    if (depth > DEPTH_MAX * 4)
        return;
    L->bottom = max_int(L->bottom, b->ay + b->h);
    if (b->widget >= 0) {
        widget_t *w = &page->widgets[b->widget];
        w->x = b->ax;
        w->y = b->ay;
        w->w = b->w;
        w->h = st->invisible ? 0 : b->h;
        if ((w->kind == W_SUBMIT || w->kind == W_BUTTON) && st->background_set) {
            /* ein Knopf mit eigenem Aussehen: Farben, Schrift und Ecken der Seite */
            uint32_t color = text_color(L, st);
            w->background = HTML_COLOR_SET | st->background;
            w->color = color ? color : HTML_COLOR_SET | (L->opt->text->text_color & 0xFFFFFF);
            w->size = st->size;
            w->radius = st->radius == 255 ? (uint8_t)min_int(b->h / 2, 250) : st->radius;
            w->bold = (st->text & STYLE_BOLD) != 0;
        }
        if (w->h > 0)
            page_add_paint(page, PAINT_WIDGET, b->widget);
        return;
    }
    emit_decoration(L, st, b->node, b->ax, b->ay, b->w, b->h, b->bor, true, true);
    if (b == L->root)
        emit_layers(L, true);
    if (b->image >= 0) {
        page_image_t *image = &page->images[b->image];
        image->x = b->ax + b->bor[3] + b->pad[3];
        image->y = b->ay + b->bor[0] + b->pad[0];
        image->width = max_int(b->w - edge_w(b), 1);
        image->height = max_int(b->h - edge_h(b), 1);
        if (st->invisible)
            image->y = -1;
        else
            page_add_paint(page, PAINT_IMAGE, b->image);
        return;
    }
    if (b->flags & F_REPLACED)
        return;
    if ((b->flags & F_MARKER) && !st->invisible)
        emit_marker(L, b);
    if (b->flags & F_LINES) {
        emit_children(L, b, depth, true);
        emit_lines(L, b, depth);
    } else {
        emit_children(L, b, depth, false);
    }
}

/* Frei platzierte Kästen: nach ihrer Ebene (z-index) geordnet; behind: die hinter dem Inhalt */
static void emit_layers(lay_t *L, bool behind)
{
    int count = 0;
    for (lbox_t *a = L->layers; a; a = a->layer_next)
        count++;
    if (count == 0 || count > 4000)
        count = min_int(count, 4000);
    if (count == 0)
        return;
    lbox_t **order = arena(L, (size_t)count * sizeof(lbox_t *));
    if (!order || (size_t)count * sizeof(lbox_t *) > SPARE_BYTES)
        return;
    int n = 0;
    for (lbox_t *a = L->layers; a && n < count; a = a->layer_next) {
        bool back = a->st->z_set && a->st->z < 0;
        if (back != behind)
            continue;
        int k = n++;
        for (; k > 0 && (order[k - 1]->st->z > a->st->z ||
                         (order[k - 1]->st->z == a->st->z && order[k - 1]->seq > a->seq)); k--)
            order[k] = order[k - 1];
        order[k] = a;
    }
    for (int i = 0; i < n; i++)
        emit_box(L, order[i], 0);
}

/* --- Einstieg ---------------------------------------------------------------------------------- */

void call_on_stack(void (*fn)(void *arg), void *arg, void *stack_top);     /* task.h */

#define STACK_BYTES (1024 * 1024)

static const dom_node *find_element(const dom_node *root, const char *name, int depth)
{
    for (const dom_node *c = root->first; c && depth < 6; c = c->next) {
        if (is_tag(c, name))
            return c;
        if (c->type == DOM_ELEMENT) {
            const dom_node *inner = find_element(c, name, depth + 1);
            if (inner)
                return inner;
        }
    }
    return NULL;
}

static void run(void *arg)
{
    lay_t *L = arg;
    page_t *page = L->page;
    const layout_options_t *opt = L->opt;
    const dom_node *html = NULL;
    for (const dom_node *c = L->doc->root->first; c && !html; c = c->next) {
        if (c->type == DOM_ELEMENT)
            html = c;
    }
    if (!html)
        return;

    const dom_node *base = find_element(html, "base", 0), *title = find_element(html, "title", 0);
    const char *href = base ? dom_attr_get(base, "href") : NULL;
    if (href) {
        char *full = resolve(L, href, true);
        if (full)
            strlcpy(L->base, full, sizeof(L->base));
        kfree(full);
    }
    if (title) {
        page->title = flat_text(title, 200);
        if (page->title)
            claude_plain_text(page->title);
    }

    style_t *initial = root_style(L);
    style_t *st = compute_style(L, html, initial, true);
    lbox_t *root = new_box(L, K_BLOCK, html, st);
    root->flags = F_ROOT;
    L->root = root;
    L->form = -1;
    build_children(L, html, root, st, 0);
    fix_children(L, root);

    /* Der Hintergrund der Seite: der des Wurzelelements, sonst der von <body> */
    if (st->background_set) {
        L->canvas = st->background;
        L->canvas_set = true;
    } else {
        for (lbox_t *c = root->first; c; c = c->next) {
            if (c->kind == K_BLOCK && is_tag(c->node, "body") && c->st->background_set) {
                L->canvas = c->st->background;
                L->canvas_set = true;
            }
        }
    }

    resolve_edges(root, opt->width);
    layout_block(L, root, max_int(opt->width - root->margin[1] - root->margin[3], 50), NULL, 0, 0);
    finalize(L, root, root->margin[3], root->margin[0], 0);
    emit_box(L, root, 0);
    emit_layers(L, false);

    for (target_t *t = L->targets; t; t = t->next) {
        const lbox_t *b = t->box;
        int y = b->kind == K_INLINE
                    ? (b->line_block ? b->line_block->ay + b->line_block->bor[0] + b->line_block->pad[0] + b->line_y : -1)
                    : b->ay;
        if (!t->id || y < 0 || !GROW(L, page->anchors, page->anchor_count, page->anchor_cap)) {
            kfree(t->id);
            continue;
        }
        page->anchors[page->anchor_count].id = t->id;
        page->anchors[page->anchor_count].line = 0;
        page->anchors[page->anchor_count].y = y;
        page->anchor_count++;
    }
    L->targets = NULL;
    page->height = max_int(root->ay + root->h + root->margin[2], L->bottom);
    page->heading_y = L->heading ? L->heading->ay : -1;
    if (L->canvas_set)
        page->background = HTML_COLOR_SET | L->canvas;
}

page_t *layout_document(const dom_doc *doc, const layout_options_t *options)
{
    lay_t *L = kzalloc(sizeof(*L));
    page_t *page = kzalloc(sizeof(*page));
    void *stack = kmalloc(STACK_BYTES);
    if (!L || !page || !stack || !doc || !doc->root || !options->text || !options->text->width) {
        kfree(L);
        kfree(page);
        kfree(stack);
        return NULL;
    }
    L->opt = options;
    L->doc = doc;
    L->page = page;
    L->css = options->css;
    if (!L->css) {
        L->own_css = css_create(options->width);
        if (L->own_css)
            css_set_viewport_height(L->own_css, options->height);
        L->css = L->own_css;
    }
    L->author_rules = css_rule_count(L->css) > 0;
    strlcpy(L->base, options->base_url ? options->base_url : "", sizeof(L->base));
    page->heading_line = -1;
    page->heading_y = -1;
    page->pixels = true;

    call_on_stack(run, L, (char *)stack + STACK_BYTES);

    for (target_t *t = L->targets; t; t = t->next)
        kfree(t->id);
    if (L->failed)
        page->failed = true;
    arena_free(L);
    css_free(L->own_css);
    kfree(stack);
    kfree(L);
    return page;
}
