/* Stylesheets (CSS) lesen und auf Elemente anwenden - siehe css.h */
#include "css.h"
#include "memory.h"
#include "string.h"

#define RULES_MAX       60000
#define DECLS_MAX       200000
#define BUCKETS         4096            /* Zweierpotenz */
#define COMPOUNDS_MAX   8               /* Glieder einer Auswahl: "a b > c" hat drei */
#define SIMPLES_MAX     6               /* Bedingungen je Glied: "a.b.c#d" hat vier */
#define VARS_MAX        2048
#define VALUE_MAX       240

/* Eigenschaften, die wir auswerten */
enum { P_COLOR, P_BACKGROUND, P_BACKGROUND_COLOR, P_WEIGHT, P_STYLE, P_SIZE, P_ALIGN,
       P_DECORATION, P_DISPLAY, P_VISIBILITY, P_LIST, P_FLOAT, P_CLEAR, P_WIDTH, P_CLIP,
       P_CLIP_PATH, P_TOP, P_RIGHT, P_BOTTOM, P_LEFT, P_FLEX_DIRECTION, P_FLEX_WRAP, P_FLEX_FLOW,
       P_GRID_COLUMNS, P_GRID_TEMPLATE, P_FLEX, P_FLEX_BASIS, P_FLEX_GROW, P_JUSTIFY, P_GRID_AREAS,
       /* ab hier: nur für den Seitenaufbau mit Kastenmodell (apply_box) */
       P_POSITION, P_BOX_SIZING, P_WHITE_SPACE, P_VALIGN, P_TRANSFORM_TEXT, P_OVERFLOW, P_FAMILY,
       P_LINE_HEIGHT, P_FONT, P_COLLAPSE, P_SPACING, P_ALIGN_ITEMS, P_ALIGN_SELF, P_ALIGN_CONTENT,
       P_PLACE_ITEMS, P_PLACE_CONTENT, P_FLEX_SHRINK, P_ORDER, P_ZINDEX, P_GAP, P_ROW_GAP,
       P_COLUMN_GAP, P_GRID_COLUMN, P_MARGIN, P_MARGIN_TOP, P_MARGIN_RIGHT, P_MARGIN_BOTTOM,
       P_MARGIN_LEFT, P_MARGIN_BLOCK, P_MARGIN_INLINE, P_PADDING, P_PADDING_TOP, P_PADDING_RIGHT,
       P_PADDING_BOTTOM, P_PADDING_LEFT, P_PADDING_BLOCK, P_PADDING_INLINE, P_INSET, P_HEIGHT,
       P_MIN_WIDTH, P_MAX_WIDTH, P_MIN_HEIGHT, P_MAX_HEIGHT, P_INDENT, P_RADIUS, P_BORDER,
       P_BORDER_TOP, P_BORDER_RIGHT, P_BORDER_BOTTOM, P_BORDER_LEFT, P_BORDER_WIDTH, P_BORDER_STYLE,
       P_BORDER_COLOR, P_BORDER_TOP_WIDTH, P_BORDER_RIGHT_WIDTH, P_BORDER_BOTTOM_WIDTH,
       P_BORDER_LEFT_WIDTH, P_BORDER_TOP_STYLE, P_BORDER_RIGHT_STYLE, P_BORDER_BOTTOM_STYLE,
       P_BORDER_LEFT_STYLE, P_BORDER_TOP_COLOR, P_BORDER_RIGHT_COLOR, P_BORDER_BOTTOM_COLOR,
       P_BORDER_LEFT_COLOR, P_BREAK, P_WORD_BREAK, P_OPACITY, P_BACKGROUND_IMAGE, P_GRID_AREA,
       P_GRID_ROW, P_MASK, P_CAPTION_SIDE, P_CONTENT, P_BG_REPEAT, P_BG_POSITION, P_BG_SIZE, P_COUNT };

/* Die Einzelangaben für Kästen: Jede hat ihren eigenen Rang (Kurzschreibweisen setzen mehrere) */
enum { L_DISPLAY, L_POSITION, L_BOX_SIZING, L_WHITE_SPACE, L_VALIGN, L_TRANSFORM, L_OVERFLOW,
       L_WEIGHT, L_SLANT, L_FAMILY, L_LIST_TYPE, L_COLLAPSE, L_DIRECTION, L_WRAP, L_JUSTIFY,
       L_ALIGN_ITEMS, L_ALIGN_SELF, L_ALIGN_CONTENT, L_MARGIN, L_PADDING = L_MARGIN + 4,
       L_BORDER_WIDTH = L_PADDING + 4, L_BORDER_STYLE = L_BORDER_WIDTH + 4,
       L_BORDER_COLOR = L_BORDER_STYLE + 4, L_INSET = L_BORDER_COLOR + 4, L_WIDTH = L_INSET + 4,
       L_HEIGHT, L_MIN_WIDTH, L_MAX_WIDTH, L_MIN_HEIGHT, L_MAX_HEIGHT, L_FONT_SIZE, L_LINE_HEIGHT,
       L_INDENT, L_RADIUS, L_GAP_ROW, L_GAP_COLUMN, L_BASIS, L_SPACING, L_GROW, L_SHRINK, L_ZINDEX,
       L_ORDER, L_GRID, L_GRID_ITEM, L_BREAK, L_OPACITY, L_BG_IMAGE, L_AREAS, L_GRID_AREA,
       L_GRID_ROW, L_MASK, L_CAPTION, L_BG_REPEAT, L_BG_POSITION, L_BG_SIZE, L_COUNT };

static const struct {
    const char *name;
    uint8_t property;
} properties[] = {
    { "color", P_COLOR }, { "background", P_BACKGROUND },
    { "background-color", P_BACKGROUND_COLOR }, { "font-weight", P_WEIGHT },
    { "font-style", P_STYLE }, { "font-size", P_SIZE }, { "text-align", P_ALIGN },
    { "text-decoration", P_DECORATION }, { "text-decoration-line", P_DECORATION },
    { "display", P_DISPLAY }, { "visibility", P_VISIBILITY }, { "list-style", P_LIST },
    { "list-style-type", P_LIST }, { "float", P_FLOAT }, { "clear", P_CLEAR },
    { "width", P_WIDTH }, { "clip", P_CLIP }, { "clip-path", P_CLIP_PATH },
    { "top", P_TOP }, { "flex-direction", P_FLEX_DIRECTION }, { "flex-wrap", P_FLEX_WRAP },
    { "flex-flow", P_FLEX_FLOW }, { "grid-template-columns", P_GRID_COLUMNS },
    { "grid-template", P_GRID_TEMPLATE }, { "flex", P_FLEX }, { "flex-basis", P_FLEX_BASIS },
    { "flex-grow", P_FLEX_GROW }, { "justify-content", P_JUSTIFY },
    { "grid-template-areas", P_GRID_AREAS },
    { "right", P_RIGHT }, { "bottom", P_BOTTOM }, { "left", P_LEFT }, { "inset", P_INSET },
    { "position", P_POSITION }, { "box-sizing", P_BOX_SIZING }, { "white-space", P_WHITE_SPACE },
    { "vertical-align", P_VALIGN }, { "text-transform", P_TRANSFORM_TEXT },
    { "overflow", P_OVERFLOW }, { "overflow-x", P_OVERFLOW }, { "overflow-y", P_OVERFLOW },
    { "font-family", P_FAMILY }, { "line-height", P_LINE_HEIGHT }, { "font", P_FONT },
    { "border-collapse", P_COLLAPSE }, { "border-spacing", P_SPACING },
    { "align-items", P_ALIGN_ITEMS }, { "align-self", P_ALIGN_SELF },
    { "align-content", P_ALIGN_CONTENT }, { "place-items", P_PLACE_ITEMS },
    { "place-content", P_PLACE_CONTENT }, { "flex-shrink", P_FLEX_SHRINK }, { "order", P_ORDER },
    { "z-index", P_ZINDEX }, { "gap", P_GAP }, { "grid-gap", P_GAP }, { "row-gap", P_ROW_GAP },
    { "grid-row-gap", P_ROW_GAP }, { "column-gap", P_COLUMN_GAP },
    { "grid-column-gap", P_COLUMN_GAP }, { "grid-column", P_GRID_COLUMN },
    { "margin", P_MARGIN }, { "margin-top", P_MARGIN_TOP }, { "margin-right", P_MARGIN_RIGHT },
    { "margin-bottom", P_MARGIN_BOTTOM }, { "margin-left", P_MARGIN_LEFT },
    { "margin-block", P_MARGIN_BLOCK }, { "margin-inline", P_MARGIN_INLINE },
    { "margin-block-start", P_MARGIN_TOP }, { "margin-block-end", P_MARGIN_BOTTOM },
    { "margin-inline-start", P_MARGIN_LEFT }, { "margin-inline-end", P_MARGIN_RIGHT },
    { "padding", P_PADDING }, { "padding-top", P_PADDING_TOP }, { "padding-right", P_PADDING_RIGHT },
    { "padding-bottom", P_PADDING_BOTTOM }, { "padding-left", P_PADDING_LEFT },
    { "padding-block", P_PADDING_BLOCK }, { "padding-inline", P_PADDING_INLINE },
    { "padding-block-start", P_PADDING_TOP }, { "padding-block-end", P_PADDING_BOTTOM },
    { "padding-inline-start", P_PADDING_LEFT }, { "padding-inline-end", P_PADDING_RIGHT },
    { "height", P_HEIGHT }, { "min-width", P_MIN_WIDTH }, { "max-width", P_MAX_WIDTH },
    { "min-height", P_MIN_HEIGHT }, { "max-height", P_MAX_HEIGHT }, { "inline-size", P_WIDTH },
    { "block-size", P_HEIGHT }, { "max-inline-size", P_MAX_WIDTH }, { "min-inline-size", P_MIN_WIDTH },
    { "text-indent", P_INDENT }, { "border-radius", P_RADIUS }, { "border", P_BORDER },
    { "border-top", P_BORDER_TOP }, { "border-right", P_BORDER_RIGHT },
    { "border-bottom", P_BORDER_BOTTOM }, { "border-left", P_BORDER_LEFT },
    { "border-width", P_BORDER_WIDTH }, { "border-style", P_BORDER_STYLE },
    { "border-color", P_BORDER_COLOR }, { "border-top-width", P_BORDER_TOP_WIDTH },
    { "border-right-width", P_BORDER_RIGHT_WIDTH }, { "border-bottom-width", P_BORDER_BOTTOM_WIDTH },
    { "border-left-width", P_BORDER_LEFT_WIDTH }, { "border-top-style", P_BORDER_TOP_STYLE },
    { "border-right-style", P_BORDER_RIGHT_STYLE }, { "border-bottom-style", P_BORDER_BOTTOM_STYLE },
    { "border-left-style", P_BORDER_LEFT_STYLE }, { "border-top-color", P_BORDER_TOP_COLOR },
    { "border-right-color", P_BORDER_RIGHT_COLOR }, { "border-bottom-color", P_BORDER_BOTTOM_COLOR },
    { "border-left-color", P_BORDER_LEFT_COLOR }, { "overflow-wrap", P_BREAK }, { "word-wrap", P_BREAK },
    { "word-break", P_WORD_BREAK }, { "opacity", P_OPACITY }, { "background-image", P_BACKGROUND_IMAGE },
    { "grid-area", P_GRID_AREA }, { "grid-row", P_GRID_ROW }, { "mask", P_MASK },
    { "mask-image", P_MASK }, { "-webkit-mask", P_MASK }, { "-webkit-mask-image", P_MASK },
    { "mask-size", P_MASK }, { "-webkit-mask-size", P_MASK }, { "mask-position", P_MASK },
    { "-webkit-mask-position", P_MASK }, { "mask-repeat", P_MASK }, { "-webkit-mask-repeat", P_MASK },
    { "caption-side", P_CAPTION_SIDE }, { "content", P_CONTENT },
    { "background-repeat", P_BG_REPEAT }, { "background-position", P_BG_POSITION },
    { "background-size", P_BG_SIZE },
};

enum { S_TAG, S_CLASS, S_ID, S_ROOT, S_ATTR,
       S_NTH,                           /* :nth-child(an+b) und Verwandte: die wievielte Stelle unter den Geschwistern */
       S_EMPTY, S_CHECKED, S_DISABLED, S_ENABLED,
       S_LANG,                          /* :lang(de) */
       S_NOT, S_IS };                   /* :not(...) - keine der Möglichkeiten; :is(...), :where(...) - eine davon */

/* Vergleiche eines Attributs: vorhanden, gleich, Wort darin, Anfang bis "-", Anfang, Ende, enthält */
enum { A_EXISTS, A_EQUALS, A_WORD, A_DASH, A_PREFIX, A_SUFFIX, A_CONTAINS };

/* S_NTH: von hinten gezählt; nur Elemente desselben Namens zählen mit */
enum { NTH_LAST = 1, NTH_TYPE = 2 };

/* Wie ein Glied der Auswahl zum vorigen steht: darin, direkt darin, direkt danach, irgendwo danach */
enum { REL_INSIDE, REL_CHILD, REL_NEXT, REL_AFTER };

struct compound;

typedef struct {
    uint8_t kind;
    uint8_t op;                         /* S_ATTR: A_...; S_NTH: NTH_...; S_NOT, S_IS: Zahl der Möglichkeiten */
    bool fold;                          /* nur S_ATTR: Groß-/Kleinschreibung egal */
    uint16_t length, value_length;
    int16_t a, b;                       /* nur S_NTH: die Stellen a*n + b (n = 0, 1, 2 ...) */
    const char *name;
    union {
        const char *value;              /* nur S_ATTR */
        struct compound *inner;         /* S_NOT, S_IS: die Möglichkeiten, je ein Glied (kmalloc) */
    };
} simple_t;

typedef struct compound {
    uint8_t count;
    uint8_t relation;                   /* REL_...: wie es zum vorigen Glied steht ("a > b", "a + b") */
    simple_t simples[SIMPLES_MAX];
} compound_t;

typedef struct {
    compound_t *compounds;              /* von links nach rechts; das letzte meint das Element */
    uint8_t compound_count;
    uint32_t specificity;
    uint32_t first_decl, decl_count;
    int32_t next;                       /* nächste Regel im selben Fach */
    bool ua;                            /* eine Grundregel des Browsers, keine der Seite */
    uint8_t pseudo;                     /* gilt einem Scheinelement: 1 ::before, 2 ::after */
} rule_t;

typedef struct {
    uint8_t property;
    bool important;
    uint16_t length;
    const char *value;
} decl_t;

typedef struct chunk {
    struct chunk *next;
    char text[];
} chunk_t;

typedef struct {
    const char *name, *value;
    uint16_t name_length, value_length;
} var_t;

struct css_sheet {
    int viewport;
    int viewport_height;                /* 0: unbekannt */
    bool adding_ua;                     /* gerade werden die Grundregeln des Browsers eingelesen */
    int ua_rules;                       /* so viele Regeln sind Grundregeln */
    chunk_t *chunks;                    /* Kopien der Texte: Regeln zeigen hinein */
    rule_t *rules;
    int rule_count, rule_capacity;
    decl_t *decls;
    int decl_count, decl_capacity;
    var_t *vars;
    int var_count;
    int32_t buckets[BUCKETS];
    bool failed;
};

/* --- kleine Helfer ------------------------------------------------------------------- */

static bool space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

static char lower(char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c;
}

static bool name_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '-' || c == '_' || (unsigned char)c >= 0x80;
}

/* Vergleicht ohne Rücksicht auf Groß-/Kleinschreibung mit einem kleingeschriebenen Wort. */
static bool word_is(const char *text, size_t length, const char *word)
{
    size_t n = strlen(word);
    if (length != n)
        return false;
    for (size_t i = 0; i < n; i++) {
        if (lower(text[i]) != word[i])
            return false;
    }
    return true;
}

static void trim(const char **text, size_t *length)
{
    while (*length && space(**text)) {
        (*text)++;
        (*length)--;
    }
    while (*length && space((*text)[*length - 1]))
        (*length)--;
}

static uint32_t hash(uint8_t kind, const char *name, size_t length)
{
    uint32_t h = 2166136261u ^ kind;
    for (size_t i = 0; i < length; i++)
        h = (h ^ (uint8_t)(kind == S_TAG ? lower(name[i]) : name[i])) * 16777619u;
    return h & (BUCKETS - 1);
}

/* --- Farben und Werte ----------------------------------------------------------------- */

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    c = lower(c);
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* Liest eine Zahl mit Nachkommastellen; liefert sie mal 1000 und die Einheit dahinter. */
static bool number(const char **p, const char *end, int64_t *milli, const char **unit,
                   size_t *unit_length)
{
    const char *s = *p;
    bool negative = false, digits = false;
    int64_t value = 0, scale = 1;
    if (s < end && (*s == '-' || *s == '+'))
        negative = *s++ == '-';
    while (s < end && *s >= '0' && *s <= '9') {
        if (value < 100000000)
            value = value * 10 + (*s - '0');
        s++;
        digits = true;
    }
    value *= 1000;
    if (s < end && *s == '.') {
        s++;
        while (s < end && *s >= '0' && *s <= '9') {
            if (scale < 1000) {
                scale *= 10;
                value += (*s - '0') * (1000 / scale);
            }
            s++;
            digits = true;
        }
    }
    if (!digits)
        return false;
    *unit = s;
    while (s < end && (name_char(*s) || *s == '%'))
        s++;
    *unit_length = (size_t)(s - *unit);
    *milli = negative ? -value : value;
    *p = s;
    return true;
}

static uint32_t clamp_byte(int64_t value)
{
    return (uint32_t)(value < 0 ? 0 : value > 255 ? 255 : value);
}

/* Farbton (0..360), Sättigung und Helligkeit (0..1000) nach Rot, Grün, Blau */
static uint32_t from_hsl(int64_t hue, int64_t saturation, int64_t lightness)
{
    hue = ((hue % 360) + 360) % 360;
    int64_t chroma = (1000 - (2 * lightness > 1000 ? 2 * lightness - 1000 : 1000 - 2 * lightness)) *
                     saturation / 1000;
    int64_t sector = hue % 120;         /* Dreieckswelle über je 120 Grad */
    int64_t x = chroma * (60 - (sector > 60 ? sector - 60 : 60 - sector)) / 60;
    int64_t base = lightness - chroma / 2;
    int64_t r = 0, g = 0, b = 0;
    switch (hue / 60) {
    case 0: r = chroma; g = x; break;
    case 1: r = x; g = chroma; break;
    case 2: g = chroma; b = x; break;
    case 3: g = x; b = chroma; break;
    case 4: r = x; b = chroma; break;
    default: r = chroma; b = x; break;
    }
    return (clamp_byte((r + base) * 255 / 1000) << 16) | (clamp_byte((g + base) * 255 / 1000) << 8) |
           clamp_byte((b + base) * 255 / 1000);
}

static const struct {
    const char *name;
    uint32_t rgb;
} named_colors[] = {
    { "black", 0x000000 }, { "white", 0xFFFFFF }, { "red", 0xFF0000 }, { "green", 0x008000 },
    { "blue", 0x0000FF }, { "yellow", 0xFFFF00 }, { "orange", 0xFFA500 },
    { "purple", 0x800080 }, { "gray", 0x808080 }, { "grey", 0x808080 },
    { "silver", 0xC0C0C0 }, { "maroon", 0x800000 }, { "navy", 0x000080 },
    { "teal", 0x008080 }, { "olive", 0x808000 }, { "lime", 0x00FF00 }, { "aqua", 0x00FFFF },
    { "cyan", 0x00FFFF }, { "fuchsia", 0xFF00FF }, { "magenta", 0xFF00FF },
    { "pink", 0xFFC0CB }, { "brown", 0xA52A2A }, { "gold", 0xFFD700 },
    { "lightgray", 0xD3D3D3 }, { "lightgrey", 0xD3D3D3 }, { "darkgray", 0xA9A9A9 },
    { "darkgrey", 0xA9A9A9 }, { "dimgray", 0x696969 }, { "dimgrey", 0x696969 },
    { "whitesmoke", 0xF5F5F5 }, { "gainsboro", 0xDCDCDC }, { "ivory", 0xFFFFF0 },
    { "beige", 0xF5F5DC }, { "lightyellow", 0xFFFFE0 }, { "lightblue", 0xADD8E6 },
    { "lightgreen", 0x90EE90 }, { "darkred", 0x8B0000 }, { "darkgreen", 0x006400 },
    { "darkblue", 0x00008B }, { "crimson", 0xDC143C }, { "tomato", 0xFF6347 },
    { "coral", 0xFF7F50 }, { "salmon", 0xFA8072 }, { "khaki", 0xF0E68C },
    { "indigo", 0x4B0082 }, { "violet", 0xEE82EE }, { "skyblue", 0x87CEEB },
    { "steelblue", 0x4682B4 }, { "royalblue", 0x4169E1 }, { "slategray", 0x708090 },
    { "slategrey", 0x708090 }, { "darkslategray", 0x2F4F4F }, { "firebrick", 0xB22222 },
    { "forestgreen", 0x228B22 }, { "seagreen", 0x2E8B57 }, { "darkorange", 0xFF8C00 },
    { "aliceblue", 0xF0F8FF }, { "ghostwhite", 0xF8F8FF }, { "snow", 0xFFFAFA },
    { "linen", 0xFAF0E6 }, { "lavender", 0xE6E6FA }, { "midnightblue", 0x191970 },
};

