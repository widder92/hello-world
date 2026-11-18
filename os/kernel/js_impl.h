/* Innenleben der JavaScript-Maschine - für die Dateien js_*.c und die Anbindung an Seiten */
#pragma once

#include "js.h"

typedef struct js_string js_string;
typedef struct js_object js_object;
typedef struct js_node js_node;

/* --- Werte ------------------------------------------------------------------------------- */

enum { JS_T_UNDEF, JS_T_NULL, JS_T_BOOL, JS_T_NUM, JS_T_STR, JS_T_OBJ };

typedef struct {
    uint8_t t;
    union {
        bool b;
        double n;
        js_string *s;
        js_object *o;
    } u;
} js_value;

/* Alles, was die Speicherbereinigung verwaltet, beginnt so */
typedef struct js_gc {
    struct js_gc *next;
    uint8_t kind;                       /* 0: Zeichenkette, 1: Objekt */
    uint8_t mark;
    uint8_t pinned;                     /* wird nie weggeräumt (Namen im Programmbaum) */
} js_gc;

#define JS_NO_INDEX 0xFFFFFFFFu

/* Zeichenketten: unveränderlich, UTF-8; "chars" zählt Zeichen (Codepunkte) */
struct js_string {
    js_gc gc;
    uint8_t ascii;                      /* nur ASCII: Zeichen i ist Byte i */
    uint8_t interned;                   /* steht in der Namenstabelle: Zeigervergleich genügt */
    uint32_t len;                       /* Bytes */
    uint32_t chars;
    uint32_t hash;
    uint32_t index;                     /* als Feldindex gelesen, sonst JS_NO_INDEX (nur bei Namen) */
    char data[];
};

/* --- Objekte ------------------------------------------------------------------------------ */

enum { JS_P_ENUM = 1, JS_P_WRITE = 2, JS_P_CONF = 4, JS_P_ACCESSOR = 8, JS_P_CONST = 16,
       JS_P_ALL = 7 };

typedef struct {
    js_string *key;                     /* NULL: gelöscht */
    js_value v;                         /* Wert - oder bei Zugriffsfunktionen die Lesefunktion */
    js_object *set;                     /* ... und die Schreibfunktion */
    uint8_t flags;
} js_prop;

enum {
    JS_C_OBJECT, JS_C_ARRAY, JS_C_FUNCTION, JS_C_NATIVE, JS_C_ERROR, JS_C_BOOLEAN, JS_C_NUMBER,
    JS_C_STRING, JS_C_DATE, JS_C_REGEXP, JS_C_SCOPE, JS_C_HOST,
};

/* Eingebaute Funktion: Argumente über js_arg(), Ergebnis mit js_push*(); false: Ausnahme */
typedef bool (*js_native_fn)(js_state *J, int argc);

/* Fremde Objekte (etwa Elemente einer Seite): eigene Antworten auf Lesen und Schreiben */
typedef struct js_host_class {
    const char *name;
    /* Liefern true, wenn sie zuständig waren. get legt den Wert auf den Stapel. */
    bool (*get)(js_state *J, js_object *o, js_string *key, bool *found);
    bool (*put)(js_state *J, js_object *o, js_string *key, const js_value *v, bool *done);
    /* Räumt auf, was am Objekt hängt, wenn es verschwindet (oder NULL) */
    void (*finalize)(js_state *J, js_object *o);
} js_host_class;

enum { JS_F_ARROW = 1, JS_F_CLASS = 2, JS_F_DERIVED = 4, JS_F_NOEXT = 8, JS_F_CTOR = 16 };

struct js_object {
    js_gc gc;
    uint8_t cls;
    uint8_t flags;
    js_object *proto;
    js_prop *props;
    uint32_t count, cap;                /* belegte Plätze (samt gelöschter) und Platz */
    uint32_t *slots;                    /* Suchtabelle Name -> Platz+1, ab einer gewissen Größe */
    uint32_t slot_cap;
    union {
        struct { js_value *v; uint32_t len, cap; } arr;
        struct { js_node *fn; js_object *scope; js_value self; js_object *home; } f;
        struct { js_native_fn fn; int magic; js_object *data; uint8_t nargs; } n;
        js_value prim;                  /* Boolean, Number, String, Date (Zahl) */
        struct { void *prog; js_string *source; uint8_t flags; } re;
        struct { const js_host_class *cls; void *ptr; uint32_t id; } host;
    } u;
};

