/*
 * JavaScript: reguläre Ausdrücke. Das Muster wird in einen kleinen Befehlssatz
 * übersetzt und dann mit Zurücksetzen (Backtracking) ausgeführt - mit einer
 * Obergrenze für die Schritte, damit ein bösartiges Muster nichts aufhält.
 * Dazu die Methoden von String, die Muster annehmen (match, replace, split ...).
 */
#include "js_impl.h"
#include "memory.h"
#include "string.h"

#define ARGI(i) (J->bot + 2 + (i))
#define THISI (J->bot + 1)
#define MAGIC (js_callee(J)->u.n.magic)

enum { RF_GLOBAL = 1, RF_ICASE = 2, RF_MULTILINE = 4, RF_DOTALL = 8, RF_STICKY = 16, RF_UNICODE = 32 };

enum { I_CHAR, I_ANY, I_CLASS, I_BOL, I_EOL, I_WORDB, I_NWORDB, I_SAVE, I_SPLIT, I_JMP, I_BACKREF,
       I_ASSERT, I_ASSERT_END, I_BEHIND_END, I_MARK, I_CHECK, I_MATCH };

typedef struct {
    uint8_t op, flag;
    int32_t x, y;
} rinst;

typedef struct {
    uint32_t lo, hi;
} rrange;

typedef struct {
    rinst *code;
    rrange *ranges;
    char **names;                       /* Namen der Gruppen (NULL: unbenannt), oder NULL */
    int ncode, nranges, groups, marks;
    uint8_t flags;
} rprog;

#define CODE_MAX   30000
#define RANGES_MAX 20000
#define GROUPS_MAX 100
#define STEPS_MAX  3000000
#define BACK_MAX   300000

/* --- Muster lesen ---------------------------------------------------------------------------------- */

enum { R_EMPTY, R_CHAR, R_ANY, R_CLASS, R_BOL, R_EOL, R_WORDB, R_NWORDB, R_CAT, R_ALT, R_GROUP,
       R_REP, R_BACKREF, R_LOOK };

typedef struct rnode {
    uint8_t type;
    bool greedy, negate, behind;
    int min, max, index;
    uint32_t ch;
    int first, count;
    struct rnode *a, *b;
} rnode;

typedef struct rchunk {
    struct rchunk *next;
    int used;
    rnode nodes[128];
} rchunk;

typedef struct {
    js_state *J;
    const char *p, *end;
    rchunk *chunks;
    rrange *ranges;
    int nranges, ranges_cap;
    rinst *code;
    int ncode, code_cap;
    int groups, total_groups, marks, depth;
    uint8_t flags;
    bool failed;
    const char *error;
    char *names[GROUPS_MAX];            /* Namen der Gruppen in der Reihenfolge des Musters */
} rcomp;

static rnode *rfail(rcomp *c, const char *message)
{
    if (!c->failed) {
        c->failed = true;
        c->error = message;
    }
    return NULL;
}

static rnode *rnew(rcomp *c, int type)
{
    if (!c->chunks || c->chunks->used == 128) {
        rchunk *chunk = kzalloc(sizeof(rchunk));
        if (!chunk)
            return rfail(c, "Out of memory");
        chunk->next = c->chunks;
        c->chunks = chunk;
    }
    rnode *n = &c->chunks->nodes[c->chunks->used++];
    n->type = (uint8_t)type;
    return n;
}

static bool add_range(rcomp *c, uint32_t lo, uint32_t hi)
{
    if (c->nranges == c->ranges_cap) {
        int cap = c->ranges_cap ? c->ranges_cap * 2 : 64;
        if (cap > RANGES_MAX) {
            rfail(c, "Regular expression too large");
            return false;
        }
        rrange *bigger = kmalloc((size_t)cap * sizeof(rrange));
        if (!bigger) {
            rfail(c, "Out of memory");
            return false;
        }
        if (c->nranges)
            memcpy(bigger, c->ranges, (size_t)c->nranges * sizeof(rrange));
        kfree(c->ranges);
        c->ranges = bigger;
        c->ranges_cap = cap;
    }
    c->ranges[c->nranges].lo = lo;
    c->ranges[c->nranges].hi = hi;
    c->nranges++;
    return true;
}

/* \d \w \s und ihre Verneinungen als Bereiche */
static void add_class_escape(rcomp *c, char kind)
{
    static const rrange digit[] = { { '0', '9' } };
    static const rrange word[] = { { '0', '9' }, { 'A', 'Z' }, { '_', '_' }, { 'a', 'z' } };
    static const rrange space[] = { { 9, 13 }, { 32, 32 }, { 0xA0, 0xA0 }, { 0x1680, 0x1680 },
                                    { 0x2000, 0x200A }, { 0x2028, 0x2029 }, { 0x202F, 0x202F },
                                    { 0x205F, 0x205F }, { 0x3000, 0x3000 }, { 0xFEFF, 0xFEFF } };
    const rrange *set = digit;
    int count = 1;
    char lower = (char)(kind | 32);
    if (lower == 'w') {
        set = word;
        count = 4;
    } else if (lower == 's') {
        set = space;
        count = 10;
    }
    if (kind == lower) {
        for (int i = 0; i < count; i++)
            add_range(c, set[i].lo, set[i].hi);
        return;
    }
    uint32_t from = 0;                  /* Verneinung: die Lücken zwischen den Bereichen */
    for (int i = 0; i < count; i++) {
        if (set[i].lo > from)
            add_range(c, from, set[i].lo - 1);
        from = set[i].hi + 1;
    }
    add_range(c, from, 0x10FFFF);
}

/*
 * \p{...}: Zeichen nach ihrer Art. Ohne die vollständigen Tabellen von Unicode
 * - sie wären größer als die ganze Maschine - gelten hier Näherungen: die
 * großen, zusammenhängenden Bereiche jeder Art. Für lateinische, griechische
 * und kyrillische Texte stimmt das, bei seltenen Zeichen nicht immer.
 */