bool css_color(const char *text, size_t length, uint32_t *rgb)
{
    trim(&text, &length);
    if (length == 0)
        return false;

    if (text[0] == '#') {
        int digits[8];
        size_t n = length - 1;
        if (n != 3 && n != 4 && n != 6 && n != 8)
            return false;
        for (size_t i = 0; i < n; i++) {
            digits[i] = hex_digit(text[1 + i]);
            if (digits[i] < 0)
                return false;
        }
        if (n <= 4) {
            if (n == 4 && digits[3] < 2)
                return false;           /* (fast) durchsichtig */
            *rgb = (uint32_t)((digits[0] * 17) << 16 | (digits[1] * 17) << 8 | digits[2] * 17);
        } else {
            if (n == 8 && digits[6] * 16 + digits[7] < 26)
                return false;
            *rgb = (uint32_t)((digits[0] * 16 + digits[1]) << 16 |
                              (digits[2] * 16 + digits[3]) << 8 | (digits[4] * 16 + digits[5]));
        }
        return true;
    }

    bool is_rgb = length > 4 && lower(text[0]) == 'r' && lower(text[1]) == 'g' && lower(text[2]) == 'b';
    bool is_hsl = length > 4 && lower(text[0]) == 'h' && lower(text[1]) == 's' && lower(text[2]) == 'l';
    if ((is_rgb || is_hsl) && text[length - 1] == ')') {
        const char *p = text + 3, *end = text + length - 1;
        if (p < end && lower(*p) == 'a')
            p++;
        if (p >= end || *p != '(')
            return false;
        p++;
        int64_t values[4] = { 0, 0, 0, 1000 };
        bool percent[4] = { false, false, false, false };
        int count = 0;
        while (p < end && count < 4) {
            while (p < end && (space(*p) || *p == ',' || *p == '/'))
                p++;
            if (p >= end)
                break;
            const char *unit;
            size_t unit_length;
            if (!number(&p, end, &values[count], &unit, &unit_length))
                return false;
            percent[count] = unit_length == 1 && unit[0] == '%';
            count++;
        }
        if (count < 3)
            return false;
        int64_t alpha = percent[3] ? values[3] / 100 : values[3];    /* Tausendstel */
        if (count == 4 && alpha < 100)
            return false;
        if (is_hsl) {
            *rgb = from_hsl(values[0] / 1000, values[1] / 100, values[2] / 100);
        } else {
            uint32_t parts[3];
            for (int i = 0; i < 3; i++)
                parts[i] = clamp_byte(percent[i] ? values[i] * 255 / 100000 : values[i] / 1000);
            *rgb = (parts[0] << 16) | (parts[1] << 8) | parts[2];
        }
        return true;
    }

    for (unsigned i = 0; i < sizeof(named_colors) / sizeof(named_colors[0]); i++) {
        if (word_is(text, length, named_colors[i].name)) {
            *rgb = named_colors[i].rgb;
            return true;
        }
    }
    return false;
}

/* Ersetzt var(--name, ersatz) durch den hinterlegten Wert; false, wenn nichts passt. */
static bool resolve_vars(const css_sheet_t *sheet, const char *text, size_t length, char *out,
                         size_t size, int depth)
{
    size_t used = 0;
    for (size_t i = 0; i < length;) {
        bool is_var = i + 4 <= length && lower(text[i]) == 'v' && lower(text[i + 1]) == 'a' &&
                      lower(text[i + 2]) == 'r' && text[i + 3] == '(';
        if (!is_var) {
            if (used + 1 >= size)
                return false;
            out[used++] = text[i++];
            continue;
        }
        /* Klammer bis zu ihrem Ende, darin: Name [, Ersatzwert] */
        size_t start = i + 4, close = start;
        int level = 1;
        while (close < length && level > 0) {
            if (text[close] == '(')
                level++;
            else if (text[close] == ')')
                level--;
            if (level > 0)
                close++;
        }
        if (close >= length || depth > 4)
            return false;
        const char *name = text + start;
        size_t name_length = 0;
        while (start + name_length < close && text[start + name_length] != ',')
            name_length++;
        const char *fallback = name + name_length;
        size_t fallback_length = (size_t)(text + close - fallback);
        if (fallback_length) {
            fallback++;
            fallback_length--;
        }
        trim(&name, &name_length);

        const char *value = NULL;
        size_t value_length = 0;
        for (int v = sheet ? sheet->var_count - 1 : -1; v >= 0; v--) {  /* die letzte Angabe gilt */
            if (sheet->vars[v].name_length == name_length &&
                memcmp(sheet->vars[v].name, name, name_length) == 0) {
                value = sheet->vars[v].value;
                value_length = sheet->vars[v].value_length;
                break;
            }
        }
        if (!value) {
            trim(&fallback, &fallback_length);
            if (!fallback_length)
                return false;
            value = fallback;
            value_length = fallback_length;
        }
        char inner[VALUE_MAX];
        if (!resolve_vars(sheet, value, value_length, inner, sizeof(inner), depth + 1))
            return false;
        size_t n = strlen(inner);
        if (used + n + 1 >= size)
            return false;
        memcpy(out + used, inner, n);
        used += n;
        i = close + 1;
    }
    out[used] = '\0';
    return true;
}

/* Sucht in einer Kurzschreibweise ("background: #fff url(x) no-repeat") die Farbe. */
static bool find_color(const char *text, size_t length, uint32_t *rgb, bool *has_image)
{
    bool found = false;
    *has_image = false;
    for (size_t i = 0; i < length;) {
        while (i < length && (space(text[i]) || text[i] == ','))
            i++;
        size_t start = i;
        int level = 0;
        while (i < length && (level > 0 || (!space(text[i]) && text[i] != ','))) {
            if (text[i] == '(')
                level++;
            else if (text[i] == ')')
                level--;
            i++;
        }
        size_t n = i - start;
        if (!n)
            continue;
        bool function = text[i - 1] == ')';
        if (function && !(n > 4 && (lower(text[start]) == 'r' || lower(text[start]) == 'h'))) {
            *has_image = true;          /* url(...), Farbverlauf: ein Bild als Hintergrund */
            continue;
        }
        uint32_t color;
        if (css_color(text + start, n, &color)) {
            *rgb = color;
            found = true;
        }
    }
    return found;
}

/* --- Regeln anwenden -------------------------------------------------------------------- */

typedef struct {
    uint64_t priority[P_COUNT];
    uint64_t box_priority[L_COUNT];
    bool boxes;                         /* auch die Angaben für Kästen und die Grundregeln auswerten */
    uint8_t pseudo;                     /* gesucht sind die Regeln dieses Scheinelements (0: des Elements) */
    const char *content;                /* der Wert von "content" */
    size_t content_length;
    uint64_t content_priority;
    css_style_t *style;
} cascade_t;

static bool contains_word(const char *text, size_t length, const char *word)
{
    size_t n = strlen(word);
    for (size_t i = 0; i + n <= length; i++) {
        if ((i == 0 || !name_char(text[i - 1])) && word_is(text + i, n, word) &&
            (i + n == length || !name_char(text[i + n])))
            return true;
    }
    return false;
}

static int sheet_viewport(const css_sheet_t *sheet)
{
    return sheet ? sheet->viewport : 1000;
}

/* Beginnt text (bis stop) mit word? Groß- und Kleinschreibung ist gleich. */
static bool starts_word(const char *text, const char *stop, const char *word)
{
    for (; *word; word++, text++) {
        if (text >= stop || lower(*text) != *word)
            return false;
    }
    return true;
}

/* Enthält die Liste ein Wort, das mit prefix beginnt ("column" trifft auch "column-reverse")? */
static bool has_word_starting(const char *text, size_t length, const char *prefix)
{
    const char *stop = text + length;
    for (const char *p = text; p < stop; p++) {
        if ((p == text || space(p[-1])) && starts_word(p, stop, prefix))
            return true;
    }
    return false;
}

/* Zählt die Stücke einer Liste von Spalten, die nicht in Klammern stehen. */
static int count_tracks(const char *p, const char *stop)
{
    int count = 0, level = 0;
    bool inside = false;
    for (; p < stop; p++) {
        if (*p == '(') {
            level++;
        } else if (*p == ')' && level > 0) {
            level--;
        }
        if (level == 0 && space(*p)) {
            inside = false;
        } else if (!inside) {
            inside = true;
            count += *p != '[';         /* [name]: nur ein Name für eine Linie */
        }
    }
    return count;
}

/*
 * grid-template-columns: wie viele Spalten? "1fr 200px repeat(3, 1fr)" sind
 * fünf. Bei repeat(auto-fill, minmax(180px, 1fr)) will die Seite so viele,
 * wie passen - dann zählt die Mindestbreite.
 */
static void grid_tracks(const css_sheet_t *sheet, const char *p, const char *stop, css_style_t *s)
{
    int count = 0, level = 0;
    s->tracks = 0;
    s->track_min_px = 0;
    while (p < stop) {
        while (p < stop && space(*p))
            p++;
        const char *word = p;
        for (level = 0; p < stop && (level > 0 || !space(*p)); p++)
            level += *p == '(' ? 1 : *p == ')' && level > 0 ? -1 : 0;
        if (p == word || *word == '[')
            continue;
        if (starts_word(word, p, "none") || starts_word(word, p, "subgrid") ||
            starts_word(word, p, "masonry")) {
            return;
        } else if (starts_word(word, p, "repeat(")) {
            const char *q = word + 7, *comma = q, *unit;
            size_t unit_length;
            int64_t value = 0;
            while (q < p && space(*q))
                q++;
            while (comma < p && *comma != ',')
                comma++;
            if (comma >= p)
                return;
            if (starts_word(q, p, "auto-f")) {
                /* so viele, wie passen: die kleinste Breite steht vorn in minmax() */
                int px = 200;
                q = comma + 1;
                while (q < p && space(*q))
                    q++;
                if (starts_word(q, p, "minmax("))
                    q += 7;
                while (q < p && space(*q))
                    q++;
                if (number(&q, p, &value, &unit, &unit_length) && value > 0) {
                    if (word_is(unit, unit_length, "px"))
                        px = (int)(value / 1000);
                    else if (word_is(unit, unit_length, "em") || word_is(unit, unit_length, "rem"))
                        px = (int)(value * 16 / 1000);
                    else if (word_is(unit, unit_length, "vw"))
                        px = (int)(value * sheet_viewport(sheet) / 100000);
                }
                s->track_min_px = px < 40 ? 40 : px > 4000 ? 4000 : px;
                return;
            }
            if (!number(&q, p, &value, &unit, &unit_length) || value < 1000 || value > 40000)
                return;
            count += (int)(value / 1000) * count_tracks(comma + 1, p - 1);
        } else {
            count++;
        }
    }
    s->tracks = (uint8_t)(count > 40 ? 40 : count);
}

/* --- Kästen: Längen, Rahmen und was der Seitenaufbau mit Kastenmodell sonst braucht ------ */

/* Eine Rechnung mit Längen: Pixel, Prozent, em und reine Zahl, je mal 1000 */
typedef struct {
    int64_t px, percent, em, num;
    bool pure;                          /* nur eine Zahl ohne Einheit */
} combo_t;

static int sheet_height(const css_sheet_t *sheet)
{
    int width = sheet_viewport(sheet);
    return sheet && sheet->viewport_height > 0 ? sheet->viewport_height : width * 3 / 4;
}

static bool unit_combo(const css_sheet_t *sheet, int64_t value, const char *unit, size_t n,
                       combo_t *out)
{
    static const struct {
        const char *name;
        int mul, div;
    } absolute[] = {
        { "px", 1, 1 }, { "pt", 4, 3 }, { "pc", 16, 1 }, { "in", 96, 1 }, { "cm", 3780, 100 },
        { "mm", 378, 100 }, { "q", 945, 1000 }, { "rem", 16, 1 },
    };
    memset(out, 0, sizeof(*out));
    if (n == 0) {
        out->num = value;
        out->pure = true;
        return true;
    }
    for (unsigned i = 0; i < sizeof(absolute) / sizeof(absolute[0]); i++) {
        if (word_is(unit, n, absolute[i].name)) {
            out->px = value * absolute[i].mul / absolute[i].div;
            return true;
        }
    }
    int width = sheet_viewport(sheet), height = sheet_height(sheet);
    /* svw, lvh, dvh ...: die Vorsilbe für kleine/große/veränderliche Anzeige zählt hier nicht */
    if (n == 3 && (lower(unit[0]) == 's' || lower(unit[0]) == 'l' || lower(unit[0]) == 'd') &&
        lower(unit[1]) == 'v') {
        unit++;
        n--;
    }
    if (word_is(unit, n, "em"))
        out->em = value;
    else if (word_is(unit, n, "ex") || word_is(unit, n, "ch"))
        out->em = value / 2;
    else if (word_is(unit, n, "%"))
        out->percent = value;
    else if (word_is(unit, n, "vw") || word_is(unit, n, "vi"))
        out->px = value * width / 100;
    else if (word_is(unit, n, "vh") || word_is(unit, n, "vb"))
        out->px = value * height / 100;
    else if (word_is(unit, n, "vmin"))
        out->px = value * (width < height ? width : height) / 100;
    else if (word_is(unit, n, "vmax"))
        out->px = value * (width > height ? width : height) / 100;
    else
        return false;
    return true;
}

/* Ungefähre Größe in Pixeln mal 1000 - nur zum Vergleichen in min(), max() und clamp() */
static int64_t combo_guess(const css_sheet_t *sheet, const combo_t *c)
{
    return c->px + c->percent * sheet_viewport(sheet) / 100 + c->em * 16 + c->num;
}

static bool combo_sum(const css_sheet_t *sheet, const char **p, const char *end, combo_t *out,
                      int depth);

static bool combo_primary(const css_sheet_t *sheet, const char **p, const char *end, combo_t *out,
                          int depth)
{
    const char *s = *p;
    while (s < end && space(*s))
        s++;
    if (s >= end || depth > 8)
        return false;
    const char *name = s;
    while (s < end && (name_char(*s)) && !(*s >= '0' && *s <= '9') && *s != '-' && *s != '+')
        s++;
    if (s < end && *s == '(') {
        size_t n = (size_t)(s - name);
        s++;
        if (n == 0 || word_is(name, n, "calc") || word_is(name, n, "-webkit-calc")) {
            if (!combo_sum(sheet, &s, end, out, depth + 1))
                return false;
        } else if (word_is(name, n, "min") || word_is(name, n, "max") || word_is(name, n, "clamp")) {
            combo_t args[3];
            int count = 0;
            for (;;) {
                combo_t one;
                if (!combo_sum(sheet, &s, end, &one, depth + 1))
                    return false;
                if (count < 3)
                    args[count] = one;
                else if (name[1] == 'i' ? combo_guess(sheet, &one) < combo_guess(sheet, &args[0])
                                        : combo_guess(sheet, &one) > combo_guess(sheet, &args[0]))
                    args[0] = one;      /* min/max mit mehr als drei Werten: laufend auswählen */
                count++;
                while (s < end && space(*s))
                    s++;
                if (s < end && *s == ',') {
                    s++;
                    continue;
                }
                break;
            }
            if (count > 3)
                count = 1;
            if (lower(name[0]) == 'c') {        /* clamp(kleinster, gewünschter, größter) */
                if (count != 3)
                    return false;
                *out = args[1];
                if (combo_guess(sheet, out) > combo_guess(sheet, &args[2]))
                    *out = args[2];
                if (combo_guess(sheet, out) < combo_guess(sheet, &args[0]))
                    *out = args[0];
            } else {
                bool smallest = lower(name[1]) == 'i';
                *out = args[0];
                for (int i = 1; i < count; i++) {
                    int64_t a = combo_guess(sheet, &args[i]), b = combo_guess(sheet, out);
                    if (smallest ? a < b : a > b)
                        *out = args[i];
                }
            }
        } else {
            return false;               /* env(), attr() und was es sonst gibt */
        }
        while (s < end && space(*s))
            s++;
        if (s >= end || *s != ')')
            return false;
        *p = s + 1;
        return true;
    }
    s = name;
    const char *unit;
    size_t unit_length;
    int64_t value;
    if (!number(&s, end, &value, &unit, &unit_length) ||
        !unit_combo(sheet, value, unit, unit_length, out))
        return false;
    *p = s;
    return true;
}

static bool combo_product(const css_sheet_t *sheet, const char **p, const char *end, combo_t *out,
                          int depth)
{
    if (!combo_primary(sheet, p, end, out, depth))
        return false;
    for (;;) {
        const char *s = *p;
        while (s < end && space(*s))
            s++;
        if (s >= end || (*s != '*' && *s != '/'))
            return true;
        char op = *s++;
        combo_t right;
        if (!combo_primary(sheet, &s, end, &right, depth))
            return false;
        if (op == '/') {
            if (!right.pure || right.num == 0)
                return false;
            out->px = out->px * 1000 / right.num;
            out->percent = out->percent * 1000 / right.num;
            out->em = out->em * 1000 / right.num;
            out->num = out->num * 1000 / right.num;
        } else {
            if (!out->pure && !right.pure)
                return false;
            if (out->pure) {            /* Zahl mal Länge: die Länge steht rechts */
                combo_t swap = *out;
                *out = right;
                right = swap;
            }
            out->px = out->px * right.num / 1000;
            out->percent = out->percent * right.num / 1000;
            out->em = out->em * right.num / 1000;
            out->num = out->num * right.num / 1000;
        }
        *p = s;
    }
}

static bool combo_sum(const css_sheet_t *sheet, const char **p, const char *end, combo_t *out,
                      int depth)
{
    if (!combo_product(sheet, p, end, out, depth))
        return false;
    for (;;) {
        const char *s = *p;
        while (s < end && space(*s))
            s++;
        if (s >= end || (*s != '+' && *s != '-'))
            return true;
        bool minus = *s++ == '-';
        combo_t right;
        if (!combo_product(sheet, &s, end, &right, depth))
            return false;
        out->px += minus ? -right.px : right.px;
        out->percent += minus ? -right.percent : right.percent;
        out->em += minus ? -right.em : right.em;
        out->num += minus ? -right.num : right.num;
        out->pure = out->pure && right.pure;
        *p = s;
    }
}

static int32_t fit32(int64_t v)
{
    return v > 2000000000 ? 2000000000 : v < -2000000000 ? -2000000000 : (int32_t)v;
}

enum { LEN_AUTO = 1, LEN_NONE = 2, LEN_NORMAL = 4, LEN_CONTENT = 8, LEN_NUMBER_EM = 16,
       LEN_NUMBER_PX = 32, LEN_POSITIVE = 64, LEN_THICKNESS = 128, LEN_PERCENT_EM = 256 };

/* Liest eine Länge; allow sagt, welche Wörter und Schreibweisen dabei gelten. */
static bool parse_len(const css_sheet_t *sheet, const char *text, size_t length, unsigned allow,
                      css_len_t *out)
{
    trim(&text, &length);
    memset(out, 0, sizeof(*out));
    if (!length)
        return false;
    if (lower(text[0]) >= 'a' && lower(text[0]) <= 'z' && text[length - 1] != ')') {
        if ((allow & LEN_AUTO) && word_is(text, length, "auto"))
            out->kind = CSS_LEN_AUTO;
        else if ((allow & LEN_NONE) && word_is(text, length, "none"))
            out->kind = CSS_LEN_NONE;
        else if ((allow & LEN_NORMAL) && word_is(text, length, "normal"))
            out->kind = CSS_LEN_AUTO;
        else if ((allow & LEN_CONTENT) &&
                 (word_is(text, length, "min-content") || word_is(text, length, "max-content") ||
                  word_is(text, length, "fit-content") || word_is(text, length, "-moz-fit-content") ||
                  word_is(text, length, "-webkit-fit-content") || word_is(text, length, "content")))
            out->kind = CSS_LEN_CONTENT;
        else if ((allow & (LEN_CONTENT | LEN_AUTO)) &&
                 (word_is(text, length, "stretch") || word_is(text, length, "-webkit-fill-available") ||
                  word_is(text, length, "-moz-available")))
            out->kind = CSS_LEN_AUTO;
        else if ((allow & LEN_THICKNESS) && (word_is(text, length, "thin") ||
                                             word_is(text, length, "medium") ||
                                             word_is(text, length, "thick"))) {
            out->kind = CSS_LEN_VALUE;
            out->px = lower(text[0]) == 'm' ? 3000 : lower(text[2]) == 'i' && length == 4 ? 1000 : 5000;
        } else {
            return false;
        }
        return true;
    }
    const char *p = text, *end = text + length;
    combo_t c;
    if (!combo_sum(sheet, &p, end, &c, 0))
        return false;
    while (p < end && space(*p))
        p++;
    if (p != end)
        return false;
    if (c.num) {
        if (!c.pure)
            return false;               /* Zahl und Länge gemischt */
        if (allow & LEN_NUMBER_EM)
            c.em = c.num;
        else if (allow & LEN_NUMBER_PX)
            c.px = c.num;
        else
            return false;
    }
    if (allow & LEN_PERCENT_EM) {       /* Prozent meinen hier die Schriftgröße */
        c.em += c.percent / 100;
        c.percent = 0;
    }
    if ((allow & LEN_POSITIVE) && c.px < 0 && c.percent <= 0 && c.em <= 0)
        return false;
    out->kind = CSS_LEN_VALUE;
    out->px = fit32(c.px);
    out->percent = fit32(c.percent);
    out->em = fit32(c.em);
    return true;
}

