/*
 * JavaScript: Map, Set, WeakMap und WeakSet - siehe js.h
 *
 * Eine Streutabelle, die sich die Reihenfolge des Einfügens merkt: Die
 * Einträge stehen hintereinander in einem Feld, Gelöschtes bleibt als Lücke
 * stehen, bis das Feld neu gepackt wird. Jeder Eintrag trägt eine laufende
 * Nummer; ein Durchlauf merkt sich die Nummer des letzten Glieds und findet
 * so auch nach dem Packen seine Stelle wieder.
 *
 * Die schwachen Sammlungen halten ihre Schlüssel nicht am Leben: Die
 * Speicherbereinigung (js_core.c) fragt hier nach, welche Werte über noch
 * lebende Schlüssel erreichbar sind, und wirft danach die übrigen Einträge
 * hinaus.
 */
#include "js_impl.h"
#include "string.h"

#define ARGI(i) (J->bot + 2 + (i))
#define THISI (J->bot + 1)
#define MAGIC (js_callee(J)->u.n.magic)

enum { C_MAP, C_SET, C_WEAKMAP, C_WEAKSET };
#define T_DEAD 0xFF                     /* in key.t: gelöschter Eintrag */

typedef struct {
    js_value key, value;
    uint64_t seq;
    uint32_t hash;
    uint32_t next;                      /* nächster Eintrag derselben Kette + 1, 0: Ende */
} entry;

typedef struct {
    entry *e;
    uint32_t used, cap, live;
    uint32_t *buckets;                  /* Eintrag + 1 */
    uint32_t nbuckets;                  /* Zweierpotenz */
    uint64_t seq;
    uint32_t epoch;                     /* zählt, wie oft die Einträge umgezogen sind */
    uint8_t kind;
} coll;

static void coll_mark(js_state *J, js_object *o);
static void coll_finalize(js_state *J, js_object *o);

static const js_host_class coll_classes[4] = {
    { "Map", NULL, NULL, coll_finalize, coll_mark },
    { "Set", NULL, NULL, coll_finalize, coll_mark },
    { "WeakMap", NULL, NULL, coll_finalize, coll_mark },
    { "WeakSet", NULL, NULL, coll_finalize, coll_mark },
};

/* Die Sammlung hinter einem Wert - oder NULL, wenn er keine (dieser Art) ist */
static coll *coll_of(const js_value *v, int kind)
{
    if (v->t != JS_T_OBJ || v->u.o->cls != JS_C_HOST || v->u.o->u.host.cls != &coll_classes[kind])
        return NULL;
    return v->u.o->u.host.ptr;
}

static coll *any_coll(const js_value *v)
{
    if (v->t != JS_T_OBJ || v->u.o->cls != JS_C_HOST)
        return NULL;
    const js_host_class *c = v->u.o->u.host.cls;
    return c >= coll_classes && c < coll_classes + 4 ? v->u.o->u.host.ptr : NULL;
}

/* --- Streuwerte und Gleichheit ----------------------------------------------------------------------- */

static uint32_t mix(uint64_t x)
{
    x ^= x >> 33;
    x *= 0xFF51AFD7ED558CCDull;
    x ^= x >> 33;
    return (uint32_t)x | 1;
}

static uint32_t hash_value(const js_value *v)
{
    switch (v->t) {
    case JS_T_UNDEF: return 3;
    case JS_T_NULL: return 5;
    case JS_T_BOOL: return v->u.b ? 7 : 9;
    case JS_T_NUM: {
        double n = v->u.n;
        uint64_t bits;
        if (n == 0)
            n = 0;                      /* -0 und +0 sind derselbe Schlüssel */
        if (js_isnan(n))
            return 11;
        memcpy(&bits, &n, sizeof(bits));
        return mix(bits);
    }
    case JS_T_STR: {
        uint32_t h = 2166136261u;
        for (uint32_t i = 0; i < v->u.s->len; i++)
            h = (h ^ (unsigned char)v->u.s->data[i]) * 16777619u;
        return h | 1;
    }
    default:
        return mix((uint64_t)(uintptr_t)v->u.o);
    }
}

