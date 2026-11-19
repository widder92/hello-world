/* JavaScript: Symbole und das Durchlaufen von Werten (Iteratoren) - siehe js.h */
#include "js_impl.h"
#include "string.h"

#define ARGI(i) (J->bot + 2 + (i))
#define THISI (J->bot + 1)
#define MAGIC (js_callee(J)->u.n.magic)

/* --- Symbol ---------------------------------------------------------------------------------------- */

bool js_push_symbol_text(js_state *J, const js_string *sym)
{
    js_string *text = js_string_concat(J, js_string_new(J, "Symbol(", 7), sym);
    js_push(J, js_str(text));           /* bleibt für die Speicherbereinigung sichtbar */
    *js_top(J, 0) = js_str(js_string_concat(J, text, js_string_new(J, ")", 1)));
    return true;
}

/* Das Symbol, auf dem eine Methode von Symbol.prototype gerufen wurde */
static js_string *this_symbol(js_state *J)
{
    js_value *v = js_this(J);
    if (v->t == JS_T_SYM)
        return v->u.s;
    if (v->t == JS_T_OBJ && v->u.o->cls == JS_C_SYMBOL && v->u.o->u.prim.t == JS_T_SYM)
        return v->u.o->u.prim.u.s;
    js_throw(J, JS_E_TYPE, "Value is not a symbol");
    return NULL;
}

static bool symbol_ctor(js_state *J, int argc)
{
    if (J->constructing)
        return js_throw(J, JS_E_TYPE, "Symbol is not a constructor");
    js_string *text = J->empty;
    if (argc > 0 && js_arg(J, 0)->t != JS_T_UNDEF) {
        if (!js_to_string(J, ARGI(0)))
            return false;
        text = js_arg(J, 0)->u.s;
    }
    return js_push(J, js_sym(js_symbol_new(J, text->data, text->len, JS_SYM_PUBLIC)));
}

/* Symbol.for(name) (0): dasselbe Symbol für denselben Namen; Symbol.keyFor(symbol) (1) */
static bool symbol_registry(js_state *J, int argc)
{
    (void)argc;
    js_string *name = js_intern_cstr(J, "symbols");
    js_prop *p = js_own(J->registry, name);
    js_object *table = p && p->v.t == JS_T_OBJ ? p->v.u.o : NULL;
    if (!table) {
        table = js_object_new(J, JS_C_OBJECT, NULL);
        js_define(J, J->registry, name, js_obj(table), JS_P_ALL);
    }
    if (MAGIC == 1) {
        if (js_arg(J, 0)->t != JS_T_SYM)
            return js_throw(J, JS_E_TYPE, "Value is not a symbol");
        for (uint32_t i = 0; i < table->count; i++) {
            if (table->props[i].key && table->props[i].v.u.s == js_arg(J, 0)->u.s)
                return js_push(J, js_str(table->props[i].key));
        }
        return js_push(J, js_undef());
    }
    js_string *key;
    if (!js_to_string(J, ARGI(0)))
        return false;
    key = js_intern_string(J, js_arg(J, 0)->u.s);
    p = js_own(table, key);
    if (p)
        return js_push(J, p->v);
    js_value sym = js_sym(js_symbol_new(J, key->data, key->len, JS_SYM_PUBLIC));
    js_define(J, table, key, sym, JS_P_ALL);
    return js_push(J, sym);
}

/* toString (0), valueOf und [Symbol.toPrimitive] (1), description (2) */
static bool symbol_proto_fn(js_state *J, int argc)
{
    (void)argc;
    js_string *sym = this_symbol(J);
    if (!sym)
        return false;
    if (MAGIC == 0)
        return js_push_symbol_text(J, sym);
    if (MAGIC == 1)
        return js_push(J, js_sym(sym));
    return sym->len ? js_push_lstr(J, sym->data, sym->len) : js_push(J, js_undef());
}

/* --- Ergebnisse von next() --------------------------------------------------------------------------- */

