/*
 * JavaScript: Proxy und Reflect - siehe js.h
 *
 * Ein Proxy steht vor einem anderen Objekt (dem Ziel) und lässt jeden Zugriff
 * erst über sein Begleitobjekt (handler) laufen: Hat es eine passende
 * Funktion ("Falle"), entscheidet die; sonst geht der Zugriff an das Ziel.
 * Die Stellen der Maschine, an denen Eigenschaften gelesen, geschrieben,
 * gelöscht und aufgezählt und an denen Funktionen gerufen werden, fragen hier
 * nach (js_run.c). Reflect bietet dieselben Grundoperationen als Funktionen.
 */
#include "js_impl.h"
#include "string.h"

#define ARGI(i) (J->bot + 2 + (i))
#define MAGIC (js_callee(J)->u.n.magic)

static bool is_proxy(const js_value *v)
{
    return v->t == JS_T_OBJ && v->u.o->cls == JS_C_PROXY;
}

js_object *js_proxy_unwrap(js_object *o)
{
    for (int i = 0; o && o->cls == JS_C_PROXY && i < 1000; i++)
        o = o->u.px.target;
    return o && o->cls != JS_C_PROXY ? o : NULL;
}

/*
 * Sucht die Falle name. Gibt es sie, liegen danach [falle, handler, ziel] auf
 * dem Stapel - fertig für js_call() mit weiteren Argumenten. Sonst liegt
 * nichts da und *found ist false.
 */
static bool trap(js_state *J, js_object *px, const char *name, bool *found)
{
    *found = false;
    if (!px->u.px.handler || !px->u.px.target)
        return js_throw(J, JS_E_TYPE, "Cannot perform '%s' on a proxy that has been revoked", name);
    if (J->top + 16 > JS_STACK_SIZE || JS_STACK_HERE() < J->stack_floor)
        return js_throw(J, JS_E_RANGE, "Maximum call stack size exceeded");
    js_push(J, js_obj(px->u.px.handler));
    if (!js_get(J, J->top - 1, js_intern_cstr(J, name)))
        return false;
    if (js_top(J, 0)->t <= JS_T_NULL) {
        js_pop(J, 2);
        return true;
    }
    if (!js_is_callable(js_top(J, 0)))
        return js_throw(J, JS_E_TYPE, "The proxy handler's '%s' trap is not a function", name);
    js_value fn = *js_top(J, 0);
    *js_top(J, 0) = *js_top(J, 1);
    *js_top(J, 1) = fn;
    js_push(J, js_obj(px->u.px.target));
    *found = true;
    return true;
}

/* --- Die Grundoperationen an einem Proxy ------------------------------------------------------------ */

bool js_proxy_get(js_state *J, int index, js_string *key)
{
    js_object *px = J->stack[index].u.o;
    bool found;
    if (key->symbol == JS_SYM_PRIVATE)
        return js_push(J, js_undef());  /* private Namen gehen an keinem Proxy vorbei und durch keinen hindurch */
    if (!trap(J, px, "get", &found))
        return false;
    if (!found) {
        js_push(J, js_obj(px->u.px.target));
        if (!js_get(J, J->top - 1, key))
            return false;
        *js_top(J, 1) = *js_top(J, 0);
        js_pop(J, 1);
        return true;
    }
    js_push(J, js_key_value(key));
    js_push(J, J->stack[index]);
    return js_call(J, 3);
}

/* Der Wert liegt oben auf dem Stapel und wird heruntergenommen. */
bool js_proxy_put(js_state *J, int index, js_string *key)
{
    js_object *px = J->stack[index].u.o;
    bool found;
    js_value value = *js_top(J, 0);
    if (key->symbol == JS_SYM_PRIVATE) {
        js_pop(J, 1);
        return true;
    }
    if (!trap(J, px, "set", &found))
        return false;
    if (!found) {
        js_push(J, js_obj(px->u.px.target));
        js_push(J, value);
        if (!js_put(J, J->top - 2, key))
            return false;
        js_pop(J, 2);
        return true;
    }
    js_push(J, js_key_value(key));
    js_push(J, value);
    js_push(J, J->stack[index]);
    if (!js_call(J, 4))
        return false;
    js_pop(J, 2);
    return true;
}

