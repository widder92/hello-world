/* JavaScript: Werte, Zeichenketten, Objekte, Speicherbereinigung, Umwandlungen - siehe js.h */
#include "js_impl.h"
#include "memory.h"
#include "string.h"

#include <stdarg.h>

#define STACK_GUARD 256                 /* Luft vor und hinter dem Stapel */

/* --- Speicher ------------------------------------------------------------------------------ */

void *js_alloc(js_state *J, size_t size)
{
    if (size > J->heap_max || J->heap + size > J->heap_max) {
        js_fatal(J, "Das Skript braucht zu viel Speicher");
        return NULL;
    }
    void *p = kmalloc(size);
    if (!p) {
        js_fatal(J, "Nicht genug Speicher für das Skript");
        return NULL;
    }
    J->heap += size;
    return p;
}

void js_release(js_state *J, void *p, size_t size)
{
    if (!p)
        return;
    kfree(p);
    J->heap -= size <= J->heap ? size : J->heap;
}

void *js_arena_alloc(js_state *J, size_t size)
{
    size = (size + 15) & ~(size_t)15;
    if (!J->arena || J->arena->used + size > J->arena->size) {
        size_t block = size > 60000 ? size : 64 * 1024 - sizeof(js_arena);
        if (J->arena_bytes + block > J->heap_max / 2) {
            js_fatal(J, "Das Skript ist zu groß");
            return NULL;
        }
        js_arena *a = kmalloc(sizeof(js_arena) + block);
        if (!a) {
            js_fatal(J, "Nicht genug Speicher für das Skript");
            return NULL;
        }
        a->next = J->arena;
        a->used = 0;
        a->size = block;
        J->arena = a;
        J->arena_bytes += block;
    }
    void *p = J->arena->data + J->arena->used;
    J->arena->used += size;
    memset(p, 0, size);
    return p;
}

/* --- UTF-8 ----------------------------------------------------------------------------------- */

uint32_t js_utf8_decode(const char *p, const char *end, int *size)
{
    const unsigned char *s = (const unsigned char *)p;
    size_t left = (size_t)(end - p);
    uint32_t c = s[0];
    int n = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
    if (n == 0 || (size_t)n > left) {
        *size = 1;
        return 0xFFFD;
    }
    if (n == 1) {
        *size = 1;
        return c;
    }
    c &= n == 2 ? 0x1F : n == 3 ? 0x0F : 0x07;
    for (int i = 1; i < n; i++) {
        if ((s[i] & 0xC0) != 0x80) {
            *size = 1;
            return 0xFFFD;
        }
        c = (c << 6) | (s[i] & 0x3F);
    }
    *size = n;
    return c;
}

int js_utf8_encode(uint32_t c, char *out)
{
    if (c >= 0xD800 && c <= 0xDFFF)
        c = 0xFFFD;                     /* halbe Ersatzpaare gibt es in UTF-8 nicht */
    if (c < 0x80) {
        out[0] = (char)c;
        return 1;
    }
    if (c < 0x800) {
        out[0] = (char)(0xC0 | (c >> 6));
        out[1] = (char)(0x80 | (c & 0x3F));
        return 2;
    }
    if (c < 0x10000) {
        out[0] = (char)(0xE0 | (c >> 12));
        out[1] = (char)(0x80 | ((c >> 6) & 0x3F));
        out[2] = (char)(0x80 | (c & 0x3F));
        return 3;
    }
    if (c > 0x10FFFF)
        c = 0xFFFD;
    if (c < 0x10000)
        return js_utf8_encode(c, out);
    out[0] = (char)(0xF0 | (c >> 18));
    out[1] = (char)(0x80 | ((c >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((c >> 6) & 0x3F));
    out[3] = (char)(0x80 | (c & 0x3F));
    return 4;
}

/* --- Zeichenketten --------------------------------------------------------------------------- */

static uint32_t hash_bytes(const char *text, size_t len)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++)
        h = (h ^ (unsigned char)text[i]) * 16777619u;
    return h ? h : 1;
}

static js_string *string_raw(js_state *J, size_t len)
{
    if (len > JS_STRING_MAX) {
        js_fatal(J, "Eine Zeichenkette des Skripts ist zu lang");
        return J->empty;
    }
    js_string *s = js_alloc(J, sizeof(js_string) + len + 1);
    if (!s)
        return J->empty;
    memset(s, 0, sizeof(*s));
    s->len = (uint32_t)len;
    s->index = JS_NO_INDEX;
    s->data[len] = '\0';
    s->gc.next = J->all;
    J->all = &s->gc;
    return s;
}