bool js_push_iter_result(js_state *J, js_value value, bool done)
{
    js_push(J, value);                  /* sichtbar halten, bis es im Ergebnis steht */
    js_object *r = js_object_new(J, JS_C_OBJECT, J->object_proto);
    js_define(J, r, J->k[K_value], value, JS_P_ALL);
    js_define(J, r, J->k[K_done], js_bool(done), JS_P_ALL);
    *js_top(J, 0) = js_obj(r);
    return !J->fatal;
}

/* --- Die eingebauten Iteratoren ---------------------------------------------------------------------- */

static js_value hidden(const js_object *o, const js_string *key)
{
    js_prop *p = js_own(o, key);
    return p ? p->v : js_undef();
}

/* Legt einen Iterator über target an und auf den Stapel. */
static void iterator_new(js_state *J, js_object *proto, js_value target, int kind)
{
    js_object *it = js_object_new(J, JS_C_OBJECT, proto);
    js_push(J, js_obj(it));
    js_define(J, it, J->hid[H_target], target, JS_P_WRITE);
    js_define(J, it, J->hid[H_index], js_num(0), JS_P_WRITE);
    js_define(J, it, J->hid[H_kind], js_num(kind), JS_P_WRITE);
}

/* [Symbol.iterator]() jedes Iterators: er selbst */
static bool iterator_self(js_state *J, int argc)
{
    (void)argc;
    return js_push(J, *js_this(J));
}

/* Array.prototype.keys (0), values (1), entries (2) - für alles mit length und Nummern */
static bool array_iterator(js_state *J, int argc)
{
    (void)argc;
    if (!js_to_object(J, THISI))
        return false;
    iterator_new(J, J->array_iter_proto, *js_this(J), MAGIC);
    return !J->fatal;
}

static bool array_iter_next(js_state *J, int argc)
{
    (void)argc;
    js_value *self = js_this(J);
    if (self->t != JS_T_OBJ || !js_own(self->u.o, J->hid[H_kind]))
        return js_throw(J, JS_E_TYPE, "next() called on a value that is not an iterator");
    js_object *it = self->u.o;
    js_value target = hidden(it, J->hid[H_target]);
    if (target.t != JS_T_OBJ)
        return js_push_iter_result(J, js_undef(), true);
    uint32_t at = (uint32_t)hidden(it, J->hid[H_index]).u.n;
    int kind = (int)hidden(it, J->hid[H_kind]).u.n;
    double length;
    js_push(J, target);
    int slot = J->top - 1;
    if (target.u.o->cls == JS_C_ARRAY) {
        length = target.u.o->u.arr.len;
    } else {
        if (!js_get(J, slot, J->k[K_length]) || !js_to_number(J, J->top - 1, &length))
            return false;
        js_pop(J, 1);
    }
    if (!(at < length)) {
        js_define(J, it, J->hid[H_target], js_undef(), JS_P_WRITE);     /* erschöpft bleibt erschöpft */
        return js_push_iter_result(J, js_undef(), true);
    }
    js_define(J, it, J->hid[H_index], js_num((double)at + 1), JS_P_WRITE);
    if (kind == 0)
        return js_push_iter_result(J, js_num(at), false);
    if (!js_get_index(J, slot, at))
        return false;
    if (kind == 2) {
        js_object *pair = js_array_new(J, 2);
        js_value v = *js_top(J, 0);
        *js_top(J, 0) = js_obj(pair);
        js_array_push(J, pair, js_num(at));
        js_array_push(J, pair, v);
    }
    return js_push_iter_result(J, *js_top(J, 0), false);
}

/* String.prototype[Symbol.iterator]: Zeichen für Zeichen (ganze Codepunkte) */
static bool string_iterator(js_state *J, int argc)
{
    (void)argc;
    js_value *self = js_this(J);
    if (self->t == JS_T_OBJ && self->u.o->cls == JS_C_STRING)
        *self = self->u.o->u.prim;
    if (self->t <= JS_T_NULL)
        return js_throw(J, JS_E_TYPE, "String.prototype[Symbol.iterator] called on null or undefined");
    if (!js_to_string(J, THISI))
        return false;
    iterator_new(J, J->string_iter_proto, *js_this(J), 0);
    return !J->fatal;
}