static const rrange p_letter[] = {
    { 'A', 'Z' }, { 'a', 'z' }, { 0xAA, 0xAA }, { 0xB5, 0xB5 }, { 0xBA, 0xBA }, { 0xC0, 0xD6 },
    { 0xD8, 0xF6 }, { 0xF8, 0x2C1 }, { 0x370, 0x373 }, { 0x376, 0x377 }, { 0x37B, 0x37D },
    { 0x386, 0x386 }, { 0x388, 0x3FF }, { 0x400, 0x481 }, { 0x48A, 0x52F }, { 0x531, 0x556 },
    { 0x561, 0x587 }, { 0x5D0, 0x5EA }, { 0x620, 0x64A }, { 0x66E, 0x6D3 }, { 0x904, 0x939 },
    { 0xE01, 0xE30 }, { 0x10A0, 0x10FF }, { 0x1100, 0x11FF }, { 0x1E00, 0x1FFF },
    { 0x2C00, 0x2DFF }, { 0x3041, 0x3096 }, { 0x30A1, 0x30FA }, { 0x3400, 0x4DBF },
    { 0x4E00, 0x9FFF }, { 0xA000, 0xA48C }, { 0xAC00, 0xD7A3 }, { 0xF900, 0xFAFF },
    { 0xFF21, 0xFF3A }, { 0xFF41, 0xFF5A }, { 0x20000, 0x2FA1F },
};
static const rrange p_upper[] = {
    { 'A', 'Z' }, { 0xC0, 0xD6 }, { 0xD8, 0xDE }, { 0x386, 0x386 }, { 0x388, 0x38F },
    { 0x391, 0x3A9 }, { 0x400, 0x42F }, { 0x531, 0x556 }, { 0x10A0, 0x10C5 }, { 0xFF21, 0xFF3A },
};
static const rrange p_lower[] = {
    { 'a', 'z' }, { 0xB5, 0xB5 }, { 0xDF, 0xF6 }, { 0xF8, 0xFF }, { 0x3AC, 0x3CE },
    { 0x430, 0x45F }, { 0x561, 0x587 }, { 0xFF41, 0xFF5A },
};
static const rrange p_digit[] = {
    { '0', '9' }, { 0x660, 0x669 }, { 0x6F0, 0x6F9 }, { 0x966, 0x96F }, { 0xFF10, 0xFF19 },
};
static const rrange p_number[] = {
    { '0', '9' }, { 0xB2, 0xB3 }, { 0xB9, 0xB9 }, { 0xBC, 0xBE }, { 0x660, 0x669 },
    { 0x6F0, 0x6F9 }, { 0x966, 0x96F }, { 0x2070, 0x2079 }, { 0x2080, 0x2089 },
    { 0x2150, 0x218B }, { 0x2460, 0x249B }, { 0x24EA, 0x24FF }, { 0x2776, 0x2793 },
    { 0x3007, 0x3007 }, { 0x3021, 0x3029 }, { 0xFF10, 0xFF19 },
};
static const rrange p_punct[] = {
    { '!', '#' }, { '%', '*' }, { ',', '/' }, { ':', ';' }, { '?', '@' }, { '[', ']' },
    { '_', '_' }, { '{', '{' }, { '}', '}' }, { 0xA1, 0xA1 }, { 0xA7, 0xA7 }, { 0xAB, 0xAB },
    { 0xB6, 0xB7 }, { 0xBB, 0xBB }, { 0xBF, 0xBF }, { 0x37E, 0x37E }, { 0x387, 0x387 },
    { 0x55A, 0x55F }, { 0x589, 0x58A }, { 0x5BE, 0x5BE }, { 0x60C, 0x60D }, { 0x61B, 0x61F },
    { 0x2010, 0x2027 }, { 0x2030, 0x2043 }, { 0x2045, 0x2051 }, { 0x2053, 0x205E },
    { 0x2E00, 0x2E4F }, { 0x3001, 0x3003 }, { 0x3008, 0x3011 }, { 0x3014, 0x301F },
    { 0xFE50, 0xFE6B }, { 0xFF01, 0xFF03 }, { 0xFF05, 0xFF0A }, { 0xFF0C, 0xFF0F },
    { 0xFF1A, 0xFF1B }, { 0xFF1F, 0xFF20 }, { 0xFF3B, 0xFF3D }, { 0xFF3F, 0xFF3F },
};
static const rrange p_symbol[] = {
    { '$', '$' }, { '+', '+' }, { '<', '>' }, { '^', '^' }, { '`', '`' }, { '|', '|' },
    { '~', '~' }, { 0xA2, 0xA6 }, { 0xA8, 0xA9 }, { 0xAC, 0xAC }, { 0xAE, 0xB1 }, { 0xB4, 0xB4 },
    { 0xB8, 0xB8 }, { 0xD7, 0xD7 }, { 0xF7, 0xF7 }, { 0x2044, 0x2044 }, { 0x2052, 0x2052 },
    { 0x20A0, 0x20C0 }, { 0x2100, 0x214F }, { 0x2190, 0x23FF }, { 0x2400, 0x243F },
    { 0x2500, 0x2775 }, { 0x2794, 0x27BF }, { 0x2900, 0x2BFF }, { 0x1F000, 0x1FAFF },
};
static const rrange p_space[] = {
    { 9, 13 }, { 32, 32 }, { 0x85, 0x85 }, { 0xA0, 0xA0 }, { 0x1680, 0x1680 }, { 0x2000, 0x200A },
    { 0x2028, 0x2029 }, { 0x202F, 0x202F }, { 0x205F, 0x205F }, { 0x3000, 0x3000 },
};
static const rrange p_separator[] = {
    { 32, 32 }, { 0xA0, 0xA0 }, { 0x1680, 0x1680 }, { 0x2000, 0x200A }, { 0x2028, 0x2029 },
    { 0x202F, 0x202F }, { 0x205F, 0x205F }, { 0x3000, 0x3000 },
};
static const rrange p_mark[] = {
    { 0x300, 0x36F }, { 0x483, 0x489 }, { 0x591, 0x5BD }, { 0x64B, 0x65F }, { 0x900, 0x903 },
    { 0x93A, 0x94F }, { 0xE31, 0xE3A }, { 0x1AB0, 0x1AFF }, { 0x1DC0, 0x1DFF }, { 0x20D0, 0x20F0 },
    { 0xFE00, 0xFE0F }, { 0xFE20, 0xFE2F },
};
static const rrange p_control[] = { { 0, 0x1F }, { 0x7F, 0x9F } };
static const rrange p_emoji[] = {
    { 0xA9, 0xA9 }, { 0xAE, 0xAE }, { 0x203C, 0x203C }, { 0x2049, 0x2049 }, { 0x2122, 0x2122 },
    { 0x2139, 0x2139 }, { 0x2194, 0x21AA }, { 0x231A, 0x23FF }, { 0x25AA, 0x25FE },
    { 0x2600, 0x27BF }, { 0x2934, 0x2935 }, { 0x2B05, 0x2B55 }, { 0x3030, 0x3030 },
    { 0x303D, 0x303D }, { 0x3297, 0x3299 }, { 0x1F000, 0x1FAFF },
};
static const rrange p_latin[] = {
    { 'A', 'Z' }, { 'a', 'z' }, { 0xAA, 0xAA }, { 0xBA, 0xBA }, { 0xC0, 0xD6 }, { 0xD8, 0xF6 },
    { 0xF8, 0x2B8 }, { 0x1E00, 0x1EFF }, { 0x2C60, 0x2C7F }, { 0xA720, 0xA7FF }, { 0xFF21, 0xFF3A },
    { 0xFF41, 0xFF5A },
};
static const rrange p_greek[] = { { 0x370, 0x3FF }, { 0x1F00, 0x1FFF } };
static const rrange p_cyrillic[] = { { 0x400, 0x52F }, { 0x1C80, 0x1C8F }, { 0x2DE0, 0x2DFF }, { 0xA640, 0xA69F } };
static const rrange p_han[] = { { 0x2E80, 0x2FDF }, { 0x3005, 0x3007 }, { 0x3400, 0x4DBF }, { 0x4E00, 0x9FFF },
                                { 0xF900, 0xFAFF }, { 0x20000, 0x2FA1F } };
static const rrange p_hiragana[] = { { 0x3041, 0x309F } };
static const rrange p_katakana[] = { { 0x30A0, 0x30FF }, { 0x31F0, 0x31FF }, { 0xFF66, 0xFF9F } };
static const rrange p_hangul[] = { { 0x1100, 0x11FF }, { 0x3130, 0x318F }, { 0xAC00, 0xD7AF } };
static const rrange p_arabic[] = { { 0x600, 0x6FF }, { 0x750, 0x77F }, { 0xFB50, 0xFDFF }, { 0xFE70, 0xFEFF } };
static const rrange p_hebrew[] = { { 0x591, 0x5F4 }, { 0xFB1D, 0xFB4F } };
static const rrange p_thai[] = { { 0xE01, 0xE5B } };
static const rrange p_any[] = { { 0, 0x10FFFF } };
static const rrange p_ascii[] = { { 0, 0x7F } };

#define PROP(name, set) { name, set, (int)(sizeof(set) / sizeof(set[0])) }
static const struct {
    const char *name;
    const rrange *set;
    int count;
} properties[] = {
    PROP("L", p_letter), PROP("Letter", p_letter), PROP("Alphabetic", p_letter), PROP("Alpha", p_letter),
    PROP("ID_Start", p_letter), PROP("XID_Start", p_letter),
    PROP("Lu", p_upper), PROP("Uppercase_Letter", p_upper), PROP("Uppercase", p_upper),
    PROP("Ll", p_lower), PROP("Lowercase_Letter", p_lower), PROP("Lowercase", p_lower),
    PROP("Nd", p_digit), PROP("Decimal_Number", p_digit), PROP("digit", p_digit),
    PROP("N", p_number), PROP("Number", p_number),
    PROP("P", p_punct), PROP("Punctuation", p_punct), PROP("punct", p_punct),
    PROP("S", p_symbol), PROP("Symbol", p_symbol),
    PROP("Z", p_separator), PROP("Separator", p_separator), PROP("Zs", p_separator),
    PROP("Space_Separator", p_separator), PROP("White_Space", p_space), PROP("space", p_space),
    PROP("M", p_mark), PROP("Mark", p_mark), PROP("Mn", p_mark), PROP("Nonspacing_Mark", p_mark),
    PROP("C", p_control), PROP("Cc", p_control), PROP("Control", p_control), PROP("Other", p_control),
    PROP("Emoji", p_emoji), PROP("Emoji_Presentation", p_emoji), PROP("Extended_Pictographic", p_emoji),
    PROP("Latin", p_latin), PROP("Latn", p_latin), PROP("Greek", p_greek), PROP("Grek", p_greek),
    PROP("Cyrillic", p_cyrillic), PROP("Cyrl", p_cyrillic), PROP("Han", p_han), PROP("Hani", p_han),
    PROP("Hiragana", p_hiragana), PROP("Hira", p_hiragana), PROP("Katakana", p_katakana),
    PROP("Kana", p_katakana), PROP("Hangul", p_hangul), PROP("Hang", p_hangul),
    PROP("Arabic", p_arabic), PROP("Arab", p_arabic), PROP("Hebrew", p_hebrew), PROP("Hebr", p_hebrew),
    PROP("Thai", p_thai), PROP("Any", p_any), PROP("ASCII", p_ascii),
};

/* Fügt die Bereiche der Eigenschaft Nummer id hinzu - oder alles außer ihnen */
static void add_property(rcomp *c, int id, bool negate)
{
    const rrange *set = properties[id].set;
    int count = properties[id].count;
    if (!negate) {
        for (int i = 0; i < count; i++)
            add_range(c, set[i].lo, set[i].hi);
        return;
    }
    uint32_t from = 0;                  /* die Bereiche stehen aufsteigend: die Lücken dazwischen */
    for (int i = 0; i < count; i++) {
        if (set[i].lo > from)
            add_range(c, from, set[i].lo - 1);
        from = set[i].hi + 1;
    }
    if (from <= 0x10FFFF)
        add_range(c, from, 0x10FFFF);
}

/* Liest "{Name}" oder "{Art=Name}" hinter \p; liefert die Nummer der Eigenschaft oder -1 */
static int read_property(rcomp *c)
{
    if (c->p >= c->end || *c->p != '{')
        return -1;
    const char *start = c->p + 1, *q = start;
    while (q < c->end && *q != '}')
        q++;
    if (q >= c->end)
        return -1;
    const char *name = start;
    for (const char *e = start; e < q; e++) {
        if (*e == '=')
            name = e + 1;               /* General_Category=L, Script=Latin, sc=Latn ... */
    }
    size_t n = (size_t)(q - name);
    for (unsigned i = 0; i < sizeof(properties) / sizeof(properties[0]); i++) {
        if (strlen(properties[i].name) == n && memcmp(properties[i].name, name, n) == 0) {
            c->p = q + 1;
            return (int)i;
        }
    }
    return -1;
}