bool css_length(const css_sheet_t *sheet, const char *text, size_t length, css_len_t *out)
{
    return parse_len(sheet, text, length, LEN_AUTO | LEN_NUMBER_PX, out);
}

/* Das nächste Stück eines Werts: bis zum Leerraum außerhalb von Klammern (oder bis stop) */
static bool next_piece(const char **p, const char *end, char stop, const char **piece, size_t *n)
{
    const char *s = *p;
    while (s < end && space(*s))
        s++;
    if (s >= end)
        return false;
    const char *start = s;
    int level = 0;
    char quote = 0;
    while (s < end && (level > 0 || quote || (!space(*s) && *s != stop))) {
        if (quote) {
            if (*s == quote)
                quote = 0;
        } else if (*s == '"' || *s == '\'') {
            quote = *s;
        } else if (*s == '(') {
            level++;
        } else if (*s == ')' && level > 0) {
            level--;
        }
        s++;
    }
    *piece = start;
    *n = (size_t)(s - start);
    if (s < end && *s == stop && *n == 0) {     /* das Trennzeichen selbst */
        *n = 1;
        s++;
    }
    *p = s;
    return true;
}

/* Längen für die vier Seiten: 1 bis 4 Werte in der üblichen Kurzschreibweise */
static bool four_lens(const css_sheet_t *sheet, const char *text, size_t length, unsigned allow,
                      css_len_t out[4])
{
    const char *p = text, *end = text + length, *piece;
    size_t n;
    css_len_t v[4];
    int count = 0;
    while (next_piece(&p, end, 0, &piece, &n)) {
        if (count >= 4 || !parse_len(sheet, piece, n, allow, &v[count]))
            return false;
        count++;
    }
    if (count == 0)
        return false;
    out[0] = v[0];
    out[1] = count > 1 ? v[1] : v[0];
    out[2] = count > 2 ? v[2] : v[0];
    out[3] = count > 3 ? v[3] : count > 1 ? v[1] : v[0];
    return true;
}

static uint8_t border_style_of(const char *text, size_t n)
{
    static const char *const visible[] = { "solid", "double", "groove", "ridge", "inset", "outset", NULL };
    if (word_is(text, n, "none") || word_is(text, n, "hidden"))
        return CSS_BS_NONE;
    if (word_is(text, n, "dashed"))
        return CSS_BS_DASHED;
    if (word_is(text, n, "dotted"))
        return CSS_BS_DOTTED;
    for (int i = 0; visible[i]; i++) {
        if (word_is(text, n, visible[i]))
            return CSS_BS_SOLID;
    }
    return 0;
}

static uint8_t color_kind(const char *text, size_t n, uint32_t *rgb)
{
    if (word_is(text, n, "currentcolor"))
        return CSS_COLOR_CURRENT;
    if (word_is(text, n, "transparent"))
        return CSS_COLOR_TRANSPARENT;
    return css_color(text, n, rgb) ? CSS_COLOR_VALUE : 0;
}

static uint8_t keyword(const char *text, size_t n, const char *const *names, const uint8_t *values)
{
    for (int i = 0; names[i]; i++) {
        if (word_is(text, n, names[i]))
            return values[i];
    }
    return 0;
}

/* Wörter für align-items, align-self, align-content */
static uint8_t align_word(const char *text, size_t n)
{
    static const char *const names[] = { "stretch", "normal", "flex-start", "start", "self-start",
                                         "center", "flex-end", "end", "self-end", "baseline",
                                         "first", "last", "safe", "unsafe", NULL };
    static const uint8_t values[] = { CSS_AL_STRETCH, CSS_AL_STRETCH, CSS_AL_START, CSS_AL_START,
                                      CSS_AL_START, CSS_AL_CENTER, CSS_AL_END, CSS_AL_END, CSS_AL_END,
                                      CSS_AL_BASELINE, CSS_AL_BASELINE, CSS_AL_BASELINE, 0, 0 };
    return keyword(text, n, names, values);
}

static uint8_t justify_word(const char *text, size_t n)
{
    static const char *const names[] = { "flex-start", "start", "left", "normal", "stretch", "center",
                                         "flex-end", "end", "right", "space-between", "space-around",
                                         "space-evenly", NULL };
    static const uint8_t values[] = { CSS_JU_START, CSS_JU_START, CSS_JU_START, CSS_JU_START,
                                      CSS_JU_START, CSS_JU_CENTER, CSS_JU_END, CSS_JU_END, CSS_JU_END,
                                      CSS_JU_BETWEEN, CSS_JU_AROUND, CSS_JU_EVENLY };
    return keyword(text, n, names, values);
}

static bool family_is_mono(const char *text, size_t length)
{
    static const char *const mono[] = { "monospace", "courier", "consolas", "menlo", "monaco",
                                        "mono", "fira code", "source code", "lucida console",
                                        "ui-monospace", "terminal", "fixed", NULL };
    /* Es zählt die erste Schrift der Liste - und "monospace" irgendwo darin */
    size_t first = 0;
    while (first < length && text[first] != ',')
        first++;
    for (int i = 0; mono[i]; i++) {
        size_t n = strlen(mono[i]);
        for (size_t at = 0; at + n <= (i == 0 ? length : first); at++) {
            size_t k = 0;
            while (k < n && lower(text[at + k]) == mono[i][k])
                k++;
            if (k == n)
                return true;
        }
    }
    return false;
}

/* font-size: Wörter, Längen; em und Prozent meinen die Schrift der Umgebung */
static bool font_size_len(const css_sheet_t *sheet, const char *text, size_t length, css_len_t *out)
{
    static const struct {
        const char *name;
        int px, em;
    } words[] = {
        { "xx-small", 9, 0 }, { "x-small", 10, 0 }, { "small", 13, 0 }, { "medium", 16, 0 },
        { "large", 18, 0 }, { "x-large", 24, 0 }, { "xx-large", 32, 0 }, { "xxx-large", 48, 0 },
        { "smaller", 0, 833 }, { "larger", 0, 1200 },
    };
    for (unsigned i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
        if (word_is(text, length, words[i].name)) {
            memset(out, 0, sizeof(*out));
            out->kind = CSS_LEN_VALUE;
            out->px = words[i].px * 1000;
            out->em = words[i].em;
            return true;
        }
    }
    return parse_len(sheet, text, length, LEN_POSITIVE | LEN_PERCENT_EM, out) &&
           (out->px > 0 || out->em > 0);
}

/* Eine Spalte eines Rasters: "200px", "1fr", "auto", "minmax(100px, 1fr)" */
static bool grid_track(const css_sheet_t *sheet, const char *text, size_t n, css_track_t *out)
{
    memset(out, 0, sizeof(*out));
    if (n > 8 && word_is(text, 7, "minmax(") && text[n - 1] == ')') {
        const char *comma = text + 7, *end = text + n - 1;
        int level = 0;
        while (comma < end && (level > 0 || *comma != ',')) {
            level += *comma == '(' ? 1 : *comma == ')' ? -1 : 0;
            comma++;
        }
        if (comma >= end)
            return false;
        css_track_t low, high;
        if (!grid_track(sheet, text + 7, (size_t)(comma - text - 7), &low) ||
            !grid_track(sheet, comma + 1, (size_t)(end - comma - 1), &high))
            return false;
        if (high.fr || high.automatic) {        /* mindestens so breit, sonst nach Anteil */
            *out = high;
            out->size = low.size;
        } else {
            *out = high;                /* zwei feste Grenzen: die obere gilt - solange der Platz reicht */
            out->shrinks = !low.fr;
            out->low = low.size;
        }
        return true;
    }
    if (n > 12 && word_is(text, 12, "fit-content(") && text[n - 1] == ')') {
        out->automatic = true;
        return true;
    }
    trim(&text, &n);
    if (word_is(text, n, "auto") || word_is(text, n, "min-content") || word_is(text, n, "max-content")) {
        out->automatic = true;
        return true;
    }
    const char *p = text, *unit;
    size_t unit_length;
    int64_t value;
    if (number(&p, text + n, &value, &unit, &unit_length) && p == text + n &&
        word_is(unit, unit_length, "fr")) {
        out->fr = fit32(value > 0 ? value : 1);
        return true;
    }
    return parse_len(sheet, text, n, LEN_POSITIVE | LEN_NUMBER_PX, &out->size);
}

/* Die Kennzahl eines Flächennamens (nie 0) */
static uint16_t area_hash(const char *name, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++)
        h = (h ^ (uint8_t)name[i]) * 16777619u;
    h = (h ^ (h >> 16)) & 0xFFFF;
    return (uint16_t)(h ? h : 1);
}

/* Die Zeilen in Anführungszeichen ("kopf kopf" "leiste inhalt"): benannte Flächen eines Rasters */
static bool grid_areas(const char *text, size_t length, css_style_t *s)
{
    uint16_t areas[CSS_AREA_ROWS][CSS_AREA_COLUMNS];
    int rows = 0, columns = 0;
    memset(areas, 0, sizeof(areas));
    for (size_t i = 0; i < length; i++) {
        if (text[i] != '"' && text[i] != '\'')
            continue;
        char quote = text[i++];
        int column = 0;
        while (i < length && text[i] != quote) {
            while (i < length && space(text[i]))
                i++;
            size_t start = i;
            while (i < length && !space(text[i]) && text[i] != quote)
                i++;
            if (i > start && rows < CSS_AREA_ROWS && column < CSS_AREA_COLUMNS) {
                areas[rows][column] = text[start] == '.' ? 0 : area_hash(text + start, i - start);
                column++;
            }
        }
        if (column > columns)
            columns = column;
        if (rows < CSS_AREA_ROWS)
            rows++;
    }
    if (!rows || !columns)
        return false;
    memcpy(s->area_map, areas, sizeof(areas));
    s->area_rows = (uint8_t)rows;
    s->area_columns = (uint8_t)columns;
    return true;
}

/* grid-template-columns: Spalten der Reihe nach, auch mit repeat(...) */
static bool grid_template(const css_sheet_t *sheet, const char *text, size_t length, css_style_t *s)
{
    const char *p = text, *end = text + length, *piece;
    size_t n;
    css_track_t tracks[CSS_GRID_TRACKS];
    int count = 0;
    bool auto_fill = false;
    if (word_is(text, length, "none") || word_is(text, length, "subgrid")) {
        s->grid_count = 0;
        s->grid_auto_fill = false;
        return true;
    }
    while (next_piece(&p, end, 0, &piece, &n)) {
        if (piece[0] == '[') {          /* Namen von Linien: zählen nicht */
            while (n && piece[n - 1] != ']' && p < end) {
                p++;
                n++;
            }
            continue;
        }
        if (n > 8 && word_is(piece, 7, "repeat(") && piece[n - 1] == ')') {
            const char *q = piece + 7, *stop = piece + n - 1, *comma = q;
            while (comma < stop && *comma != ',')
                comma++;
            if (comma >= stop)
                return false;
            const char *times = q;
            size_t times_length = (size_t)(comma - q);
            trim(&times, &times_length);
            int repeat = 0;
            if (word_is(times, times_length, "auto-fill") || word_is(times, times_length, "auto-fit")) {
                auto_fill = true;
                repeat = 1;
            } else {
                for (size_t i = 0; i < times_length && times[i] >= '0' && times[i] <= '9'; i++)
                    repeat = repeat * 10 + (times[i] - '0');
            }
            if (repeat < 1)
                return false;
            css_track_t group[CSS_GRID_TRACKS];
            int group_count = 0;
            const char *g = comma + 1, *one;
            size_t one_length;
            while (next_piece(&g, stop, 0, &one, &one_length)) {
                if (one[0] == '[')
                    continue;
                if (group_count >= CSS_GRID_TRACKS || !grid_track(sheet, one, one_length, &group[group_count]))
                    return false;
                group_count++;
            }
            if (!group_count)
                return false;
            for (int r = 0; r < repeat && count < CSS_GRID_TRACKS; r++) {
                for (int k = 0; k < group_count && count < CSS_GRID_TRACKS; k++)
                    tracks[count++] = group[k];
            }
            continue;
        }
        if (count >= CSS_GRID_TRACKS)
            break;
        if (!grid_track(sheet, piece, n, &tracks[count]))
            return false;
        count++;
    }
    if (!count)
        return false;
    memcpy(s->grid, tracks, sizeof(tracks[0]) * (size_t)count);
    s->grid_count = (uint8_t)count;
    s->grid_auto_fill = auto_fill;
    return true;
}

/*
 * Ein Farbverlauf ("linear-gradient(to right, #fff, #000)"): seine erste und
 * letzte Farbe und ob er von links nach rechts läuft. Andere Verläufe
 * (kreisförmig, wiederholt) liefern nur ihre erste Farbe.
 */
static bool gradient_color(const char *text, size_t length, uint32_t *rgb, uint32_t *last, bool *across)
{
    size_t open = 0;
    while (open < length && text[open] != '(')
        open++;
    if (open < 8 || open >= length)
        return false;
    bool gradient = false;
    for (size_t i = 0; i + 8 <= open; i++)
        gradient = gradient || word_is(text + i, 8, "gradient");
    if (!gradient)
        return false;
    bool linear = false, found = false, swap = false;
    for (size_t i = 0; i + 15 <= open; i++)
        linear = linear || word_is(text + i, 15, "linear-gradient");
    for (size_t i = 0; i + 9 <= open; i++)
        linear = linear && !word_is(text + i, 9, "repeating");
    *across = false;
    bool first_part = true, heading = false;
    for (size_t i = open + 1; i < length;) {
        while (i < length && (space(text[i]) || text[i] == ','))
            i++;
        size_t start = i;
        int level = 0;
        while (i < length && (level > 0 || (!space(text[i]) && text[i] != ',' && text[i] != ')'))) {
            level += text[i] == '(' ? 1 : text[i] == ')' ? -1 : 0;
            i++;
        }
        size_t n = i - start;
        uint32_t color;
        if (n && css_color(text + start, n, &color)) {
            if (!found)
                *rgb = color;
            *last = color;
            found = true;
            first_part = false;
        } else if (n && first_part && !found) {
            /* die Richtung: "to right", "to top left", "90deg" */
            const char *unit, *q = text + start;
            size_t unit_length;
            int64_t value;
            if (word_is(text + start, n, "to")) {
                heading = true;
            } else if (heading && (word_is(text + start, n, "right") || word_is(text + start, n, "left"))) {
                *across = true;
                swap = lower(text[start]) == 'l';
            } else if (heading && word_is(text + start, n, "top")) {
                swap = true;
            } else if (number(&q, text + start + n, &value, &unit, &unit_length) && word_is(unit, unit_length, "deg")) {
                int degrees = (int)(((value / 1000) % 360 + 360) % 360);
                *across = (degrees >= 45 && degrees < 135) || (degrees >= 225 && degrees < 315);
                swap = degrees < 45 || degrees >= 225;      /* 0 Grad: nach oben; 270: nach links */
            }
        }
        if (i < length && text[i] == ',')
            first_part = false;
        if (i < length && text[i] == ')')
            i++;
    }
    if (!found)
        return false;
    if (!linear) {
        *last = *rgb;
        *across = false;
    } else if (swap) {
        uint32_t keep = *rgb;
        *rgb = *last;
        *last = keep;
    }
    return true;
}

/* Die Adresse in "url(...)": ihr Anfang im Text und ihre Länge; NULL, wenn keine dasteht */
static const char *url_inside(const char *text, size_t length, size_t *n)
{
    for (size_t i = 0; i + 4 <= length; i++) {
        if (lower(text[i]) != 'u' || lower(text[i + 1]) != 'r' || lower(text[i + 2]) != 'l' || text[i + 3] != '(')
            continue;
        size_t start = i + 4, end;
        while (start < length && space(text[start]))
            start++;
        char quote = start < length && (text[start] == '"' || text[start] == '\'') ? text[start++] : 0;
        end = start;
        while (end < length && (quote ? text[end] != quote : text[end] != ')' && !space(text[end])))
            end++;
        if (end > start && end - start < 2000) {
            *n = end - start;
            return text + start;
        }
        return NULL;
    }
    return NULL;
}

/* Lage des Hintergrundbilds: Wörter oder Längen, erst waagerecht, dann senkrecht */
static bool position_pair(const css_sheet_t *sheet, const char *const *pieces, const size_t *lengths, int count,
                          css_len_t *x, css_len_t *y)
{
    css_len_t middle = { CSS_LEN_VALUE, 0, 50000, 0 };
    bool has_x = false, has_y = false;
    *x = *y = middle;
    for (int i = 0; i < count && i < 2; i++) {
        const char *t = pieces[i];
        size_t n = lengths[i];
        css_len_t v = { CSS_LEN_VALUE, 0, 0, 0 };
        if (word_is(t, n, "left") || word_is(t, n, "right")) {
            v.percent = lower(t[0]) == 'r' ? 100000 : 0;
            *x = v;
            has_x = true;
        } else if (word_is(t, n, "top") || word_is(t, n, "bottom")) {
            v.percent = lower(t[0]) == 'b' ? 100000 : 0;
            *y = v;
            has_y = true;
        } else if (word_is(t, n, "center")) {
            continue;
        } else if (parse_len(sheet, t, n, LEN_NUMBER_PX, &v)) {
            if (!has_x && i == 0) {
                *x = v;
                has_x = true;
            } else if (!has_y) {
                *y = v;
                has_y = true;
            }
        } else {
            return false;
        }
    }
    return count > 0;
}

#define BOX_SET(id) (priority >= c->box_priority[id] ? (c->box_priority[id] = priority, true) : false)

/* Setzt Breite, Art und Farbe des Rahmens für die Seiten in sides (Bit 0 oben ... Bit 3 links). */
static void border_parts(cascade_t *c, uint64_t priority, unsigned sides, const css_len_t *width,
                         uint8_t style, uint8_t kind, uint32_t color)
{
    css_style_t *s = c->style;
    for (int i = 0; i < 4; i++) {
        if (!(sides & (1u << i)))
            continue;
        if (width && BOX_SET(L_BORDER_WIDTH + i))
            s->border_width[i] = *width;
        if (style && BOX_SET(L_BORDER_STYLE + i))
            s->border_style[i] = style;
        if (kind && BOX_SET(L_BORDER_COLOR + i)) {
            s->border_color_kind[i] = kind;
            s->border_color[i] = color;
        }
    }
}