static void string_measure(js_string *s)
{
    uint32_t chars = 0;
    bool ascii = true;
    for (uint32_t i = 0; i < s->len; i++) {
        unsigned char c = (unsigned char)s->data[i];
        if (c >= 0x80)
            ascii = false;
        if ((c & 0xC0) != 0x80)
            chars++;
    }
    s->ascii = ascii;
    s->chars = chars;
}

js_string *js_string_new(js_state *J, const char *text, size_t len)
{
    if (len == 0 && J->empty)
        return J->empty;
    js_string *s = string_raw(J, len);
    if (s == J->empty && J->empty)
        return s;
    if (len)
        memcpy(s->data, text, len);
    string_measure(s);
    return s;
}

js_string *js_string_concat(js_state *J, const js_string *a, const js_string *b)
{
    if (a->len == 0)
        return (js_string *)b;
    if (b->len == 0)
        return (js_string *)a;
    js_string *s = string_raw(J, (size_t)a->len + b->len);
    if (s == J->empty)
        return s;
    memcpy(s->data, a->data, a->len);
    memcpy(s->data + a->len, b->data, b->len);
    s->ascii = a->ascii && b->ascii;
    s->chars = a->chars + b->chars;
    return s;
}

uint32_t js_string_offset(const js_string *s, uint32_t index)
{
    if (s->ascii || index == 0)
        return index < s->len ? index : s->len;
    uint32_t at = 0;
    while (at < s->len && index > 0) {
        at++;
        while (at < s->len && ((unsigned char)s->data[at] & 0xC0) == 0x80)
            at++;
        index--;
    }
    return at;
}

bool js_string_equal(const js_string *a, const js_string *b)
{
    return a == b || (a->len == b->len && memcmp(a->data, b->data, a->len) == 0);
}

int js_string_compare(const js_string *a, const js_string *b)
{
    uint32_t n = a->len < b->len ? a->len : b->len;
    int c = memcmp(a->data, b->data, n);
    return c ? c : a->len < b->len ? -1 : a->len > b->len ? 1 : 0;
}

/* Die Namenstabelle: jede Zeichenkette, die als Name dient, gibt es nur einmal. */
static uint32_t index_of_name(const char *text, size_t len)
{
    if (len == 0 || len > 10 || (len > 1 && text[0] == '0'))
        return JS_NO_INDEX;
    uint64_t value = 0;
    for (size_t i = 0; i < len; i++) {
        if (text[i] < '0' || text[i] > '9')
            return JS_NO_INDEX;
        value = value * 10 + (uint64_t)(text[i] - '0');
    }
    return value < 0xFFFFFFFFu ? (uint32_t)value : JS_NO_INDEX;
}

static bool names_grow(js_state *J)
{
    uint32_t cap = J->name_cap ? J->name_cap * 2 : 1024;
    js_string **table = kzalloc((size_t)cap * sizeof(js_string *));
    if (!table)
        return false;
    for (uint32_t i = 0; i < J->name_cap; i++) {
        js_string *s = J->names[i];
        if (!s)
            continue;
        uint32_t at = s->hash & (cap - 1);
        while (table[at])
            at = (at + 1) & (cap - 1);
        table[at] = s;
    }
    kfree(J->names);
    J->names = table;
    J->name_cap = cap;
    return true;
}

static js_string *names_find(js_state *J, const char *text, size_t len, uint32_t hash,
                             uint32_t *slot)
{
    uint32_t at = hash & (J->name_cap - 1);
    while (J->names[at]) {
        js_string *s = J->names[at];
        if (s->hash == hash && s->len == len && memcmp(s->data, text, len) == 0)
            return s;
        at = (at + 1) & (J->name_cap - 1);
    }
    *slot = at;
    return NULL;
}

static js_string *names_add(js_state *J, js_string *s, uint32_t slot)
{
    s->interned = 1;
    s->index = index_of_name(s->data, s->len);
    J->names[slot] = s;
    J->name_count++;
    if (J->name_count * 2 > J->name_cap && !names_grow(J))
        js_fatal(J, "Nicht genug Speicher für das Skript");
    return s;
}

js_string *js_intern(js_state *J, const char *text, size_t len)
{
    uint32_t hash = hash_bytes(text, len), slot = 0;
    js_string *found = names_find(J, text, len, hash, &slot);
    if (found)
        return found;
    js_string *s = string_raw(J, len);
    if (!s || (s == J->empty && len))
        return s;                       /* Speichermangel: die Ausführung endet ohnehin */
    if (len)
        memcpy(s->data, text, len);
    string_measure(s);
    s->hash = hash;
    return names_add(J, s, slot);
}