/* Gleich im Sinne der Sammlungen: NaN gleicht NaN, +0 gleicht -0 */
static bool same_key(const js_value *a, const js_value *b)
{
    if (a->t != b->t)
        return false;
    if (a->t == JS_T_NUM)
        return a->u.n == b->u.n || (js_isnan(a->u.n) && js_isnan(b->u.n));
    return js_strict_equal(a, b);
}

/* --- Die Tabelle ------------------------------------------------------------------------------------- */

static int coll_find(const coll *c, const js_value *key, uint32_t hash)
{
    if (!c->nbuckets)
        return -1;
    for (uint32_t i = c->buckets[hash & (c->nbuckets - 1)]; i; i = c->e[i - 1].next) {
        const entry *e = &c->e[i - 1];
        if (e->hash == hash && e->key.t != T_DEAD && same_key(&e->key, key))
            return (int)(i - 1);
    }
    return -1;
}

/* Packt die Einträge neu (ohne Lücken) in ein Feld für mindestens want Einträge. */
static bool coll_rebuild(js_state *J, coll *c, uint32_t want)
{
    uint32_t cap = 8, nbuckets = 8;
    while (cap < want)
        cap *= 2;
    while (nbuckets < cap)
        nbuckets *= 2;
    entry *e = js_alloc(J, (size_t)cap * sizeof(entry));
    uint32_t *buckets = e ? js_alloc(J, (size_t)nbuckets * sizeof(uint32_t)) : NULL;
    if (!buckets) {
        js_release(J, e, (size_t)cap * sizeof(entry));
        return false;
    }
    memset(buckets, 0, (size_t)nbuckets * sizeof(uint32_t));
    uint32_t n = 0;
    for (uint32_t i = 0; i < c->used; i++) {
        if (c->e[i].key.t == T_DEAD)
            continue;
        e[n] = c->e[i];
        uint32_t *slot = &buckets[e[n].hash & (nbuckets - 1)];
        e[n].next = *slot;
        *slot = ++n;
    }
    if (n != c->used)
        c->epoch++;                     /* Einträge haben neue Plätze: Durchläufe suchen neu */
    js_release(J, c->e, (size_t)c->cap * sizeof(entry));
    js_release(J, c->buckets, (size_t)c->nbuckets * sizeof(uint32_t));
    c->e = e;
    c->buckets = buckets;
    c->cap = cap;
    c->nbuckets = nbuckets;
    c->used = c->live = n;
    return true;
}

static bool coll_set(js_state *J, coll *c, js_value key, js_value value)
{
    if (key.t == JS_T_NUM && key.u.n == 0)
        key.u.n = 0;                    /* -0 wird als +0 abgelegt */
    uint32_t hash = hash_value(&key);
    int at = coll_find(c, &key, hash);
    if (at >= 0) {
        c->e[at].value = value;
        return true;
    }
    if (c->used == c->cap) {
        if (c->live >= JS_ARRAY_MAX)
            return js_throw(J, JS_E_RANGE, "Map maximum size exceeded");
        /* viele Lücken: nur packen; sonst wachsen */
        if (!coll_rebuild(J, c, c->live * 2 > c->cap ? c->cap * 2 : c->cap))
            return false;
    }
    entry *e = &c->e[c->used];
    e->key = key;
    e->value = value;
    e->hash = hash;
    e->seq = ++c->seq;
    uint32_t *slot = &c->buckets[hash & (c->nbuckets - 1)];
    e->next = *slot;
    *slot = ++c->used;
    c->live++;
    return true;
}

static void coll_unlink(coll *c, uint32_t at)
{
    uint32_t *link = &c->buckets[c->e[at].hash & (c->nbuckets - 1)];
    while (*link && *link != at + 1)
        link = &c->e[*link - 1].next;
    if (*link)
        *link = c->e[at].next;
    c->e[at].key.t = T_DEAD;
    c->e[at].key.u.o = NULL;
    c->e[at].value = js_undef();
    c->live--;
}

static bool coll_delete(coll *c, const js_value *key)
{
    int at = coll_find(c, key, hash_value(key));
    if (at < 0)
        return false;
    coll_unlink(c, (uint32_t)at);
    return true;
}

static void coll_clear(js_state *J, coll *c)
{
    js_release(J, c->e, (size_t)c->cap * sizeof(entry));
    js_release(J, c->buckets, (size_t)c->nbuckets * sizeof(uint32_t));
    c->e = NULL;
    c->buckets = NULL;
    c->cap = c->nbuckets = c->used = c->live = 0;
    c->epoch++;
}

