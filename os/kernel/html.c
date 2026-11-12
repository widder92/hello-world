#include "html.h"
#include "css.h"
#include "claude.h"
#include "console.h"
#include "memory.h"
#include "string.h"
#include "url.h"

#define STACK_MAX    192
#define ATTRS_MAX    24
#define WORD_MAX     480
#define CAPTURE_MAX  512
#define LINES_MAX    120000
#define LISTS_MAX    8
#define FLOATS_MAX   6
#define FLOAT_GAP    14              /* Abstand zwischen Text und Kasten daneben */

typedef enum {
    T_UNKNOWN, T_A, T_BOLD, T_BLOCK, T_BLOCKQUOTE, T_BR, T_BUTTON, T_DD, T_DL, T_DT,
    T_FORM, T_HEADING, T_HR, T_IMG, T_INPUT, T_LI, T_OL, T_OPTION, T_P, T_PRE,
    T_SCRIPT, T_SELECT, T_SKIP, T_STYLE, T_TABLE, T_TD, T_TEXTAREA, T_TH, T_TITLE,
    T_TR, T_UL, T_VOID, T_ITALIC, T_MONO,
} tag_t;

static const struct {
    const char *name;
    tag_t tag;
} tag_names[] = {
    { "a", T_A }, { "b", T_BOLD }, { "strong", T_BOLD },
    { "i", T_ITALIC }, { "em", T_ITALIC }, { "cite", T_ITALIC }, { "var", T_ITALIC },
    { "dfn", T_ITALIC }, { "code", T_MONO }, { "tt", T_MONO }, { "kbd", T_MONO },
    { "samp", T_MONO },
    { "blockquote", T_BLOCKQUOTE }, { "br", T_BR }, { "button", T_BUTTON },
    { "dd", T_DD }, { "dl", T_DL }, { "dt", T_DT }, { "form", T_FORM },
    { "h1", T_HEADING }, { "h2", T_HEADING }, { "h3", T_HEADING },
    { "h4", T_HEADING }, { "h5", T_HEADING }, { "h6", T_HEADING }, { "hr", T_HR },
    { "img", T_IMG }, { "input", T_INPUT }, { "li", T_LI }, { "ol", T_OL },
    { "option", T_OPTION }, { "p", T_P }, { "pre", T_PRE }, { "script", T_SCRIPT },
    { "select", T_SELECT }, { "style", T_STYLE }, { "table", T_TABLE },
    { "td", T_TD }, { "textarea", T_TEXTAREA }, { "th", T_TH }, { "title", T_TITLE },
    { "tr", T_TR }, { "ul", T_UL }, { "menu", T_UL },
    /* Abschnitte: beginnen und enden mit einem Zeilenwechsel */
    { "div", T_BLOCK }, { "section", T_BLOCK }, { "article", T_BLOCK },
    { "header", T_BLOCK }, { "footer", T_BLOCK }, { "nav", T_BLOCK },
    { "main", T_BLOCK }, { "aside", T_BLOCK }, { "address", T_BLOCK },
    { "fieldset", T_BLOCK }, { "figure", T_BLOCK }, { "figcaption", T_BLOCK },
    { "details", T_BLOCK }, { "summary", T_BLOCK }, { "legend", T_BLOCK },
    { "center", T_BLOCK }, { "caption", T_BLOCK }, { "body", T_BLOCK },
    /* Inhalt, mit dem ein Browser ohne Bilder und Skripte nichts anfangen kann */
    { "svg", T_SKIP }, { "template", T_SKIP }, { "iframe", T_SKIP },
    { "canvas", T_SKIP }, { "video", T_SKIP }, { "audio", T_SKIP },
    { "object", T_SKIP }, { "datalist", T_SKIP }, { "map", T_SKIP },
    /* Elemente ohne Inhalt und ohne Ende-Marke */
    { "meta", T_VOID }, { "link", T_VOID }, { "area", T_VOID }, { "base", T_VOID },
    { "col", T_VOID }, { "embed", T_VOID }, { "source", T_VOID },
    { "track", T_VOID }, { "wbr", T_VOID }, { "param", T_VOID },
};

typedef struct {
    const char *name, *value;
    size_t name_len, value_len;
} attr_t;

/* Was ein offenes Element verändert hat und bei seinem Ende zurückzunehmen ist */
enum { U_HIDDEN = 1, U_BOLD = 2, U_LINK = 4, U_PRE = 8, U_HEADING = 16, U_LIST = 32,
       U_QUOTE = 64, U_CAPTURE = 128, U_ITALIC = 256, U_MONO = 512,
       U_BREAK = 1024,                  /* laut Gestaltung ein eigener Block: danach neue Zeile */
       U_INLINE = 2048 };               /* laut Gestaltung im Textfluss: keine neue Zeile */

/* Was die Gestaltung der Seite für das laufende Element festlegt (wird vererbt) */
typedef struct {
    uint32_t color;                     /* 0 oder HTML_COLOR_SET | Farbe */
    uint32_t background;                /* Hintergrund des umgebenden Blocks */
    uint32_t highlight;                 /* Hintergrund nur des Textstücks */
    int8_t bold, italic, underline;     /* -1: wie das Element es von sich aus hält */
    uint8_t size;                       /* Schriftgröße in Pixeln; 0: die übliche */
    uint8_t align;
    bool list_plain;                    /* Listen ohne Punkte */
} style_t;

typedef struct {
    uint8_t tag;
    uint16_t undo;
    css_node_t node;                    /* woran Stylesheet-Regeln das Element erkennen */
    style_t saved;                      /* was vor dem Element galt */
} element_t;

typedef struct {
    page_t *page;
    const char *base;
    int columns;
    const html_pictures_t *pictures;    /* NULL: Bilder nur als Ersatztext */
    const html_text_t *text;            /* NULL: festes Zeichenraster */
    int unit;                           /* Breite eines Rasterzeichens: 1 oder Pixel */
    style_t cur;                        /* geltende Gestaltung */
    uint32_t page_color;                /* Hintergrund der Seite, für die Lesbarkeitsprüfung */
    const char *name;                   /* Name des Elements, das gerade beginnt */
    size_t name_len;

    /* Tabellen (nur in echter Schrift): Zellen werden einzeln gesetzt */
    int table_depth;                    /* so tief stecken wir in Tabellen */
    int floor;                          /* Elemente unterhalb dieser Tiefe schließt eine Zelle nicht */
    int widest_line, widest_piece;      /* beim Ausmessen: breiteste Zeile, breitestes Stück */
    int cell_line;                      /* erste Zeile der laufenden Zelle; davor ist nichts */

    /* Kästen neben dem Text ("float"): fertig gesetzt, warten auf ihre Zeilen */
    struct {
        int side;                       /* CSS_SIDE_LEFT oder CSS_SIDE_RIGHT */
        int start, count;               /* erste Zeile der Seite und Anzahl Zeilen */
        int x, width;
        line_t *lines;
        span_t *spans;
    } floats[FLOATS_MAX];
    int float_count;
    int in_float;                       /* wir setzen gerade den Inhalt eines solchen Kastens */
    int pending_float;                  /* das eben begonnene Element gehört neben den Text */
    bool no_float;                      /* ... passt aber nicht: wie gewöhnlich setzen */
    int widest_image;                   /* beim Ausmessen: breitestes Bild */

    element_t stack[STACK_MAX];
    int depth;
    int hidden, bold, pre, heading, quote, italic, mono;
    int link;                           /* laufender Link oder -1 */
    bool muted;
    int list_depth;
    int counter[LISTS_MAX];             /* > 0: nummerierte Liste, nächste Nummer */
    int form;
    int row_cells;
    int heading_level;                  /* 1 bis 6 beim Beginn einer Überschrift */

    /* Die Zeile, an der gerade gebaut wird */
    int column;
    bool has_content, space_pending;
    bool only_bullet;                   /* in der Zeile steht bisher nur der Listenpunkt */

    char word[WORD_MAX + 8];
    size_t word_len;

    /* Text, der nicht auf die Seite kommt, sondern zu einem Knopf oder Eintrag wird */
    bool capturing;
    char capture[CAPTURE_MAX];
    size_t capture_len;
    int select;                         /* Auswahlfeld, das gerade gefüllt wird, oder -1 */
    int button;
    char *option_value;
    bool option_selected, option_open;
} parser_t;

/* --- Speicher ---------------------------------------------------------------------------- */

static bool ensure(page_t *page, void **array, int count, int *cap, size_t item)
{
    if (count < *cap)
        return true;
    int bigger = *cap ? *cap * 2 : 32;
    void *grown = kmalloc((size_t)bigger * item);
    if (!grown) {
        page->failed = true;
        return false;
    }
    if (*cap)
        memcpy(grown, *array, (size_t)*cap * item);
    kfree(*array);
    *array = grown;
    *cap = bigger;
    return true;
}

#define ENSURE(page, array, count, cap) \
    ensure(page, (void **)&(array), count, &(cap), sizeof(*(array)))

static char *duplicate(const char *text, size_t len)
{
    char *copy = kmalloc(len + 1);
    if (copy) {
        memcpy(copy, text, len);
        copy[len] = '\0';
    }
    return copy;
}

bool widget_set_value(widget_t *w, const char *text, size_t len)
{
    char *copy = duplicate(text, len);
    if (!copy)
        return false;
    kfree(w->value);
    w->value = copy;
    return true;
}

void page_free(page_t *page)
{
    if (!page)
        return;
    for (int i = 0; i < page->link_count; i++)
        kfree(page->links[i]);
    for (int i = 0; i < page->widget_count; i++) {
        kfree(page->widgets[i].name);
        kfree(page->widgets[i].value);
        kfree(page->widgets[i].label);
    }
    for (int i = 0; i < page->option_count; i++) {
        kfree(page->options[i].label);
        kfree(page->options[i].value);
    }
    for (int i = 0; i < page->form_count; i++)
        kfree(page->forms[i].action);
    for (int i = 0; i < page->anchor_count; i++)
        kfree(page->anchors[i].id);
    for (int i = 0; i < page->image_count; i++) {
        kfree(page->images[i].url);
        kfree(page->images[i].alt);
    }
    kfree(page->images);
    kfree(page->title);
    kfree(page->pool);
    kfree(page->spans);
    kfree(page->lines);
    kfree(page->links);
    kfree(page->widgets);
    kfree(page->options);
    kfree(page->forms);
    kfree(page->anchors);
    kfree(page);
}

/* --- Zeichen ------------------------------------------------------------------------------- */

static char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

static bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

static bool is_alpha(char c)
{
    c = lower(c);
    return c >= 'a' && c <= 'z';
}

/* Vergleicht ein Stück der Seite mit einem Wort in Kleinbuchstaben. */
static bool same_word(const char *text, size_t len, const char *word)
{
    if (strlen(word) != len)
        return false;
    for (size_t i = 0; i < len; i++) {
        if (lower(text[i]) != word[i])
            return false;
    }
    return true;
}

