#include "html.h"
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

typedef enum {
    T_UNKNOWN, T_A, T_BOLD, T_BLOCK, T_BLOCKQUOTE, T_BR, T_BUTTON, T_DD, T_DL, T_DT,
    T_FORM, T_HEADING, T_HR, T_IMG, T_INPUT, T_LI, T_OL, T_OPTION, T_P, T_PRE,
    T_SCRIPT, T_SELECT, T_SKIP, T_STYLE, T_TABLE, T_TD, T_TEXTAREA, T_TH, T_TITLE,
    T_TR, T_UL, T_VOID,
} tag_t;

static const struct {
    const char *name;
    tag_t tag;
} tag_names[] = {
    { "a", T_A }, { "b", T_BOLD }, { "strong", T_BOLD },
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
       U_QUOTE = 64, U_CAPTURE = 128 };

typedef struct {
    uint8_t tag;
    uint8_t undo;
} element_t;

typedef struct {
    page_t *page;
    const char *base;
    int columns;
    const html_pictures_t *pictures;    /* NULL: Bilder nur als Ersatztext */

    element_t stack[STACK_MAX];
    int depth;
    int hidden, bold, pre, heading, quote;
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
        "d-none", NULL,
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
    int n = 2 * ps->list_depth + 2 * ps->quote;
    return n > ps->columns / 2 ? ps->columns / 2 : n;
}

static void push_line(parser_t *ps)
{
    page_t *page = ps->page;
    if (page->line_count >= LINES_MAX ||
        !ENSURE(page, page->lines, page->line_count, page->line_cap)) {
        page->failed = true;
        return;
    }
    page->lines[page->line_count].first = (uint32_t)page->span_count;
    page->lines[page->line_count].count = 0;
    page->lines[page->line_count].image = -1;
    page->lines[page->line_count].image_row = 0;
    page->line_count++;
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
    if (page->line_count >= 2 && page->lines[page->line_count - 2].count > 0)
        push_line(ps);
}

static uint8_t current_style(const parser_t *ps)
{
    return (uint8_t)((ps->bold > 0 ? STYLE_BOLD : 0) |
                     (ps->heading > 0 ? STYLE_HEADING | STYLE_BOLD : 0) |
                     (ps->link >= 0 ? STYLE_LINK : 0) | (ps->muted ? STYLE_MUTED : 0));
}

