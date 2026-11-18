/* JavaScript: die eingebauten Objekte und Funktionen - siehe js.h */
#include "js_impl.h"
#include "string.h"

void js_lib2_init(js_state *J);         /* js_lib2.c: JSON, Date, Zeitgeber, Vorspann */

typedef struct {
    const char *name;
    js_native_fn fn;
    uint8_t nargs;
    int magic;
} fndef;

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))
#define ARGI(i) (J->bot + 2 + (i))
#define THISI (J->bot + 1)
#define MAGIC (js_callee(J)->u.n.magic)

static void def_functions(js_state *J, js_object *o, const fndef *defs, int count)
{
    for (int i = 0; i < count; i++)
        js_def_native(J, o, defs[i].name, defs[i].fn, defs[i].nargs, defs[i].magic);
}

static js_object *def_constructor(js_state *J, const char *name, js_native_fn fn, int nargs,
                                  js_object *proto)
{
    js_object *ctor = js_native_new(J, fn, name, nargs, 0);
    js_define(J, ctor, J->k[K_prototype], js_obj(proto), 0);
    js_define(J, proto, J->k[K_constructor], js_obj(ctor), JS_P_WRITE | JS_P_CONF);
    js_define(J, J->global, js_intern_cstr(J, name), js_obj(ctor), JS_P_WRITE | JS_P_CONF);
    return ctor;
}

static bool arg_number(js_state *J, int i, int argc, double *out)
{
    if (i >= argc) {
        *out = js_nan();
        return true;
    }
    return js_to_number(J, ARGI(i), out);
}

/* Ganzzahliges Argument (abgeschnitten, begrenzt); fehlt es, gilt fallback */
static bool arg_int(js_state *J, int i, int argc, double fallback, double *out)
{
    if (i >= argc || js_arg(J, i)->t == JS_T_UNDEF) {
        *out = fallback;
        return true;
    }
    double n;
    if (!js_to_number(J, ARGI(i), &n))
        return false;
    *out = js_isnan(n) ? 0 : js_trunc(n);
    return true;
}

static bool arg_string(js_state *J, int i, js_string **out)
{
    if (!js_to_string(J, ARGI(i)))
        return false;
    *out = js_arg(J, i)->u.s;
    return true;
}

/* Ruft eine Rückruffunktion: fn(value, index, array) mit thisArg; Ergebnis oben auf dem Stapel */
static bool call_callback(js_state *J, const js_value *fn, const js_value *this_arg, js_value v,
                          uint32_t index, js_object *array)
{
    js_push(J, *fn);
    js_push(J, this_arg ? *this_arg : js_undef());
    js_push(J, v);
    js_push(J, js_num(index));
    js_push(J, js_obj(array));
    return js_call(J, 3);
}

/* --- Object ---------------------------------------------------------------------------------------- */

static bool object_ctor(js_state *J, int argc)
{
    if (argc == 0 || js_arg(J, 0)->t <= JS_T_NULL)
        return js_push(J, js_obj(js_object_new(J, JS_C_OBJECT, J->object_proto)));
    return js_to_object(J, ARGI(0)) && js_push(J, *js_arg(J, 0));
}

static const char *class_name(const js_value *v)
{
    if (v->t == JS_T_UNDEF) return "Undefined";
    if (v->t == JS_T_NULL) return "Null";
    if (v->t == JS_T_BOOL) return "Boolean";
    if (v->t == JS_T_NUM) return "Number";
    if (v->t == JS_T_STR) return "String";
    switch (v->u.o->cls) {
    case JS_C_ARRAY: return "Array";
    case JS_C_FUNCTION: case JS_C_NATIVE: return "Function";
    case JS_C_ERROR: return "Error";
    case JS_C_BOOLEAN: return "Boolean";
    case JS_C_NUMBER: return "Number";
    case JS_C_STRING: return "String";
    case JS_C_DATE: return "Date";
    case JS_C_REGEXP: return "RegExp";
    case JS_C_HOST: return v->u.o->u.host.cls && v->u.o->u.host.cls->name
                               ? v->u.o->u.host.cls->name : "Object";
    default: return "Object";
    }
}

static bool object_to_string(js_state *J, int argc)
{
    (void)argc;
    char text[64];
    const char *name = class_name(js_this(J));
    size_t n = strlen(name);
    if (n > 40)
        n = 40;
    memcpy(text, "[object ", 8);
    memcpy(text + 8, name, n);
    text[8 + n] = ']';
    return js_push_lstr(J, text, n + 9);
}

static bool has_own(js_state *J, js_object *o, js_string *key)
{
    if (js_own(o, key))
        return true;
    if (o->cls == JS_C_ARRAY)
        return (key->index != JS_NO_INDEX && key->index < o->u.arr.len) || key == J->k[K_length];
    if (o->cls == JS_C_STRING)
        return (key->index != JS_NO_INDEX && key->index < o->u.prim.u.s->chars) ||
               key == J->k[K_length];
    if (o->cls == JS_C_FUNCTION)
        return key == J->k[K_prototype] && !(o->flags & JS_F_ARROW);
    return false;
}

static bool object_proto_fn(js_state *J, int argc)
{
    (void)argc;
    js_string *key;
    switch (MAGIC) {
    case 0:                             /* hasOwnProperty */
    case 2: {                           /* propertyIsEnumerable */
        if (!js_to_key(J, ARGI(0), &key) || !js_to_object(J, THISI))
            return false;
        js_object *o = js_this(J)->u.o;
        bool found = has_own(J, o, key);
        if (found && MAGIC == 2) {
            js_prop *p = js_own(o, key);
            found = !p || (p->flags & JS_P_ENUM);
        }
        return js_push(J, js_bool(found));
    }
    case 1: {                           /* isPrototypeOf */
        bool found = false;
        if (js_arg(J, 0)->t == JS_T_OBJ && js_this(J)->t == JS_T_OBJ) {
            js_object *o = js_arg(J, 0)->u.o->proto;
            for (int i = 0; o && i < 10000 && !found; i++, o = o->proto)
                found = o == js_this(J)->u.o;
        }
        return js_push(J, js_bool(found));
    }
    case 3:                             /* valueOf */
        return js_to_object(J, THISI) && js_push(J, *js_this(J));
    default:                            /* toLocaleString */
        return js_to_string(J, THISI) && js_push(J, *js_this(J));
    }
}

static bool proto_get(js_state *J, int argc)
{
    (void)argc;
    if (!js_to_object(J, THISI))
        return false;
    js_object *p = js_this(J)->u.o->proto;
    return js_push(J, p ? js_obj(p) : js_null());
}

static bool set_proto(js_state *J, js_object *o, const js_value *v)
{
    if (v->t != JS_T_OBJ && v->t != JS_T_NULL)
        return true;
    js_object *p = v->t == JS_T_OBJ ? v->u.o : NULL;
    for (js_object *q = p; q; q = q->proto) {
        if (q == o)
            return js_throw(J, JS_E_TYPE, "Cyclic __proto__ value");
    }
    o->proto = p;
    return true;
}

static bool proto_set(js_state *J, int argc)
{
    (void)argc;
    if (js_this(J)->t == JS_T_OBJ && !set_proto(J, js_this(J)->u.o, js_arg(J, 0)))
        return false;
    return js_push(J, js_undef());
}

/* Liest {value, get, set, writable, enumerable, configurable} und legt die Eigenschaft an */
static bool define_from_descriptor(js_state *J, js_object *o, js_string *key, int desc)
{
    if (J->stack[desc].t != JS_T_OBJ)
        return js_throw(J, JS_E_TYPE, "Property description must be an object");
    js_prop *old = js_own(o, key);
    int flags = old ? old->flags & JS_P_ALL : 0;
    static const int names[3] = { K_enumerable, K_writable, K_configurable };
    static const int bits[3] = { JS_P_ENUM, JS_P_WRITE, JS_P_CONF };
    js_object *d = J->stack[desc].u.o;
    for (int i = 0; i < 3; i++) {
        if (!js_lookup(d, J->k[names[i]], NULL))
            continue;
        if (!js_get(J, desc, J->k[names[i]]))
            return false;
        flags = js_truthy(js_top(J, 0)) ? flags | bits[i] : flags & ~bits[i];
        js_pop(J, 1);
    }
    bool has_get = js_lookup(d, J->k[K_get], NULL) != NULL;
    bool has_set = js_lookup(d, J->k[K_set], NULL) != NULL;
    if (has_get || has_set) {
        if (!js_get(J, desc, J->k[K_get]) || !js_get(J, desc, J->k[K_set]))
            return false;
        js_value *g = js_top(J, 1), *s = js_top(J, 0);
        if ((g->t != JS_T_UNDEF && !js_is_callable(g)) || (s->t != JS_T_UNDEF && !js_is_callable(s)))
            return js_throw(J, JS_E_TYPE, "Getter or setter is not a function");
        if (o->cls == JS_C_ARRAY && key->index != JS_NO_INDEX)
            return js_throw(J, JS_E_TYPE, "Accessors on array elements are not supported");
        js_prop *p = js_define(J, o, key, js_is_callable(g) ? *g : js_undef(),
                               (flags & (JS_P_ENUM | JS_P_CONF)) | JS_P_ACCESSOR);
        if (p)
            p->set = js_is_callable(s) ? s->u.o : NULL;
        js_pop(J, 2);
        return true;
    }
    if (!js_get(J, desc, J->k[K_value]))
        return false;
    bool has_value = js_lookup(d, J->k[K_value], NULL) != NULL;
    if (o->cls == JS_C_ARRAY && (key->index != JS_NO_INDEX || key == J->k[K_length])) {
        js_push(J, js_obj(o));
        js_push(J, *js_top(J, 1));
        bool ok = js_put(J, J->top - 2, key);
        if (ok)
            js_pop(J, 2);
        return ok;
    }
    js_value v = has_value ? *js_top(J, 0) : old && !(old->flags & JS_P_ACCESSOR) ? old->v : js_undef();
    js_define(J, o, key, v, flags);
    js_pop(J, 1);
    return true;
}