/* Die neue Rangfolge: jede Einzelangabe für sich, Kurzschreibweisen setzen mehrere. */
static void apply_box(const css_sheet_t *sheet, cascade_t *c, uint8_t property, const char *text,
                      size_t length, uint64_t priority, bool lasting)
{
    css_style_t *s = c->style;
    css_len_t len, four[4];
    const char *p = text, *end = text + length, *piece;
    size_t n;
    uint32_t color;

    switch (property) {
    case P_DISPLAY: {
        static const char *const names[] = {
            "none", "block", "flow-root", "inline", "inline-block", "list-item", "flex", "-webkit-box",
            "-webkit-flex", "-ms-flexbox", "inline-flex", "-webkit-inline-box", "grid", "inline-grid",
            "table", "inline-table", "table-row", "table-cell", "table-row-group",
            "table-header-group", "table-footer-group", "table-caption", "table-column",
            "table-column-group", "contents", "ruby", "run-in", NULL };
        static const uint8_t values[] = {
            CSS_D_NONE, CSS_D_BLOCK, CSS_D_FLOW_ROOT, CSS_D_INLINE, CSS_D_INLINE_BLOCK, CSS_D_LIST_ITEM,
            CSS_D_FLEX, CSS_D_FLEX, CSS_D_FLEX, CSS_D_FLEX, CSS_D_INLINE_FLEX, CSS_D_INLINE_FLEX,
            CSS_D_GRID, CSS_D_INLINE_GRID, CSS_D_TABLE, CSS_D_INLINE_TABLE, CSS_D_TABLE_ROW,
            CSS_D_TABLE_CELL, CSS_D_TABLE_GROUP, CSS_D_TABLE_GROUP, CSS_D_TABLE_GROUP,
            CSS_D_TABLE_CAPTION, CSS_D_TABLE_COLUMN, CSS_D_TABLE_COLUMN, CSS_D_CONTENTS, CSS_D_INLINE,
            CSS_D_INLINE };
        uint8_t kind = 0;
        while (next_piece(&p, end, 0, &piece, &n)) {
            uint8_t one = keyword(piece, n, names, values);
            if (one)
                kind = one;             /* "block flex": das letzte Wort sagt, wie die Kinder stehen */
        }
        if (kind && BOX_SET(L_DISPLAY))
            s->display_kind = kind;
        break;
    }
    case P_POSITION: {
        static const char *const names[] = { "static", "relative", "absolute", "fixed", "sticky",
                                             "-webkit-sticky", NULL };
        static const uint8_t values[] = { CSS_POS_STATIC, CSS_POS_RELATIVE, CSS_POS_ABSOLUTE,
                                          CSS_POS_FIXED, CSS_POS_STICKY, CSS_POS_STICKY };
        uint8_t kind = keyword(text, length, names, values);
        if (kind && BOX_SET(L_POSITION))
            s->position = kind;
        break;
    }
    case P_BOX_SIZING:
        if ((word_is(text, length, "border-box") || word_is(text, length, "content-box")) &&
            BOX_SET(L_BOX_SIZING))
            s->box_sizing = lower(text[0]) == 'b' ? CSS_BOX_BORDER : CSS_BOX_CONTENT;
        break;
    case P_WHITE_SPACE: {
        static const char *const names[] = { "normal", "nowrap", "pre", "pre-wrap", "pre-line",
                                             "break-spaces", "collapse", NULL };
        static const uint8_t values[] = { CSS_WS_NORMAL, CSS_WS_NOWRAP, CSS_WS_PRE, CSS_WS_PRE_WRAP,
                                          CSS_WS_PRE_LINE, CSS_WS_PRE_WRAP, CSS_WS_NORMAL };
        uint8_t kind = keyword(text, length, names, values);
        if (kind && BOX_SET(L_WHITE_SPACE))
            s->white_space = kind;
        break;
    }
    case P_VALIGN: {
        static const char *const names[] = { "baseline", "top", "text-top", "middle", "bottom",
                                             "text-bottom", "sub", "super", NULL };
        static const uint8_t values[] = { CSS_VA_BASELINE, CSS_VA_TOP, CSS_VA_TOP, CSS_VA_MIDDLE,
                                          CSS_VA_BOTTOM, CSS_VA_BOTTOM, CSS_VA_SUB, CSS_VA_SUPER };
        uint8_t kind = keyword(text, length, names, values);
        if (kind && BOX_SET(L_VALIGN))
            s->vertical_align = kind;
        break;
    }
    case P_TRANSFORM_TEXT: {
        static const char *const names[] = { "none", "uppercase", "lowercase", "capitalize", NULL };
        static const uint8_t values[] = { CSS_TT_NONE, CSS_TT_UPPER, CSS_TT_LOWER, CSS_TT_CAPITALIZE };
        uint8_t kind = keyword(text, length, names, values);
        if (kind && BOX_SET(L_TRANSFORM))
            s->text_transform = kind;
        break;
    }
    case P_OVERFLOW: {
        static const char *const names[] = { "visible", "hidden", "clip", "auto", "scroll", "overlay", NULL };
        static const uint8_t values[] = { CSS_OV_VISIBLE, CSS_OV_HIDDEN, CSS_OV_HIDDEN, CSS_OV_SCROLL,
                                          CSS_OV_SCROLL, CSS_OV_SCROLL };
        uint8_t kind = 0;
        while (next_piece(&p, end, 0, &piece, &n)) {
            uint8_t one = keyword(piece, n, names, values);
            if (one > kind)
                kind = one;
        }
        if (kind && BOX_SET(L_OVERFLOW))
            s->overflow = kind;
        break;
    }
    case P_WEIGHT: {
        const char *q = text, *unit;
        size_t unit_length;
        int64_t value;
        uint8_t weight = 0;
        if (word_is(text, length, "bold") || word_is(text, length, "bolder"))
            weight = CSS_YES;
        else if (word_is(text, length, "normal") || word_is(text, length, "lighter"))
            weight = CSS_NO;
        else if (number(&q, end, &value, &unit, &unit_length))
            weight = value >= 600000 ? CSS_YES : CSS_NO;
        if (weight && BOX_SET(L_WEIGHT))
            s->font_weight = weight;
        break;
    }
    case P_STYLE:
        if ((word_is(text, length, "italic") || contains_word(text, length, "oblique") ||
             word_is(text, length, "normal")) && BOX_SET(L_SLANT))
            s->font_slant = word_is(text, length, "normal") ? CSS_NO : CSS_YES;
        break;
    case P_SIZE:
        if (font_size_len(sheet, text, length, &len) && BOX_SET(L_FONT_SIZE))
            s->font_size = len;
        break;
    case P_FAMILY:
        if (BOX_SET(L_FAMILY))
            s->font_family = family_is_mono(text, length) ? CSS_FF_MONO : CSS_FF_SANS;
        break;
    case P_LINE_HEIGHT:
        if (parse_len(sheet, text, length, LEN_NORMAL | LEN_NUMBER_EM | LEN_PERCENT_EM | LEN_POSITIVE, &len) &&
            BOX_SET(L_LINE_HEIGHT))
            s->line_height = len;
        break;
    case P_FONT: {
        /* font: [kursiv] [fett] größe[/zeilenhöhe] schrift - vor der Größe stehen nur Wörter */
        uint8_t weight = CSS_NO, slant = CSS_NO;
        css_len_t size, height;
        bool found = false;
        memset(&height, 0, sizeof(height));
        height.kind = CSS_LEN_AUTO;
        while (!found && next_piece(&p, end, 0, &piece, &n)) {
            size_t slash = 0;
            while (slash < n && piece[slash] != '/')
                slash++;
            if (word_is(piece, n, "bold") || word_is(piece, n, "bolder")) {
                weight = CSS_YES;
            } else if (word_is(piece, n, "italic") || word_is(piece, n, "oblique")) {
                slant = CSS_YES;
            } else if (n == 3 && piece[0] >= '1' && piece[0] <= '9' && piece[1] == '0' && piece[2] == '0') {
                weight = piece[0] >= '6' ? CSS_YES : CSS_NO;
            } else if (font_size_len(sheet, piece, slash, &size)) {
                found = true;
                if (slash < n)
                    parse_len(sheet, piece + slash + 1, n - slash - 1,
                              LEN_NORMAL | LEN_NUMBER_EM | LEN_PERCENT_EM | LEN_POSITIVE, &height);
            } else if (!word_is(piece, n, "normal") && !word_is(piece, n, "small-caps") &&
                       !word_is(piece, n, "lighter") && !has_word_starting(piece, n, "condensed") &&
                       !has_word_starting(piece, n, "expanded")) {
                return;                 /* "caption", "menu" ...: Schriften des Systems */
            }
        }
        if (!found)
            return;
        if (BOX_SET(L_FONT_SIZE))
            s->font_size = size;
        if (BOX_SET(L_LINE_HEIGHT))
            s->line_height = height;
        if (BOX_SET(L_WEIGHT))
            s->font_weight = weight;
        if (BOX_SET(L_SLANT))
            s->font_slant = slant;
        if (p < end && BOX_SET(L_FAMILY))
            s->font_family = family_is_mono(p, (size_t)(end - p)) ? CSS_FF_MONO : CSS_FF_SANS;
        break;
    }
    case P_LIST: {
        static const char *const names[] = { "none", "disc", "circle", "square", "decimal",
                                             "decimal-leading-zero", "lower-alpha", "lower-latin",
                                             "upper-alpha", "upper-latin", "lower-roman", "upper-roman",
                                             NULL };
        static const uint8_t values[] = { CSS_LT_NONE, CSS_LT_DISC, CSS_LT_CIRCLE, CSS_LT_SQUARE,
                                          CSS_LT_DECIMAL, CSS_LT_DECIMAL, CSS_LT_LOWER_ALPHA,
                                          CSS_LT_LOWER_ALPHA, CSS_LT_UPPER_ALPHA, CSS_LT_UPPER_ALPHA,
                                          CSS_LT_LOWER_ROMAN, CSS_LT_UPPER_ROMAN };
        uint8_t kind = 0;
        while (next_piece(&p, end, 0, &piece, &n)) {
            uint8_t one = keyword(piece, n, names, values);
            if (one && (!kind || one != CSS_LT_NONE))
                kind = one;
        }
        if (kind && BOX_SET(L_LIST_TYPE))
            s->list_type = kind;
        break;
    }
    case P_COLLAPSE:
        if ((word_is(text, length, "collapse") || word_is(text, length, "separate")) && BOX_SET(L_COLLAPSE))
            s->border_collapse = lower(text[0]) == 'c' ? CSS_YES : CSS_NO;
        break;
    case P_SPACING:
        if (next_piece(&p, end, 0, &piece, &n) && parse_len(sheet, piece, n, LEN_POSITIVE, &len) &&
            BOX_SET(L_SPACING))
            s->border_spacing = len;
        break;
    case P_FLEX_DIRECTION: case P_FLEX_WRAP: case P_FLEX_FLOW:
        while (next_piece(&p, end, 0, &piece, &n)) {
            static const char *const names[] = { "row", "row-reverse", "column", "column-reverse", NULL };
            static const uint8_t values[] = { CSS_FD_ROW, CSS_FD_ROW_REVERSE, CSS_FD_COLUMN,
                                              CSS_FD_COLUMN_REVERSE };
            uint8_t direction = property != P_FLEX_WRAP ? keyword(piece, n, names, values) : 0;
            if (direction && BOX_SET(L_DIRECTION))
                s->flex_direction = direction;
            if (property != P_FLEX_DIRECTION && (word_is(piece, n, "wrap") || word_is(piece, n, "nowrap") ||
                                                 word_is(piece, n, "wrap-reverse")) && BOX_SET(L_WRAP))
                s->flex_wrap = lower(piece[0]) == 'n' ? CSS_FW_NOWRAP : CSS_FW_WRAP;
        }
        break;
    case P_JUSTIFY: {
        uint8_t kind = 0;
        while (!kind && next_piece(&p, end, 0, &piece, &n))
            kind = justify_word(piece, n);
        if (kind && BOX_SET(L_JUSTIFY))
            s->justify_content = kind;
        break;
    }
    case P_ALIGN_ITEMS: case P_ALIGN_SELF: case P_ALIGN_CONTENT: case P_PLACE_ITEMS:
    case P_PLACE_CONTENT: {
        uint8_t kind = 0, second = 0;
        while (next_piece(&p, end, 0, &piece, &n)) {
            if (!kind)
                kind = property == P_ALIGN_CONTENT || property == P_PLACE_CONTENT
                           ? (align_word(piece, n) ? align_word(piece, n)
                              : justify_word(piece, n) >= CSS_JU_BETWEEN ? CSS_AL_STRETCH : 0)
                           : align_word(piece, n);
            else if (!second)
                second = justify_word(piece, n);
        }
        if (!kind)
            break;
        if ((property == P_ALIGN_ITEMS || property == P_PLACE_ITEMS) && BOX_SET(L_ALIGN_ITEMS))
            s->align_items = kind;
        if (property == P_ALIGN_SELF && BOX_SET(L_ALIGN_SELF))
            s->align_self = kind;
        if ((property == P_ALIGN_CONTENT || property == P_PLACE_CONTENT) && BOX_SET(L_ALIGN_CONTENT))
            s->align_content = kind;
        if (property == P_PLACE_CONTENT) {
            /* place-content: center - auch in der Reihe mittig */
            if (!second)
                second = justify_word(text, length);
            if (second && BOX_SET(L_JUSTIFY))
                s->justify_content = second;
        }
        break;
    }
    case P_FLEX: {
        /* flex: none | auto | wachsen [schrumpfen] [grundbreite] */
        int64_t grow = 0, shrink = 1000;
        css_len_t basis;
        memset(&basis, 0, sizeof(basis));
        basis.kind = CSS_LEN_VALUE;     /* ohne Angabe: 0 */
        if (word_is(text, length, "none")) {
            shrink = 0;
            basis.kind = CSS_LEN_AUTO;
        } else if (word_is(text, length, "auto")) {
            grow = 1000;
            basis.kind = CSS_LEN_AUTO;
        } else {
            int numbers = 0;
            bool any = false;
            while (next_piece(&p, end, 0, &piece, &n)) {
                const char *q = piece, *unit;
                size_t unit_length;
                int64_t value;
                if (number(&q, piece + n, &value, &unit, &unit_length) && q == piece + n &&
                    unit_length == 0 && numbers < 2) {
                    if (numbers++ == 0)
                        grow = value;
                    else
                        shrink = value;
                } else if (!parse_len(sheet, piece, n, LEN_AUTO | LEN_CONTENT | LEN_NUMBER_PX, &basis)) {
                    return;
                }
                any = true;
            }
            if (!any)
                return;
        }
        if (BOX_SET(L_GROW)) {
            s->flex_grow = fit32(grow);
            s->grow_set = true;
        }
        if (BOX_SET(L_SHRINK)) {
            s->flex_shrink = fit32(shrink);
            s->shrink_set = true;
        }
        if (BOX_SET(L_BASIS))
            s->flex_basis = basis;
        break;
    }
    case P_FLEX_GROW: case P_FLEX_SHRINK: case P_ORDER: case P_ZINDEX: {
        const char *q = text, *unit;
        size_t unit_length;
        int64_t value;
        if (!number(&q, end, &value, &unit, &unit_length) || unit_length)
            break;
        if (property == P_FLEX_GROW && BOX_SET(L_GROW)) {
            s->flex_grow = fit32(value);
            s->grow_set = true;
        } else if (property == P_FLEX_SHRINK && BOX_SET(L_SHRINK)) {
            s->flex_shrink = fit32(value);
            s->shrink_set = true;
        } else if (property == P_ORDER && BOX_SET(L_ORDER)) {
            s->order = fit32(value / 1000);
            s->order_set = true;
        } else if (property == P_ZINDEX && BOX_SET(L_ZINDEX)) {
            s->z_index = fit32(value / 1000);
            s->z_set = true;
        }
        break;
    }
    case P_FLEX_BASIS:
        if (parse_len(sheet, text, length, LEN_AUTO | LEN_CONTENT | LEN_POSITIVE, &len) && BOX_SET(L_BASIS))
            s->flex_basis = len;
        break;
    case P_GAP: case P_ROW_GAP: case P_COLUMN_GAP: {
        css_len_t gaps[2];
        int count = 0;
        while (count < 2 && next_piece(&p, end, 0, &piece, &n)) {
            if (word_is(piece, n, "normal")) {
                memset(&gaps[count], 0, sizeof(gaps[0]));
                gaps[count].kind = CSS_LEN_VALUE;
            } else if (!parse_len(sheet, piece, n, LEN_POSITIVE, &gaps[count])) {
                return;
            }
            count++;
        }
        if (!count)
            break;
        if (property != P_COLUMN_GAP && BOX_SET(L_GAP_ROW))
            s->gap_row = gaps[0];
        if (property != P_ROW_GAP && BOX_SET(L_GAP_COLUMN))
            s->gap_column = gaps[property == P_GAP && count > 1 ? 1 : 0];
        break;
    }
    case P_GRID_AREAS:
        if (word_is(text, length, "none")) {
            if (BOX_SET(L_AREAS))
                s->area_rows = s->area_columns = 0;
        } else {
            css_style_t probe;
            if (grid_areas(text, length, &probe) && BOX_SET(L_AREAS)) {
                memcpy(s->area_map, probe.area_map, sizeof(s->area_map));
                s->area_rows = probe.area_rows;
                s->area_columns = probe.area_columns;
            }
        }
        break;
    case P_GRID_AREA: {
        /* der Name einer Fläche - oder "Zeile / Spalte / bis Zeile / bis Spalte" */
        if (lower(text[0]) >= 'a' && lower(text[0]) <= 'z' && !word_is(text, length, "auto")) {
            if (next_piece(&p, end, '/', &piece, &n) && BOX_SET(L_GRID_AREA))
                s->grid_area = area_hash(piece, n);
        }
        break;
    }
    case P_CAPTION_SIDE:
        if (BOX_SET(L_CAPTION))
            s->caption_below = has_word_starting(text, length, "bottom") ||
                               word_is(text, length, "block-end") ? CSS_YES : CSS_NO;
        break;
    case P_MASK:
        if (BOX_SET(L_MASK))
            s->masked = word_is(text, length, "none") ? CSS_NO : CSS_YES;
        break;
    case P_GRID_COLUMNS: case P_GRID_TEMPLATE: {
        const char *from = text;
        if (property == P_GRID_TEMPLATE) {
            /* "Zeilen / Spalten" - die Zeilen auch als benannte Flächen in Anführungszeichen */
            const char *slash = NULL;
            bool named = false;
            char quote = 0;
            for (const char *q = text; q < end; q++) {
                if (quote) {
                    if (*q == quote)
                        quote = 0;
                } else if (*q == '"' || *q == '\'') {
                    quote = *q;
                    named = true;
                } else if (*q == '/') {
                    slash = q;
                }
            }
            if (named) {
                css_style_t probe;
                if (grid_areas(text, slash ? (size_t)(slash - text) : length, &probe) && BOX_SET(L_AREAS)) {
                    memcpy(s->area_map, probe.area_map, sizeof(s->area_map));
                    s->area_rows = probe.area_rows;
                    s->area_columns = probe.area_columns;
                }
            }
            if (!slash)
                return;
            from = slash + 1;
        }
        css_style_t probe;
        probe.grid_count = 0;
        probe.grid_auto_fill = false;
        if (grid_template(sheet, from, (size_t)(end - from), &probe) && BOX_SET(L_GRID)) {
            memcpy(s->grid, probe.grid, sizeof(s->grid[0]) * probe.grid_count);
            s->grid_count = probe.grid_count;
            s->grid_auto_fill = probe.grid_auto_fill;
            s->grid_set = true;
        }
        break;
    }
    case P_GRID_COLUMN: case P_GRID_ROW: {
        /* "span 2", "1 / 3", "1 / -1", "2 / span 3" */
        long first = 0, second = 0, span = 0;
        bool slash = false;
        while (next_piece(&p, end, '/', &piece, &n)) {
            if (n == 1 && piece[0] == '/') {
                slash = true;
                continue;
            }
            if (word_is(piece, n, "span")) {
                if (next_piece(&p, end, '/', &piece, &n)) {
                    for (size_t i = 0; i < n && piece[i] >= '0' && piece[i] <= '9'; i++)
                        span = span * 10 + (piece[i] - '0');
                }
                continue;
            }
            long value = 0;
            bool negative = piece[0] == '-';
            for (size_t i = negative ? 1 : 0; i < n && piece[i] >= '0' && piece[i] <= '9'; i++)
                value = value * 10 + (piece[i] - '0');
            if (slash)
                second = negative ? -value : value;
            else
                first = negative ? -value : value;
        }
        if (!span && second > first && first > 0)
            span = second - first;
        if (property == P_GRID_ROW) {
            if (BOX_SET(L_GRID_ROW)) {
                s->grid_row = (uint8_t)(first > 0 && first < 200 ? first : 0);
                s->grid_row_span = (uint8_t)(span > 0 && span < 200 ? span : 0);
            }
            break;
        }
        if (!BOX_SET(L_GRID_ITEM))
            break;
        s->grid_full = second < 0 && first <= 1;
        s->grid_span = (uint8_t)(span > 0 && span < 200 ? span : 0);
        s->grid_column = (uint8_t)(first > 0 && first < 200 ? first : 0);
        break;
    }
    case P_MARGIN: case P_PADDING: case P_INSET: case P_BORDER_WIDTH: {
        unsigned allow = property == P_MARGIN ? LEN_AUTO | LEN_NUMBER_PX
                       : property == P_INSET ? LEN_AUTO | LEN_NUMBER_PX
                       : property == P_BORDER_WIDTH ? LEN_POSITIVE | LEN_THICKNESS | LEN_NUMBER_PX
                       : LEN_POSITIVE | LEN_NUMBER_PX;
        int base = property == P_MARGIN ? L_MARGIN : property == P_PADDING ? L_PADDING
                 : property == P_INSET ? L_INSET : L_BORDER_WIDTH;
        css_len_t *to = property == P_MARGIN ? s->margin : property == P_PADDING ? s->padding
                      : property == P_INSET ? s->inset : s->border_width;
        if (!four_lens(sheet, text, length, allow, four))
            break;
        for (int i = 0; i < 4; i++) {
            if (BOX_SET(base + i))
                to[i] = four[i];
        }
        break;
    }
    case P_MARGIN_TOP: case P_MARGIN_RIGHT: case P_MARGIN_BOTTOM: case P_MARGIN_LEFT:
        if (parse_len(sheet, text, length, LEN_AUTO | LEN_NUMBER_PX, &len) &&
            BOX_SET(L_MARGIN + property - P_MARGIN_TOP))
            s->margin[property - P_MARGIN_TOP] = len;
        break;
    case P_MARGIN_BLOCK: case P_MARGIN_INLINE: case P_PADDING_BLOCK: case P_PADDING_INLINE: {
        /* Anfang und Ende in Lese- bzw. Stapelrichtung: hier oben/unten und links/rechts */
        bool margin = property == P_MARGIN_BLOCK || property == P_MARGIN_INLINE;
        bool block = property == P_MARGIN_BLOCK || property == P_PADDING_BLOCK;
        if (!four_lens(sheet, text, length, margin ? LEN_AUTO | LEN_NUMBER_PX : LEN_POSITIVE | LEN_NUMBER_PX, four))
            break;
        int a = block ? 0 : 3, b = block ? 2 : 1;
        css_len_t *to = margin ? s->margin : s->padding;
        if (BOX_SET((margin ? L_MARGIN : L_PADDING) + a))
            to[a] = four[0];
        if (BOX_SET((margin ? L_MARGIN : L_PADDING) + b))
            to[b] = four[1];
        break;
    }
    case P_PADDING_TOP: case P_PADDING_RIGHT: case P_PADDING_BOTTOM: case P_PADDING_LEFT:
        if (parse_len(sheet, text, length, LEN_POSITIVE | LEN_NUMBER_PX, &len) &&
            BOX_SET(L_PADDING + property - P_PADDING_TOP))
            s->padding[property - P_PADDING_TOP] = len;
        break;
    case P_TOP: case P_RIGHT: case P_BOTTOM: case P_LEFT:
        if (parse_len(sheet, text, length, LEN_AUTO | LEN_NUMBER_PX, &len) &&
            BOX_SET(L_INSET + property - P_TOP))
            s->inset[property - P_TOP] = len;
        break;
    case P_WIDTH: case P_HEIGHT: case P_MIN_WIDTH: case P_MAX_WIDTH: case P_MIN_HEIGHT:
    case P_MAX_HEIGHT: {
        bool limit = property == P_MAX_WIDTH || property == P_MAX_HEIGHT;
        if (!parse_len(sheet, text, length,
                       (limit ? LEN_NONE : LEN_AUTO) | LEN_CONTENT | LEN_POSITIVE | LEN_NUMBER_PX, &len))
            break;
        int id = property == P_WIDTH ? L_WIDTH : property == P_HEIGHT ? L_HEIGHT
               : property == P_MIN_WIDTH ? L_MIN_WIDTH : property == P_MAX_WIDTH ? L_MAX_WIDTH
               : property == P_MIN_HEIGHT ? L_MIN_HEIGHT : L_MAX_HEIGHT;
        css_len_t *to = property == P_WIDTH ? &s->width : property == P_HEIGHT ? &s->height
                      : property == P_MIN_WIDTH ? &s->min_width : property == P_MAX_WIDTH ? &s->max_width
                      : property == P_MIN_HEIGHT ? &s->min_height : &s->max_height;
        if (BOX_SET(id))
            *to = len;
        break;
    }
    case P_INDENT:
        if (next_piece(&p, end, 0, &piece, &n) && parse_len(sheet, piece, n, LEN_NUMBER_PX, &len) &&
            BOX_SET(L_INDENT))
            s->text_indent = len;
        break;
    case P_RADIUS:
        if (next_piece(&p, end, '/', &piece, &n) && parse_len(sheet, piece, n, LEN_POSITIVE | LEN_NUMBER_PX, &len) &&
            BOX_SET(L_RADIUS))
            s->radius = len;
        break;
    case P_BORDER: case P_BORDER_TOP: case P_BORDER_RIGHT: case P_BORDER_BOTTOM: case P_BORDER_LEFT: {
        /* Breite, Art und Farbe in beliebiger Reihenfolge; was fehlt, gilt wie üblich */
        css_len_t width;
        uint8_t style = CSS_BS_NONE, kind = CSS_COLOR_CURRENT;
        memset(&width, 0, sizeof(width));
        width.kind = CSS_LEN_VALUE;
        width.px = 3000;
        color = 0;
        while (next_piece(&p, end, 0, &piece, &n)) {
            uint8_t one = border_style_of(piece, n), ck;
            uint32_t rgb;
            if (one)
                style = one;
            else if (parse_len(sheet, piece, n, LEN_POSITIVE | LEN_THICKNESS | LEN_NUMBER_PX, &len))
                width = len;
            else if ((ck = color_kind(piece, n, &rgb)) != 0) {
                kind = ck;
                color = rgb;
            } else {
                return;
            }
        }
        border_parts(c, priority, property == P_BORDER ? 15 : 1u << (property - P_BORDER_TOP), &width,
                     style, kind, color);
        break;
    }
    case P_BORDER_STYLE: {
        uint8_t v[4];
        int count = 0;
        while (count < 4 && next_piece(&p, end, 0, &piece, &n)) {
            v[count] = border_style_of(piece, n);
            if (!v[count])
                return;
            count++;
        }
        if (!count)
            break;
        border_parts(c, priority, 1, NULL, v[0], 0, 0);
        border_parts(c, priority, 2, NULL, count > 1 ? v[1] : v[0], 0, 0);
        border_parts(c, priority, 4, NULL, count > 2 ? v[2] : v[0], 0, 0);
        border_parts(c, priority, 8, NULL, count > 3 ? v[3] : count > 1 ? v[1] : v[0], 0, 0);
        break;
    }
    case P_BORDER_COLOR: {
        uint8_t kinds[4];
        uint32_t colors[4] = { 0, 0, 0, 0 };
        int count = 0;
        while (count < 4 && next_piece(&p, end, 0, &piece, &n)) {
            kinds[count] = color_kind(piece, n, &colors[count]);
            if (!kinds[count])
                return;
            count++;
        }
        if (!count)
            break;
        for (int i = 0; i < 4; i++) {
            int from = i == 0 ? 0 : i == 1 ? (count > 1 ? 1 : 0) : i == 2 ? (count > 2 ? 2 : 0)
                     : count > 3 ? 3 : count > 1 ? 1 : 0;
            border_parts(c, priority, 1u << i, NULL, 0, kinds[from], colors[from]);
        }
        break;
    }
    case P_BORDER_TOP_WIDTH: case P_BORDER_RIGHT_WIDTH: case P_BORDER_BOTTOM_WIDTH:
    case P_BORDER_LEFT_WIDTH:
        if (parse_len(sheet, text, length, LEN_POSITIVE | LEN_THICKNESS | LEN_NUMBER_PX, &len))
            border_parts(c, priority, 1u << (property - P_BORDER_TOP_WIDTH), &len, 0, 0, 0);
        break;
    case P_BORDER_TOP_STYLE: case P_BORDER_RIGHT_STYLE: case P_BORDER_BOTTOM_STYLE:
    case P_BORDER_LEFT_STYLE:
        if (border_style_of(text, length))
            border_parts(c, priority, 1u << (property - P_BORDER_TOP_STYLE), NULL,
                         border_style_of(text, length), 0, 0);
        break;
    case P_BORDER_TOP_COLOR: case P_BORDER_RIGHT_COLOR: case P_BORDER_BOTTOM_COLOR:
    case P_BORDER_LEFT_COLOR: {
        uint8_t kind = color_kind(text, length, &color);
        if (kind)
            border_parts(c, priority, 1u << (property - P_BORDER_TOP_COLOR), NULL, 0, kind, color);
        break;
    }
    case P_BREAK: case P_WORD_BREAK:
        if (BOX_SET(L_BREAK))
            s->break_words = word_is(text, length, "anywhere") || word_is(text, length, "break-word") ||
                             word_is(text, length, "break-all") ? CSS_YES : CSS_NO;
        break;
    case P_OPACITY: {
        const char *q = text, *unit;
        size_t unit_length;
        int64_t value;
        if (number(&q, end, &value, &unit, &unit_length) && BOX_SET(L_OPACITY))
            s->transparent = value == 0 ? CSS_YES : CSS_NO;
        break;
    }
    case P_BACKGROUND: case P_BACKGROUND_IMAGE: {
        size_t url_length = 0;
        const char *url = url_inside(text, length, &url_length);
        uint32_t last = 0;
        bool across = false;
        bool gradient = gradient_color(text, length, &color, &last, &across);
        if (BOX_SET(L_BG_IMAGE)) {
            s->background_image = url || gradient ? CSS_YES : CSS_NO;
            s->gradient_set = gradient;
            s->gradient_color = gradient ? color : 0;
            s->gradient_end = gradient ? last : 0;
            s->gradient_across = gradient && across;
            /* die Adresse nur, wenn ihr Text bestehen bleibt (nicht aus einer Variablen zusammengesetzt) */
            s->background_url = url && lasting ? url : NULL;
            s->background_url_length = url && lasting ? (uint16_t)url_length : 0;
        }
        if (property == P_BACKGROUND) {
            /* die Kurzschreibweise nennt auch Wiederholung, Lage und - hinter "/" - Größe */
            const char *spots[2], *sizes[2];
            size_t spot_lengths[2], size_lengths[2];
            int spot_count = 0, size_count = 0;
            uint8_t repeat = CSS_REPEAT_BOTH;
            bool after_slash = false;
            while (next_piece(&p, end, '/', &piece, &n)) {
                uint32_t rgb;
                if (n == 1 && piece[0] == '/') {
                    after_slash = true;
                } else if (piece[n - 1] == ')' || css_color(piece, n, &rgb) || word_is(piece, n, "transparent")) {
                    continue;           /* Bild, Verlauf oder Farbe */
                } else if (word_is(piece, n, "no-repeat")) {
                    repeat = CSS_REPEAT_NONE;
                } else if (word_is(piece, n, "repeat-x") || word_is(piece, n, "repeat-y")) {
                    repeat = lower(piece[7]) == 'x' ? CSS_REPEAT_X : CSS_REPEAT_Y;
                } else if (word_is(piece, n, "repeat") || word_is(piece, n, "scroll") || word_is(piece, n, "fixed") ||
                           word_is(piece, n, "none") || has_word_starting(piece, n, "border") ||
                           has_word_starting(piece, n, "padding") || has_word_starting(piece, n, "content")) {
                    continue;
                } else if (after_slash && size_count < 2) {
                    sizes[size_count] = piece;
                    size_lengths[size_count++] = n;
                } else if (!after_slash && spot_count < 2) {
                    spots[spot_count] = piece;
                    spot_lengths[spot_count++] = n;
                }
            }
            if (BOX_SET(L_BG_REPEAT))
                s->background_repeat = repeat;
            if (BOX_SET(L_BG_POSITION)) {
                css_len_t x = { CSS_LEN_VALUE, 0, 0, 0 }, y = x;
                if (spot_count)
                    position_pair(sheet, spots, spot_lengths, spot_count, &x, &y);
                s->background_x = x;
                s->background_y = y;
            }
            if (BOX_SET(L_BG_SIZE)) {
                s->background_size = CSS_BG_AUTO;
                if (size_count && word_is(sizes[0], size_lengths[0], "cover")) {
                    s->background_size = CSS_BG_COVER;
                } else if (size_count && word_is(sizes[0], size_lengths[0], "contain")) {
                    s->background_size = CSS_BG_CONTAIN;
                } else if (size_count) {
                    s->background_size = CSS_BG_GIVEN;
                    if (!parse_len(sheet, sizes[0], size_lengths[0], LEN_AUTO | LEN_NUMBER_PX, &s->background_w))
                        s->background_w.kind = CSS_LEN_AUTO;
                    if (size_count < 2 || !parse_len(sheet, sizes[1], size_lengths[1], LEN_AUTO | LEN_NUMBER_PX, &s->background_h))
                        s->background_h.kind = CSS_LEN_AUTO;
                }
            }
        }
        break;
    }
    case P_BG_REPEAT:
        if (BOX_SET(L_BG_REPEAT))
            s->background_repeat = word_is(text, length, "no-repeat") ? CSS_REPEAT_NONE
                                 : word_is(text, length, "repeat-x") ? CSS_REPEAT_X
                                 : word_is(text, length, "repeat-y") ? CSS_REPEAT_Y : CSS_REPEAT_BOTH;
        break;
    case P_BG_POSITION: {
        const char *spots[2];
        size_t spot_lengths[2];
        int count = 0;
        while (count < 2 && next_piece(&p, end, ',', &piece, &n)) {
            if (n == 1 && piece[0] == ',')
                break;                  /* mehrere Bilder: Das erste zählt */
            spots[count] = piece;
            spot_lengths[count++] = n;
        }
        css_len_t x, y;
        if (position_pair(sheet, spots, spot_lengths, count, &x, &y) && BOX_SET(L_BG_POSITION)) {
            s->background_x = x;
            s->background_y = y;
        }
        break;
    }
    case P_BG_SIZE: {
        uint8_t kind = word_is(text, length, "cover") ? CSS_BG_COVER : word_is(text, length, "contain") ? CSS_BG_CONTAIN
                     : word_is(text, length, "auto") ? CSS_BG_AUTO : CSS_BG_GIVEN;
        css_len_t w = { CSS_LEN_AUTO, 0, 0, 0 }, h = w;
        if (kind == CSS_BG_GIVEN) {
            if (!next_piece(&p, end, ',', &piece, &n) || !parse_len(sheet, piece, n, LEN_AUTO | LEN_NUMBER_PX, &w))
                break;
            if (next_piece(&p, end, ',', &piece, &n) && !(n == 1 && piece[0] == ','))
                parse_len(sheet, piece, n, LEN_AUTO | LEN_NUMBER_PX, &h);
        }
        if (BOX_SET(L_BG_SIZE)) {
            s->background_size = kind;
            s->background_w = w;
            s->background_h = h;
        }
        break;
    }
    default:
        break;
    }
}