/*
 * Für Durchläufe: der erste lebende Eintrag ab der gemerkten Stelle. *at ist
 * der Platz hinter dem zuletzt gelieferten Eintrag, seq dessen Nummer, epoch
 * der Stand, zu dem *at galt. Liefert den Platz oder -1 (zu Ende).
 */
static int coll_step(const coll *c, uint32_t *at, uint64_t seq, uint32_t *epoch)
{
    uint32_t i = *at;
    if (*epoch != c->epoch) {
        /* die Einträge sind umgezogen: den ersten mit größerer Nummer suchen */
        uint32_t lo = 0, hi = c->used;
        while (lo < hi) {
            uint32_t mid = lo + (hi - lo) / 2;
            if (c->e[mid].seq > seq)
                hi = mid;
            else
                lo = mid + 1;
        }
        i = lo;
        *epoch = c->epoch;
    }
    while (i < c->used && c->e[i].key.t == T_DEAD)
        i++;
    if (i >= c->used) {
        *at = i;
        return -1;
    }
    *at = i + 1;
    return (int)i;
}

/* --- Speicherbereinigung ----------------------------------------------------------------------------- */

static void coll_finalize(js_state *J, js_object *o)
{
    coll *c = o->u.host.ptr;
    if (!c)
        return;
    js_release(J, c->e, (size_t)c->cap * sizeof(entry));
    js_release(J, c->buckets, (size_t)c->nbuckets * sizeof(uint32_t));
    js_release(J, c, sizeof(coll));
    o->u.host.ptr = NULL;
}

static void coll_mark(js_state *J, js_object *o)
{
    coll *c = o->u.host.ptr;
    if (!c)
        return;
    if (c->kind >= C_WEAKMAP && js_gc_weak(J, o))
        return;                         /* schwach: wird nach dem Markieren gesondert behandelt */
    for (uint32_t i = 0; i < c->used; i++) {
        if (c->e[i].key.t == T_DEAD)
            continue;
        js_gc_mark(J, &c->e[i].key);
        js_gc_mark(J, &c->e[i].value);
    }
}

static bool alive(const js_value *v)
{
    if (v->t == JS_T_OBJ)
        return v->u.o->gc.mark || v->u.o->gc.pinned;
    if (v->t == JS_T_SYM || v->t == JS_T_STR)
        return v->u.s->gc.mark || v->u.s->gc.pinned;
    return true;
}

bool js_coll_trace_weak(js_state *J, js_object *o)
{
    coll *c = o->u.host.ptr;
    bool found = false;
    for (uint32_t i = 0; c && i < c->used; i++) {
        entry *e = &c->e[i];
        if (e->key.t == T_DEAD || !alive(&e->key) || alive(&e->value))
            continue;
        js_gc_mark(J, &e->value);
        found = true;
    }
    return found;
}

void js_coll_sweep_weak(js_state *J, js_object *o)
{
    coll *c = o->u.host.ptr;
    (void)J;
    for (uint32_t i = 0; c && i < c->used; i++) {
        if (c->e[i].key.t != T_DEAD && !alive(&c->e[i].key))
            coll_unlink(c, i);
    }
}

/* --- Map und Set ------------------------------------------------------------------------------------- */

static js_object *coll_new(js_state *J, int kind, js_object *proto)
{
    coll *c = js_alloc(J, sizeof(coll));
    if (!c)
        return NULL;
    memset(c, 0, sizeof(*c));
    c->kind = (uint8_t)kind;
    js_object *o = js_object_new(J, JS_C_HOST, proto);
    if (o == J->dummy) {
        js_release(J, c, sizeof(coll));
        return NULL;
    }
    o->u.host.cls = &coll_classes[kind];
    o->u.host.ptr = c;
    return o;
}

static bool weak_key_ok(js_state *J, const js_value *key)
{
    if (key->t == JS_T_OBJ || key->t == JS_T_SYM)
        return true;
    return js_throw(J, JS_E_TYPE, "Invalid value used as weak map key");
}

static js_object *proto_of(js_state *J, int kind)
{
    static const char *const names[4] = { "Map", "Set", "WeakMap", "WeakSet" };
    js_prop *p = js_own(J->registry, js_intern_cstr(J, names[kind]));
    return p && p->v.t == JS_T_OBJ ? p->v.u.o : J->object_proto;
}