static size_t put_codepoint(char *out, uint32_t cp)
{
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

/*
 * Wandelt eine Zeichenangabe wie "&amp;" oder "&#228;" um. p zeigt auf das '&';
 * liefert die Zahl der verbrauchten Bytes (0: kein gültiger Name, '&' steht für sich).
 */
static size_t decode_entity(const char *p, const char *end, uint32_t *codepoint)
{
    static const struct {
        const char *name;
        uint16_t codepoint;
    } names[] = {
        { "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' }, { "apos", '\'' },
        { "nbsp", ' ' }, { "auml", 0xE4 }, { "ouml", 0xF6 }, { "uuml", 0xFC },
        { "Auml", 0xC4 }, { "Ouml", 0xD6 }, { "Uuml", 0xDC }, { "szlig", 0xDF },
        { "euro", 0x20AC }, { "copy", 0xA9 }, { "reg", 0xAE }, { "laquo", 0xAB },
        { "raquo", 0xBB }, { "ndash", 0x2013 }, { "mdash", 0x2014 },
        { "hellip", 0x2026 }, { "bull", 0x2022 }, { "middot", 0xB7 },
        { "lsquo", 0x2018 }, { "rsquo", 0x2019 }, { "ldquo", 0x201C },
        { "rdquo", 0x201D }, { "bdquo", 0x201E }, { "times", 0xD7 },
        { "trade", 0x2122 }, { "deg", 0xB0 }, { "sect", 0xA7 }, { "eacute", 0xE9 },
        { "egrave", 0xE8 }, { "agrave", 0xE0 }, { "ccedil", 0xE7 }, { "rarr", 0x2192 },
        { "larr", 0x2190 }, { "shy", 0 }, { "lsaquo", '<' }, { "rsaquo", '>' },
        { "iexcl", 0xA1 }, { "pound", 0xA3 }, { "frac12", 0xBD }, { "aacute", 0xE1 },
        { "oacute", 0xF3 }, { "uacute", 0xFA }, { "ntilde", 0xF1 }, { "ensp", ' ' },
        { "emsp", ' ' }, { "thinsp", ' ' }, { "zwnj", 0 }, { "zwj", 0 },
    };
    size_t n = 1;

    if (p + 1 < end && p[1] == '#') {
        uint32_t value = 0;
        bool hex = p + 2 < end && lower(p[2]) == 'x';
        n = hex ? 3 : 2;
        size_t digits = 0;
        for (; p + n < end && digits < 8; n++, digits++) {
            char c = lower(p[n]);
            if (c >= '0' && c <= '9')
                value = value * (hex ? 16 : 10) + (uint32_t)(c - '0');
            else if (hex && c >= 'a' && c <= 'f')
                value = value * 16 + (uint32_t)(c - 'a' + 10);
            else
                break;
        }
        if (digits == 0)
            return 0;
        if (p + n < end && p[n] == ';')
            n++;
        *codepoint = (value == 0 || value > 0x10FFFF) ? '?' : value == 0xA0 ? ' ' : value;
        return n;
    }
    while (p + n < end && n < 10 && (is_alpha(p[n]) || (p[n] >= '0' && p[n] <= '9')))
        n++;
    if (p + n >= end || p[n] != ';')
        return 0;
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (strlen(names[i].name) == n - 1 && memcmp(p + 1, names[i].name, n - 1) == 0) {
            *codepoint = names[i].codepoint;
            return n + 1;
        }
    }
    return 0;
}

/* Kopiert Text und löst dabei Zeichenangaben auf; out fasst size Bytes. */
static size_t decode_text(const char *text, size_t len, char *out, size_t size)
{
    const char *end = text + len;
    size_t n = 0;
    while (text < end && n + 5 < size) {
        uint32_t cp;
        size_t used = *text == '&' ? decode_entity(text, end, &cp) : 0;
        if (used) {
            if (cp)
                n += put_codepoint(out + n, cp);
            text += used;
        } else {
            out[n++] = *text++;
        }
    }
    out[n] = '\0';
    return n;
}

static const attr_t *find_attr(const attr_t *attrs, int count, const char *name)
{
    for (int i = 0; i < count; i++) {
        if (same_word(attrs[i].name, attrs[i].name_len, name))
            return &attrs[i];
    }
    return NULL;
}

/* Wert eines Attributs als eigener Text (kfree), Zeichenangaben aufgelöst; NULL, wenn es fehlt. */
static char *attr_text(const attr_t *attrs, int count, const char *name)
{
    const attr_t *a = find_attr(attrs, count, name);
    if (!a)
        return NULL;
    char *out = kmalloc(a->value_len + 8);
    if (out)
        decode_text(a->value, a->value_len, out, a->value_len + 8);
    return out;
}

static bool attr_is(const attr_t *attrs, int count, const char *name, const char *word)
{
    const attr_t *a = find_attr(attrs, count, name);
    return a && same_word(a->value, a->value_len, word);
}

static int attr_number(const attr_t *attrs, int count, const char *name, int fallback)
{
    const attr_t *a = find_attr(attrs, count, name);
    int value = 0;
    if (!a || a->value_len == 0 || a->value_len > 6)
        return fallback;
    for (size_t i = 0; i < a->value_len; i++) {
        if (a->value[i] < '0' || a->value[i] > '9')
            return fallback;
        value = value * 10 + (a->value[i] - '0');
    }
    return value;
}

/* Enthält die Liste (durch Leerraum getrennte Wörter) dieses Wort? */
static bool has_token(const attr_t *a, const char *word)
{
    size_t n = strlen(word);
    for (size_t i = 0; a && i < a->value_len; ) {
        while (i < a->value_len && is_space(a->value[i]))
            i++;
        size_t start = i;
        while (i < a->value_len && !is_space(a->value[i]))
            i++;
        if (i - start == n && memcmp(a->value + start, word, n) == 0)
            return true;
    }
    return false;
}

/* Soll das Element unsichtbar sein? Ohne Stylesheets entscheiden ein paar Anzeichen. */
static bool wants_hidden(const attr_t *attrs, int count)
{
    static const char *const classes[] = {
        "hidden", "screen-reader-text", "hide-if-no-js", "sr-only", "visually-hidden",
        "d-none",
        /*
         * Aufklappmenüs: Ihr Inhalt ist in einem normalen Browser verborgen,
         * bis man sie anklickt. Ohne Stylesheets stünden sie in voller Länge
         * mitten im Text (bei Wikipedia über 300 Sprachen vor dem Artikel).
         */
        "dropdown-menu", "dropdown-content", "vector-dropdown-content",
        "vector-sticky-header", "vector-pinnable-header", NULL,
    };
    if (find_attr(attrs, count, "hidden") || attr_is(attrs, count, "aria-hidden", "true"))
        return true;

    const attr_t *class = find_attr(attrs, count, "class");
    /* Sprungmarken wie "Zum Inhalt springen" bleiben - sie sind hier nützlich. */
    if (has_token(class, "screen-reader-shortcut") || has_token(class, "skip-link"))
        return false;
    for (int i = 0; classes[i]; i++) {
        if (has_token(class, classes[i]))
            return true;
    }

    const attr_t *style = find_attr(attrs, count, "style");
    if (style) {                        /* "display: none" ohne Leerraum suchen */
        char compact[160];
        size_t n = 0;
        for (size_t i = 0; i < style->value_len && n + 1 < sizeof(compact); i++) {
            if (!is_space(style->value[i]))
                compact[n++] = lower(style->value[i]);
        }
        compact[n] = '\0';
        for (size_t i = 0; i + 12 <= n; i++) {
            if (memcmp(compact + i, "display:none", 12) == 0)
                return true;
        }
    }
    return false;
}

/* --- Zeilen bauen ---------------------------------------------------------------------------- */

static int indent(const parser_t *ps)
{
    int n = (2 * ps->list_depth + 2 * ps->quote) * ps->unit;
    return n > ps->columns / 2 ? ps->columns / 2 : n;
}

/* Platz, den Kästen neben dem Text der laufenden Zeile an einer Seite wegnehmen */
static int float_inset(const parser_t *ps, int side)
{
    int line = ps->page->line_count - 1, inset = 0;
    if (ps->in_float > 0 || ps->table_depth > 0)
        return 0;
    for (int i = 0; i < ps->float_count; i++) {
        if (ps->floats[i].side == side && line >= ps->floats[i].start &&
            line < ps->floats[i].start + ps->floats[i].count &&
            ps->floats[i].width + FLOAT_GAP > inset)
            inset = ps->floats[i].width + FLOAT_GAP;
    }
    return inset;
}

/* Wo der Text der laufenden Zeile beginnt und wo er spätestens endet */
static int line_left(const parser_t *ps)
{
    return indent(ps) + float_inset(ps, CSS_SIDE_LEFT);
}

static int line_right(const parser_t *ps)
{
    int right = ps->columns - float_inset(ps, CSS_SIDE_RIGHT);
    int least = line_left(ps) + 8 * ps->unit;
    return right > least ? right : least;
}

static bool raw_line(parser_t *ps)
{
    page_t *page = ps->page;
    if (page->line_count >= LINES_MAX ||
        !ENSURE(page, page->lines, page->line_count, page->line_cap)) {
        page->failed = true;
        return false;
    }
    line_t *line = &page->lines[page->line_count++];
    memset(line, 0, sizeof(*line));
    line->first = (uint32_t)page->span_count;
    line->image = -1;
    line->background = ps->cur.background;
    line->align = ps->cur.align;
    return true;
}

/*
 * Hängt an die fertige Zeile, was Kästen neben dem Text dort stehen haben:
 * ihre Stücke (verschoben an den Platz des Kastens), Bilder als Bildstücke.
 */
static void attach_floats(parser_t *ps)
{
    page_t *page = ps->page;
    if (ps->in_float > 0 || ps->table_depth > 0 || ps->float_count == 0)
        return;
    int target = page->line_count - 1;
    line_t *line = &page->lines[target];

    for (int i = 0; i < ps->float_count; i++) {
        int row = target - ps->floats[i].start;
        if (row < 0 || row >= ps->floats[i].count)
            continue;
        const line_t *source = &ps->floats[i].lines[row];
        int x = ps->floats[i].x;

        if (source->background && ENSURE(page, page->spans, page->span_count, page->span_cap)) {
            span_t *box = &page->spans[page->span_count++];     /* farbiger Grund des Kastens */
            memset(box, 0, sizeof(*box));
            box->column = (uint16_t)x;
            box->width = (uint16_t)ps->floats[i].width;
            box->background = source->background;
            box->style = STYLE_ASIDE;
            box->link = box->widget = box->image = -1;
            line->count++;
        }
        if (source->image >= 0 && source->image < page->image_count) {
            page_image_t *image = &page->images[source->image];
            if (!ENSURE(page, page->spans, page->span_count, page->span_cap))
                continue;
            span_t *piece = &page->spans[page->span_count++];
            memset(piece, 0, sizeof(*piece));
            if (source->image_row == 0) {
                image->column += x;
                image->line = target;
            }
            piece->column = (uint16_t)image->column;
            piece->width = (uint16_t)(image->width > 0 ? image->width : 60);
            piece->style = STYLE_ASIDE;
            piece->link = (int16_t)image->link;
            piece->widget = -1;
            piece->image = source->image;
            piece->image_row = source->image_row;
            line->count++;
        }
        for (uint32_t k = 0; k < source->count; k++) {
            if (!ENSURE(page, page->spans, page->span_count, page->span_cap))
                break;
            span_t *piece = &page->spans[page->span_count++];
            *piece = ps->floats[i].spans[source->first + k];
            piece->column = (uint16_t)(piece->column + x);
            piece->style |= STYLE_ASIDE;
            if (piece->image >= 0 && piece->image < page->image_count && piece->image_row == 0) {
                /* Bild aus einer Tabelle im Kasten: wandert mit */
                page->images[piece->image].column += x;
                page->images[piece->image].line = target;
            }
            if (piece->widget >= 0 && piece->widget < page->widget_count) {
                page->widgets[piece->widget].line = target;
                page->widgets[piece->widget].column += x;
            }
            line->count++;
        }
    }

    /* Kästen, deren letzte Zeile jetzt steht, sind erledigt */
    int kept = 0;
    for (int i = 0; i < ps->float_count; i++) {
        if (target >= ps->floats[i].start + ps->floats[i].count - 1) {
            kfree(ps->floats[i].lines);
            kfree(ps->floats[i].spans);
        } else {
            ps->floats[kept++] = ps->floats[i];
        }
    }
    ps->float_count = kept;
}

/* Rückt eine fertige Zeile in die Mitte oder nach rechts, wenn die Gestaltung das will. */
static void finish_line(parser_t *ps)
{
    page_t *page = ps->page;
    if (!ps->text || page->line_count == 0)
        return;
    line_t *line = &page->lines[page->line_count - 1];
    if (line->count > 0 && line->align >= CSS_CENTER && line->image < 0) {
        span_t *spans = &page->spans[line->first];
        int room = line_right(ps) -
                   (spans[line->count - 1].column + spans[line->count - 1].width);
        int shift = room <= 0 ? 0 : line->align == CSS_CENTER ? room / 2 : room;
        for (uint32_t i = 0; shift && i < line->count; i++) {
            spans[i].column = (uint16_t)(spans[i].column + shift);
            if (spans[i].widget >= 0 && spans[i].widget < page->widget_count)
                page->widgets[spans[i].widget].column += shift;
        }
        line->align = 0;                /* erledigt */
    }
    attach_floats(ps);
}

/* Eine noch leere Zeile übernimmt Hintergrund und Ausrichtung dessen, was jetzt gilt. */
static void sync_line(parser_t *ps)
{
    page_t *page = ps->page;
    if (!ps->text || page->line_count == 0 || ps->has_content)
        return;
    line_t *line = &page->lines[page->line_count - 1];
    if (line->image < 0 && !line->filler) {
        line->background = ps->cur.background;
        line->align = ps->cur.align;
    }
}

static void push_line(parser_t *ps)
{
    page_t *page = ps->page;
    finish_line(ps);
    /* Große Schrift braucht den Platz von zwei Zeilen. */
    if (page->line_count > 0 && page->lines[page->line_count - 1].tall) {
        uint32_t background = page->lines[page->line_count - 1].background;
        if (!raw_line(ps))
            return;
        page->lines[page->line_count - 1].filler = 1;
        page->lines[page->line_count - 1].background = background;
        attach_floats(ps);              /* auch neben der unteren Hälfte kann ein Kasten stehen */
    }
    if (!raw_line(ps))
        return;
    ps->has_content = false;
    ps->space_pending = false;
    ps->column = 0;
}

static void flush_word(parser_t *ps);

/* Beendet die laufende Zeile; forced: auch dann, wenn sie leer ist. */
static void new_line(parser_t *ps, bool forced)
{
    flush_word(ps);
    /* Nach einem Listenpunkt soll dessen Text in derselben Zeile folgen. */
    if (ps->only_bullet && !forced)
        return;
    if (ps->has_content || forced)
        push_line(ps);
    ps->space_pending = false;
    ps->only_bullet = false;
}

/* Sorgt für eine Leerzeile vor dem, was kommt (aber nie für zwei). */
static void paragraph(parser_t *ps)
{
    page_t *page = ps->page;
    if (ps->only_bullet) {
        flush_word(ps);
        return;
    }
    new_line(ps, false);
    /* Am Anfang einer Tabellenzelle steht nichts darüber, wovon Abstand zu halten wäre. */
    if (ps->table_depth > 0 && page->line_count - 1 <= ps->cell_line)
        return;
    if (page->line_count >= 2 && page->lines[page->line_count - 2].count > 0)
        push_line(ps);
}

/*
 * Lässt Platz, bis die Kästen neben dem Text an den genannten Seiten zu Ende
 * sind: Was jetzt kommt, soll darunter beginnen.
 */
static void clear_floats(parser_t *ps, int sides)
{
    page_t *page = ps->page;
    if (ps->float_count == 0 || ps->in_float > 0 || ps->table_depth > 0)
        return;
    new_line(ps, false);
    int until = 0;
    for (int i = 0; i < ps->float_count; i++) {
        if ((ps->floats[i].side & sides) && ps->floats[i].start + ps->floats[i].count > until)
            until = ps->floats[i].start + ps->floats[i].count;
    }
    while (page->line_count - 1 < until && !page->failed)
        push_line(ps);
}

static uint8_t current_style(const parser_t *ps)
{
    bool link = ps->link >= 0;
    bool bold = ps->cur.bold >= 0 ? ps->cur.bold : (ps->bold > 0 || ps->heading > 0);
    bool italic = ps->cur.italic >= 0 ? ps->cur.italic : ps->italic > 0;
    bool underline = ps->cur.underline >= 0 ? ps->cur.underline : link;
    return (uint8_t)((bold ? STYLE_BOLD : 0) | (ps->heading > 0 ? STYLE_HEADING : 0) |
                     (link ? STYLE_LINK : 0) | (ps->muted ? STYLE_MUTED : 0) |
                     (italic ? STYLE_ITALIC : 0) | (underline ? STYLE_UNDERLINE : 0) |
                     (ps->mono > 0 || ps->pre > 0 ? STYLE_MONO : 0));
}

/* Schriftgröße in Pixeln: laut Gestaltung, sonst nach Überschriftsstufe; 0 im Zeichenraster */
static uint8_t current_size(const parser_t *ps)
{
    if (!ps->text)
        return 0;
    int size = ps->text->base_size;
    if (ps->cur.size)
        size = ps->cur.size;
    else if (ps->heading > 0 && ps->heading_level >= 1 && ps->heading_level <= 3)
        size = ps->text->heading_size[ps->heading_level - 1];
    /* In Tabellenzellen und Kästen neben dem Text stehen Zeilen nebeneinander: einfache Höhe */
    if ((ps->table_depth > 0 || ps->in_float > 0) && size > 18)
        size = 18;
    return (uint8_t)size;
}

static int brightness(uint32_t rgb)
{
    return (int)(((rgb >> 16) & 0xFF) * 299 + ((rgb >> 8) & 0xFF) * 587 + (rgb & 0xFF) * 114) / 1000;
}

/*
 * Textfarbe für das nächste Stück: die der Seite - außer sie wäre auf dem
 * Hintergrund kaum zu lesen. Das passiert, wenn eine Seite helle Schrift auf
 * ein Hintergrundbild setzt, das wir nicht zeigen, oder nur eines von beiden
 * festlegt. Dann gilt die übliche dunkle Schrift bzw. auf dunklem Grund helle.
 */
static uint32_t current_color(const parser_t *ps, uint8_t style)
{
    if (!ps->text)
        return 0;
    uint32_t behind = ps->cur.highlight ? ps->cur.highlight
                    : ps->cur.background ? ps->cur.background : ps->page_color;
    uint32_t usual = (style & STYLE_LINK) ? ps->text->link_color : ps->text->text_color;
    uint32_t wanted = ps->cur.color ? ps->cur.color : usual;
    int difference = brightness(wanted & 0xFFFFFF) - brightness(behind & 0xFFFFFF);
    if (difference < 0)
        difference = -difference;
    if (difference >= 70)
        return ps->cur.color;
    bool dark = brightness(behind & 0xFFFFFF) < 128;
    return HTML_COLOR_SET | (dark ? 0xF2F2F2 : (ps->text->text_color & 0xFFFFFF));
}

static int count_chars(const char *text, size_t len);

/* Breite eines Textstücks in der laufenden Darstellung: Zeichen oder Pixel */
static int measure(const parser_t *ps, const char *text, size_t len)
{
    if (!ps->text)
        return count_chars(text, len);
    return ps->text->width(ps->text->ctx, text, len, current_style(ps), current_size(ps));
}

/* Setzt Text (oder ein Formularfeld) in die laufende Zeile, bricht nötigenfalls um. */
static void place(parser_t *ps, const char *bytes, size_t n, int width, int widget)
{
    page_t *page = ps->page;
    if (ps->hidden > 0 || page->failed || page->line_count == 0)
        return;

    int space = ps->space_pending && ps->has_content ? measure(ps, " ", 1) : 0;
    bool spaced = space > 0;
    if (ps->has_content && ps->column + space + width > line_right(ps)) {
        push_line(ps);
        space = 0;
        spaced = false;
    }
    if (!ps->has_content) {
        sync_line(ps);
        ps->column = line_left(ps);
    }

    /* Text in den Vorrat der Seite */
    if (page->pool_len + n + 2 > page->pool_cap) {
        size_t cap = page->pool_cap ? page->pool_cap * 2 : 8192;
        while (cap < page->pool_len + n + 2)
            cap *= 2;
        char *grown = kmalloc(cap);
        if (!grown) {
            page->failed = true;
            return;
        }
        if (page->pool_len)
            memcpy(grown, page->pool, page->pool_len);
        kfree(page->pool);
        page->pool = grown;
        page->pool_cap = cap;
    }

    line_t *line = &page->lines[page->line_count - 1];
    uint8_t style = current_style(ps), size = current_size(ps);
    uint32_t color = current_color(ps, style);
    span_t *last = line->count ? &page->spans[page->span_count - 1] : NULL;
    bool merge = last && widget < 0 && last->widget < 0 && last->style == style &&
                 last->size == size && last->color == color &&
                 last->background == ps->cur.highlight &&
                 last->link == ps->link && last->text + last->bytes == page->pool_len &&
                 last->column + last->width == ps->column &&
                 last->bytes + n + 1 < 60000;
    if (merge) {
        if (spaced)
            page->pool[page->pool_len++] = ' ';
        memcpy(page->pool + page->pool_len, bytes, n);
        page->pool_len += n;
        last->bytes = (uint16_t)(last->bytes + (spaced ? 1 : 0) + n);
        last->width = (uint16_t)(last->width + space + width);
    } else {
        if (!ENSURE(page, page->spans, page->span_count, page->span_cap))
            return;
        span_t *span = &page->spans[page->span_count++];
        span->text = (uint32_t)page->pool_len;
        span->bytes = (uint16_t)n;
        span->column = (uint16_t)(ps->column + space);
        span->width = (uint16_t)width;
        span->style = style;
        span->size = size;
        span->color = color;
        span->background = ps->cur.highlight;
        span->link = (int16_t)ps->link;
        span->widget = (int16_t)widget;
        span->image = -1;
        span->image_row = 0;
        memcpy(page->pool + page->pool_len, bytes, n);
        page->pool_len += n;
        line->count++;
    }
    /* Große Schrift ist zwei Zeilen hoch. */
    if (ps->text && widget < 0 && size >= 19 && !(style & STYLE_MONO))
        line->tall = 1;
    ps->column += space + width;
    if (width > ps->widest_piece)
        ps->widest_piece = width;
    if (ps->column - indent(ps) > ps->widest_line)
        ps->widest_line = ps->column - indent(ps);
    ps->has_content = true;
    ps->space_pending = false;
    ps->only_bullet = false;
}

static int count_chars(const char *text, size_t len)
{
    int n = 0;
    for (size_t i = 0; i < len; i++) {
        if (((uint8_t)text[i] & 0xC0) != 0x80)
            n++;
    }
    return n;
}

/* Das angefangene Wort kommt in die Zeile. */
static void flush_word(parser_t *ps)
{
    if (ps->word_len == 0)
        return;
    ps->word[ps->word_len] = '\0';
    ps->word_len = 0;
    if (!ps->text)
        claude_plain_text(ps->word);    /* typografische Zeichen, die die Rasterschrift nicht hat */

    const char *text = ps->word;
    size_t len = strlen(text);
    int room = line_right(ps) - line_left(ps);
    if (measure(ps, text, len) <= room) {
        place(ps, text, len, measure(ps, text, len), -1);
        return;
    }
    while (len > 0) {                   /* überlange Wörter (Adressen) in Stücken */
        size_t n = 0;
        int width = 0;
        while (n < len) {
            size_t next = n + 1;
            while (next < len && ((uint8_t)text[next] & 0xC0) == 0x80)
                next++;
            int with = measure(ps, text, next);
            if (n > 0 && with > room)
                break;
            n = next;
            width = with;
        }
        place(ps, text, n, width, -1);
        text += n;
        len -= n;
    }
}

static void add_byte(parser_t *ps, char c)
{
    if (ps->capturing) {
        if (ps->capture_len + 1 < CAPTURE_MAX)
            ps->capture[ps->capture_len++] = c;
        return;
    }
    ps->word[ps->word_len++] = c;
    /* Nicht mitten in einem UTF-8-Zeichen trennen */
    if (ps->word_len >= WORD_MAX && ((uint8_t)c & 0xC0) != 0x80 && (uint8_t)c < 0xC0)
        flush_word(ps);
    else if (ps->word_len >= WORD_MAX + 4)
        flush_word(ps);
}

static void add_space(parser_t *ps)
{
    if (ps->capturing) {
        if (ps->capture_len > 0 && ps->capture[ps->capture_len - 1] != ' ' &&
            ps->capture_len + 1 < CAPTURE_MAX)
            ps->capture[ps->capture_len++] = ' ';
        return;
    }
    flush_word(ps);
    ps->space_pending = true;
}

/* Verarbeitet ein Stück Fließtext der Seite. */
static void text_run(parser_t *ps, const char *text, size_t len, bool entities)
{
    const char *end = text + len;
    while (text < end) {
        char c = *text;
        uint32_t cp;
        size_t used = (entities && c == '&') ? decode_entity(text, end, &cp) : 0;
        if (used) {
            text += used;
            if (cp == ' ') {
                add_space(ps);
            } else if (cp) {
                char bytes[4];
                size_t n = put_codepoint(bytes, cp);
                for (size_t i = 0; i < n; i++)
                    add_byte(ps, bytes[i]);
            }
            continue;
        }
        text++;
        if (ps->pre > 0 && !ps->capturing) {
            if (c == '\n') {
                new_line(ps, true);
            } else if (c == '\t') {
                flush_word(ps);
                place(ps, "    ", 4, measure(ps, "    ", 4), -1);
            } else if (c == ' ') {
                flush_word(ps);
                place(ps, " ", 1, measure(ps, " ", 1), -1);
            } else if (c != '\r') {
                add_byte(ps, c);
            }
        } else if (is_space(c)) {
            add_space(ps);
        } else if ((unsigned char)c >= 32) {
            add_byte(ps, c);
        }
    }
}

/* Setzt festen Text in gedämpfter Farbe. */
static void place_note(parser_t *ps, const char *text)
{
    bool muted = ps->muted;
    flush_word(ps);
    ps->muted = true;
    place(ps, text, strlen(text), measure(ps, text, strlen(text)), -1);
    ps->muted = muted;
}

/* --- Formulare -------------------------------------------------------------------------------- */

static int add_widget(parser_t *ps, widget_kind_t kind, const attr_t *attrs, int count)
{
    page_t *page = ps->page;
    if (page->widget_count >= 30000 ||
        !ENSURE(page, page->widgets, page->widget_count, page->widget_cap))
        return -1;
    widget_t *w = &page->widgets[page->widget_count];
    memset(w, 0, sizeof(*w));
    w->kind = kind;
    w->form = ps->form;
    w->name = attr_text(attrs, count, "name");
    w->value = attr_text(attrs, count, "value");
    w->checked = find_attr(attrs, count, "checked") != NULL;
    w->line = -1;
    /* Abgeschaltete Felder werden nicht gesendet - am einfachsten: ohne Namen */
    if (find_attr(attrs, count, "disabled")) {
        kfree(w->name);
        w->name = NULL;
    }
    return page->widget_count++;
}

/* Gibt dem Feld seinen Platz auf der Seite. */
static void show_widget(parser_t *ps, int index, int width)
{
    if (index < 0 || ps->hidden > 0)
        return;
    widget_t *w = &ps->page->widgets[index];
    int room = (line_right(ps) - line_left(ps)) / ps->unit;     /* Felder zählen in Rasterzeichen */
    flush_word(ps);
    w->width = width > room ? room : width;
    if (w->width < 3)
        w->width = 3;                   /* schmaler ist kein Feld mehr zu bedienen */
    ps->space_pending = ps->has_content;
    int link = ps->link;
    ps->link = -1;
    place(ps, "", 0, w->width * ps->unit, index);
    ps->link = link;
    w->line = ps->page->line_count - 1;
    w->column = ps->column - w->width * ps->unit;
    ps->space_pending = true;
}

static void start_input(parser_t *ps, const attr_t *attrs, int count)
{
    const attr_t *type = find_attr(attrs, count, "type");
    const char *t = type ? type->value : "text";
    size_t n = type ? type->value_len : 4;
    int index;

    if (same_word(t, n, "hidden")) {
        add_widget(ps, W_HIDDEN, attrs, count);
    } else if ((same_word(t, n, "checkbox") || same_word(t, n, "radio")) && ps->form < 0 &&
               !find_attr(attrs, count, "name")) {
        /* Ohne Namen und Formular: ein Schalter für die Gestaltung, kein Eingabefeld */
    } else if (same_word(t, n, "checkbox") || same_word(t, n, "radio")) {
        index = add_widget(ps, lower(t[0]) == 'c' ? W_CHECKBOX : W_RADIO, attrs, count);
        if (index >= 0 && !ps->page->widgets[index].value)
            ps->page->widgets[index].value = duplicate("on", 2);
        show_widget(ps, index, 3);
    } else if (same_word(t, n, "submit") || same_word(t, n, "image")) {
        index = add_widget(ps, W_SUBMIT, attrs, count);
        if (index < 0)
            return;
        widget_t *w = &ps->page->widgets[index];
        const char *label = w->value && w->value[0] ? w->value : "Senden";
        w->label = duplicate(label, strlen(label));
        if (w->label)
            claude_plain_text(w->label);
        show_widget(ps, index, count_chars(label, strlen(label)) + 4);
    } else if (same_word(t, n, "file")) {
        if (ps->hidden == 0)
            place_note(ps, "[Datei auswählen: hier nicht möglich]");
    } else if (same_word(t, n, "button") || same_word(t, n, "reset")) {
        /* Knöpfe, die nur mit Skripten etwas tun */
    } else {
        index = add_widget(ps, same_word(t, n, "password") ? W_PASSWORD : W_TEXT, attrs,
                           count);
        if (index < 0)
            return;
        if (!ps->page->widgets[index].value)
            ps->page->widgets[index].value = duplicate("", 0);
        int size = attr_number(attrs, count, "size", 24);
        show_widget(ps, index, (size < 6 ? 6 : size > 44 ? 44 : size) + 2);
    }
}

static void finish_option(parser_t *ps)
{
    page_t *page = ps->page;
    if (!ps->option_open)
        return;
    ps->option_open = false;
    ps->capturing = false;
    while (ps->capture_len > 0 && ps->capture[ps->capture_len - 1] == ' ')
        ps->capture_len--;
    ps->capture[ps->capture_len] = '\0';
    claude_plain_text(ps->capture);

    if (ps->select >= 0 && ENSURE(page, page->options, page->option_count, page->option_cap)) {
        widget_t *w = &page->widgets[ps->select];
        option_t *o = &page->options[page->option_count];
        o->label = duplicate(ps->capture, strlen(ps->capture));
        /* Ohne eigenes value-Attribut wird die Beschriftung gesendet. */
        o->value = ps->option_value ? ps->option_value
                                    : duplicate(ps->capture, strlen(ps->capture));
        ps->option_value = NULL;
        if (ps->option_selected)
            w->selected = w->option_count;
        w->option_count++;
        page->option_count++;
    }
    kfree(ps->option_value);
    ps->option_value = NULL;
    ps->capture_len = 0;
}

static void finish_select(parser_t *ps)
{
    page_t *page = ps->page;
    finish_option(ps);
    if (ps->select < 0)
        return;
    widget_t *w = &page->widgets[ps->select];
    int widest = 6;
    for (int i = 0; i < w->option_count; i++) {
        const char *label = page->options[w->first_option + i].label;
        int n = label ? count_chars(label, strlen(label)) : 0;
        if (n > widest)
            widest = n;
    }
    int index = ps->select;
    ps->select = -1;
    show_widget(ps, index, (widest > 36 ? 36 : widest) + 4);
}

static void finish_button(parser_t *ps)
{
    if (ps->button < 0)
        return;
    widget_t *w = &ps->page->widgets[ps->button];
    while (ps->capture_len > 0 && ps->capture[ps->capture_len - 1] == ' ')
        ps->capture_len--;
    ps->capture[ps->capture_len] = '\0';
    claude_plain_text(ps->capture);
    const char *label = ps->capture_len ? ps->capture
                      : w->value && w->value[0] ? w->value : "Senden";
    w->label = duplicate(label, strlen(label));
    int index = ps->button;
    ps->button = -1;
    ps->capturing = false;
    ps->capture_len = 0;
    show_widget(ps, index, count_chars(label, strlen(label)) + 4);
}

/* --- Elemente ---------------------------------------------------------------------------------- */

static void pop(parser_t *ps)
{
    element_t e = ps->stack[--ps->depth];

    /* Erst abschließen, was zum Element gehört - noch mit dessen Einstellungen */
    switch (e.tag) {
    case T_SELECT: finish_select(ps); break;
    case T_OPTION: finish_option(ps); break;
    case T_BUTTON: finish_button(ps); break;
    default: flush_word(ps); break;
    }

    ps->cur = e.saved;
    if (e.undo & U_HIDDEN)
        ps->hidden--;
    if (e.undo & U_BOLD)
        ps->bold--;
    if (e.undo & U_ITALIC)
        ps->italic--;
    if (e.undo & U_MONO)
        ps->mono--;
    if (e.undo & U_HEADING)
        ps->heading--;
    if (e.undo & U_PRE)
        ps->pre--;
    if (e.undo & U_LINK)
        ps->link = -1;
    if (e.undo & U_QUOTE)
        ps->quote--;
    if (e.undo & U_LIST) {
        new_line(ps, false);
        ps->list_depth--;
    }

    switch (e.tag) {
    case T_P: case T_HEADING: case T_TABLE: case T_PRE: case T_BLOCKQUOTE: case T_DL:
    case T_FORM:
        paragraph(ps);
        if (e.tag == T_FORM)
            ps->form = -1;
        break;
    case T_UL: case T_OL:
        if (ps->list_depth == 0)
            paragraph(ps);
        break;
    case T_BLOCK: case T_LI: case T_TR: case T_DT: case T_DD:
        if (e.undo & U_INLINE)
            ps->space_pending = true;   /* nebeneinander: nur ein Abstand */
        else
            new_line(ps, false);
        break;
    default:
        break;
    }
    if (e.undo & U_BREAK)
        new_line(ps, false);
    sync_line(ps);
}

/* Schließt das innerste offene Element dieser Art - aber nicht über eine Grenze hinweg. */
static void close_open(parser_t *ps, tag_t tag, tag_t tag2, tag_t stop, tag_t stop2)
{
    for (int i = ps->depth - 1; i >= ps->floor; i--) {
        tag_t t = ps->stack[i].tag;
        if (t == tag || t == tag2) {
            while (ps->depth > i)
                pop(ps);
            return;
        }
        if (t == stop || t == stop2)
            return;
    }
}

static tag_t lookup_tag(const char *name, size_t len)
{
    for (unsigned i = 0; i < sizeof(tag_names) / sizeof(tag_names[0]); i++) {
        if (same_word(name, len, tag_names[i].name))
            return tag_names[i].tag;
    }
    return T_UNKNOWN;
}

/* --- Bilder ------------------------------------------------------------------------------------ */

#define IMAGES_MAX 300

/* Die Adresse des Bildes: "src", bei nachladenden Seiten auch deren Ersatzangaben. */
static char *picture_address(parser_t *ps, const attr_t *attrs, int count)
{
    static const char *const names[] = { "src", "data-src", "data-lazy-src", "data-original" };
    char *full = kmalloc(URL_SIZE);
    if (!full)
        return NULL;
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        char *value = attr_text(attrs, count, names[i]);
        bool usable = value && value[0] && !same_word(value, strlen(value) < 5 ? strlen(value) : 5, "data:") &&
                      url_resolve(ps->base, value, full, URL_SIZE) &&
                      (memcmp(full, "http://", 7) == 0 || memcmp(full, "https://", 8) == 0);
        kfree(value);
        if (usable)
            return full;
    }
    kfree(full);
    return NULL;
}