bool js_proxy_has(js_state *J, js_object *px, js_string *key, bool *result)
{
    bool found;
    if (!trap(J, px, "has", &found))
        return false;
    if (!found)
        return js_has_property(J, px->u.px.target, key, result);
    js_push(J, js_key_value(key));
    if (!js_call(J, 2))
        return false;
    *result = js_truthy(js_top(J, 0));
    js_pop(J, 1);
    return true;
}

bool js_proxy_delete(js_state *J, js_object *px, js_string *key, bool *done)
{
    bool found;
    if (!trap(J, px, "deleteProperty", &found))
        return false;
    if (!found)
        return js_delete_property(J, px->u.px.target, key, done);
    js_push(J, js_key_value(key));
    if (!js_call(J, 2))
        return false;
    *done = js_truthy(js_top(J, 0));
    js_pop(J, 1);
    return true;
}

/*
 * Die Beschreibung einer eigenen Eigenschaft, wie sie die Falle
 * getOwnPropertyDescriptor liefert - oder das Ziel selbst. Legt sie (oder
 * undefined) auf den Stapel.
 */
bool js_proxy_descriptor(js_state *J, js_object *px, js_string *key)
{
    bool found;
    if (!trap(J, px, "getOwnPropertyDescriptor", &found))
        return false;
    if (found) {
        js_push(J, js_key_value(key));
        return js_call(J, 2);
    }
    js_push(J, js_obj(px->u.px.target));
    if (!js_own_descriptor(J, J->top - 1, key))
        return false;
    *js_top(J, 1) = *js_top(J, 0);
    js_pop(J, 1);
    return true;
}

static bool own_enumerable(js_state *J, js_object *px, js_string *key, bool *yes)
{
    if (!js_proxy_descriptor(J, px, key))
        return false;
    *yes = false;
    if (js_top(J, 0)->t == JS_T_OBJ) {
        if (!js_get(J, J->top - 1, J->k[K_enumerable]))
            return false;
        *yes = js_truthy(js_top(J, 0));
        js_pop(J, 1);
    }
    js_pop(J, 1);
    return true;
}

/* Die eigenen Namen, wie js_own_keys() sie liefert */
bool js_proxy_keys(js_state *J, js_object *px, int which, js_object *out)
{
    int base = J->top;
    bool found;
    js_object *all = js_array_new(J, 0);
    js_push(J, js_obj(all));
    if (!trap(J, px, "ownKeys", &found))
        return false;
    if (found) {
        if (!js_call(J, 1) || !js_iterate(J, J->top - 1, all))
            return false;
    } else if (!js_own_keys(J, px->u.px.target, JS_KEYS_BOTH, all)) {
        return false;
    }
    for (uint32_t i = 0; i < all->u.arr.len; i++) {
        js_string *key;
        js_push(J, all->u.arr.v[i]);
        if (js_top(J, 0)->t != JS_T_STR && js_top(J, 0)->t != JS_T_SYM)
            return js_throw(J, JS_E_TYPE, "The proxy's ownKeys trap must return strings or symbols");
        if (!js_to_key(J, J->top - 1, &key))
            return false;
        bool wanted = (which & JS_KEYS_BOTH) ||
                      ((which & JS_KEYS_SYMBOLS) ? key->symbol != JS_SYM_NONE : key->symbol == JS_SYM_NONE);
        if (wanted && (which & JS_KEYS_ENUM) && !own_enumerable(J, px, key, &wanted))
            return false;
        if (wanted && !js_array_push(J, out, js_key_value(key)))
            return false;
        js_pop(J, 1);
    }
    J->top = base;
    return true;
}

