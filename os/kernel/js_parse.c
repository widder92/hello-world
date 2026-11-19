/* JavaScript: Quelltext lesen und in einen Programmbaum umsetzen - siehe js.h */
#include "js_impl.h"
#include "memory.h"
#include "string.h"

enum { TK_EOF, TK_NUM, TK_STR, TK_NAME, TK_PUNCT };

enum {
    KW_NONE, KW_BREAK, KW_CASE, KW_CATCH, KW_CLASS, KW_CONST, KW_CONTINUE, KW_DEBUGGER, KW_DEFAULT,
    KW_DELETE, KW_DO, KW_ELSE, KW_EXTENDS, KW_FALSE, KW_FINALLY, KW_FOR, KW_FUNCTION, KW_IF,
    KW_IN, KW_INSTANCEOF, KW_NEW, KW_NULL, KW_RETURN, KW_SUPER, KW_SWITCH, KW_THIS, KW_THROW,
    KW_TRUE, KW_TRY, KW_TYPEOF, KW_VAR, KW_VOID, KW_WHILE, KW_WITH, KW_IMPORT, KW_EXPORT, KW_ENUM,
    KW_RESERVED_END,
    /* nur an bestimmten Stellen besonders, sonst gewöhnliche Namen */
    KW_LET, KW_STATIC, KW_OF, KW_GET, KW_SET, KW_ASYNC, KW_AWAIT, KW_YIELD,
};

static const struct {
    const char *text;
    uint8_t kw;
} keywords[] = {
    { "break", KW_BREAK }, { "case", KW_CASE }, { "catch", KW_CATCH }, { "class", KW_CLASS },
    { "const", KW_CONST }, { "continue", KW_CONTINUE }, { "debugger", KW_DEBUGGER },
    { "default", KW_DEFAULT }, { "delete", KW_DELETE }, { "do", KW_DO }, { "else", KW_ELSE },
    { "extends", KW_EXTENDS }, { "false", KW_FALSE }, { "finally", KW_FINALLY }, { "for", KW_FOR },
    { "function", KW_FUNCTION }, { "if", KW_IF }, { "in", KW_IN }, { "instanceof", KW_INSTANCEOF },
    { "new", KW_NEW }, { "null", KW_NULL }, { "return", KW_RETURN }, { "super", KW_SUPER },
    { "switch", KW_SWITCH }, { "this", KW_THIS }, { "throw", KW_THROW }, { "true", KW_TRUE },
    { "try", KW_TRY }, { "typeof", KW_TYPEOF }, { "var", KW_VAR }, { "void", KW_VOID },
    { "while", KW_WHILE }, { "with", KW_WITH }, { "import", KW_IMPORT }, { "export", KW_EXPORT },
    { "enum", KW_ENUM }, { "let", KW_LET }, { "static", KW_STATIC }, { "of", KW_OF },
    { "get", KW_GET }, { "set", KW_SET }, { "async", KW_ASYNC }, { "await", KW_AWAIT },
    { "yield", KW_YIELD },
};

enum {
    P_LBRACE, P_RBRACE, P_LPAREN, P_RPAREN, P_LBRACKET, P_RBRACKET, P_SEMI, P_COMMA, P_LT, P_GT,
    P_LE, P_GE, P_EQ, P_NE, P_SEQ, P_SNE, P_PLUS, P_MINUS, P_STAR, P_SLASH, P_PERCENT, P_POW,
    P_INC, P_DEC, P_SHL, P_SHR, P_USHR, P_AND, P_OR, P_XOR, P_NOT, P_TILDE, P_LAND, P_LOR,
    P_NULLISH, P_QUESTION, P_OPTCHAIN, P_COLON, P_ASSIGN, P_ADD_A, P_SUB_A, P_MUL_A, P_DIV_A,
    P_MOD_A, P_POW_A, P_SHL_A, P_SHR_A, P_USHR_A, P_AND_A, P_OR_A, P_XOR_A, P_LAND_A, P_LOR_A,
    P_NULLISH_A, P_ARROW, P_DOT, P_ELLIPSIS, P_BACKTICK,
};

/* längere zuerst: Der erste Treffer gilt */
static const struct {
    const char *text;
    uint8_t id;
} puncts[] = {
    { ">>>=", P_USHR_A }, { "...", P_ELLIPSIS }, { "===", P_SEQ }, { "!==", P_SNE },
    { "**=", P_POW_A }, { "<<=", P_SHL_A }, { ">>=", P_SHR_A }, { ">>>", P_USHR },
    { "&&=", P_LAND_A }, { "||=", P_LOR_A }, { "?\?=", P_NULLISH_A }, { "=>", P_ARROW },
    { "==", P_EQ }, { "!=", P_NE }, { "<=", P_LE }, { ">=", P_GE }, { "&&", P_LAND },
    { "||", P_LOR }, { "??", P_NULLISH }, { "?.", P_OPTCHAIN }, { "++", P_INC }, { "--", P_DEC },
    { "+=", P_ADD_A }, { "-=", P_SUB_A }, { "*=", P_MUL_A }, { "/=", P_DIV_A }, { "%=", P_MOD_A },
    { "&=", P_AND_A }, { "|=", P_OR_A }, { "^=", P_XOR_A }, { "<<", P_SHL }, { ">>", P_SHR },
    { "**", P_POW }, { "{", P_LBRACE }, { "}", P_RBRACE }, { "(", P_LPAREN }, { ")", P_RPAREN },
    { "[", P_LBRACKET }, { "]", P_RBRACKET }, { ";", P_SEMI }, { ",", P_COMMA }, { "<", P_LT },
    { ">", P_GT }, { "+", P_PLUS }, { "-", P_MINUS }, { "*", P_STAR }, { "/", P_SLASH },
    { "%", P_PERCENT }, { "&", P_AND }, { "|", P_OR }, { "^", P_XOR }, { "!", P_NOT },
    { "~", P_TILDE }, { "?", P_QUESTION }, { ":", P_COLON }, { "=", P_ASSIGN }, { ".", P_DOT },
    { "`", P_BACKTICK },
};

/* Sammelt, was in einer Funktion vereinbart wird */
typedef struct fnctx {
    struct fnctx *outer;
    js_string **vars;
    js_node **funcs;
    int nvars, vars_cap, nfuncs, funcs_cap;
    bool arrow, uses_arguments, uses_new_target;
    bool generator, is_async;           /* hier gelten "yield" und "await" als Schlüsselwörter */
} fnctx;

#define PF_NO_PARAMS 0x100              /* für function(): gleich der Rumpf, keine Parameterliste */

typedef struct {
    js_state *J;
    const char *src, *end, *pos;
    int line;
    /* das laufende Zeichen (Token) */
    int tk, kw, punct;
    bool nl;                            /* davor stand ein Zeilenwechsel */
    bool at_start;
    bool private_name;                  /* TK_NAME: ein privater Name (#x); str enthält das "#" */
    const char *tk_start;
    double num;
    js_string *str;

    bool failed, peeking, no_in;
    int depth;
    fnctx *fn;
    char *buf;                          /* Zwischenspeicher für Zeichenketten */
    size_t buf_len, buf_cap;
} parser;

typedef struct {
    const char *pos, *tk_start;
    int line, tk, kw, punct;
    bool nl, at_start, private_name;
    double num;
    js_string *str;
} lex_state;

/* --- Fehler, Speicher ------------------------------------------------------------------------ */

static void *fail(parser *p, const char *message)
{
    if (!p->failed && !p->peeking) {
        p->failed = true;
        p->J->line = p->line;
        js_throw(p->J, JS_E_SYNTAX, "%s", message);
    }
    if (p->peeking)
        p->failed = true;
    p->tk = TK_EOF;                     /* alles Weitere läuft ins Leere */
    return NULL;
}

static js_node *node(parser *p, int type)
{
    js_node *n = js_arena_alloc(p->J, sizeof(js_node));
    if (!n) {
        p->failed = true;
        p->tk = TK_EOF;
        /* gültiger Platz, damit niemand über NULL stolpert; das Ergebnis wird verworfen */
        static js_node spare;
        memset(&spare, 0, sizeof(spare));
        return &spare;
    }
    n->type = (uint8_t)type;
    n->op = OP_NONE;
    n->line = p->line;
    return n;
}

static js_string *pin(js_string *s)
{
    s->gc.pinned = 1;
    return s;
}

static bool buf_put(parser *p, const char *bytes, size_t n)
{
    if (p->peeking)
        return true;
    if (p->buf_len + n > p->buf_cap) {
        size_t cap = p->buf_cap ? p->buf_cap * 2 : 256;
        while (cap < p->buf_len + n)
            cap *= 2;
        if (cap > JS_STRING_MAX) {
            fail(p, "String literal too long");
            return false;
        }
        char *bigger = kmalloc(cap);
        if (!bigger) {
            js_fatal(p->J, "Nicht genug Speicher für das Skript");
            p->failed = true;
            return false;
        }
        if (p->buf_len)
            memcpy(bigger, p->buf, p->buf_len);
        kfree(p->buf);
        p->buf = bigger;
        p->buf_cap = cap;
    }
    memcpy(p->buf + p->buf_len, bytes, n);
    p->buf_len += n;
    return true;
}

static void buf_char(parser *p, uint32_t c)
{
    char utf8[4];
    buf_put(p, utf8, (size_t)js_utf8_encode(c, utf8));
}

/* --- Zeichen lesen --------------------------------------------------------------------------- */

static bool name_start(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$' || c >= 0x80;
}

