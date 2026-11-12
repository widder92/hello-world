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
       P_DECORATION, P_DISPLAY, P_VISIBILITY, P_LIST, P_FLOAT, P_CLEAR, P_COUNT };

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
};

enum { S_TAG, S_CLASS, S_ID, S_ROOT };

typedef struct {
    uint8_t kind;
    uint16_t length;
    const char *name;
} simple_t;

typedef struct {
    uint8_t count;
    bool child;                         /* steht direkt unter dem vorigen Glied ("a > b") */
    simple_t simples[SIMPLES_MAX];
} compound_t;

typedef struct {
    compound_t *compounds;              /* von links nach rechts; das letzte meint das Element */
    uint8_t compound_count;
    uint32_t specificity;
    uint32_t first_decl, decl_count;
    int32_t next;                       /* nächste Regel im selben Fach */
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

static void apply(const css_sheet_t *sheet, cascade_t *c, uint8_t property, const char *raw,
                  size_t raw_length, uint64_t priority)
{
    /* Zwei Schreibweisen für dasselbe teilen sich die Rangfolge */
    uint8_t slot = property == P_BACKGROUND_COLOR ? P_BACKGROUND : property;
    if (priority < c->priority[slot])
        return;

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
    if (!length || word_is(text, length, "inherit") || word_is(text, length, "unset") ||
        word_is(text, length, "initial") || word_is(text, length, "revert"))
        return;                         /* gilt wie nicht angegeben */

    css_style_t *s = c->style;
    uint32_t color;
    switch (property) {
    case P_COLOR:
        if (!css_color(text, length, &color))
            return;
        s->color = color;
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
        s->has |= CSS_DISPLAY;
        break;
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

static bool compound_matches(const compound_t *c, const css_node_t *node)
{
    for (int i = 0; i < c->count; i++) {
        const simple_t *s = &c->simples[i];
        switch (s->kind) {
        case S_TAG:
            if (node->tag_len != s->length)
                return false;
            for (size_t k = 0; k < s->length; k++) {
                if (lower(node->tag[k]) != s->name[k])
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
        }
    }
    return true;
}

/* Passt die Auswahl bis zum Glied index, wenn dieses für node steht? */
static bool selector_matches(const rule_t *rule, int index, const css_node_t *node)
{
    if (!node || !compound_matches(&rule->compounds[index], node))
        return false;
    if (index == 0)
        return true;
    if (rule->compounds[index].child)
        return selector_matches(rule, index - 1, node->parent);
    for (const css_node_t *up = node->parent; up; up = up->parent) {
        if (selector_matches(rule, index - 1, up))
            return true;
    }
    return false;
}

static void match_bucket(const css_sheet_t *sheet, uint32_t bucket, const css_node_t *node,
                         cascade_t *c)
{
    for (int32_t index = sheet->buckets[bucket]; index >= 0; index = sheet->rules[index].next) {
        const rule_t *rule = &sheet->rules[index];
        if (!selector_matches(rule, rule->compound_count - 1, node))
            continue;
        for (uint32_t d = 0; d < rule->decl_count; d++) {
            const decl_t *decl = &sheet->decls[rule->first_decl + d];
            uint64_t priority = ((uint64_t)decl->important << 50) |
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
    uint64_t priority = ((uint64_t)1 << 49) | ((uint64_t)important << 51);
    apply(c->sheet, c->cascade, property, value, value_length, priority);
}

void css_match(const css_sheet_t *sheet, const css_node_t *node, const char *style,
               size_t style_length, css_style_t *out)
{
    cascade_t c;
    memset(&c, 0, sizeof(c));
    memset(out, 0, sizeof(*out));
    c.style = out;

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
    if (style && style_length) {
        inline_ctx_t ctx = { sheet, &c };
        each_declaration(style, style_length, inline_found, &ctx);
    }
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
 * Liest eine Auswahl ("ul.menu > li a"). Liefert false, wenn etwas darin
 * vorkommt, das wir nicht prüfen können - die Regel entfällt dann.
 */
static bool parse_selector(const char *text, size_t length, compound_t *out, int *count,
                           uint32_t *specificity)
{
    int ids = 0, classes = 0, tags = 0;
    bool child = false;
    size_t i = 0;
    *count = 0;

    while (i < length) {
        while (i < length && space(text[i]))
            i++;
        if (i >= length)
            break;
        if (text[i] == '>') {
            if (*count == 0 || child)
                return false;
            child = true;
            i++;
            continue;
        }
        if (text[i] == '+' || text[i] == '~' || *count >= COMPOUNDS_MAX)
            return false;

        compound_t *c = &out[(*count)++];
        c->count = 0;
        c->child = child;
        child = false;
        bool any = false;
        while (i < length && !space(text[i]) && text[i] != '>' && text[i] != '+' &&
               text[i] != '~') {
            simple_t s = { 0 };
            if (text[i] == '*') {
                i++;
                any = true;
                continue;
            }
            if (text[i] == '.' || text[i] == '#') {
                s.kind = text[i] == '.' ? S_CLASS : S_ID;
                i++;
            } else if (text[i] == ':') {
                /* nur ":root" lässt sich ohne Wissen über Zustände prüfen */
                size_t start = ++i;
                while (i < length && name_char(text[i]))
                    i++;
                if (word_is(text + start, i - start, "root")) {
                    s.kind = S_ROOT;
                } else if (word_is(text + start, i - start, "link") ||
                           word_is(text + start, i - start, "any-link")) {
                    /* Links gelten als unbesucht; gemeint sind nur <a>-Elemente */
                    if (c->count >= SIMPLES_MAX)
                        return false;
                    c->simples[c->count++] = (simple_t){ S_TAG, 1, "a" };
                    any = true;
                    classes++;
                    continue;
                } else {
                    return false;
                }
                if (c->count >= SIMPLES_MAX)
                    return false;
                c->simples[c->count++] = s;
                classes++;
                any = true;
                continue;
            } else if (name_char(text[i])) {
                s.kind = S_TAG;
            } else {
                return false;           /* Attribute, Klammern, Sonderzeichen */
            }
            size_t start = i;
            while (i < length && (name_char(text[i]) || text[i] == '\\'))
                i++;
            if (i == start || i - start > 200 || c->count >= SIMPLES_MAX)
                return false;
            for (size_t k = start; k < i; k++) {
                if (text[k] == '\\')
                    return false;       /* maskierte Zeichen: selten, wird ausgelassen */
            }
            s.name = text + start;
            s.length = (uint16_t)(i - start);
            c->simples[c->count++] = s;
            if (s.kind == S_ID)
                ids++;
            else if (s.kind == S_CLASS)
                classes++;
            else
                tags++;
            any = true;
        }
        if (!any)
            return false;
    }
    if (*count == 0 || child)
        return false;
    *specificity = ((uint32_t)(ids > 255 ? 255 : ids) << 16) |
                   ((uint32_t)(classes > 255 ? 255 : classes) << 8) |
                   (uint32_t)(tags > 255 ? 255 : tags);
    return true;
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
        while (i < selectors_length && (level > 0 || selectors[i] != ',')) {
            if (selectors[i] == '(')
                level++;
            else if (selectors[i] == ')')
                level--;
            i++;
        }
        const char *one = selectors + start;
        size_t n = i - start;
        i++;
        trim(&one, &n);

        compound_t compounds[COMPOUNDS_MAX];
        int count;
        uint32_t specificity;
        if (!n || !parse_selector(one, n, compounds, &count, &specificity))
            continue;
        rule_t *rules = grow(sheet->rules, sheet->rule_count, &sheet->rule_capacity,
                             sizeof(rule_t), RULES_MAX);
        compound_t *copy = rules ? kmalloc((size_t)count * sizeof(compound_t)) : NULL;
        if (!rules || !copy) {
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
                size_t close = k;
                while (close < n && query[close] != ')')
                    close++;
                const char *feature = query + k + 1;
                size_t feature_length = close - k - 1, name_length = 0;
                while (name_length < feature_length && feature[name_length] != ':')
                    name_length++;
                const char *value = feature + name_length + (name_length < feature_length);
                const char *value_end = feature + feature_length;
                const char *name = feature;
                trim(&name, &name_length);
                while (value < value_end && space(*value))
                    value++;
                int64_t amount;
                const char *unit;
                size_t unit_length;
                bool min = word_is(name, name_length, "min-width");
                bool max = word_is(name, name_length, "max-width");
                if ((min || max) && number(&value, value_end, &amount, &unit, &unit_length)) {
                    int64_t px = word_is(unit, unit_length, "px") ? amount / 1000
                               : (word_is(unit, unit_length, "em") ||
                                  word_is(unit, unit_length, "rem")) ? amount * 16 / 1000 : -1;
                    if (px < 0)
                        ok = false;
                    else
                        ok = min ? sheet->viewport >= px : sheet->viewport <= px;
                } else {
                    ok = false;         /* Druck, dunkles Design, Zeigergeräte ...: gilt nicht */
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
            /* @supports, @font-face, @keyframes ...: übergehen */
            continue;
        }
        if (prelude_length)
            add_rule(sheet, prelude, prelude_length, block, block_length);
    }
}

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
    return sheet;
}

void css_free(css_sheet_t *sheet)
{
    if (!sheet)
        return;
    for (int i = 0; i < sheet->rule_count; i++)
        kfree(sheet->rules[i].compounds);
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
    return sheet ? sheet->rule_count : 0;
}