/* --- Programmbaum -------------------------------------------------------------------------- */

enum {
    /* Ausdrücke */
    N_NUM, N_STR, N_TEMPLATE, N_REGEX, N_IDENT, N_THIS, N_NULL, N_TRUE, N_FALSE, N_ARRAY, N_OBJECT,
    N_PROP, N_FUNC, N_CLASS, N_MEMBER, N_INDEX, N_CALL, N_NEW, N_UNARY, N_UPDATE, N_BINARY,
    N_LOGICAL, N_ASSIGN, N_COND, N_SEQ, N_SPREAD, N_SUPER, N_HOLE, N_CHAIN,
    /* Muster zum Zerlegen */
    N_ARRAY_PAT, N_OBJECT_PAT, N_DEFAULT, N_REST,
    /* Anweisungen */
    S_VAR, S_DECL, S_EXPR, S_BLOCK, S_IF, S_FOR, S_FORIN, S_FOROF, S_WHILE, S_DO, S_RETURN,
    S_BREAK, S_CONTINUE, S_THROW, S_TRY, S_SWITCH, S_CASE, S_LABEL, S_FUNC, S_CLASS, S_EMPTY,
};

/* Operatoren (in "op") */
enum {
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_POW, OP_SHL, OP_SHR, OP_USHR, OP_AND, OP_OR, OP_XOR,
    OP_EQ, OP_NE, OP_SEQ, OP_SNE, OP_LT, OP_LE, OP_GT, OP_GE, OP_IN, OP_INSTANCEOF,
    OP_LAND, OP_LOR, OP_NULLISH,
    OP_NOT, OP_BITNOT, OP_NEG, OP_POS, OP_TYPEOF, OP_VOID, OP_DELETE, OP_INC, OP_DEC,
    OP_NONE,
};

/* Merkmale eines Knotens (in "flags") */
enum {
    NF_OPTIONAL = 1,                    /* "?." */
    NF_PREFIX = 2,                      /* ++x statt x++ */
    NF_LET = 4, NF_CONST = 8,           /* S_VAR: Art der Vereinbarung */
    NF_GETTER = 16, NF_SETTER = 32, NF_COMPUTED = 64, NF_STATIC = 128, NF_METHOD = 256,
    NF_SCOPE = 512,                     /* Block mit eigenen let/const-Namen */
    NF_SHORTHAND = 1024,
    NF_FIELD = 2048,                    /* Klassen: Feld statt Methode */
    NF_SPREAD = 4096,                   /* Aufruf oder Feld enthält "..." */
};

typedef struct js_fninfo {
    js_string *name;
    js_node *params;                    /* Liste: N_IDENT, Muster, N_DEFAULT, N_REST */
    js_node *body;                      /* Liste von Anweisungen - oder ein Ausdruck (expr_body) */
    js_string **vars;                   /* mit "var" vereinbarte Namen */
    js_node **funcs;                    /* Funktionsvereinbarungen: gelten ab Beginn */
    int nvars, nfuncs, nparams;
    uint8_t flags;                      /* JS_F_ARROW, JS_F_CLASS, JS_F_DERIVED */
    bool expr_body, uses_arguments, simple_params;
    js_node *fields;                    /* Klassen: Felder, die jedes neue Objekt bekommt */
    const char *source;                 /* Quelltext der Funktion (für toString) */
    uint32_t source_len;
} js_fninfo;

struct js_node {
    uint8_t type, op;
    uint16_t flags;
    int32_t line;
    js_node *a, *b, *c, *d;
    js_node *next;                      /* nächstes Glied einer Liste */
    union {
        double num;
        js_string *str;
        js_fninfo *fn;
    } u;
};

/* --- Zustand ------------------------------------------------------------------------------- */