/*
 * Plant ein Bild als eigenen Block ein. Liefert false, wenn stattdessen der
 * Ersatztext in den Lauftext soll (Bild nicht ladbar oder keine Adresse).
 */
static bool place_picture(parser_t *ps, const attr_t *attrs, int count)
{
    const html_pictures_t *pictures = ps->pictures;
    page_t *page = ps->page;
    if (page->failed || page->image_count >= IMAGES_MAX)
        return false;
    char *url = picture_address(ps, attrs, count);
    if (!url)
        return false;

    int hint_w = attr_number(attrs, count, "width", 0);
    int hint_h = attr_number(attrs, count, "height", 0);
    int natural_w = 0, natural_h = 0;
    picture_state_t state = pictures->lookup(pictures->ctx, url, &natural_w, &natural_h);
    bool ready = state == PICTURE_READY && natural_w > 0 && natural_h > 0;

    /* Zählpixel und Abstandhalter sind keine Bilder für den Leser */
    bool tiny = (hint_w > 0 && hint_w <= 2) || (hint_h > 0 && hint_h <= 2) ||
                (ready && (natural_w <= 2 || natural_h <= 2));
    if (tiny) {
        kfree(url);
        return true;
    }
    if (state == PICTURE_BROKEN) {
        kfree(url);
        return false;
    }

    /*
     * Seiten, die Bilder erst per Skript nachladen, nennen dasselbe Bild oft
     * gleich noch einmal für Browser ohne Skripte: nur einmal zeigen.
     */
    flush_word(ps);
    if (page->image_count > 0 && !ps->has_content) {
        const page_image_t *last = &page->images[page->image_count - 1];
        if (strcmp(last->url, url) == 0 && last->line + last->rows == page->line_count - 1) {
            kfree(url);
            return true;
        }
    }

    /* Gewünschte Größe: die Angabe der Seite, sonst die des Bildes selbst */
    int width = 0, height = 0;
    if (ready) {
        width = hint_w > 0 ? hint_w : natural_w;
        height = (int)((int64_t)width * natural_h / natural_w);
    } else if (hint_w > 0 && hint_h > 0) {
        width = hint_w;
        height = hint_h;
    }

    /* Ein Listenpunkt, hinter dem nur das Bild käme, entfällt. */
    flush_word(ps);
    if (ps->only_bullet && page->line_count > 0) {
        line_t *line = &page->lines[page->line_count - 1];
        page->span_count = (int)line->first;
        line->count = 0;
        ps->column = 0;
        ps->has_content = false;
        ps->only_bullet = false;
    }
    new_line(ps, false);

    int column = line_left(ps);
    int rows = 1;
    if (width > 0 && height > 0) {
        int available = (line_right(ps) - column) * pictures->cell_width;
        int tallest = pictures->max_rows * pictures->line_height;
        if (width > available) {
            height = (int)((int64_t)height * available / width);
            width = available;
        }
        if (height > tallest) {
            width = (int)((int64_t)width * tallest / height);
            height = tallest;
        }
        if (width < 1)
            width = 1;
        if (height < 1)
            height = 1;
        rows = (height + pictures->line_height - 1) / pictures->line_height;
    }

    if (!ENSURE(page, page->images, page->image_count, page->image_cap)) {
        page->failed = true;
        kfree(url);
        return true;
    }
    if (width > ps->widest_piece)
        ps->widest_piece = width;
    if (width > ps->widest_line)
        ps->widest_line = width;
    if (width > ps->widest_image)
        ps->widest_image = width;
    int index = page->image_count++;
    page_image_t *image = &page->images[index];
    memset(image, 0, sizeof(*image));
    image->url = url;
    image->alt = attr_text(attrs, count, "alt");
    if (image->alt) {
        if (!image->alt[0] || strlen(image->alt) > 120) {
            kfree(image->alt);
            image->alt = NULL;
        } else {
            if (!ps->text)
                claude_plain_text(image->alt);
        }
    }
    image->link = ps->link;
    image->line = page->line_count - 1;
    image->rows = rows;
    image->column = column;
    image->width = width;
    image->height = height;
    image->ready = ready;

    for (int row = 0; row < rows && !page->failed; row++) {
        line_t *line = &page->lines[page->line_count - 1];
        line->image = (int16_t)index;
        line->image_row = (uint16_t)row;
        line->tall = 0;
        push_line(ps);
    }
    return true;
}