/* new Map(paare), new Set(glieder), new WeakMap(paare), new WeakSet(glieder) */
static bool coll_ctor(js_state *J, int argc)
{
    int kind = MAGIC;
    if (!J->constructing)
        return js_throw(J, JS_E_TYPE, "Constructor %s requires 'new'", coll_classes[kind].name);
    js_object *o = coll_new(J, kind, proto_of(J, kind));
    if (!o)
        return false;
    js_push(J, js_obj(o));
    int result = J->top - 1;
    if (argc == 0 || js_arg(J, 0)->t <= JS_T_NULL)
        return true;
    coll *c = o->u.host.ptr;
    bool pairs = kind == C_MAP || kind == C_WEAKMAP;
    js_iter it;
    if (!js_iter_open(J, ARGI(0), &it))
        return false;
    for (;;) {
        bool done;
        if (!js_iter_next(J, &it, &done))
            return false;
        if (done)
            break;
        bool ok = true;
        if (pairs) {
            if (js_top(J, 0)->t != JS_T_OBJ)
                ok = js_throw(J, JS_E_TYPE, "Iterator value is not an entry object");
            ok = ok && js_get_index(J, J->top - 1, 0) && js_get_index(J, J->top - 2, 1) &&
                 (kind == C_MAP || weak_key_ok(J, js_top(J, 1))) &&
                 coll_set(J, c, *js_top(J, 1), *js_top(J, 0));
            if (ok)
                js_pop(J, 2);
        } else {
            ok = (kind == C_SET || weak_key_ok(J, js_top(J, 0))) &&
                 coll_set(J, c, *js_top(J, 0), js_undef());
        }
        if (!ok)
            return js_iter_close(J, &it, true);
        js_pop(J, 1);
    }
    J->top = result + 1;
    return true;
}

static coll *this_coll(js_state *J, int kind)
{
    coll *c = coll_of(js_this(J), kind);
    if (!c)
        js_throw(J, JS_E_TYPE, "Method called on a value that is not a %s", coll_classes[kind].name);
    return c;
}

enum { M_GET, M_SET, M_HAS, M_DELETE, M_CLEAR, M_SIZE, M_ADD, M_FOREACH };

/* Die Methoden aller vier Arten: magic = art * 16 + methode */
static bool coll_method(js_state *J, int argc)
{
    int kind = MAGIC / 16, method = MAGIC % 16;
    coll *c = this_coll(J, kind);
    if (!c)
        return false;
    js_value *key = js_arg(J, 0);
    switch (method) {
    case M_GET: {
        int at = coll_find(c, key, hash_value(key));
        return js_push(J, at >= 0 ? c->e[at].value : js_undef());
    }
    case M_HAS:
        return js_push(J, js_bool(coll_find(c, key, hash_value(key)) >= 0));
    case M_SET: case M_ADD:
        if (kind >= C_WEAKMAP && !weak_key_ok(J, key))
            return false;
        if (!coll_set(J, c, *key, method == M_SET ? *js_arg(J, 1) : js_undef()))
            return false;
        return js_push(J, *js_this(J));
    case M_DELETE:
        return js_push(J, js_bool(coll_delete(c, key)));
    case M_CLEAR:
        coll_clear(J, c);
        return js_push(J, js_undef());
    case M_SIZE:
        return js_push(J, js_num(c->live));
    default: {                          /* forEach(fn, thisArg) */
        if (!js_is_callable(key))
            return js_throw(J, JS_E_TYPE, "forEach needs a function");
        uint32_t at = 0, epoch = c->epoch;
        uint64_t seq = 0;
        for (;;) {
            int i = coll_step(c, &at, seq, &epoch);
            if (i < 0)
                break;
            seq = c->e[i].seq;
            js_push(J, *js_arg(J, 0));
            js_push(J, argc > 1 ? *js_arg(J, 1) : js_undef());
            js_push(J, kind == C_SET ? c->e[i].key : c->e[i].value);
            js_push(J, c->e[i].key);
            js_push(J, *js_this(J));
            if (!js_call(J, 3))
                return false;
            js_pop(J, 1);
            js_checkpoint(J);
        }
        return js_push(J, js_undef());
    }
    }
}