static int rhex(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if ((ch | 32) >= 'a' && (ch | 32) <= 'f') return (ch | 32) - 'a' + 10;
    return -1;
}

static uint32_t next_char(rcomp *c)
{
    int size;
    uint32_t ch = js_utf8_decode(c->p, c->end, &size);
    c->p += size;
    return ch;
}

/*
 * Liest eine Maskierung hinter dem "\". Liefert das Zeichen - oder 0x110000 + x
 * für die Klassen d, w, s (und groß), 0x120000 + n für einen Rückbezug,
 * 0x130000/0x130001 für \b und \B, 0x140000 + nummer (+ 0x8000: verneint)
 * für eine Eigenschaft \p{...}.
 */
static uint32_t rescape(rcomp *c, bool in_class)
{
    if (c->p >= c->end) {
        rfail(c, "\\ at end of pattern");
        return 0;
    }
    char ch = *c->p;
    switch (ch) {
    case 'd': case 'D': case 'w': case 'W': case 's': case 'S':
        c->p++;
        return 0x110000 + (uint32_t)ch;
    case 'b':
        c->p++;
        return in_class ? 8 : 0x130000;
    case 'B':
        c->p++;
        return in_class ? 'B' : 0x130001;
    case 'n': c->p++; return '\n';
    case 'r': c->p++; return '\r';
    case 't': c->p++; return '\t';
    case 'v': c->p++; return '\v';
    case 'f': c->p++; return '\f';
    case 'p': case 'P':
        if (c->flags & RF_UNICODE) {
            c->p++;
            int id = read_property(c);
            if (id < 0) {
                rfail(c, "Invalid property name");
                return 0;
            }
            return 0x140000 + (uint32_t)id + (ch == 'P' ? 0x8000u : 0);
        }
        break;
    case 'k':
        /* \k<name>: Rückbezug auf eine benannte Gruppe */
        if (!in_class && c->p + 1 < c->end && c->p[1] == '<') {
            const char *start = c->p + 2, *q = start;
            while (q < c->end && *q != '>')
                q++;
            for (int i = 1; q < c->end && i < c->total_groups && i < GROUPS_MAX; i++) {
                if (c->names[i] && strlen(c->names[i]) == (size_t)(q - start) &&
                    memcmp(c->names[i], start, (size_t)(q - start)) == 0) {
                    c->p = q + 1;
                    return 0x120000 + (uint32_t)i;
                }
            }
        }
        break;
    case '0':
        if (c->p + 1 >= c->end || c->p[1] < '0' || c->p[1] > '9') {
            c->p++;
            return 0;
        }
        break;
    case 'c':
        if (c->p + 1 < c->end && ((c->p[1] | 32) >= 'a' && (c->p[1] | 32) <= 'z')) {
            c->p += 2;
            return (uint32_t)(c->p[-1] & 31);
        }
        c->p++;
        return 'c';
    case 'x': case 'u': {
        const char *start = c->p++;
        uint32_t code = 0;
        if (ch == 'u' && c->p < c->end && *c->p == '{') {
            c->p++;
            int count = 0;
            while (c->p < c->end && rhex(*c->p) >= 0 && count < 6) {
                code = code * 16 + (uint32_t)rhex(*c->p++);
                count++;
            }
            if (count && c->p < c->end && *c->p == '}') {
                c->p++;
                return code > 0x10FFFF ? 0xFFFD : code;
            }
        } else {
            int digits = ch == 'x' ? 2 : 4, i = 0;
            for (; i < digits && c->p < c->end && rhex(*c->p) >= 0; i++)
                code = code * 16 + (uint32_t)rhex(*c->p++);
            if (i == digits) {
                /* Ersatzpaar zusammenziehen */
                if (code >= 0xD800 && code <= 0xDBFF && c->end - c->p >= 6 && c->p[0] == '\\' &&
                    c->p[1] == 'u') {
                    uint32_t low = 0;
                    bool ok = true;
                    for (int k = 2; k < 6; k++) {
                        ok &= rhex(c->p[k]) >= 0;
                        low = low * 16 + (uint32_t)(rhex(c->p[k]) < 0 ? 0 : rhex(c->p[k]));
                    }
                    if (ok && low >= 0xDC00 && low <= 0xDFFF) {
                        c->p += 6;
                        return 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                    }
                }
                return code;
            }
        }
        c->p = start + 1;               /* keine gültige Angabe: der Buchstabe selbst */
        return (uint32_t)ch;
    }
    default:
        break;
    }
    if (ch >= '1' && ch <= '9' && !in_class) {
        int n = 0;
        const char *start = c->p;
        while (c->p < c->end && *c->p >= '0' && *c->p <= '9' && n < 1000)
            n = n * 10 + (*c->p++ - '0');
        if (n < c->total_groups)
            return 0x120000 + (uint32_t)n;
        c->p = start;
    }
    return next_char(c);
}

static rnode *rparse_alt(rcomp *c);

static rnode *rparse_class(rcomp *c)
{
    rnode *n = rnew(c, R_CLASS);
    if (!n)
        return NULL;
    if (c->p < c->end && *c->p == '^') {
        n->negate = true;
        c->p++;
    }
    n->first = c->nranges;
    while (!c->failed) {
        if (c->p >= c->end)
            return rfail(c, "Unterminated character class");
        if (*c->p == ']') {
            c->p++;
            break;
        }
        uint32_t lo;
        if (*c->p == '\\') {
            c->p++;
            lo = rescape(c, true);
            if (lo >= 0x140000) {
                add_property(c, (int)(lo & 0x7FFF), (lo & 0x8000) != 0);
                continue;
            }
            if (lo >= 0x110000) {
                add_class_escape(c, (char)(lo - 0x110000));
                continue;
            }
        } else {
            lo = next_char(c);
        }
        uint32_t hi = lo;
        if (c->end - c->p >= 2 && c->p[0] == '-' && c->p[1] != ']') {
            const char *dash = c->p++;
            if (*c->p == '\\') {
                c->p++;
                hi = rescape(c, true);
                if (hi >= 0x110000) {   /* [a-\d]: der Strich steht für sich */
                    add_range(c, lo, lo);
                    add_range(c, '-', '-');
                    if (hi >= 0x140000)
                        add_property(c, (int)(hi & 0x7FFF), (hi & 0x8000) != 0);
                    else
                        add_class_escape(c, (char)(hi - 0x110000));
                    continue;
                }
            } else {
                hi = next_char(c);
            }
            if (hi < lo) {
                c->p = dash;
                return rfail(c, "Range out of order in character class");
            }
        }
        add_range(c, lo, hi);
    }
    n->count = c->nranges - n->first;
    return n;
}

static rnode *rparse_atom(rcomp *c)
{
    char ch = *c->p;
    rnode *n;
    switch (ch) {
    case '.':
        c->p++;
        return rnew(c, R_ANY);
    case '^':
        c->p++;
        return rnew(c, R_BOL);
    case '$':
        c->p++;
        return rnew(c, R_EOL);
    case '[':
        c->p++;
        return rparse_class(c);
    case '(': {
        c->p++;
        bool capture = true, look = false, negate = false, behind = false;
        if (c->p < c->end && *c->p == '?') {
            c->p++;
            if (c->p >= c->end)
                return rfail(c, "Invalid group");
            if (*c->p == ':') {
                capture = false;
                c->p++;
            } else if (*c->p == '=' || *c->p == '!') {
                capture = false;
                look = true;
                negate = *c->p == '!';
                c->p++;
            } else if (*c->p == '<' && c->p + 1 < c->end && (c->p[1] == '=' || c->p[1] == '!')) {
                /* Rückwärtsblick: (?<=x) und (?<!x) */
                capture = false;
                look = true;
                behind = true;
                negate = c->p[1] == '!';
                c->p += 2;
            } else if (*c->p == '<') {
                while (c->p < c->end && *c->p != '>')   /* benannte Gruppe: Namen stehen schon in c->names */
                    c->p++;
                if (c->p >= c->end)
                    return rfail(c, "Invalid group name");
                c->p++;
            } else {
                return rfail(c, "Invalid group");
            }
        }
        int index = 0;
        if (capture) {
            if (c->groups >= GROUPS_MAX)
                return rfail(c, "Too many groups");
            index = c->groups++;
        }
        if (++c->depth > 200)
            return rfail(c, "Regular expression too deeply nested");
        rnode *inner = rparse_alt(c);
        c->depth--;
        if (c->failed)
            return NULL;
        if (c->p >= c->end || *c->p != ')')
            return rfail(c, "Unterminated group");
        c->p++;
        if (look) {
            n = rnew(c, R_LOOK);
            if (n) {
                n->a = inner;
                n->negate = negate;
                n->behind = behind;
            }
            return n;
        }
        if (!capture)
            return inner;
        n = rnew(c, R_GROUP);
        if (n) {
            n->a = inner;
            n->index = index;
        }
        return n;
    }
    case '\\': {
        c->p++;
        uint32_t code = rescape(c, false);
        if (c->failed)
            return NULL;
        if (code >= 0x140000) {
            n = rnew(c, R_CLASS);
            if (n) {
                n->first = c->nranges;
                add_property(c, (int)(code & 0x7FFF), (code & 0x8000) != 0);
                n->count = c->nranges - n->first;
            }
            return n;
        }
        if (code == 0x130000 || code == 0x130001)
            return rnew(c, code == 0x130000 ? R_WORDB : R_NWORDB);
        if (code >= 0x120000) {
            n = rnew(c, R_BACKREF);
            if (n)
                n->index = (int)(code - 0x120000);
            return n;
        }
        if (code >= 0x110000) {
            n = rnew(c, R_CLASS);
            if (n) {
                n->first = c->nranges;
                add_class_escape(c, (char)(code - 0x110000));
                n->count = c->nranges - n->first;
            }
            return n;
        }
        n = rnew(c, R_CHAR);
        if (n)
            n->ch = code;
        return n;
    }
    case '*': case '+': case '?':
        return rfail(c, "Nothing to repeat");
    default:
        n = rnew(c, R_CHAR);
        if (n)
            n->ch = next_char(c);
        return n;
    }
}