/* Aufruf oder "new": Stapel [proxy, this, arg0 ... argN-1] -> [ergebnis] */
bool js_proxy_call(js_state *J, int argc, bool construct)
{
    int base = J->top - argc - 2;
    js_object *px = J->stack[base].u.o;
    bool found;
    if (!trap(J, px, construct ? "construct" : "apply", &found))
        return false;
    if (!found) {
        J->stack[base] = js_obj(px->u.px.target);
        return js_call_mode(J, argc, construct);
    }
    /* [.., falle, handler, ziel] */
    js_object *args = js_array_new(J, (uint32_t)argc);
    js_value self = J->stack[base + 1];
    if (!construct)
        js_push(J, self);
    js_push(J, js_obj(args));
    for (int i = 0; i < argc; i++) {
        if (!js_array_push(J, args, J->stack[base + 2 + i]))
            return false;
    }
    if (construct)
        js_push(J, js_obj(px));
    if (!js_call(J, 3))
        return false;
    if (construct && js_top(J, 0)->t != JS_T_OBJ)
        return js_throw(J, JS_E_TYPE, "The proxy's construct trap must return an object");
    J->stack[base] = *js_top(J, 0);
    J->top = base + 1;
    return true;
}

/*
 * Für Object.xxx(proxy, ...) und Reflect.xxx: ruft die Falle name mit dem Ziel
 * und den obersten extra Stapelwerten. *handled: Die Falle gab es, ihr
 * Ergebnis liegt statt der Werte auf dem Stapel. Sonst bleibt der Stapel, wie
 * er war.
 */
bool js_proxy_invoke(js_state *J, js_object *px, const char *name, int extra, bool *handled)
{
    int first = J->top - extra;
    if (!trap(J, px, name, handled))
        return false;
    if (!*handled)
        return true;
    for (int i = 0; i < extra; i++)
        js_push(J, J->stack[first + i]);
    if (!js_call(J, 1 + extra))
        return false;
    J->stack[first] = *js_top(J, 0);
    J->top = first + 1;
    return true;
}

/* --- Proxy ------------------------------------------------------------------------------------------- */

static js_object *proxy_new(js_state *J, js_value *target, js_value *handler)
{
    if (target->t != JS_T_OBJ || handler->t != JS_T_OBJ) {
        js_throw(J, JS_E_TYPE, "Cannot create proxy with a non-object as target or handler");
        return NULL;
    }
    js_object *px = js_object_new(J, JS_C_PROXY, NULL);
    if (px == J->dummy)
        return NULL;
    px->u.px.target = target->u.o;
    px->u.px.handler = handler->u.o;
    px->u.px.callable = js_is_callable(target);
    return px;
}

static bool proxy_ctor(js_state *J, int argc)
{
    (void)argc;
    if (!J->constructing)
        return js_throw(J, JS_E_TYPE, "Constructor Proxy requires 'new'");
    js_object *px = proxy_new(J, js_arg(J, 0), js_arg(J, 1));
    return px && js_push(J, js_obj(px));
}

/* die Funktion "revoke" eines widerrufbaren Proxys: Danach scheitert jeder Zugriff. */
static bool proxy_revoke(js_state *J, int argc)
{
    (void)argc;
    js_object *px = js_callee(J)->u.n.data;
    if (px && px->cls == JS_C_PROXY) {
        px->u.px.target = NULL;
        px->u.px.handler = NULL;
    }
    return js_push(J, js_undef());
}

static bool proxy_revocable(js_state *J, int argc)
{
    (void)argc;
    js_object *px = proxy_new(J, js_arg(J, 0), js_arg(J, 1));
    if (!px)
        return false;
    js_push(J, js_obj(px));
    js_object *result = js_object_new(J, JS_C_OBJECT, J->object_proto);
    js_push(J, js_obj(result));
    js_object *revoke = js_native_new(J, proxy_revoke, "", 0, 0);
    revoke->u.n.data = px;
    js_define(J, result, js_intern_cstr(J, "proxy"), js_obj(px), JS_P_ALL);
    js_define(J, result, js_intern_cstr(J, "revoke"), js_obj(revoke), JS_P_ALL);
    return !J->fatal;
}

/* --- Reflect ----------------------------------------------------------------------------------------- */

enum { R_APPLY, R_CONSTRUCT, R_DEFINE, R_DELETE, R_GET, R_DESCRIPTOR, R_GET_PROTO, R_HAS,
       R_EXTENSIBLE, R_OWN_KEYS, R_PREVENT, R_SET, R_SET_PROTO };