static uint32_t char_end(const js_string *s, uint32_t at)
{
    uint32_t end = at + 1;
    while (end < s->len && ((unsigned char)s->data[end] & 0xC0) == 0x80)
        end++;
    return end;
}

static bool string_iter_next(js_state *J, int argc)
{
    (void)argc;
    js_value *self = js_this(J);
    if (self->t != JS_T_OBJ || !js_own(self->u.o, J->hid[H_kind]))
        return js_throw(J, JS_E_TYPE, "next() called on a value that is not an iterator");
    js_object *it = self->u.o;
    js_value target = hidden(it, J->hid[H_target]);
    uint32_t at = (uint32_t)hidden(it, J->hid[H_index]).u.n;
    if (target.t != JS_T_STR || at >= target.u.s->len) {
        js_define(J, it, J->hid[H_target], js_undef(), JS_P_WRITE);
        return js_push_iter_result(J, js_undef(), true);
    }
    uint32_t end = char_end(target.u.s, at);
    js_define(J, it, J->hid[H_index], js_num(end), JS_P_WRITE);
    return js_push_iter_result(J, js_str(js_string_new(J, target.u.s->data + at, end - at)), false);
}

/* --- Durchlaufen von C aus ---------------------------------------------------------------------------- */

enum { IT_ARRAY, IT_STRING, IT_COLL, IT_LIKE, IT_PROTOCOL };

/* Ist die Eigenschaft [Symbol.iterator] von o noch die eingebaute Funktion fn? */
static bool builtin_iteration(js_state *J, const js_object *o, const js_object *fn)
{
    js_prop *p = js_lookup(o, J->sym[SYM_iterator], NULL);
    return p && !(p->flags & JS_P_ACCESSOR) && p->v.t == JS_T_OBJ && p->v.u.o == fn;
}

bool js_iter_open(js_state *J, int index, js_iter *it)
{
    js_value v = J->stack[index];
    it->slot = J->top;
    it->at = 0;
    it->done = true;                    /* bis das Öffnen gelungen ist */
    if (v.t <= JS_T_NULL)
        return js_throw(J, JS_E_TYPE, "%s is not iterable", v.t == JS_T_NULL ? "null" : "undefined");
    if (J->top + 8 > JS_STACK_SIZE)
        return js_throw(J, JS_E_RANGE, "Maximum call stack size exceeded");
    js_push(J, v);
    js_push(J, js_undef());
    if (v.t == JS_T_OBJ && v.u.o->cls == JS_C_ARRAY && builtin_iteration(J, v.u.o, J->array_values)) {
        it->kind = IT_ARRAY;
        it->done = false;
        return true;
    }
    if (v.t == JS_T_STR && builtin_iteration(J, J->string_proto, J->string_values)) {
        it->kind = IT_STRING;
        it->done = false;
        return true;
    }
    int what;
    if (js_coll_iterable(J, &v, &what)) {
        it->kind = IT_COLL;
        it->what = (uint8_t)what;
        it->epoch = js_coll_epoch(&v);
        it->seq = 0;
        it->done = false;
        return true;
    }
    if (!js_get(J, it->slot, J->sym[SYM_iterator]))
        return false;
    if (!js_is_callable(js_top(J, 0))) {
        js_pop(J, 1);
        if (v.t == JS_T_OBJ) {
            /*
             * Großzügiger als die Regel: Was eine Länge und nummerierte
             * Glieder hat (Listen von Elementen einer Seite), gilt auch.
             */
            if (js_lookup(v.u.o, J->k[K_length], NULL) || v.u.o->cls == JS_C_HOST) {
                /* ... solange es sich in vernünftiger Zeit durchlaufen lässt */
                double length = 0;
                if (!js_get(J, it->slot, J->k[K_length]) || !js_to_number(J, J->top - 1, &length))
                    return false;
                js_pop(J, 1);
                if (length > 1000000)
                    return js_throw(J, JS_E_RANGE, "Invalid array length");
                it->kind = IT_LIKE;
                it->done = false;
                return true;
            }
        }
        return js_throw(J, JS_E_TYPE, "Value is not iterable");
    }
    /* [wert, -, methode] -> methode.call(wert) -> Iterator */
    js_push(J, v);
    if (!js_call(J, 0))
        return false;
    if (js_top(J, 0)->t != JS_T_OBJ)
        return js_throw(J, JS_E_TYPE, "Result of the Symbol.iterator method is not an object");
    J->stack[it->slot] = *js_top(J, 0);
    js_pop(J, 1);
    if (!js_get(J, it->slot, J->k[K_next]))
        return false;
    J->stack[it->slot + 1] = *js_top(J, 0);
    js_pop(J, 1);
    it->kind = IT_PROTOCOL;
    it->done = false;
    return true;
}