static void apply(const css_sheet_t *sheet, cascade_t *c, uint8_t property, const char *raw,
                  size_t raw_length, uint64_t priority)
{
    /* Zwei Schreibweisen für dasselbe teilen sich die Rangfolge */
    uint8_t slot = property == P_BACKGROUND_COLOR ? P_BACKGROUND
                 : property == P_CLIP_PATH ? P_CLIP : property;
    if (!c->boxes && (property >= P_POSITION || priority < c->priority[slot]))
        return;
    if (property == P_CONTENT) {
        /* der Inhalt eines Scheinelements: wird so festgehalten, wie er dasteht */
        if (priority >= c->content_priority) {
            c->content = raw;
            c->content_length = raw_length;
            c->content_priority = priority;
        }
        return;
    }

    char resolved[VALUE_MAX];
    const char *text = raw;
    size_t length = raw_length;
    for (size_t i = 0; i + 4 <= raw_length; i++) {
        if (raw[i] == '(' && i >= 3 && lower(raw[i - 1]) == 'r' && lower(raw[i - 2]) == 'a' &&
            lower(raw[i - 3]) == 'v') {
            if (!resolve_vars(sheet, raw, raw_length, resolved, sizeof(resolved), 0))
                return;
            text = resolved;
            length = strlen(resolved);
            break;
        }
    }
    trim(&text, &length);
    if (word_is(text, length, "inherit") || word_is(text, length, "unset")) {
        /*
         * Bei Angaben, die ohnehin vom umgebenden Element kommen (Schrift,
         * Textfarbe), heißt das: Was schwächere Regeln festgelegt haben, gilt
         * nicht mehr. Bei allen anderen bleibt es dabei: wie nicht angegeben.
         */
        css_style_t *s = c->style;
        if (property == P_COLOR && priority >= c->priority[slot]) {
            s->has &= ~(uint32_t)CSS_COLOR;
            s->color_inherited = true;
            c->priority[slot] = priority;
        }
        if (c->boxes) {
            bool all = property == P_FONT;
            if ((all || property == P_SIZE) && BOX_SET(L_FONT_SIZE))
                memset(&s->font_size, 0, sizeof(s->font_size));
            if ((all || property == P_WEIGHT) && BOX_SET(L_WEIGHT))
                s->font_weight = 0;
            if ((all || property == P_STYLE) && BOX_SET(L_SLANT))
                s->font_slant = 0;
            if ((all || property == P_FAMILY) && BOX_SET(L_FAMILY))
                s->font_family = 0;
            if ((all || property == P_LINE_HEIGHT) && BOX_SET(L_LINE_HEIGHT))
                memset(&s->line_height, 0, sizeof(s->line_height));
        }
        return;
    }
    if (!length || word_is(text, length, "initial") || word_is(text, length, "revert"))
        return;                         /* gilt wie nicht angegeben */
    if (c->boxes) {
        apply_box(sheet, c, property, text, length, priority, text != resolved);
        if (property >= P_POSITION || priority < c->priority[slot])
            return;
    }

    css_style_t *s = c->style;
    uint32_t color;
    switch (property) {
    case P_COLOR:
        if (!css_color(text, length, &color))
            return;
        s->color = color;
        s->color_inherited = false;
        s->has |= CSS_COLOR;
        break;
    case P_BACKGROUND:
    case P_BACKGROUND_COLOR: {
        bool image = false;
        if (property == P_BACKGROUND_COLOR ? css_color(text, length, &color)
                                           : find_color(text, length, &color, &image)) {
            s->background = color;
            s->background_clear = false;
            s->has |= CSS_BACKGROUND;
        } else if (word_is(text, length, "transparent") || word_is(text, length, "none") ||
                   image) {
            /* kein Hintergrund - oder ein Bild, das wir nicht zeigen */
            s->background_clear = true;
            s->has &= ~(uint32_t)CSS_BACKGROUND;
        } else {
            return;
        }
        break;
    }
    case P_WEIGHT: {
        const char *p = text, *unit;
        size_t unit_length;
        int64_t value;
        if (word_is(text, length, "bold") || word_is(text, length, "bolder"))
            s->bold = true;
        else if (word_is(text, length, "normal") || word_is(text, length, "lighter"))
            s->bold = false;
        else if (number(&p, text + length, &value, &unit, &unit_length))
            s->bold = value >= 600000;
        else
            return;
        s->has |= CSS_WEIGHT;
        break;
    }
    case P_STYLE:
        if (word_is(text, length, "italic") || contains_word(text, length, "oblique"))
            s->italic = true;
        else if (word_is(text, length, "normal"))
            s->italic = false;
        else
            return;
        s->has |= CSS_SLANT;
        break;
    case P_SIZE: {
        static const struct {
            const char *name;
            int px, percent;
        } keywords[] = {
            { "xx-small", 9, 0 }, { "x-small", 10, 0 }, { "small", 13, 0 }, { "medium", 16, 0 },
            { "large", 18, 0 }, { "x-large", 24, 0 }, { "xx-large", 32, 0 },
            { "smaller", 0, 83 }, { "larger", 0, 120 },
        };
        const char *p = text, *unit;
        size_t unit_length;
        int64_t value;
        int px = 0, percent = 0;
        for (unsigned i = 0; i < sizeof(keywords) / sizeof(keywords[0]); i++) {
            if (word_is(text, length, keywords[i].name)) {
                px = keywords[i].px;
                percent = keywords[i].percent;
            }
        }
        if (!px && !percent) {
            if (!number(&p, text + length, &value, &unit, &unit_length) || p != text + length ||
                value <= 0)
                return;
            if (word_is(unit, unit_length, "px"))
                px = (int)((value + 500) / 1000);
            else if (word_is(unit, unit_length, "pt"))
                px = (int)(value * 4 / 3000);
            else if (word_is(unit, unit_length, "rem"))
                px = (int)(value * 16 / 1000);
            else if (word_is(unit, unit_length, "em"))
                percent = (int)(value / 10);
            else if (word_is(unit, unit_length, "%"))
                percent = (int)(value / 1000);
            else
                return;
        }
        if (px > 200)
            px = 200;
        if (percent > 1000)
            percent = 1000;
        if (px < 1 && percent < 1)
            return;
        s->size_px = px;
        s->size_percent = px ? 0 : percent;
        s->has |= CSS_SIZE;
        break;
    }
    case P_ALIGN:
        if (word_is(text, length, "center") || word_is(text, length, "-webkit-center"))
            s->align = CSS_CENTER;
        else if (word_is(text, length, "right") || word_is(text, length, "end"))
            s->align = CSS_RIGHT;
        else if (word_is(text, length, "left") || word_is(text, length, "start") ||
                 word_is(text, length, "justify"))
            s->align = CSS_LEFT;
        else
            return;
        s->has |= CSS_ALIGN;
        break;
    case P_DECORATION:
        if (contains_word(text, length, "underline"))
            s->underline = true;
        else if (contains_word(text, length, "none"))
            s->underline = false;
        else
            return;
        s->has |= CSS_UNDERLINE;
        break;
    case P_DISPLAY:
        if (word_is(text, length, "none"))
            s->display = CSS_NONE;
        else if (word_is(text, length, "inline") || word_is(text, length, "inline-block") ||
                 word_is(text, length, "inline-flex") || word_is(text, length, "inline-grid") ||
                 word_is(text, length, "contents"))
            s->display = CSS_INLINE;
        else
            s->display = CSS_BLOCK;
        s->layout = word_is(text, length, "flex") || word_is(text, length, "inline-flex") ? CSS_FLEX
                  : word_is(text, length, "grid") || word_is(text, length, "inline-grid") ? CSS_GRID
                  : CSS_FLOW;
        s->has |= CSS_DISPLAY;
        break;
    case P_FLEX_DIRECTION: case P_FLEX_WRAP: case P_FLEX_FLOW:
        /* flex-flow nennt Richtung und Umbruch in einem */
        if (property != P_FLEX_WRAP && (has_word_starting(text, length, "row") ||
                                        has_word_starting(text, length, "column"))) {
            s->stacked = has_word_starting(text, length, "column");
            s->has |= CSS_DIRECTION;
        }
        if (property != P_FLEX_DIRECTION && (has_word_starting(text, length, "wrap") ||
                                             has_word_starting(text, length, "nowrap"))) {
            s->wraps = has_word_starting(text, length, "wrap");
            s->has |= CSS_WRAP;
        }
        break;
    case P_GRID_AREAS:
        /* benannte Flächen: Wo ein Kind steht, sagt dann sein Name - nicht die Reihenfolge */
        s->areas = !word_is(text, length, "none");
        s->has |= CSS_AREAS;
        break;
    case P_GRID_COLUMNS: case P_GRID_TEMPLATE: {
        /* grid-template: "Zeilen / Spalten" - hier zählen nur die Spalten */
        const char *p = text, *stop = text + length;
        if (property == P_GRID_TEMPLATE) {
            const char *slash = NULL;
            int level = 0;
            bool named = false;         /* Flächen mit Namen stehen gleich mit darin */
            for (const char *q = text; q < stop; q++) {
                level += *q == '(' ? 1 : *q == ')' && level > 0 ? -1 : 0;
                if (*q == '/' && level == 0)
                    slash = q;
                named |= *q == '"' || *q == '\'';
            }
            if (named || slash) {
                s->areas = named;
                s->has |= CSS_AREAS;
            }
            if (named || !slash)
                break;
            p = slash + 1;
        }
        grid_tracks(sheet, p, stop, s);
        s->has |= CSS_TRACKS;
        break;
    }
    case P_FLEX_GROW: {
        const char *p = text, *unit;
        size_t unit_length;
        int64_t value;
        if (!number(&p, text + length, &value, &unit, &unit_length))
            return;
        s->grows = value > 0;
        s->has |= CSS_GROW;
        break;
    }
    case P_JUSTIFY:
        s->justify = has_word_starting(text, length, "center") ? CSS_PACK_CENTER
                   : has_word_starting(text, length, "flex-end") ||
                     has_word_starting(text, length, "end") || has_word_starting(text, length, "right")
                         ? CSS_PACK_END
                   : has_word_starting(text, length, "space-") ? CSS_PACK_BETWEEN : CSS_PACK_START;
        s->has |= CSS_JUSTIFY;
        break;
    case P_FLEX: case P_FLEX_BASIS: {
        /* die Wunschbreite: bei "flex" die letzte Angabe mit Einheit ("0 0 33.3%") */
        const char *p = text, *stop = text + length, *unit;
        int px = 0, percent = 0;
        bool first = true;
        if (property == P_FLEX) {
            /* die erste Zahl sagt, ob das Kind freien Platz nimmt ("1", "1 1 0", "auto") */
            s->grows = word_is(text, length, "auto");
            s->has |= CSS_GROW;
        }
        while (p < stop) {
            size_t unit_length;
            int64_t value;
            while (p < stop && space(*p))
                p++;
            const char *word = p;
            if (!number(&p, stop, &value, &unit, &unit_length) || value < 0) {
                p = word;
                while (p < stop && !space(*p))
                    p++;                /* auto, none, content ... */
                first = false;
                continue;
            }
            if (property == P_FLEX && first && unit_length == 0)
                s->grows = value > 0;
            first = false;
            if (word_is(unit, unit_length, "px"))
                px = (int)((value + 500) / 1000), percent = 0;
            else if (word_is(unit, unit_length, "em") || word_is(unit, unit_length, "rem"))
                px = (int)(value * 16 / 1000), percent = 0;
            else if (word_is(unit, unit_length, "%"))
                percent = (int)((value + 500) / 1000), px = 0;
        }
        s->basis_px = px > 20000 ? 20000 : px;
        s->basis_percent = percent > 100 ? 100 : percent;
        if (px > 0 || percent > 0)
            s->has |= CSS_BASIS;
        else
            s->has &= ~(uint32_t)CSS_BASIS;
        break;
    }
    case P_VISIBILITY:
        s->hidden = word_is(text, length, "hidden") || word_is(text, length, "collapse");
        s->has |= CSS_VISIBILITY;
        break;
    case P_LIST:
        s->list_plain = contains_word(text, length, "none");
        s->has |= CSS_LIST;
        break;
    case P_FLOAT:
        s->float_side = word_is(text, length, "left") || word_is(text, length, "inline-start")
                            ? CSS_SIDE_LEFT
                      : word_is(text, length, "right") || word_is(text, length, "inline-end")
                            ? CSS_SIDE_RIGHT : CSS_SIDE_NONE;
        s->has |= CSS_FLOAT;
        break;
    case P_CLEAR:
        s->clear = word_is(text, length, "left") ? CSS_SIDE_LEFT
                 : word_is(text, length, "right") ? CSS_SIDE_RIGHT
                 : word_is(text, length, "none") ? CSS_SIDE_NONE : CSS_SIDE_BOTH;
        s->has |= CSS_CLEAR;
        break;
    case P_WIDTH: {
        const char *p = text, *unit;
        size_t unit_length;
        int64_t value;
        int px = 0, percent = 0;
        if (word_is(text, length, "auto")) {
            s->width_px = s->width_percent = 0;
            s->has &= ~(uint32_t)CSS_WIDTH;
            break;
        }
        if (!number(&p, text + length, &value, &unit, &unit_length) || p != text + length ||
            value < 0)
            return;                     /* calc(), fit-content ...: wie nicht angegeben */
        if (word_is(unit, unit_length, "px") || (unit_length == 0 && value == 0))
            px = (int)((value + 500) / 1000);
        else if (word_is(unit, unit_length, "pt"))
            px = (int)(value * 4 / 3000);
        else if (word_is(unit, unit_length, "em") || word_is(unit, unit_length, "rem"))
            px = (int)(value * 16 / 1000);
        else if (word_is(unit, unit_length, "ch") || word_is(unit, unit_length, "ex"))
            px = (int)(value * 8 / 1000);
        else if (word_is(unit, unit_length, "vw"))
            px = (int)(value * sheet_viewport(sheet) / 100000);
        else if (word_is(unit, unit_length, "%"))
            percent = (int)((value + 500) / 1000);
        else
            return;
        if (px > 20000)
            px = 20000;
        if (percent > 100)
            percent = 100;
        s->width_px = px;
        s->width_percent = px ? 0 : percent;
        s->has |= CSS_WIDTH;            /* auch "0": so breit soll es sein */
        break;
    }
    case P_TOP: {
        /*
         * Aufklappmenüs und Texte für Vorleseprogramme werden oft weit nach
         * oben aus dem Bild geschoben (top: -1000em) statt ausgeblendet. Nur
         * das wird hier erkannt. Nach links Geschobenes (left: -9999px)
         * bleibt sichtbar: So verstecken Seiten gern Bedienelemente, die erst
         * unter dem Mauszeiger erscheinen - hier wären sie sonst unerreichbar.
         */
        const char *p = text, *unit;
        size_t unit_length;
        int64_t value;
        bool away = false;
        if (number(&p, text + length, &value, &unit, &unit_length) && p == text + length) {
            if (word_is(unit, unit_length, "em") || word_is(unit, unit_length, "rem"))
                value *= 16;
            else if (!word_is(unit, unit_length, "px"))
                value = 0;
            away = value <= -500000;
        }
        s->off_top = away;
        break;
    }
    case P_CLIP:
    case P_CLIP_PATH: {
        /*
         * "Nur für Vorleseprogramme": Solche Texte werden auf ein Rechteck
         * ohne Fläche beschnitten - rect(0 0 0 0), rect(1px, 1px, 1px, 1px)
         * oder inset(50%).
         */
        size_t open = 0;
        while (open < length && text[open] != '(')
            open++;
        bool rect = open == 4 && word_is(text, 4, "rect");
        bool inset = open == 5 && word_is(text, 5, "inset");
        bool nothing = false;
        if ((rect && property == P_CLIP) || inset) {
            const char *p = text + open + 1, *end = text + length, *unit;
            size_t unit_length;
            int64_t value;
            int count = 0;
            nothing = true;
            while (p < end && *p != ')') {
                if (space(*p) || *p == ',') {
                    p++;
                    continue;
                }
                if (!number(&p, end, &value, &unit, &unit_length)) {
                    nothing = false;
                    break;
                }
                count++;
                if (rect ? value > 1000
                         : !(word_is(unit, unit_length, "%") && value >= 50000))
                    nothing = false;
            }
            if (count == 0)
                nothing = false;
        } else if (!word_is(text, length, "auto") && !word_is(text, length, "none")) {
            return;
        }
        s->clipped = nothing;
        s->has |= CSS_CLIP;
        break;
    }
    default:
        return;
    }
    c->priority[slot] = priority;
}