static bool name_part(unsigned char c)
{
    return name_start(c) || (c >= '0' && c <= '9');
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Liest eine Maskierung hinter dem "\" - für Zeichenketten und Vorlagen */
static void escape(parser *p)
{
    if (p->pos >= p->end) {
        fail(p, "Unterminated string");
        return;
    }
    char c = *p->pos++;
    switch (c) {
    case 'n': buf_char(p, '\n'); return;
    case 't': buf_char(p, '\t'); return;
    case 'r': buf_char(p, '\r'); return;
    case 'b': buf_char(p, '\b'); return;
    case 'f': buf_char(p, '\f'); return;
    case 'v': buf_char(p, '\v'); return;
    case '0':
        if (p->pos >= p->end || *p->pos < '0' || *p->pos > '9') {
            buf_char(p, 0);
            return;
        }
        break;
    case '\r':
        if (p->pos < p->end && *p->pos == '\n')
            p->pos++;
        p->line++;
        return;                         /* Fortsetzung in der nächsten Zeile */
    case '\n':
        p->line++;
        return;
    case 'x': case 'u': {
        uint32_t code = 0;
        int digits = c == 'x' ? 2 : 4;
        if (c == 'u' && p->pos < p->end && *p->pos == '{') {
            p->pos++;
            int count = 0;
            while (p->pos < p->end && *p->pos != '}') {
                int h = hex_digit(*p->pos++);
                if (h < 0 || ++count > 6) {
                    fail(p, "Invalid Unicode escape sequence");
                    return;
                }
                code = code * 16 + (uint32_t)h;
            }
            if (p->pos >= p->end || count == 0) {
                fail(p, "Invalid Unicode escape sequence");
                return;
            }
            p->pos++;
        } else {
            for (int i = 0; i < digits; i++) {
                int h = p->pos < p->end ? hex_digit(*p->pos) : -1;
                if (h < 0) {
                    fail(p, "Invalid escape sequence");
                    return;
                }
                p->pos++;
                code = code * 16 + (uint32_t)h;
            }
            /* Ersatzpaar 😀: zu einem Zeichen zusammenziehen */
            if (code >= 0xD800 && code <= 0xDBFF && p->end - p->pos >= 6 && p->pos[0] == '\\' &&
                p->pos[1] == 'u') {
                uint32_t low = 0;
                bool ok = true;
                for (int i = 0; i < 4; i++) {
                    int h = hex_digit(p->pos[2 + i]);
                    ok &= h >= 0;
                    low = low * 16 + (uint32_t)(h < 0 ? 0 : h);
                }
                if (ok && low >= 0xDC00 && low <= 0xDFFF) {
                    code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                    p->pos += 6;
                }
            }
        }
        buf_char(p, code);
        return;
    }
    default:
        break;
    }
    /* alles andere steht für sich selbst (auch Mehrbyte-Zeichen) */
    buf_put(p, &c, 1);
}

static void lex_string(parser *p, char quote)
{
    p->buf_len = 0;
    for (;;) {
        if (p->pos >= p->end || *p->pos == '\n') {
            fail(p, "Unterminated string");
            return;
        }
        char c = *p->pos++;
        if (c == quote)
            break;
        if (c == '\\') {
            escape(p);
            if (p->failed)
                return;
        } else {
            buf_put(p, &c, 1);
        }
    }
    p->tk = TK_STR;
    p->str = p->peeking ? p->J->empty : pin(js_string_new(p->J, p->buf, p->buf_len));
}

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static void lex_number(parser *p)
{
    const char *s = p->pos;
    double value = 0;
    if (s[0] == '0' && p->end - s > 2 && ((s[1] | 32) == 'x' || (s[1] | 32) == 'o' ||
                                          (s[1] | 32) == 'b')) {
        int base = (s[1] | 32) == 'x' ? 16 : (s[1] | 32) == 'o' ? 8 : 2;
        const char *q = s + 2;
        int count = 0;
        for (; q < p->end; q++) {
            int d = hex_digit(*q);
            if (*q == '_' && count > 0 && q + 1 < p->end && hex_digit(q[1]) >= 0)
                continue;               /* Trennstrich zwischen Ziffern: 0xFF_FF */
            if (d < 0 || d >= base)
                break;
            value = value * base + d;
            count++;
        }
        if (count == 0) {
            fail(p, "Invalid number");
            return;
        }
        p->pos = q;
    } else {
        /* Trennstriche (1_000_000) herausnehmen; ends[k]: so weit reicht die Quelle nach k Zeichen */
        char clean[100];
        uint16_t ends[100];
        size_t n = 0, i = 0, len = (size_t)(p->end - s), used = 0;
        bool separated = false;
        for (; i < len && n + 1 < sizeof(clean); i++) {
            char c = s[i];
            if (c == '_' && i > 0 && is_digit(s[i - 1]) && i + 1 < len && is_digit(s[i + 1])) {
                separated = true;
                continue;
            }
            if (!is_digit(c) && c != '.' && (c | 32) != 'e' &&
                !((c == '+' || c == '-') && i > 0 && (s[i - 1] | 32) == 'e'))
                break;
            clean[n] = c;
            ends[n++] = (uint16_t)(i + 1);
        }
        if (separated) {
            value = js_text_to_number(clean, n, &used);
            used = used ? ends[used - 1] : 0;
        } else {
            value = js_text_to_number(s, len, &used);
        }
        if (used == 0) {
            fail(p, "Invalid number");
            return;
        }
        p->pos = s + used;
    }
    /*
     * Große Ganzzahlen (10n): Diese Maschine kennt nur eine Art Zahl. Sie
     * liest sie als gewöhnliche Zahl - über 2^53 hinaus also ungenau -, statt
     * das ganze Skript abzulehnen.
     */
    if (p->pos < p->end && *p->pos == 'n' &&
        !(p->pos + 1 < p->end && name_part((unsigned char)p->pos[1])))
        p->pos++;
    if (p->pos < p->end && name_start((unsigned char)*p->pos)) {
        fail(p, "Invalid number");
        return;
    }
    p->tk = TK_NUM;
    p->num = value;
}

static void skip_line(parser *p)
{
    while (p->pos < p->end && *p->pos != '\n')
        p->pos++;
}

static void next(parser *p)
{
    p->nl = false;
    for (;;) {
        if (p->pos >= p->end) {
            p->tk = TK_EOF;
            p->tk_start = p->end;
            return;
        }
        unsigned char c = (unsigned char)*p->pos;
        size_t left = (size_t)(p->end - p->pos);
        if (c == '\n') {
            p->line++;
            p->nl = true;
            p->pos++;
        } else if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') {
            p->pos++;
        } else if (c == 0xC2 && left >= 2 && (unsigned char)p->pos[1] == 0xA0) {
            p->pos += 2;
        } else if (c == 0xEF && left >= 3 && (unsigned char)p->pos[1] == 0xBB &&
                   (unsigned char)p->pos[2] == 0xBF) {
            p->pos += 3;
        } else if (c == 0xE2 && left >= 3 && (unsigned char)p->pos[1] == 0x80 &&
                   ((unsigned char)p->pos[2] == 0xA8 || (unsigned char)p->pos[2] == 0xA9)) {
            p->nl = true;
            p->pos += 3;
        } else if (c == '/' && left >= 2 && p->pos[1] == '/') {
            skip_line(p);
        } else if (c == '/' && left >= 2 && p->pos[1] == '*') {
            p->pos += 2;
            while (p->pos < p->end && !(p->pos[0] == '*' && p->end - p->pos >= 2 &&
                                        p->pos[1] == '/')) {
                if (*p->pos == '\n') {
                    p->line++;
                    p->nl = true;
                }
                p->pos++;
            }
            if (p->pos >= p->end) {
                fail(p, "Unterminated comment");
                return;
            }
            p->pos += 2;
        } else if (c == '<' && left >= 4 && memcmp(p->pos, "<!--", 4) == 0) {
            skip_line(p);               /* Kommentare im HTML-Stil, wie Browser sie dulden */
        } else if (c == '-' && left >= 3 && memcmp(p->pos, "-->", 3) == 0 &&
                   (p->nl || p->at_start)) {
            skip_line(p);
        } else {
            break;
        }
    }
    p->at_start = false;
    p->tk_start = p->pos;
    p->kw = KW_NONE;
    p->private_name = false;
    unsigned char c = (unsigned char)*p->pos;
    if (c == '#' && p->end - p->pos >= 2 && name_start((unsigned char)p->pos[1])) {
        /* privater Name einer Klasse: "#" gehört zum Namen, so gleicht er keinem gewöhnlichen */
        const char *s = p->pos++;
        while (p->pos < p->end && name_part((unsigned char)*p->pos))
            p->pos++;
        p->tk = TK_NAME;
        p->private_name = true;
        p->str = p->peeking ? p->J->empty : pin(js_intern(p->J, s, (size_t)(p->pos - s)));
        return;
    }
    if ((c >= '0' && c <= '9') ||
        (c == '.' && p->end - p->pos >= 2 && p->pos[1] >= '0' && p->pos[1] <= '9')) {
        lex_number(p);
        return;
    }
    if (c == '"' || c == '\'') {
        p->pos++;
        lex_string(p, (char)c);
        return;
    }
    if (name_start(c)) {
        const char *s = p->pos;
        while (p->pos < p->end && name_part((unsigned char)*p->pos))
            p->pos++;
        size_t n = (size_t)(p->pos - s);
        p->tk = TK_NAME;
        for (unsigned i = 0; i < sizeof(keywords) / sizeof(keywords[0]); i++) {
            if (strlen(keywords[i].text) == n && memcmp(keywords[i].text, s, n) == 0)
                p->kw = keywords[i].kw;
        }
        p->str = p->peeking ? p->J->empty : pin(js_intern(p->J, s, n));
        return;
    }
    for (unsigned i = 0; i < sizeof(puncts) / sizeof(puncts[0]); i++) {
        size_t n = strlen(puncts[i].text);
        if ((size_t)(p->end - p->pos) >= n && memcmp(p->pos, puncts[i].text, n) == 0) {
            /* "a?.5:b" ist eine Bedingung, keine Kette */
            if (puncts[i].id == P_OPTCHAIN && p->end - p->pos >= 3 && p->pos[2] >= '0' &&
                p->pos[2] <= '9')
                continue;
            p->pos += n;
            p->tk = TK_PUNCT;
            p->punct = puncts[i].id;
            return;
        }
    }
    fail(p, "Unexpected character");
}