bool js_iter_next(js_state *J, js_iter *it, bool *done)
{
    *done = true;
    if (it->done)
        return true;
    js_value *source = &J->stack[it->slot];
    switch (it->kind) {
    case IT_ARRAY: {
        js_object *a = source->u.o;
        if (it->at >= a->u.arr.len) {
            it->done = true;
            return true;
        }
        *done = false;
        return js_push(J, a->u.arr.v[it->at++]);
    }
    case IT_STRING: {
        js_string *s = source->u.s;
        if (it->at >= s->len) {
            it->done = true;
            return true;
        }
        uint32_t end = char_end(s, it->at);
        js_string *one = js_string_new(J, s->data + it->at, end - it->at);
        it->at = end;
        *done = false;
        return js_push(J, js_str(one));
    }
    case IT_COLL:
        if (!js_coll_next(J, source, it->what, &it->at, &it->seq, &it->epoch, done)) {
            it->done = true;
            return false;
        }
        it->done = *done;
        return true;
    case IT_LIKE: {
        double length;
        it->done = true;
        if (!js_get(J, it->slot, J->k[K_length]) || !js_to_number(J, J->top - 1, &length))
            return false;
        js_pop(J, 1);
        if (!(it->at < length) || it->at >= JS_ARRAY_MAX)
            return true;
        if (!js_get_index(J, it->slot, it->at++))
            return false;
        it->done = false;
        *done = false;
        return true;
    }
    default:
        break;
    }
    it->done = true;
    js_push(J, J->stack[it->slot + 1]);
    js_push(J, J->stack[it->slot]);
    if (!js_is_callable(js_top(J, 1)))
        return js_throw(J, JS_E_TYPE, "Iterator has no next() method");
    if (!js_call(J, 0))
        return false;
    if (js_top(J, 0)->t != JS_T_OBJ)
        return js_throw(J, JS_E_TYPE, "Iterator result is not an object");
    if (!js_get(J, J->top - 1, J->k[K_done]))
        return false;
    bool finished = js_truthy(js_top(J, 0));
    js_pop(J, 1);
    if (finished) {
        js_pop(J, 1);
        return true;
    }
    if (!js_get(J, J->top - 1, J->k[K_value]))
        return false;
    *js_top(J, 1) = *js_top(J, 0);
    js_pop(J, 1);
    it->done = false;
    *done = false;
    return true;
}

bool js_iter_close(js_state *J, js_iter *it, bool failing)
{
    if (it->done || it->kind != IT_PROTOCOL || J->fatal) {
        it->done = true;
        return !failing;
    }
    it->done = true;
    int base = J->top;
    js_push(J, failing ? J->exception : js_undef());
    bool throwing = J->throwing;
    if (failing) {
        J->throwing = false;
        J->exception = js_undef();
    }
    bool ok = js_get(J, it->slot, J->k[K_return]);
    if (ok && js_is_callable(js_top(J, 0))) {
        js_push(J, J->stack[it->slot]);
        ok = js_call(J, 0);
        if (ok && js_top(J, 0)->t != JS_T_OBJ && !failing)
            ok = js_throw(J, JS_E_TYPE, "Iterator result is not an object");
    }
    if (failing && !J->fatal) {
        /* die ursprüngliche Ausnahme gilt weiter */
        J->exception = J->stack[base];
        J->throwing = throwing;
        ok = false;
    }
    if (ok || failing)
        J->top = base;
    return ok;
}