static bool object_static(js_state *J, int argc)
{
    js_value *a = js_arg(J, 0);
    switch (MAGIC) {
    case 0: case 1: case 2: case 3: {   /* keys, values, entries, getOwnPropertyNames */
        if (!js_to_object(J, ARGI(0)))
            return false;
        js_object *o = js_arg(J, 0)->u.o, *keys = js_array_new(J, 0);
        js_push(J, js_obj(keys));
        if (!js_own_keys(J, o, MAGIC != 3, keys))
            return false;
        if (MAGIC == 3 && o->cls == JS_C_ARRAY && !js_array_push(J, keys, js_str(J->k[K_length])))
            return false;
        if (MAGIC == 0 || MAGIC == 3)
            return true;
        js_object *out = js_array_new(J, keys->u.arr.len);
        js_push(J, js_obj(out));
        for (uint32_t i = 0; i < keys->u.arr.len; i++) {
            js_string *key = keys->u.arr.v[i].u.s;
            if (!js_get(J, ARGI(0), key))
                return false;
            if (MAGIC == 2) {
                js_object *pair = js_array_new(J, 2);
                js_array_push(J, pair, js_str(key));
                js_array_push(J, pair, *js_top(J, 0));
                *js_top(J, 0) = js_obj(pair);
            }
            if (!js_array_push(J, out, *js_top(J, 0)))
                return false;
            js_pop(J, 1);
        }
        return true;
    }
    case 4: {                           /* assign */
        if (!js_to_object(J, ARGI(0)))
            return false;
        for (int i = 1; i < argc; i++) {
            if (js_arg(J, i)->t != JS_T_OBJ)
                continue;
            js_object *keys = js_array_new(J, 0);
            js_push(J, js_obj(keys));
            if (!js_own_keys(J, js_arg(J, i)->u.o, true, keys))
                return false;
            for (uint32_t k = 0; k < keys->u.arr.len; k++) {
                js_string *key = keys->u.arr.v[k].u.s;
                if (!js_get(J, ARGI(i), key) || !js_put(J, ARGI(0), key))
                    return false;
            }
            js_pop(J, 1);
        }
        return js_push(J, *js_arg(J, 0));
    }
    case 5: {                           /* create */
        if (a->t != JS_T_OBJ && a->t != JS_T_NULL)
            return js_throw(J, JS_E_TYPE, "Object prototype may only be an Object or null");
        js_object *o = js_object_new(J, JS_C_OBJECT, a->t == JS_T_OBJ ? a->u.o : NULL);
        js_push(J, js_obj(o));
        if (argc > 1 && js_arg(J, 1)->t == JS_T_OBJ) {
            js_object *keys = js_array_new(J, 0);
            js_push(J, js_obj(keys));
            if (!js_own_keys(J, js_arg(J, 1)->u.o, true, keys))
                return false;
            for (uint32_t k = 0; k < keys->u.arr.len; k++) {
                js_string *key = keys->u.arr.v[k].u.s;
                if (!js_get(J, ARGI(1), key) || !define_from_descriptor(J, o, key, J->top - 1))
                    return false;
                js_pop(J, 1);
            }
            js_pop(J, 1);
        }
        return true;
    }
    case 6:                             /* getPrototypeOf */
        if (!js_to_object(J, ARGI(0)))
            return false;
        return js_push(J, js_arg(J, 0)->u.o->proto ? js_obj(js_arg(J, 0)->u.o->proto) : js_null());
    case 7:                             /* setPrototypeOf */
        if (a->t == JS_T_OBJ && !set_proto(J, a->u.o, js_arg(J, 1)))
            return false;
        return js_push(J, *js_arg(J, 0));
    case 8: {                           /* defineProperty */
        js_string *key;
        if (a->t != JS_T_OBJ)
            return js_throw(J, JS_E_TYPE, "Object.defineProperty called on non-object");
        if (!js_to_key(J, ARGI(1), &key) || !define_from_descriptor(J, a->u.o, key, ARGI(2)))
            return false;
        return js_push(J, *js_arg(J, 0));
    }
    case 9: {                           /* defineProperties */
        if (a->t != JS_T_OBJ || js_arg(J, 1)->t != JS_T_OBJ)
            return js_throw(J, JS_E_TYPE, "Object.defineProperties called on non-object");
        js_object *keys = js_array_new(J, 0);
        js_push(J, js_obj(keys));
        if (!js_own_keys(J, js_arg(J, 1)->u.o, true, keys))
            return false;
        for (uint32_t k = 0; k < keys->u.arr.len; k++) {
            js_string *key = keys->u.arr.v[k].u.s;
            if (!js_get(J, ARGI(1), key) || !define_from_descriptor(J, a->u.o, key, J->top - 1))
                return false;
            js_pop(J, 1);
        }
        return js_push(J, *js_arg(J, 0));
    }
    case 10: {                          /* getOwnPropertyDescriptor */
        js_string *key;
        if (!js_to_object(J, ARGI(0)) || !js_to_key(J, ARGI(1), &key))
            return false;
        js_object *o = js_arg(J, 0)->u.o;
        js_prop *p = js_own(o, key);
        if (!p && !has_own(J, o, key))
            return js_push(J, js_undef());
        js_object *d = js_object_new(J, JS_C_OBJECT, J->object_proto);
        js_push(J, js_obj(d));
        int flags = p ? p->flags : JS_P_ALL;
        if (p && (p->flags & JS_P_ACCESSOR)) {
            js_define(J, d, J->k[K_get], p->v, JS_P_ALL);
            js_define(J, d, J->k[K_set], p->set ? js_obj(p->set) : js_undef(), JS_P_ALL);
        } else {
            if (!js_get(J, ARGI(0), key))
                return false;
            js_define(J, d, J->k[K_value], *js_top(J, 0), JS_P_ALL);
            js_pop(J, 1);
            js_define(J, d, J->k[K_writable], js_bool(flags & JS_P_WRITE), JS_P_ALL);
        }
        js_define(J, d, J->k[K_enumerable], js_bool(flags & JS_P_ENUM), JS_P_ALL);
        js_define(J, d, J->k[K_configurable], js_bool(flags & JS_P_CONF), JS_P_ALL);
        return true;
    }
    case 11: case 12: case 13:          /* freeze, seal, preventExtensions */
        if (a->t == JS_T_OBJ) {
            js_object *o = a->u.o;
            o->flags |= JS_F_NOEXT;
            for (uint32_t i = 0; MAGIC != 13 && i < o->count; i++) {
                if (!o->props[i].key)
                    continue;
                o->props[i].flags &= (uint8_t)~JS_P_CONF;
                if (MAGIC == 11 && !(o->props[i].flags & JS_P_ACCESSOR))
                    o->props[i].flags &= (uint8_t)~JS_P_WRITE;
            }
        }
        return js_push(J, *js_arg(J, 0));
    case 14: case 15:                   /* isFrozen, isSealed */
        return js_push(J, js_bool(a->t != JS_T_OBJ || (a->u.o->flags & JS_F_NOEXT)));
    case 16:                            /* isExtensible */
        return js_push(J, js_bool(a->t == JS_T_OBJ && !(a->u.o->flags & JS_F_NOEXT)));
    case 17: {                          /* is */
        js_value *b = js_arg(J, 1);
        bool same = js_strict_equal(a, b);
        if (a->t == JS_T_NUM && b->t == JS_T_NUM) {
            if (js_isnan(a->u.n) && js_isnan(b->u.n))
                same = true;
            else if (a->u.n == 0 && b->u.n == 0)
                same = (1 / a->u.n) == (1 / b->u.n);
        }
        return js_push(J, js_bool(same));
    }
    default: {                          /* fromEntries */
        js_object *items = js_array_new(J, 0), *o = js_object_new(J, JS_C_OBJECT, J->object_proto);
        js_push(J, js_obj(items));
        js_push(J, js_obj(o));
        if (!js_iterate(J, ARGI(0), items))
            return false;
        for (uint32_t i = 0; i < items->u.arr.len; i++) {
            js_string *key;
            js_push(J, items->u.arr.v[i]);
            if (!js_get_index(J, J->top - 1, 0) || !js_to_key(J, J->top - 1, &key) ||
                !js_get_index(J, J->top - 2, 1))
                return false;
            js_define(J, o, key, *js_top(J, 0), JS_P_ALL);
            js_pop(J, 3);
        }
        return true;
    }
    }
}

/* --- Function -------------------------------------------------------------------------------------- */

static bool function_ctor(js_state *J, int argc)
{
    /* new Function("a", "b", "return a + b") */
    js_string *source = js_string_new(J, "(function anonymous(", 20);
    js_push(J, js_str(source));
    for (int i = 0; i < argc; i++) {
        if (!js_to_string(J, ARGI(i)))
            return false;
        const char *glue = i == argc - 1 ? "\n) {\n" : i == 0 ? "" : ",";
        js_string *g = js_string_new(J, glue, strlen(glue));
        js_push(J, js_str(g));
        js_string *joined = js_string_concat(J, js_top(J, 1)->u.s, g);
        *js_top(J, 1) = js_str(joined);
        js_pop(J, 1);
        *js_top(J, 0) = js_str(js_string_concat(J, js_top(J, 0)->u.s, js_arg(J, i)->u.s));
    }
    const char *tail = argc == 0 ? "\n) {\n\n})" : "\n})";
    js_string *t = js_string_new(J, tail, strlen(tail));
    js_push(J, js_str(t));
    js_string *all = js_string_concat(J, js_top(J, 1)->u.s, t);
    *js_top(J, 1) = js_str(all);
    js_pop(J, 1);
    js_fninfo *program = js_parse(J, all->data, all->len, "Function");
    if (!program)
        return false;
    return js_exec_program(J, program);
}

static bool function_to_string(js_state *J, int argc)
{
    (void)argc;
    js_value *f = js_this(J);
    if (!js_is_callable(f))
        return js_throw(J, JS_E_TYPE, "Function.prototype.toString requires a function");
    if (f->u.o->cls == JS_C_FUNCTION) {
        js_fninfo *info = f->u.o->u.f.fn->u.fn;
        if (info->source && info->source_len)
            return js_push_lstr(J, info->source, info->source_len);
    }
    return js_push_cstr(J, "function () { [native code] }");
}

static bool function_call(js_state *J, int argc)
{
    /* f.call(thisArg, ...args): [fn?, f, thisArg, args] - einfach neu auflegen */
    int base = J->top;
    js_push(J, *js_this(J));
    js_push(J, argc > 0 ? *js_arg(J, 0) : js_undef());
    for (int i = 1; i < argc; i++)
        js_push(J, *js_arg(J, i));
    (void)base;
    return js_call(J, argc > 1 ? argc - 1 : 0);
}

static bool function_apply(js_state *J, int argc)
{
    js_push(J, *js_this(J));
    js_push(J, argc > 0 ? *js_arg(J, 0) : js_undef());
    int count = 0;
    if (argc > 1 && js_arg(J, 1)->t > JS_T_NULL) {
        js_object *items = js_array_new(J, 0);
        js_push(J, js_obj(items));
        if (!js_iterate(J, ARGI(1), items))
            return false;
        uint32_t n = items->u.arr.len;
        if (J->top + (int)n + 200 > JS_STACK_SIZE || n > JS_STACK_SIZE)
            return js_throw(J, JS_E_RANGE, "Too many arguments");
        js_pop(J, 1);
        for (uint32_t i = 0; i < n; i++)
            J->stack[J->top + i] = items->u.arr.v[i];
        J->top += (int)n;
        count = (int)n;
    }
    return js_call(J, count);
}

static bool bound_call(js_state *J, int argc)
{
    js_object *data = js_callee(J)->u.n.data;
    js_prop *target = js_own(data, js_intern_cstr(J, "target"));
    js_prop *self = js_own(data, js_intern_cstr(J, "self"));
    js_prop *args = js_own(data, js_intern_cstr(J, "args"));
    if (!target || !self || !args || !js_is_array(&args->v))
        return js_throw(J, JS_E_TYPE, "Bound function is broken");
    js_object *bound = args->v.u.o;
    bool construct = J->constructing;
    if (J->top + (int)bound->u.arr.len + argc + 200 > JS_STACK_SIZE)
        return js_throw(J, JS_E_RANGE, "Too many arguments");
    js_push(J, target->v);
    if (!construct)
        js_push(J, self->v);
    for (uint32_t i = 0; i < bound->u.arr.len; i++)
        js_push(J, bound->u.arr.v[i]);
    for (int i = 0; i < argc; i++)
        js_push(J, *js_arg(J, i));
    int total = (int)bound->u.arr.len + argc;
    return construct ? js_construct(J, total) : js_call(J, total);
}

static bool function_bind(js_state *J, int argc)
{
    if (!js_is_callable(js_this(J)))
        return js_throw(J, JS_E_TYPE, "Bind must be called on a function");
    js_object *f = js_native_new(J, bound_call, "bound", 0, -77);
    js_push(J, js_obj(f));
    js_object *data = js_object_new(J, JS_C_OBJECT, NULL);
    f->u.n.data = data;
    js_object *args = js_array_new(J, 0);
    js_define(J, data, js_intern_cstr(J, "target"), *js_this(J), JS_P_ALL);
    js_define(J, data, js_intern_cstr(J, "self"), argc > 0 ? *js_arg(J, 0) : js_undef(), JS_P_ALL);
    js_define(J, data, js_intern_cstr(J, "args"), js_obj(args), JS_P_ALL);
    for (int i = 1; i < argc; i++) {
        if (!js_array_push(J, args, *js_arg(J, i)))
            return false;
    }
    return true;
}

static bool function_proto_call(js_state *J, int argc)
{
    (void)argc;
    return js_push(J, js_undef());
}

/* --- Array ----------------------------------------------------------------------------------------- */