static void save(const parser *p, lex_state *s)
{
    s->pos = p->pos; s->tk_start = p->tk_start; s->line = p->line; s->tk = p->tk; s->kw = p->kw;
    s->punct = p->punct; s->nl = p->nl; s->at_start = p->at_start; s->num = p->num;
    s->str = p->str; s->private_name = p->private_name;
}

static void restore(parser *p, const lex_state *s)
{
    p->pos = s->pos; p->tk_start = s->tk_start; p->line = s->line; p->tk = s->tk; p->kw = s->kw;
    p->punct = s->punct; p->nl = s->nl; p->at_start = s->at_start; p->num = s->num;
    p->str = s->str; p->private_name = s->private_name;
}

static bool is_punct(const parser *p, int id)
{
    return p->tk == TK_PUNCT && p->punct == id;
}

static bool is_kw(const parser *p, int kw)
{
    return p->tk == TK_NAME && p->kw == kw;
}

/* Ein Name, der als Bezeichner taugt (keine reservierten Wörter) */
static bool is_ident(const parser *p)
{
    return p->tk == TK_NAME && !p->private_name && (p->kw == KW_NONE || p->kw > KW_RESERVED_END);
}

static bool accept(parser *p, int id)
{
    if (!is_punct(p, id))
        return false;
    next(p);
    return true;
}

static void expect(parser *p, int id)
{
    if (!accept(p, id))
        fail(p, "Unexpected token");
}

/* Ende einer Anweisung: ";" - oder es darf fehlen (Zeilenende, "}", Dateiende) */
static void semicolon(parser *p)
{
    if (accept(p, P_SEMI) || is_punct(p, P_RBRACE) || p->tk == TK_EOF || p->nl)
        return;
    fail(p, "Unexpected token");
}

/* Schaut voraus, ohne etwas zu verändern: Folgt auf das laufende Zeichen dieses? */
static bool peek_punct(parser *p, int id)
{
    lex_state s;
    save(p, &s);
    bool was = p->peeking, failed = p->failed;
    p->peeking = true;
    next(p);
    bool found = !p->failed && is_punct(p, id) && !(id == P_ARROW && p->nl);
    p->peeking = was;
    p->failed = failed;
    restore(p, &s);
    return found;
}

/* Steht hier "(...) =>"? Dann ist die Klammer die Parameterliste einer Pfeilfunktion. */
static bool arrow_ahead(parser *p)
{
    if (is_ident(p))
        return peek_punct(p, P_ARROW);
    if (!is_punct(p, P_LPAREN))
        return false;
    lex_state s;
    save(p, &s);
    bool was = p->peeking, failed = p->failed, found = false;
    p->peeking = true;
    int level = 0;
    for (int guard = 0; guard < 2000000 && !p->failed; guard++) {
        if (p->tk == TK_EOF)
            break;
        if (p->tk == TK_PUNCT) {
            if (p->punct == P_LPAREN || p->punct == P_LBRACKET || p->punct == P_LBRACE) {
                level++;
            } else if (p->punct == P_RPAREN || p->punct == P_RBRACKET || p->punct == P_RBRACE) {
                if (--level == 0) {
                    next(p);
                    found = !p->failed && is_punct(p, P_ARROW);
                    break;
                }
            } else if (p->punct == P_BACKTICK) {
                /* Vorlage grob überspringen */
                while (p->pos < p->end && *p->pos != '`') {
                    if (*p->pos == '\\' && p->pos + 1 < p->end)
                        p->pos++;
                    p->pos++;
                }
                if (p->pos < p->end)
                    p->pos++;
            }
        }
        next(p);
    }
    p->peeking = was;
    p->failed = failed;
    restore(p, &s);
    return found;
}

/* --- Vereinbarungen merken ------------------------------------------------------------------- */

static void declare_var(parser *p, js_string *name)
{
    fnctx *f = p->fn;
    for (int i = 0; i < f->nvars; i++) {
        if (f->vars[i] == name)
            return;
    }
    if (f->nvars == f->vars_cap) {
        int cap = f->vars_cap ? f->vars_cap * 2 : 8;
        js_string **bigger = js_arena_alloc(p->J, (size_t)cap * sizeof(js_string *));
        if (!bigger) {
            p->failed = true;
            return;
        }
        if (f->nvars)
            memcpy(bigger, f->vars, (size_t)f->nvars * sizeof(js_string *));
        f->vars = bigger;
        f->vars_cap = cap;
    }
    f->vars[f->nvars++] = name;
}

static void declare_func(parser *p, js_node *fn)
{
    fnctx *f = p->fn;
    if (f->nfuncs == f->funcs_cap) {
        int cap = f->funcs_cap ? f->funcs_cap * 2 : 8;
        js_node **bigger = js_arena_alloc(p->J, (size_t)cap * sizeof(js_node *));
        if (!bigger) {
            p->failed = true;
            return;
        }
        if (f->nfuncs)
            memcpy(bigger, f->funcs, (size_t)f->nfuncs * sizeof(js_node *));
        f->funcs = bigger;
        f->funcs_cap = cap;
    }
    f->funcs[f->nfuncs++] = fn;
}

static bool enter(parser *p)
{
    if (p->failed)
        return false;
    if (++p->depth > 1500 || JS_STACK_HERE() < p->J->stack_floor) {
        fail(p, "Too much nesting");
        return false;
    }
    return true;
}

#define LEAVE(value) do { p->depth--; return (value); } while (0)

static js_node *assign(parser *p);
static js_node *expression(parser *p);
static js_node *statement(parser *p);
static js_node *function(parser *p, js_string *name, int flags, const char *start, js_node *one);
static js_node *class_node(parser *p);
static js_node *binding(parser *p, bool hoist);
static js_node *block(parser *p);

/* --- Zeichenketten-Vorlagen und reguläre Ausdrücke -------------------------------------------- */

/*
 * Das laufende Zeichen ist "`"; liest bis hinter das schließende.
 *
 * tag: Die Vorlage gehört zu einem Aufruf f`...`. Dann entsteht N_TAGGED mit
 * den Textstücken (b), ihrer rohen Fassung ohne ausgewertete Maskierungen (c)
 * und den Ausdrücken dazwischen (d) - Stücke gibt es immer eines mehr.
 */
static js_node *template_literal(parser *p, js_node *tag)
{
    js_node *n = node(p, tag ? N_TAGGED : N_TEMPLATE), **tail = &n->a;
    js_node **cooked = &n->b, **raw = &n->c, **values = &n->d;
    n->a = tag;
    for (;;) {
        p->buf_len = 0;
        bool expr = false;
        const char *start = p->pos, *stop;
        for (;;) {
            if (p->pos >= p->end)
                return fail(p, "Unterminated template");
            stop = p->pos;
            char c = *p->pos++;
            if (c == '`')
                break;
            if (c == '$' && p->pos < p->end && *p->pos == '{') {
                p->pos++;
                expr = true;
                break;
            }
            if (c == '\\') {
                escape(p);
                if (p->failed)
                    return NULL;
                continue;
            }
            if (c == '\n')
                p->line++;
            if (c == '\r')
                continue;               /* Zeilenenden gelten als "\n" */
            buf_put(p, &c, 1);
        }
        if (tag) {
            js_node *part = node(p, N_STR), *rough = node(p, N_STR);
            part->u.str = pin(js_string_new(p->J, p->buf, p->buf_len));
            /* die rohe Fassung: der Quelltext selbst, nur die Zeilenenden vereinheitlicht */
            p->buf_len = 0;
            for (const char *q = start; q < stop; q++) {
                if (*q != '\r')
                    buf_put(p, q, 1);
            }
            rough->u.str = pin(js_string_new(p->J, p->buf, p->buf_len));
            *cooked = part;
            cooked = &part->next;
            *raw = rough;
            raw = &rough->next;
        } else if (p->buf_len > 0) {
            js_node *part = node(p, N_STR);
            part->u.str = pin(js_string_new(p->J, p->buf, p->buf_len));
            *tail = part;
            tail = &part->next;
        }
        if (!expr)
            break;
        next(p);
        js_node *value = expression(p);
        if (p->failed)
            return NULL;
        if (!is_punct(p, P_RBRACE))
            return fail(p, "Unexpected token in template");
        /* nicht next(): hinter "}" geht der Text der Vorlage weiter */
        if (tag) {
            *values = value;
            values = &value->next;
        } else {
            *tail = value;
            tail = &value->next;
        }
    }
    next(p);
    return n;
}