js_string *js_intern_string(js_state *J, js_string *s)
{
    if (s->interned)
        return s;
    uint32_t hash = hash_bytes(s->data, s->len), slot = 0;
    js_string *found = names_find(J, s->data, s->len, hash, &slot);
    if (found)
        return found;
    s->hash = hash;
    return names_add(J, s, slot);
}

js_string *js_intern_cstr(js_state *J, const char *text)
{
    return js_intern(J, text, strlen(text));
}

js_string *js_index_name(js_state *J, uint32_t index)
{
    char text[12];
    int n = 0;
    do {
        text[n++] = (char)('0' + index % 10);
        index /= 10;
    } while (index);
    for (int i = 0; i < n / 2; i++) {
        char t = text[i];
        text[i] = text[n - 1 - i];
        text[n - 1 - i] = t;
    }
    return js_intern(J, text, (size_t)n);
}

/* --- Objekte --------------------------------------------------------------------------------- */

js_object *js_object_new(js_state *J, int cls, js_object *proto)
{
    js_object *o = js_alloc(J, sizeof(js_object));
    if (!o)
        return J->dummy;
    memset(o, 0, sizeof(*o));
    o->gc.kind = 1;
    o->cls = (uint8_t)cls;
    o->proto = proto;
    o->gc.next = J->all;
    J->all = &o->gc;
    return o;
}

js_object *js_array_new(js_state *J, uint32_t reserve)
{
    js_object *a = js_object_new(J, JS_C_ARRAY, J->array_proto);
    if (a == J->dummy)
        return a;
    if (reserve > 0 && reserve <= JS_ARRAY_MAX) {
        a->u.arr.v = js_alloc(J, (size_t)reserve * sizeof(js_value));
        if (a->u.arr.v)
            a->u.arr.cap = reserve;
    }
    return a;
}

bool js_array_resize(js_state *J, js_object *a, uint32_t len)
{
    if (a->cls != JS_C_ARRAY)
        return false;                   /* der Ersatz bei Speichermangel ist kein Feld */
    if (len > JS_ARRAY_MAX)
        return js_throw(J, JS_E_RANGE, "Invalid array length");
    if (len > a->u.arr.cap) {
        uint32_t cap = a->u.arr.cap ? a->u.arr.cap * 2 : 8;
        if (cap < len)
            cap = len;
        js_value *v = js_alloc(J, (size_t)cap * sizeof(js_value));
        if (!v)
            return false;
        if (a->u.arr.len)
            memcpy(v, a->u.arr.v, (size_t)a->u.arr.len * sizeof(js_value));
        js_release(J, a->u.arr.v, (size_t)a->u.arr.cap * sizeof(js_value));
        a->u.arr.v = v;
        a->u.arr.cap = cap;
    }
    for (uint32_t i = a->u.arr.len; i < len; i++)
        a->u.arr.v[i] = js_undef();
    a->u.arr.len = len;
    return true;
}

bool js_array_push(js_state *J, js_object *a, js_value v)
{
    if (a->cls != JS_C_ARRAY)
        return false;
    uint32_t at = a->u.arr.len;
    if (!js_array_resize(J, a, at + 1))
        return false;
    a->u.arr.v[at] = v;
    return true;
}

static void slots_build(js_state *J, js_object *o)
{
    uint32_t cap = 16;
    while (cap < o->cap * 2)
        cap *= 2;
    uint32_t *slots = js_alloc(J, (size_t)cap * sizeof(uint32_t));
    js_release(J, o->slots, (size_t)o->slot_cap * sizeof(uint32_t));
    o->slots = slots;
    o->slot_cap = slots ? cap : 0;
    if (!slots)
        return;
    memset(slots, 0, (size_t)cap * sizeof(uint32_t));
    for (uint32_t i = 0; i < o->count; i++) {
        if (!o->props[i].key)
            continue;
        uint32_t at = o->props[i].key->hash & (cap - 1);
        while (slots[at])
            at = (at + 1) & (cap - 1);
        slots[at] = i + 1;
    }
}

js_prop *js_own(const js_object *o, const js_string *key)
{
    if (o->slots) {
        uint32_t at = key->hash & (o->slot_cap - 1);
        while (o->slots[at]) {
            js_prop *p = &o->props[o->slots[at] - 1];
            if (p->key == key)
                return p;
            at = (at + 1) & (o->slot_cap - 1);
        }
        return NULL;
    }
    for (uint32_t i = 0; i < o->count; i++) {
        if (o->props[i].key == key)
            return &o->props[i];
    }
    return NULL;
}

js_prop *js_lookup(const js_object *o, const js_string *key, js_object **holder)
{
    for (int depth = 0; o && depth < 10000; depth++, o = o->proto) {
        js_prop *p = js_own(o, key);
        if (p) {
            if (holder)
                *holder = (js_object *)o;
            return p;
        }
    }
    return NULL;
}