static bool reflect(js_state *J, int argc)
{
    js_value *target = js_arg(J, 0);
    js_string *key = NULL;
    int op = MAGIC;
    if (target->t != JS_T_OBJ)
        return js_throw(J, JS_E_TYPE, "Reflect called on a value that is not an object");
    if ((op == R_DEFINE || op == R_DELETE || op == R_GET || op == R_DESCRIPTOR || op == R_HAS ||
         op == R_SET) && !js_to_key(J, ARGI(1), &key))
        return false;
    switch (op) {
    case R_APPLY: case R_CONSTRUCT: {
        if (!js_is_callable(target))
            return js_throw(J, JS_E_TYPE, "Reflect target is not a function");
        js_object *list = js_array_new(J, 0);
        js_push(J, js_obj(list));
        int at = op == R_APPLY ? 2 : 1;
        if (js_arg(J, at)->t > JS_T_NULL && !js_iterate(J, ARGI(at), list))
            return false;
        if (list->u.arr.len > 10000 || J->top + (int)list->u.arr.len + 200 > JS_STACK_SIZE)
            return js_throw(J, JS_E_RANGE, "Too many arguments");
        js_push(J, *js_arg(J, 0));
        js_push(J, op == R_APPLY ? *js_arg(J, 1) : js_undef());
        for (uint32_t i = 0; i < list->u.arr.len; i++)
            js_push(J, list->u.arr.v[i]);
        if (op == R_APPLY)
            return js_call(J, (int)list->u.arr.len);
        /* construct(ziel, argumente, neuesZiel): Das Objekt erbt vom Prototyp des dritten Arguments */
        js_value *derived = js_arg(J, 2);
        if (argc > 2 && js_is_callable(derived) && derived->u.o != target->u.o) {
            if (!js_call_mode(J, (int)list->u.arr.len, true))
                return false;
            if (!js_get(J, ARGI(2), J->k[K_prototype]))
                return false;
            if (js_top(J, 0)->t == JS_T_OBJ && js_top(J, 1)->t == JS_T_OBJ &&
                js_top(J, 1)->u.o->cls != JS_C_PROXY)
                js_top(J, 1)->u.o->proto = js_top(J, 0)->u.o;
            js_pop(J, 1);
            return true;
        }
        return js_call_mode(J, (int)list->u.arr.len, true);
    }
    case R_DEFINE: {
        bool handled;
        if (is_proxy(target)) {
            js_push(J, js_key_value(key));
            js_push(J, *js_arg(J, 2));
            if (!js_proxy_invoke(J, target->u.o, "defineProperty", 2, &handled))
                return false;
            if (handled) {
                *js_top(J, 0) = js_bool(js_truthy(js_top(J, 0)));
                return true;
            }
            js_pop(J, 2);
        }
        js_object *o = js_proxy_unwrap(target->u.o);
        if (!o)
            return js_throw(J, JS_E_TYPE, "Cannot perform 'defineProperty' on a proxy that has been revoked");
        if (!js_define_descriptor(J, o, key, ARGI(2))) {
            /* Reflect meldet das Scheitern als false, statt eine Ausnahme zu werfen */
            if (J->fatal)
                return false;
            J->throwing = false;
            J->exception = js_undef();
            return js_push(J, js_bool(false));
        }
        return js_push(J, js_bool(true));
    }
    case R_DELETE: {
        bool done;
        if (!js_delete_property(J, target->u.o, key, &done))
            return false;
        return js_push(J, js_bool(done));
    }
    case R_GET: {
        /* get(ziel, name, empfänger): Eine Lesefunktion läuft mit dem Empfänger als "this" */
        js_object *o = target->u.o;
        if (argc > 2 && o->cls != JS_C_PROXY && js_arg(J, 2)->t == JS_T_OBJ) {
            js_prop *p = js_lookup(o, key, NULL);
            if (p && (p->flags & JS_P_ACCESSOR)) {
                if (p->v.t != JS_T_OBJ)
                    return js_push(J, js_undef());
                js_push(J, p->v);
                js_push(J, *js_arg(J, 2));
                return js_call(J, 0);
            }
        }
        return js_get(J, ARGI(0), key);
    }
    case R_SET: {
        /* set(ziel, name, wert, empfänger): Eine Schreibfunktion läuft mit dem Empfänger als "this" */
        js_object *o = target->u.o;
        if (argc > 3 && o->cls != JS_C_PROXY && js_arg(J, 3)->t == JS_T_OBJ) {
            js_prop *p = js_lookup(o, key, NULL);
            if (p && (p->flags & JS_P_ACCESSOR)) {
                if (!p->set)
                    return js_push(J, js_bool(false));
                js_push(J, js_obj(p->set));
                js_push(J, *js_arg(J, 3));
                js_push(J, *js_arg(J, 2));
                if (!js_call(J, 1))
                    return false;
                return js_push(J, js_bool(true));
            }
        }
        js_prop *own = o->cls != JS_C_PROXY ? js_own(o, key) : NULL;
        if (own && !(own->flags & (JS_P_WRITE | JS_P_ACCESSOR)))
            return js_push(J, js_bool(false));
        js_push(J, *js_arg(J, 2));
        if (!js_put(J, ARGI(0), key))
            return false;
        return js_push(J, js_bool(true));
    }
    case R_DESCRIPTOR:
        return js_own_descriptor(J, ARGI(0), key);
    case R_HAS: {
        bool found;
        if (!js_has_property(J, target->u.o, key, &found))
            return false;
        return js_push(J, js_bool(found));
    }
    case R_GET_PROTO: case R_SET_PROTO: case R_EXTENSIBLE: case R_PREVENT: {
        /* dieselbe Arbeit wie die gleichnamigen Funktionen von Object */
        static const char *const names[] = { "getPrototypeOf", "setPrototypeOf", "isExtensible",
                                             "preventExtensions" };
        const char *name = names[op == R_GET_PROTO ? 0 : op == R_SET_PROTO ? 1 : op == R_EXTENSIBLE ? 2 : 3];
        js_push(J, js_obj(J->global));
        if (!js_get_cstr(J, J->top - 1, "Object") || !js_get_cstr(J, J->top - 1, name))
            return false;
        js_push(J, js_undef());
        js_push(J, *js_arg(J, 0));
        js_push(J, *js_arg(J, 1));
        if (!js_call(J, 2))
            return false;
        if (op == R_SET_PROTO || op == R_PREVENT)
            *js_top(J, 0) = js_bool(true);
        return true;
    }
    default: {                          /* ownKeys: Namen und Symbole */
        js_object *keys = js_array_new(J, 0);
        js_push(J, js_obj(keys));
        if (!js_own_keys(J, target->u.o, JS_KEYS_BOTH, keys))
            return false;
        if (target->u.o->cls == JS_C_ARRAY && !js_array_push(J, keys, js_str(J->k[K_length])))
            return false;
        return true;
    }
    }
}