/* Das laufende Zeichen ist "/" oder "/=" und steht dort, wo ein Ausdruck beginnt. */
static js_node *regex_literal(parser *p)
{
    const char *s = p->tk_start + 1, *q = s;
    bool in_class = false;
    for (;; q++) {
        if (q >= p->end || *q == '\n')
            return fail(p, "Unterminated regular expression");
        if (*q == '\\') {
            q++;
            continue;
        }
        if (*q == '[')
            in_class = true;
        else if (*q == ']')
            in_class = false;
        else if (*q == '/' && !in_class)
            break;
    }
    js_node *n = node(p, N_REGEX);
    n->u.str = pin(js_string_new(p->J, s, (size_t)(q - s)));
    const char *flags = ++q;
    while (q < p->end && name_part((unsigned char)*q))
        q++;
    js_node *f = node(p, N_STR);
    f->u.str = pin(js_string_new(p->J, flags, (size_t)(q - flags)));
    n->a = f;
    p->pos = q;
    next(p);
    return n;
}

/* --- Muster (Zerlegung) ------------------------------------------------------------------------ */

static js_node *name_node(parser *p, js_string *name)
{
    js_node *n = node(p, N_IDENT);
    n->u.str = name;
    return n;
}

static js_node *string_node(parser *p, js_string *text)
{
    js_node *n = node(p, N_STR);
    n->u.str = text;
    return n;
}

/* Schlüssel einer Eigenschaft: Name, Zeichenkette, Zahl oder [Ausdruck] */
static js_node *property_key(parser *p, bool *computed)
{
    *computed = false;
    if (p->tk == TK_NAME || p->tk == TK_STR) {
        js_node *key = string_node(p, pin(js_intern_string(p->J, p->str)));
        if (p->tk == TK_NAME && p->private_name)
            key->flags |= NF_PRIVATE;
        next(p);
        return key;
    }
    if (p->tk == TK_NUM) {
        char text[48];
        int n = js_number_to_text(p->num, text);
        js_node *key = string_node(p, pin(js_intern(p->J, text, (size_t)n)));
        next(p);
        return key;
    }
    if (accept(p, P_LBRACKET)) {
        bool no_in = p->no_in;
        p->no_in = false;
        js_node *key = assign(p);
        p->no_in = no_in;
        expect(p, P_RBRACKET);
        *computed = true;
        return key;
    }
    return fail(p, "Unexpected token");
}

/* Ziel einer Vereinbarung: Name oder Muster. hoist: Namen gelten in der ganzen Funktion (var). */
static js_node *binding(parser *p, bool hoist)
{
    if (!enter(p))
        return NULL;
    if (is_ident(p)) {
        js_node *n = name_node(p, p->str);
        if (hoist)
            declare_var(p, p->str);
        next(p);
        LEAVE(n);
    }
    if (accept(p, P_LBRACKET)) {
        js_node *pat = node(p, N_ARRAY_PAT), **tail = &pat->a;
        while (!is_punct(p, P_RBRACKET) && !p->failed) {
            js_node *item;
            if (is_punct(p, P_COMMA)) {
                item = node(p, N_HOLE);
            } else if (accept(p, P_ELLIPSIS)) {
                item = node(p, N_REST);
                item->a = binding(p, hoist);
            } else {
                item = binding(p, hoist);
                if (accept(p, P_ASSIGN)) {
                    js_node *d = node(p, N_DEFAULT);
                    d->a = item;
                    d->b = assign(p);
                    item = d;
                }
            }
            if (p->failed)
                break;
            *tail = item;
            tail = &item->next;
            if (!is_punct(p, P_RBRACKET))
                expect(p, P_COMMA);
        }
        expect(p, P_RBRACKET);
        LEAVE(pat);
    }
    if (accept(p, P_LBRACE)) {
        js_node *pat = node(p, N_OBJECT_PAT), **tail = &pat->a;
        while (!is_punct(p, P_RBRACE) && !p->failed) {
            js_node *item;
            if (accept(p, P_ELLIPSIS)) {
                item = node(p, N_REST);
                item->a = binding(p, hoist);
            } else {
                bool computed, plain = is_ident(p);
                js_string *name = p->str;
                item = node(p, N_PROP);
                item->a = property_key(p, &computed);
                if (computed)
                    item->flags |= NF_COMPUTED;
                if (accept(p, P_COLON)) {
                    item->b = binding(p, hoist);
                } else if (plain) {
                    item->b = name_node(p, name);
                    if (hoist)
                        declare_var(p, name);
                } else {
                    fail(p, "Unexpected token");
                }
                if (accept(p, P_ASSIGN)) {
                    js_node *d = node(p, N_DEFAULT);
                    d->a = item->b;
                    d->b = assign(p);
                    item->b = d;
                }
            }
            if (p->failed)
                break;
            *tail = item;
            tail = &item->next;
            if (!is_punct(p, P_RBRACE))
                expect(p, P_COMMA);
        }
        expect(p, P_RBRACE);
        LEAVE(pat);
    }
    fail(p, "Unexpected token");
    LEAVE(NULL);
}

/* Macht aus einem schon gelesenen Ausdruck ein Ziel für "=" - etwa aus [a, b] ein Muster. */
static js_node *to_target(parser *p, js_node *e, bool pattern_ok)
{
    if (!e || p->failed)
        return e;
    switch (e->type) {
    case N_IDENT: case N_MEMBER: case N_INDEX:
        return e;
    case N_ARRAY_PAT: case N_OBJECT_PAT:
        return e;
    case N_ARRAY:
        if (!pattern_ok)
            break;
        e->type = N_ARRAY_PAT;
        for (js_node **link = &e->a; *link; link = &(*link)->next) {
            js_node *item = *link, *rest = item->next;
            if (item->type == N_HOLE)
                continue;
            if (item->type == N_SPREAD) {
                item->type = N_REST;
                item->a = to_target(p, item->a, true);
            } else if (item->type == N_ASSIGN && item->op == OP_NONE) {
                item->type = N_DEFAULT;
                item->a = to_target(p, item->a, true);
            } else {
                item = to_target(p, item, true);
                if (item) {
                    item->next = rest;
                    *link = item;
                }
            }
            if (p->failed)
                return NULL;
        }
        return e;
    case N_OBJECT:
        if (!pattern_ok)
            break;
        e->type = N_OBJECT_PAT;
        for (js_node *item = e->a; item; item = item->next) {
            if (item->type == N_SPREAD) {
                item->type = N_REST;
                item->a = to_target(p, item->a, true);
            } else if (item->type == N_PROP && !(item->flags & (NF_GETTER | NF_SETTER | NF_METHOD))) {
                js_node *value = item->b;
                if (value && value->type == N_ASSIGN && value->op == OP_NONE) {
                    value->type = N_DEFAULT;
                    value->a = to_target(p, value->a, true);
                } else {
                    item->b = to_target(p, value, true);
                }
            } else {
                return fail(p, "Invalid destructuring target");
            }
            if (p->failed)
                return NULL;
        }
        return e;
    default:
        break;
    }
    return fail(p, "Invalid assignment target");
}

/* --- Ausdrücke --------------------------------------------------------------------------------- */

static js_node *arguments(parser *p, bool *spread)
{
    js_node *list = NULL, **tail = &list;
    bool no_in = p->no_in;
    p->no_in = false;
    while (!is_punct(p, P_RPAREN) && !p->failed) {
        js_node *arg;
        if (accept(p, P_ELLIPSIS)) {
            arg = node(p, N_SPREAD);
            arg->a = assign(p);
            *spread = true;
        } else {
            arg = assign(p);
        }
        if (p->failed)
            break;
        *tail = arg;
        tail = &arg->next;
        if (!is_punct(p, P_RPAREN))
            expect(p, P_COMMA);
    }
    expect(p, P_RPAREN);
    p->no_in = no_in;
    return list;
}

static js_node *array_literal(parser *p)
{
    js_node *n = node(p, N_ARRAY), **tail = &n->a;
    bool no_in = p->no_in;
    p->no_in = false;
    while (!is_punct(p, P_RBRACKET) && !p->failed) {
        js_node *item;
        if (is_punct(p, P_COMMA)) {
            item = node(p, N_HOLE);
        } else if (accept(p, P_ELLIPSIS)) {
            item = node(p, N_SPREAD);
            item->a = assign(p);
            n->flags |= NF_SPREAD;
        } else {
            item = assign(p);
        }
        if (p->failed)
            break;
        *tail = item;
        tail = &item->next;
        if (!is_punct(p, P_RBRACKET))
            expect(p, P_COMMA);
    }
    expect(p, P_RBRACKET);
    p->no_in = no_in;
    return n;
}