static bool array_ctor(js_state *J, int argc)
{
    js_object *a = js_array_new(J, 0);
    js_push(J, js_obj(a));
    if (argc == 1 && js_arg(J, 0)->t == JS_T_NUM) {
        double n = js_arg(J, 0)->u.n;
        if (!(n >= 0 && n <= JS_ARRAY_MAX && n == js_trunc(n)))
            return js_throw(J, JS_E_RANGE, "Invalid array length");
        return js_array_resize(J, a, (uint32_t)n);
    }
    for (int i = 0; i < argc; i++) {
        if (!js_array_push(J, a, *js_arg(J, i)))
            return false;
    }
    return true;
}

/*
 * Das Feld, auf dem eine Methode arbeitet. Für feldähnliche Dinge (Listen von
 * Elementen, arguments) entsteht bei lesenden Methoden eine Kopie.
 */
static js_object *this_array(js_state *J, bool mutate)
{
    js_value *self = js_this(J);
    if (js_is_array(self))
        return self->u.o;
    if (self->t <= JS_T_NULL || (mutate && self->t != JS_T_OBJ)) {
        js_throw(J, JS_E_TYPE, "Array method called on a value that is not an array");
        return NULL;
    }
    js_object *copy = js_array_new(J, 0);
    js_push(J, js_obj(copy));
    bool like = self->t == JS_T_STR || (self->t == JS_T_OBJ && js_lookup(self->u.o, J->k[K_length], NULL)) ||
                (self->t == JS_T_OBJ && self->u.o->cls == JS_C_HOST);
    if (like && !js_iterate(J, THISI, copy))
        return NULL;
    if (mutate)
        return copy;                    /* bleibt auf dem Stapel; array_done() schreibt zurück */
    *js_this(J) = js_obj(copy);         /* nichts Feldähnliches: leer */
    js_pop(J, 1);
    return copy;
}

/*
 * Nach einer ändernden Methode auf etwas Feldähnlichem (push.call(obj, ...)):
 * überträgt das bearbeitete Feld in das Objekt - Glieder und length.
 */
static bool array_done(js_state *J, js_object *a)
{
    js_value *self = js_this(J);
    if (self->t != JS_T_OBJ || self->u.o == a)
        return true;
    double old = 0;
    if (!js_get(J, THISI, J->k[K_length]) || !js_to_number(J, J->top - 1, &old))
        return false;
    js_pop(J, 1);
    for (uint32_t i = 0; i < a->u.arr.len; i++) {
        js_push(J, *js_this(J));
        js_push(J, js_num(i));
        js_push(J, a->u.arr.v[i]);
        if (!js_put_value(J))
            return false;
    }
    for (double i = a->u.arr.len; i < old && i < a->u.arr.len + 100000.0; i++) {
        js_string *key;
        js_push(J, js_num(i));
        bool ok = js_to_key(J, J->top - 1, &key);
        js_pop(J, 1);
        if (!ok)
            return false;
        js_delete_own(J, js_this(J)->u.o, key);
    }
    js_push(J, js_num(a->u.arr.len));
    return js_put(J, THISI, J->k[K_length]);
}

/* Rechnet einen Index wie bei slice um: negativ zählt von hinten */
static uint32_t clamp_index(double i, uint32_t len)
{
    if (i < 0)
        i += len;
    if (i < 0)
        return 0;
    return i > len ? len : (uint32_t)i;
}

static bool array_push(js_state *J, int argc)
{
    js_object *a = this_array(J, true);
    if (!a)
        return false;
    for (int i = 0; i < argc; i++) {
        if (!js_array_push(J, a, *js_arg(J, i)))
            return false;
    }
    return array_done(J, a) && js_push(J, js_num(a->u.arr.len));
}

static bool array_pop_shift(js_state *J, int argc)
{
    (void)argc;
    js_object *a = this_array(J, true);
    if (!a)
        return false;
    if (a->u.arr.len == 0)
        return array_done(J, a) && js_push(J, js_undef());
    js_value v;
    if (MAGIC == 0) {
        v = a->u.arr.v[--a->u.arr.len];
    } else {
        v = a->u.arr.v[0];
        memmove(a->u.arr.v, a->u.arr.v + 1, (size_t)(a->u.arr.len - 1) * sizeof(js_value));
        a->u.arr.len--;
    }
    js_push(J, v);                      /* festhalten, bis es zurückgegeben ist */
    return array_done(J, a) && js_push(J, v);
}

static bool array_unshift(js_state *J, int argc)
{
    js_object *a = this_array(J, true);
    if (!a)
        return false;
    uint32_t old = a->u.arr.len;
    if (argc > 0) {
        if (!js_array_resize(J, a, old + (uint32_t)argc))
            return false;
        memmove(a->u.arr.v + argc, a->u.arr.v, (size_t)old * sizeof(js_value));
        for (int i = 0; i < argc; i++)
            a->u.arr.v[i] = *js_arg(J, i);
    }
    return array_done(J, a) && js_push(J, js_num(a->u.arr.len));
}

static bool array_slice(js_state *J, int argc)
{
    js_object *a = this_array(J, false);
    double from, to;
    if (!a || !arg_int(J, 0, argc, 0, &from) || !arg_int(J, 1, argc, a->u.arr.len, &to))
        return false;
    uint32_t start = clamp_index(from, a->u.arr.len), end = clamp_index(to, a->u.arr.len);
    js_object *out = js_array_new(J, end > start ? end - start : 0);
    js_push(J, js_obj(out));
    for (uint32_t i = start; i < end && i < a->u.arr.len; i++) {
        if (!js_array_push(J, out, a->u.arr.v[i]))
            return false;
    }
    return true;
}

static bool array_splice(js_state *J, int argc)
{
    js_object *a = this_array(J, true);
    double from, count;
    if (!a || !arg_int(J, 0, argc, 0, &from) || !arg_int(J, 1, argc, 4294967295.0, &count))
        return false;
    uint32_t len = a->u.arr.len, start = clamp_index(from, len);
    uint32_t removed = argc == 0 ? 0 : count < 0 ? 0 : count > len - start ? len - start
                                                                            : (uint32_t)count;
    uint32_t added = argc > 2 ? (uint32_t)(argc - 2) : 0;
    js_object *out = js_array_new(J, removed);
    js_push(J, js_obj(out));
    for (uint32_t i = 0; i < removed; i++) {
        if (!js_array_push(J, out, a->u.arr.v[start + i]))
            return false;
    }
    uint32_t tail = len - start - removed;
    if (added > removed) {
        if (!js_array_resize(J, a, len + added - removed))
            return false;
        memmove(a->u.arr.v + start + added, a->u.arr.v + start + removed,
                (size_t)tail * sizeof(js_value));
    } else if (added < removed) {
        memmove(a->u.arr.v + start + added, a->u.arr.v + start + removed,
                (size_t)tail * sizeof(js_value));
        a->u.arr.len = len + added - removed;
    }
    for (uint32_t i = 0; i < added; i++)
        a->u.arr.v[start + i] = *js_arg(J, 2 + (int)i);
    return array_done(J, a) && js_push(J, js_obj(out));
}

static bool array_concat(js_state *J, int argc)
{
    js_object *a = this_array(J, false);
    if (!a)
        return false;
    js_object *out = js_array_new(J, a->u.arr.len);
    js_push(J, js_obj(out));
    for (uint32_t i = 0; i < a->u.arr.len; i++) {
        if (!js_array_push(J, out, a->u.arr.v[i]))
            return false;
    }
    for (int i = 0; i < argc; i++) {
        js_value *v = js_arg(J, i);
        if (js_is_array(v)) {
            for (uint32_t k = 0; k < v->u.o->u.arr.len; k++) {
                if (!js_array_push(J, out, v->u.o->u.arr.v[k]))
                    return false;
            }
        } else if (!js_array_push(J, out, *v)) {
            return false;
        }
    }
    return true;
}

/* Verbindet die Glieder zu einer Zeichenkette; "seen" verhindert endlose Schachtelung */
static bool join_array(js_state *J, js_object *a, const js_string *sep, int depth)
{
    js_push(J, js_str(J->empty));
    int acc = J->top - 1;
    if (depth > 20)
        return true;
    for (uint32_t i = 0; i < a->u.arr.len; i++) {
        if (i > 0)
            J->stack[acc] = js_str(js_string_concat(J, J->stack[acc].u.s, sep));
        js_value v = a->u.arr.v[i];
        if (v.t <= JS_T_NULL)
            continue;
        if (js_is_array(&v)) {
            js_string *comma = js_string_new(J, ",", 1);
            js_push(J, js_str(comma));
            if (!join_array(J, v.u.o, comma, depth + 1))
                return false;
            J->stack[acc] = js_str(js_string_concat(J, J->stack[acc].u.s, js_top(J, 0)->u.s));
            js_pop(J, 2);
            continue;
        }
        js_push(J, v);
        if (!js_to_string(J, J->top - 1))
            return false;
        J->stack[acc] = js_str(js_string_concat(J, J->stack[acc].u.s, js_top(J, 0)->u.s));
        js_pop(J, 1);
        if (J->fatal)
            return false;
    }
    return true;
}

static bool array_join(js_state *J, int argc)
{
    js_object *a = this_array(J, false);
    if (!a)
        return false;
    js_string *sep;
    if (MAGIC == 1 || argc == 0 || js_arg(J, 0)->t == JS_T_UNDEF) {
        sep = js_string_new(J, ",", 1);
        js_push(J, js_str(sep));
    } else if (!arg_string(J, 0, &sep)) {
        return false;
    }
    return join_array(J, a, sep, 0);
}

static bool array_reverse(js_state *J, int argc)
{
    (void)argc;
    js_object *a = this_array(J, true);
    if (!a)
        return false;
    for (uint32_t i = 0, k = a->u.arr.len; i + 1 < k; i++) {
        js_value t = a->u.arr.v[i];
        a->u.arr.v[i] = a->u.arr.v[--k];
        a->u.arr.v[k] = t;
    }
    return array_done(J, a) && js_push(J, *js_this(J));
}

static bool same_value_zero(const js_value *a, const js_value *b)
{
    if (a->t == JS_T_NUM && b->t == JS_T_NUM && js_isnan(a->u.n) && js_isnan(b->u.n))
        return true;
    return js_strict_equal(a, b);
}

static bool array_index_of(js_state *J, int argc)
{
    js_object *a = this_array(J, false);
    double from;
    if (!a)
        return false;
    uint32_t len = a->u.arr.len;
    int kind = MAGIC;                   /* 0 indexOf, 1 lastIndexOf, 2 includes */
    if (!arg_int(J, 1, argc, kind == 1 ? (double)len - 1 : 0, &from))
        return false;
    js_value *needle = js_arg(J, 0);
    if (kind == 1) {
        if (from < 0)
            from += len;
        for (double i = from >= len ? (double)len - 1 : from; i >= 0; i--) {
            if (js_strict_equal(&a->u.arr.v[(uint32_t)i], needle))
                return js_push(J, js_num(i));
        }
        return js_push(J, js_num(-1));
    }
    for (uint32_t i = clamp_index(from, len); i < len; i++) {
        if (kind == 2 ? same_value_zero(&a->u.arr.v[i], needle)
                      : js_strict_equal(&a->u.arr.v[i], needle))
            return js_push(J, kind == 2 ? js_bool(true) : js_num(i));
    }
    return js_push(J, kind == 2 ? js_bool(false) : js_num(-1));
}

enum { IT_FOREACH, IT_MAP, IT_FILTER, IT_SOME, IT_EVERY, IT_FIND, IT_FIND_INDEX, IT_FIND_LAST,
       IT_FIND_LAST_INDEX, IT_FLAT_MAP };