/* --- Durchläufe -------------------------------------------------------------------------------------- */

static js_value hidden(const js_object *o, const js_string *key)
{
    js_prop *p = js_own(o, key);
    return p ? p->v : js_undef();
}

/* Legt das Glied i so auf den Stapel, wie es die Art des Durchlaufs verlangt (0 Schlüssel, 1 Werte, 2 Paare) */
static bool push_item(js_state *J, const coll *c, int i, int what)
{
    const entry *e = &c->e[i];
    if (what == 0 || (what == 1 && c->kind == C_SET))
        return js_push(J, e->key);
    if (what == 1)
        return js_push(J, e->value);
    js_object *pair = js_array_new(J, 2);
    js_push(J, js_obj(pair));
    return js_array_push(J, pair, e->key) &&
           js_array_push(J, pair, c->kind == C_SET ? e->key : e->value);
}

/* keys() (0), values() (1), entries() (2) von Map (magic < 16) und Set (magic >= 16) */
static bool coll_iterator(js_state *J, int argc)
{
    (void)argc;
    int kind = MAGIC / 16;
    coll *c = this_coll(J, kind);
    if (!c)
        return false;
    js_prop *proto = js_own(J->registry, js_intern_cstr(J, kind == C_MAP ? "MapIterator"
                                                                         : "SetIterator"));
    js_object *it = js_object_new(J, JS_C_OBJECT, proto ? proto->v.u.o : J->iterator_proto);
    js_push(J, js_obj(it));
    js_define(J, it, J->hid[H_target], *js_this(J), JS_P_WRITE);
    js_define(J, it, J->hid[H_kind], js_num(MAGIC % 16), JS_P_WRITE);
    js_define(J, it, J->hid[H_index], js_num(0), JS_P_WRITE);
    js_define(J, it, J->hid[H_data], js_num(0), JS_P_WRITE);
    js_define(J, it, J->hid[H_extra], js_num(c->epoch), JS_P_WRITE);
    return !J->fatal;
}

static bool coll_iter_next(js_state *J, int argc)
{
    (void)argc;
    js_value *self = js_this(J);
    if (self->t != JS_T_OBJ || !js_own(self->u.o, J->hid[H_extra]))
        return js_throw(J, JS_E_TYPE, "next() called on a value that is not an iterator");
    js_object *it = self->u.o;
    js_value target = hidden(it, J->hid[H_target]);
    coll *c = any_coll(&target);
    if (!c)
        return js_push_iter_result(J, js_undef(), true);
    uint32_t at = (uint32_t)hidden(it, J->hid[H_index]).u.n;
    uint32_t epoch = (uint32_t)hidden(it, J->hid[H_extra]).u.n;
    uint64_t seq = (uint64_t)hidden(it, J->hid[H_data]).u.n;
    int i = coll_step(c, &at, seq, &epoch);
    if (i < 0) {
        js_define(J, it, J->hid[H_target], js_undef(), JS_P_WRITE);
        return js_push_iter_result(J, js_undef(), true);
    }
    js_define(J, it, J->hid[H_index], js_num(at), JS_P_WRITE);
    js_define(J, it, J->hid[H_data], js_num((double)c->e[i].seq), JS_P_WRITE);
    js_define(J, it, J->hid[H_extra], js_num(epoch), JS_P_WRITE);
    if (!push_item(J, c, i, (int)hidden(it, J->hid[H_kind]).u.n))
        return false;
    return js_push_iter_result(J, *js_top(J, 0), false);
}

/* Der kurze Weg für for-of und Spread: ohne Iterator-Objekt (siehe js_sym.c) */
bool js_coll_iterable(js_state *J, const js_value *v, int *what)
{
    coll *c = any_coll(v);
    if (!c || c->kind > C_SET)
        return false;
    js_object *builtin = c->kind == C_MAP ? J->map_entries : J->set_values;
    js_prop *p = js_lookup(v->u.o, J->sym[SYM_iterator], NULL);
    if (!p || (p->flags & JS_P_ACCESSOR) || p->v.t != JS_T_OBJ || p->v.u.o != builtin)
        return false;
    *what = c->kind == C_MAP ? 2 : 1;
    return true;
}