static void remember_anchor(parser_t *ps, const attr_t *attrs, int count)
{
    page_t *page = ps->page;
    const attr_t *id = find_attr(attrs, count, "id");
    if (!id || id->value_len == 0 || id->value_len > 80 || page->anchor_count >= 4000 ||
        !ENSURE(page, page->anchors, page->anchor_count, page->anchor_cap))
        return;
    char *copy = duplicate(id->value, id->value_len);
    if (!copy)
        return;
    page->anchors[page->anchor_count].id = copy;
    /* Die Marke zeigt auf die Zeile, in der der nächste Inhalt landet. */
    page->anchors[page->anchor_count].line = page->line_count - 1;
    page->anchor_count++;
}

/* Was ein neues Element stillschweigend beendet */
static void close_implied(parser_t *ps, tag_t tag)
{
    switch (tag) {
    case T_P: case T_BLOCK: case T_UL: case T_OL: case T_TABLE: case T_HEADING:
    case T_PRE: case T_FORM: case T_BLOCKQUOTE: case T_HR: case T_DL:
        close_open(ps, T_P, T_P, T_TD, T_TH);
        break;
    case T_LI:
        close_open(ps, T_LI, T_LI, T_UL, T_OL);
        break;
    case T_TR:
        close_open(ps, T_TR, T_TR, T_TABLE, T_TABLE);
        break;
    case T_TD: case T_TH:
        close_open(ps, T_TD, T_TH, T_TR, T_TABLE);
        break;
    case T_OPTION:
        finish_option(ps);
        close_open(ps, T_OPTION, T_OPTION, T_SELECT, T_SELECT);
        break;
    case T_DT: case T_DD:
        close_open(ps, T_DT, T_DD, T_DL, T_DL);
        break;
    default:
        break;
    }
}

/* Elemente, die von sich aus im Textfluss stehen */
static bool inline_tag(tag_t tag)
{
    return tag == T_UNKNOWN || tag == T_A || tag == T_BOLD || tag == T_ITALIC ||
           tag == T_MONO || tag == T_TD || tag == T_TH;
}

/*
 * Überträgt, was Stylesheets und style-Attribut für ein Element festlegen,
 * in die geltende Gestaltung. Vorher fällt zurück, was das Element von sich
 * aus anders hält als seine Umgebung (ein Link ist blau, <b> ist fett ...).
 */