static bool has_class(const css_node_t *node, const char *name, size_t length)
{
    const char *text = node->classes;
    size_t total = node->classes_len;
    for (size_t i = 0; i < total;) {
        while (i < total && space(text[i]))
            i++;
        size_t start = i;
        while (i < total && !space(text[i]))
            i++;
        if (i - start == length && memcmp(text + start, name, length) == 0)
            return true;
    }
    return false;
}

static bool same_text(const char *a, const char *b, size_t length, bool fold)
{
    for (size_t i = 0; i < length; i++) {
        if (fold ? lower(a[i]) != lower(b[i]) : a[i] != b[i])
            return false;
    }
    return true;
}

/*
 * Sucht ein Attribut im Text des Start-Tags (name, name=wert, name="wert").
 * Liefert false, wenn es fehlt.
 */
static bool find_attribute(const css_node_t *node, const char *name, size_t length,
                           const char **value, size_t *value_length)
{
    if (node->attr) {
        const char *found = node->attr(node, name, length, value_length);
        if (!found)
            return false;
        *value = found;
        return true;
    }
    const char *p = node->attrs;
    const char *end = p + node->attrs_len;
    if (!p)
        return false;
    while (p < end) {
        while (p < end && (space(*p) || *p == '/'))
            p++;
        const char *start = p;
        while (p < end && !space(*p) && *p != '=' && *p != '>' && *p != '/')
            p++;
        size_t found = (size_t)(p - start);
        if (found == 0) {
            if (p < end)
                p++;                    /* verirrtes Zeichen */
            continue;
        }
        while (p < end && space(*p))
            p++;
        const char *text = "";
        size_t text_length = 0;
        if (p < end && *p == '=') {
            p++;
            while (p < end && space(*p))
                p++;
            if (p < end && (*p == '"' || *p == '\'')) {
                char quote = *p++;
                text = p;
                while (p < end && *p != quote)
                    p++;
                text_length = (size_t)(p - text);
                if (p < end)
                    p++;
            } else {
                text = p;
                while (p < end && !space(*p) && *p != '>')
                    p++;
                text_length = (size_t)(p - text);
            }
        }
        if (found == length && same_text(start, name, length, true)) {
            *value = text;
            *value_length = text_length;
            return true;
        }
    }
    return false;
}

static bool attribute_matches(const simple_t *s, const css_node_t *node)
{
    const char *have;
    size_t n;
    if (!find_attribute(node, s->name, s->length, &have, &n))
        return false;
    const char *want = s->value;
    size_t m = s->value_length;
    switch (s->op) {
    case A_EXISTS:
        return true;
    case A_EQUALS:
        return n == m && same_text(have, want, m, s->fold);
    case A_DASH:
        return n >= m && same_text(have, want, m, s->fold) && (n == m || have[m] == '-');
    case A_PREFIX:
        return m > 0 && n >= m && same_text(have, want, m, s->fold);
    case A_SUFFIX:
        return m > 0 && n >= m && same_text(have + n - m, want, m, s->fold);
    case A_CONTAINS:
        if (m == 0)
            return false;
        for (size_t i = 0; i + m <= n; i++) {
            if (same_text(have + i, want, m, s->fold))
                return true;
        }
        return false;
    case A_WORD:
        if (m == 0)
            return false;
        for (size_t i = 0; i < n;) {
            while (i < n && space(have[i]))
                i++;
            size_t start = i;
            while (i < n && !space(have[i]))
                i++;
            if (i - start == m && same_text(have + start, want, m, s->fold))
                return true;
        }
        return false;
    }
    return false;
}

static const css_node_t *sibling_of(const css_node_t *node, int direction)
{
    return node->tree && node->tree->sibling ? node->tree->sibling(node, direction) : NULL;
}

static bool same_tag(const css_node_t *a, const css_node_t *b)
{
    return a->tag_len == b->tag_len && same_text(a->tag, b->tag, a->tag_len, true);
}

/* Steht das Element unter seinen Geschwistern an einer der Stellen a*n + b? */
static bool nth_matches(const simple_t *s, const css_node_t *node)
{
    if (!node->tree || !node->tree->sibling)
        return false;                   /* niemand kennt die Geschwister */
    int direction = (s->op & NTH_LAST) ? 1 : -1, index = 1;
    for (const css_node_t *p = sibling_of(node, direction); p; p = sibling_of(p, direction)) {
        if (!(s->op & NTH_TYPE) || same_tag(p, node))
            index++;
        if ((s->a == 0 && index > s->b) || index > 100000)
            return false;
    }
    if (s->a == 0)
        return index == s->b;
    int d = index - s->b;
    return d % s->a == 0 && d / s->a >= 0;
}

/* Formularfelder: Nur sie können ein- oder ausgeschaltet sein (:enabled, :disabled). */
static bool form_control(const css_node_t *node)
{
    static const char *const names[] = { "button", "input", "select", "textarea", "option", "optgroup",
                                         "fieldset", NULL };
    for (int i = 0; names[i]; i++) {
        if (node->tag_len == strlen(names[i]) && same_text(node->tag, names[i], node->tag_len, true))
            return true;
    }
    return false;
}

/* Fragt das Glied nach Geschwistern oder Inhalt - also nach etwas, das nur der Baum weiß? */
static bool needs_tree(const compound_t *c)
{
    for (int i = 0; i < c->count; i++) {
        const simple_t *s = &c->simples[i];
        if (s->kind == S_NTH || s->kind == S_EMPTY)
            return true;
        for (int k = 0; (s->kind == S_NOT || s->kind == S_IS) && k < s->op; k++) {
            if (needs_tree(&s->inner[k]))
                return true;
        }
    }
    return false;
}

static bool compound_matches(const compound_t *c, const css_node_t *node)
{
    const char *have;
    size_t n;
    for (int i = 0; i < c->count; i++) {
        const simple_t *s = &c->simples[i];
        switch (s->kind) {
        case S_TAG:
            if (node->tag_len != s->length)
                return false;
            for (size_t k = 0; k < s->length; k++) {
                if (lower(node->tag[k]) != lower(s->name[k]))
                    return false;
            }
            break;
        case S_CLASS:
            if (!has_class(node, s->name, s->length))
                return false;
            break;
        case S_ID:
            if (node->id_len != s->length || memcmp(node->id, s->name, s->length) != 0)
                return false;
            break;
        case S_ROOT:
            if (node->parent)
                return false;
            break;
        case S_ATTR:
            if (!attribute_matches(s, node))
                return false;
            break;
        case S_NTH:
            if (!nth_matches(s, node))
                return false;
            break;
        case S_EMPTY:
            if (!node->tree || !node->tree->state || !node->tree->state(node, CSS_STATE_EMPTY))
                return false;
            break;
        case S_CHECKED:
            /* der Stand von jetzt, wenn ihn jemand kennt - sonst der aus dem Quelltext */
            if (node->tree && node->tree->state ? !node->tree->state(node, CSS_STATE_CHECKED)
                                                : !find_attribute(node, "checked", 7, &have, &n) &&
                                                  !find_attribute(node, "selected", 8, &have, &n))
                return false;
            break;
        case S_DISABLED: case S_ENABLED:
            if (!form_control(node) ||
                find_attribute(node, "disabled", 8, &have, &n) != (s->kind == S_DISABLED))
                return false;
            break;
        case S_LANG: {
            /* die Sprache steht am Element oder an einem, in dem es steckt */
            const css_node_t *p = node;
            while (p && !find_attribute(p, "lang", 4, &have, &n))
                p = p->parent;
            if (!p || n < s->length || !same_text(have, s->name, s->length, true) ||
                (n > s->length && have[s->length] != '-'))
                return false;
            break;
        }
        case S_NOT:
            for (int k = 0; k < s->op; k++) {
                /* "nicht das erste Kind" lässt sich ohne Wissen über die Geschwister nicht bejahen */
                if ((!node->tree && needs_tree(&s->inner[k])) || compound_matches(&s->inner[k], node))
                    return false;
            }
            break;
        case S_IS: {
            bool any = false;
            for (int k = 0; k < s->op && !any; k++)
                any = compound_matches(&s->inner[k], node);
            if (!any)
                return false;
            break;
        }
        }
    }
    return true;
}

/*
 * Passt die Auswahl bis zum Glied index, wenn dieses für node steht? steps
 * begrenzt die Arbeit: Verschachtelte "irgendwo darin / irgendwo danach"
 * könnten sonst sehr lange probieren.
 */
static bool selector_walk(const rule_t *rule, int index, const css_node_t *node, int *steps)
{
    if (!node || --*steps < 0 || !compound_matches(&rule->compounds[index], node))
        return false;
    if (index == 0)
        return true;
    switch (rule->compounds[index].relation) {
    case REL_CHILD:
        return selector_walk(rule, index - 1, node->parent, steps);
    case REL_NEXT:
        return selector_walk(rule, index - 1, sibling_of(node, -1), steps);
    case REL_AFTER:
        for (const css_node_t *p = sibling_of(node, -1); p && *steps > 0; p = sibling_of(p, -1)) {
            if (selector_walk(rule, index - 1, p, steps))
                return true;
        }
        return false;
    default:
        for (const css_node_t *up = node->parent; up && *steps > 0; up = up->parent) {
            if (selector_walk(rule, index - 1, up, steps))
                return true;
        }
        return false;
    }
}

static bool selector_matches(const rule_t *rule, int index, const css_node_t *node)
{
    int steps = 20000;
    return selector_walk(rule, index, node, &steps);
}

static void match_bucket(const css_sheet_t *sheet, uint32_t bucket, const css_node_t *node,
                         cascade_t *c)
{
    for (int32_t index = sheet->buckets[bucket]; index >= 0; index = sheet->rules[index].next) {
        const rule_t *rule = &sheet->rules[index];
        if (((rule->ua || rule->pseudo) && !c->boxes) || (rule->pseudo && c->pseudo && rule->pseudo != c->pseudo) ||
            (!rule->pseudo && c->pseudo) || !selector_matches(rule, rule->compound_count - 1, node))
            continue;
        if (rule->pseudo && !c->pseudo) {
            /* nur vermerken: Das Element hat ein Scheinelement davor oder danach */
            if (rule->pseudo == 1)
                c->style->has_before = true;
            else
                c->style->has_after = true;
            continue;
        }
        for (uint32_t d = 0; d < rule->decl_count; d++) {
            const decl_t *decl = &sheet->decls[rule->first_decl + d];
            /* Regeln der Seite gehen den Grundregeln des Browsers immer vor */
            uint64_t priority = ((uint64_t)decl->important << 50) | ((uint64_t)!rule->ua << 48) |
                                ((uint64_t)rule->specificity << 24) | (uint32_t)index;
            apply(sheet, c, decl->property, decl->value, decl->length, priority + 1);
        }
    }
}

/* Zerlegt "name: wert; name: wert" und ruft für jede bekannte Eigenschaft found auf. */
typedef void (*decl_found_t)(void *ctx, uint8_t property, bool important, const char *name,
                             size_t name_length, const char *value, size_t value_length);

static void each_declaration(const char *text, size_t length, decl_found_t found, void *ctx)
{
    size_t i = 0;
    while (i < length) {
        size_t start = i;
        int level = 0;
        char quote = 0;
        while (i < length && (level > 0 || quote || text[i] != ';')) {
            char ch = text[i];
            if (quote) {
                if (ch == quote)
                    quote = 0;
            } else if (ch == '"' || ch == '\'') {
                quote = ch;
            } else if (ch == '(') {
                level++;
            } else if (ch == ')' && level > 0) {
                level--;
            }
            i++;
        }
        const char *name = text + start;
        size_t total = i - start, name_length = 0;
        i++;                            /* das ';' */
        while (name_length < total && name[name_length] != ':')
            name_length++;
        if (name_length >= total)
            continue;
        const char *value = name + name_length + 1;
        size_t value_length = total - name_length - 1;
        trim(&name, &name_length);
        trim(&value, &value_length);

        bool important = false;
        if (value_length >= 10 && word_is(value + value_length - 9, 9, "important")) {
            size_t cut = value_length - 9;
            while (cut && space(value[cut - 1]))
                cut--;
            if (cut && value[cut - 1] == '!') {
                important = true;
                value_length = cut - 1;
                trim(&value, &value_length);
            }
        }
        if (!name_length || !value_length)
            continue;

        uint8_t property = P_COUNT;
        for (unsigned k = 0; k < sizeof(properties) / sizeof(properties[0]); k++) {
            if (word_is(name, name_length, properties[k].name))
                property = properties[k].property;
        }
        found(ctx, property, important, name, name_length, value, value_length);
    }
}

typedef struct {
    const css_sheet_t *sheet;
    cascade_t *cascade;
    bool hints;                         /* Festlegungen aus Attributen: unter allen Regeln der Seite */
} inline_ctx_t;

static void inline_found(void *ctx, uint8_t property, bool important, const char *name,
                         size_t name_length, const char *value, size_t value_length)
{
    inline_ctx_t *c = ctx;
    (void)name;
    (void)name_length;
    if (property == P_COUNT || value_length >= VALUE_MAX)
        return;
    /* über allen Regeln; mit !important auch über deren !important */
    uint64_t priority = c->hints ? (uint64_t)1 << 48
                                 : ((uint64_t)1 << 49) | ((uint64_t)important << 51);
    apply(c->sheet, c->cascade, property, value, value_length, priority);
}

static void match(const css_sheet_t *sheet, const css_node_t *node, const char *hints,
                  size_t hints_length, const char *style, size_t style_length, css_style_t *out,
                  bool boxes, int pseudo, const char **content, size_t *content_length)
{
    cascade_t c;
    memset(&c, 0, sizeof(c));
    memset(out, 0, sizeof(*out));
    c.style = out;
    c.boxes = boxes;
    c.pseudo = (uint8_t)pseudo;

    if (sheet && node) {
        if (node->id_len)
            match_bucket(sheet, hash(S_ID, node->id, node->id_len), node, &c);
        for (size_t i = 0; i < node->classes_len;) {
            while (i < node->classes_len && space(node->classes[i]))
                i++;
            size_t start = i;
            while (i < node->classes_len && !space(node->classes[i]))
                i++;
            if (i > start)
                match_bucket(sheet, hash(S_CLASS, node->classes + start, i - start), node, &c);
        }
        if (node->tag_len)
            match_bucket(sheet, hash(S_TAG, node->tag, node->tag_len), node, &c);
        match_bucket(sheet, hash(S_ROOT, "", 0), node, &c);         /* "*" und ":root" */
    }
    if (hints && hints_length) {
        inline_ctx_t ctx = { sheet, &c, true };
        each_declaration(hints, hints_length, inline_found, &ctx);
    }
    if (style && style_length) {
        inline_ctx_t ctx = { sheet, &c, false };
        each_declaration(style, style_length, inline_found, &ctx);
    }
    if (content) {
        *content = c.content;
        *content_length = c.content_length;
    }
}