bool js_coll_next(js_state *J, const js_value *v, int what, uint32_t *at, uint64_t *seq,
                  uint32_t *epoch, bool *done)
{
    coll *c = any_coll(v);
    int i = c ? coll_step(c, at, *seq, epoch) : -1;
    *done = i < 0;
    if (i < 0)
        return true;
    *seq = c->e[i].seq;
    return push_item(J, c, i, what);
}

uint32_t js_coll_epoch(const js_value *v)
{
    coll *c = any_coll(v);
    return c ? c->epoch : 0;
}

/* --- Einrichten ------------------------------------------------------------------------------------ */

static js_object *define_class(js_state *J, int kind)
{
    const char *name = coll_classes[kind].name;
    js_object *proto = js_object_new(J, JS_C_OBJECT, J->object_proto);
    js_object *ctor = js_native_new(J, coll_ctor, name, 0, kind);
    js_define(J, ctor, J->k[K_prototype], js_obj(proto), 0);
    js_define(J, proto, J->k[K_constructor], js_obj(ctor), JS_P_WRITE | JS_P_CONF);
    js_define(J, proto, J->sym[SYM_toStringTag], js_str(js_intern_cstr(J, name)), JS_P_CONF);
    js_define(J, J->global, js_intern_cstr(J, name), js_obj(ctor), JS_P_WRITE | JS_P_CONF);
    js_define(J, J->registry, js_intern_cstr(J, name), js_obj(proto), JS_P_ALL);
    js_def_native(J, proto, "has", coll_method, 1, kind * 16 + M_HAS);
    js_def_native(J, proto, "delete", coll_method, 1, kind * 16 + M_DELETE);
    if (kind == C_MAP || kind == C_WEAKMAP) {
        js_def_native(J, proto, "get", coll_method, 1, kind * 16 + M_GET);
        js_def_native(J, proto, "set", coll_method, 2, kind * 16 + M_SET);
    } else {
        js_def_native(J, proto, "add", coll_method, 1, kind * 16 + M_ADD);
    }
    if (kind <= C_SET) {
        js_def_native(J, proto, "clear", coll_method, 0, kind * 16 + M_CLEAR);
        js_def_native(J, proto, "forEach", coll_method, 1, kind * 16 + M_FOREACH);
        js_def_accessor(J, proto, "size", coll_method, NULL, kind * 16 + M_SIZE);
    }
    return proto;
}

static js_object *define_iterator(js_state *J, js_object *proto, const char *name, int kind,
                                  int what, bool is_default)
{
    js_object *f = js_native_new(J, coll_iterator, name, 0, kind * 16 + what);
    js_define(J, proto, js_intern_cstr(J, name), js_obj(f), JS_P_WRITE | JS_P_CONF);
    if (is_default)
        js_define(J, proto, J->sym[SYM_iterator], js_obj(f), JS_P_WRITE | JS_P_CONF);
    return f;
}

void js_coll_init(js_state *J)
{
    static const char *const iter_names[2] = { "MapIterator", "SetIterator" };
    static const char *const iter_tags[2] = { "Map Iterator", "Set Iterator" };
    for (int kind = C_MAP; kind <= C_SET; kind++) {
        js_object *ip = js_object_new(J, JS_C_OBJECT, J->iterator_proto);
        js_def_native(J, ip, "next", coll_iter_next, 0, 0);
        js_define(J, ip, J->sym[SYM_toStringTag], js_str(js_intern_cstr(J, iter_tags[kind])),
                  JS_P_CONF);
        js_define(J, J->registry, js_intern_cstr(J, iter_names[kind]), js_obj(ip), JS_P_ALL);
    }
    js_object *map = define_class(J, C_MAP);
    define_iterator(J, map, "keys", C_MAP, 0, false);
    define_iterator(J, map, "values", C_MAP, 1, false);
    J->map_entries = define_iterator(J, map, "entries", C_MAP, 2, true);

    js_object *set = define_class(J, C_SET);
    J->set_values = define_iterator(J, set, "values", C_SET, 1, true);
    js_define(J, set, js_intern_cstr(J, "keys"), js_obj(J->set_values), JS_P_WRITE | JS_P_CONF);
    define_iterator(J, set, "entries", C_SET, 2, false);

    define_class(J, C_WEAKMAP);
    define_class(J, C_WEAKSET);
}