static bool array_iterate(js_state *J, int argc)
{
    js_object *a = this_array(J, false);
    if (!a)
        return false;
    int kind = MAGIC;
    js_value fn = *js_arg(J, 0), self = argc > 1 ? *js_arg(J, 1) : js_undef();
    if (!js_is_callable(&fn))
        return js_throw(J, JS_E_TYPE, "Callback is not a function");
    js_object *out = NULL;
    if (kind == IT_MAP || kind == IT_FILTER || kind == IT_FLAT_MAP) {
        out = js_array_new(J, kind == IT_MAP ? a->u.arr.len : 0);
        js_push(J, js_obj(out));
    }
    bool backwards = kind == IT_FIND_LAST || kind == IT_FIND_LAST_INDEX;
    uint32_t len = a->u.arr.len;
    for (uint32_t step = 0; step < len && step < a->u.arr.len; step++) {
        uint32_t i = backwards ? a->u.arr.len - 1 - step : step;
        js_value v = a->u.arr.v[i];
        js_push(J, v);                  /* das Glied bleibt erreichbar, was der Rückruf auch tut */
        if (!call_callback(J, &fn, &self, v, i, a))
            return false;
        js_value *r = js_top(J, 0);
        bool yes = js_truthy(r);
        switch (kind) {
        case IT_MAP:
            if (!js_array_push(J, out, *r))
                return false;
            break;
        case IT_FLAT_MAP:
            if (js_is_array(r)) {
                for (uint32_t k = 0; k < r->u.o->u.arr.len; k++) {
                    if (!js_array_push(J, out, r->u.o->u.arr.v[k]))
                        return false;
                }
            } else if (!js_array_push(J, out, *r)) {
                return false;
            }
            break;
        case IT_FILTER:
            if (yes && !js_array_push(J, out, *js_top(J, 1)))
                return false;
            break;
        case IT_SOME:
            if (yes)
                return js_push(J, js_bool(true));
            break;
        case IT_EVERY:
            if (!yes)
                return js_push(J, js_bool(false));
            break;
        case IT_FIND: case IT_FIND_LAST:
            if (yes)
                return js_push(J, *js_top(J, 1));
            break;
        case IT_FIND_INDEX: case IT_FIND_LAST_INDEX:
            if (yes)
                return js_push(J, js_num(i));
            break;
        default:
            break;
        }
        js_pop(J, 2);
    }
    switch (kind) {
    case IT_MAP: case IT_FILTER: case IT_FLAT_MAP: return true;
    case IT_SOME: return js_push(J, js_bool(false));
    case IT_EVERY: return js_push(J, js_bool(true));
    case IT_FIND_INDEX: case IT_FIND_LAST_INDEX: return js_push(J, js_num(-1));
    default: return js_push(J, js_undef());
    }
}

static bool array_reduce(js_state *J, int argc)
{
    js_object *a = this_array(J, false);
    if (!a)
        return false;
    bool right = MAGIC == 1;
    js_value fn = *js_arg(J, 0);
    if (!js_is_callable(&fn))
        return js_throw(J, JS_E_TYPE, "Callback is not a function");
    uint32_t len = a->u.arr.len, done = 0;
    if (argc > 1) {
        js_push(J, *js_arg(J, 1));
    } else {
        if (len == 0)
            return js_throw(J, JS_E_TYPE, "Reduce of empty array with no initial value");
        js_push(J, a->u.arr.v[right ? len - 1 : 0]);
        done = 1;
    }
    for (uint32_t step = done; step < len && step < a->u.arr.len; step++) {
        uint32_t i = right ? a->u.arr.len - 1 - step : step;
        js_push(J, fn);
        js_push(J, js_undef());
        js_push(J, *js_top(J, 2));
        js_push(J, a->u.arr.v[i]);
        js_push(J, js_num(i));
        js_push(J, js_obj(a));
        if (!js_call(J, 4))
            return false;
        *js_top(J, 1) = *js_top(J, 0);
        js_pop(J, 1);
    }
    return true;
}

static bool array_fill(js_state *J, int argc)
{
    js_object *a = this_array(J, true);
    double from, to;
    if (!a || !arg_int(J, 1, argc, 0, &from) || !arg_int(J, 2, argc, a->u.arr.len, &to))
        return false;
    uint32_t end = clamp_index(to, a->u.arr.len);
    for (uint32_t i = clamp_index(from, a->u.arr.len); i < end; i++)
        a->u.arr.v[i] = *js_arg(J, 0);
    return array_done(J, a) && js_push(J, *js_this(J));
}

/* Vergleich beim Sortieren: mit Funktion oder als Zeichenketten; *out < 0, wenn a vor b gehört */
static bool sort_compare(js_state *J, const js_value *fn, js_value a, js_value b, double *out)
{
    if (a.t == JS_T_UNDEF || b.t == JS_T_UNDEF) {
        *out = (a.t == JS_T_UNDEF) - (b.t == JS_T_UNDEF);
        return true;
    }
    if (js_is_callable(fn)) {
        js_push(J, *fn);
        js_push(J, js_undef());
        js_push(J, a);
        js_push(J, b);
        if (!js_call(J, 2) || !js_to_number(J, J->top - 1, out))
            return false;
        if (js_isnan(*out))
            *out = 0;
        js_pop(J, 1);
        return true;
    }
    js_push(J, a);
    js_push(J, b);
    if (!js_to_string(J, J->top - 2) || !js_to_string(J, J->top - 1))
        return false;
    *out = js_string_compare(js_top(J, 1)->u.s, js_top(J, 0)->u.s);
    js_pop(J, 2);
    return true;
}

static bool array_sort(js_state *J, int argc)
{
    js_object *a = this_array(J, true);
    if (!a)
        return false;
    js_value fn = argc > 0 ? *js_arg(J, 0) : js_undef();
    uint32_t n = a->u.arr.len;
    /* auf Kopien sortieren: Die Vergleichsfunktion kann das Feld inzwischen verändern */
    js_object *work = js_array_new(J, n), *spare = js_array_new(J, n);
    js_push(J, js_obj(work));
    js_push(J, js_obj(spare));
    if (!js_array_resize(J, work, n) || !js_array_resize(J, spare, n))
        return false;
    if (n)
        memcpy(work->u.arr.v, a->u.arr.v, (size_t)n * sizeof(js_value));
    for (uint32_t width = 1; width < n; width *= 2) {
        for (uint32_t lo = 0; lo < n; lo += 2 * width) {
            uint32_t mid = lo + width < n ? lo + width : n, hi = lo + 2 * width < n ? lo + 2 * width : n;
            uint32_t i = lo, k = mid, out = lo;
            while (i < mid && k < hi) {
                double c;
                if (!sort_compare(J, &fn, work->u.arr.v[i], work->u.arr.v[k], &c))
                    return false;
                spare->u.arr.v[out++] = c <= 0 ? work->u.arr.v[i++] : work->u.arr.v[k++];
            }
            while (i < mid)
                spare->u.arr.v[out++] = work->u.arr.v[i++];
            while (k < hi)
                spare->u.arr.v[out++] = work->u.arr.v[k++];
        }
        js_object *t = work;
        work = spare;
        spare = t;
    }
    for (uint32_t i = 0; i < n && i < a->u.arr.len; i++)
        a->u.arr.v[i] = work->u.arr.v[i];
    return array_done(J, a) && js_push(J, *js_this(J));
}

static bool flatten(js_state *J, js_object *out, js_object *a, int depth)
{
    for (uint32_t i = 0; i < a->u.arr.len; i++) {
        js_value v = a->u.arr.v[i];
        if (js_is_array(&v) && depth > 0) {
            if (!flatten(J, out, v.u.o, depth - 1))
                return false;
        } else if (!js_array_push(J, out, v)) {
            return false;
        }
    }
    return true;
}

static bool array_misc(js_state *J, int argc)
{
    js_object *a = this_array(J, false);
    if (!a)
        return false;
    switch (MAGIC) {
    case 0: {                           /* flat */
        double depth;
        if (!arg_int(J, 0, argc, 1, &depth))
            return false;
        js_object *out = js_array_new(J, 0);
        js_push(J, js_obj(out));
        return flatten(J, out, a, depth > 50 ? 50 : (int)depth);
    }
    case 1: {                           /* at */
        double i;
        if (!arg_int(J, 0, argc, 0, &i))
            return false;
        if (i < 0)
            i += a->u.arr.len;
        return js_push(J, i >= 0 && i < a->u.arr.len ? a->u.arr.v[(uint32_t)i] : js_undef());
    }
    case 2: case 3: case 4: {           /* keys, values, entries: als fertige Felder */
        js_object *out = js_array_new(J, a->u.arr.len);
        js_push(J, js_obj(out));
        for (uint32_t i = 0; i < a->u.arr.len; i++) {
            js_value v = MAGIC == 2 ? js_num(i) : a->u.arr.v[i];
            if (MAGIC == 4) {
                js_object *pair = js_array_new(J, 2);
                js_push(J, js_obj(pair));
                js_array_push(J, pair, js_num(i));
                js_array_push(J, pair, a->u.arr.v[i]);
                v = js_obj(pair);
                js_pop(J, 1);
            }
            if (!js_array_push(J, out, v))
                return false;
        }
        return true;
    }
    default:
        return js_push(J, js_undef());
    }
}

static bool array_static(js_state *J, int argc)
{
    switch (MAGIC) {
    case 0:
        return js_push(J, js_bool(js_is_array(js_arg(J, 0))));
    case 1: {                           /* from(iterable, mapFn) */
        js_object *out = js_array_new(J, 0);
        js_push(J, js_obj(out));
        js_value *src = js_arg(J, 0);
        if (src->t == JS_T_OBJ && !js_is_array(src) && src->u.o->cls != JS_C_HOST &&
            !js_lookup(src->u.o, J->k[K_length], NULL) &&
            !js_lookup(src->u.o, js_intern_cstr(J, "__items__"), NULL))
            return true;                /* nichts Aufzählbares: leeres Feld */
        if (src->t > JS_T_NULL && src->t != JS_T_NUM && src->t != JS_T_BOOL &&
            !js_iterate(J, ARGI(0), out))
            return false;
        if (argc > 1 && js_is_callable(js_arg(J, 1))) {
            for (uint32_t i = 0; i < out->u.arr.len; i++) {
                if (!call_callback(J, js_arg(J, 1), argc > 2 ? js_arg(J, 2) : NULL,
                                   out->u.arr.v[i], i, out))
                    return false;
                if (i < out->u.arr.len)
                    out->u.arr.v[i] = *js_top(J, 0);
                js_pop(J, 1);
            }
        }
        return true;
    }
    default: {                          /* of */
        js_object *out = js_array_new(J, (uint32_t)argc);
        js_push(J, js_obj(out));
        for (int i = 0; i < argc; i++) {
            if (!js_array_push(J, out, *js_arg(J, i)))
                return false;
        }
        return true;
    }
    }
}

/* --- String ---------------------------------------------------------------------------------------- */

static bool this_string(js_state *J, js_string **out)
{
    js_value *self = js_this(J);
    if (self->t <= JS_T_NULL)
        return js_throw(J, JS_E_TYPE, "String method called on null or undefined");
    if (self->t == JS_T_OBJ && self->u.o->cls == JS_C_STRING)
        *self = self->u.o->u.prim;
    if (!js_to_string(J, THISI))
        return false;
    *out = js_this(J)->u.s;
    return true;
}

static bool string_ctor(js_state *J, int argc)
{
    js_string *s = J->empty;
    if (argc > 0 && !arg_string(J, 0, &s))
        return false;
    if (!J->constructing)
        return js_push(J, js_str(s));
    js_object *o = js_object_new(J, JS_C_STRING, J->string_proto);
    o->u.prim = js_str(s);
    return js_push(J, js_obj(o));
}

/* Zeichen-Nummer -> Byte-Stelle umgekehrt: so viele Zeichen stehen vor der Byte-Stelle */
static uint32_t char_index(const js_string *s, uint32_t offset)
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

static bool push_slice(js_state *J, const js_string *s, uint32_t from, uint32_t to)
{
    if (to > s->chars)
        to = s->chars;
    if (from >= to)
        return js_push(J, js_str(J->empty));
    uint32_t a = js_string_offset(s, from), b = js_string_offset(s, to);
    return js_push_lstr(J, s->data + a, b - a);
}