/* Liest {n}, {n,} oder {n,m}; false, wenn es keine Wiederholungsangabe ist */
static bool rparse_count(rcomp *c, int *min, int *max)
{
    const char *q = c->p + 1;
    int a = 0, b = -1, digits = 0;
    for (; q < c->end && *q >= '0' && *q <= '9'; q++, digits++)
        a = a > 100000 ? a : a * 10 + (*q - '0');
    if (digits == 0)
        return false;
    if (q < c->end && *q == ',') {
        q++;
        if (q < c->end && *q >= '0' && *q <= '9') {
            b = 0;
            for (; q < c->end && *q >= '0' && *q <= '9'; q++)
                b = b > 100000 ? b : b * 10 + (*q - '0');
        }
    } else {
        b = a;
    }
    if (q >= c->end || *q != '}')
        return false;
    c->p = q + 1;
    *min = a;
    *max = b;
    return true;
}

static rnode *rparse_seq(rcomp *c)
{
    rnode *seq = NULL;
    while (c->p < c->end && *c->p != '|' && *c->p != ')' && !c->failed) {
        rnode *atom = rparse_atom(c);
        if (c->failed || !atom)
            return NULL;
        int min = -1, max = -1;
        if (c->p < c->end) {
            if (*c->p == '*') { min = 0; max = -1; c->p++; }
            else if (*c->p == '+') { min = 1; max = -1; c->p++; }
            else if (*c->p == '?') { min = 0; max = 1; c->p++; }
            else if (*c->p == '{' && !rparse_count(c, &min, &max)) min = -1;
        }
        if (min >= 0) {
            if (max >= 0 && max < min)
                return rfail(c, "Numbers out of order in quantifier");
            if (atom->type == R_BOL || atom->type == R_EOL || atom->type == R_WORDB ||
                atom->type == R_NWORDB)
                return rfail(c, "Nothing to repeat");
            rnode *rep = rnew(c, R_REP);
            if (!rep)
                return NULL;
            rep->a = atom;
            rep->min = min;
            rep->max = max;
            rep->greedy = true;
            if (c->p < c->end && *c->p == '?') {
                rep->greedy = false;
                c->p++;
            }
            atom = rep;
        }
        if (!seq) {
            seq = atom;
        } else {
            rnode *cat = rnew(c, R_CAT);
            if (!cat)
                return NULL;
            cat->a = seq;
            cat->b = atom;
            seq = cat;
        }
    }
    return seq ? seq : rnew(c, R_EMPTY);
}

static rnode *rparse_alt(rcomp *c)
{
    rnode *left = rparse_seq(c);
    while (!c->failed && c->p < c->end && *c->p == '|') {
        c->p++;
        rnode *alt = rnew(c, R_ALT);
        if (!alt)
            return NULL;
        alt->a = left;
        alt->b = rparse_seq(c);
        left = alt;
    }
    return left;
}

/* --- Befehle erzeugen ------------------------------------------------------------------------------ */

static int emit(rcomp *c, int op, int flag, int x, int y)
{
    if (c->ncode == c->code_cap) {
        int cap = c->code_cap ? c->code_cap * 2 : 64;
        if (cap > CODE_MAX) {
            rfail(c, "Regular expression too large");
            return 0;
        }
        rinst *bigger = kmalloc((size_t)cap * sizeof(rinst));
        if (!bigger) {
            rfail(c, "Out of memory");
            return 0;
        }
        if (c->ncode)
            memcpy(bigger, c->code, (size_t)c->ncode * sizeof(rinst));
        kfree(c->code);
        c->code = bigger;
        c->code_cap = cap;
    }
    c->code[c->ncode].op = (uint8_t)op;
    c->code[c->ncode].flag = (uint8_t)flag;
    c->code[c->ncode].x = x;
    c->code[c->ncode].y = y;
    return c->ncode++;
}

static bool nullable(const rnode *n)
{
    if (!n)
        return true;
    switch (n->type) {
    case R_CHAR: case R_ANY: case R_CLASS: return false;
    case R_CAT: return nullable(n->a) && nullable(n->b);
    case R_ALT: return nullable(n->a) || nullable(n->b);
    case R_GROUP: return nullable(n->a);
    case R_REP: return n->min == 0 || nullable(n->a);
    default: return true;               /* Anker, Vorausschau, Rückbezug, leer */
    }
}

static void gen(rcomp *c, const rnode *n)
{
    if (c->failed || !n)
        return;
    if (++c->depth > 2000) {
        rfail(c, "Regular expression too deeply nested");
        return;
    }
    switch (n->type) {
    case R_EMPTY: break;
    case R_CHAR: emit(c, I_CHAR, 0, (int)n->ch, 0); break;
    case R_ANY: emit(c, I_ANY, 0, 0, 0); break;
    case R_CLASS: emit(c, I_CLASS, n->negate, n->first, n->count); break;
    case R_BOL: emit(c, I_BOL, 0, 0, 0); break;
    case R_EOL: emit(c, I_EOL, 0, 0, 0); break;
    case R_WORDB: emit(c, I_WORDB, 0, 0, 0); break;
    case R_NWORDB: emit(c, I_NWORDB, 0, 0, 0); break;
    case R_BACKREF: emit(c, I_BACKREF, 0, n->index, 0); break;
    case R_CAT:
        gen(c, n->a);
        gen(c, n->b);
        break;
    case R_ALT: {
        int split = emit(c, I_SPLIT, 0, 0, 0);
        gen(c, n->a);
        int jump = emit(c, I_JMP, 0, 0, 0);
        if (!c->failed) {
            c->code[split].x = split + 1;
            c->code[split].y = c->ncode;
        }
        gen(c, n->b);
        if (!c->failed)
            c->code[jump].x = c->ncode;
        break;
    }
    case R_GROUP:
        emit(c, I_SAVE, 0, 2 * n->index, 0);
        gen(c, n->a);
        emit(c, I_SAVE, 0, 2 * n->index + 1, 0);
        break;
    case R_LOOK: {
        int assert = emit(c, I_ASSERT, (n->negate ? 1 : 0) | (n->behind ? 2 : 0), 0, 0);
        gen(c, n->a);
        emit(c, n->behind ? I_BEHIND_END : I_ASSERT_END, 0, 0, 0);
        if (!c->failed)
            c->code[assert].x = c->ncode;
        break;
    }
    case R_REP: {
        for (int i = 0; i < n->min && !c->failed; i++)
            gen(c, n->a);
        if (n->max < 0) {
            /* L: split körper, ende; körper; jmp L */
            bool guard = nullable(n->a);
            int mark = guard ? c->marks++ : 0;
            int split = emit(c, I_SPLIT, 0, 0, 0);
            if (guard)
                emit(c, I_MARK, 0, mark, 0);
            gen(c, n->a);
            if (guard)
                emit(c, I_CHECK, 0, mark, 0);
            emit(c, I_JMP, 0, split, 0);
            if (!c->failed) {
                c->code[split].x = n->greedy ? split + 1 : c->ncode;
                c->code[split].y = n->greedy ? c->ncode : split + 1;
            }
        } else {
            /* (x(x(x)?)?)? - jede Stufe darf aussteigen */
            int count = n->max - n->min, first = c->ncode;
            for (int i = 0; i < count && !c->failed; i++) {
                emit(c, I_SPLIT, 0, 0, 0);
                gen(c, n->a);
            }
            if (!c->failed) {
                int end = c->ncode;
                for (int i = first; i < end; i++) {
                    if (c->code[i].op == I_SPLIT && c->code[i].x == 0 && c->code[i].y == 0) {
                        c->code[i].x = n->greedy ? i + 1 : end;
                        c->code[i].y = n->greedy ? end : i + 1;
                    }
                }
            }
        }
        break;
    }
    default:
        break;
    }
    c->depth--;
}