static js_node *object_literal(parser *p)
{
    js_node *n = node(p, N_OBJECT), **tail = &n->a;
    bool no_in = p->no_in;
    p->no_in = false;
    while (!is_punct(p, P_RBRACE) && !p->failed) {
        js_node *item;
        if (accept(p, P_ELLIPSIS)) {
            item = node(p, N_SPREAD);
            item->a = assign(p);
        } else {
            item = node(p, N_PROP);
            const char *start = p->tk_start;
            bool computed, plain = is_ident(p);
            js_string *name = p->str;
            /* "get x() {...}" und "set x(v) {...}" - aber nicht { get: 1 } oder { get() {} } */
            int kind = 0;               /* JS_F_ASYNC, JS_F_GENERATOR: "async name() {}", "*name() {}" */
            if ((is_kw(p, KW_GET) || is_kw(p, KW_SET)) && !peek_punct(p, P_COLON) &&
                !peek_punct(p, P_LPAREN) && !peek_punct(p, P_COMMA) && !peek_punct(p, P_RBRACE) &&
                !peek_punct(p, P_ASSIGN)) {
                item->flags |= p->kw == KW_GET ? NF_GETTER : NF_SETTER;
                next(p);
            } else {
                if (is_kw(p, KW_ASYNC) && !peek_punct(p, P_COLON) && !peek_punct(p, P_LPAREN) &&
                    !peek_punct(p, P_COMMA) && !peek_punct(p, P_RBRACE) && !peek_punct(p, P_ASSIGN)) {
                    kind |= JS_F_ASYNC;
                    next(p);
                }
                if (accept(p, P_STAR))
                    kind |= JS_F_GENERATOR;
                plain = plain && !kind;
            }
            item->a = property_key(p, &computed);
            if (computed)
                item->flags |= NF_COMPUTED;
            if (p->failed)
                break;
            if (item->flags & (NF_GETTER | NF_SETTER)) {
                item->b = function(p, computed ? NULL : item->a->u.str, 0, start, NULL);
            } else if (is_punct(p, P_LPAREN)) {
                item->flags |= NF_METHOD;
                item->b = function(p, computed ? NULL : item->a->u.str, kind, start, NULL);
            } else if (kind) {
                fail(p, "Unexpected token");
            } else if (accept(p, P_COLON)) {
                item->b = assign(p);
            } else if (plain && !computed) {
                item->flags |= NF_SHORTHAND;
                item->b = name_node(p, name);
                if (name == p->J->k[K_arguments]) {
                    for (fnctx *f = p->fn; f; f = f->outer) {
                        if (!f->arrow) {
                            f->uses_arguments = true;
                            break;
                        }
                    }
                }
                if (accept(p, P_ASSIGN)) {      /* nur als Muster sinnvoll: {a = 1} = obj */
                    js_node *d = node(p, N_ASSIGN);
                    d->a = item->b;
                    d->b = assign(p);
                    item->b = d;
                }
            } else {
                fail(p, "Unexpected token");
            }
        }
        if (p->failed)
            break;
        *tail = item;
        tail = &item->next;
        if (!is_punct(p, P_RBRACE))
            expect(p, P_COMMA);
    }
    expect(p, P_RBRACE);
    p->no_in = no_in;
    return n;
}

static js_node *primary(parser *p)
{
    js_node *n;
    switch (p->tk) {
    case TK_NUM:
        n = node(p, N_NUM);
        n->u.num = p->num;
        next(p);
        return n;
    case TK_STR:
        n = string_node(p, p->str);
        next(p);
        return n;
    case TK_NAME:
        if (p->private_name) {
            /* nur als "#name in objekt" */
            n = string_node(p, p->str);
            n->flags |= NF_PRIVATE;
            next(p);
            if (!is_kw(p, KW_IN))
                return fail(p, "Unexpected private name");
            return n;
        }
        switch (p->kw) {
        case KW_THIS: next(p); return node(p, N_THIS);
        case KW_NULL: next(p); return node(p, N_NULL);
        case KW_TRUE: next(p); return node(p, N_TRUE);
        case KW_FALSE: next(p); return node(p, N_FALSE);
        case KW_SUPER: next(p); return node(p, N_SUPER);
        case KW_FUNCTION: {
            const char *start = p->tk_start;
            next(p);
            int kind = accept(p, P_STAR) ? JS_F_GENERATOR : 0;
            js_string *name = NULL;
            if (is_ident(p)) {
                name = p->str;
                next(p);
            }
            return function(p, name, kind, start, NULL);
        }
        case KW_CLASS:
            return class_node(p);
        case KW_ASYNC: {
            /* "async function ..." - alles andere mit "async" ist ein gewöhnlicher Name */
            lex_state s;
            const char *start = p->tk_start;
            save(p, &s);
            next(p);
            if (is_kw(p, KW_FUNCTION) && !p->nl) {
                next(p);
                int kind = JS_F_ASYNC | (accept(p, P_STAR) ? JS_F_GENERATOR : 0);
                js_string *name = NULL;
                if (is_ident(p)) {
                    name = p->str;
                    next(p);
                }
                return function(p, name, kind, start, NULL);
            }
            restore(p, &s);
            break;
        }
        default:
            if (p->kw != KW_NONE && p->kw < KW_RESERVED_END)
                return fail(p, "Unexpected keyword");
        }
        n = name_node(p, p->str);
        if (p->str == p->J->k[K_arguments]) {
            for (fnctx *f = p->fn; f; f = f->outer) {
                if (!f->arrow) {
                    f->uses_arguments = true;
                    break;
                }
            }
        }
        next(p);
        return n;
    case TK_PUNCT:
        switch (p->punct) {
        case P_LPAREN: {
            next(p);
            bool no_in = p->no_in;
            p->no_in = false;
            n = expression(p);
            p->no_in = no_in;
            expect(p, P_RPAREN);
            return n;
        }
        case P_LBRACKET: next(p); return array_literal(p);
        case P_LBRACE: next(p); return object_literal(p);
        case P_SLASH: case P_DIV_A: return regex_literal(p);
        case P_BACKTICK: return template_literal(p, NULL);
        default: break;
        }
        break;
    default:
        break;
    }
    return fail(p, p->tk == TK_EOF ? "Unexpected end of input" : "Unexpected token");
}

/* Zugriffe und Aufrufe hinter einem Ausdruck: a.b, a[b], a(b), a?.b */
static js_node *call_member(parser *p)
{
    js_node *e;
    bool chain = false;
    if (is_kw(p, KW_NEW)) {
        next(p);
        if (accept(p, P_DOT)) {
            if (p->tk != TK_NAME || p->str->len != 6 || memcmp(p->str->data, "target", 6) != 0)
                return fail(p, "Unexpected token");
            next(p);
            /* gemeint ist die nächste gewöhnliche Funktion (Pfeilfunktionen reichen durch) */
            for (fnctx *f = p->fn; f; f = f->outer) {
                if (!f->arrow) {
                    f->uses_new_target = true;
                    break;
                }
            }
            e = node(p, N_NEWTARGET);
            goto suffix;                /* dahinter darf es weitergehen: new.target.name */
        }
        js_node *callee;
        if (is_kw(p, KW_NEW)) {
            callee = call_member(p);
        } else {
            callee = primary(p);
            /* nur Zugriffe gehören zum Namen der Klasse, kein Aufruf */
            while (!p->failed) {
                if (accept(p, P_DOT)) {
                    if (p->tk != TK_NAME)
                        return fail(p, "Unexpected token");
                    js_node *m = node(p, N_MEMBER);
                    m->a = callee;
                    m->u.str = p->str;
                    if (p->private_name)
                        m->flags |= NF_PRIVATE;
                    callee = m;
                    next(p);
                } else if (accept(p, P_LBRACKET)) {
                    js_node *m = node(p, N_INDEX);
                    m->a = callee;
                    m->b = expression(p);
                    expect(p, P_RBRACKET);
                    callee = m;
                } else {
                    break;
                }
            }
        }
        e = node(p, N_NEW);
        e->a = callee;
        if (accept(p, P_LPAREN)) {
            bool spread = false;
            e->b = arguments(p, &spread);
            if (spread)
                e->flags |= NF_SPREAD;
        }
    } else {
        e = primary(p);
    }
suffix:
    while (!p->failed) {
        int optional = 0;
        if (accept(p, P_OPTCHAIN)) {
            optional = NF_OPTIONAL;
            chain = true;
            if (p->tk == TK_NAME) {
                js_node *m = node(p, N_MEMBER);
                m->a = e;
                m->u.str = p->str;
                m->flags |= NF_OPTIONAL | (p->private_name ? NF_PRIVATE : 0);
                e = m;
                next(p);
                continue;
            }
            if (!is_punct(p, P_LBRACKET) && !is_punct(p, P_LPAREN))
                return fail(p, "Unexpected token");
        }
        if (accept(p, P_DOT)) {
            if (p->tk != TK_NAME)
                return fail(p, "Unexpected token");
            js_node *m = node(p, N_MEMBER);
            m->a = e;
            m->u.str = p->str;
            if (p->private_name)
                m->flags |= NF_PRIVATE;
            e = m;
            next(p);
        } else if (accept(p, P_LBRACKET)) {
            js_node *m = node(p, N_INDEX);
            bool no_in = p->no_in;
            p->no_in = false;
            m->a = e;
            m->b = expression(p);
            p->no_in = no_in;
            m->flags |= (uint16_t)optional;
            expect(p, P_RBRACKET);
            e = m;
        } else if (accept(p, P_LPAREN)) {
            js_node *c = node(p, N_CALL);
            bool spread = false;
            c->a = e;
            c->b = arguments(p, &spread);
            c->flags |= (uint16_t)(optional | (spread ? NF_SPREAD : 0));
            e = c;
        } else if (is_punct(p, P_BACKTICK)) {
            e = template_literal(p, e);
        } else {
            break;
        }
    }
    if (chain && !p->failed) {
        js_node *c = node(p, N_CHAIN);
        c->a = e;
        e = c;
    }
    return e;
}