static int32_t find_bytes(const js_string *s, const js_string *needle, uint32_t from)
{
    if (needle->len > s->len)
        return -1;
    for (uint32_t i = from; i + needle->len <= s->len; i++) {
        if (memcmp(s->data + i, needle->data, needle->len) == 0)
            return (int32_t)i;
    }
    return -1;
}

static bool string_char(js_state *J, int argc)
{
    js_string *s;
    double pos;
    if (!this_string(J, &s) || !arg_int(J, 0, argc, 0, &pos))
        return false;
    int kind = MAGIC;                   /* 0 charAt, 1 charCodeAt, 2 codePointAt, 3 at */
    if (kind == 3 && pos < 0)
        pos += s->chars;
    if (pos < 0 || pos >= s->chars)
        return js_push(J, kind == 0 ? js_str(J->empty) : kind == 1 ? js_num(js_nan()) : js_undef());
    uint32_t at = js_string_offset(s, (uint32_t)pos);
    int size;
    uint32_t c = js_utf8_decode(s->data + at, s->data + s->len, &size);
    if (kind == 0 || kind == 3)
        return js_push_lstr(J, s->data + at, (size_t)size);
    return js_push(J, js_num(c));
}

static bool string_index_of(js_state *J, int argc)
{
    js_string *s, *needle;
    if (!this_string(J, &s) || !arg_string(J, 0, &needle))
        return false;
    int kind = MAGIC;                   /* 0 indexOf, 1 lastIndexOf, 2 includes, 3 startsWith, 4 endsWith */
    double pos;
    if (!arg_int(J, 1, argc, kind == 1 || kind == 4 ? s->chars : 0, &pos))
        return false;
    uint32_t at = pos < 0 ? 0 : pos > s->chars ? s->chars : (uint32_t)pos;
    uint32_t offset = js_string_offset(s, at);
    switch (kind) {
    case 0: case 2: {
        int32_t found = find_bytes(s, needle, offset);
        if (kind == 2)
            return js_push(J, js_bool(found >= 0));
        return js_push(J, js_num(found < 0 ? -1 : (double)char_index(s, (uint32_t)found)));
    }
    case 1: {
        for (int64_t i = offset; i >= 0; i--) {
            if ((uint64_t)i + needle->len <= s->len &&
                memcmp(s->data + i, needle->data, needle->len) == 0)
                return js_push(J, js_num(char_index(s, (uint32_t)i)));
        }
        return js_push(J, js_num(-1));
    }
    case 3:
        return js_push(J, js_bool(offset + needle->len <= s->len &&
                                  memcmp(s->data + offset, needle->data, needle->len) == 0));
    default:
        return js_push(J, js_bool(needle->len <= offset &&
                                  memcmp(s->data + offset - needle->len, needle->data,
                                         needle->len) == 0));
    }
}

static bool string_slice(js_state *J, int argc)
{
    js_string *s;
    double a, b;
    if (!this_string(J, &s) || !arg_int(J, 0, argc, 0, &a) || !arg_int(J, 1, argc, s->chars, &b))
        return false;
    double len = s->chars;
    switch (MAGIC) {
    case 0:                             /* slice */
        if (a < 0) a += len;
        if (b < 0) b += len;
        break;
    case 1:                             /* substring */
        if (a > b) {
            double t = a;
            a = b;
            b = t;
        }
        break;
    default:                            /* substr(start, länge) */
        if (a < 0) a += len;
        if (a < 0) a = 0;
        b = argc > 1 && js_arg(J, 1)->t != JS_T_UNDEF ? a + (b < 0 ? 0 : b) : len;
        break;
    }
    if (a < 0) a = 0;
    if (b < 0) b = 0;
    if (a > len) a = len;
    if (b > len) b = len;
    return push_slice(J, s, (uint32_t)a, (uint32_t)b);
}

/* Groß- und Kleinbuchstaben: ASCII, Latin-1, Griechisch, Kyrillisch */
static uint32_t change_case(uint32_t c, bool upper)
{
    if (upper) {
        if (c >= 'a' && c <= 'z') return c - 32;
        if ((c >= 0xE0 && c <= 0xFE && c != 0xF7) || (c >= 0x3B1 && c <= 0x3C9 && c != 0x3C2) ||
            (c >= 0x430 && c <= 0x44F))
            return c - 32;
        if (c >= 0x450 && c <= 0x45F) return c - 80;
        if (c == 0xFF) return 0x178;
        if (c >= 0x100 && c <= 0x17F && c != 0x131 && c != 0x138 && c != 0x149 && (c & 1) &&
            !(c >= 0x139 && c <= 0x148))
            return c - 1;
        return c;
    }
    if (c >= 'A' && c <= 'Z') return c + 32;
    if ((c >= 0xC0 && c <= 0xDE && c != 0xD7) || (c >= 0x391 && c <= 0x3A9 && c != 0x3A2) ||
        (c >= 0x410 && c <= 0x42F))
        return c + 32;
    if (c >= 0x400 && c <= 0x40F) return c + 80;
    if (c >= 0x100 && c <= 0x17F && c != 0x130 && !(c & 1) && !(c >= 0x139 && c <= 0x148))
        return c + 1;
    return c;
}

static bool string_case(js_state *J, int argc)
{
    (void)argc;
    js_string *s;
    if (!this_string(J, &s))
        return false;
    bool upper = MAGIC == 1;
    /* "ß" wird groß zu "SS": deshalb etwas Luft */
    size_t cap = (size_t)s->len * 2 + 4;
    char *out = js_alloc(J, cap);
    if (!out)
        return false;
    size_t n = 0;
    for (uint32_t i = 0; i < s->len;) {
        int size;
        uint32_t c = js_utf8_decode(s->data + i, s->data + s->len, &size);
        i += (uint32_t)size;
        if (upper && c == 0xDF) {
            out[n++] = 'S';
            out[n++] = 'S';
            continue;
        }
        n += (size_t)js_utf8_encode(change_case(c, upper), out + n);
    }
    js_string *r = js_string_new(J, out, n);
    js_release(J, out, cap);
    return js_push(J, js_str(r));
}

static bool is_blank(const char *p, const char *end, int *size)
{
    unsigned char c = (unsigned char)*p;
    *size = 1;
    if (c == ' ' || (c >= 9 && c <= 13))
        return true;
    if (c == 0xC2 && end - p >= 2 && (unsigned char)p[1] == 0xA0) {
        *size = 2;
        return true;
    }
    if (c == 0xEF && end - p >= 3 && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) {
        *size = 3;
        return true;
    }
    if (c == 0xE2 && end - p >= 3 && (unsigned char)p[1] == 0x80 &&
        (((unsigned char)p[2] >= 0x80 && (unsigned char)p[2] <= 0x8A) ||
         (unsigned char)p[2] == 0xA8 || (unsigned char)p[2] == 0xA9 || (unsigned char)p[2] == 0xAF)) {
        *size = 3;
        return true;
    }
    return false;
}

static bool string_trim(js_state *J, int argc)
{
    (void)argc;
    js_string *s;
    if (!this_string(J, &s))
        return false;
    const char *a = s->data, *b = s->data + s->len;
    int size;
    if (MAGIC != 2) {
        while (a < b && is_blank(a, b, &size))
            a += size;
    }
    if (MAGIC != 1) {
        for (;;) {
            /* rückwärts: Anfang des letzten Zeichens suchen */
            const char *q = b;
            if (q <= a)
                break;
            q--;
            while (q > a && ((unsigned char)*q & 0xC0) == 0x80)
                q--;
            if (!is_blank(q, b, &size) || q + size != b)
                break;
            b = q;
        }
    }
    return js_push_lstr(J, a, (size_t)(b - a));
}

static bool string_pad(js_state *J, int argc)
{
    js_string *s, *fill;
    double want;
    if (!this_string(J, &s) || !arg_int(J, 0, argc, 0, &want))
        return false;
    if (argc > 1 && js_arg(J, 1)->t != JS_T_UNDEF) {
        if (!arg_string(J, 1, &fill))
            return false;
    } else {
        fill = js_string_new(J, " ", 1);
        js_push(J, js_str(fill));
    }
    if (want <= s->chars || fill->len == 0)
        return js_push(J, js_str(s));
    if (want > 1000000)
        return js_throw(J, JS_E_RANGE, "Invalid string length");
    uint32_t missing = (uint32_t)want - s->chars;
    js_push(J, js_str(J->empty));
    int acc = J->top - 1;
    while (J->stack[acc].u.s->chars < missing && !J->fatal)
        J->stack[acc] = js_str(js_string_concat(J, J->stack[acc].u.s, fill));
    if (!push_slice(J, J->stack[acc].u.s, 0, missing))
        return false;
    js_string *pad = js_top(J, 0)->u.s;
    return js_push(J, js_str(MAGIC == 0 ? js_string_concat(J, pad, s)
                                        : js_string_concat(J, s, pad)));
}

static bool string_repeat(js_state *J, int argc)
{
    js_string *s;
    double count;
    if (!this_string(J, &s) || !arg_int(J, 0, argc, 0, &count))
        return false;
    if (count < 0 || count * (s->len ? s->len : 1) > JS_STRING_MAX)
        return js_throw(J, JS_E_RANGE, "Invalid count value");
    size_t total = (size_t)count * s->len;
    if (total == 0)
        return js_push(J, js_str(J->empty));
    char *out = js_alloc(J, total);
    if (!out)
        return false;
    for (size_t i = 0; i < (size_t)count; i++)
        memcpy(out + i * s->len, s->data, s->len);
    js_string *r = js_string_new(J, out, total);
    js_release(J, out, total);
    return js_push(J, js_str(r));
}

static bool string_concat(js_state *J, int argc)
{
    js_string *s;
    if (!this_string(J, &s))
        return false;
    js_push(J, js_str(s));
    for (int i = 0; i < argc; i++) {
        js_string *part;
        if (!arg_string(J, i, &part))
            return false;
        *js_top(J, 0) = js_str(js_string_concat(J, js_top(J, 0)->u.s, part));
    }
    return true;
}

static bool string_misc(js_state *J, int argc)
{
    js_string *s, *other;
    if (!this_string(J, &s))
        return false;
    switch (MAGIC) {
    case 0:                             /* toString, valueOf, normalize */
        return js_push(J, js_str(s));
    default:                            /* localeCompare */
        if (!arg_string(J, 0, &other))
            return false;
        (void)argc;
        return js_push(J, js_num(js_string_compare(s, other) < 0 ? -1
                                 : js_string_compare(s, other) > 0 ? 1 : 0));
    }
}

static bool string_from_char_code(js_state *J, int argc)
{
    size_t cap = (size_t)argc * 4 + 1, n = 0;
    char *out = js_alloc(J, cap);
    if (!out)
        return false;
    uint32_t pending = 0;               /* erste Hälfte eines Ersatzpaars */
    for (int i = 0; i < argc; i++) {
        double d;
        if (!js_to_number(J, ARGI(i), &d)) {
            js_release(J, out, cap);
            return false;
        }
        uint32_t c = MAGIC == 0 ? (uint32_t)js_double_to_int32(d) & 0xFFFF
                                : d >= 0 && d <= 0x10FFFF ? (uint32_t)d : 0xFFFD;
        if (pending) {
            if (c >= 0xDC00 && c <= 0xDFFF) {
                c = 0x10000 + ((pending - 0xD800) << 10) + (c - 0xDC00);
                pending = 0;
            } else {
                n += (size_t)js_utf8_encode(0xFFFD, out + n);
                pending = 0;
            }
        }
        if (MAGIC == 0 && c >= 0xD800 && c <= 0xDBFF) {
            pending = c;
            continue;
        }
        n += (size_t)js_utf8_encode(c, out + n);
    }
    if (pending)
        n += (size_t)js_utf8_encode(0xFFFD, out + n);
    js_string *r = js_string_new(J, out, n);
    js_release(J, out, cap);
    return js_push(J, js_str(r));
}