/* Setzt Text (oder ein Formularfeld) in die laufende Zeile, bricht nötigenfalls um. */
static void place(parser_t *ps, const char *bytes, size_t n, int width, int widget)
{
    page_t *page = ps->page;
    if (ps->hidden > 0 || page->failed || page->line_count == 0)
        return;

    int space = ps->space_pending && ps->has_content ? 1 : 0;
    if (ps->has_content && ps->column + space + width > ps->columns) {
        push_line(ps);
        space = 0;
    }
    if (!ps->has_content)
        ps->column = indent(ps);

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
    uint8_t style = current_style(ps);
    span_t *last = line->count ? &page->spans[page->span_count - 1] : NULL;
    bool merge = last && widget < 0 && last->widget < 0 && last->style == style &&
                 last->link == ps->link && last->text + last->bytes == page->pool_len &&
                 last->column + last->width == ps->column &&
                 last->bytes + n + 1 < 60000;
    if (merge) {
        if (space)
            page->pool[page->pool_len++] = ' ';
        memcpy(page->pool + page->pool_len, bytes, n);
        page->pool_len += n;
        last->bytes = (uint16_t)(last->bytes + space + n);
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
        span->link = (int16_t)ps->link;
        span->widget = (int16_t)widget;
        memcpy(page->pool + page->pool_len, bytes, n);
        page->pool_len += n;
        line->count++;
    }
    ps->column += space + width;
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
    claude_plain_text(ps->word);        /* typografische Zeichen, die die Schrift nicht hat */

    const char *text = ps->word;
    size_t len = strlen(text);
    int room = ps->columns - indent(ps);
    while (len > 0) {                   /* überlange Wörter (Adressen) in Stücken */
        size_t n = 0;
        int width = 0;
        while (n < len && width < room) {
            n++;
            while (n < len && ((uint8_t)text[n] & 0xC0) == 0x80)
                n++;
            width++;
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
                place(ps, "    ", 4, 4, -1);
            } else if (c == ' ') {
                flush_word(ps);
                place(ps, " ", 1, 1, -1);
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
    place(ps, text, strlen(text), count_chars(text, strlen(text)), -1);
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
    int room = ps->columns - indent(ps);
    flush_word(ps);
    w->width = width > room ? room : width;
    ps->space_pending = ps->has_content;
    int link = ps->link;
    ps->link = -1;
    place(ps, "", 0, w->width, index);
    ps->link = link;
    w->line = ps->page->line_count - 1;
    w->column = ps->column - w->width;
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

    if (e.undo & U_HIDDEN)
        ps->hidden--;
    if (e.undo & U_BOLD)
        ps->bold--;
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
        new_line(ps, false);
        break;
    default:
        break;
    }
}

/* Schließt das innerste offene Element dieser Art - aber nicht über eine Grenze hinweg. */
static void close_open(parser_t *ps, tag_t tag, tag_t tag2, tag_t stop, tag_t stop2)
{
    for (int i = ps->depth - 1; i >= 0; i--) {
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

    int column = indent(ps);
    int rows = 1;
    if (width > 0 && height > 0) {
        int available = (ps->columns - column) * pictures->cell_width;
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

static void start_tag(parser_t *ps, tag_t tag, const attr_t *attrs, int count)
{
    page_t *page = ps->page;
    uint8_t undo = 0;

    /* Was ein neues Element stillschweigend beendet */
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

    if (tag == T_SKIP || wants_hidden(attrs, count)) {
        flush_word(ps);
        ps->hidden++;
        undo |= U_HIDDEN;
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
            int n = ps->columns - indent(ps);
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
    case T_BLOCK: case T_DT:
        new_line(ps, false);
        break;
    case T_DD:
        new_line(ps, false);
        break;
    case T_UL: case T_OL:
        if (ps->list_depth == 0)
            paragraph(ps);
        else
            new_line(ps, false);
        if (ps->list_depth < LISTS_MAX)
            ps->counter[ps->list_depth] = tag == T_OL ? 1 : 0;
        ps->list_depth++;
        undo |= U_LIST;
        break;
    case T_LI: {
        new_line(ps, false);
        char bullet[12] = "-";
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
        new_line(ps, false);
        ps->row_cells = 0;
        break;
    case T_TD: case T_TH:
        if (ps->row_cells++ > 0) {
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
        /* Knöpfe ohne type senden das Formular ab; type="button" braucht Skripte. */
        if (!attr_is(attrs, count, "type", "button") &&
            !attr_is(attrs, count, "type", "reset")) {
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
    for (int i = ps->depth - 1; i >= 0; i--) {
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

static void parse(parser_t *ps, const char *html, size_t len)
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

        /* Attribute: name, name=wert, name="wert", name='wert' */
        attr_t attrs[ATTRS_MAX];
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
                p++;                    /* etwas Unverständliches: weiter */
            } else if (!closing && count < ATTRS_MAX) {
                attrs[count++] = a;
            }
        }
        p += p < end;                   /* das '>' */

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
                int width = ps->columns - indent(ps) - 2;
                show_widget(ps, index, width > 64 ? 64 : width);
            }
            p = stop;
            continue;
        }
        start_tag(ps, tag, attrs, count);
    }

    while (ps->depth > 0)
        pop(ps);
    new_line(ps, false);
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
    ps->link = -1;
    ps->form = -1;
    ps->select = -1;
    ps->button = -1;
    page->heading_line = -1;
    push_line(ps);
    return page;
}

page_t *html_layout_pictures(const char *html, size_t len, const char *base_url,
                             int columns, const html_pictures_t *pictures)
{
    parser_t *ps = kmalloc(sizeof(*ps));
    page_t *page = ps ? begin(ps, base_url, columns) : NULL;
    if (page) {
        if (pictures && pictures->lookup && pictures->cell_width > 0 &&
            pictures->line_height > 0 && pictures->max_rows > 0)
            ps->pictures = pictures;
        parse(ps, html, len);
    }
    kfree(ps);
    return page;
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