static rprog *compile(js_state *J, const js_string *source, uint8_t flags, const char **error)
{
    rcomp c;
    memset(&c, 0, sizeof(c));
    c.J = J;
    c.p = source->data;
    c.end = source->data + source->len;
    c.flags = flags;
    c.groups = 1;
    /* Gruppen vorab zählen: Rückbezüge können vor ihrer Gruppe stehen */
    c.total_groups = 1;
    for (const char *q = c.p; q < c.end; q++) {
        if (*q == '\\') {
            q++;
        } else if (*q == '[') {
            for (q++; q < c.end && *q != ']'; q++) {
                if (*q == '\\')
                    q++;
            }
        } else if (*q == '(' && (q + 1 >= c.end || q[1] != '?' ||
                                 (q + 2 < c.end && q[2] == '<' && q + 3 < c.end && q[3] != '=' &&
                                  q[3] != '!'))) {
            if (q + 2 < c.end && q[1] == '?' && c.total_groups < GROUPS_MAX) {
                /* (?<name>...): den Namen festhalten */
                const char *name = q + 3, *e = name;
                while (e < c.end && *e != '>')
                    e++;
                char *copy = e < c.end ? kmalloc((size_t)(e - name) + 1) : NULL;
                if (copy) {
                    memcpy(copy, name, (size_t)(e - name));
                    copy[e - name] = '\0';
                    c.names[c.total_groups] = copy;
                }
            }
            c.total_groups++;
        }
    }
    rnode *tree = rparse_alt(&c);
    if (!c.failed && c.p < c.end)
        rfail(&c, *c.p == ')' ? "Unmatched ')'" : "Invalid regular expression");
    if (!c.failed) {
        emit(&c, I_SAVE, 0, 0, 0);
        gen(&c, tree);
        emit(&c, I_SAVE, 0, 1, 0);
        emit(&c, I_MATCH, 0, 0, 0);
    }
    while (c.chunks) {
        rchunk *next = c.chunks->next;
        kfree(c.chunks);
        c.chunks = next;
    }
    rprog *prog = c.failed ? NULL : kzalloc(sizeof(rprog));
    bool named = false;
    for (int i = 0; i < GROUPS_MAX; i++)
        named |= c.names[i] != NULL;
    if (prog && named) {
        prog->names = kzalloc((size_t)c.groups * sizeof(char *));
        for (int i = 0; prog->names && i < c.groups && i < GROUPS_MAX; i++) {
            prog->names[i] = c.names[i];
            c.names[i] = NULL;
        }
    }
    for (int i = 0; i < GROUPS_MAX; i++)
        kfree(c.names[i]);
    if (!prog) {
        kfree(c.code);
        kfree(c.ranges);
        *error = c.error ? c.error : "Out of memory";
        return NULL;
    }
    prog->code = c.code;
    prog->ncode = c.ncode;
    prog->ranges = c.ranges;
    prog->nranges = c.nranges;
    prog->groups = c.groups;
    prog->marks = c.marks;
    prog->flags = flags;
    return prog;
}

/* --- Ausführen ------------------------------------------------------------------------------------- */

typedef struct {
    int pc, pos, slot, old;
} rback;

typedef struct {
    js_state *J;
    const rprog *prog;
    const char *text;
    int len;
    int *caps;
    rback *stack;
    int top, cap;
    uint32_t steps;
    bool failed;
    int target[34];                     /* Rückwärtsblick: Hier muss der Lauf dieser Tiefe enden */
} rmatch;

static uint32_t fold(uint32_t c)
{
    if (c >= 'A' && c <= 'Z')
        return c + 32;
    if ((c >= 0xC0 && c <= 0xDE && c != 0xD7) || (c >= 0x391 && c <= 0x3A9) ||
        (c >= 0x410 && c <= 0x42F))
        return c + 32;
    if (c >= 0x400 && c <= 0x40F)
        return c + 80;
    return c;
}

static uint32_t unfold(uint32_t c)
{
    if (c >= 'a' && c <= 'z')
        return c - 32;
    if ((c >= 0xE0 && c <= 0xFE && c != 0xF7) || (c >= 0x3B1 && c <= 0x3C9) ||
        (c >= 0x430 && c <= 0x44F))
        return c - 32;
    if (c >= 0x450 && c <= 0x45F)
        return c - 80;
    return c;
}

static bool in_class(const rprog *prog, const rinst *in, uint32_t c)
{
    const rrange *r = prog->ranges + in->x;
    bool found = false;
    for (int i = 0; i < in->y && !found; i++)
        found = c >= r[i].lo && c <= r[i].hi;
    if (!found && (prog->flags & RF_ICASE)) {
        uint32_t a = fold(c), b = unfold(c);
        for (int i = 0; i < in->y && !found; i++)
            found = (a >= r[i].lo && a <= r[i].hi) || (b >= r[i].lo && b <= r[i].hi);
    }
    return found != (in->flag != 0);
}