static void apply_style(parser_t *ps, tag_t tag, const css_style_t *cs)
{
    style_t *cur = &ps->cur;
    int around = current_size(ps);      /* Schriftgröße der Umgebung, für Angaben wie "120%" */

    if (tag == T_BOLD || tag == T_HEADING || tag == T_TH)
        cur->bold = -1;
    if (ps->table_depth > 0 && (tag == T_TD || tag == T_TH))
        cur->align = tag == T_TH ? CSS_CENTER : CSS_LEFT;   /* Kopfzellen stehen mittig */
    if (tag == T_ITALIC)
        cur->italic = -1;
    if (tag == T_A) {
        cur->color = 0;
        cur->underline = -1;
    }
    if (tag == T_HEADING) {
        cur->size = 0;
        around = ps->heading_level >= 1 && ps->heading_level <= 3
                     ? ps->text->heading_size[ps->heading_level - 1] : ps->text->base_size;
    }

    if (cs->has & CSS_COLOR)
        cur->color = HTML_COLOR_SET | cs->color;
    if (cs->has & CSS_WEIGHT)
        cur->bold = cs->bold;
    if (cs->has & CSS_SLANT)
        cur->italic = cs->italic;
    if (cs->has & CSS_UNDERLINE)
        cur->underline = cs->underline;
    if (cs->has & CSS_ALIGN)
        cur->align = cs->align;
    if (cs->has & CSS_LIST)
        cur->list_plain = cs->list_plain;
    if (cs->has & CSS_SIZE) {
        /* Seiten rechnen mit 16 Pixeln Grundgröße, wir mit base_size */
        int px = cs->size_px ? (cs->size_px * ps->text->base_size + 8) / 16
                             : (around * cs->size_percent + 50) / 100;
        cur->size = (uint8_t)(px < 10 ? 10 : px > 28 ? 28 : px);
    }
    if (cs->has & CSS_BACKGROUND) {
        uint32_t color = HTML_COLOR_SET | cs->background;
        bool page_level = same_word(ps->name, ps->name_len, "body") ||
                          same_word(ps->name, ps->name_len, "html");
        bool block = (cs->has & CSS_DISPLAY) ? cs->display == CSS_BLOCK : !inline_tag(tag);
        if (page_level) {
            ps->page->background = color;
            ps->page_color = color;
        } else if (block) {
            cur->background = color;
            cur->highlight = 0;
        } else {
            cur->highlight = color;
        }
    }
}

static void element_start(parser_t *ps, tag_t tag, const attr_t *attrs, int count,
                          bool css_hidden, int display);

static void start_tag(parser_t *ps, tag_t tag, const attr_t *attrs, int count)
{
    close_implied(ps, tag);

    style_t saved = ps->cur;
    css_node_t node;
    bool css_hidden = false;
    int display = 0;
    memset(&node, 0, sizeof(node));
    if (ps->text) {
        const attr_t *id = find_attr(attrs, count, "id");
        const attr_t *class = find_attr(attrs, count, "class");
        const attr_t *style = find_attr(attrs, count, "style");
        node.tag = ps->name;
        node.tag_len = ps->name_len;
        if (id) {
            node.id = id->value;
            node.id_len = id->value_len;
        }
        if (class) {
            node.classes = class->value;
            node.classes_len = class->value_len;
        }
        node.parent = ps->depth > 0 ? &ps->stack[ps->depth - 1].node : NULL;

        css_style_t cs;
        css_match(ps->text->css, &node, style ? style->value : NULL,
                  style ? style->value_len : 0, &cs);
        css_hidden = ((cs.has & CSS_DISPLAY) && cs.display == CSS_NONE) ||
                     ((cs.has & CSS_VISIBILITY) && cs.hidden);
        if (cs.has & CSS_DISPLAY)
            display = cs.display;

        /* Erst unterhalb von Kästen neben dem Text beginnen? */
        if ((cs.has & CSS_CLEAR) && cs.clear && ps->hidden == 0 && !css_hidden)
            clear_floats(ps, cs.clear);

        /*
         * Gehört das Element selbst neben den Text (float, oder align= bei
         * Bildern und Tabellen)? Dann setzt es der Aufrufer als Kasten; hier
         * geschieht nichts weiter.
         */
        int side = (cs.has & CSS_FLOAT) ? cs.float_side : 0;
        if (!(cs.has & CSS_FLOAT) && (tag == T_IMG || tag == T_TABLE)) {
            if (attr_is(attrs, count, "align", "right"))
                side = CSS_SIDE_RIGHT;
            else if (attr_is(attrs, count, "align", "left"))
                side = CSS_SIDE_LEFT;
        }
        bool can_float = tag == T_IMG || tag == T_TABLE || tag == T_BLOCK || tag == T_UNKNOWN ||
                         tag == T_A || tag == T_UL || tag == T_OL || tag == T_P;
        if (side && can_float && !ps->no_float && !css_hidden && ps->hidden == 0 &&
            ps->in_float == 0 && ps->table_depth == 0) {
            ps->pending_float = side;
            return;
        }
        apply_style(ps, tag, &cs);
    }

    int depth = ps->depth;
    element_start(ps, tag, attrs, count, css_hidden, display);
    if (ps->depth > depth) {
        ps->stack[ps->depth - 1].node = node;
        ps->stack[ps->depth - 1].saved = saved;
    } else {
        ps->cur = saved;                /* Element ohne Inhalt: gilt nicht weiter */
    }
    sync_line(ps);
}

static void element_start(parser_t *ps, tag_t tag, const attr_t *attrs, int count,
                          bool css_hidden, int display)
{
    page_t *page = ps->page;
    uint16_t undo = 0;

    if (tag == T_SKIP || css_hidden || wants_hidden(attrs, count)) {
        flush_word(ps);
        ps->hidden++;
        undo |= U_HIDDEN;
    }
    /*
     * Laut Gestaltung ein eigener Block, obwohl das Element sonst im Textfluss
     * steht. In Tabellenzellen gilt das nicht: Die stehen hier nebeneinander
     * in einer Zeile, und Umbrüche darin zerrissen die Zeile.
     */
    bool in_cell = false;
    for (int i = ps->depth - 1; i >= 0 && !in_cell; i--)
        in_cell = ps->stack[i].tag == T_TD || ps->stack[i].tag == T_TH;
    if (display == CSS_BLOCK && inline_tag(tag) && tag != T_TD && tag != T_TH && !in_cell) {
        new_line(ps, false);
        undo |= U_BREAK;
    }
    if (ps->hidden == 0)
        remember_anchor(ps, attrs, count);

    switch (tag) {
    case T_BR:
        if (ps->hidden == 0)
            new_line(ps, true);
        return;
    case T_HR:
        new_line(ps, false);
        if (ps->hidden == 0) {
            char rule[160];
            int dash = measure(ps, "-", 1);
            int n = (line_right(ps) - line_left(ps)) / (dash > 0 ? dash : 1);
            if (n > (int)sizeof(rule) - 1)
                n = sizeof(rule) - 1;
            memset(rule, '-', (size_t)n);
            rule[n] = '\0';
            place_note(ps, rule);
        }
        new_line(ps, false);
        if (undo & U_HIDDEN)
            ps->hidden--;
        return;
    case T_IMG: {
        if (ps->pictures && ps->hidden == 0 && place_picture(ps, attrs, count)) {
            if (undo & U_HIDDEN)
                ps->hidden--;
            return;
        }
        char *alt = attr_text(attrs, count, "alt");
        if (alt && alt[0] && strlen(alt) < 80) {
            char text[100];
            ksnprintf(text, sizeof(text), "[Bild: %s]", alt);
            claude_plain_text(text);
            text_run(ps, text, strlen(text), false);
        } else if (ps->link >= 0) {
            text_run(ps, "[Bild]", 6, false);
        }
        kfree(alt);
        if (undo & U_HIDDEN)
            ps->hidden--;
        return;
    }
    case T_INPUT:
        start_input(ps, attrs, count);
        if (undo & U_HIDDEN)
            ps->hidden--;
        return;
    case T_VOID:
        if (undo & U_HIDDEN)
            ps->hidden--;
        return;

    case T_P: case T_TABLE: case T_BLOCKQUOTE: case T_DL: case T_PRE:
        paragraph(ps);
        if (tag == T_BLOCKQUOTE) {
            ps->quote++;
            undo |= U_QUOTE;
        } else if (tag == T_PRE) {
            ps->pre++;
            undo |= U_PRE;
        }
        break;
    case T_HEADING:
        paragraph(ps);
        if (ps->hidden == 0 && page->heading_line < 0 && ps->heading_level == 1)
            page->heading_line = page->line_count - 1;
        ps->heading++;
        undo |= U_HEADING;
        break;
    case T_BLOCK: case T_DT: case T_DD:
        if (display == CSS_INLINE)
            undo |= U_INLINE;           /* etwa Menüpunkte nebeneinander */
        else
            new_line(ps, false);
        break;
    case T_UL: case T_OL:
        if (ps->list_depth == 0)
            paragraph(ps);
        else
            new_line(ps, false);
        if (ps->cur.list_plain)
            break;                      /* Liste ohne Punkte: auch ohne Einzug */
        if (ps->list_depth < LISTS_MAX)
            ps->counter[ps->list_depth] = tag == T_OL ? 1 : 0;
        ps->list_depth++;
        undo |= U_LIST;
        break;
    case T_LI: {
        if (display == CSS_INLINE) {    /* etwa Menüpunkte nebeneinander */
            flush_word(ps);
            ps->space_pending = true;
            undo |= U_INLINE;
            break;
        }
        new_line(ps, false);
        if (ps->cur.list_plain)
            break;                      /* Liste ohne Punkte */
        char bullet[12] = "-";
        if (ps->text)
            strlcpy(bullet, "\xE2\x80\xA2", sizeof(bullet));    /* runder Punkt */
        int level = ps->list_depth - 1;
        if (level >= 0 && level < LISTS_MAX && ps->counter[level] > 0)
            ksnprintf(bullet, sizeof(bullet), "%d.", ps->counter[level]++);
        /* Der Punkt steht links vom eingerückten Text. */
        if (ps->list_depth > 0) {
            ps->list_depth--;
            place_note(ps, bullet);
            ps->list_depth++;
            ps->space_pending = true;
            ps->only_bullet = ps->hidden == 0;
        }
        break;
    }
    case T_TR:
        if (ps->table_depth == 0)
            new_line(ps, false);
        ps->row_cells = 0;
        break;
    case T_TD: case T_TH:
        if (ps->table_depth == 0 && ps->row_cells++ > 0) {
            ps->space_pending = true;
            place_note(ps, "|");
            ps->space_pending = true;
        }
        if (tag == T_TH) {
            ps->bold++;
            undo |= U_BOLD;
        }
        break;
    case T_BOLD:
        ps->bold++;
        undo |= U_BOLD;
        break;
    case T_ITALIC:
        flush_word(ps);
        ps->italic++;
        undo |= U_ITALIC;
        break;
    case T_MONO:
        flush_word(ps);
        ps->mono++;
        undo |= U_MONO;
        break;
    case T_A: {
        char *href = attr_text(attrs, count, "href");
        flush_word(ps);
        if (href && ps->link < 0 && page->link_count < 30000 &&
            ENSURE(page, page->links, page->link_count, page->link_cap)) {
            char *target = NULL;
            if (href[0] == '#' && href[1]) {
                target = duplicate(href, strlen(href));
            } else {
                char *full = kmalloc(URL_SIZE);
                if (full && url_resolve(ps->base, href, full, URL_SIZE))
                    target = duplicate(full, strlen(full));
                kfree(full);
            }
            if (target) {
                page->links[page->link_count] = target;
                ps->link = page->link_count++;
                undo |= U_LINK;
            }
        }
        kfree(href);
        break;
    }
    case T_FORM: {
        paragraph(ps);
        if (!ENSURE(page, page->forms, page->form_count, page->form_cap))
            break;
        char *action = attr_text(attrs, count, "action");
        char *full = kmalloc(URL_SIZE);
        form_t *form = &page->forms[page->form_count];
        form->action = NULL;
        /* Ohne Angabe geht das Formular an die Adresse der Seite selbst. */
        if (full && url_resolve(ps->base, action ? action : "", full, URL_SIZE))
            form->action = duplicate(full, strlen(full));
        form->post = attr_is(attrs, count, "method", "post");
        kfree(full);
        kfree(action);
        ps->form = page->form_count++;
        break;
    }
    case T_SELECT:
        flush_word(ps);
        finish_select(ps);
        ps->select = add_widget(ps, W_SELECT, attrs, count);
        if (ps->select >= 0)
            page->widgets[ps->select].first_option = page->option_count;
        break;
    case T_OPTION:
        if (ps->select >= 0) {
            ps->option_value = attr_text(attrs, count, "value");
            ps->option_selected = find_attr(attrs, count, "selected") != NULL;
            ps->option_open = true;
            ps->capturing = true;
            ps->capture_len = 0;
        }
        break;
    case T_BUTTON:
        flush_word(ps);
        finish_button(ps);
        /*
         * Knöpfe ohne type senden das Formular ab; type="button" braucht
         * Skripte - ebenso jeder Knopf, der zu keinem Formular gehört.
         */
        if (!attr_is(attrs, count, "type", "button") &&
            !attr_is(attrs, count, "type", "reset") &&
            (ps->form >= 0 || find_attr(attrs, count, "form"))) {
            ps->button = add_widget(ps, W_SUBMIT, attrs, count);
            if (ps->button >= 0) {
                ps->capturing = true;
                ps->capture_len = 0;
            }
        } else if (!(undo & U_HIDDEN)) {
            ps->hidden++;               /* samt Beschriftung weglassen */
            undo |= U_HIDDEN;
        }
        break;
    default:
        break;
    }

    if (ps->depth < STACK_MAX) {
        ps->stack[ps->depth].tag = (uint8_t)tag;
        ps->stack[ps->depth].undo = undo;
        ps->depth++;
    } else if (undo & U_HIDDEN) {
        ps->hidden--;                   /* zu tief verschachtelt: nichts merken */
    }
}

static void end_tag(parser_t *ps, tag_t tag)
{
    if (tag == T_BR) {
        if (ps->hidden == 0)
            new_line(ps, true);
        return;
    }
    for (int i = ps->depth - 1; i >= ps->floor; i--) {
        if (ps->stack[i].tag == tag) {
            while (ps->depth > i)
                pop(ps);
            return;
        }
    }
    if (tag == T_P)
        paragraph(ps);                  /* ein einzelnes </p> wirkt wie ein Absatz */
}