void css_match(const css_sheet_t *sheet, const css_node_t *node, const char *style,
               size_t style_length, css_style_t *out)
{
    match(sheet, node, NULL, 0, style, style_length, out, false, 0, NULL, NULL);
}

void css_match_pseudo(const css_sheet_t *sheet, const css_node_t *node, int which, css_style_t *out,
                      const char **content, size_t *content_length)
{
    match(sheet, node, NULL, 0, NULL, 0, out, true, which == 1 ? 1 : 2, content, content_length);
}

void css_match_full(const css_sheet_t *sheet, const css_node_t *node, const char *hints,
                    size_t hints_length, const char *style, size_t style_length,
                    css_style_t *out)
{
    match(sheet, node, hints, hints_length, style, style_length, out, true, 0, NULL, NULL);
}

void css_set_viewport_height(css_sheet_t *sheet, int height)
{
    if (sheet)
        sheet->viewport_height = height;
}

/* --- Stylesheet einlesen ---------------------------------------------------------------- */

static void *grow(void *array, int count, int *capacity, size_t item, int limit)
{
    if (count < *capacity)
        return array;
    int wanted = *capacity ? *capacity * 2 : 256;
    if (wanted > limit)
        wanted = limit;
    if (count >= wanted)
        return NULL;
    void *bigger = kmalloc((size_t)wanted * item);
    if (!bigger)
        return NULL;
    if (array)
        memcpy(bigger, array, (size_t)count * item);
    kfree(array);
    *capacity = wanted;
    return bigger;
}

/*
 * Liest eine Attribut-Bedingung ab "[": [name], [name=wert], [name~="wert" i].
 * Setzt *at hinter die schließende Klammer.
 */
static bool parse_attribute(const char *text, size_t length, size_t *at, simple_t *out)
{
    size_t i = *at + 1;
    while (i < length && space(text[i]))
        i++;
    size_t start = i;
    while (i < length && (name_char(text[i]) || text[i] == ':'))
        i++;
    if (i == start || i - start > 200)
        return false;
    out->kind = S_ATTR;
    out->op = A_EXISTS;
    out->fold = false;
    out->name = text + start;
    out->length = (uint16_t)(i - start);
    out->value = "";
    out->value_length = 0;
    while (i < length && space(text[i]))
        i++;
    if (i < length && text[i] == ']') {
        *at = i + 1;
        return true;
    }

    if (i < length && text[i] == '=') {
        out->op = A_EQUALS;
        i++;
    } else if (i + 1 < length && text[i + 1] == '=') {
        switch (text[i]) {
        case '~': out->op = A_WORD; break;
        case '|': out->op = A_DASH; break;
        case '^': out->op = A_PREFIX; break;
        case '$': out->op = A_SUFFIX; break;
        case '*': out->op = A_CONTAINS; break;
        default: return false;
        }
        i += 2;
    } else {
        return false;
    }
    while (i < length && space(text[i]))
        i++;
    if (i >= length)
        return false;
    if (text[i] == '"' || text[i] == '\'') {
        char quote = text[i++];
        start = i;
        while (i < length && text[i] != quote) {
            if (text[i] == '\\')
                return false;           /* maskierte Zeichen: selten, wird ausgelassen */
            i++;
        }
        if (i >= length || i - start > 2000)
            return false;
        out->value = text + start;
        out->value_length = (uint16_t)(i - start);
        i++;
    } else {
        start = i;
        while (i < length && !space(text[i]) && text[i] != ']') {
            if (text[i] == '\\')
                return false;
            i++;
        }
        if (i == start || i - start > 2000)
            return false;               /* "[name=]": ungültig */
        out->value = text + start;
        out->value_length = (uint16_t)(i - start);
    }
    while (i < length && space(text[i]))
        i++;
    if (i < length && (text[i] == 'i' || text[i] == 'I' || text[i] == 's' || text[i] == 'S')) {
        out->fold = lower(text[i]) == 'i';
        i++;
        while (i < length && space(text[i]))
            i++;
    }
    if (i >= length || text[i] != ']')
        return false;
    *at = i + 1;
    return true;
}

/* Gibt frei, was die Glieder an Möglichkeiten für :not() und :is() angelegt haben. */
static void free_compounds(compound_t *list, int count)
{
    for (int c = 0; c < count; c++) {
        for (int k = 0; k < list[c].count; k++) {
            simple_t *s = &list[c].simples[k];
            if ((s->kind == S_NOT || s->kind == S_IS) && s->inner) {
                free_compounds(s->inner, s->op);
                kfree(s->inner);
                s->inner = NULL;
            }
        }
    }
}

/*
 * Was beim Lesen herauskommen kann: prüfbar; trifft hier nie zu (ein Zustand
 * wie :hover - nichts liegt unter dem Mauszeiger, nichts hat den Fokus); oder
 * etwas, das sich nicht prüfen lässt.
 */
enum { SEL_OK, SEL_NEVER, SEL_UNKNOWN };

typedef struct {
    int ids, classes, tags;
} weight_t;

#define ALTERNATIVES_MAX 8

/* "odd", "even", "3", "2n", "-n+3", "2n + 1": die Stellen a*n + b */
static bool parse_nth(const char *text, size_t n, int16_t *a, int16_t *b)
{
    trim(&text, &n);
    if (word_is(text, n, "odd")) {
        *a = 2;
        *b = 1;
        return true;
    }
    if (word_is(text, n, "even")) {
        *a = 2;
        *b = 0;
        return true;
    }
    size_t i = 0;
    int sign = 1, value = 0;
    bool digits = false;
    if (i < n && (text[i] == '+' || text[i] == '-'))
        sign = text[i++] == '-' ? -1 : 1;
    for (; i < n && text[i] >= '0' && text[i] <= '9' && value < 3000; i++, digits = true)
        value = value * 10 + (text[i] - '0');
    if (i >= n || lower(text[i]) != 'n') {
        *a = 0;                         /* nur eine Zahl: genau diese Stelle */
        *b = (int16_t)(sign * value);
        return digits && i == n;
    }
    *a = (int16_t)(sign * (digits ? value : 1));
    i++;
    while (i < n && space(text[i]))
        i++;
    *b = 0;
    if (i == n)
        return true;
    if (text[i] != '+' && text[i] != '-')
        return false;                   /* auch "2n+1 of .x": lässt sich hier nicht prüfen */
    sign = text[i++] == '-' ? -1 : 1;
    while (i < n && space(text[i]))
        i++;
    value = 0;
    digits = false;
    for (; i < n && text[i] >= '0' && text[i] <= '9' && value < 3000; i++, digits = true)
        value = value * 10 + (text[i] - '0');
    *b = (int16_t)(sign * value);
    return digits && i == n;
}

static int parse_compound(const char *text, size_t length, size_t *at, compound_t *c, weight_t *w,
                          uint8_t *pseudo, int depth);

/*
 * Die Möglichkeiten in den Klammern von :not() und :is(): durch Kommas
 * getrennt, jede ein einzelnes Glied. Legt sie in *out an (kmalloc).
 */
static int parse_alternatives(const char *text, size_t length, bool negated, compound_t **out, int *count,
                              weight_t *w, int depth)
{
    compound_t list[ALTERNATIVES_MAX];
    int n = 0, result = SEL_OK;
    bool first = true;
    *out = NULL;
    *count = 0;
    for (size_t i = 0; i <= length && result == SEL_OK;) {
        size_t start = i;
        int level = 0;
        char quote = 0;
        while (i < length && (level > 0 || quote || text[i] != ',')) {
            char ch = text[i];
            if (quote) {
                if (ch == quote)
                    quote = 0;
            } else if (ch == '"' || ch == '\'') {
                quote = ch;
            } else if (ch == '(' || ch == '[') {
                level++;
            } else if ((ch == ')' || ch == ']') && level > 0) {
                level--;
            }
            i++;
        }
        const char *one = text + start;
        size_t len = i - start, used = 0;
        i++;
        trim(&one, &len);
        if (len == 0)
            continue;
        compound_t c;
        weight_t inner = { 0, 0, 0 };
        int kind = n < ALTERNATIVES_MAX ? parse_compound(one, len, &used, &c, &inner, NULL, depth + 1)
                                        : SEL_UNKNOWN;
        if (kind == SEL_OK && used != len) {
            free_compounds(&c, 1);
            kind = SEL_UNKNOWN;         /* mehrere Glieder ("a b"): lässt sich so nicht prüfen */
        }
        if (kind == SEL_OK) {
            c.relation = REL_INSIDE;
            list[n++] = c;
            if (first && w) {           /* das Gewicht richtet sich (vereinfacht) nach der ersten Möglichkeit */
                w->ids += inner.ids;
                w->classes += inner.classes;
                w->tags += inner.tags;
            }
            first = false;
        } else if (kind == SEL_UNKNOWN && negated) {
            result = SEL_UNKNOWN;       /* "nicht irgendwas Unbekanntes": lieber die ganze Regel auslassen */
        }
        /* sonst: Diese Möglichkeit trifft nie zu - bei :not() stört sie nicht, bei :is() entfällt sie */
    }
    if (result == SEL_OK && n > 0) {
        *out = kmalloc((size_t)n * sizeof(compound_t));
        if (*out) {
            memcpy(*out, list, (size_t)n * sizeof(compound_t));
            *count = n;
            return SEL_OK;
        }
        result = SEL_UNKNOWN;
    }
    free_compounds(list, n);
    return result;
}

static bool add_simple(compound_t *c, simple_t s)
{
    if (c->count >= SIMPLES_MAX)
        return false;
    c->simples[c->count++] = s;
    return true;
}

/* Eine Bedingung ab ":" (text[*at] steht dahinter): Zustände, Stellung unter Geschwistern, Verneinung */
static int parse_pseudo(const char *text, size_t length, size_t *at, compound_t *c, weight_t *w,
                        uint8_t *pseudo, int depth)
{
    static const char *const never[] = {
        "hover", "active", "focus", "focus-visible", "focus-within", "target", "target-within",
        "visited", "indeterminate", "invalid", "user-invalid", "out-of-range", "fullscreen", "modal",
        "popover-open", "picture-in-picture", "playing", "paused", "autofill", "-webkit-autofill",
        "-moz-focusring", "-moz-ui-invalid", "defined", "open", "current", "past", "future", NULL };
    size_t i = *at;
    bool element = i < length && text[i] == ':';        /* "::after": ein Scheinelement */
    if (element)
        i++;
    size_t start = i;
    while (i < length && name_char(text[i]))
        i++;
    const char *name = text + start, *arg = NULL;
    size_t n = i - start, arg_n = 0;
    if (n == 0)
        return SEL_UNKNOWN;
    if (i < length && text[i] == '(') {
        size_t open = ++i;
        int level = 1;
        char quote = 0;
        for (; i < length && level > 0; i++) {
            char ch = text[i];
            if (quote)
                quote = ch == quote ? 0 : quote;
            else if (ch == '"' || ch == '\'')
                quote = ch;
            else if (ch == '(')
                level++;
            else if (ch == ')')
                level--;
        }
        if (level > 0)
            return SEL_UNKNOWN;
        arg = text + open;
        arg_n = i - 1 - open;
        trim(&arg, &arg_n);
    }
    *at = i;

    if (!arg && (word_is(name, n, "after") || word_is(name, n, "before"))) {
        /* ein Scheinelement vor oder nach dem Inhalt: Die Regel gilt ihm, nicht dem Element */
        if (!pseudo || i != length || depth > 0)
            return SEL_UNKNOWN;
        *pseudo = lower(name[0]) == 'b' ? 1 : 2;
        return SEL_OK;
    }
    if (element)
        return SEL_UNKNOWN;             /* ::placeholder, ::selection ...: gibt es hier nicht */
    if (w)
        w->classes++;

    simple_t s = { 0 };
    if (!arg) {
        static const struct {
            const char *name;
            uint8_t kind, op;
            bool both;                  /* ":only-...": von vorn und von hinten die erste Stelle */
        } plain[] = {
            { "root", S_ROOT, 0, false }, { "empty", S_EMPTY, 0, false }, { "checked", S_CHECKED, 0, false },
            { "disabled", S_DISABLED, 0, false }, { "enabled", S_ENABLED, 0, false },
            { "first-child", S_NTH, 0, false }, { "last-child", S_NTH, NTH_LAST, false },
            { "only-child", S_NTH, 0, true }, { "first-of-type", S_NTH, NTH_TYPE, false },
            { "last-of-type", S_NTH, NTH_TYPE | NTH_LAST, false }, { "only-of-type", S_NTH, NTH_TYPE, true },
        };
        for (unsigned k = 0; k < sizeof(plain) / sizeof(plain[0]); k++) {
            if (!word_is(name, n, plain[k].name))
                continue;
            s.kind = plain[k].kind;
            s.op = plain[k].op;
            s.b = 1;
            if (!add_simple(c, s))
                return SEL_UNKNOWN;
            if (plain[k].both) {
                s.op |= NTH_LAST;
                if (!add_simple(c, s))
                    return SEL_UNKNOWN;
            }
            return SEL_OK;
        }
        if (word_is(name, n, "link") || word_is(name, n, "any-link")) {
            /* Links gelten als unbesucht; gemeint sind nur <a>-Elemente */
            return add_simple(c, (simple_t){ .kind = S_TAG, .length = 1, .name = "a" }) ? SEL_OK : SEL_UNKNOWN;
        }
        if (word_is(name, n, "scope"))
            return SEL_OK;              /* ohne Bezugselement: die ganze Seite */
        for (int k = 0; never[k]; k++) {
            if (word_is(name, n, never[k]))
                return SEL_NEVER;
        }
        return SEL_UNKNOWN;
    }

    bool last = word_is(name, n, "nth-last-child") || word_is(name, n, "nth-last-of-type");
    bool typed = word_is(name, n, "nth-of-type") || word_is(name, n, "nth-last-of-type");
    if (last || typed || word_is(name, n, "nth-child")) {
        s.kind = S_NTH;
        s.op = (uint8_t)((last ? NTH_LAST : 0) | (typed ? NTH_TYPE : 0));
        return parse_nth(arg, arg_n, &s.a, &s.b) && add_simple(c, s) ? SEL_OK : SEL_UNKNOWN;
    }
    if (word_is(name, n, "lang")) {
        if (arg_n >= 2 && (arg[0] == '"' || arg[0] == '\'') && arg[arg_n - 1] == arg[0]) {
            arg++;
            arg_n -= 2;
        }
        for (size_t k = 0; k < arg_n; k++) {
            if (!name_char(arg[k]))
                return SEL_UNKNOWN;     /* Listen und Platzhalter */
        }
        if (arg_n == 0 || arg_n > 60)
            return SEL_UNKNOWN;
        s.kind = S_LANG;
        s.name = arg;
        s.length = (uint16_t)arg_n;
        return add_simple(c, s) ? SEL_OK : SEL_UNKNOWN;
    }
    if (word_is(name, n, "dir"))        /* geschrieben wird hier immer von links nach rechts */
        return word_is(arg, arg_n, "ltr") ? SEL_OK : word_is(arg, arg_n, "rtl") ? SEL_NEVER : SEL_UNKNOWN;

    bool negated = word_is(name, n, "not");
    bool weightless = word_is(name, n, "where");
    if (negated || weightless || word_is(name, n, "is") || word_is(name, n, "matches") ||
        word_is(name, n, "-webkit-any") || word_is(name, n, "-moz-any")) {
        if (depth >= 3)
            return SEL_UNKNOWN;
        compound_t *inner;
        int count;
        if (w)
            w->classes--;               /* das Gewicht kommt von dem, was in den Klammern steht */
        int result = parse_alternatives(arg, arg_n, negated, &inner, &count, weightless ? NULL : w, depth);
        if (result != SEL_OK)
            return result;
        if (count == 0)                 /* :not(:hover) trifft immer zu; :is(:hover) nie */
            return negated ? SEL_OK : SEL_NEVER;
        s.kind = negated ? S_NOT : S_IS;
        s.op = (uint8_t)count;
        s.inner = inner;
        if (!add_simple(c, s)) {
            free_compounds(inner, count);
            kfree(inner);
            return SEL_UNKNOWN;
        }
        return SEL_OK;
    }
    return SEL_UNKNOWN;                 /* :has() und anderes */
}

/*
 * Liest ein Glied der Auswahl ("li.aktiv[data-x]:first-child") ab text[*at]
 * bis zum nächsten Leerraum oder Verknüpfungszeichen.
 */
static int parse_compound(const char *text, size_t length, size_t *at, compound_t *c, weight_t *w,
                          uint8_t *pseudo, int depth)
{
    size_t i = *at;
    bool any = false;
    int result = SEL_OK;
    c->count = 0;
    c->relation = REL_INSIDE;
    while (result != SEL_UNKNOWN && i < length && !space(text[i]) && text[i] != '>' && text[i] != '+' &&
           text[i] != '~' && text[i] != ',') {
        simple_t s = { 0 };
        if (text[i] == '*') {
            i++;
            any = true;
            continue;
        }
        if (text[i] == '&') {
            result = SEL_UNKNOWN;       /* verschachtelte Regeln */
            break;
        }
        if (text[i] == '[') {
            if (!parse_attribute(text, length, &i, &s) || !add_simple(c, s))
                result = SEL_UNKNOWN;
            if (w)
                w->classes++;
            any = true;
            continue;
        }
        if (text[i] == ':') {
            i++;
            int one = parse_pseudo(text, length, &i, c, w, pseudo, depth);
            if (one != SEL_OK)
                result = one == SEL_UNKNOWN || result == SEL_UNKNOWN ? SEL_UNKNOWN : SEL_NEVER;
            any = true;
            continue;
        }
        if (text[i] == '.' || text[i] == '#') {
            s.kind = text[i] == '.' ? S_CLASS : S_ID;
            i++;
        } else if (name_char(text[i])) {
            s.kind = S_TAG;
        } else {
            result = SEL_UNKNOWN;       /* Klammern, Sonderzeichen */
            break;
        }
        size_t start = i;
        while (i < length && (name_char(text[i]) || text[i] == '\\'))
            i++;
        if (i == start || i - start > 200) {
            result = SEL_UNKNOWN;
            break;
        }
        for (size_t k = start; k < i; k++) {
            if (text[k] == '\\')
                result = SEL_UNKNOWN;   /* maskierte Zeichen: selten, wird ausgelassen */
        }
        s.name = text + start;
        s.length = (uint16_t)(i - start);
        if (!add_simple(c, s))
            result = SEL_UNKNOWN;
        if (w) {
            if (s.kind == S_ID)
                w->ids++;
            else if (s.kind == S_CLASS)
                w->classes++;
            else
                w->tags++;
        }
        any = true;
    }
    *at = i;
    if (!any && result == SEL_OK)
        result = SEL_UNKNOWN;
    if (result != SEL_OK)
        free_compounds(c, 1);
    return result;
}

/*
 * Liest eine Auswahl ("ul.menu > li + li a:not(.aus)"). Liefert false, wenn
 * etwas darin vorkommt, das sich hier nicht prüfen lässt oder nie zutrifft -
 * die Regel entfällt dann.
 */
static bool parse_selector(const char *text, size_t length, compound_t *out, int *count,
                           uint32_t *specificity, uint8_t *pseudo)
{
    weight_t w = { 0, 0, 0 };
    uint8_t relation = REL_INSIDE;
    bool pending = false;               /* ein Verknüpfungszeichen wartet auf sein Glied */
    size_t i = 0;
    *count = 0;

    while (i < length) {
        while (i < length && space(text[i]))
            i++;
        if (i >= length)
            break;
        if (text[i] == '>' || text[i] == '+' || text[i] == '~') {
            if (*count == 0 || pending)
                goto fail;
            relation = text[i] == '>' ? REL_CHILD : text[i] == '+' ? REL_NEXT : REL_AFTER;
            pending = true;
            i++;
            continue;
        }
        if (*count >= COMPOUNDS_MAX)
            goto fail;
        compound_t *c = &out[*count];
        if (parse_compound(text, length, &i, c, &w, pseudo, 0) != SEL_OK)
            goto fail;
        c->relation = relation;
        (*count)++;
        relation = REL_INSIDE;
        pending = false;
        if (i < length && text[i] == ',')
            goto fail;
    }
    if (*count == 0 || pending)
        goto fail;
    *specificity = ((uint32_t)(w.ids > 255 ? 255 : w.ids) << 16) |
                   ((uint32_t)(w.classes > 255 ? 255 : w.classes < 0 ? 0 : w.classes) << 8) |
                   (uint32_t)(w.tags > 255 ? 255 : w.tags);
    return true;
fail:
    free_compounds(out, *count);
    *count = 0;
    return false;
}

typedef struct {
    css_sheet_t *sheet;
    bool global;                        /* Regel gilt der ganzen Seite: Variablen merken */
    int count;
} block_ctx_t;