/* --- Number, Boolean ------------------------------------------------------------------------------- */

static bool this_number(js_state *J, double *out)
{
    js_value *self = js_this(J);
    if (self->t == JS_T_NUM) {
        *out = self->u.n;
        return true;
    }
    if (self->t == JS_T_OBJ && self->u.o->cls == JS_C_NUMBER) {
        *out = self->u.o->u.prim.u.n;
        return true;
    }
    return js_throw(J, JS_E_TYPE, "Number method called on a value that is not a number");
}

static bool number_ctor(js_state *J, int argc)
{
    double n = 0;
    if (argc > 0 && !js_to_number(J, ARGI(0), &n))
        return false;
    if (!J->constructing)
        return js_push(J, js_num(n));
    js_object *o = js_object_new(J, JS_C_NUMBER, J->number_proto);
    o->u.prim = js_num(n);
    return js_push(J, js_obj(o));
}

static bool number_to_string(js_state *J, int argc)
{
    double n, digits;
    if (!this_number(J, &n))
        return false;
    char text[1300];
    int len;
    switch (MAGIC) {
    case 0:                             /* toString(radix) */
        if (!arg_int(J, 0, argc, 10, &digits))
            return false;
        if (digits < 2 || digits > 36)
            return js_throw(J, JS_E_RANGE, "toString() radix must be between 2 and 36");
        len = js_number_to_radix(n, (int)digits, text, sizeof(text));
        break;
    case 1:                             /* toFixed */
        if (!arg_int(J, 0, argc, 0, &digits))
            return false;
        if (digits < 0 || digits > 100)
            return js_throw(J, JS_E_RANGE, "toFixed() digits argument must be between 0 and 100");
        len = js_number_fixed(n, (int)digits, text, sizeof(text));
        break;
    case 2:                             /* toPrecision */
        if (argc == 0 || js_arg(J, 0)->t == JS_T_UNDEF) {
            len = js_number_to_text(n, text);
            break;
        }
        if (!arg_int(J, 0, argc, 0, &digits))
            return false;
        if (digits < 1 || digits > 100)
            return js_throw(J, JS_E_RANGE, "toPrecision() argument must be between 1 and 100");
        len = js_number_precision(n, (int)digits, false, text, sizeof(text));
        break;
    case 3:                             /* toExponential */
        if (!arg_int(J, 0, argc, 6, &digits))
            return false;
        if (digits < 0 || digits > 99)
            return js_throw(J, JS_E_RANGE, "toExponential() argument must be between 0 and 100");
        len = js_number_precision(n, (int)digits + 1, true, text, sizeof(text));
        break;
    case 4:                             /* valueOf */
        return js_push(J, js_num(n));
    default: {                          /* toLocaleString: deutsch, mit Tausenderpunkten */
        char plain[140];
        double whole = js_trunc(n);
        bool fraction = n != whole;
        int k = js_number_fixed(n, fraction ? 3 : 0, plain, sizeof(plain));
        while (fraction && k > 0 && plain[k - 1] == '0')
            k--;
        if (k > 0 && plain[k - 1] == '.')
            k--;
        int dot = k;
        for (int i = 0; i < k; i++) {
            if (plain[i] == '.')
                dot = i;
        }
        len = 0;
        for (int i = 0; i < k; i++) {
            if (i == dot) {
                text[len++] = ',';
                continue;
            }
            text[len++] = plain[i];
            int left = dot - 1 - i;
            if (i < dot && left > 0 && left % 3 == 0 && plain[i] != '-')
                text[len++] = '.';
        }
        break;
    }
    }
    return js_push_lstr(J, text, (size_t)len);
}

static bool number_static(js_state *J, int argc)
{
    (void)argc;
    js_value *v = js_arg(J, 0);
    double n = v->t == JS_T_NUM ? v->u.n : js_nan();
    bool number = v->t == JS_T_NUM, whole = number && js_isfinite(n) && js_trunc(n) == n;
    switch (MAGIC) {
    case 0: return js_push(J, js_bool(number && js_isnan(n)));
    case 1: return js_push(J, js_bool(number && js_isfinite(n)));
    case 2: return js_push(J, js_bool(whole));
    default: return js_push(J, js_bool(whole && n >= -9007199254740991.0 && n <= 9007199254740991.0));
    }
}

static bool boolean_ctor(js_state *J, int argc)
{
    bool b = argc > 0 && js_truthy(js_arg(J, 0));
    if (!J->constructing)
        return js_push(J, js_bool(b));
    js_object *o = js_object_new(J, JS_C_BOOLEAN, J->boolean_proto);
    o->u.prim = js_bool(b);
    return js_push(J, js_obj(o));
}

static bool boolean_proto_fn(js_state *J, int argc)
{
    (void)argc;
    js_value *self = js_this(J);
    bool b;
    if (self->t == JS_T_BOOL)
        b = self->u.b;
    else if (self->t == JS_T_OBJ && self->u.o->cls == JS_C_BOOLEAN)
        b = self->u.o->u.prim.u.b;
    else
        return js_throw(J, JS_E_TYPE, "Boolean method called on a value that is not a boolean");
    return MAGIC == 0 ? js_push(J, js_str(J->k[b ? K_true : K_false])) : js_push(J, js_bool(b));
}

/* --- Math ------------------------------------------------------------------------------------------ */

enum { M_ABS, M_FLOOR, M_CEIL, M_ROUND, M_TRUNC, M_SIGN, M_SQRT, M_CBRT, M_EXP, M_LOG, M_LOG2,
       M_LOG10, M_SIN, M_COS, M_TAN, M_ASIN, M_ACOS, M_ATAN, M_FROUND, M_EXPM1, M_LOG1P };

static bool math_one(js_state *J, int argc)
{
    double x, r;
    if (!arg_number(J, 0, argc, &x))
        return false;
    switch (MAGIC) {
    case M_ABS: r = x < 0 ? -x : x == 0 ? 0 : x; break;
    case M_FLOOR: r = js_floor(x); break;
    case M_CEIL: r = js_ceil(x); break;
    case M_ROUND: r = js_isfinite(x) ? js_floor(x + 0.5) : x; break;
    case M_TRUNC: r = js_trunc(x); break;
    case M_SIGN: r = x > 0 ? 1 : x < 0 ? -1 : x; break;
    case M_SQRT: r = x < 0 ? js_nan() : __builtin_sqrt(x); break;
    case M_CBRT: r = x < 0 ? -js_pow(-x, 1.0 / 3) : js_pow(x, 1.0 / 3); break;
    case M_EXP: r = js_exp(x); break;
    case M_EXPM1: r = js_exp(x) - 1; break;
    case M_LOG: r = js_log(x); break;
    case M_LOG1P: r = js_log(1 + x); break;
    case M_LOG2: r = js_log(x) / 0.6931471805599453; break;
    case M_LOG10: r = js_log(x) / 2.302585092994046; break;
    case M_SIN: r = js_sin(x); break;
    case M_COS: r = js_cos(x); break;
    case M_TAN: r = js_tan(x); break;
    case M_ASIN: r = js_asin(x); break;
    case M_ACOS: r = 1.5707963267948966 - js_asin(x); break;
    case M_ATAN: r = js_atan(x); break;
    default: r = (double)(float)x; break;
    }
    return js_push(J, js_num(r));
}

static bool math_many(js_state *J, int argc)
{
    double x, y;
    switch (MAGIC) {
    case 0: case 1: {                   /* min, max */
        double best = MAGIC == 0 ? js_infinity() : -js_infinity();
        bool nan = false;
        for (int i = 0; i < argc; i++) {
            if (!js_to_number(J, ARGI(i), &x))
                return false;
            if (js_isnan(x))
                nan = true;
            else if (MAGIC == 0 ? x < best : x > best)
                best = x;
        }
        return js_push(J, js_num(nan ? js_nan() : best));
    }
    case 2:                             /* pow */
        if (!arg_number(J, 0, argc, &x) || !arg_number(J, 1, argc, &y))
            return false;
        return js_push(J, js_num(js_pow(x, y)));
    case 3:                             /* atan2 */
        if (!arg_number(J, 0, argc, &x) || !arg_number(J, 1, argc, &y))
            return false;
        return js_push(J, js_num(js_atan2(x, y)));
    case 4: {                           /* random */
        uint64_t bits = 0;
        for (int i = 0; i < 2; i++) {
            J->rng ^= J->rng << 13;
            J->rng ^= J->rng >> 17;
            J->rng ^= J->rng << 5;
            bits = (bits << 32) | J->rng;
        }
        return js_push(J, js_num((double)(bits >> 11) / 9007199254740992.0));
    }
    default: {                          /* hypot */
        double sum = 0;
        for (int i = 0; i < argc; i++) {
            if (!js_to_number(J, ARGI(i), &x))
                return false;
            sum += x * x;
        }
        return js_push(J, js_num(__builtin_sqrt(sum)));
    }
    }
}

/* --- Fehler ---------------------------------------------------------------------------------------- */

static bool error_ctor(js_state *J, int argc)
{
    int kind = MAGIC;
    js_object *e = js_object_new(J, JS_C_ERROR, J->error_protos[kind]);
    js_push(J, js_obj(e));
    if (argc > 0 && js_arg(J, 0)->t != JS_T_UNDEF) {
        js_string *text;
        if (!arg_string(J, 0, &text))
            return false;
        js_define(J, e, J->k[K_message], js_str(text), JS_P_WRITE | JS_P_CONF);
    }
    if (argc > 1 && js_arg(J, 1)->t == JS_T_OBJ) {
        js_prop *cause = js_lookup(js_arg(J, 1)->u.o, J->k[K_cause], NULL);
        if (cause && !(cause->flags & JS_P_ACCESSOR))
            js_define(J, e, J->k[K_cause], cause->v, JS_P_WRITE | JS_P_CONF);
    }
    js_define(J, e, J->k[K_line], js_num(J->line), JS_P_WRITE | JS_P_CONF);
    js_define(J, e, J->k[K_stack], js_str(J->empty), JS_P_WRITE | JS_P_CONF);
    return true;
}

static bool error_to_string(js_state *J, int argc)
{
    (void)argc;
    if (js_this(J)->t != JS_T_OBJ)
        return js_throw(J, JS_E_TYPE, "Error.prototype.toString requires an object");
    if (!js_get(J, THISI, J->k[K_name]))
        return false;
    if (js_top(J, 0)->t == JS_T_UNDEF)
        *js_top(J, 0) = js_str(js_intern_cstr(J, "Error"));
    if (!js_to_string(J, J->top - 1) || !js_get(J, THISI, J->k[K_message]))
        return false;
    if (js_top(J, 0)->t == JS_T_UNDEF)
        *js_top(J, 0) = js_str(J->empty);
    if (!js_to_string(J, J->top - 1))
        return false;
    js_string *name = js_top(J, 1)->u.s, *text = js_top(J, 0)->u.s;
    if (text->len == 0)
        return js_push(J, js_str(name));
    if (name->len == 0)
        return js_push(J, js_str(text));
    js_string *sep = js_string_new(J, ": ", 2);
    js_push(J, js_str(sep));
    js_string *head = js_string_concat(J, name, sep);
    js_push(J, js_str(head));
    return js_push(J, js_str(js_string_concat(J, head, text)));
}

/* --- Globale Funktionen ------------------------------------------------------------------------------ */