/* --- Die Seite durchgehen ------------------------------------------------------------------------- */

/* Sucht "</name" ab p; liefert die Stelle oder end. */
static const char *find_close(const char *p, const char *end, const char *name)
{
    size_t n = strlen(name);
    for (; p + n + 2 <= end; p++) {
        if (p[0] == '<' && p[1] == '/' && same_word(p + 2, n, name))
            return p;
    }
    return end;
}

/* Liest die Attribute eines Elements (name, name=wert, name="wert") bis hinter das ">". */
static int read_attributes(const char **cursor, const char *end, attr_t *attrs, bool closing)
{
    const char *p = *cursor;
    int count = 0;
    while (p < end && *p != '>') {
        if (is_space(*p) || *p == '/') {
            p++;
            continue;
        }
        attr_t a = { p, "", 0, 0 };
        while (p < end && !is_space(*p) && *p != '=' && *p != '>' && *p != '/')
            p++;
        a.name_len = (size_t)(p - a.name);
        while (p < end && is_space(*p))
            p++;
        if (p < end && *p == '=') {
            p++;
            while (p < end && is_space(*p))
                p++;
            if (p < end && (*p == '"' || *p == '\'')) {
                char quote = *p++;
                a.value = p;
                while (p < end && *p != quote)
                    p++;
                a.value_len = (size_t)(p - a.value);
                p += p < end;
            } else {
                a.value = p;
                while (p < end && !is_space(*p) && *p != '>')
                    p++;
                a.value_len = (size_t)(p - a.value);
            }
        }
        if (a.name_len == 0) {
            p++;                        /* etwas Unverständliches: weiter */
        } else if (!closing && count < ATTRS_MAX) {
            attrs[count++] = a;
        }
    }
    p += p < end;                       /* das '>' */
    *cursor = p;
    return count;
}

void html_find_styles(const char *html, size_t len, const char *base_url,
                      html_style_found_t found, void *ctx)
{
    const char *p = html, *end = html + len;
    char *full = kmalloc(URL_SIZE);
    if (!full)
        return;

    while (p < end) {
        if (*p != '<') {
            p++;
            continue;
        }
        if (end - p >= 4 && memcmp(p, "<!--", 4) == 0) {
            p += 4;
            while (p + 3 <= end && memcmp(p, "-->", 3) != 0)
                p++;
            continue;
        }
        const char *name = p + 1;
        size_t name_len = 0;
        while (name + name_len < end && is_alpha(name[name_len]))
            name_len++;
        bool style = same_word(name, name_len, "style");
        bool link = same_word(name, name_len, "link");
        bool script = same_word(name, name_len, "script");
        if (!style && !link && !script) {
            p++;
            continue;
        }
        p = name + name_len;
        attr_t attrs[ATTRS_MAX];
        int count = read_attributes(&p, end, attrs, false);

        /* Für den Druck gedachte Stylesheets zählen nicht */
        const attr_t *media = find_attr(attrs, count, "media");
        bool for_screen = !media || media->value_len == 0 || has_token(media, "all") ||
                          has_token(media, "screen");
        if (script) {
            p = find_close(p, end, "script");
        } else if (style) {
            const char *stop = find_close(p, end, "style");
            if (for_screen && stop > p)
                found(ctx, false, p, (size_t)(stop - p));
            p = stop;
        } else {
            const attr_t *rel = find_attr(attrs, count, "rel");
            char *href = attr_text(attrs, count, "href");
            if (rel && has_token(rel, "stylesheet") && !has_token(rel, "alternate") &&
                for_screen && !find_attr(attrs, count, "disabled") && href && href[0] &&
                url_resolve(base_url, href, full, URL_SIZE))
                found(ctx, true, full, strlen(full));
            kfree(href);
        }
    }
    kfree(full);
}

static const char *layout_table(parser_t *ps, const char *p, const char *end);
static const char *layout_float(parser_t *ps, int side, tag_t tag, const char *tag_start,
                                const char *name, size_t name_len, const char *p,
                                const char *end);

/* Arbeitet ein Stück der Seite ab (die ganze Seite oder den Inhalt einer Tabellenzelle). */
static void parse_range(parser_t *ps, const char *html, size_t len)
{
    const char *p = html, *end = html + len;

    while (p < end && !ps->page->failed) {
        if (*p != '<') {
            const char *start = p;
            while (p < end && *p != '<')
                p++;
            text_run(ps, start, (size_t)(p - start), true);
            continue;
        }
        if (end - p >= 4 && memcmp(p, "<!--", 4) == 0) {
            p += 4;
            while (p + 3 <= end && memcmp(p, "-->", 3) != 0)
                p++;
            p = p + 3 <= end ? p + 3 : end;
            continue;
        }
        if (p + 1 < end && (p[1] == '!' || p[1] == '?')) {
            while (p < end && *p != '>')
                p++;
            p += p < end;
            continue;
        }
        bool closing = p + 1 < end && p[1] == '/';
        const char *tag_start = p;
        const char *name = p + (closing ? 2 : 1);
        if (name >= end || !is_alpha(*name)) {
            text_run(ps, p, 1, false);  /* ein '<', das nur Text ist */
            p++;
            continue;
        }

        size_t name_len = 0;
        while (name + name_len < end && !is_space(name[name_len]) &&
               name[name_len] != '>' && name[name_len] != '/')
            name_len++;
        tag_t tag = lookup_tag(name, name_len);
        if (tag == T_HEADING)
            ps->heading_level = name[1] - '0';
        p = name + name_len;

        ps->name = name;
        ps->name_len = name_len;
        attr_t attrs[ATTRS_MAX];
        int count = read_attributes(&p, end, attrs, closing);

        if (closing) {
            end_tag(ps, tag);
            continue;
        }

        /* Elemente, deren Inhalt kein HTML ist */
        if (tag == T_SCRIPT || tag == T_STYLE) {
            p = find_close(p, end, tag == T_SCRIPT ? "script" : "style");
            continue;
        }
        if (tag == T_TITLE) {
            const char *stop = find_close(p, end, "title");
            if (!ps->page->title) {
                size_t n = (size_t)(stop - p);
                char *title = kmalloc(n + 8);
                if (title) {
                    decode_text(p, n > 300 ? 300 : n, title, n + 8);
                    if (!ps->text)
                        claude_plain_text(title);
                    ps->page->title = title;
                }
            }
            p = stop;
            continue;
        }
        if (tag == T_TEXTAREA) {
            const char *stop = find_close(p, end, "textarea");
            int index = add_widget(ps, W_TEXTAREA, attrs, count);
            if (index >= 0) {
                widget_t *w = &ps->page->widgets[index];
                size_t n = (size_t)(stop - p);
                if (n > 0 && *p == '\n') {      /* ein Zeilenwechsel direkt am Anfang zählt nicht */
                    p++;
                    n--;
                }
                kfree(w->value);
                w->value = kmalloc(n + 8);
                if (w->value) {
                    size_t out = decode_text(p, n, w->value, n + 8);
                    /* Windows-Zeilenenden vereinheitlichen */
                    size_t k = 0;
                    for (size_t i = 0; i < out; i++) {
                        if (w->value[i] != '\r')
                            w->value[k++] = w->value[i];
                    }
                    w->value[k] = '\0';
                }
                int width = (line_right(ps) - line_left(ps)) / ps->unit - 2;
                show_widget(ps, index, width > 64 ? 64 : width);
            }
            p = stop;
            continue;
        }
        start_tag(ps, tag, attrs, count);

        /* Ein Element, das neben dem Text stehen soll: als Kasten für sich setzen */
        if (ps->pending_float) {
            int side = ps->pending_float;
            ps->pending_float = 0;
            const char *after = layout_float(ps, side, tag, tag_start, name, name_len, p, end);
            if (after) {
                p = after;
                continue;
            }
            ps->no_float = true;        /* passt nicht daneben: wie gewöhnlich */
            start_tag(ps, tag, attrs, count);
            ps->no_float = false;
        }

        /* In echter Schrift werden Tabellen als Ganzes gesetzt: Zellen in Spalten. */
        if (tag == T_TABLE && ps->text && ps->float_count > 0 && ps->in_float == 0)
            clear_floats(ps, CSS_SIDE_BOTH);    /* Tabellen stehen nicht neben Kästen */
        if (tag == T_TABLE && ps->text && ps->hidden == 0 && ps->table_depth < 3 &&
            ps->depth > 0 && ps->stack[ps->depth - 1].tag == T_TABLE) {
            const char *after = layout_table(ps, p, end);
            if (after) {
                p = after;
                end_tag(ps, T_TABLE);
            }
        }
    }
}

/* --- Tabellen --------------------------------------------------------------------------------- */

#define TABLE_CELLS_MAX   6000
#define TABLE_COLUMNS_MAX 40
#define TABLE_GAP         14            /* Abstand zwischen zwei Spalten in Pixeln */
#define TABLE_MEASURE_W   4000          /* "unendlich breit" beim Ausmessen */

typedef struct {
    const char *tag;                    /* das "<" des Zellen-Elements */
    const char *content, *content_end;
    int row;
    bool caption, hidden;
    int column, colspan, rowspan;
    int min, max;                       /* schmalste und breiteste sinnvolle Breite */
    int first_line, lines;              /* vorläufige Zeilen der Zelle */
    uint32_t background;
} cell_t;

typedef struct {
    const char *tag;                    /* das "<" von <tr>, oder NULL (Zeile ohne eigenes Element) */
    int first_cell, cell_count;
    uint32_t background;
} table_row_t;

typedef struct {
    cell_t *cells;
    table_row_t *rows;
    int cell_count, row_count, column_count;
    int widths[TABLE_COLUMNS_MAX];
    const char *end;                    /* Ende des Tabellen-Quelltexts */
} table_t;

/* Liest ein Element ab seinem "<" und öffnet es wie beim normalen Durchgang. */
static void open_element(parser_t *ps, const char *tag_start, const char *end, tag_t fallback)
{
    attr_t attrs[ATTRS_MAX];
    int count = 0;
    if (tag_start) {
        const char *name = tag_start + 1;
        size_t name_len = 0;
        while (name + name_len < end && is_alpha(name[name_len]))
            name_len++;
        const char *p = name + name_len;
        count = read_attributes(&p, end, attrs, false);
        ps->name = name;
        ps->name_len = name_len;
        fallback = lookup_tag(name, name_len);
        if (fallback == T_UNKNOWN)
            fallback = T_BLOCK;         /* <caption> und Ähnliches */
    } else {
        ps->name = "tr";
        ps->name_len = 2;
    }
    start_tag(ps, fallback, attrs, count);
}

/* Zerlegt den Quelltext einer Tabelle in Zeilen und Zellen (ohne sie zu setzen). */
static bool table_scan(table_t *t, const char *p, const char *end)
{
    int depth = 0, cell_capacity = 0, row_capacity = 0;
    cell_t *cell = NULL;                /* offene Zelle */
    bool row_open = false;

    t->end = end;
    while (p < end) {
        if (*p != '<') {
            p++;
            continue;
        }
        if (end - p >= 4 && memcmp(p, "<!--", 4) == 0) {
            p += 4;
            while (p + 3 <= end && memcmp(p, "-->", 3) != 0)
                p++;
            continue;
        }
        const char *tag_start = p;
        bool closing = p + 1 < end && p[1] == '/';
        const char *name = p + (closing ? 2 : 1);
        size_t name_len = 0;
        while (name + name_len < end && is_alpha(name[name_len]))
            name_len++;
        if (name_len == 0) {
            p++;
            continue;
        }
        bool is_table = same_word(name, name_len, "table");
        bool is_row = same_word(name, name_len, "tr");
        bool is_cell = same_word(name, name_len, "td") || same_word(name, name_len, "th");
        bool is_caption = same_word(name, name_len, "caption");
        if (!is_table && (depth > 0 || (!is_row && !is_cell && !is_caption))) {
            p = name + name_len;        /* Inhalt: hier nicht von Belang */
            continue;
        }
        attr_t attrs[ATTRS_MAX];
        p = name + name_len;
        int count = read_attributes(&p, end, attrs, closing);

        if (is_table) {
            if (!closing) {
                depth++;
            } else if (depth > 0) {
                depth--;
            } else {
                if (cell)
                    cell->content_end = tag_start;
                t->end = p;
                return true;
            }
            continue;
        }
        if (cell) {                     /* jede Zeile oder Zelle beendet die offene Zelle */
            cell->content_end = tag_start;
            cell = NULL;
        }
        if (is_row || (is_caption && !closing)) {
            row_open = false;
            if (closing)
                continue;
        }
        if (closing) {
            if (is_caption)
                row_open = false;
            continue;
        }
        if (!row_open) {
            if (t->row_count == row_capacity) {
                int capacity = row_capacity ? row_capacity * 2 : 32;
                table_row_t *grown = kmalloc((size_t)capacity * sizeof(table_row_t));
                if (!grown)
                    return false;
                if (t->rows)
                    memcpy(grown, t->rows, (size_t)t->row_count * sizeof(table_row_t));
                kfree(t->rows);
                t->rows = grown;
                row_capacity = capacity;
            }
            table_row_t *row = &t->rows[t->row_count++];
            memset(row, 0, sizeof(*row));
            row->tag = is_row ? tag_start : NULL;
            row->first_cell = t->cell_count;
            row_open = true;
            if (is_row)
                continue;
        }
        /* eine Zelle (oder die Überschrift der Tabelle als Zeile mit einer Zelle) */
        if (t->cell_count >= TABLE_CELLS_MAX)
            return false;
        if (t->cell_count == cell_capacity) {
            int capacity = cell_capacity ? cell_capacity * 2 : 64;
            cell_t *grown = kmalloc((size_t)capacity * sizeof(cell_t));
            if (!grown)
                return false;
            if (t->cells)
                memcpy(grown, t->cells, (size_t)t->cell_count * sizeof(cell_t));
            kfree(t->cells);
            t->cells = grown;
            cell_capacity = capacity;
        }
        cell = &t->cells[t->cell_count++];
        memset(cell, 0, sizeof(*cell));
        cell->tag = tag_start;
        cell->content = p;
        cell->content_end = end;
        cell->row = t->row_count - 1;
        cell->caption = is_caption;
        cell->colspan = attr_number(attrs, count, "colspan", 1);
        cell->rowspan = attr_number(attrs, count, "rowspan", 1);
        if (cell->colspan < 1 || cell->colspan > TABLE_COLUMNS_MAX)
            cell->colspan = 1;
        if (cell->rowspan < 1)
            cell->rowspan = 1;
        t->rows[t->row_count - 1].cell_count++;
    }
    return true;                        /* ohne Ende-Marke: bis zum Schluss */
}