static bool is_word(const char *text, int len, int pos)
{
    if (pos < 0 || pos >= len)
        return false;
    unsigned char c = (unsigned char)text[pos];
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

static bool is_line_end(const char *text, int len, int pos)
{
    return pos < len && (text[pos] == '\n' || text[pos] == '\r');
}

static bool rpush(rmatch *m, int pc, int pos, int slot, int old)
{
    if (m->top == m->cap) {
        int cap = m->cap ? m->cap * 2 : 256;
        if (cap > BACK_MAX) {
            m->failed = true;
            return false;
        }
        rback *bigger = kmalloc((size_t)cap * sizeof(rback));
        if (!bigger) {
            m->failed = true;
            return false;
        }
        if (m->top)
            memcpy(bigger, m->stack, (size_t)m->top * sizeof(rback));
        kfree(m->stack);
        m->stack = bigger;
        m->cap = cap;
    }
    m->stack[m->top].pc = pc;
    m->stack[m->top].pos = pos;
    m->stack[m->top].slot = slot;
    m->stack[m->top].old = old;
    m->top++;
    return true;
}

/* Läuft ab pc/pos; liefert die Endstelle oder -1. depth: Schachtelung von Vorausschauen */
static int run(rmatch *m, int pc, int pos, int depth)
{
    const rprog *prog = m->prog;
    const char *text = m->text;
    int len = m->len, base = m->top;
    bool icase = (prog->flags & RF_ICASE) != 0;
    for (;;) {
        if (++m->steps > STEPS_MAX) {
            m->failed = true;
            return -1;
        }
        const rinst *in = &prog->code[pc];
        bool ok = true;
        switch (in->op) {
        case I_CHAR: {
            if (pos >= len) {
                ok = false;
                break;
            }
            int size;
            uint32_t c = js_utf8_decode(text + pos, text + len, &size);
            if (c == (uint32_t)in->x || (icase && fold(c) == fold((uint32_t)in->x))) {
                pos += size;
                pc++;
            } else {
                ok = false;
            }
            break;
        }
        case I_ANY: {
            if (pos >= len || (!(prog->flags & RF_DOTALL) && is_line_end(text, len, pos))) {
                ok = false;
                break;
            }
            int size;
            js_utf8_decode(text + pos, text + len, &size);
            pos += size;
            pc++;
            break;
        }
        case I_CLASS: {
            if (pos >= len) {
                ok = false;
                break;
            }
            int size;
            uint32_t c = js_utf8_decode(text + pos, text + len, &size);
            if (in_class(prog, in, c)) {
                pos += size;
                pc++;
            } else {
                ok = false;
            }
            break;
        }
        case I_BOL:
            ok = pos == 0 || ((prog->flags & RF_MULTILINE) && is_line_end(text, len, pos - 1));
            pc++;
            break;
        case I_EOL:
            ok = pos == len || ((prog->flags & RF_MULTILINE) && is_line_end(text, len, pos));
            pc++;
            break;
        case I_WORDB: case I_NWORDB:
            ok = (is_word(text, len, pos - 1) != is_word(text, len, pos)) == (in->op == I_WORDB);
            pc++;
            break;
        case I_SAVE: case I_MARK: {
            int slot = in->op == I_SAVE ? in->x : 2 * prog->groups + in->x;
            if (!rpush(m, 0, 0, slot, m->caps[slot]))
                return -1;
            m->caps[slot] = pos;
            pc++;
            break;
        }
        case I_CHECK:
            ok = m->caps[2 * prog->groups + in->x] != pos;     /* leere Runde: nicht noch einmal */
            pc++;
            break;
        case I_SPLIT:
            if (!rpush(m, in->y, pos, -1, 0))
                return -1;
            pc = in->x;
            break;
        case I_JMP:
            pc = in->x;
            break;
        case I_BACKREF: {
            int from = in->x < prog->groups ? m->caps[2 * in->x] : -1;
            int to = in->x < prog->groups ? m->caps[2 * in->x + 1] : -1;
            if (from >= 0 && to >= from) {
                int n = to - from;
                if (pos + n > len) {
                    ok = false;
                    break;
                }
                for (int i = 0; i < n && ok; i++) {
                    unsigned char a = (unsigned char)text[from + i], b = (unsigned char)text[pos + i];
                    ok = a == b || (icase && fold(a) == fold(b));
                }
                if (ok)
                    pos += n;
            }
            pc++;
            break;
        }
        case I_ASSERT: {
            if (depth > 30) {
                m->failed = true;
                return -1;
            }
            int end = -1;
            if (in->flag & 2) {
                /* Rückwärtsblick: Passt das Muster irgendwo davor so, dass es genau hier endet? */
                m->target[depth + 1] = pos;
                for (int start = pos; start >= 0 && end < 0 && !m->failed; start--) {
                    if (start < len && start > 0 && ((unsigned char)text[start] & 0xC0) == 0x80)
                        continue;       /* nur an Zeichengrenzen */
                    end = run(m, pc + 1, start, depth + 1);
                }
            } else {
                end = run(m, pc + 1, pos, depth + 1);
            }
            if (m->failed)
                return -1;
            ok = (end >= 0) != ((in->flag & 1) != 0);
            pc = in->x;
            break;
        }
        case I_BEHIND_END:
            if (pos != m->target[depth]) {
                ok = false;             /* endet nicht an der gefragten Stelle: andere Wege versuchen */
                break;
            }
            /* fall through */
        case I_ASSERT_END: case I_MATCH:
            /* was dieser Lauf an Rücksprüngen offen hat, verfällt */
            if (in->op != I_MATCH) {
                /* Fundstellen der Gruppen behalten: nur die Wahlpunkte dieses Laufs entfernen */
                int keep = base;
                for (int i = base; i < m->top; i++) {
                    if (m->stack[i].slot >= 0)
                        m->stack[keep++] = m->stack[i];
                }
                m->top = keep;
            }
            return pos;
        default:
            m->failed = true;
            return -1;
        }
        if (ok)
            continue;
        /* zurück zum letzten Wahlpunkt dieses Laufs */
        for (;;) {
            if (m->top <= base)
                return -1;
            rback *b = &m->stack[--m->top];
            if (b->slot >= 0) {
                m->caps[b->slot] = b->old;
            } else {
                pc = b->pc;
                pos = b->pos;
                break;
            }
        }
    }
}

int js_regex_groups(const js_object *re)
{
    const rprog *prog = re->u.re.prog;
    return prog ? prog->groups : 1;
}

int js_regex_exec(js_state *J, js_object *re, const js_string *text, uint32_t start, int *caps,
                  int max_groups)
{
    const rprog *prog = re->u.re.prog;
    if (!prog)
        return 0;
    int slots = 2 * prog->groups + prog->marks;
    int *work = kmalloc((size_t)(slots > 0 ? slots : 1) * sizeof(int));
    if (!work) {
        js_fatal(J, "Nicht genug Speicher für das Skript");
        return -1;
    }
    rmatch m;
    memset(&m, 0, sizeof(m));
    m.J = J;
    m.prog = prog;
    m.text = text->data;
    m.len = (int)text->len;
    m.caps = work;
    int found = 0;
    /* ein Muster, das mit einem festen Zeichen beginnt, kann nur dort anfangen */
    const rinst *first = prog->ncode > 1 ? &prog->code[1] : NULL;
    bool literal = first && first->op == I_CHAR && first->x < 0x80 && !(prog->flags & RF_ICASE);
    for (uint32_t at = start; at <= text->len; at++) {
        if (at > start && at < text->len && ((unsigned char)text->data[at] & 0xC0) == 0x80)
            continue;                   /* nur an Zeichengrenzen */
        if (literal && !(prog->flags & RF_STICKY)) {
            while (at < text->len && text->data[at] != (char)first->x)
                at++;
            if (at >= text->len)
                break;
        }
        for (int i = 0; i < slots; i++)
            work[i] = -1;
        m.top = 0;
        int end = run(&m, 0, (int)at, 0);
        if (m.failed)
            break;
        if (end >= 0) {
            found = 1;
            break;
        }
        if (prog->flags & RF_STICKY)
            break;
    }
    if (found) {
        for (int i = 0; i < max_groups; i++) {
            caps[2 * i] = i < prog->groups ? work[2 * i] : -1;
            caps[2 * i + 1] = i < prog->groups ? work[2 * i + 1] : -1;
        }
    }
    kfree(m.stack);
    kfree(work);
    if (m.failed) {
        js_throw(J, JS_E_ERROR, "Regular expression too complex");
        return -1;
    }
    return found;
}

void js_regex_free(js_state *J, js_object *o)
{
    (void)J;
    rprog *prog = o->u.re.prog;
    if (prog) {
        for (int i = 0; prog->names && i < prog->groups; i++)
            kfree(prog->names[i]);
        kfree(prog->names);
        kfree(prog->code);
        kfree(prog->ranges);
        kfree(prog);
    }
    o->u.re.prog = NULL;
}

js_object *js_regex_new(js_state *J, js_string *source, js_string *flags)
{
    uint8_t bits = 0;
    for (uint32_t i = 0; flags && i < flags->len; i++) {
        switch (flags->data[i]) {
        case 'g': bits |= RF_GLOBAL; break;
        case 'i': bits |= RF_ICASE; break;
        case 'm': bits |= RF_MULTILINE; break;
        case 's': bits |= RF_DOTALL; break;
        case 'y': bits |= RF_STICKY; break;
        case 'u': case 'v': bits |= RF_UNICODE; break;
        case 'd': break;
        default:
            js_throw(J, JS_E_SYNTAX, "Invalid regular expression flags");
            return NULL;
        }
    }
    const char *error = NULL;
    rprog *prog = compile(J, source, bits, &error);
    if (!prog) {
        js_throw(J, JS_E_SYNTAX, "Invalid regular expression: %s", error ? error : "");
        return NULL;
    }
    js_object *re = js_object_new(J, JS_C_REGEXP, J->regexp_proto);
    if (re == J->dummy) {
        js_object holder;
        holder.u.re.prog = prog;
        js_regex_free(J, &holder);
        return NULL;
    }
    re->u.re.prog = prog;
    re->u.re.source = source;
    re->u.re.flags = bits;
    js_define(J, re, J->k[K_lastIndex], js_num(0), JS_P_WRITE);
    return re;
}

/* --- RegExp-Objekte -------------------------------------------------------------------------------- */

static uint32_t chars_before(const js_string *s, uint32_t offset)
{
    if (s->ascii)
        return offset;
    uint32_t n = 0;
    for (uint32_t i = 0; i < offset && i < s->len; i++) {
        if (((unsigned char)s->data[i] & 0xC0) != 0x80)
            n++;
    }
    return n;
}

static bool regexp_ctor(js_state *J, int argc)
{
    js_value *pattern = js_arg(J, 0);
    js_string *source, *flags = J->empty;
    if (pattern->t == JS_T_OBJ && pattern->u.o->cls == JS_C_REGEXP) {
        if (!J->constructing && (argc < 2 || js_arg(J, 1)->t == JS_T_UNDEF))
            return js_push(J, *pattern);
        source = pattern->u.o->u.re.source;
        char text[8];
        int n = 0;
        uint8_t bits = pattern->u.o->u.re.flags;
        if (bits & RF_GLOBAL) text[n++] = 'g';
        if (bits & RF_ICASE) text[n++] = 'i';
        if (bits & RF_MULTILINE) text[n++] = 'm';
        if (bits & RF_DOTALL) text[n++] = 's';
        if (bits & RF_STICKY) text[n++] = 'y';
        flags = js_string_new(J, text, (size_t)n);
        js_push(J, js_str(flags));
    } else if (argc == 0 || pattern->t == JS_T_UNDEF) {
        source = js_string_new(J, "(?:)", 4);
        js_push(J, js_str(source));
    } else {
        if (!js_to_string(J, ARGI(0)))
            return false;
        source = js_arg(J, 0)->u.s;
    }
    if (argc > 1 && js_arg(J, 1)->t != JS_T_UNDEF) {
        if (!js_to_string(J, ARGI(1)))
            return false;
        flags = js_arg(J, 1)->u.s;
    }
    js_object *re = js_regex_new(J, source, flags);
    return re && js_push(J, js_obj(re));
}

static bool this_regexp(js_state *J, js_object **out)
{
    js_value *self = js_this(J);
    if (self->t != JS_T_OBJ || self->u.o->cls != JS_C_REGEXP)
        return js_throw(J, JS_E_TYPE, "RegExp method called on a value that is not a RegExp");
    *out = self->u.o;
    return true;
}

/* Liest lastIndex als Byte-Stelle in text; -1: hinter dem Ende */
static bool last_index(js_state *J, js_object *re, const js_string *text, int64_t *offset)
{
    js_prop *p = js_own(re, J->k[K_lastIndex]);
    double n = p && p->v.t == JS_T_NUM ? p->v.u.n : 0;
    if (!(re->u.re.flags & (RF_GLOBAL | RF_STICKY)))
        n = 0;
    if (n < 0 || js_isnan(n))
        n = 0;
    *offset = n > text->chars ? -1 : (int64_t)js_string_offset(text, (uint32_t)n);
    return true;
}

static void set_last_index(js_state *J, js_object *re, double value)
{
    js_prop *p = js_own(re, J->k[K_lastIndex]);
    if (p && !(p->flags & JS_P_ACCESSOR))
        p->v = js_num(value);
}

/* Baut das Ergebnis von exec: [treffer, gruppen...] mit index und input */
static bool push_match(js_state *J, js_object *re, js_string *text, const int *caps)
{
    int groups = js_regex_groups(re);
    js_object *a = js_array_new(J, (uint32_t)groups);
    js_push(J, js_obj(a));
    for (int i = 0; i < groups; i++) {
        js_value v = js_undef();
        if (caps[2 * i] >= 0 && caps[2 * i + 1] >= caps[2 * i])
            v = js_str(js_string_new(J, text->data + caps[2 * i],
                                     (size_t)(caps[2 * i + 1] - caps[2 * i])));
        if (!js_array_push(J, a, v))
            return false;
    }
    js_define(J, a, J->k[K_index], js_num(chars_before(text, (uint32_t)caps[0])), JS_P_ALL);
    js_define(J, a, J->k[K_input], js_str(text), JS_P_ALL);
    /* benannte Gruppen noch einmal unter ihrem Namen */
    const rprog *prog = re->u.re.prog;
    js_value named = js_undef();
    if (prog && prog->names) {
        js_object *g = js_object_new(J, JS_C_OBJECT, NULL);
        named = js_obj(g);
        js_define(J, a, js_intern_cstr(J, "groups"), named, JS_P_ALL);
        for (int i = 1; i < groups; i++) {
            if (prog->names[i])
                js_define(J, g, js_intern_cstr(J, prog->names[i]),
                          i < (int)a->u.arr.len ? a->u.arr.v[i] : js_undef(), JS_P_ALL);
        }
    } else {
        js_define(J, a, js_intern_cstr(J, "groups"), named, JS_P_ALL);
    }
    return !J->fatal;
}

/* exec für das Objekt re auf text; legt das Ergebnis (Feld oder null) auf den Stapel */
static bool regexp_exec_on(js_state *J, js_object *re, js_string *text)
{
    int caps[2 * GROUPS_MAX];
    int64_t start;
    last_index(J, re, text, &start);
    int found = start < 0 ? 0 : js_regex_exec(J, re, text, (uint32_t)start, caps, GROUPS_MAX);
    if (found < 0)
        return false;
    bool advances = (re->u.re.flags & (RF_GLOBAL | RF_STICKY)) != 0;
    if (!found) {
        if (advances)
            set_last_index(J, re, 0);
        return js_push(J, js_null());
    }
    if (advances)
        set_last_index(J, re, chars_before(text, (uint32_t)caps[1]));
    return push_match(J, re, text, caps);
}

static bool regexp_exec(js_state *J, int argc)
{
    (void)argc;
    js_object *re;
    if (!this_regexp(J, &re) || !js_to_string(J, ARGI(0)))
        return false;
    if (!regexp_exec_on(J, re, js_arg(J, 0)->u.s))
        return false;
    if (MAGIC == 1)                     /* test */
        *js_top(J, 0) = js_bool(js_top(J, 0)->t != JS_T_NULL);
    return true;
}

static bool regexp_to_string(js_state *J, int argc)
{
    (void)argc;
    js_object *re;
    if (!this_regexp(J, &re))
        return false;
    js_string *source = re->u.re.source;
    size_t cap = (size_t)source->len + 12, n = 0;
    char *out = js_alloc(J, cap);
    if (!out)
        return false;
    out[n++] = '/';
    memcpy(out + n, source->data, source->len);
    n += source->len;
    out[n++] = '/';
    uint8_t bits = re->u.re.flags;
    if (bits & RF_GLOBAL) out[n++] = 'g';
    if (bits & RF_ICASE) out[n++] = 'i';
    if (bits & RF_MULTILINE) out[n++] = 'm';
    if (bits & RF_DOTALL) out[n++] = 's';
    if (bits & RF_UNICODE) out[n++] = 'u';
    if (bits & RF_STICKY) out[n++] = 'y';
    js_string *s = js_string_new(J, out, n);
    js_release(J, out, cap);
    return js_push(J, js_str(s));
}

static bool regexp_property(js_state *J, int argc)
{
    (void)argc;
    js_value *self = js_this(J);
    if (self->t != JS_T_OBJ || self->u.o->cls != JS_C_REGEXP)
        return js_push(J, js_undef());
    js_object *re = self->u.o;
    switch (MAGIC) {
    case 0: return js_push(J, js_str(re->u.re.source));
    case 1: return js_push(J, js_bool(re->u.re.flags & RF_GLOBAL));
    case 2: return js_push(J, js_bool(re->u.re.flags & RF_ICASE));
    case 3: return js_push(J, js_bool(re->u.re.flags & RF_MULTILINE));
    case 4: return js_push(J, js_bool(re->u.re.flags & RF_STICKY));
    default: {
        char text[8];
        int n = 0;
        uint8_t bits = re->u.re.flags;
        if (bits & RF_GLOBAL) text[n++] = 'g';
        if (bits & RF_ICASE) text[n++] = 'i';
        if (bits & RF_MULTILINE) text[n++] = 'm';
        if (bits & RF_DOTALL) text[n++] = 's';
        if (bits & RF_UNICODE) text[n++] = 'u';
        if (bits & RF_STICKY) text[n++] = 'y';
        return js_push_lstr(J, text, (size_t)n);
    }
    }
}

/* --- Methoden von String mit Mustern --------------------------------------------------------------- */

typedef struct {
    js_state *J;
    char *text;
    size_t len, cap;
} sbuf;

static bool sbuf_put(sbuf *b, const char *text, size_t n)
{
    js_state *J = b->J;
    if (b->len + n > b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 256;
        while (cap < b->len + n)
            cap *= 2;
        if (cap > JS_STRING_MAX)
            return js_throw(J, JS_E_RANGE, "Invalid string length");
        char *bigger = js_alloc(J, cap);
        if (!bigger)
            return false;
        if (b->len)
            memcpy(bigger, b->text, b->len);
        js_release(J, b->text, b->cap);
        b->text = bigger;
        b->cap = cap;
    }
    if (n)
        memcpy(b->text + b->len, text, n);
    b->len += n;
    return true;
}

static bool this_text(js_state *J, js_string **out)
{
    js_value *self = js_this(J);
    if (self->t <= JS_T_NULL)
        return js_throw(J, JS_E_TYPE, "String method called on null or undefined");
    if (!js_to_string(J, THISI))
        return false;
    *out = js_this(J)->u.s;
    return true;
}

static bool is_regexp(const js_value *v)
{
    return v->t == JS_T_OBJ && v->u.o->cls == JS_C_REGEXP;
}

/* Fügt die Ersetzung für einen Treffer an: Funktion aufrufen oder $-Angaben auflösen */
static bool append_replacement(js_state *J, sbuf *out, js_string *text, const int *caps, int groups,
                               const js_value *with, const js_object *re)
{
    const rprog *prog = re ? re->u.re.prog : NULL;
    if (js_is_callable(with)) {
        js_push(J, *with);
        js_push(J, js_undef());
        for (int i = 0; i < groups; i++) {
            if (caps[2 * i] >= 0 && caps[2 * i + 1] >= caps[2 * i])
                js_push_lstr(J, text->data + caps[2 * i], (size_t)(caps[2 * i + 1] - caps[2 * i]));
            else
                js_push(J, js_undef());
        }
        js_push(J, js_num(chars_before(text, (uint32_t)caps[0])));
        js_push(J, js_str(text));
        if (!js_call(J, groups + 2) || !js_to_string(J, J->top - 1))
            return false;
        bool ok = sbuf_put(out, js_top(J, 0)->u.s->data, js_top(J, 0)->u.s->len);
        js_pop(J, 1);
        return ok;
    }
    const js_string *r = with->u.s;
    for (uint32_t i = 0; i < r->len; i++) {
        char c = r->data[i];
        if (c != '$' || i + 1 >= r->len) {
            if (!sbuf_put(out, &c, 1))
                return false;
            continue;
        }
        char d = r->data[i + 1];
        if (d == '$') {
            if (!sbuf_put(out, "$", 1))
                return false;
            i++;
        } else if (d == '&') {
            if (!sbuf_put(out, text->data + caps[0], (size_t)(caps[1] - caps[0])))
                return false;
            i++;
        } else if (d == '`') {
            if (!sbuf_put(out, text->data, (size_t)caps[0]))
                return false;
            i++;
        } else if (d == '\'') {
            if (!sbuf_put(out, text->data + caps[1], text->len - (size_t)caps[1]))
                return false;
            i++;
        } else if (d == '<' && prog && prog->names) {
            /* $<name>: der Text der benannten Gruppe */
            uint32_t close = i + 2;
            while (close < r->len && r->data[close] != '>')
                close++;
            if (close >= r->len) {
                if (!sbuf_put(out, "$", 1))
                    return false;
                continue;
            }
            for (int g = 1; g < groups; g++) {
                if (prog->names[g] && strlen(prog->names[g]) == close - i - 2 &&
                    memcmp(prog->names[g], r->data + i + 2, close - i - 2) == 0) {
                    if (caps[2 * g] >= 0 &&
                        !sbuf_put(out, text->data + caps[2 * g], (size_t)(caps[2 * g + 1] - caps[2 * g])))
                        return false;
                    break;
                }
            }
            i = close;
        } else if (d >= '0' && d <= '9') {
            int n = d - '0', used = 1;
            if (i + 2 < r->len && r->data[i + 2] >= '0' && r->data[i + 2] <= '9' &&
                n * 10 + (r->data[i + 2] - '0') < groups) {
                n = n * 10 + (r->data[i + 2] - '0');
                used = 2;
            }
            if (n >= 1 && n < groups) {
                if (caps[2 * n] >= 0 &&
                    !sbuf_put(out, text->data + caps[2 * n], (size_t)(caps[2 * n + 1] - caps[2 * n])))
                    return false;
                i += (uint32_t)used;
            } else if (!sbuf_put(out, "$", 1)) {
                return false;
            }
        } else if (!sbuf_put(out, "$", 1)) {
            return false;
        }
    }
    return true;
}

static bool string_replace(js_state *J, int argc)
{
    (void)argc;
    js_string *text;
    if (!this_text(J, &text))
        return false;
    bool all = MAGIC == 1;
    js_value *pattern = js_arg(J, 0), *with = js_arg(J, 1);
    if (!js_is_callable(with) && !js_to_string(J, ARGI(1)))
        return false;
    with = js_arg(J, 1);
    sbuf out = { J, NULL, 0, 0 };
    bool ok = true;
    uint32_t at = 0;
    int caps[2 * GROUPS_MAX];
    if (is_regexp(pattern)) {
        js_object *re = pattern->u.o;
        int groups = js_regex_groups(re);
        bool global = (re->u.re.flags & RF_GLOBAL) != 0;
        uint32_t from = 0;
        if (global)
            set_last_index(J, re, 0);
        while (ok && from <= text->len) {
            int found = js_regex_exec(J, re, text, from, caps, GROUPS_MAX);
            if (found < 0) {
                ok = false;
                break;
            }
            if (!found)
                break;
            ok = sbuf_put(&out, text->data + at, (size_t)caps[0] - at) &&
                 append_replacement(J, &out, text, caps, groups, with, re);
            at = (uint32_t)caps[1];
            if (!global)
                break;
            from = (uint32_t)caps[1];
            if (caps[1] == caps[0]) {   /* leerer Treffer: ein Zeichen weiter */
                from++;
                while (from < text->len && ((unsigned char)text->data[from] & 0xC0) == 0x80)
                    from++;
            }
        }
    } else {
        if (!js_to_string(J, ARGI(0)))
            return false;
        js_string *needle = js_arg(J, 0)->u.s;
        uint32_t from = 0;
        while (ok && from + needle->len <= text->len) {
            uint32_t hit = from;
            while (hit + needle->len <= text->len &&
                   memcmp(text->data + hit, needle->data, needle->len) != 0)
                hit++;
            if (hit + needle->len > text->len)
                break;
            caps[0] = (int)hit;
            caps[1] = (int)(hit + needle->len);
            ok = sbuf_put(&out, text->data + at, hit - at) &&
                 append_replacement(J, &out, text, caps, 1, with, NULL);
            at = hit + needle->len;
            from = needle->len ? at : at + 1;
            if (!all)
                break;
            if (!needle->len && at < text->len) {
                ok = ok && sbuf_put(&out, text->data + at, 1);
                at++;
            }
        }
    }
    ok = ok && (at >= text->len || sbuf_put(&out, text->data + at, text->len - at));
    if (ok)
        ok = js_push_lstr(J, out.text, out.len);
    js_release(J, out.text, out.cap);
    return ok;
}

/* Macht aus dem ersten Argument ein Muster-Objekt (aus einer Zeichenkette ein neues) */
static bool arg_regexp(js_state *J, const char *flags, js_object **out)
{
    js_value *v = js_arg(J, 0);
    if (is_regexp(v)) {
        *out = v->u.o;
        return true;
    }
    js_string *source;
    if (v->t == JS_T_UNDEF) {
        source = js_string_new(J, "(?:)", 4);
        *v = js_str(source);
    } else {
        if (!js_to_string(J, ARGI(0)))
            return false;
        source = js_arg(J, 0)->u.s;
    }
    js_string *f = js_string_new(J, flags, strlen(flags));
    js_push(J, js_str(f));
    js_object *re = js_regex_new(J, source, f);
    if (!re)
        return false;
    *js_top(J, 0) = js_obj(re);         /* bleibt auf dem Stapel erreichbar */
    *out = re;
    return true;
}

static bool string_match(js_state *J, int argc)
{
    (void)argc;
    js_string *text;
    js_object *re;
    if (!this_text(J, &text) || !arg_regexp(J, MAGIC == 2 ? "g" : "", &re))
        return false;
    bool global = (re->u.re.flags & RF_GLOBAL) != 0;
    if (MAGIC == 1) {                   /* search */
        int caps[2 * GROUPS_MAX];
        int found = js_regex_exec(J, re, text, 0, caps, GROUPS_MAX);
        if (found < 0)
            return false;
        return js_push(J, js_num(found ? (double)chars_before(text, (uint32_t)caps[0]) : -1));
    }
    if (MAGIC == 0 && !global)
        return regexp_exec_on(J, re, text);
    /* alle Treffer: match mit "g" liefert die Texte, matchAll die vollständigen Ergebnisse */
    js_object *out = js_array_new(J, 0);
    js_push(J, js_obj(out));
    uint32_t from = 0;
    int caps[2 * GROUPS_MAX];
    while (from <= text->len) {
        int found = js_regex_exec(J, re, text, from, caps, GROUPS_MAX);
        if (found < 0)
            return false;
        if (!found)
            break;
        if (MAGIC == 2) {
            if (!push_match(J, re, text, caps) || !js_array_push(J, out, *js_top(J, 0)))
                return false;
            js_pop(J, 1);
        } else if (!js_array_push(J, out, js_str(js_string_new(J, text->data + caps[0],
                                                                (size_t)(caps[1] - caps[0]))))) {
            return false;
        }
        from = (uint32_t)caps[1];
        if (caps[1] == caps[0]) {
            from++;
            while (from < text->len && ((unsigned char)text->data[from] & 0xC0) == 0x80)
                from++;
        }
    }
    set_last_index(J, re, 0);
    if (MAGIC == 0 && out->u.arr.len == 0)
        return js_push(J, js_null());
    if (MAGIC == 2) {
        /* matchAll: ein Iterator über die Treffer */
        if (!js_get(J, J->top - 1, J->sym[SYM_iterator]))
            return false;
        js_push(J, js_obj(out));
        return js_call(J, 0);
    }
    return true;
}

static bool string_split(js_state *J, int argc)
{
    js_string *text;
    if (!this_text(J, &text))
        return false;
    double limit = 4294967295.0;
    if (argc > 1 && js_arg(J, 1)->t != JS_T_UNDEF) {
        if (!js_to_number(J, ARGI(1), &limit))
            return false;
        limit = js_isnan(limit) || limit < 0 ? 4294967295.0 : js_trunc(limit);
    }
    js_object *out = js_array_new(J, 0);
    js_push(J, js_obj(out));
    js_value *sep = js_arg(J, 0);
    if (limit == 0)
        return true;
    if (sep->t == JS_T_UNDEF)
        return js_array_push(J, out, js_str(text));
    if (is_regexp(sep)) {
        js_object *re = sep->u.o;
        int groups = js_regex_groups(re), caps[2 * GROUPS_MAX];
        uint32_t at = 0, from = 0;
        if (text->len == 0) {
            int found = js_regex_exec(J, re, text, 0, caps, GROUPS_MAX);
            if (found < 0)
                return false;
            return found ? true : js_array_push(J, out, js_str(text));
        }
        while (from < text->len) {
            int found = js_regex_exec(J, re, text, from, caps, GROUPS_MAX);
            if (found < 0)
                return false;
            if (!found || (uint32_t)caps[0] >= text->len)
                break;
            if (caps[1] == caps[0] && (uint32_t)caps[0] == at) {
                /* leerer Treffer am Stückanfang: ein Zeichen weiter suchen */
                from = (uint32_t)caps[0] + 1;
                while (from < text->len && ((unsigned char)text->data[from] & 0xC0) == 0x80)
                    from++;
                continue;
            }
            if (!js_array_push(J, out, js_str(js_string_new(J, text->data + at,
                                                            (size_t)caps[0] - at))))
                return false;
            for (int g = 1; g < groups && out->u.arr.len < limit; g++) {
                js_value v = js_undef();
                if (caps[2 * g] >= 0)
                    v = js_str(js_string_new(J, text->data + caps[2 * g],
                                             (size_t)(caps[2 * g + 1] - caps[2 * g])));
                if (!js_array_push(J, out, v))
                    return false;
            }
            if (out->u.arr.len >= limit)
                return true;
            at = (uint32_t)caps[1];
            from = caps[1] > caps[0] ? at : at + 1;
        }
        return js_array_push(J, out, js_str(js_string_new(J, text->data + at, text->len - at)));
    }
    if (!js_to_string(J, ARGI(0)))
        return false;
    js_string *needle = js_arg(J, 0)->u.s;
    if (needle->len == 0) {             /* in einzelne Zeichen */
        for (uint32_t at = 0; at < text->len && out->u.arr.len < limit;) {
            uint32_t end = at + 1;
            while (end < text->len && ((unsigned char)text->data[end] & 0xC0) == 0x80)
                end++;
            if (!js_array_push(J, out, js_str(js_string_new(J, text->data + at, end - at))))
                return false;
            at = end;
        }
        return true;
    }
    uint32_t at = 0;
    for (uint32_t i = 0; i + needle->len <= text->len;) {
        if (memcmp(text->data + i, needle->data, needle->len) == 0) {
            if (!js_array_push(J, out, js_str(js_string_new(J, text->data + at, i - at))))
                return false;
            if (out->u.arr.len >= limit)
                return true;
            i += needle->len;
            at = i;
        } else {
            i++;
        }
    }
    return js_array_push(J, out, js_str(js_string_new(J, text->data + at, text->len - at)));
}

void js_regex_init(js_state *J)
{
    J->regexp_proto = js_object_new(J, JS_C_OBJECT, J->object_proto);
    js_object *ctor = js_native_new(J, regexp_ctor, "RegExp", 2, 0);
    js_define(J, ctor, J->k[K_prototype], js_obj(J->regexp_proto), 0);
    js_define(J, J->regexp_proto, J->k[K_constructor], js_obj(ctor), JS_P_WRITE | JS_P_CONF);
    js_def_value(J, J->global, "RegExp", js_obj(ctor), JS_P_WRITE | JS_P_CONF);
    js_def_native(J, J->regexp_proto, "exec", regexp_exec, 1, 0);
    js_def_native(J, J->regexp_proto, "test", regexp_exec, 1, 1);
    js_def_native(J, J->regexp_proto, "toString", regexp_to_string, 0, 0);
    static const char *const props[] = { "source", "global", "ignoreCase", "multiline", "sticky",
                                         "flags" };
    for (int i = 0; i < 6; i++)
        js_def_accessor(J, J->regexp_proto, props[i], regexp_property, NULL, i);

    js_def_native(J, J->string_proto, "replace", string_replace, 2, 0);
    js_def_native(J, J->string_proto, "replaceAll", string_replace, 2, 1);
    js_def_native(J, J->string_proto, "match", string_match, 1, 0);
    js_def_native(J, J->string_proto, "search", string_match, 1, 1);
    js_def_native(J, J->string_proto, "matchAll", string_match, 1, 2);
    js_def_native(J, J->string_proto, "split", string_split, 2, 0);
}