static bool global_parse_int(js_state *J, int argc)
{
    js_string *s;
    double radix;
    if (!arg_string(J, 0, &s) || !arg_int(J, 1, argc, 0, &radix))
        return false;
    const char *p = s->data, *end = s->data + s->len;
    int size;
    while (p < end && is_blank(p, end, &size))
        p += size;
    bool negative = false;
    if (p < end && (*p == '+' || *p == '-'))
        negative = *p++ == '-';
    int base = (int)js_double_to_int32(radix);
    if (base == 0)
        base = 10;
    if (base < 2 || base > 36)
        return js_push(J, js_num(js_nan()));
    if ((base == 16 || radix == 0) && end - p >= 2 && p[0] == '0' && (p[1] | 32) == 'x') {
        p += 2;
        base = 16;
    }
    double value = 0;
    bool any = false;
    for (; p < end; p++) {
        int d = *p >= '0' && *p <= '9' ? *p - '0'
              : (*p | 32) >= 'a' && (*p | 32) <= 'z' ? (*p | 32) - 'a' + 10 : 99;
        if (d >= base)
            break;
        value = value * base + d;
        any = true;
    }
    return js_push(J, js_num(!any ? js_nan() : negative ? -value : value));
}

static bool global_parse_float(js_state *J, int argc)
{
    (void)argc;
    js_string *s;
    if (!arg_string(J, 0, &s))
        return false;
    const char *p = s->data, *end = s->data + s->len;
    int size;
    while (p < end && is_blank(p, end, &size))
        p += size;
    const char *q = p;
    bool negative = false;
    if (q < end && (*q == '+' || *q == '-'))
        negative = *q++ == '-';
    if (end - q >= 8 && memcmp(q, "Infinity", 8) == 0)
        return js_push(J, js_num(negative ? -js_infinity() : js_infinity()));
    size_t used = 0;
    double value = js_text_to_number(p, (size_t)(end - p), &used);
    return js_push(J, js_num(used ? value : js_nan()));
}

static bool global_is_nan(js_state *J, int argc)
{
    double n;
    if (!arg_number(J, 0, argc, &n))
        return false;
    return js_push(J, js_bool(MAGIC == 0 ? js_isnan(n) : js_isfinite(n)));
}

/* encodeURIComponent (0), encodeURI (1), escape (2) */
static bool global_encode(js_state *J, int argc)
{
    (void)argc;
    static const char *const keep[3] = {
        "-_.!~*'()", "-_.!~*'();/?:@&=+$,#", "@*_+-./"
    };
    js_string *s;
    if (!arg_string(J, 0, &s))
        return false;
    size_t cap = (size_t)s->len * 3 + 1, n = 0;
    char *out = js_alloc(J, cap);
    if (!out)
        return false;
    for (uint32_t i = 0; i < s->len; i++) {
        unsigned char c = (unsigned char)s->data[i];
        if (MAGIC == 2 && c >= 0x80) {
            /* escape() rechnet in Zeichen: bis 255 als %XX, darüber als %uXXXX */
            int size;
            uint32_t code = js_utf8_decode(s->data + i, s->data + s->len, &size);
            static const char hex[] = "0123456789ABCDEF";
            i += (uint32_t)size - 1;
            if (code > 0xFFFF)
                code = 0xFFFD;
            out[n++] = '%';
            if (code > 0xFF) {
                out[n++] = 'u';
                out[n++] = hex[code >> 12];
                out[n++] = hex[(code >> 8) & 15];
            }
            out[n++] = hex[(code >> 4) & 15];
            out[n++] = hex[code & 15];
            continue;
        }
        bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        for (const char *k = keep[MAGIC]; *k && !plain; k++)
            plain = c == (unsigned char)*k;
        if (plain) {
            out[n++] = (char)c;
        } else {
            out[n++] = '%';
            out[n++] = "0123456789ABCDEF"[c >> 4];
            out[n++] = "0123456789ABCDEF"[c & 15];
        }
    }
    js_string *r = js_string_new(J, out, n);
    js_release(J, out, cap);
    return js_push(J, js_str(r));
}

static int hex_of(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if ((c | 32) >= 'a' && (c | 32) <= 'f') return (c | 32) - 'a' + 10;
    return -1;
}

/* decodeURIComponent (0), decodeURI (1), unescape (2) */
static bool global_decode(js_state *J, int argc)
{
    (void)argc;
    js_string *s;
    if (!arg_string(J, 0, &s))
        return false;
    size_t cap = (size_t)s->len + 4, n = 0;
    char *out = js_alloc(J, cap);
    if (!out)
        return false;
    for (uint32_t i = 0; i < s->len; i++) {
        char c = s->data[i];
        if (c == '%' && MAGIC == 2 && i + 5 < s->len && s->data[i + 1] == 'u') {
            uint32_t code = 0;
            bool ok = true;
            for (int k = 2; k < 6; k++) {
                int h = hex_of(s->data[i + (uint32_t)k]);
                ok &= h >= 0;
                code = code * 16 + (uint32_t)(h < 0 ? 0 : h);
            }
            if (ok && n + 4 <= cap) {
                n += (size_t)js_utf8_encode(code, out + n);
                i += 5;
                continue;
            }
        }
        if (c == '%' && i + 2 < s->len + 0 && hex_of(s->data[i + 1]) >= 0 &&
            hex_of(s->data[i + 2]) >= 0) {
            int byte = hex_of(s->data[i + 1]) * 16 + hex_of(s->data[i + 2]);
            if (MAGIC == 1 && byte < 0x80 && strlen(";/?:@&=+$,#") > 0) {
                bool reserved = false;
                for (const char *k = ";/?:@&=+$,#"; *k; k++)
                    reserved |= byte == *k;
                if (reserved) {
                    out[n++] = c;
                    continue;
                }
            }
            if (MAGIC == 2 && byte >= 0x80) {
                n += (size_t)js_utf8_encode((uint32_t)byte, out + n > out + cap - 4 ? out : out + n);
            } else {
                out[n++] = (char)byte;
            }
            i += 2;
            continue;
        }
        if (c == '%' && MAGIC != 2) {
            js_release(J, out, cap);
            return js_throw(J, JS_E_URI, "URI malformed");
        }
        out[n++] = c;
    }
    js_string *r = js_string_new(J, out, n);
    js_release(J, out, cap);
    return js_push(J, js_str(r));
}

static bool global_eval(js_state *J, int argc)
{
    if (argc == 0 || js_arg(J, 0)->t != JS_T_STR)
        return js_push(J, argc ? *js_arg(J, 0) : js_undef());
    js_string *source = js_arg(J, 0)->u.s;
    js_fninfo *program = js_parse(J, source->data, source->len, "eval");
    if (!program)
        return false;
    return js_exec_program(J, program);
}

/* --- console --------------------------------------------------------------------------------------- */

typedef struct {
    char *text;
    size_t len, cap;
} inspect_t;

static void inspect_put(inspect_t *out, const char *text, size_t n)
{
    if (out->len + n >= out->cap)
        n = out->cap > out->len + 1 ? out->cap - out->len - 1 : 0;
    memcpy(out->text + out->len, text, n);
    out->len += n;
}

static void inspect(js_state *J, const js_value *v, inspect_t *out, int depth, bool top)
{
    char number[48];
    switch (v->t) {
    case JS_T_UNDEF: inspect_put(out, "undefined", 9); return;
    case JS_T_NULL: inspect_put(out, "null", 4); return;
    case JS_T_BOOL: inspect_put(out, v->u.b ? "true" : "false", v->u.b ? 4 : 5); return;
    case JS_T_NUM: inspect_put(out, number, (size_t)js_number_to_text(v->u.n, number)); return;
    case JS_T_STR:
        if (!top)
            inspect_put(out, "\"", 1);
        inspect_put(out, v->u.s->data, v->u.s->len);
        if (!top)
            inspect_put(out, "\"", 1);
        return;
    default:
        break;
    }
    js_object *o = v->u.o;
    if (o->cls == JS_C_FUNCTION || o->cls == JS_C_NATIVE) {
        inspect_put(out, "[Function]", 10);
        return;
    }
    if (o->cls == JS_C_ERROR) {
        js_prop *name = js_lookup(o, J->k[K_name], NULL), *text = js_lookup(o, J->k[K_message], NULL);
        if (name && name->v.t == JS_T_STR)
            inspect_put(out, name->v.u.s->data, name->v.u.s->len);
        inspect_put(out, ": ", 2);
        if (text && text->v.t == JS_T_STR)
            inspect_put(out, text->v.u.s->data, text->v.u.s->len);
        return;
    }
    if (depth > 3 || out->len + 40 > out->cap) {
        inspect_put(out, o->cls == JS_C_ARRAY ? "[...]" : "{...}", 5);
        return;
    }
    if (o->cls == JS_C_ARRAY) {
        inspect_put(out, "[", 1);
        for (uint32_t i = 0; i < o->u.arr.len && i < 100; i++) {
            if (i)
                inspect_put(out, ", ", 2);
            inspect(J, &o->u.arr.v[i], out, depth + 1, false);
        }
        inspect_put(out, "]", 1);
        return;
    }
    if (o->cls == JS_C_HOST) {
        inspect_put(out, "[object ", 8);
        const char *name = class_name(v);
        inspect_put(out, name, strlen(name));
        inspect_put(out, "]", 1);
        return;
    }
    inspect_put(out, "{", 1);
    bool first = true;
    for (uint32_t i = 0; i < o->count && i < 100; i++) {
        js_prop *p = &o->props[i];
        if (!p->key || !(p->flags & JS_P_ENUM))
            continue;
        inspect_put(out, first ? " " : ", ", first ? 1 : 2);
        first = false;
        inspect_put(out, p->key->data, p->key->len);
        inspect_put(out, ": ", 2);
        if (p->flags & JS_P_ACCESSOR)
            inspect_put(out, "[Getter]", 8);
        else
            inspect(J, &p->v, out, depth + 1, false);
    }
    inspect_put(out, first ? "}" : " }", first ? 1 : 2);
}

static bool console_log(js_state *J, int argc)
{
    char text[1000];
    inspect_t out = { text, 0, sizeof(text) };
    for (int i = 0; i < argc; i++) {
        if (i)
            inspect_put(&out, " ", 1);
        inspect(J, js_arg(J, i), &out, 0, true);
    }
    text[out.len] = '\0';
    if (J->host.print)
        J->host.print(J->host.ctx, text);
    return js_push(J, js_undef());
}

/* --- Einrichten ------------------------------------------------------------------------------------ */

static const fndef object_statics[] = {
    { "keys", object_static, 1, 0 }, { "values", object_static, 1, 1 },
    { "entries", object_static, 1, 2 }, { "getOwnPropertyNames", object_static, 1, 3 },
    { "assign", object_static, 2, 4 }, { "create", object_static, 2, 5 },
    { "getPrototypeOf", object_static, 1, 6 }, { "setPrototypeOf", object_static, 2, 7 },
    { "defineProperty", object_static, 3, 8 }, { "defineProperties", object_static, 2, 9 },
    { "getOwnPropertyDescriptor", object_static, 2, 10 }, { "freeze", object_static, 1, 11 },
    { "seal", object_static, 1, 12 }, { "preventExtensions", object_static, 1, 13 },
    { "isFrozen", object_static, 1, 14 }, { "isSealed", object_static, 1, 15 },
    { "isExtensible", object_static, 1, 16 }, { "is", object_static, 2, 17 },
    { "fromEntries", object_static, 1, 18 },
};

static const fndef object_methods[] = {
    { "hasOwnProperty", object_proto_fn, 1, 0 }, { "isPrototypeOf", object_proto_fn, 1, 1 },
    { "propertyIsEnumerable", object_proto_fn, 1, 2 }, { "valueOf", object_proto_fn, 0, 3 },
    { "toLocaleString", object_proto_fn, 0, 4 }, { "toString", object_to_string, 0, 0 },
};

static const fndef function_methods[] = {
    { "call", function_call, 1, 0 }, { "apply", function_apply, 2, 0 },
    { "bind", function_bind, 1, 0 }, { "toString", function_to_string, 0, 0 },
};