#define JS_STACK_SIZE   20000           /* Werte auf dem Stapel */
#define JS_STRING_MAX   (16u * 1024 * 1024)
#define JS_ARRAY_MAX    (4u * 1024 * 1024)

typedef struct js_arena {
    struct js_arena *next;
    size_t used, size;
    char data[];
} js_arena;

/* Häufig gebrauchte Namen, einmal eingetragen */
enum {
    K_length, K_prototype, K_constructor, K_name, K_message, K_toString, K_valueOf, K_arguments,
    K_undefined, K_null, K_true, K_false, K_object, K_function, K_number, K_string, K_boolean,
    K_empty, K_index, K_input, K_lastIndex, K_stack, K_get, K_set, K_value, K_writable,
    K_enumerable, K_configurable, K_callee, K_join, K_toJSON, K_then, K_done, K_next, K_this,
    K_source, K_global, K_ignoreCase, K_multiline, K_line, K_cause, K_COUNT
};

struct js_state {
    js_host_t host;

    js_value *stack;
    int top;
    int bot;                            /* laufende eingebaute Funktion: ihr Platz auf dem Stapel */
    int fbase;                          /* laufende JS-Funktion: ihr Platz auf dem Stapel */
    bool constructing;                  /* die laufende eingebaute Funktion wurde mit "new" gerufen */

    js_gc *all;                         /* alles, was die Speicherbereinigung verwaltet */
    size_t heap, heap_max, gc_at;
    uint32_t gc_runs;
    bool gc_stress;                     /* für Tests: bei jeder Gelegenheit aufräumen */
    js_gc **gray;                       /* Arbeitsliste beim Markieren */
    size_t gray_count, gray_cap;

    js_string **names;                  /* Namenstabelle */
    uint32_t name_count, name_cap;
    js_string *k[K_COUNT];
    js_string *empty;                   /* "" - auch Ersatz bei Speichermangel */

    js_arena *arena;                    /* Programmbäume und ihr Quelltext */
    size_t arena_bytes;

    js_object *global, *registry, *dummy;
    js_object *object_proto, *function_proto, *array_proto, *string_proto, *number_proto,
              *boolean_proto, *error_proto, *date_proto, *regexp_proto;
    js_object *error_protos[8];

    /* Ausnahmen und Ergebnisse von Anweisungen */
    bool throwing;
    js_value exception;
    js_value ret;
    js_string *label;                   /* Ziel von break/continue mit Marke */
    int line;                           /* zuletzt ausgeführte Zeile */

    /* Grenzen */
    bool short_circuit;                 /* "a?.b": die Kette endet hier mit undefined */
    bool fatal;                         /* abgebrochen: nichts fängt das mehr auf */
    const char *fatal_reason;
    uint64_t steps, deadline;
    uintptr_t stack_floor;              /* tiefer darf der C-Stack nicht wachsen */
    int depth;

    char error[400];
    uint32_t rng;
    void *user;                         /* für die Anbindung (Seite) */
};

/*
 * Wo der C-Stack gerade steht - zum Vergleich mit stack_floor. (Die Adresse
 * einer lokalen Variable taugt dafür nicht überall: Prüfwerkzeuge legen
 * solche Variablen mitunter woandershin.)
 */
#define JS_STACK_HERE() ((uintptr_t)__builtin_frame_address(0))

/* --- js_core.c ------------------------------------------------------------------------------ */

void *js_alloc(js_state *J, size_t size);           /* gezählt gegen die Speichergrenze; NULL möglich */
void js_release(js_state *J, void *p, size_t size);
void *js_arena_alloc(js_state *J, size_t size);     /* für Programmbäume; NULL möglich */

js_string *js_string_new(js_state *J, const char *text, size_t len);
js_string *js_string_concat(js_state *J, const js_string *a, const js_string *b);
js_string *js_intern(js_state *J, const char *text, size_t len);
js_string *js_intern_string(js_state *J, js_string *s);
js_string *js_intern_cstr(js_state *J, const char *text);
js_string *js_index_name(js_state *J, uint32_t index);
/* Byte-Stelle des Zeichens mit der Nummer index (höchstens len) */
uint32_t js_string_offset(const js_string *s, uint32_t index);
uint32_t js_utf8_decode(const char *p, const char *end, int *size);
int js_utf8_encode(uint32_t c, char *out);