js_prop *js_define(js_state *J, js_object *o, js_string *key, js_value v, int flags)
{
    js_prop *p = js_own(o, key);
    if (!p) {
        if (o->count == o->cap) {
            /* erst Gelöschtes zusammenschieben, dann wachsen */
            uint32_t live = 0;
            for (uint32_t i = 0; i < o->count; i++) {
                if (o->props[i].key)
                    o->props[live++] = o->props[i];
            }
            bool compacted = live != o->count;
            o->count = live;
            if (live * 4 >= o->cap * 3) {
                uint32_t cap = o->cap ? o->cap * 2 : 4;
                js_prop *props = js_alloc(J, (size_t)cap * sizeof(js_prop));
                if (!props) {
                    if (compacted && o->slots)
                        slots_build(J, o);
                    return NULL;
                }
                if (live)
                    memcpy(props, o->props, (size_t)live * sizeof(js_prop));
                js_release(J, o->props, (size_t)o->cap * sizeof(js_prop));
                o->props = props;
                o->cap = cap;
                compacted = true;
            }
            if (o->slots ? compacted : o->cap > 8)
                slots_build(J, o);
        }
        p = &o->props[o->count++];
        p->key = key;
        p->set = NULL;
        if (o->slots) {
            uint32_t at = key->hash & (o->slot_cap - 1);
            while (o->slots[at])
                at = (at + 1) & (o->slot_cap - 1);
            o->slots[at] = o->count;
        }
    }
    p->v = v;
    p->flags = (uint8_t)flags;
    return p;
}

bool js_delete_own(js_state *J, js_object *o, js_string *key)
{
    (void)J;
    js_prop *p = js_own(o, key);
    if (!p)
        return true;
    if (!(p->flags & JS_P_CONF))
        return false;
    p->key = NULL;                      /* der Platz bleibt, bis das Objekt wieder wächst */
    p->v = js_undef();
    p->set = NULL;
    return true;
}

js_object *js_native_new(js_state *J, js_native_fn fn, const char *name, int nargs, int magic)
{
    js_object *o = js_object_new(J, JS_C_NATIVE, J->function_proto);
    if (o == J->dummy)
        return o;
    o->u.n.fn = fn;
    o->u.n.nargs = (uint8_t)nargs;
    o->u.n.magic = magic;
    if (name && name[0])
        js_define(J, o, J->k[K_name], js_str(js_intern_cstr(J, name)), JS_P_CONF);
    return o;
}

void js_def_native(js_state *J, js_object *o, const char *name, js_native_fn fn, int nargs,
                   int magic)
{
    js_object *f = js_native_new(J, fn, name, nargs, magic);
    js_define(J, o, js_intern_cstr(J, name), js_obj(f), JS_P_WRITE | JS_P_CONF);
}

void js_def_value(js_state *J, js_object *o, const char *name, js_value v, int flags)
{
    js_define(J, o, js_intern_cstr(J, name), v, flags);
}

void js_def_accessor(js_state *J, js_object *o, const char *name, js_native_fn get,
                     js_native_fn set, int magic)
{
    js_object *g = get ? js_native_new(J, get, name, 0, magic) : NULL;
    js_object *s = set ? js_native_new(J, set, name, 1, magic) : NULL;
    js_prop *p = js_define(J, o, js_intern_cstr(J, name), g ? js_obj(g) : js_undef(),
                           JS_P_ACCESSOR | JS_P_CONF);
    if (p)
        p->set = s;
}

/* --- Speicherbereinigung --------------------------------------------------------------------- */

static void mark_object(js_state *J, js_object *o)
{
    if (!o || o->gc.mark)
        return;
    o->gc.mark = 1;
    if (J->gray_count == J->gray_cap) {
        size_t cap = J->gray_cap ? J->gray_cap * 2 : 1024;
        js_gc **list = kmalloc(cap * sizeof(js_gc *));
        if (!list) {
            js_fatal(J, "Nicht genug Speicher für das Skript");
            return;
        }
        if (J->gray_count)
            memcpy(list, J->gray, J->gray_count * sizeof(js_gc *));
        kfree(J->gray);
        J->gray = list;
        J->gray_cap = cap;
    }
    J->gray[J->gray_count++] = &o->gc;
}

static void mark_value(js_state *J, const js_value *v)
{
    if (v->t == JS_T_STR)
        v->u.s->gc.mark = 1;
    else if (v->t == JS_T_OBJ)
        mark_object(J, v->u.o);
}