static js_node *unary(parser *p)
{
    if (!enter(p))
        return NULL;
    int op = OP_NONE;
    if (p->tk == TK_PUNCT) {
        switch (p->punct) {
        case P_NOT: op = OP_NOT; break;
        case P_TILDE: op = OP_BITNOT; break;
        case P_PLUS: op = OP_POS; break;
        case P_MINUS: op = OP_NEG; break;
        case P_INC: op = OP_INC; break;
        case P_DEC: op = OP_DEC; break;
        default: break;
        }
    } else if (p->tk == TK_NAME) {
        if (p->kw == KW_TYPEOF) op = OP_TYPEOF;
        else if (p->kw == KW_VOID) op = OP_VOID;
        else if (p->kw == KW_DELETE) op = OP_DELETE;
        else if (p->kw == KW_AWAIT && p->fn->is_async) {
            js_node *n = node(p, N_AWAIT);
            next(p);
            n->a = unary(p);
            LEAVE(n);
        }
    }
    if (op != OP_NONE) {
        js_node *n = node(p, op == OP_INC || op == OP_DEC ? N_UPDATE : N_UNARY);
        next(p);
        n->op = (uint8_t)op;
        n->a = unary(p);
        if (n->type == N_UPDATE) {
            n->flags |= NF_PREFIX;
            n->a = to_target(p, n->a, false);
        }
        LEAVE(n);
    }
    js_node *e = call_member(p);
    if (!p->failed && !p->nl && (is_punct(p, P_INC) || is_punct(p, P_DEC))) {
        js_node *n = node(p, N_UPDATE);
        n->op = p->punct == P_INC ? OP_INC : OP_DEC;
        n->a = to_target(p, e, false);
        next(p);
        e = n;
    }
    LEAVE(e);
}

/* Rang und Operator des laufenden Zeichens; 0, wenn es kein zweistelliger Operator ist */
static int binary_op(const parser *p, int *op)
{
    if (p->tk == TK_NAME) {
        if (p->kw == KW_INSTANCEOF) { *op = OP_INSTANCEOF; return 8; }
        if (p->kw == KW_IN && !p->no_in) { *op = OP_IN; return 8; }
        return 0;
    }
    if (p->tk != TK_PUNCT)
        return 0;
    switch (p->punct) {
    case P_NULLISH: *op = OP_NULLISH; return 1;
    case P_LOR: *op = OP_LOR; return 2;
    case P_LAND: *op = OP_LAND; return 3;
    case P_OR: *op = OP_OR; return 4;
    case P_XOR: *op = OP_XOR; return 5;
    case P_AND: *op = OP_AND; return 6;
    case P_EQ: *op = OP_EQ; return 7;
    case P_NE: *op = OP_NE; return 7;
    case P_SEQ: *op = OP_SEQ; return 7;
    case P_SNE: *op = OP_SNE; return 7;
    case P_LT: *op = OP_LT; return 8;
    case P_GT: *op = OP_GT; return 8;
    case P_LE: *op = OP_LE; return 8;
    case P_GE: *op = OP_GE; return 8;
    case P_SHL: *op = OP_SHL; return 9;
    case P_SHR: *op = OP_SHR; return 9;
    case P_USHR: *op = OP_USHR; return 9;
    case P_PLUS: *op = OP_ADD; return 10;
    case P_MINUS: *op = OP_SUB; return 10;
    case P_STAR: *op = OP_MUL; return 11;
    case P_SLASH: *op = OP_DIV; return 11;
    case P_PERCENT: *op = OP_MOD; return 11;
    case P_POW: *op = OP_POW; return 12;
    default: return 0;
    }
}

static js_node *binary(parser *p, int min_rank)
{
    js_node *left = unary(p);
    for (;;) {
        int op = OP_NONE, rank = p->failed ? 0 : binary_op(p, &op);
        if (rank == 0 || rank < min_rank)
            return left;
        if (!enter(p))
            return NULL;
        js_node *n = node(p, op == OP_LAND || op == OP_LOR || op == OP_NULLISH ? N_LOGICAL : N_BINARY);
        next(p);
        n->op = (uint8_t)op;
        n->a = left;
        n->b = binary(p, op == OP_POW ? rank : rank + 1);
        p->depth--;
        left = n;
    }
}

/* Pfeilfunktion; kind: JS_F_ASYNC, wenn "async" davorstand (start zeigt dann dorthin) */
static js_node *arrow(parser *p, int kind, const char *start)
{
    if (is_ident(p)) {
        js_node *param = name_node(p, p->str);
        next(p);
        return function(p, NULL, JS_F_ARROW | kind, start, param);
    }
    return function(p, NULL, JS_F_ARROW | kind, start, NULL);
}

/* Das laufende Zeichen ist "yield" in einem Generator. */
static js_node *yield_expression(parser *p)
{
    js_node *n = node(p, N_YIELD);
    next(p);
    if (!p->nl && accept(p, P_STAR)) {
        n->flags |= NF_DELEGATE;
        n->a = assign(p);
        return n;
    }
    /* "yield" allein liefert undefined; ein Wert folgt nur in derselben Zeile */
    bool ends = p->nl || p->tk == TK_EOF || is_punct(p, P_RPAREN) || is_punct(p, P_RBRACKET) ||
                is_punct(p, P_RBRACE) || is_punct(p, P_COMMA) || is_punct(p, P_SEMI) ||
                is_punct(p, P_COLON) || is_kw(p, KW_IN) || is_kw(p, KW_OF);
    if (!ends)
        n->a = assign(p);
    return n;
}

static int assign_op(const parser *p)
{
    if (p->tk != TK_PUNCT)
        return -1;
    switch (p->punct) {
    case P_ASSIGN: return OP_NONE;
    case P_ADD_A: return OP_ADD;
    case P_SUB_A: return OP_SUB;
    case P_MUL_A: return OP_MUL;
    case P_DIV_A: return OP_DIV;
    case P_MOD_A: return OP_MOD;
    case P_POW_A: return OP_POW;
    case P_SHL_A: return OP_SHL;
    case P_SHR_A: return OP_SHR;
    case P_USHR_A: return OP_USHR;
    case P_AND_A: return OP_AND;
    case P_OR_A: return OP_OR;
    case P_XOR_A: return OP_XOR;
    case P_LAND_A: return OP_LAND;
    case P_LOR_A: return OP_LOR;
    case P_NULLISH_A: return OP_NULLISH;
    default: return -1;
    }
}

static js_node *assign(parser *p)
{
    if (!enter(p))
        return NULL;
    if ((is_ident(p) || is_punct(p, P_LPAREN)) && arrow_ahead(p))
        LEAVE(arrow(p, 0, p->tk_start));
    if (is_kw(p, KW_ASYNC)) {
        /* "async x => ..." und "async (a, b) => ..." */
        lex_state s;
        const char *start = p->tk_start;
        save(p, &s);
        next(p);
        if (!p->failed && !p->nl && (is_ident(p) || is_punct(p, P_LPAREN)) && arrow_ahead(p))
            LEAVE(arrow(p, JS_F_ASYNC, start));
        restore(p, &s);
    }
    if (is_kw(p, KW_YIELD) && p->fn->generator)
        LEAVE(yield_expression(p));
    js_node *left = binary(p, 1);
    if (!p->failed && accept(p, P_QUESTION)) {
        js_node *n = node(p, N_COND);
        bool no_in = p->no_in;
        p->no_in = false;
        n->a = left;
        n->b = assign(p);
        p->no_in = no_in;
        expect(p, P_COLON);
        n->c = assign(p);
        LEAVE(n);
    }
    int op = p->failed ? -1 : assign_op(p);
    if (op >= 0) {
        js_node *n = node(p, N_ASSIGN);
        n->op = (uint8_t)op;
        n->a = to_target(p, left, op == OP_NONE);
        next(p);
        n->b = assign(p);
        LEAVE(n);
    }
    LEAVE(left);
}

static js_node *expression(parser *p)
{
    js_node *first = assign(p);
    if (p->failed || !is_punct(p, P_COMMA))
        return first;
    js_node *seq = node(p, N_SEQ), **tail = &seq->a;
    *tail = first;
    tail = &first->next;
    while (accept(p, P_COMMA) && !p->failed) {
        js_node *e = assign(p);
        if (p->failed)
            break;
        *tail = e;
        tail = &e->next;
    }
    return seq;
}

/* --- Funktionen und Klassen -------------------------------------------------------------------- */

static js_node *statement_list(parser *p, bool *lexical)
{
    js_node *list = NULL, **tail = &list;
    while (!is_punct(p, P_RBRACE) && p->tk != TK_EOF && !p->failed) {
        js_node *s = statement(p);
        if (p->failed || !s)
            break;
        if ((s->type == S_VAR && (s->flags & (NF_LET | NF_CONST))) || s->type == S_CLASS)
            *lexical = true;
        *tail = s;
        tail = &s->next;
    }
    return list;
}

/*
 * Liest Parameter und Rumpf. Das laufende Zeichen ist "(" - außer bei einer
 * Pfeilfunktion mit einem einzelnen Namen (one), dort "=>".
 */