js_object *js_object_new(js_state *J, int cls, js_object *proto);
js_object *js_array_new(js_state *J, uint32_t reserve);
js_object *js_native_new(js_state *J, js_native_fn fn, const char *name, int nargs, int magic);
bool js_array_push(js_state *J, js_object *a, js_value v);
bool js_array_resize(js_state *J, js_object *a, uint32_t len);

js_prop *js_own(const js_object *o, const js_string *key);
js_prop *js_lookup(const js_object *o, const js_string *key, js_object **holder);
js_prop *js_define(js_state *J, js_object *o, js_string *key, js_value v, int flags);
bool js_delete_own(js_state *J, js_object *o, js_string *key);
void js_def_native(js_state *J, js_object *o, const char *name, js_native_fn fn, int nargs,
                   int magic);
void js_def_value(js_state *J, js_object *o, const char *name, js_value v, int flags);
void js_def_accessor(js_state *J, js_object *o, const char *name, js_native_fn get,
                     js_native_fn set, int magic);

void js_gc_run(js_state *J);
static inline void js_checkpoint(js_state *J)
{
    if (J->heap >= J->gc_at || J->gc_stress)
        js_gc_run(J);
}

/* Werte bauen */
static inline js_value js_undef(void) { js_value v; v.t = JS_T_UNDEF; v.u.o = NULL; return v; }
static inline js_value js_null(void) { js_value v; v.t = JS_T_NULL; v.u.o = NULL; return v; }
static inline js_value js_bool(bool b) { js_value v; v.t = JS_T_BOOL; v.u.o = NULL; v.u.b = b; return v; }
static inline js_value js_num(double n) { js_value v; v.t = JS_T_NUM; v.u.n = n; return v; }
static inline js_value js_str(js_string *s) { js_value v; v.t = JS_T_STR; v.u.s = s; return v; }
static inline js_value js_obj(js_object *o) { js_value v; v.t = JS_T_OBJ; v.u.o = o; return v; }

/* Stapel */
bool js_push(js_state *J, js_value v);
static inline void js_pop(js_state *J, int n) { J->top -= n; }
static inline js_value *js_at(js_state *J, int index) { return &J->stack[index]; }
static inline js_value *js_top(js_state *J, int back) { return &J->stack[J->top - 1 - back]; }
bool js_push_cstr(js_state *J, const char *text);
bool js_push_lstr(js_state *J, const char *text, size_t len);
/* Für eingebaute Funktionen */
static inline js_value *js_arg(js_state *J, int i) { return &J->stack[J->bot + 2 + i]; }
static inline js_value *js_this(js_state *J) { return &J->stack[J->bot + 1]; }
static inline js_object *js_callee(js_state *J) { return J->stack[J->bot].u.o; }

/* Ausnahmen: liefern immer false, damit man "return js_throw(...)" schreiben kann */
enum { JS_E_ERROR, JS_E_TYPE, JS_E_RANGE, JS_E_REFERENCE, JS_E_SYNTAX, JS_E_URI, JS_E_EVAL };
bool js_throw(js_state *J, int kind, const char *fmt, ...);
bool js_throw_value(js_state *J, js_value v);
bool js_fatal(js_state *J, const char *reason);

/* Umwandlungen; die mit Stapelplatz wandeln dort an Ort und Stelle um */
bool js_truthy(const js_value *v);
bool js_to_primitive(js_state *J, int index, int hint);    /* hint: 0 egal, 1 Zahl, 2 Text */
bool js_to_number(js_state *J, int index, double *out);
bool js_to_string(js_state *J, int index);                  /* danach steht dort eine Zeichenkette */
bool js_to_object(js_state *J, int index);
bool js_to_int32(js_state *J, int index, int32_t *out);
bool js_to_key(js_state *J, int index, js_string **key);    /* Name einer Eigenschaft (eingetragen) */
double js_string_to_number(const js_string *s);
const char *js_typeof(const js_value *v);
bool js_is_callable(const js_value *v);
bool js_is_array(const js_value *v);
bool js_strict_equal(const js_value *a, const js_value *b);
bool js_loose_equal(js_state *J, int a, int b, bool *out);
bool js_string_equal(const js_string *a, const js_string *b);
int js_string_compare(const js_string *a, const js_string *b);