static void mark_children(js_state *J, js_object *o)
{
    mark_object(J, o->proto);
    for (uint32_t i = 0; i < o->count; i++) {
        js_prop *p = &o->props[i];
        if (!p->key)
            continue;
        p->key->gc.mark = 1;
        mark_value(J, &p->v);
        mark_object(J, p->set);
    }
    switch (o->cls) {
    case JS_C_ARRAY:
        for (uint32_t i = 0; i < o->u.arr.len; i++)
            mark_value(J, &o->u.arr.v[i]);
        break;
    case JS_C_FUNCTION:
        mark_object(J, o->u.f.scope);
        mark_object(J, o->u.f.home);
        mark_value(J, &o->u.f.self);
        break;
    case JS_C_NATIVE:
        mark_object(J, o->u.n.data);
        break;
    case JS_C_BOOLEAN: case JS_C_NUMBER: case JS_C_STRING: case JS_C_DATE:
        mark_value(J, &o->u.prim);
        break;
    case JS_C_REGEXP:
        if (o->u.re.source)
            o->u.re.source->gc.mark = 1;
        break;
    default:
        break;
    }
}

static void free_object(js_state *J, js_object *o)
{
    js_release(J, o->props, (size_t)o->cap * sizeof(js_prop));
    js_release(J, o->slots, (size_t)o->slot_cap * sizeof(uint32_t));
    if (o->cls == JS_C_ARRAY)
        js_release(J, o->u.arr.v, (size_t)o->u.arr.cap * sizeof(js_value));
    else if (o->cls == JS_C_REGEXP)
        js_regex_free(J, o);
    else if (o->cls == JS_C_HOST && o->u.host.cls && o->u.host.cls->finalize)
        o->u.host.cls->finalize(J, o);
    js_release(J, o, sizeof(js_object));
}

static void free_all(js_state *J, bool everything)
{
    js_gc **link = &J->all;
    bool names_changed = false;
    while (*link) {
        js_gc *g = *link;
        if (!everything && (g->mark || g->pinned)) {
            g->mark = 0;
            link = &g->next;
            continue;
        }
        *link = g->next;
        if (g->kind == 0) {
            js_string *s = (js_string *)g;
            names_changed |= s->interned;
            js_release(J, s, sizeof(js_string) + s->len + 1);
        } else {
            free_object(J, (js_object *)g);
        }
    }
    if (names_changed && !everything) {
        /* Die Namenstabelle neu aufbauen: nur die verbliebenen Namen */
        memset(J->names, 0, (size_t)J->name_cap * sizeof(js_string *));
        J->name_count = 0;
        for (js_gc *g = J->all; g; g = g->next) {
            js_string *s = (js_string *)g;
            if (g->kind != 0 || !s->interned)
                continue;
            uint32_t at = s->hash & (J->name_cap - 1);
            while (J->names[at])
                at = (at + 1) & (J->name_cap - 1);
            J->names[at] = s;
            J->name_count++;
        }
    }
}

void js_gc_run(js_state *J)
{
    if (J->fatal)
        return;
    J->gray_count = 0;
    for (int i = 0; i < J->top; i++)
        mark_value(J, &J->stack[i]);
    mark_value(J, &J->ret);
    mark_value(J, &J->exception);
    js_object *roots[] = { J->global, J->registry, J->dummy, J->object_proto, J->function_proto,
                           J->array_proto, J->string_proto, J->number_proto, J->boolean_proto,
                           J->error_proto, J->date_proto, J->regexp_proto };
    for (unsigned i = 0; i < sizeof(roots) / sizeof(roots[0]); i++)
        mark_object(J, roots[i]);
    for (int i = 0; i < 8; i++)
        mark_object(J, J->error_protos[i]);
    if (J->label)
        J->label->gc.mark = 1;
    J->empty->gc.mark = 1;
    while (J->gray_count > 0 && !J->fatal)
        mark_children(J, (js_object *)J->gray[--J->gray_count]);
    if (J->fatal) {
        /* Markieren abgebrochen: nichts wegräumen, nur die Marken löschen */
        for (js_gc *g = J->all; g; g = g->next)
            g->mark = 0;
        return;
    }
    free_all(J, false);
    J->gc_runs++;
    /*
     * Die nächste Runde, wenn sich der Speicher verdoppelt hat (mindestens
     * 8 MiB mehr): So bleibt der Aufwand im Verhältnis zur Arbeit des Skripts.
     * Kurz vor der Obergrenze wird öfter aufgeräumt.
     */
    size_t step = J->heap > 8u * 1024 * 1024 ? J->heap : 8u * 1024 * 1024;
    size_t late = J->heap_max - J->heap_max / 8;
    J->gc_at = J->heap + step;
    if (J->gc_at > late)
        J->gc_at = late > J->heap + 1024 * 1024 ? late : J->heap + 1024 * 1024;
}

/* --- Stapel ---------------------------------------------------------------------------------- */