/* --- Einrichten ------------------------------------------------------------------------------------ */

static void define_symbol_fn(js_state *J, js_object *o, int sym, const char *name, js_native_fn fn,
                             int magic)
{
    js_object *f = js_native_new(J, fn, name, 0, magic);
    js_define(J, o, J->sym[sym], js_obj(f), JS_P_WRITE | JS_P_CONF);
}

static void define_tag(js_state *J, js_object *o, const char *tag)
{
    js_define(J, o, J->sym[SYM_toStringTag], js_str(js_intern_cstr(J, tag)), JS_P_CONF);
}

void js_sym_init(js_state *J)
{
    /* Symbol */
    J->symbol_proto = js_object_new(J, JS_C_OBJECT, J->object_proto);
    js_object *symbol = js_native_new(J, symbol_ctor, "Symbol", 0, 0);
    js_define(J, symbol, J->k[K_prototype], js_obj(J->symbol_proto), 0);
    js_define(J, J->symbol_proto, J->k[K_constructor], js_obj(symbol), JS_P_WRITE | JS_P_CONF);
    js_define(J, J->global, js_intern_cstr(J, "Symbol"), js_obj(symbol), JS_P_WRITE | JS_P_CONF);
    js_def_native(J, symbol, "for", symbol_registry, 1, 0);
    js_def_native(J, symbol, "keyFor", symbol_registry, 1, 1);
    static const char *const names[SYM_COUNT] = {
        "iterator", "asyncIterator", "hasInstance", "toPrimitive", "toStringTag", "species",
        "unscopables", "isConcatSpreadable", "match", "matchAll", "replace", "search", "split",
    };
    for (int i = 0; i < SYM_COUNT; i++)
        js_def_value(J, symbol, names[i], js_sym(J->sym[i]), 0);
    js_def_native(J, J->symbol_proto, "toString", symbol_proto_fn, 0, 0);
    js_def_native(J, J->symbol_proto, "valueOf", symbol_proto_fn, 0, 1);
    js_def_accessor(J, J->symbol_proto, "description", symbol_proto_fn, NULL, 2);
    define_symbol_fn(J, J->symbol_proto, SYM_toPrimitive, "[Symbol.toPrimitive]", symbol_proto_fn, 1);
    define_tag(J, J->symbol_proto, "Symbol");

    /* Iteratoren: alle erben [Symbol.iterator]() { return this } */
    J->iterator_proto = js_object_new(J, JS_C_OBJECT, J->object_proto);
    define_symbol_fn(J, J->iterator_proto, SYM_iterator, "[Symbol.iterator]", iterator_self, 0);

    J->array_iter_proto = js_object_new(J, JS_C_OBJECT, J->iterator_proto);
    js_def_native(J, J->array_iter_proto, "next", array_iter_next, 0, 0);
    define_tag(J, J->array_iter_proto, "Array Iterator");
    js_def_native(J, J->array_proto, "keys", array_iterator, 0, 0);
    js_def_native(J, J->array_proto, "entries", array_iterator, 0, 2);
    J->array_values = js_native_new(J, array_iterator, "values", 0, 1);
    js_define(J, J->array_proto, js_intern_cstr(J, "values"), js_obj(J->array_values),
              JS_P_WRITE | JS_P_CONF);
    js_define(J, J->array_proto, J->sym[SYM_iterator], js_obj(J->array_values),
              JS_P_WRITE | JS_P_CONF);

    J->string_iter_proto = js_object_new(J, JS_C_OBJECT, J->iterator_proto);
    js_def_native(J, J->string_iter_proto, "next", string_iter_next, 0, 0);
    define_tag(J, J->string_iter_proto, "String Iterator");
    J->string_values = js_native_new(J, string_iterator, "[Symbol.iterator]", 0, 0);
    js_define(J, J->string_proto, J->sym[SYM_iterator], js_obj(J->string_values),
              JS_P_WRITE | JS_P_CONF);
}