/* --- js_num.c --------------------------------------------------------------------------------- */

int js_number_to_text(double value, char *out);             /* out: mindestens 40 Zeichen */
int js_number_to_radix(double value, int radix, char *out, int size);
int js_number_fixed(double value, int digits, char *out, int size);
int js_number_precision(double value, int digits, bool exponential, char *out, int size);
double js_text_to_number(const char *text, size_t len, size_t *used);   /* Dezimalzahl am Anfang */
double js_floor(double x);
double js_ceil(double x);
double js_trunc(double x);
double js_fmod(double x, double y);
double js_pow(double x, double y);
double js_exp(double x);
double js_log(double x);
double js_sin(double x);
double js_cos(double x);
double js_tan(double x);
double js_atan(double x);
double js_atan2(double y, double x);
double js_asin(double x);
bool js_isnan(double x);
bool js_isfinite(double x);
double js_nan(void);
double js_infinity(void);
int32_t js_double_to_int32(double x);
js_string *js_number_string(js_state *J, double n);

/* --- js_parse.c ------------------------------------------------------------------------------- */

/* Liest ein Programm; NULL und eine SyntaxError-Ausnahme, wenn es nicht geht. */
js_fninfo *js_parse(js_state *J, const char *source, size_t len, const char *name);

/* --- js_run.c --------------------------------------------------------------------------------- */

/* Stapel: [fn, this, arg0 ... argN-1] -> [ergebnis] */
bool js_call(js_state *J, int argc);
/* Stapel: [fn, arg0 ... argN-1] -> [objekt] */
bool js_construct(js_state *J, int argc);
bool js_get(js_state *J, int obj, js_string *key);          /* legt den Wert auf den Stapel */
bool js_put(js_state *J, int obj, js_string *key);          /* nimmt den Wert vom Stapel */
bool js_get_cstr(js_state *J, int obj, const char *name);
bool js_put_cstr(js_state *J, int obj, const char *name);
bool js_get_value(js_state *J);                             /* [obj, key] -> [wert] */
bool js_put_value(js_state *J);                             /* [obj, key, wert] -> [] */
bool js_get_index(js_state *J, int obj, uint32_t index);
bool js_instanceof(js_state *J, const js_value *v, const js_value *ctor, bool *out);
bool js_exec_program(js_state *J, js_fninfo *program);
js_object *js_function_new(js_state *J, js_node *fn, js_object *scope);
bool js_own_keys(js_state *J, js_object *o, bool only_enumerable, js_object *out);
bool js_iterate(js_state *J, int index, js_object *out);    /* Wert -> Feld seiner Glieder */

/* Vor jedem Einstieg von außen: Grenzen setzen. Danach: Fehler abholen (false: es gab einen). */
void js_enter(js_state *J, uint32_t budget_ms);
bool js_leave(js_state *J, bool ok);

/* --- js_lib.c, js_lib2.c, js_regex.c ------------------------------------------------------------ */

/* Was sich Promises für "gleich danach" vorgemerkt haben */
void js_jobs_run(js_state *J);
/* Zeitgeber: Millisekunden bis zum nächsten (-1: keiner) - und die fälligen ausführen */
int64_t js_timers_due(js_state *J);
bool js_timers_run(js_state *J, uint32_t budget_ms);

void js_lib_init(js_state *J);
void js_regex_init(js_state *J);
void js_typed_init(js_state *J);        /* js_typed.c: ArrayBuffer, typisierte Felder, DataView */
void js_regex_free(js_state *J, js_object *o);
js_object *js_regex_new(js_state *J, js_string *source, js_string *flags);
/* Sucht ab der Byte-Stelle start; caps: 2 Byte-Stellen je Gruppe (-1: nicht beteiligt) */
int js_regex_exec(js_state *J, js_object *re, const js_string *text, uint32_t start, int *caps,
                  int max_groups);
int js_regex_groups(const js_object *re);