bool js_push(js_state *J, js_value v)
{
    if (J->top >= JS_STACK_SIZE) {
        js_fatal(J, "Das Skript ist zu tief verschachtelt");
        return false;
    }
    if (J->top < 0)
        J->top = 0;
    J->stack[J->top++] = v;
    return true;
}

bool js_push_lstr(js_state *J, const char *text, size_t len)
{
    return js_push(J, js_str(js_string_new(J, text, len)));
}

bool js_push_cstr(js_state *J, const char *text)
{
    return js_push_lstr(J, text, strlen(text));
}

/* --- Ausnahmen ------------------------------------------------------------------------------- */

static size_t format(char *out, size_t size, const char *fmt, va_list ap)
{
    size_t n = 0;
    for (; *fmt && n + 1 < size; fmt++) {
        if (*fmt != '%') {
            out[n++] = *fmt;
            continue;
        }
        fmt++;
        if (*fmt == 's') {
            const char *s = va_arg(ap, const char *);
            for (int k = 0; s && s[k] && k < 120 && n + 1 < size; k++)
                out[n++] = s[k];
        } else if (*fmt == 'd') {
            int v = va_arg(ap, int);
            char digits[12];
            int k = 0;
            unsigned u = v < 0 ? 0u - (unsigned)v : (unsigned)v;
            do {
                digits[k++] = (char)('0' + u % 10);
                u /= 10;
            } while (u);
            if (v < 0 && n + 1 < size)
                out[n++] = '-';
            while (k > 0 && n + 1 < size)
                out[n++] = digits[--k];
        } else if (*fmt == 'c') {
            out[n++] = (char)va_arg(ap, int);
        } else if (*fmt) {
            out[n++] = *fmt;
        } else {
            break;
        }
    }
    out[n] = '\0';
    return n;
}

bool js_fatal(js_state *J, const char *reason)
{
    if (!J->fatal) {
        J->fatal = true;
        J->fatal_reason = reason;
    }
    return false;
}

bool js_throw_value(js_state *J, js_value v)
{
    if (!J->fatal) {
        J->exception = v;
        J->throwing = true;
    }
    return false;
}

bool js_throw(js_state *J, int kind, const char *fmt, ...)
{
    if (J->fatal || J->throwing)
        return false;
    char text[300];
    va_list ap;
    va_start(ap, fmt);
    size_t n = format(text, sizeof(text), fmt, ap);
    va_end(ap);
    js_object *e = js_object_new(J, JS_C_ERROR, J->error_protos[kind]);
    js_define(J, e, J->k[K_message], js_str(js_string_new(J, text, n)), JS_P_WRITE | JS_P_CONF);
    js_define(J, e, J->k[K_line], js_num(J->line), JS_P_WRITE | JS_P_CONF);
    return js_throw_value(J, js_obj(e));
}

/* --- Umwandlungen ---------------------------------------------------------------------------- */

bool js_truthy(const js_value *v)
{
    switch (v->t) {
    case JS_T_UNDEF: case JS_T_NULL: return false;
    case JS_T_BOOL: return v->u.b;
    case JS_T_NUM: return v->u.n != 0 && !js_isnan(v->u.n);
    case JS_T_STR: return v->u.s->len > 0;
    default: return true;
    }
}

const char *js_typeof(const js_value *v)
{
    switch (v->t) {
    case JS_T_UNDEF: return "undefined";
    case JS_T_NULL: return "object";
    case JS_T_BOOL: return "boolean";
    case JS_T_NUM: return "number";
    case JS_T_STR: return "string";
    default: return js_is_callable(v) ? "function" : "object";
    }
}

bool js_is_callable(const js_value *v)
{
    return v->t == JS_T_OBJ && (v->u.o->cls == JS_C_FUNCTION || v->u.o->cls == JS_C_NATIVE);
}

bool js_is_array(const js_value *v)
{
    return v->t == JS_T_OBJ && v->u.o->cls == JS_C_ARRAY;
}