/* Bereitet die Zeile für den Inhalt einer Zelle vor: eigene Breite, kein Einzug. */
static void cell_begin(parser_t *ps, int width)
{
    flush_word(ps);
    ps->columns = width;
    ps->list_depth = 0;
    ps->quote = 0;
    raw_line(ps);
    ps->column = 0;
    ps->has_content = false;
    ps->space_pending = false;
    ps->only_bullet = false;
    ps->widest_line = ps->widest_piece = 0;
    sync_line(ps);
}

/*
 * Setzt alle Zellen der Reihe nach in die Seite von ps. measure: nur
 * ausmessen (Zellen sehr breit); sonst mit den festgelegten Spaltenbreiten.
 */
static void table_pass(parser_t *ps, table_t *t, bool measure)
{
    const char *end = t->end;
    int table_floor = ps->floor, outer_columns = ps->columns;
    int outer_lists = ps->list_depth, outer_quote = ps->quote;

    for (int r = 0; r < t->row_count && !ps->page->failed; r++) {
        table_row_t *row = &t->rows[r];
        int row_depth = ps->depth;
        open_element(ps, row->tag, end, T_TR);
        row->background = ps->cur.background;
        bool row_hidden = ps->hidden > 0;

        for (int c = 0; c < row->cell_count; c++) {
            cell_t *cell = &t->cells[row->first_cell + c];
            int cell_depth = ps->depth;
            open_element(ps, cell->tag, end, T_TD);
            if (measure)
                cell->hidden = row_hidden || ps->hidden > 0;

            int width = TABLE_MEASURE_W;
            if (!measure) {
                width = -TABLE_GAP;
                for (int k = 0; k < cell->colspan && cell->column + k < t->column_count; k++)
                    width += t->widths[cell->column + k] + TABLE_GAP;
                if (cell->caption) {
                    width = -TABLE_GAP;
                    for (int k = 0; k < t->column_count; k++)
                        width += t->widths[k] + TABLE_GAP;
                    ps->cur.align = CSS_CENTER;
                }
                if (width < 8)
                    width = 8;
            }
            cell->background = ps->cur.highlight;
            cell_begin(ps, width);
            cell->first_line = ps->page->line_count - 1;
            int outer_cell_line = ps->cell_line;
            ps->cell_line = cell->first_line;
            ps->floor = ps->depth;
            if (!cell->hidden)
                parse_range(ps, cell->content, (size_t)(cell->content_end - cell->content));
            while (ps->depth > ps->floor)
                pop(ps);
            flush_word(ps);
            finish_line(ps);
            ps->floor = table_floor;
            ps->cell_line = outer_cell_line;

            if (measure) {
                cell->max = ps->widest_line;
                cell->min = ps->widest_piece;
            } else {
                /* die Zeilen der Zelle, ohne leere am Ende */
                int last = ps->page->line_count - 1;
                while (last >= cell->first_line && ps->page->lines[last].count == 0 &&
                       ps->page->lines[last].image < 0 && !ps->page->lines[last].filler)
                    last--;
                cell->lines = last - cell->first_line + 1;
            }
            while (ps->depth > cell_depth)
                pop(ps);
        }
        while (ps->depth > row_depth)
            pop(ps);
    }
    flush_word(ps);
    ps->columns = outer_columns;
    ps->list_depth = outer_lists;
    ps->quote = outer_quote;
}

/* Ordnet die Zellen Spalten zu (mit colspan und rowspan) und verteilt die Breite. */
static bool table_columns(table_t *t, int available)
{
    int busy[TABLE_COLUMNS_MAX] = { 0 };            /* von oben hereinragende Zellen */
    int min[TABLE_COLUMNS_MAX] = { 0 }, max[TABLE_COLUMNS_MAX] = { 0 };

    t->column_count = 0;
    for (int r = 0; r < t->row_count; r++) {
        table_row_t *row = &t->rows[r];
        int column = 0;
        for (int c = 0; c < row->cell_count; c++) {
            cell_t *cell = &t->cells[row->first_cell + c];
            if (cell->hidden || cell->caption) {
                cell->column = 0;
                continue;
            }
            while (column < TABLE_COLUMNS_MAX && busy[column] > 0)
                column++;
            if (column + cell->colspan > TABLE_COLUMNS_MAX)
                return false;           /* zu viele Spalten: lieber zeilenweise */
            cell->column = column;
            for (int k = 0; k < cell->colspan; k++)
                busy[column + k] = cell->rowspan;
            if (cell->colspan == 1) {
                if (cell->min > min[column]) min[column] = cell->min;
                if (cell->max > max[column]) max[column] = cell->max;
            }
            column += cell->colspan;
            if (column > t->column_count)
                t->column_count = column;
        }
        for (int k = 0; k < TABLE_COLUMNS_MAX; k++) {
            if (busy[k] > 0)
                busy[k]--;
        }
    }
    if (t->column_count == 0)
        return false;

    /* Zellen über mehrere Spalten: reicht deren Summe nicht, die letzte verbreitern */
    for (int i = 0; i < t->cell_count; i++) {
        const cell_t *cell = &t->cells[i];
        if (cell->hidden || cell->caption || cell->colspan == 1)
            continue;
        int have_min = (cell->colspan - 1) * TABLE_GAP, have_max = have_min;
        for (int k = 0; k < cell->colspan; k++) {
            have_min += min[cell->column + k];
            have_max += max[cell->column + k];
        }
        int last = cell->column + cell->colspan - 1;
        if (cell->min > have_min) min[last] += cell->min - have_min;
        if (cell->max > have_max) max[last] += cell->max - have_max;
    }

    int total_min = 0, total_max = 0;
    for (int k = 0; k < t->column_count; k++) {
        if (min[k] < 8) min[k] = 8;
        if (max[k] < min[k]) max[k] = min[k];
        total_min += min[k];
        total_max += max[k];
    }
    available -= (t->column_count - 1) * TABLE_GAP;
    if (available < t->column_count * 16)
        return false;                   /* kein Platz für Spalten */

    if (total_max <= available) {       /* alles passt ohne Umbruch */
        for (int k = 0; k < t->column_count; k++)
            t->widths[k] = max[k];
        return true;
    }
    if (total_min >= available) {
        /* selbst die längsten Wörter passen nicht: anteilig stauchen */
        for (int k = 0; k < t->column_count; k++) {
            t->widths[k] = (int)((int64_t)min[k] * available / total_min);
            if (t->widths[k] < 12)
                t->widths[k] = 12;
        }
        return true;
    }

    /*
     * Dazwischen: Erst bekommen schmale Spalten (Nummern, Preise, Datum) ihre
     * volle Breite, solange sie nicht mehr als ihren gleichmäßigen Anteil
     * verlangen - sie sollen nicht umbrechen. Den Rest teilen sich die
     * breiten Spalten nach Bedarf.
     */
    bool settled[TABLE_COLUMNS_MAX] = { false };
    int remaining = available, open = t->column_count;
    for (bool changed = true; changed && open > 0;) {
        changed = false;
        int open_min = 0;
        for (int k = 0; k < t->column_count; k++) {
            if (!settled[k])
                open_min += min[k];
        }
        for (int k = 0; k < t->column_count; k++) {
            /* nur, wenn danach den übrigen noch ihr Minimum bleibt */
            if (!settled[k] && (int64_t)max[k] * open <= remaining &&
                remaining - max[k] >= open_min - min[k]) {
                t->widths[k] = max[k];
                settled[k] = true;
                remaining -= max[k];
                open_min -= min[k];
                open--;
                changed = true;
            }
        }
    }
    int open_min = 0, open_max = 0;
    for (int k = 0; k < t->column_count; k++) {
        if (!settled[k]) {
            open_min += min[k];
            open_max += max[k];
        }
    }
    for (int k = 0; k < t->column_count; k++) {
        if (settled[k])
            continue;
        if (remaining <= open_min || open_max <= open_min) {
            t->widths[k] = open_min > 0 ? (int)((int64_t)min[k] * remaining / open_min) : 12;
            if (t->widths[k] < 12)
                t->widths[k] = 12;
        } else {
            int64_t spare = remaining - open_min;
            t->widths[k] = min[k] + (int)(spare * (max[k] - min[k]) / (open_max - open_min));
        }
    }
    return true;
}

/*
 * Setzt eine Tabelle: p steht hinter ihrem Start-Element. Liefert die Stelle
 * hinter der Tabelle - oder NULL, wenn sie zeilenweise gesetzt werden soll
 * (zu groß, zu viele Spalten, Speichermangel).
 */
static const char *layout_table(parser_t *ps, const char *p, const char *end)
{
    page_t *page = ps->page;
    table_t table;
    const char *result = NULL;
    parser_t *snapshot = NULL;
    page_t *scratch = NULL;
    line_t *old_lines = NULL;
    span_t *old_spans = NULL;
    int *line_map = NULL;

    memset(&table, 0, sizeof(table));
    if (!table_scan(&table, p, end) || table.cell_count == 0)
        goto done;

    /* 1. Ausmessen - auf einer Wegwerf-Seite, danach ist alles wie vorher */
    snapshot = kmalloc(sizeof(*snapshot));
    scratch = kzalloc(sizeof(*scratch));
    if (!snapshot || !scratch)
        goto done;
    flush_word(ps);
    memcpy(snapshot, ps, sizeof(*ps));
    scratch->heading_line = -1;
    scratch->pixels = true;
    ps->page = scratch;
    ps->table_depth++;
    raw_line(ps);
    table_pass(ps, &table, true);
    bool measured = !scratch->failed;
    if (ps->option_value != snapshot->option_value)
        kfree(ps->option_value);
    memcpy(ps, snapshot, sizeof(*ps));
    page_free(scratch);
    scratch = NULL;
    if (!measured)
        goto done;

    int left = indent(ps);
    if (!table_columns(&table, ps->columns - left))
        goto done;

    /* 2. Setzen: jede Zelle für sich, vorläufig hinter die bisherigen Zeilen */
    /*
     * Die laufende Zeile ist leer. Steht direkt darüber Inhalt, bleibt sie
     * als Abstand stehen; sonst wird sie zur ersten Zeile der Tabelle.
     */
    int line0 = page->line_count - 1;
    bool cell_start = ps->table_depth > 0 && line0 <= ps->cell_line;
    if (line0 > 0 && !cell_start &&
        (page->lines[line0 - 1].count > 0 || page->lines[line0 - 1].image >= 0))
        line0++;
    int span0 = page->span_count;
    int anchor0 = page->anchor_count;
    int outer_widest_line = ps->widest_line, outer_widest_piece = ps->widest_piece;
    ps->table_depth++;
    table_pass(ps, &table, false);
    ps->table_depth--;
    if (page->failed)
        goto done;

    /* 3. Zusammensetzen: die Zeilen der Zellen einer Tabellenzeile nebeneinander */
    int temp_lines = page->line_count - line0, temp_spans = page->span_count - span0;
    old_lines = kmalloc((size_t)(temp_lines > 0 ? temp_lines : 1) * sizeof(line_t));
    old_spans = kmalloc((size_t)(temp_spans > 0 ? temp_spans : 1) * sizeof(span_t));
    line_map = kmalloc((size_t)(temp_lines > 0 ? temp_lines : 1) * sizeof(int));
    if (!old_lines || !old_spans || !line_map) {
        page->failed = true;
        goto done;
    }
    if (temp_lines > 0)
        memcpy(old_lines, page->lines + line0, (size_t)temp_lines * sizeof(line_t));
    if (temp_spans > 0)
        memcpy(old_spans, page->spans + span0, (size_t)temp_spans * sizeof(span_t));
    for (int i = 0; i < temp_lines; i++)
        line_map[i] = -1;
    page->line_count = line0;
    page->span_count = span0;

    int starts[TABLE_COLUMNS_MAX], total_width = 0;
    for (int k = 0; k < table.column_count; k++) {
        starts[k] = left + total_width;
        total_width += table.widths[k] + TABLE_GAP;
    }
    total_width -= TABLE_GAP;

    for (int r = 0; r < table.row_count && !page->failed; r++) {
        const table_row_t *row = &table.rows[r];
        int height = 0;
        for (int c = 0; c < row->cell_count; c++) {
            if (table.cells[row->first_cell + c].lines > height)
                height = table.cells[row->first_cell + c].lines;
        }
        for (int j = 0; j < height && !page->failed; j++) {
            if (page->line_count >= LINES_MAX ||
                !ENSURE(page, page->lines, page->line_count, page->line_cap)) {
                page->failed = true;
                break;
            }
            int target = page->line_count++;
            line_t *line = &page->lines[target];
            memset(line, 0, sizeof(*line));
            line->first = (uint32_t)page->span_count;
            line->image = -1;
            line->background = row->background;

            for (int c = 0; c < row->cell_count && !page->failed; c++) {
                const cell_t *cell = &table.cells[row->first_cell + c];
                if (cell->hidden)
                    continue;
                int x = cell->caption ? left : starts[cell->column];
                int width = cell->caption ? total_width : -TABLE_GAP;
                for (int k = 0; !cell->caption && k < cell->colspan &&
                                cell->column + k < table.column_count; k++)
                    width += table.widths[cell->column + k] + TABLE_GAP;

                /* farbige Zellen: ein Kasten über die ganze Zellbreite, davor der Inhalt */
                if (cell->background) {
                    if (!ENSURE(page, page->spans, page->span_count, page->span_cap))
                        break;
                    span_t *box = &page->spans[page->span_count++];
                    memset(box, 0, sizeof(*box));
                    box->column = (uint16_t)(x > TABLE_GAP / 2 ? x - TABLE_GAP / 2 : 0);
                    box->width = (uint16_t)(width + TABLE_GAP);
                    box->background = cell->background;
                    box->link = box->widget = box->image = -1;
                    line->count++;
                }
                if (j >= cell->lines)
                    continue;
                int source_index = cell->first_line - line0 + j;
                const line_t *source = &old_lines[source_index];
                line_map[source_index] = target;
                if (source->image >= 0 && source->image < page->image_count) {
                    /* Bild in der Zelle: als Stück an seiner Stelle in der Zeile */
                    page_image_t *image = &page->images[source->image];
                    if (!ENSURE(page, page->spans, page->span_count, page->span_cap))
                        break;
                    span_t *piece = &page->spans[page->span_count++];
                    memset(piece, 0, sizeof(*piece));
                    if (source->image_row == 0) {
                        image->column += x;
                        image->line = target;
                    }
                    piece->column = (uint16_t)image->column;
                    piece->width = (uint16_t)(image->width > 0 ? image->width : 60);
                    piece->link = (int16_t)image->link;
                    piece->widget = -1;
                    piece->image = source->image;
                    piece->image_row = source->image_row;
                    line->count++;
                    continue;
                }
                for (uint32_t k = 0; k < source->count; k++) {
                    if (!ENSURE(page, page->spans, page->span_count, page->span_cap))
                        break;
                    span_t *piece = &page->spans[page->span_count++];
                    *piece = old_spans[source->first - (uint32_t)span0 + k];
                    piece->column = (uint16_t)(piece->column + x);
                    if (piece->widget >= 0 && piece->widget < page->widget_count) {
                        page->widgets[piece->widget].line = target;
                        page->widgets[piece->widget].column += x;
                    }
                    line->count++;
                }
            }
        }
    }

    /* Sprungmarken und die Hauptüberschrift zeigen jetzt auf die endgültigen Zeilen */
    for (int i = anchor0; i < page->anchor_count; i++) {
        int at = page->anchors[i].line - line0;
        page->anchors[i].line = at >= 0 && at < temp_lines && line_map[at] >= 0
                                    ? line_map[at] : page->line_count - 1;
    }
    if (page->heading_line >= line0) {
        int at = page->heading_line - line0;
        page->heading_line = at < temp_lines && line_map[at] >= 0 ? line_map[at] : line0;
    }

    /* Danach geht es in einer frischen Zeile weiter */
    raw_line(ps);
    ps->column = 0;
    ps->has_content = false;
    ps->space_pending = false;
    ps->only_bullet = false;
    ps->widest_line = outer_widest_line > total_width ? outer_widest_line : total_width;
    int narrowest = (table.column_count - 1) * TABLE_GAP;
    for (int i = 0; i < table.column_count; i++)
        narrowest += table.widths[i] < 60 ? table.widths[i] : 60;
    ps->widest_piece = outer_widest_piece > narrowest ? outer_widest_piece : narrowest;
    result = table.end;

done:
    kfree(snapshot);
    page_free(scratch);
    kfree(old_lines);
    kfree(old_spans);
    kfree(line_map);
    kfree(table.cells);
    kfree(table.rows);
    return result;
}