static void block_found(void *ctx, uint8_t property, bool important, const char *name,
                        size_t name_length, const char *value, size_t value_length)
{
    block_ctx_t *c = ctx;
    css_sheet_t *sheet = c->sheet;
    if (name_length > 2 && name[0] == '-' && name[1] == '-') {
        if (c->global && sheet->var_count < VARS_MAX && value_length < VALUE_MAX) {
            var_t *v = &sheet->vars[sheet->var_count++];
            v->name = name;
            v->name_length = (uint16_t)name_length;
            v->value = value;
            v->value_length = (uint16_t)value_length;
        }
        return;
    }
    if (property == P_COUNT || value_length >= VALUE_MAX)
        return;
    decl_t *decls = grow(sheet->decls, sheet->decl_count, &sheet->decl_capacity, sizeof(decl_t),
                         DECLS_MAX);
    if (!decls) {
        sheet->failed = true;
        return;
    }
    sheet->decls = decls;
    decl_t *d = &decls[sheet->decl_count++];
    d->property = property;
    d->important = important;
    d->value = value;
    d->length = (uint16_t)value_length;
    c->count++;
}

/* Eine Regel: Auswahl(en) und der Block mit den Festlegungen */
static void add_rule(css_sheet_t *sheet, const char *selectors, size_t selectors_length,
                     const char *block, size_t block_length)
{
    /* Gilt eine der Auswahlen der ganzen Seite? Dann zählen ihre Variablen. */
    block_ctx_t ctx = { sheet, false, 0 };
    for (size_t i = 0; i < selectors_length;) {
        size_t start = i;
        while (i < selectors_length && selectors[i] != ',')
            i++;
        const char *one = selectors + start;
        size_t n = i - start;
        i++;
        trim(&one, &n);
        if (word_is(one, n, ":root") || word_is(one, n, "html") || word_is(one, n, "body") ||
            word_is(one, n, "*"))
            ctx.global = true;
    }

    uint32_t first = (uint32_t)sheet->decl_count;
    each_declaration(block, block_length, block_found, &ctx);
    if (ctx.count == 0)
        return;

    for (size_t i = 0; i < selectors_length;) {
        size_t start = i;
        int level = 0;
        char quote = 0;
        while (i < selectors_length && (level > 0 || quote || selectors[i] != ',')) {
            char ch = selectors[i];
            if (quote) {
                if (ch == quote)
                    quote = 0;
            } else if (ch == '"' || ch == '\'') {
                quote = ch;
            } else if (ch == '(' || ch == '[') {
                level++;
            } else if ((ch == ')' || ch == ']') && level > 0) {
                level--;
            }
            i++;
        }
        const char *one = selectors + start;
        size_t n = i - start;
        i++;
        trim(&one, &n);

        compound_t compounds[COMPOUNDS_MAX];
        int count;
        uint32_t specificity;
        uint8_t pseudo = 0;
        if (!n || !parse_selector(one, n, compounds, &count, &specificity, &pseudo))
            continue;
        rule_t *rules = grow(sheet->rules, sheet->rule_count, &sheet->rule_capacity,
                             sizeof(rule_t), RULES_MAX);
        compound_t *copy = rules ? kmalloc((size_t)count * sizeof(compound_t)) : NULL;
        if (!rules || !copy) {
            free_compounds(compounds, count);
            sheet->failed = true;
            return;
        }
        sheet->rules = rules;
        memcpy(copy, compounds, (size_t)count * sizeof(compound_t));
        rule_t *rule = &rules[sheet->rule_count];
        rule->compounds = copy;
        rule->compound_count = (uint8_t)count;
        rule->specificity = specificity;
        rule->first_decl = first;
        rule->decl_count = (uint32_t)ctx.count;
        rule->ua = sheet->adding_ua;
        rule->pseudo = pseudo;

        /* Ins Fach des aussagekräftigsten Merkmals des letzten Glieds */
        const compound_t *subject = &copy[count - 1];
        const simple_t *key = NULL;
        for (int k = 0; k < subject->count; k++) {
            const simple_t *s = &subject->simples[k];
            if (s->kind == S_ID || (s->kind == S_CLASS && (!key || key->kind == S_TAG)) ||
                (s->kind == S_TAG && !key))
                key = s;
            if (s->kind == S_ID)
                break;
        }
        uint32_t bucket = key ? hash(key->kind, key->name, key->length) : hash(S_ROOT, "", 0);
        rule->next = -1;
        /* hinten anhängen wäre teuer; die Reihenfolge steckt in der Regelnummer */
        rule->next = sheet->buckets[bucket];
        sheet->buckets[bucket] = sheet->rule_count;
        sheet->rule_count++;
    }
}

/* Gilt die Bedingung hinter "@media"? */
static bool media_applies(const css_sheet_t *sheet, const char *text, size_t length)
{
    /* Liste mit Kommas: eine zutreffende Angabe genügt */
    for (size_t i = 0; i <= length;) {
        size_t start = i;
        while (i < length && text[i] != ',')
            i++;
        const char *query = text + start;
        size_t n = i - start;
        i++;
        trim(&query, &n);
        bool ok = true, negate = false;

        for (size_t k = 0; k < n && ok;) {
            while (k < n && space(query[k]))
                k++;
            if (k >= n)
                break;
            if (query[k] == '(') {
                /* bis zur zugehörigen Klammer - der Wert kann selbst welche enthalten: calc(...) */
                size_t close = k + 1;
                for (int level = 1; close < n; close++) {
                    level += query[close] == '(' ? 1 : query[close] == ')' ? -1 : 0;
                    if (level == 0)
                        break;
                }
                const char *feature = query + k + 1;
                size_t feature_length = close - k - 1, name_length = 0;
                trim(&feature, &feature_length);
                while (name_length < feature_length && feature[name_length] != ':' &&
                       feature[name_length] != '<' && feature[name_length] != '>')
                    name_length++;
                const char *name = feature, *value = feature + name_length;
                size_t value_length = feature_length - name_length;
                /* "name: wert" oder ein Vergleich wie "width >= 600px" */
                int compare = 0;        /* 0: Doppelpunkt; sonst 1 <, 2 <=, 3 >, 4 >= */
                if (value_length && *value == ':') {
                    value++;
                    value_length--;
                } else if (value_length) {
                    compare = *value == '<' ? 1 : 3;
                    value++;
                    value_length--;
                    if (value_length && *value == '=') {
                        compare++;
                        value++;
                        value_length--;
                    }
                }
                trim(&name, &name_length);
                trim(&value, &value_length);
                bool low = name_length > 4 && word_is(name, 4, "min-"), high = name_length > 4 && word_is(name, 4, "max-");
                const char *what = low || high ? name + 4 : name;
                size_t what_length = low || high ? name_length - 4 : name_length;
                bool wide = word_is(what, what_length, "width"), tall = word_is(what, what_length, "height");
                css_len_t amount;
                if ((wide || tall) && value_length && parse_len(sheet, value, value_length, LEN_NUMBER_PX, &amount) &&
                    amount.kind == CSS_LEN_VALUE) {
                    /* em meint in solchen Bedingungen immer 16 Pixel */
                    int64_t px = ((int64_t)amount.px + (int64_t)amount.em * 16) / 1000;
                    int64_t have = wide ? sheet->viewport : sheet_height(sheet);
                    ok = low || compare == 4 ? have >= px : high || compare == 2 ? have <= px
                       : compare == 1 ? have < px : compare == 3 ? have > px : have == px;
                } else if (word_is(name, name_length, "orientation")) {
                    ok = word_is(value, value_length, "landscape") == (sheet->viewport >= sheet_height(sheet));
                } else if (word_is(name, name_length, "prefers-color-scheme")) {
                    ok = word_is(value, value_length, "light");
                } else if (word_is(name, name_length, "prefers-reduced-motion")) {
                    ok = word_is(value, value_length, "reduce");    /* hier bewegt sich ohnehin nichts */
                } else if (word_is(name, name_length, "hover") || word_is(name, name_length, "any-hover")) {
                    ok = word_is(value, value_length, "hover");
                } else if (word_is(name, name_length, "pointer") || word_is(name, name_length, "any-pointer")) {
                    ok = word_is(value, value_length, "fine");
                } else if (word_is(name, name_length, "color") && !value_length) {
                    ok = true;
                } else {
                    ok = false;         /* Druck, Auflösung, was es sonst gibt: gilt nicht */
                }
                k = close + 1;
            } else {
                size_t word = k;
                while (k < n && !space(query[k]) && query[k] != '(')
                    k++;
                size_t word_length = k - word;
                if (word_is(query + word, word_length, "not"))
                    negate = true;
                else if (word_is(query + word, word_length, "print") ||
                         word_is(query + word, word_length, "speech"))
                    ok = false;
                else if (!word_is(query + word, word_length, "screen") &&
                         !word_is(query + word, word_length, "all") &&
                         !word_is(query + word, word_length, "only") &&
                         !word_is(query + word, word_length, "and"))
                    ok = false;
            }
        }
        if (n && ok != negate)
            return true;
        if (i > length)
            break;
    }
    return false;
}

/* Liefert die Stelle hinter der zu "{" bei open gehörenden "}". */
static size_t block_end(const char *text, size_t length, size_t open)
{
    int level = 0;
    char quote = 0;
    for (size_t i = open; i < length; i++) {
        char ch = text[i];
        if (quote) {
            if (ch == '\\')
                i++;
            else if (ch == quote)
                quote = 0;
        } else if (ch == '"' || ch == '\'') {
            quote = ch;
        } else if (ch == '{') {
            level++;
        } else if (ch == '}' && --level == 0) {
            return i + 1;
        }
    }
    return length;
}

static void parse_rules(css_sheet_t *sheet, const char *text, size_t length, int depth)
{
    size_t i = 0;
    while (i < length && !sheet->failed) {
        while (i < length && (space(text[i]) || text[i] == ';' || text[i] == '}'))
            i++;
        if (i >= length)
            break;

        /* Vorspann bis "{" oder ";" */
        size_t start = i;
        char quote = 0;
        while (i < length && (quote || (text[i] != '{' && text[i] != ';'))) {
            if (quote) {
                if (text[i] == quote)
                    quote = 0;
            } else if (text[i] == '"' || text[i] == '\'') {
                quote = text[i];
            }
            i++;
        }
        if (i >= length)
            break;
        if (text[i] == ';') {           /* @import, @charset ... */
            i++;
            continue;
        }
        size_t end = block_end(text, length, i);
        const char *prelude = text + start;
        size_t prelude_length = i - start;
        const char *block = text + i + 1;
        size_t block_length = end > i + 1 ? end - i - 2 : 0;
        if (end == length && (end == 0 || text[end - 1] != '}'))
            block_length = length - i - 1;      /* abgeschnitten: nehmen, was da ist */
        i = end;
        trim(&prelude, &prelude_length);

        if (prelude_length && prelude[0] == '@') {
            if (prelude_length >= 6 && word_is(prelude, 6, "@media") && depth < 4 &&
                media_applies(sheet, prelude + 6, prelude_length - 6))
                parse_rules(sheet, block, block_length, depth + 1);
            /*
             * "@supports (display: grid)": Regeln für Browser, die etwas
             * können - das meiste davon kann dieser inzwischen. Nur was
             * ausdrücklich für Browser ohne etwas gedacht ist ("not"), bleibt
             * aus. "@layer" ordnet Regeln nur.
             */
            else if (prelude_length >= 9 && word_is(prelude, 9, "@supports") && depth < 4) {
                const char *condition = prelude + 9;
                size_t n = prelude_length - 9;
                trim(&condition, &n);
                if (!(n >= 3 && word_is(condition, 3, "not")))
                    parse_rules(sheet, block, block_length, depth + 1);
            } else if (prelude_length >= 6 && word_is(prelude, 6, "@layer") && depth < 4) {
                parse_rules(sheet, block, block_length, depth + 1);
            }
            /* @font-face, @keyframes ...: übergehen */
            continue;
        }
        if (prelude_length)
            add_rule(sheet, prelude, prelude_length, block, block_length);
    }
}

/*
 * Die Grundregeln: wie eine Seite aussieht, die selbst nichts festlegt. Sie
 * entsprechen dem, was alle großen Browser mitbringen.
 */
static const char ua_rules[] =
    "html, body, div, p, h1, h2, h3, h4, h5, h6, ul, ol, menu, dl, dt, dd, form, fieldset, legend, pre,"
    " blockquote, address, article, aside, footer, header, hgroup, main, nav, section, figure,"
    " figcaption, hr, details, summary, center, dialog, search, listing, xmp, plaintext, dir,"
    " optgroup { display: block }\n"
    "li { display: list-item }\n"
    "head, script, style, title, meta, link, base, template, datalist, param, source, track, area,"
    " noembed, noframes, rp, [hidden] { display: none }\n"
    "body { margin: 8px }\n"
    "p, dl, blockquote, figure, pre, ul, ol, menu, dir, listing, xmp { margin-top: 1em; margin-bottom: 1em }\n"
    "blockquote, figure { margin-left: 40px; margin-right: 40px }\n"
    "ul, ol, menu, dir { padding-left: 40px }\n"
    "ul, menu, dir { list-style-type: disc }\n"
    "ol { list-style-type: decimal }\n"
    "ul ul, ol ul { list-style-type: circle }\n"
    "ul ul ul, ol ul ul, ul ol ul, ol ol ul { list-style-type: square }\n"
    "ul ul, ul ol, ol ul, ol ol, li ul, li ol { margin-top: 0; margin-bottom: 0 }\n"
    "dd { margin-left: 40px }\n"
    "h1 { font-size: 2em; margin-top: .67em; margin-bottom: .67em; font-weight: bold }\n"
    "h2 { font-size: 1.5em; margin-top: .83em; margin-bottom: .83em; font-weight: bold }\n"
    "h3 { font-size: 1.17em; margin-top: 1em; margin-bottom: 1em; font-weight: bold }\n"
    "h4 { margin-top: 1.33em; margin-bottom: 1.33em; font-weight: bold }\n"
    "h5 { font-size: .83em; margin-top: 1.67em; margin-bottom: 1.67em; font-weight: bold }\n"
    "h6 { font-size: .67em; margin-top: 2.33em; margin-bottom: 2.33em; font-weight: bold }\n"
    "b, strong, th, dt { font-weight: bold }\n"
    "i, em, cite, var, dfn, address { font-style: italic }\n"
    "pre, code, kbd, samp, tt, listing, xmp, plaintext { font-family: monospace }\n"
    "pre, listing, xmp, plaintext { white-space: pre }\n"
    "nobr { white-space: nowrap }\n"
    "small { font-size: smaller }\n"
    "big { font-size: larger }\n"
    "sub { font-size: smaller; vertical-align: sub }\n"
    "sup { font-size: smaller; vertical-align: super }\n"
    "u, ins { text-decoration: underline }\n"
    "center { text-align: center }\n"
    "hr { border-top: 1px solid #9a9a9a; margin-top: 8px; margin-bottom: 8px }\n"
    "mark { background-color: #ffff00; color: #000000 }\n"
    "fieldset { border: 2px solid #c0c0c0; margin-left: 2px; margin-right: 2px;"
    " padding: .35em .75em .625em }\n"
    "legend { padding-left: 2px; padding-right: 2px }\n"
    "table { display: table; border-spacing: 2px; box-sizing: border-box; text-indent: 0 }\n"
    "thead, tbody, tfoot { display: table-row-group; vertical-align: middle }\n"
    "tr { display: table-row }\n"
    "td, th { display: table-cell; padding: 1px; vertical-align: inherit }\n"
    "th { text-align: center }\n"
    "caption { display: table-caption; text-align: center }\n"
    "col, colgroup { display: table-column }\n"
    "img, input, select, textarea, button, video, audio, canvas, iframe, object, embed, svg,"
    " progress, meter { display: inline-block }\n"
    "button { box-sizing: border-box; padding: 3px 10px; border: 1px solid #3c465c; border-radius: 4px;"
    " background-color: #3c465c; color: #f0f2f6; text-align: center; font-size: 14px; font-weight: normal;"
    " font-style: normal; line-height: normal; white-space: normal }\n"
    "dialog { display: none }\n"
    "summary { font-weight: bold }\n";

css_sheet_t *css_create(int viewport_width)
{
    css_sheet_t *sheet = kzalloc(sizeof(*sheet));
    if (!sheet)
        return NULL;
    sheet->vars = kmalloc(VARS_MAX * sizeof(var_t));
    if (!sheet->vars) {
        kfree(sheet);
        return NULL;
    }
    sheet->viewport = viewport_width;
    for (int i = 0; i < BUCKETS; i++)
        sheet->buckets[i] = -1;
    /* Jede Seite beginnt mit den Grundregeln des Browsers (nur css_match_full sieht sie). */
    sheet->adding_ua = true;
    css_add(sheet, ua_rules, strlen(ua_rules));
    sheet->adding_ua = false;
    sheet->ua_rules = sheet->rule_count;
    return sheet;
}

void css_free(css_sheet_t *sheet)
{
    if (!sheet)
        return;
    for (int i = 0; i < sheet->rule_count; i++) {
        free_compounds(sheet->rules[i].compounds, sheet->rules[i].compound_count);
        kfree(sheet->rules[i].compounds);
    }
    while (sheet->chunks) {
        chunk_t *next = sheet->chunks->next;
        kfree(sheet->chunks);
        sheet->chunks = next;
    }
    kfree(sheet->rules);
    kfree(sheet->decls);
    kfree(sheet->vars);
    kfree(sheet);
}

bool css_add(css_sheet_t *sheet, const char *text, size_t length)
{
    if (!sheet || sheet->failed)
        return false;
    chunk_t *chunk = kmalloc(sizeof(chunk_t) + length + 1);
    if (!chunk)
        return false;

    /* Kopie ohne Kommentare; die Regeln zeigen später in diese Kopie */
    size_t used = 0;
    for (size_t i = 0; i < length;) {
        if (i + 1 < length && text[i] == '/' && text[i + 1] == '*') {
            i += 2;
            while (i + 1 < length && !(text[i] == '*' && text[i + 1] == '/'))
                i++;
            i = i + 2 <= length ? i + 2 : length;
            chunk->text[used++] = ' ';
        } else {
            chunk->text[used++] = text[i++];
        }
    }
    chunk->text[used] = '\0';
    chunk->next = sheet->chunks;
    sheet->chunks = chunk;
    parse_rules(sheet, chunk->text, used, 0);
    return !sheet->failed;
}

int css_rule_count(const css_sheet_t *sheet)
{
    return sheet ? sheet->rule_count - sheet->ua_rules : 0;
}

/* --- Einzelne Auswahlen (querySelector) -------------------------------------------------------- */

struct css_selector {
    int count;
    rule_t rules[16];                   /* je Komma-Glied eine */
    char text[];
};

css_selector_t *css_selector_parse(const char *text, size_t length)
{
    if (length == 0 || length > 4000)
        return NULL;
    css_selector_t *sel = kzalloc(sizeof(css_selector_t) + length + 1);
    if (!sel)
        return NULL;
    memcpy(sel->text, text, length);
    for (size_t i = 0; i < length;) {
        size_t start = i;
        int level = 0;
        char quote = 0;
        while (i < length && (level > 0 || quote || sel->text[i] != ',')) {
            char ch = sel->text[i];
            if (quote) {
                if (ch == quote)
                    quote = 0;
            } else if (ch == '"' || ch == '\'') {
                quote = ch;
            } else if (ch == '(' || ch == '[') {
                level++;
            } else if ((ch == ')' || ch == ']') && level > 0) {
                level--;
            }
            i++;
        }
        const char *one = sel->text + start;
        size_t n = i - start;
        i++;
        trim(&one, &n);
        compound_t compounds[COMPOUNDS_MAX];
        int count;
        uint32_t specificity;
        if (!n || sel->count >= 16 || !parse_selector(one, n, compounds, &count, &specificity, NULL)) {
            css_selector_free(sel);
            return NULL;
        }
        compound_t *copy = kmalloc((size_t)count * sizeof(compound_t));
        if (!copy) {
            free_compounds(compounds, count);
            css_selector_free(sel);
            return NULL;
        }
        memcpy(copy, compounds, (size_t)count * sizeof(compound_t));
        /* Elementnamen zählen ohne Rücksicht auf Groß- und Kleinschreibung */
        for (int c = 0; c < count; c++) {
            for (int k = 0; k < copy[c].count; k++) {
                simple_t *simple = &copy[c].simples[k];
                if (simple->kind != S_TAG || simple->name < sel->text ||
                    simple->name >= sel->text + length)
                    continue;
                char *name = sel->text + (simple->name - sel->text);
                for (size_t x = 0; x < simple->length; x++)
                    name[x] = lower(name[x]);
            }
        }
        sel->rules[sel->count].compounds = copy;
        sel->rules[sel->count].compound_count = (uint8_t)count;
        sel->count++;
    }
    if (sel->count == 0) {
        css_selector_free(sel);
        return NULL;
    }
    return sel;
}

bool css_selector_matches(const css_selector_t *selector, const css_node_t *node)
{
    for (int i = 0; selector && i < selector->count; i++) {
        if (selector_matches(&selector->rules[i], selector->rules[i].compound_count - 1, node))
            return true;
    }
    return false;
}

void css_selector_free(css_selector_t *selector)
{
    if (!selector)
        return;
    for (int i = 0; i < selector->count; i++) {
        free_compounds(selector->rules[i].compounds, selector->rules[i].compound_count);
        kfree(selector->rules[i].compounds);
    }
    kfree(selector);
}