static js_node *function(parser *p, js_string *name, int flags, const char *start, js_node *one)
{
    if (!enter(p))
        return NULL;
    js_fninfo *info = js_arena_alloc(p->J, sizeof(js_fninfo));
    js_node *n = node(p, N_FUNC);
    fnctx ctx;
    memset(&ctx, 0, sizeof(ctx));
    if (!info) {
        p->failed = true;
        LEAVE(NULL);
    }
    ctx.outer = p->fn;
    ctx.arrow = (flags & JS_F_ARROW) != 0;
    ctx.generator = (flags & JS_F_GENERATOR) != 0;
    ctx.is_async = (flags & JS_F_ASYNC) != 0;
    p->fn = &ctx;
    bool no_in = p->no_in;
    p->no_in = false;

    info->name = name;
    info->flags = (uint8_t)flags;
    info->simple_params = true;
    js_node **tail = &info->params;
    if (flags & PF_NO_PARAMS) {
        /* Rumpf ohne Parameterliste */
    } else if (one) {
        *tail = one;
        info->nparams = 1;
    } else {
        expect(p, P_LPAREN);
        bool counting = true;
        while (!is_punct(p, P_RPAREN) && !p->failed) {
            js_node *param;
            if (accept(p, P_ELLIPSIS)) {
                param = node(p, N_REST);
                param->a = binding(p, false);
                counting = false;
            } else {
                param = binding(p, false);
                if (accept(p, P_ASSIGN)) {
                    js_node *d = node(p, N_DEFAULT);
                    d->a = param;
                    d->b = assign(p);
                    param = d;
                    counting = false;
                }
            }
            if (p->failed)
                break;
            if (param->type != N_IDENT)
                info->simple_params = false;
            if (counting)
                info->nparams++;
            *tail = param;
            tail = &param->next;
            if (!is_punct(p, P_RPAREN))
                expect(p, P_COMMA);
        }
        expect(p, P_RPAREN);
    }
    if (flags & JS_F_ARROW) {
        if (!is_punct(p, P_ARROW) || p->nl)
            fail(p, "Unexpected token");
        next(p);
    }
    if ((flags & JS_F_ARROW) && !is_punct(p, P_LBRACE)) {
        info->expr_body = true;
        info->body = assign(p);
    } else {
        bool lexical = false;
        expect(p, P_LBRACE);
        info->body = statement_list(p, &lexical);
        if (!is_punct(p, P_RBRACE))
            fail(p, "Unexpected token");
        info->source_len = (uint32_t)(p->pos - start);
        next(p);
    }
    if (info->expr_body)
        info->source_len = (uint32_t)(p->tk_start - start);
    info->source = start;
    info->vars = ctx.vars;
    info->nvars = ctx.nvars;
    info->funcs = ctx.funcs;
    info->nfuncs = ctx.nfuncs;
    info->uses_arguments = ctx.uses_arguments;
    info->uses_new_target = ctx.uses_new_target;
    p->fn = ctx.outer;
    p->no_in = no_in;
    n->u.fn = info;
    LEAVE(n);
}

/* Das laufende Zeichen ist "class". */
static js_node *class_node(parser *p)
{
    js_node *n = node(p, N_CLASS), **members = &n->b, *fields = NULL, **field_tail = &fields;
    const char *class_start = p->tk_start;
    next(p);
    if (is_ident(p) && p->kw != KW_EXTENDS) {
        n->u.str = p->str;
        next(p);
    }
    if (is_kw(p, KW_EXTENDS)) {
        next(p);
        n->a = call_member(p);
    }
    expect(p, P_LBRACE);
    while (!is_punct(p, P_RBRACE) && !p->failed) {
        if (accept(p, P_SEMI))
            continue;
        js_node *item = node(p, N_PROP);
        const char *start = p->tk_start;
        if (is_kw(p, KW_STATIC) && !peek_punct(p, P_LPAREN) && !peek_punct(p, P_ASSIGN) &&
            !peek_punct(p, P_SEMI)) {
            item->flags |= NF_STATIC;
            next(p);
            if (is_punct(p, P_LBRACE)) {
                /* static { ... }: läuft einmal, wenn die Klasse entsteht - mit der Klasse als "this" */
                item->flags |= NF_SHORTHAND;
                item->b = function(p, NULL, PF_NO_PARAMS, start, NULL);
                if (p->failed)
                    break;
                *members = item;
                members = &item->next;
                continue;
            }
        }
        int kind = 0;                   /* JS_F_ASYNC, JS_F_GENERATOR */
        if ((is_kw(p, KW_GET) || is_kw(p, KW_SET)) && !peek_punct(p, P_LPAREN) &&
            !peek_punct(p, P_ASSIGN) && !peek_punct(p, P_SEMI) && !peek_punct(p, P_RBRACE)) {
            item->flags |= p->kw == KW_GET ? NF_GETTER : NF_SETTER;
            next(p);
        } else {
            if (is_kw(p, KW_ASYNC) && !peek_punct(p, P_LPAREN) && !peek_punct(p, P_ASSIGN) &&
                !peek_punct(p, P_SEMI) && !peek_punct(p, P_RBRACE)) {
                kind |= JS_F_ASYNC;
                next(p);
            }
            if (accept(p, P_STAR))
                kind |= JS_F_GENERATOR;
        }
        bool computed;
        item->a = property_key(p, &computed);
        if (p->failed)
            break;
        if (computed)
            item->flags |= NF_COMPUTED;
        else if (item->a->flags & NF_PRIVATE)
            item->flags |= NF_PRIVATE;
        js_string *key = computed ? NULL : item->a->u.str;
        if (is_punct(p, P_LPAREN)) {
            bool ctor = key == p->J->k[K_constructor] && !kind &&
                        !(item->flags & (NF_STATIC | NF_GETTER | NF_SETTER));
            if (!(item->flags & (NF_GETTER | NF_SETTER)))
                item->flags |= NF_METHOD;
            js_node *fn = function(p, ctor ? n->u.str : key,
                                   ctor ? JS_F_CLASS | (n->a ? JS_F_DERIVED : 0) : kind,
                                   ctor ? class_start : start, NULL);
            if (ctor) {
                n->c = fn;
                continue;
            }
            item->b = fn;
        } else {
            /* ein Feld: name = wert; */
            if ((item->flags & (NF_GETTER | NF_SETTER)) || kind)
                return fail(p, "Unexpected token");
            item->flags |= NF_FIELD;
            if (accept(p, P_ASSIGN))
                item->b = assign(p);
            semicolon(p);
            if (!(item->flags & NF_STATIC)) {
                *field_tail = item;
                field_tail = &item->next;
                continue;
            }
        }
        if (p->failed)
            break;
        *members = item;
        members = &item->next;
    }
    if (!is_punct(p, P_RBRACE))
        return fail(p, "Unexpected token");
    uint32_t source_len = (uint32_t)(p->pos - class_start);
    next(p);
    if (p->failed)
        return NULL;
    if (!n->c) {
        /* kein eigener Konstruktor: ein leerer - oder einer, der alles weiterreicht */
        js_fninfo *info = js_arena_alloc(p->J, sizeof(js_fninfo));
        js_node *fn = node(p, N_FUNC);
        if (!info) {
            p->failed = true;
            return NULL;
        }
        info->name = n->u.str;
        info->flags = JS_F_CLASS | (n->a ? JS_F_DERIVED : 0);
        info->simple_params = !n->a;
        info->source = class_start;
        if (n->a) {
            js_string *args = pin(js_intern_cstr(p->J, "args"));
            js_node *rest = node(p, N_REST), *call = node(p, N_CALL), *spread = node(p, N_SPREAD);
            js_node *stmt = node(p, S_EXPR);
            rest->a = name_node(p, args);
            spread->a = name_node(p, args);
            call->a = node(p, N_SUPER);
            call->b = spread;
            call->flags |= NF_SPREAD;
            stmt->a = call;
            info->params = rest;
            info->body = stmt;
        }
        fn->u.fn = info;
        n->c = fn;
    }
    if (n->c && n->c->u.fn) {
        n->c->u.fn->fields = fields;
        n->c->u.fn->source_len = source_len;
    }
    return n;
}

/* --- Anweisungen -------------------------------------------------------------------------------- */

static js_node *var_statement(parser *p, bool in_for)
{
    js_node *n = node(p, S_VAR), **tail = &n->a;
    bool hoist = p->kw == KW_VAR;
    n->flags |= p->kw == KW_LET ? NF_LET : p->kw == KW_CONST ? NF_CONST : 0;
    next(p);
    do {
        js_node *d = node(p, S_DECL);
        d->a = binding(p, hoist);
        if (accept(p, P_ASSIGN))
            d->b = assign(p);
        if (p->failed)
            break;
        *tail = d;
        tail = &d->next;
    } while (accept(p, P_COMMA));
    if (!in_for)
        semicolon(p);
    return n;
}

static js_node *block(parser *p)
{
    js_node *n = node(p, S_BLOCK);
    bool lexical = false;
    expect(p, P_LBRACE);
    n->a = statement_list(p, &lexical);
    expect(p, P_RBRACE);
    if (lexical)
        n->flags |= NF_SCOPE;
    return n;
}

static js_node *for_statement(parser *p)
{
    js_node *n = node(p, S_FOR), *init = NULL;
    next(p);
    bool awaiting = false;
    if (is_kw(p, KW_AWAIT)) {
        if (!p->fn->is_async)
            return fail(p, "for await is only valid in async functions");
        awaiting = true;
        next(p);
    }
    expect(p, P_LPAREN);
    bool declaration = is_kw(p, KW_VAR) || is_kw(p, KW_CONST) ||
                       (is_kw(p, KW_LET) && !peek_punct(p, P_SEMI) && !peek_punct(p, P_ASSIGN) &&
                        !peek_punct(p, P_DOT));
    if (declaration) {
        p->no_in = true;
        init = var_statement(p, true);
        p->no_in = false;
        if (init && (init->flags & (NF_LET | NF_CONST)))
            n->flags |= NF_SCOPE;
    } else if (!is_punct(p, P_SEMI)) {
        p->no_in = true;
        js_node *e = expression(p);
        p->no_in = false;
        if (p->failed)
            return NULL;
        if (is_kw(p, KW_IN) || is_kw(p, KW_OF)) {
            init = to_target(p, e, true);
        } else {
            init = node(p, S_EXPR);
            init->a = e;
        }
    }
    if (p->failed)
        return NULL;
    if (is_kw(p, KW_IN) || is_kw(p, KW_OF)) {
        bool of = p->kw == KW_OF;
        if (!init || (init->type == S_VAR && (!init->a || init->a->next || init->a->b)))
            return fail(p, "Invalid left-hand side in for loop");
        n->type = of ? S_FOROF : S_FORIN;
        if (awaiting) {
            if (!of)
                return fail(p, "Unexpected token");
            n->flags |= NF_AWAIT;
        }
        next(p);
        n->a = init;
        n->b = of ? assign(p) : expression(p);
        expect(p, P_RPAREN);
        n->d = statement(p);
        return n;
    }
    if (awaiting)
        return fail(p, "Unexpected token");
    n->a = init;
    expect(p, P_SEMI);
    if (!is_punct(p, P_SEMI))
        n->b = expression(p);
    expect(p, P_SEMI);
    if (!is_punct(p, P_RPAREN))
        n->c = expression(p);
    expect(p, P_RPAREN);
    n->d = statement(p);
    return n;
}