static const fndef array_methods[] = {
    { "push", array_push, 1, 0 }, { "pop", array_pop_shift, 0, 0 },
    { "shift", array_pop_shift, 0, 1 }, { "unshift", array_unshift, 1, 0 },
    { "slice", array_slice, 2, 0 }, { "splice", array_splice, 2, 0 },
    { "concat", array_concat, 1, 0 }, { "join", array_join, 1, 0 },
    { "toString", array_join, 0, 1 }, { "toLocaleString", array_join, 0, 1 },
    { "reverse", array_reverse, 0, 0 }, { "indexOf", array_index_of, 1, 0 },
    { "lastIndexOf", array_index_of, 1, 1 }, { "includes", array_index_of, 1, 2 },
    { "forEach", array_iterate, 1, IT_FOREACH }, { "map", array_iterate, 1, IT_MAP },
    { "filter", array_iterate, 1, IT_FILTER }, { "some", array_iterate, 1, IT_SOME },
    { "every", array_iterate, 1, IT_EVERY }, { "find", array_iterate, 1, IT_FIND },
    { "findIndex", array_iterate, 1, IT_FIND_INDEX }, { "findLast", array_iterate, 1, IT_FIND_LAST },
    { "findLastIndex", array_iterate, 1, IT_FIND_LAST_INDEX },
    { "flatMap", array_iterate, 1, IT_FLAT_MAP }, { "reduce", array_reduce, 1, 0 },
    { "reduceRight", array_reduce, 1, 1 }, { "fill", array_fill, 1, 0 },
    { "sort", array_sort, 1, 0 }, { "flat", array_misc, 0, 0 }, { "at", array_misc, 1, 1 },
    { "keys", array_misc, 0, 2 }, { "values", array_misc, 0, 3 }, { "entries", array_misc, 0, 4 },
};

static const fndef string_methods[] = {
    { "charAt", string_char, 1, 0 }, { "charCodeAt", string_char, 1, 1 },
    { "codePointAt", string_char, 1, 2 }, { "at", string_char, 1, 3 },
    { "indexOf", string_index_of, 1, 0 }, { "lastIndexOf", string_index_of, 1, 1 },
    { "includes", string_index_of, 1, 2 }, { "startsWith", string_index_of, 1, 3 },
    { "endsWith", string_index_of, 1, 4 }, { "slice", string_slice, 2, 0 },
    { "substring", string_slice, 2, 1 }, { "substr", string_slice, 2, 2 },
    { "toLowerCase", string_case, 0, 0 }, { "toUpperCase", string_case, 0, 1 },
    { "toLocaleLowerCase", string_case, 0, 0 }, { "toLocaleUpperCase", string_case, 0, 1 },
    { "trim", string_trim, 0, 0 }, { "trimStart", string_trim, 0, 1 },
    { "trimEnd", string_trim, 0, 2 }, { "trimLeft", string_trim, 0, 1 },
    { "trimRight", string_trim, 0, 2 }, { "padStart", string_pad, 2, 0 },
    { "padEnd", string_pad, 2, 1 }, { "repeat", string_repeat, 1, 0 },
    { "concat", string_concat, 1, 0 }, { "toString", string_misc, 0, 0 },
    { "valueOf", string_misc, 0, 0 }, { "normalize", string_misc, 0, 0 },
    { "localeCompare", string_misc, 1, 1 },
};

static const fndef number_methods[] = {
    { "toString", number_to_string, 1, 0 }, { "toFixed", number_to_string, 1, 1 },
    { "toPrecision", number_to_string, 1, 2 }, { "toExponential", number_to_string, 1, 3 },
    { "valueOf", number_to_string, 0, 4 }, { "toLocaleString", number_to_string, 0, 5 },
};

static const fndef math_functions[] = {
    { "abs", math_one, 1, M_ABS }, { "floor", math_one, 1, M_FLOOR }, { "ceil", math_one, 1, M_CEIL },
    { "round", math_one, 1, M_ROUND }, { "trunc", math_one, 1, M_TRUNC },
    { "sign", math_one, 1, M_SIGN }, { "sqrt", math_one, 1, M_SQRT }, { "cbrt", math_one, 1, M_CBRT },
    { "exp", math_one, 1, M_EXP }, { "expm1", math_one, 1, M_EXPM1 }, { "log", math_one, 1, M_LOG },
    { "log1p", math_one, 1, M_LOG1P }, { "log2", math_one, 1, M_LOG2 },
    { "log10", math_one, 1, M_LOG10 }, { "sin", math_one, 1, M_SIN }, { "cos", math_one, 1, M_COS },
    { "tan", math_one, 1, M_TAN }, { "asin", math_one, 1, M_ASIN }, { "acos", math_one, 1, M_ACOS },
    { "atan", math_one, 1, M_ATAN }, { "fround", math_one, 1, M_FROUND },
    { "min", math_many, 0, 0 }, { "max", math_many, 0, 1 }, { "pow", math_many, 2, 2 },
    { "atan2", math_many, 2, 3 }, { "random", math_many, 0, 4 }, { "hypot", math_many, 0, 5 },
};

static const fndef global_functions[] = {
    { "parseInt", global_parse_int, 2, 0 }, { "parseFloat", global_parse_float, 1, 0 },
    { "isNaN", global_is_nan, 1, 0 }, { "isFinite", global_is_nan, 1, 1 },
    { "encodeURIComponent", global_encode, 1, 0 }, { "encodeURI", global_encode, 1, 1 },
    { "escape", global_encode, 1, 2 }, { "decodeURIComponent", global_decode, 1, 0 },
    { "decodeURI", global_decode, 1, 1 }, { "unescape", global_decode, 1, 2 },
    { "eval", global_eval, 1, 0 },
};

void js_lib_init(js_state *J)
{
    js_object *G = J->global;

    /* Object und Function */
    js_object *object = def_constructor(J, "Object", object_ctor, 1, J->object_proto);
    def_functions(J, object, object_statics, COUNT(object_statics));
    def_functions(J, J->object_proto, object_methods, COUNT(object_methods));
    js_def_accessor(J, J->object_proto, "__proto__", proto_get, proto_set, 0);
    js_prop *hidden = js_own(J->object_proto, js_intern_cstr(J, "__proto__"));
    if (hidden)
        hidden->flags &= (uint8_t)~JS_P_ENUM;

    /* Function.prototype ist selbst aufrufbar - als gewöhnliches Objekt genügt es hier */
    def_constructor(J, "Function", function_ctor, 1, J->function_proto);
    def_functions(J, J->function_proto, function_methods, COUNT(function_methods));
    (void)function_proto_call;

    /* Array */
    J->array_proto = js_object_new(J, JS_C_OBJECT, J->object_proto);
    js_object *array = def_constructor(J, "Array", array_ctor, 1, J->array_proto);
    def_functions(J, J->array_proto, array_methods, COUNT(array_methods));
    js_def_native(J, array, "isArray", array_static, 1, 0);
    js_def_native(J, array, "from", array_static, 1, 1);
    js_def_native(J, array, "of", array_static, 0, 2);

    /* String */
    J->string_proto = js_object_new(J, JS_C_STRING, J->object_proto);
    J->string_proto->u.prim = js_str(J->empty);
    js_object *string = def_constructor(J, "String", string_ctor, 1, J->string_proto);
    def_functions(J, J->string_proto, string_methods, COUNT(string_methods));
    js_def_native(J, string, "fromCharCode", string_from_char_code, 1, 0);
    js_def_native(J, string, "fromCodePoint", string_from_char_code, 1, 1);

    /* Number */
    J->number_proto = js_object_new(J, JS_C_NUMBER, J->object_proto);
    J->number_proto->u.prim = js_num(0);
    js_object *number = def_constructor(J, "Number", number_ctor, 1, J->number_proto);
    def_functions(J, J->number_proto, number_methods, COUNT(number_methods));
    js_def_native(J, number, "isNaN", number_static, 1, 0);
    js_def_native(J, number, "isFinite", number_static, 1, 1);
    js_def_native(J, number, "isInteger", number_static, 1, 2);
    js_def_native(J, number, "isSafeInteger", number_static, 1, 3);
    js_def_native(J, number, "parseFloat", global_parse_float, 1, 0);
    js_def_native(J, number, "parseInt", global_parse_int, 2, 0);
    js_def_value(J, number, "MAX_VALUE", js_num(1.7976931348623157e308), 0);
    js_def_value(J, number, "MIN_VALUE", js_num(4.9406564584124654e-324), 0);
    js_def_value(J, number, "EPSILON", js_num(2.220446049250313e-16), 0);
    js_def_value(J, number, "MAX_SAFE_INTEGER", js_num(9007199254740991.0), 0);
    js_def_value(J, number, "MIN_SAFE_INTEGER", js_num(-9007199254740991.0), 0);
    js_def_value(J, number, "POSITIVE_INFINITY", js_num(js_infinity()), 0);
    js_def_value(J, number, "NEGATIVE_INFINITY", js_num(-js_infinity()), 0);
    js_def_value(J, number, "NaN", js_num(js_nan()), 0);

    /* Boolean */
    J->boolean_proto = js_object_new(J, JS_C_BOOLEAN, J->object_proto);
    J->boolean_proto->u.prim = js_bool(false);
    def_constructor(J, "Boolean", boolean_ctor, 1, J->boolean_proto);
    js_def_native(J, J->boolean_proto, "toString", boolean_proto_fn, 0, 0);
    js_def_native(J, J->boolean_proto, "valueOf", boolean_proto_fn, 0, 1);

    /* Math */
    js_object *math = js_object_new(J, JS_C_OBJECT, J->object_proto);
    js_def_value(J, G, "Math", js_obj(math), JS_P_WRITE | JS_P_CONF);
    def_functions(J, math, math_functions, COUNT(math_functions));
    js_def_value(J, math, "PI", js_num(3.141592653589793), 0);
    js_def_value(J, math, "E", js_num(2.718281828459045), 0);
    js_def_value(J, math, "LN2", js_num(0.6931471805599453), 0);
    js_def_value(J, math, "LN10", js_num(2.302585092994046), 0);
    js_def_value(J, math, "LOG2E", js_num(1.4426950408889634), 0);
    js_def_value(J, math, "LOG10E", js_num(0.4342944819032518), 0);
    js_def_value(J, math, "SQRT2", js_num(1.4142135623730951), 0);
    js_def_value(J, math, "SQRT1_2", js_num(0.7071067811865476), 0);

    /* Fehlerarten */
    static const char *const names[] = { "Error", "TypeError", "RangeError", "ReferenceError",
                                         "SyntaxError", "URIError", "EvalError" };
    J->error_proto = js_object_new(J, JS_C_OBJECT, J->object_proto);
    for (int i = 0; i < 7; i++) {
        js_object *proto = i == 0 ? J->error_proto
                                  : js_object_new(J, JS_C_OBJECT, J->error_proto);
        J->error_protos[i] = proto;
        js_object *ctor = def_constructor(J, names[i], error_ctor, 1, proto);
        ctor->u.n.magic = i;
        js_define(J, proto, J->k[K_name], js_str(js_intern_cstr(J, names[i])),
                  JS_P_WRITE | JS_P_CONF);
        js_define(J, proto, J->k[K_message], js_str(J->empty), JS_P_WRITE | JS_P_CONF);
    }
    js_def_native(J, J->error_proto, "toString", error_to_string, 0, 0);

    /* Globales */
    def_functions(J, G, global_functions, COUNT(global_functions));
    js_def_value(J, G, "undefined", js_undef(), 0);
    js_def_value(J, G, "NaN", js_num(js_nan()), 0);
    js_def_value(J, G, "Infinity", js_num(js_infinity()), 0);
    js_def_value(J, G, "globalThis", js_obj(G), JS_P_WRITE | JS_P_CONF);

    js_object *console = js_object_new(J, JS_C_OBJECT, J->object_proto);
    js_def_value(J, G, "console", js_obj(console), JS_P_WRITE | JS_P_CONF);
    static const char *const logs[] = { "log", "info", "warn", "error", "debug", "trace" };
    for (int i = 0; i < 6; i++)
        js_def_native(J, console, logs[i], console_log, 0, i);

    js_lib2_init(J);
}