/* --- Kästen neben dem Text ---------------------------------------------------------------------- */

/* Gleicher Name, ohne Rücksicht auf Groß- und Kleinschreibung? */
static bool same_word_n(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (lower(a[i]) != lower(b[i]))
            return false;
    }
    return true;
}

/* Sucht das Ende eines Elements: die Stelle hinter seinem "</name>". NULL, wenn es fehlt. */
static const char *element_end(const char *name, size_t name_len, const char *p, const char *end)
{
    int depth = 0;
    while (p + name_len + 2 <= end) {
        if (*p != '<') {
            p++;
            continue;
        }
        if (end - p >= 4 && memcmp(p, "<!--", 4) == 0) {
            p += 4;
            while (p + 3 <= end && memcmp(p, "-->", 3) != 0)
                p++;
            continue;
        }
        bool closing = p[1] == '/';
        const char *word = p + (closing ? 2 : 1);
        bool same = word + name_len < end && same_word_n(word, name, name_len) &&
                    !is_alpha(word[name_len]) && !(word[name_len] >= '0' && word[name_len] <= '9');
        if (!same) {
            p++;
            continue;
        }
        p = word + name_len;
        while (p < end && *p != '>')
            p++;
        p += p < end;
        if (!closing) {
            depth++;
        } else if (depth == 0) {
            return p;
        } else {
            depth--;
        }
    }
    return NULL;
}

/* Setzt [start, stop) als eigenen Block der Breite width hinter die bisherigen Zeilen. */
static void float_content(parser_t *ps, const char *start, const char *stop, int width)
{
    int floor = ps->floor, depth = ps->depth;
    cell_begin(ps, width);
    ps->floor = ps->depth;
    parse_range(ps, start, (size_t)(stop - start));
    while (ps->depth > depth)
        pop(ps);
    flush_word(ps);
    finish_line(ps);
    ps->floor = floor;
}

/*
 * Setzt ein Element als Kasten neben den Text: p steht hinter seinem
 * Start-Element. Liefert die Stelle hinter dem Element - oder NULL, wenn es
 * wie gewöhnlich gesetzt werden soll (kein Platz, kein Ende, kein Speicher).
 */
static const char *layout_float(parser_t *ps, int side, tag_t tag, const char *tag_start,
                                const char *name, size_t name_len, const char *p,
                                const char *end)
{
    page_t *page = ps->page;
    const char *stop = tag == T_IMG ? p : element_end(name, name_len, p, end);
    if (!stop || ps->float_count >= FLOATS_MAX || page->failed)
        return NULL;

    /* Neben den Kasten muss noch Text passen: höchstens 45 % der Zeile */
    int room = line_right(ps) - line_left(ps);
    int widest = room * 45 / 100;
    if (room < 280 || widest < 80)
        return NULL;

    /* 1. Ausmessen - auf einer Wegwerf-Seite, danach ist alles wie vorher */
    parser_t *snapshot = kmalloc(sizeof(*snapshot));
    page_t *scratch = kzalloc(sizeof(*scratch));
    if (!snapshot || !scratch) {
        kfree(snapshot);
        kfree(scratch);
        return NULL;
    }
    flush_word(ps);
    memcpy(snapshot, ps, sizeof(*ps));
    scratch->heading_line = -1;
    scratch->pixels = true;
    ps->page = scratch;
    ps->in_float++;
    ps->widest_image = 0;
    raw_line(ps);
    float_content(ps, tag_start, stop, widest);
    int content = ps->widest_line, picture = ps->widest_image;
    bool measured = !scratch->failed;
    if (ps->option_value != snapshot->option_value)
        kfree(ps->option_value);
    memcpy(ps, snapshot, sizeof(*ps));
    kfree(snapshot);
    page_free(scratch);
    if (!measured || content <= 0)
        return NULL;

    /* So breit wie das Bild darin; ohne Bild so breit wie der Inhalt */
    int width = picture > 0 ? picture : content;
    if (width > widest)
        width = widest;
    if (width < 40)
        width = 40;

    /* 2. Setzen, vorläufig hinter die bisherigen Zeilen, und beiseite legen */
    int line_count = page->line_count, span_count = page->span_count;
    int anchor_count = page->anchor_count;
    int columns = ps->columns, column = ps->column, lists = ps->list_depth, quote = ps->quote;
    bool has_content = ps->has_content, space_pending = ps->space_pending;
    bool only_bullet = ps->only_bullet;
    int widest_line = ps->widest_line, widest_piece = ps->widest_piece;
    ps->in_float++;
    float_content(ps, tag_start, stop, width);
    ps->in_float--;
    ps->columns = columns;
    ps->list_depth = lists;
    ps->quote = quote;

    int last = page->line_count - 1;
    while (last >= line_count && page->lines[last].count == 0 && page->lines[last].image < 0)
        last--;
    int rows = last - line_count + 1;
    int pieces = page->span_count - span_count;
    line_t *lines = rows > 0 ? kmalloc((size_t)rows * sizeof(line_t)) : NULL;
    span_t *spans = rows > 0 ? kmalloc((size_t)(pieces > 0 ? pieces : 1) * sizeof(span_t)) : NULL;
    if (lines && spans) {
        memcpy(lines, page->lines + line_count, (size_t)rows * sizeof(line_t));
        if (pieces > 0)
            memcpy(spans, page->spans + span_count, (size_t)pieces * sizeof(span_t));
        for (int i = 0; i < rows; i++)
            lines[i].first -= (uint32_t)span_count;
    }
    page->line_count = line_count;      /* die laufende Zeile ist wieder die letzte */
    page->span_count = span_count;
    ps->column = column;
    ps->has_content = has_content;
    ps->space_pending = space_pending;
    ps->only_bullet = only_bullet;
    ps->widest_line = widest_line;
    ps->widest_piece = widest_piece;
    if (rows <= 0)
        return stop;                    /* leer (etwa ausgeblendet): fertig */
    if (!lines || !spans || page->failed) {
        kfree(lines);
        kfree(spans);
        page->failed = true;
        return stop;
    }

    /*
     * 3. Anmelden: Der Kasten beginnt in der laufenden Zeile - steht dort
     * schon Text, in der nächsten - und unter früheren Kästen derselben Seite.
     */
    int start = line_count - 1 + (has_content ? 1 : 0);
    for (int i = 0; i < ps->float_count; i++) {
        if (ps->floats[i].side == side && ps->floats[i].start + ps->floats[i].count > start)
            start = ps->floats[i].start + ps->floats[i].count;
    }
    int at = ps->float_count++;
    ps->floats[at].side = side;
    ps->floats[at].start = start;
    ps->floats[at].count = rows;
    ps->floats[at].width = width;
    ps->floats[at].x = side == CSS_SIDE_RIGHT ? ps->columns - width : indent(ps);
    ps->floats[at].lines = lines;
    ps->floats[at].spans = spans;
    for (int i = anchor_count; i < page->anchor_count; i++)
        page->anchors[i].line = start;

    /* Die laufende, noch leere Zeile beginnt jetzt neben dem Kasten */
    if (!ps->has_content)
        ps->column = line_left(ps);
    return stop;
}

static void parse(parser_t *ps, const char *html, size_t len)
{
    parse_range(ps, html, len);
    while (ps->depth > 0)
        pop(ps);
    new_line(ps, false);
    clear_floats(ps, CSS_SIDE_BOTH);    /* Kästen, die über das Ende hinausreichen */
    for (int i = 0; i < ps->float_count; i++) {
        kfree(ps->floats[i].lines);
        kfree(ps->floats[i].spans);
    }
    ps->float_count = 0;
}

static page_t *begin(parser_t *ps, const char *base, int columns)
{
    page_t *page = kzalloc(sizeof(*page));
    if (!page)
        return NULL;
    memset(ps, 0, sizeof(*ps));
    ps->page = page;
    ps->base = base;
    ps->columns = columns < 20 ? 20 : columns > 250 ? 250 : columns;
    ps->unit = 1;
    ps->cur.bold = ps->cur.italic = ps->cur.underline = -1;
    ps->link = -1;
    ps->form = -1;
    ps->select = -1;
    ps->button = -1;
    page->heading_line = -1;
    push_line(ps);
    return page;
}

page_t *html_layout_rich(const char *html, size_t len, const char *base_url, int columns,
                         const html_pictures_t *pictures, const html_text_t *text)
{
    parser_t *ps = kmalloc(sizeof(*ps));
    page_t *page = ps ? begin(ps, base_url, columns) : NULL;
    if (page) {
        if (text && text->width && text->cell_width > 0) {
            /* echte Schrift: alle Breiten in Pixeln */
            ps->text = text;
            ps->unit = text->cell_width;
            ps->page_color = HTML_COLOR_SET | (text->page_color & 0xFFFFFF);
            page->pixels = true;
            ps->columns = columns < 160 ? 160 : columns > 8000 ? 8000 : columns;
        }
        if (pictures && pictures->lookup && pictures->cell_width > 0 &&
            pictures->line_height > 0 && pictures->max_rows > 0)
            ps->pictures = pictures;
        parse(ps, html, len);
        flush_word(ps);
        finish_line(ps);
    }
    kfree(ps);
    return page;
}

page_t *html_layout_pictures(const char *html, size_t len, const char *base_url,
                             int columns, const html_pictures_t *pictures)
{
    return html_layout_rich(html, len, base_url, columns, pictures, NULL);
}

page_t *html_layout(const char *html, size_t len, const char *base_url, int columns)
{
    return html_layout_pictures(html, len, base_url, columns, NULL);
}

page_t *html_plain_text(const char *text, size_t len, int columns)
{
    parser_t *ps = kmalloc(sizeof(*ps));
    page_t *page = ps ? begin(ps, "", columns) : NULL;
    if (page) {
        ps->pre = 1;
        text_run(ps, text, len, false);
        new_line(ps, false);
    }
    kfree(ps);
    return page;
}