static bool is_space(unsigned char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

double js_string_to_number(const js_string *s)
{
    const char *p = s->data, *end = s->data + s->len;
    /* Leerraum, auch das geschützte Leerzeichen U+00A0 und das BOM */
    for (;;) {
        if (p < end && is_space((unsigned char)*p))
            p++;
        else if (end - p >= 2 && (unsigned char)p[0] == 0xC2 && (unsigned char)p[1] == 0xA0)
            p += 2;
        else if (end - p >= 3 && (unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB &&
                 (unsigned char)p[2] == 0xBF)
            p += 3;
        else
            break;
    }
    while (end > p && is_space((unsigned char)end[-1]))
        end--;
    if (p == end)
        return 0;
    if (end - p > 2 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X' || p[1] == 'o' || p[1] == 'O' ||
                                        p[1] == 'b' || p[1] == 'B')) {
        int base = (p[1] | 32) == 'x' ? 16 : (p[1] | 32) == 'o' ? 8 : 2;
        double value = 0;
        for (p += 2; p < end; p++) {
            int d = hex_value(*p);
            if (d < 0 || d >= base)
                return js_nan();
            value = value * base + d;
        }
        return value;
    }
    bool negative = false;
    const char *q = p;
    if (*q == '+' || *q == '-')
        negative = *q++ == '-';
    if ((size_t)(end - q) == 8 && memcmp(q, "Infinity", 8) == 0)
        return negative ? -js_infinity() : js_infinity();
    size_t used = 0;
    double value = js_text_to_number(p, (size_t)(end - p), &used);
    return used == (size_t)(end - p) && used > 0 ? value : js_nan();
}

js_string *js_number_string(js_state *J, double n)
{
    char text[48];
    int len = js_number_to_text(n, text);
    return js_string_new(J, text, (size_t)len);
}

bool js_to_primitive(js_state *J, int index, int hint)
{
    if (J->stack[index].t != JS_T_OBJ)
        return true;
    js_object *o = J->stack[index].u.o;
    if (hint == 0)
        hint = o->cls == JS_C_DATE ? 2 : 1;
    for (int round = 0; round < 2; round++) {
        js_string *name = J->k[(round == 0) == (hint == 2) ? K_toString : K_valueOf];
        if (!js_get(J, index, name))
            return false;
        if (js_is_callable(js_top(J, 0))) {
            js_push(J, J->stack[index]);
            if (!js_call(J, 0))
                return false;
            if (js_top(J, 0)->t != JS_T_OBJ) {
                J->stack[index] = *js_top(J, 0);
                js_pop(J, 1);
                return true;
            }
        }
        js_pop(J, 1);
    }
    return js_throw(J, JS_E_TYPE, "Cannot convert object to primitive value");
}

bool js_to_number(js_state *J, int index, double *out)
{
    for (;;) {
        const js_value *v = &J->stack[index];
        switch (v->t) {
        case JS_T_UNDEF: *out = js_nan(); return true;
        case JS_T_NULL: *out = 0; return true;
        case JS_T_BOOL: *out = v->u.b ? 1 : 0; return true;
        case JS_T_NUM: *out = v->u.n; return true;
        case JS_T_STR: *out = js_string_to_number(v->u.s); return true;
        default:
            if (!js_to_primitive(J, index, 1))
                return false;
        }
    }
}

bool js_to_int32(js_state *J, int index, int32_t *out)
{
    double n;
    if (J->stack[index].t == JS_T_NUM) {
        n = J->stack[index].u.n;
    } else if (!js_to_number(J, index, &n)) {
        return false;
    }
    *out = js_double_to_int32(n);
    return true;
}

bool js_to_string(js_state *J, int index)
{
    for (;;) {
        js_value *v = &J->stack[index];
        switch (v->t) {
        case JS_T_STR: return true;
        case JS_T_UNDEF: *v = js_str(J->k[K_undefined]); return true;
        case JS_T_NULL: *v = js_str(J->k[K_null]); return true;
        case JS_T_BOOL: *v = js_str(J->k[v->u.b ? K_true : K_false]); return true;
        case JS_T_NUM: *v = js_str(js_number_string(J, v->u.n)); return true;
        default:
            if (!js_to_primitive(J, index, 2))
                return false;
        }
    }
}

bool js_to_object(js_state *J, int index)
{
    js_value *v = &J->stack[index];
    js_object *o;
    switch (v->t) {
    case JS_T_OBJ:
        return true;
    case JS_T_UNDEF: case JS_T_NULL:
        return js_throw(J, JS_E_TYPE, "Cannot convert %s to object",
                        v->t == JS_T_NULL ? "null" : "undefined");
    case JS_T_BOOL:
        o = js_object_new(J, JS_C_BOOLEAN, J->boolean_proto);
        break;
    case JS_T_NUM:
        o = js_object_new(J, JS_C_NUMBER, J->number_proto);
        break;
    default:
        o = js_object_new(J, JS_C_STRING, J->string_proto);
        break;
    }
    if (o != J->dummy)
        o->u.prim = *v;
    *v = js_obj(o);
    return true;
}

bool js_to_key(js_state *J, int index, js_string **key)
{
    js_value *v = &J->stack[index];
    if (v->t == JS_T_NUM) {
        double n = v->u.n;
        if (n >= 0 && n < 4294967295.0 && n == (double)(uint32_t)n) {
            *key = js_index_name(J, (uint32_t)n);
            return true;
        }
    }
    if (!js_to_string(J, index))
        return false;
    *key = js_intern_string(J, J->stack[index].u.s);
    J->stack[index] = js_str(*key);
    return true;
}

bool js_strict_equal(const js_value *a, const js_value *b)
{
    if (a->t != b->t)
        return false;
    switch (a->t) {
    case JS_T_UNDEF: case JS_T_NULL: return true;
    case JS_T_BOOL: return a->u.b == b->u.b;
    case JS_T_NUM: return a->u.n == b->u.n;
    case JS_T_STR: return js_string_equal(a->u.s, b->u.s);
    default: return a->u.o == b->u.o;
    }
}

bool js_loose_equal(js_state *J, int a, int b, bool *out)
{
    for (int round = 0; round < 6; round++) {
        js_value *x = &J->stack[a], *y = &J->stack[b];
        if (x->t == y->t) {
            *out = js_strict_equal(x, y);
            return true;
        }
        bool x_nullish = x->t <= JS_T_NULL, y_nullish = y->t <= JS_T_NULL;
        if (x_nullish || y_nullish) {
            *out = x_nullish && y_nullish;
            return true;
        }
        if (x->t == JS_T_OBJ) {
            if (!js_to_primitive(J, a, 0))
                return false;
            continue;
        }
        if (y->t == JS_T_OBJ) {
            if (!js_to_primitive(J, b, 0))
                return false;
            continue;
        }
        /* Zahl, Zeichenkette, Wahrheitswert gemischt: als Zahlen vergleichen */
        double p, q;
        if (!js_to_number(J, a, &p) || !js_to_number(J, b, &q))
            return false;
        *out = p == q;
        return true;
    }
    *out = false;
    return true;
}

/* --- Anlegen und Abräumen -------------------------------------------------------------------- */

static const char *const key_names[K_COUNT] = {
    "length", "prototype", "constructor", "name", "message", "toString", "valueOf", "arguments",
    "undefined", "null", "true", "false", "object", "function", "number", "string", "boolean",
    "", "index", "input", "lastIndex", "stack", "get", "set", "value", "writable",
    "enumerable", "configurable", "callee", "join", "toJSON", "then", "done", "next", "this",
    "source", "global", "ignoreCase", "multiline", "line", "cause",
};

js_state *js_create(const js_host_t *host)
{
    js_state *J = kzalloc(sizeof(js_state));
    if (!J)
        return NULL;
    if (host)
        J->host = *host;
    J->heap_max = J->host.heap_max ? J->host.heap_max : 32u * 1024 * 1024;
    J->gc_at = 8u * 1024 * 1024;
    J->rng = 0x9E3779B9u;
    js_value *stack = kzalloc((size_t)(JS_STACK_SIZE + 2 * STACK_GUARD) * sizeof(js_value));
    J->names = kzalloc(1024 * sizeof(js_string *));
    J->name_cap = 1024;
    if (!stack || !J->names) {
        kfree(stack);
        kfree(J->names);
        kfree(J);
        return NULL;
    }
    J->stack = stack + STACK_GUARD;

    /* "" zuerst: Es dient auch als Ersatz, wenn der Speicher ausgeht. */
    J->empty = js_intern(J, "", 0);
    for (int i = 0; i < K_COUNT; i++) {
        J->k[i] = js_intern_cstr(J, key_names[i]);
        if (J->k[i])
            J->k[i]->gc.pinned = 1;
    }
    if (J->empty)
        J->empty->gc.pinned = 1;
    J->dummy = js_object_new(J, JS_C_OBJECT, NULL);
    if (!J->empty || !J->dummy || J->fatal) {
        js_destroy(J);
        return NULL;
    }
    J->object_proto = js_object_new(J, JS_C_OBJECT, NULL);
    J->function_proto = js_object_new(J, JS_C_OBJECT, J->object_proto);
    J->global = js_object_new(J, JS_C_OBJECT, J->object_proto);
    J->registry = js_object_new(J, JS_C_OBJECT, NULL);
    js_lib_init(J);
    js_regex_init(J);
    js_typed_init(J);
    if (J->fatal) {
        js_destroy(J);
        return NULL;
    }
    return J;
}

void js_destroy(js_state *J)
{
    if (!J)
        return;
    free_all(J, true);
    while (J->arena) {
        js_arena *next = J->arena->next;
        kfree(J->arena);
        J->arena = next;
    }
    kfree(J->gray);
    kfree(J->names);
    if (J->stack)
        kfree(J->stack - STACK_GUARD);
    kfree(J);
}

const char *js_last_error(const js_state *J)
{
    return J->error;
}

bool js_dead(const js_state *J)
{
    return J->fatal;
}