void js_proxy_init(js_state *J)
{
    js_object *proxy = js_native_new(J, proxy_ctor, "Proxy", 2, 0);
    js_define(J, J->global, js_intern_cstr(J, "Proxy"), js_obj(proxy), JS_P_WRITE | JS_P_CONF);
    js_def_native(J, proxy, "revocable", proxy_revocable, 2, 0);

    js_object *r = js_object_new(J, JS_C_OBJECT, J->object_proto);
    js_define(J, J->global, js_intern_cstr(J, "Reflect"), js_obj(r), JS_P_WRITE | JS_P_CONF);
    js_define(J, r, J->sym[SYM_toStringTag], js_str(js_intern_cstr(J, "Reflect")), JS_P_CONF);
    static const struct {
        const char *name;
        uint8_t nargs, op;
    } defs[] = {
        { "apply", 3, R_APPLY }, { "construct", 3, R_CONSTRUCT }, { "defineProperty", 3, R_DEFINE },
        { "deleteProperty", 2, R_DELETE }, { "get", 2, R_GET },
        { "getOwnPropertyDescriptor", 2, R_DESCRIPTOR }, { "getPrototypeOf", 2, R_GET_PROTO },
        { "has", 2, R_HAS }, { "isExtensible", 2, R_EXTENSIBLE }, { "ownKeys", 1, R_OWN_KEYS },
        { "preventExtensions", 2, R_PREVENT }, { "set", 3, R_SET },
        { "setPrototypeOf", 2, R_SET_PROTO },
    };
    for (unsigned i = 0; i < sizeof(defs) / sizeof(defs[0]); i++)
        js_def_native(J, r, defs[i].name, reflect, defs[i].nargs, defs[i].op);
}