static js_node *statement(parser *p)
{
    if (!enter(p))
        return NULL;
    js_node *n = NULL;
    if (p->tk == TK_PUNCT && p->punct == P_LBRACE) {
        LEAVE(block(p));
    } else if (p->tk == TK_PUNCT && p->punct == P_SEMI) {
        next(p);
        LEAVE(node(p, S_EMPTY));
    } else if (p->tk == TK_NAME) {
        switch (p->kw) {
        case KW_VAR: case KW_CONST:
            LEAVE(var_statement(p, false));
        case KW_LET:
            /* "let" darf auch ein gewöhnlicher Name sein */
            if (!peek_punct(p, P_ASSIGN) && !peek_punct(p, P_SEMI) && !peek_punct(p, P_DOT) &&
                !peek_punct(p, P_LPAREN))
                LEAVE(var_statement(p, false));
            break;
        case KW_FUNCTION: {
            const char *start = p->tk_start;
            next(p);
            int kind = accept(p, P_STAR) ? JS_F_GENERATOR : 0;
            if (!is_ident(p))
                LEAVE(fail(p, "Function statements require a name"));
            js_string *name = p->str;
            next(p);
            js_node *fn = function(p, name, kind, start, NULL);
            if (!p->failed && fn) {
                declare_func(p, fn);
                declare_var(p, name);
            }
            LEAVE(node(p, S_FUNC));
        }
        case KW_ASYNC: {
            lex_state s;
            const char *start = p->tk_start;
            save(p, &s);
            next(p);
            if (is_kw(p, KW_FUNCTION) && !p->nl) {
                next(p);
                int kind = JS_F_ASYNC | (accept(p, P_STAR) ? JS_F_GENERATOR : 0);
                if (!is_ident(p))
                    LEAVE(fail(p, "Function statements require a name"));
                js_string *name = p->str;
                next(p);
                js_node *fn = function(p, name, kind, start, NULL);
                if (!p->failed && fn) {
                    declare_func(p, fn);
                    declare_var(p, name);
                }
                LEAVE(node(p, S_FUNC));
            }
            restore(p, &s);
            break;
        }
        case KW_CLASS:
            n = node(p, S_CLASS);
            n->a = class_node(p);
            if (!p->failed && n->a && !n->a->u.str)
                fail(p, "Class statements require a name");
            LEAVE(n);
        case KW_IF:
            n = node(p, S_IF);
            next(p);
            expect(p, P_LPAREN);
            n->a = expression(p);
            expect(p, P_RPAREN);
            n->b = statement(p);
            if (is_kw(p, KW_ELSE)) {
                next(p);
                n->c = statement(p);
            }
            LEAVE(n);
        case KW_FOR:
            LEAVE(for_statement(p));
        case KW_WHILE:
            n = node(p, S_WHILE);
            next(p);
            expect(p, P_LPAREN);
            n->a = expression(p);
            expect(p, P_RPAREN);
            n->d = statement(p);
            LEAVE(n);
        case KW_DO:
            n = node(p, S_DO);
            next(p);
            n->d = statement(p);
            if (!is_kw(p, KW_WHILE))
                LEAVE(fail(p, "Unexpected token"));
            next(p);
            expect(p, P_LPAREN);
            n->a = expression(p);
            expect(p, P_RPAREN);
            accept(p, P_SEMI);
            LEAVE(n);
        case KW_RETURN:
            n = node(p, S_RETURN);
            next(p);
            if (!p->nl && !is_punct(p, P_SEMI) && !is_punct(p, P_RBRACE) && p->tk != TK_EOF)
                n->a = expression(p);
            semicolon(p);
            LEAVE(n);
        case KW_BREAK: case KW_CONTINUE:
            n = node(p, p->kw == KW_BREAK ? S_BREAK : S_CONTINUE);
            next(p);
            if (!p->nl && is_ident(p)) {
                n->u.str = p->str;
                next(p);
            }
            semicolon(p);
            LEAVE(n);
        case KW_THROW:
            n = node(p, S_THROW);
            next(p);
            if (p->nl)
                LEAVE(fail(p, "Illegal newline after throw"));
            n->a = expression(p);
            semicolon(p);
            LEAVE(n);
        case KW_TRY:
            n = node(p, S_TRY);
            next(p);
            n->a = block(p);
            if (is_kw(p, KW_CATCH)) {
                next(p);
                if (accept(p, P_LPAREN)) {
                    n->b = binding(p, false);
                    expect(p, P_RPAREN);
                }
                n->c = block(p);
            }
            if (is_kw(p, KW_FINALLY)) {
                next(p);
                n->d = block(p);
            }
            if (!p->failed && !n->c && !n->d)
                LEAVE(fail(p, "Missing catch or finally after try"));
            LEAVE(n);
        case KW_SWITCH: {
            n = node(p, S_SWITCH);
            next(p);
            expect(p, P_LPAREN);
            n->a = expression(p);
            expect(p, P_RPAREN);
            expect(p, P_LBRACE);
            js_node **tail = &n->b;
            while (!is_punct(p, P_RBRACE) && !p->failed) {
                js_node *c = node(p, S_CASE), **body = &c->b;
                if (is_kw(p, KW_CASE)) {
                    next(p);
                    c->a = expression(p);
                } else if (is_kw(p, KW_DEFAULT)) {
                    next(p);
                } else {
                    LEAVE(fail(p, "Unexpected token"));
                }
                expect(p, P_COLON);
                while (!is_punct(p, P_RBRACE) && !is_kw(p, KW_CASE) && !is_kw(p, KW_DEFAULT) &&
                       p->tk != TK_EOF && !p->failed) {
                    js_node *s = statement(p);
                    if (p->failed || !s)
                        break;
                    if ((s->type == S_VAR && (s->flags & (NF_LET | NF_CONST))) || s->type == S_CLASS)
                        n->flags |= NF_SCOPE;
                    *body = s;
                    body = &s->next;
                }
                *tail = c;
                tail = &c->next;
            }
            expect(p, P_RBRACE);
            LEAVE(n);
        }
        case KW_DEBUGGER:
            next(p);
            semicolon(p);
            LEAVE(node(p, S_EMPTY));
        case KW_WITH:
            LEAVE(fail(p, "The with statement is not supported"));
        case KW_IMPORT:
            if (!peek_punct(p, P_LPAREN) && !peek_punct(p, P_DOT))
                LEAVE(fail(p, "Modules are not supported"));
            LEAVE(fail(p, "import() is not supported"));
        case KW_EXPORT:
            LEAVE(fail(p, "Modules are not supported"));
        default:
            break;
        }
        /* Marke: name: anweisung */
        if (is_ident(p) && peek_punct(p, P_COLON)) {
            n = node(p, S_LABEL);
            n->u.str = p->str;
            next(p);
            next(p);
            n->a = statement(p);
            LEAVE(n);
        }
    }
    n = node(p, S_EXPR);
    n->a = expression(p);
    semicolon(p);
    LEAVE(n);
}

js_fninfo *js_parse(js_state *J, const char *source, size_t len, const char *name)
{
    (void)name;
    parser *p = kzalloc(sizeof(parser));
    char *copy = js_arena_alloc(J, len + 1);
    js_fninfo *info = js_arena_alloc(J, sizeof(js_fninfo));
    if (!p || !copy || !info) {
        kfree(p);
        js_fatal(J, "Nicht genug Speicher für das Skript");
        return NULL;
    }
    memcpy(copy, source, len);
    fnctx ctx;
    memset(&ctx, 0, sizeof(ctx));
    p->J = J;
    p->src = p->pos = copy;
    p->end = copy + len;
    p->line = 1;
    p->at_start = true;
    p->fn = &ctx;
    /* "#!" in der ersten Zeile */
    if (len >= 2 && copy[0] == '#' && copy[1] == '!')
        skip_line(p);
    next(p);
    bool lexical = false;
    js_node *list = NULL, **tail = &list;
    while (p->tk != TK_EOF && !p->failed) {
        js_node *s = statement(p);
        if (p->failed || !s)
            break;
        *tail = s;
        tail = &s->next;
    }
    (void)lexical;
    info->body = list;
    info->vars = ctx.vars;
    info->nvars = ctx.nvars;
    info->funcs = ctx.funcs;
    info->nfuncs = ctx.nfuncs;
    info->simple_params = true;
    info->source = copy;
    info->source_len = (uint32_t)len;
    bool failed = p->failed || J->fatal;
    kfree(p->buf);
    kfree(p);
    if (failed) {
        if (!J->throwing && !J->fatal)
            js_throw(J, JS_E_SYNTAX, "Invalid script");
        return NULL;
    }
    return info;
}
