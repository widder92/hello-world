/* JavaScript: den Programmbaum ausführen - siehe js.h */
#include "js_impl.h"
#include "string.h"

enum { C_NORMAL, C_BREAK, C_CONTINUE, C_RETURN, C_THROW };
enum { A_ASSIGN, A_VAR, A_LET, A_CONST };

#define DEPTH_MAX 3000

static bool eval(js_state *J, js_node *n, js_object *env);
static int exec(js_state *J, js_node *n, js_object *env, js_string *label);
static int exec_list(js_state *J, js_node *list, js_object *env);
static bool assign_to(js_state *J, js_node *t, js_object *env, int mode);
static bool do_call(js_state *J, int argc, int mode);

/* --- Grenzen ----------------------------------------------------------------------------------- */

static bool enter(js_state *J)
{
    if (J->fatal || J->throwing)
        return false;
    if (++J->depth > DEPTH_MAX || JS_STACK_HERE() < J->stack_floor ||
        J->top + 200 > JS_STACK_SIZE) {
        J->depth--;
        return js_throw(J, JS_E_RANGE, "Maximum call stack size exceeded");
    }
    return true;
}

#define LEAVE(value) do { J->depth--; return (value); } while (0)

static bool tick(js_state *J)
{
    if ((++J->steps & 0x3FF) == 0) {
        if (J->host.ticks_ms && J->deadline && J->host.ticks_ms(J->host.ctx) > J->deadline)
            return js_fatal(J, "Das Skript braucht zu lange und wurde abgebrochen");
        if (J->host.cancelled && J->host.cancelled(J->host.ctx))
            return js_fatal(J, "Das Skript wurde von außen beendet");
    }
    return !J->fatal;
}

/* --- Eigenschaften lesen und schreiben ----------------------------------------------------------- */

static js_object *function_prototype(js_state *J, js_object *f)
{
    js_prop *p = js_own(f, J->k[K_prototype]);
    if (p)
        return p->v.t == JS_T_OBJ ? p->v.u.o : NULL;
    if (f->cls != JS_C_FUNCTION || (f->flags & JS_F_ARROW))
        return NULL;
    if (f->flags & JS_F_GENERATOR) {
        /* was eine Generatorfunktion liefert, erbt von hier - und darüber next() und Co. */
        js_object *parent = J->generator_proto;
        if (f->flags & JS_F_ASYNC) {
            js_prop *ag = js_own(J->registry, js_intern_cstr(J, "AsyncGeneratorPrototype"));
            parent = ag && ag->v.t == JS_T_OBJ ? ag->v.u.o : J->object_proto;
        }
        js_object *proto = js_object_new(J, JS_C_OBJECT, parent);
        js_define(J, f, J->k[K_prototype], js_obj(proto), JS_P_WRITE);
        return proto;
    }
    if (f->flags & JS_F_ASYNC)
        return NULL;
    js_object *proto = js_object_new(J, JS_C_OBJECT, J->object_proto);
    js_define(J, proto, J->k[K_constructor], js_obj(f), JS_P_WRITE | JS_P_CONF);
    js_define(J, f, J->k[K_prototype], js_obj(proto), JS_P_WRITE);
    return proto;
}

static bool push_char(js_state *J, const js_string *s, uint32_t index)
{
    if (index >= s->chars)
        return js_push(J, js_undef());
    uint32_t at = js_string_offset(s, index), end = at + 1;
    while (end < s->len && ((unsigned char)s->data[end] & 0xC0) == 0x80)
        end++;
    return js_push_lstr(J, s->data + at, end - at);
}

bool js_get(js_state *J, int index, js_string *key)
{
    js_value *v = &J->stack[index];
    js_object *o;
    switch (v->t) {
    case JS_T_OBJ:
        o = v->u.o;
        break;
    case JS_T_STR:
        if (key == J->k[K_length])
            return js_push(J, js_num(v->u.s->chars));
        if (key->index != JS_NO_INDEX)
            return push_char(J, v->u.s, key->index);
        o = J->string_proto;
        break;
    case JS_T_NUM:
        o = J->number_proto;
        break;
    case JS_T_BOOL:
        o = J->boolean_proto;
        break;
    case JS_T_SYM:
        o = J->symbol_proto;
        break;
    default:
        return js_throw(J, JS_E_TYPE, "Cannot read properties of %s (reading '%s')",
                        v->t == JS_T_NULL ? "null" : "undefined", key->data);
    }
    if (v->t == JS_T_OBJ) {
        switch (o->cls) {
        case JS_C_PROXY:
            return js_proxy_get(J, index, key);
        case JS_C_ARRAY:
            if (key->index != JS_NO_INDEX) {
                if (key->index < o->u.arr.len)
                    return js_push(J, o->u.arr.v[key->index]);
            } else if (key == J->k[K_length]) {
                return js_push(J, js_num(o->u.arr.len));
            }
            break;
        case JS_C_STRING:
            if (key == J->k[K_length])
                return js_push(J, js_num(o->u.prim.u.s->chars));
            if (key->index != JS_NO_INDEX && key->index < o->u.prim.u.s->chars)
                return push_char(J, o->u.prim.u.s, key->index);
            break;
        case JS_C_FUNCTION:
            if (key == J->k[K_prototype]) {
                js_object *proto = function_prototype(J, o);
                if (proto)
                    return js_push(J, js_obj(proto));
            } else if (key == J->k[K_length] && !js_own(o, key)) {
                return js_push(J, js_num(o->u.f.fn->u.fn->nparams));
            } else if (key == J->k[K_name] && !js_own(o, key)) {
                js_string *name = o->u.f.fn->u.fn->name;
                return js_push(J, js_str(name ? name : J->empty));
            }
            break;
        case JS_C_NATIVE:
            if (key == J->k[K_length] && !js_own(o, key))
                return js_push(J, js_num(o->u.n.nargs));
            break;
        case JS_C_HOST:
            if (o->u.host.cls && o->u.host.cls->get) {
                bool found = false;
                if (!o->u.host.cls->get(J, o, key, &found))
                    return false;
                if (found)
                    return true;
                v = &J->stack[index];
            }
            break;
        default:
            break;
        }
    }
    js_prop *p = js_lookup(o, key, NULL);
    if (!p) {
        /* nirgends gefunden - es sei denn, in der Kette der Prototypen steht ein Proxy */
        for (js_object *q = o->proto; q; q = q->proto) {
            if (q->cls != JS_C_PROXY)
                continue;
            js_push(J, js_obj(q));
            if (!js_proxy_get(J, J->top - 1, key))
                return false;
            *js_top(J, 1) = *js_top(J, 0);
            js_pop(J, 1);
            return true;
        }
        return js_push(J, js_undef());
    }
    if (!(p->flags & JS_P_ACCESSOR))
        return js_push(J, p->v);
    if (p->v.t != JS_T_OBJ)
        return js_push(J, js_undef());
    js_push(J, p->v);
    js_push(J, J->stack[index]);
    return js_call(J, 0);
}

bool js_get_cstr(js_state *J, int index, const char *name)
{
    return js_get(J, index, js_intern_cstr(J, name));
}

bool js_get_index(js_state *J, int index, uint32_t i)
{
    js_value *v = &J->stack[index];
    if (v->t == JS_T_OBJ && v->u.o->cls == JS_C_ARRAY && i < v->u.o->u.arr.len)
        return js_push(J, v->u.o->u.arr.v[i]);
    return js_get(J, index, js_index_name(J, i));
}

/* Der Wert liegt oben auf dem Stapel und wird heruntergenommen. */
bool js_put(js_state *J, int index, js_string *key)
{
    js_value *target = &J->stack[index];
    js_value value = *js_top(J, 0);
    if (target->t != JS_T_OBJ) {
        if (target->t <= JS_T_NULL)
            return js_throw(J, JS_E_TYPE, "Cannot set properties of %s (setting '%s')",
                            target->t == JS_T_NULL ? "null" : "undefined", key->data);
        js_pop(J, 1);                   /* Eigenschaften von Zahlen und Texten: vergeblich */
        return true;
    }
    js_object *o = target->u.o;
    if (o->cls == JS_C_PROXY)
        return js_proxy_put(J, index, key);
    if (o->cls == JS_C_ARRAY) {
        if (key->index != JS_NO_INDEX) {
            if (key->index >= o->u.arr.len && !js_array_resize(J, o, key->index + 1))
                return false;
            o->u.arr.v[key->index] = value;
            js_pop(J, 1);
            return true;
        }
        if (key == J->k[K_length]) {
            double n;
            if (!js_to_number(J, J->top - 1, &n))
                return false;
            if (!(n >= 0 && n <= JS_ARRAY_MAX && n == js_trunc(n)))
                return js_throw(J, JS_E_RANGE, "Invalid array length");
            if (!js_array_resize(J, o, (uint32_t)n))
                return false;
            js_pop(J, 1);
            return true;
        }
    } else if (o->cls == JS_C_HOST && o->u.host.cls && o->u.host.cls->put) {
        bool done = false;
        if (!o->u.host.cls->put(J, o, key, &value, &done))
            return false;
        if (done) {
            js_pop(J, 1);
            return true;
        }
    }
    js_object *holder = NULL;
    js_prop *p = js_lookup(o, key, &holder);
    if (p && (p->flags & JS_P_ACCESSOR)) {
        if (p->set) {
            js_push(J, js_obj(p->set));
            js_push(J, J->stack[index]);
            js_push(J, value);
            if (!js_call(J, 1))
                return false;
            js_pop(J, 1);
        }
        js_pop(J, 1);
        return true;
    }
    if (p && holder == o) {
        if (p->flags & JS_P_WRITE)
            p->v = value;
    } else if (!(p && !(p->flags & JS_P_WRITE)) && !(o->flags & JS_F_NOEXT)) {
        js_define(J, o, key, value, JS_P_ALL);
    }
    js_pop(J, 1);
    return true;
}

bool js_put_cstr(js_state *J, int index, const char *name)
{
    return js_put(J, index, js_intern_cstr(J, name));
}

/* [obj, key] -> [wert] */
bool js_get_value(js_state *J)
{
    js_value *o = js_top(J, 1), *k = js_top(J, 0);
    if (k->t == JS_T_NUM && k->u.n >= 0 && k->u.n < 4294967295.0) {
        uint32_t i = (uint32_t)k->u.n;
        if ((double)i == k->u.n) {
            if (o->t == JS_T_OBJ && o->u.o->cls == JS_C_ARRAY && i < o->u.o->u.arr.len) {
                *o = o->u.o->u.arr.v[i];
                js_pop(J, 1);
                return true;
            }
            if (o->t == JS_T_STR && i < o->u.s->chars) {
                if (!push_char(J, o->u.s, i))
                    return false;
                *js_top(J, 2) = *js_top(J, 0);
                js_pop(J, 2);
                return true;
            }
        }
    }
    js_string *key;
    if (!js_to_key(J, J->top - 1, &key) || !js_get(J, J->top - 2, key))
        return false;
    *js_top(J, 2) = *js_top(J, 0);
    js_pop(J, 2);
    return true;
}

/* [obj, key, wert] -> [] */
bool js_put_value(js_state *J)
{
    js_value *o = js_top(J, 2), *k = js_top(J, 1);
    if (k->t == JS_T_NUM && o->t == JS_T_OBJ && o->u.o->cls == JS_C_ARRAY && k->u.n >= 0 &&
        k->u.n < o->u.o->u.arr.len && (double)(uint32_t)k->u.n == k->u.n) {
        o->u.o->u.arr.v[(uint32_t)k->u.n] = *js_top(J, 0);
        js_pop(J, 3);
        return true;
    }
    js_string *key;
    if (!js_to_key(J, J->top - 2, &key) || !js_put(J, J->top - 3, key))
        return false;
    js_pop(J, 2);
    return true;
}

bool js_instanceof(js_state *J, const js_value *v, const js_value *ctor, bool *out)
{
    if (ctor->t == JS_T_OBJ && js_lookup(ctor->u.o, J->sym[SYM_hasInstance], NULL)) {
        /* die Klasse entscheidet selbst: [Symbol.hasInstance](wert) */
        js_value value = *v;
        js_push(J, *ctor);
        if (!js_get(J, J->top - 1, J->sym[SYM_hasInstance]))
            return false;
        if (js_is_callable(js_top(J, 0))) {
            js_push(J, *js_top(J, 1));
            js_push(J, value);
            if (!js_call(J, 1))
                return false;
            *out = js_truthy(js_top(J, 0));
            js_pop(J, 2);
            return true;
        }
        js_pop(J, 2);
    }
    if (!js_is_callable(ctor))
        return js_throw(J, JS_E_TYPE, "Right-hand side of 'instanceof' is not callable");
    *out = false;
    if (v->t != JS_T_OBJ)
        return true;
    js_object *f = ctor->u.o;
    if (f->cls == JS_C_NATIVE && f->u.n.data && f->u.n.magic == -77) {
        /* mit bind() gebundene Funktion: es zählt die ursprüngliche */
        js_prop *t = js_own(f->u.n.data, js_intern_cstr(J, "target"));
        if (t && t->v.t == JS_T_OBJ)
            f = t->v.u.o;
    }
    js_object *proto = function_prototype(J, f);
    if (f->cls == JS_C_PROXY) {
        /* eine Klasse hinter einem Proxy: ihr "prototype" über den Proxy erfragen */
        js_value value = *v;
        js_push(J, *ctor);
        if (!js_get(J, J->top - 1, J->k[K_prototype]))
            return false;
        proto = js_top(J, 0)->t == JS_T_OBJ ? js_top(J, 0)->u.o : NULL;
        js_pop(J, 2);
        v = &value;
        if (!proto)
            return true;
        js_object *start = js_proxy_unwrap(value.u.o);
        for (js_object *o = start ? start->proto : NULL; o; o = o->proto) {
            if (o == proto) {
                *out = true;
                break;
            }
        }
        return true;
    }
    if (!proto)
        return true;
    js_object *first = js_proxy_unwrap(v->u.o);
    js_object *o = first ? first->proto : NULL;
    for (int i = 0; o && i < 10000; i++, o = o->proto) {
        if (o == proto) {
            *out = true;
            break;
        }
    }
    return true;
}

/* --- Variablen ------------------------------------------------------------------------------------ */

static js_object *scope_new(js_state *J, js_object *parent)
{
    js_object *s = js_object_new(J, JS_C_SCOPE, parent);
    js_push(J, js_obj(s));              /* bleibt auf dem Stapel, solange der Bereich gilt */
    return s;
}

static bool is_scope(const js_state *J, const js_object *o)
{
    return o->cls == JS_C_SCOPE || o == J->global;
}

static bool get_var(js_state *J, js_object *env, js_string *name, bool quiet)
{
    js_object *holder = NULL;
    js_prop *p = js_lookup(env, name, &holder);
    if (!p) {
        if (quiet)
            return js_push(J, js_undef());
        return js_throw(J, JS_E_REFERENCE, "%s is not defined", name->data);
    }
    if (!(p->flags & JS_P_ACCESSOR))
        return js_push(J, p->v);
    if (p->v.t != JS_T_OBJ)
        return js_push(J, js_undef());
    js_push(J, p->v);
    js_push(J, js_obj(J->global));
    return js_call(J, 0);
}

/* Der Wert liegt oben auf dem Stapel und bleibt dort. */
static bool set_var(js_state *J, js_object *env, js_string *name)
{
    js_object *holder = NULL;
    js_prop *p = js_lookup(env, name, &holder);
    if (p && is_scope(J, holder)) {
        if (p->flags & JS_P_ACCESSOR) {
            if (!p->set)
                return true;
            js_push(J, js_obj(p->set));
            js_push(J, js_obj(J->global));
            js_push(J, *js_top(J, 2));
            if (!js_call(J, 1))
                return false;
            js_pop(J, 1);
            return true;
        }
        if (!(p->flags & JS_P_WRITE)) {
            if (p->flags & JS_P_CONST)
                return js_throw(J, JS_E_TYPE, "Assignment to constant variable '%s'", name->data);
            return true;
        }
        p->v = *js_top(J, 0);
        return true;
    }
    /* nirgends vereinbart: wird eine Eigenschaft des globalen Objekts */
    js_define(J, J->global, name, *js_top(J, 0), JS_P_ALL);
    return true;
}

/* --- Aufzählen ------------------------------------------------------------------------------------- */

bool js_own_keys(js_state *J, js_object *o, int which, js_object *out)
{
    bool only_enumerable = which & JS_KEYS_ENUM;
    if (o->cls == JS_C_PROXY)
        return js_proxy_keys(J, o, which, out);
    if (which & JS_KEYS_SYMBOLS) {
        for (uint32_t i = 0; i < o->count; i++) {
            js_prop *p = &o->props[i];
            if (p->key && p->key->symbol == JS_SYM_PUBLIC &&
                (!only_enumerable || (p->flags & JS_P_ENUM)) &&
                !js_array_push(J, out, js_sym(p->key)))
                return false;
        }
        return true;
    }
    if (o->cls == JS_C_ARRAY) {
        for (uint32_t i = 0; i < o->u.arr.len; i++) {
            if (!js_array_push(J, out, js_str(js_index_name(J, i))))
                return false;
        }
    } else if (o->cls == JS_C_STRING) {
        for (uint32_t i = 0; i < o->u.prim.u.s->chars; i++) {
            if (!js_array_push(J, out, js_str(js_index_name(J, i))))
                return false;
        }
    }
    /* erst Zahlen aufsteigend, dann die übrigen Namen in der Reihenfolge ihres Entstehens */
    uint32_t first = out->u.arr.len;
    for (uint32_t i = 0; i < o->count; i++) {
        js_prop *p = &o->props[i];
        if (p->key && p->key->index != JS_NO_INDEX && (!only_enumerable || (p->flags & JS_P_ENUM)) &&
            !js_array_push(J, out, js_str(p->key)))
            return false;
    }
    if (out->cls == JS_C_ARRAY) {
        for (uint32_t i = first + 1; i < out->u.arr.len; i++) {
            js_value item = out->u.arr.v[i];
            uint32_t k = i;
            while (k > first && out->u.arr.v[k - 1].u.s->index > item.u.s->index) {
                out->u.arr.v[k] = out->u.arr.v[k - 1];
                k--;
            }
            out->u.arr.v[k] = item;
        }
    }
    for (uint32_t i = 0; i < o->count; i++) {
        js_prop *p = &o->props[i];
        if (p->key && p->key->index == JS_NO_INDEX && !p->key->symbol &&
            (!only_enumerable || (p->flags & JS_P_ENUM)) && !js_array_push(J, out, js_str(p->key)))
            return false;
    }
    if (which & JS_KEYS_BOTH)
        return js_own_keys(J, o, (which & JS_KEYS_ENUM) | JS_KEYS_SYMBOLS, out);
    return true;
}

static bool tick(js_state *J);

/* Hängt die Glieder des Werts an Platz index an out an - alles, was sich durchlaufen lässt. */
bool js_iterate(js_state *J, int index, js_object *out)
{
    js_value *v = &J->stack[index];
    if (v->t == JS_T_OBJ && v->u.o->cls == JS_C_ARRAY && out->cls == JS_C_ARRAY &&
        out->u.arr.len == 0 && v->u.o != out) {
        /* ein Feld in ein leeres Feld: in einem Zug, wenn es wie üblich durchlaufen wird */
        js_prop *p = js_lookup(v->u.o, J->sym[SYM_iterator], NULL);
        if (p && p->v.t == JS_T_OBJ && p->v.u.o == J->array_values) {
            js_object *a = v->u.o;
            if (!js_array_resize(J, out, a->u.arr.len))
                return false;
            if (a->u.arr.len)
                memcpy(out->u.arr.v, a->u.arr.v, (size_t)a->u.arr.len * sizeof(js_value));
            return true;
        }
    }
    int base = J->top;
    js_iter it;
    if (!js_iter_open(J, index, &it))
        return false;
    for (;;) {
        bool done;
        if (!tick(J) || !js_iter_next(J, &it, &done))
            return false;
        if (done)
            break;
        if (!js_array_push(J, out, *js_top(J, 0)))
            return js_iter_close(J, &it, true);
        js_pop(J, 1);
    }
    J->top = base;
    return true;
}

/* --- Funktionen ------------------------------------------------------------------------------------ */

js_object *js_function_new(js_state *J, js_node *fn, js_object *scope)
{
    js_object *f = js_object_new(J, JS_C_FUNCTION, J->function_proto);
    if (f == J->dummy)
        return f;
    f->u.f.fn = fn;
    f->u.f.scope = scope;
    f->u.f.self = js_undef();
    f->flags = fn->u.fn->flags;
    return f;
}

static const char *callee_name(js_node *n)
{
    if (!n)
        return "expression";
    if (n->type == N_IDENT || n->type == N_MEMBER)
        return n->u.str->data;
    if (n->type == N_CHAIN)
        return callee_name(n->a);
    return "expression";
}

/*
 * Private Namen einer Klasse (#x): Für jeden steht im Bereich der Klasse ein
 * eigenes Symbol unter dem Namen "#x". Unter diesem Symbol liegt das Feld im
 * Objekt - von außen kommt niemand an das Symbol und damit an das Feld.
 */
static bool private_key(js_state *J, js_object *env, js_string *name, js_string **key)
{
    js_prop *p = js_lookup(env, name, NULL);
    if (!p || p->v.t != JS_T_SYM)
        return js_throw(J, JS_E_SYNTAX, "Private field '%s' must be declared in an enclosing class",
                        name->data);
    *key = p->v.u.s;
    return true;
}

/* Der Name, den ein Zugriff "objekt.name" meint; das Objekt liegt oben auf dem Stapel */
static bool member_key(js_state *J, js_node *n, js_object *env, js_string **key, bool writing)
{
    *key = n->u.str;
    if (!(n->flags & NF_PRIVATE))
        return true;
    if (!private_key(J, env, n->u.str, key))
        return false;
    js_value *o = js_top(J, 0);
    if (o->t != JS_T_OBJ || !js_lookup(o->u.o, *key, NULL))
        return js_throw(J, JS_E_TYPE, "Cannot %s private member %s from an object whose class did not declare it",
                        writing ? "write" : "read", n->u.str->data);
    return true;
}

static bool init_fields(js_state *J, js_node *fields, js_object *env)
{
    for (js_node *f = fields; f; f = f->next) {
        js_string *key;
        if (f->flags & NF_COMPUTED) {
            if (!eval(J, f->a, env) || !js_to_key(J, J->top - 1, &key))
                return false;
            js_pop(J, 1);
        } else if (f->flags & NF_PRIVATE) {
            if (!private_key(J, env, f->a->u.str, &key))
                return false;
        } else {
            key = f->a->u.str;
        }
        if (f->b) {
            if (!eval(J, f->b, env))
                return false;
        } else {
            js_push(J, js_undef());
        }
        js_value *self = &J->stack[J->fbase + 1];
        if (self->t == JS_T_OBJ)
            js_define(J, self->u.o, key, *js_top(J, 0), JS_P_ALL);
        js_pop(J, 1);
    }
    return true;
}

static bool bind_params(js_state *J, js_fninfo *info, js_object *env, int base, int argc)
{
    int i = 0;
    for (js_node *param = info->params; param; param = param->next, i++) {
        if (param->type == N_IDENT) {
            js_define(J, env, param->u.str, i < argc ? J->stack[base + 2 + i] : js_undef(),
                      JS_P_ALL);
            continue;
        }
        if (param->type == N_REST) {
            js_object *rest = js_array_new(J, 0);
            js_push(J, js_obj(rest));
            for (int k = i; k < argc; k++) {
                if (!js_array_push(J, rest, J->stack[base + 2 + k]))
                    return false;
            }
            if (!assign_to(J, param->a, env, A_LET))
                return false;
            js_pop(J, 1);
            continue;
        }
        js_node *target = param;
        js_push(J, i < argc ? J->stack[base + 2 + i] : js_undef());
        if (param->type == N_DEFAULT) {
            target = param->a;
            if (js_top(J, 0)->t == JS_T_UNDEF) {
                js_pop(J, 1);
                if (!eval(J, param->b, env))
                    return false;
            }
        }
        if (!assign_to(J, target, env, A_LET))
            return false;
        js_pop(J, 1);
    }
    return true;
}

static void hoist(js_state *J, js_fninfo *info, js_object *env)
{
    for (int i = 0; i < info->nvars; i++) {
        if (!js_own(env, info->vars[i]))
            js_define(J, env, info->vars[i], js_undef(), JS_P_ENUM | JS_P_WRITE);
    }
    for (int i = 0; i < info->nfuncs; i++) {
        js_object *f = js_function_new(J, info->funcs[i], env);
        js_define(J, env, info->funcs[i]->u.fn->name, js_obj(f), JS_P_ENUM | JS_P_WRITE);
    }
}

/*
 * Stapel: [fn, this, arg0 ... argN-1] -> [ergebnis]. mode: CALL - gewöhnlicher
 * Aufruf; CONSTRUCT - mit "new" (ein neues Objekt wird "this"); DERIVE -
 * super(...) in einer abgeleiteten Klasse: "this" gibt es schon, und wie bei
 * "new" ist es das Ergebnis, wenn die Funktion kein Objekt zurückgibt.
 */
enum { CALL, CONSTRUCT, DERIVE };

static bool do_call(js_state *J, int argc, int mode)
{
    bool construct = mode == CONSTRUCT;
    int base = J->top - argc - 2;
    js_value *fv = &J->stack[base];
    if (!js_is_callable(fv)) {
        J->top = base;
        return js_throw(J, JS_E_TYPE, construct ? "Value is not a constructor"
                                                : "Value is not a function");
    }
    if (!enter(J)) {
        J->top = base;
        return false;
    }
    js_checkpoint(J);
    if (!tick(J)) {
        J->top = base;
        LEAVE(false);
    }
    js_object *f = fv->u.o;
    bool ok;
    js_value result = js_undef();
    if (f->cls == JS_C_PROXY) {
        ok = js_proxy_call(J, argc, mode != CALL);
        if (!ok)
            J->top = base;
        LEAVE(ok);
    }

    if (f->cls == JS_C_NATIVE) {
        /*
         * Fehlende Argumente werden mit undefined aufgefüllt, damit die
         * Funktion ihre festen Plätze gefahrlos lesen kann; sie erfährt aber
         * die wirkliche Anzahl.
         */
        int given = argc;
        while (argc < f->u.n.nargs) {
            js_push(J, js_undef());
            argc++;
        }
        int bot = J->bot;
        bool constructing = J->constructing;
        J->bot = base;
        J->constructing = mode != CALL;
        ok = f->u.n.fn(J, given);
        J->bot = bot;
        J->constructing = constructing;
        if (ok && J->top > base + 2 + argc)
            result = *js_top(J, 0);
        if (ok && (J->fatal || J->throwing))
            ok = false;
    } else {
        js_fninfo *info = f->u.f.fn->u.fn;
        if (construct && (f->flags & (JS_F_ARROW | JS_F_GENERATOR | JS_F_ASYNC))) {
            J->top = base;
            js_throw(J, JS_E_TYPE, (f->flags & JS_F_ARROW)
                     ? "Arrow functions cannot be used as constructors"
                     : "Generators and async functions cannot be used as constructors");
            LEAVE(false);
        }
        if (construct) {
            js_object *proto = function_prototype(J, f);
            J->stack[base + 1] = js_obj(js_object_new(J, JS_C_OBJECT,
                                                      proto ? proto : J->object_proto));
        } else if (f->flags & JS_F_ARROW) {
            J->stack[base + 1] = f->u.f.self;
        } else if (J->stack[base + 1].t <= JS_T_NULL) {
            J->stack[base + 1] = js_obj(J->global);
        }
        js_object *env = scope_new(J, f->u.f.scope);
        int fbase = J->fbase, line = J->line;
        J->fbase = base;
        ok = true;
        /* new.target: wer mit "new" gemeint war - bei super(...) reicht es die abgeleitete Klasse durch */
        js_object *target = mode == CONSTRUCT ? f : mode == DERIVE ? J->new_target : NULL;
        J->new_target = NULL;
        if ((info->uses_new_target || (f->flags & JS_F_DERIVED)) && !(f->flags & JS_F_ARROW))
            js_define(J, env, J->hid[H_newtarget], target ? js_obj(target) : js_undef(), 0);
        if (info->uses_arguments && !(f->flags & JS_F_ARROW)) {
            js_object *args = js_array_new(J, (uint32_t)argc);
            js_define(J, env, J->k[K_arguments], js_obj(args), JS_P_WRITE);
            for (int i = 0; i < argc && ok; i++)
                ok = js_array_push(J, args, J->stack[base + 2 + i]);
        }
        ok = ok && bind_params(J, info, env, base, argc);
        if (ok) {
            hoist(J, info, env);
            /* der eigene Name einer benannten Funktion ist in ihr sichtbar */
            if (info->name && !js_own(env, info->name))
                js_define(J, env, info->name, js_obj(f), JS_P_WRITE);
            if (info->fields && !(f->flags & JS_F_DERIVED))
                ok = init_fields(J, info->fields, env);
        }
        if (ok && (f->flags & (JS_F_GENERATOR | JS_F_ASYNC))) {
            /* der Rumpf läuft später, als Koroutine: jetzt entsteht der Generator oder das Promise */
            ok = js_co_call(J, base, env, &result);
        } else if (ok && info->expr_body) {
            ok = eval(J, info->body, env);
            if (ok)
                result = *js_top(J, 0);
        } else if (ok) {
            int c = exec_list(J, info->body, env);
            if (c == C_THROW)
                ok = false;
            else if (c == C_RETURN)
                result = J->ret;
            J->ret = js_undef();
        }
        if (ok && mode != CALL && result.t != JS_T_OBJ)
            result = J->stack[base + 1];
        J->fbase = fbase;
        J->line = line;
        if (ok && (J->fatal || J->throwing))
            ok = false;
    }
    J->top = base;
    if (ok)
        js_push(J, result);
    LEAVE(ok);
}

bool js_call(js_state *J, int argc)
{
    return do_call(J, argc, CALL);
}

void js_hoist(js_state *J, js_fninfo *info, js_object *env)
{
    hoist(J, info, env);
}

bool js_call_mode(js_state *J, int argc, bool construct)
{
    return do_call(J, argc, construct ? CONSTRUCT : CALL);
}

bool js_has_property(js_state *J, js_object *o, js_string *key, bool *found)
{
    if (o->cls == JS_C_PROXY)
        return js_proxy_has(J, o, key, found);
    *found = js_lookup(o, key, NULL) != NULL;
    if (!*found && o->cls == JS_C_ARRAY)
        *found = (key->index != JS_NO_INDEX && key->index < o->u.arr.len) || key == J->k[K_length];
    if (!*found && o->cls == JS_C_HOST && o->u.host.cls && o->u.host.cls->get) {
        if (!o->u.host.cls->get(J, o, key, found))
            return false;
        if (*found)
            js_pop(J, 1);
    }
    /* über einen Proxy in der Kette der Prototypen */
    for (js_object *p = o->proto; !*found && p; p = p->proto) {
        if (p->cls == JS_C_PROXY)
            return js_proxy_has(J, p, key, found);
    }
    return true;
}

bool js_delete_property(js_state *J, js_object *o, js_string *key, bool *done)
{
    if (o->cls == JS_C_PROXY)
        return js_proxy_delete(J, o, key, done);
    *done = true;
    if (o->cls == JS_C_ARRAY && key->index != JS_NO_INDEX) {
        if (key->index < o->u.arr.len)
            o->u.arr.v[key->index] = js_undef();
    } else {
        *done = js_delete_own(J, o, key);
    }
    return true;
}

bool js_exec_body(js_state *J, js_object *f, js_object *env, js_value *result)
{
    js_fninfo *info = f->u.f.fn->u.fn;
    bool ok = true;
    *result = js_undef();
    J->fbase = 0;
    if (info->expr_body) {
        ok = eval(J, info->body, env);
        if (ok)
            *result = *js_top(J, 0);
    } else {
        int c = exec_list(J, info->body, env);
        if (c == C_THROW)
            ok = false;
        else if (c == C_RETURN)
            *result = J->ret;
        J->ret = js_undef();
    }
    return ok && !J->fatal && !J->throwing;
}

/* Stapel: [fn, arg0 ... argN-1] -> [objekt] */
bool js_construct(js_state *J, int argc)
{
    /* Platz für "this" hinter der Funktion schaffen */
    int first = J->top - argc;
    js_push(J, js_undef());
    for (int i = J->top - 1; i > first; i--)
        J->stack[i] = J->stack[i - 1];
    J->stack[first] = js_undef();
    return do_call(J, argc, CONSTRUCT);
}

/* Legt die Argumente eines Aufrufs auf den Stapel; liefert ihre Anzahl oder -1. */
static int push_arguments(js_state *J, js_node *list, js_object *env)
{
    int argc = 0;
    for (js_node *a = list; a; a = a->next) {
        if (a->type != N_SPREAD) {
            if (!eval(J, a, env))
                return -1;
            argc++;
            continue;
        }
        if (!eval(J, a->a, env))
            return -1;
        js_object *items = js_array_new(J, 0);
        js_push(J, js_obj(items));
        if (!js_iterate(J, J->top - 2, items))
            return -1;
        uint32_t n = items->cls == JS_C_ARRAY ? items->u.arr.len : 0;
        if (J->top + (int)n + 200 > JS_STACK_SIZE || n > JS_STACK_SIZE) {
            js_throw(J, JS_E_RANGE, "Too many arguments");
            return -1;
        }
        /* die beiden Hilfswerte durch die Glieder ersetzen */
        js_value *v = items->u.arr.v;
        J->top -= 2;
        js_value keep = js_obj(items);
        for (uint32_t i = 0; i < n; i++)
            J->stack[J->top + i] = v[i];
        J->top += (int)n;
        (void)keep;
        argc += (int)n;
    }
    return argc;
}

/* --- Klassen --------------------------------------------------------------------------------------- */

static bool eval_class(js_state *J, js_node *n, js_object *env)
{
    int base = J->top;
    js_object *parent = NULL, *parent_proto = J->object_proto;
    if (n->a) {
        if (!eval(J, n->a, env))
            return false;
        js_value *pv = js_top(J, 0);
        if (pv->t == JS_T_NULL) {
            parent_proto = NULL;
        } else if (!js_is_callable(pv)) {
            return js_throw(J, JS_E_TYPE, "Class extends value is not a constructor");
        } else {
            parent = pv->u.o;
            if (!js_get(J, J->top - 1, J->k[K_prototype]))
                return false;
            parent_proto = js_top(J, 0)->t == JS_T_OBJ ? js_top(J, 0)->u.o : NULL;
        }
    }
    /* der Name der Klasse ist in ihr selbst sichtbar - ebenso ihre privaten Namen */
    bool private_names = false;
    for (js_node *m = n->b; m && !private_names; m = m->next)
        private_names = m->flags & NF_PRIVATE;
    for (js_node *m = n->c->u.fn->fields; m && !private_names; m = m->next)
        private_names = m->flags & NF_PRIVATE;
    js_object *scope = n->u.str || private_names ? scope_new(J, env) : env;
    for (int list = 0; list < 2 && private_names; list++) {
        for (js_node *m = list ? n->c->u.fn->fields : n->b; m; m = m->next) {
            js_string *name = m->a ? m->a->u.str : NULL;
            if (!(m->flags & NF_PRIVATE) || js_own(scope, name))
                continue;               /* (Lese- und Schreibfunktion teilen sich einen Namen) */
            js_define(J, scope, name,
                      js_sym(js_symbol_new(J, name->data, name->len, JS_SYM_PRIVATE)), 0);
        }
    }
    js_object *ctor = js_function_new(J, n->c, scope);
    js_push(J, js_obj(ctor));
    js_object *proto = js_object_new(J, JS_C_OBJECT, parent_proto);
    js_push(J, js_obj(proto));
    js_define(J, ctor, J->k[K_prototype], js_obj(proto), 0);
    js_define(J, proto, J->k[K_constructor], js_obj(ctor), JS_P_WRITE | JS_P_CONF);
    if (parent)
        ctor->proto = parent;
    ctor->u.f.home = proto;
    if (n->u.str)
        js_define(J, scope, n->u.str, js_obj(ctor), 0);

    for (js_node *m = n->b; m; m = m->next) {
        js_object *target = (m->flags & NF_STATIC) ? ctor : proto;
        js_string *key;
        if ((m->flags & (NF_STATIC | NF_SHORTHAND)) == (NF_STATIC | NF_SHORTHAND)) {
            /* static { ... }: jetzt ausführen, mit der Klasse als "this" */
            js_push(J, js_obj(js_function_new(J, m->b, scope)));
            js_push(J, js_obj(ctor));
            if (!js_call(J, 0))
                return false;
            js_pop(J, 1);
            continue;
        }
        if (m->flags & NF_COMPUTED) {
            if (!eval(J, m->a, scope) || !js_to_key(J, J->top - 1, &key))
                return false;
        } else if (m->flags & NF_PRIVATE) {
            if (!private_key(J, scope, m->a->u.str, &key))
                return false;
            js_push(J, js_sym(key));
        } else {
            key = m->a->u.str;
            js_push(J, js_str(key));
        }
        if (m->flags & NF_FIELD) {
            /* statisches Feld: Im Anfangswert meint "this" die Klasse */
            js_value outer_this = J->stack[J->fbase + 1];
            bool ok = true;
            J->stack[J->fbase + 1] = js_obj(ctor);
            if (m->b)
                ok = eval(J, m->b, scope);
            else
                js_push(J, js_undef());
            J->stack[J->fbase + 1] = outer_this;
            if (!ok)
                return false;
            js_define(J, target, key, *js_top(J, 0), (m->flags & NF_PRIVATE) ? JS_P_WRITE : JS_P_ALL);
            js_pop(J, 2);
            continue;
        }
        js_object *fn = js_function_new(J, m->b, scope);
        fn->u.f.home = target;
        js_push(J, js_obj(fn));
        if (m->flags & (NF_GETTER | NF_SETTER)) {
            js_prop *p = js_own(target, key);
            if (!p || !(p->flags & JS_P_ACCESSOR)) {
                p = js_define(J, target, key, js_undef(), JS_P_ACCESSOR | JS_P_CONF);
                if (p)
                    p->set = NULL;
            }
            if (p && (m->flags & NF_GETTER))
                p->v = js_obj(fn);
            else if (p)
                p->set = fn;
        } else {
            js_define(J, target, key, js_obj(fn), JS_P_WRITE | JS_P_CONF);
        }
        js_pop(J, 2);
    }
    J->stack[base] = js_obj(ctor);
    J->top = base + 1;
    return true;
}

/* super(...) im Konstruktor einer abgeleiteten Klasse */
static bool eval_super_call(js_state *J, js_node *n, js_object *env)
{
    js_object *self_fn = J->stack[J->fbase].t == JS_T_OBJ ? J->stack[J->fbase].u.o : NULL;
    js_object *parent = self_fn && self_fn->cls == JS_C_FUNCTION ? self_fn->proto : NULL;
    js_value pv = parent ? js_obj(parent) : js_undef();
    if (!parent || !js_is_callable(&pv))
        return js_throw(J, JS_E_SYNTAX, "'super' keyword unexpected here");
    int base = J->top;
    js_push(J, pv);
    js_push(J, J->stack[J->fbase + 1]);
    int argc = push_arguments(J, n->b, env);
    if (argc < 0)
        return false;
    if (parent->cls == JS_C_NATIVE) {
        /*
         * Eingebaute Elternklasse (Error, Array, Map ...): Sie erzeugt das
         * Objekt - das wird "this" und bekommt den Prototyp der eigenen Klasse.
         */
        J->stack[base + 1] = js_undef();
        if (!do_call(J, argc, CONSTRUCT))
            return false;
        js_value *made = js_top(J, 0), *self = &J->stack[J->fbase + 1];
        if (made->t == JS_T_OBJ && self->t == JS_T_OBJ && made->u.o != self->u.o) {
            made->u.o->proto = self->u.o->proto;
            *self = *made;
        }
    } else {
        js_prop *target = js_lookup(env, J->hid[H_newtarget], NULL);
        J->new_target = target && target->v.t == JS_T_OBJ ? target->v.u.o : NULL;
        if (!do_call(J, argc, DERIVE))
            return false;
        if (js_top(J, 0)->t == JS_T_OBJ)
            J->stack[J->fbase + 1] = *js_top(J, 0);
    }
    js_pop(J, 1);
    js_fninfo *info = self_fn->u.f.fn->u.fn;
    if (info->fields && !init_fields(J, info->fields, env))
        return false;
    return js_push(J, J->stack[J->fbase + 1]);
}

/* Das Objekt, in dem "super.name" sucht */
static bool push_super(js_state *J)
{
    js_object *self_fn = J->stack[J->fbase].t == JS_T_OBJ ? J->stack[J->fbase].u.o : NULL;
    js_object *home = self_fn && self_fn->cls == JS_C_FUNCTION ? self_fn->u.f.home : NULL;
    if (!home || !home->proto)
        return js_throw(J, JS_E_SYNTAX, "'super' keyword unexpected here");
    return js_push(J, js_obj(home->proto));
}

/* --- Rechnen ---------------------------------------------------------------------------------------- */

static bool compare(js_state *J, int op, bool *out)
{
    js_value *a = js_top(J, 1), *b = js_top(J, 0);
    if (a->t == JS_T_NUM && b->t == JS_T_NUM) {
        double x = a->u.n, y = b->u.n;
        *out = op == OP_LT ? x < y : op == OP_LE ? x <= y : op == OP_GT ? x > y : x >= y;
        return true;
    }
    if (!js_to_primitive(J, J->top - 2, 1) || !js_to_primitive(J, J->top - 1, 1))
        return false;
    a = js_top(J, 1);
    b = js_top(J, 0);
    if (a->t == JS_T_STR && b->t == JS_T_STR) {
        int c = js_string_compare(a->u.s, b->u.s);
        *out = op == OP_LT ? c < 0 : op == OP_LE ? c <= 0 : op == OP_GT ? c > 0 : c >= 0;
        return true;
    }
    double x, y;
    if (!js_to_number(J, J->top - 2, &x) || !js_to_number(J, J->top - 1, &y))
        return false;
    *out = op == OP_LT ? x < y : op == OP_LE ? x <= y : op == OP_GT ? x > y : x >= y;
    return true;
}

/* [a, b] -> [a op b] */
static bool binary(js_state *J, int op)
{
    js_value *a = js_top(J, 1), *b = js_top(J, 0);
    double x, y;
    bool flag;
    switch (op) {
    case OP_ADD:
        if (a->t == JS_T_NUM && b->t == JS_T_NUM) {
            *a = js_num(a->u.n + b->u.n);
            break;
        }
        if (!js_to_primitive(J, J->top - 2, 0) || !js_to_primitive(J, J->top - 1, 0))
            return false;
        a = js_top(J, 1);
        b = js_top(J, 0);
        if (a->t == JS_T_STR || b->t == JS_T_STR) {
            if (!js_to_string(J, J->top - 2) || !js_to_string(J, J->top - 1))
                return false;
            a = js_top(J, 1);
            *a = js_str(js_string_concat(J, a->u.s, js_top(J, 0)->u.s));
            break;
        }
        if (!js_to_number(J, J->top - 2, &x) || !js_to_number(J, J->top - 1, &y))
            return false;
        *js_top(J, 1) = js_num(x + y);
        break;
    case OP_SUB: case OP_MUL: case OP_DIV: case OP_MOD: case OP_POW:
        if (a->t == JS_T_NUM && b->t == JS_T_NUM) {
            x = a->u.n;
            y = b->u.n;
        } else if (!js_to_number(J, J->top - 2, &x) || !js_to_number(J, J->top - 1, &y)) {
            return false;
        }
        *js_top(J, 1) = js_num(op == OP_SUB ? x - y : op == OP_MUL ? x * y : op == OP_DIV ? x / y
                               : op == OP_MOD ? js_fmod(x, y) : js_pow(x, y));
        break;
    case OP_SHL: case OP_SHR: case OP_USHR: case OP_AND: case OP_OR: case OP_XOR: {
        int32_t p, q;
        if (!js_to_int32(J, J->top - 2, &p) || !js_to_int32(J, J->top - 1, &q))
            return false;
        uint32_t shift = (uint32_t)q & 31;
        double r = op == OP_SHL ? (double)(int32_t)((uint32_t)p << shift)
                 : op == OP_SHR ? (double)(p >> shift)
                 : op == OP_USHR ? (double)((uint32_t)p >> shift)
                 : op == OP_AND ? (double)(p & q) : op == OP_OR ? (double)(p | q)
                 : (double)(p ^ q);
        *js_top(J, 1) = js_num(r);
        break;
    }
    case OP_EQ: case OP_NE:
        if (!js_loose_equal(J, J->top - 2, J->top - 1, &flag))
            return false;
        *js_top(J, 1) = js_bool(flag == (op == OP_EQ));
        break;
    case OP_SEQ: case OP_SNE:
        *a = js_bool(js_strict_equal(a, b) == (op == OP_SEQ));
        break;
    case OP_LT: case OP_LE: case OP_GT: case OP_GE:
        if (!compare(J, op, &flag))
            return false;
        *js_top(J, 1) = js_bool(flag);
        break;
    case OP_INSTANCEOF:
        if (!js_instanceof(J, a, b, &flag))
            return false;
        *js_top(J, 1) = js_bool(flag);
        break;
    case OP_IN: {
        if (b->t != JS_T_OBJ)
            return js_throw(J, JS_E_TYPE, "Cannot use 'in' operator on a value that is not an object");
        js_string *key;
        if (!js_to_key(J, J->top - 2, &key))
            return false;
        bool found;
        if (!js_has_property(J, js_top(J, 0)->u.o, key, &found))
            return false;
        *js_top(J, 1) = js_bool(found);
        break;
    }
    default:
        return js_throw(J, JS_E_SYNTAX, "Unknown operator");
    }
    js_pop(J, 1);
    return true;
}

/* --- Zuweisen und Zerlegen -------------------------------------------------------------------------- */

static bool declare(js_state *J, js_object *env, js_string *name, int mode)
{
    if (mode == A_ASSIGN || mode == A_VAR)
        return set_var(J, env, name);
    js_define(J, env, name, *js_top(J, 0),
              mode == A_CONST ? JS_P_ENUM | JS_P_CONF | JS_P_CONST : JS_P_ALL);
    return true;
}

/* Der Wert liegt oben auf dem Stapel und bleibt dort. */
static bool assign_to(js_state *J, js_node *t, js_object *env, int mode)
{
    if (!enter(J))
        return false;
    bool ok = true;
    int base = J->top;
    switch (t->type) {
    case N_IDENT:
        ok = declare(J, env, t->u.str, mode);
        break;
    case N_MEMBER: {
        js_string *key;
        ok = (t->a->type == N_SUPER ? js_push(J, J->stack[J->fbase + 1]) : eval(J, t->a, env)) &&
             member_key(J, t, env, &key, true) &&
             js_push(J, J->stack[base - 1]) && js_put(J, J->top - 2, key);
        break;
    }
    case N_INDEX:
        ok = eval(J, t->a, env) && eval(J, t->b, env) && js_push(J, J->stack[base - 1]) &&
             js_put_value(J);
        break;
    case N_CHAIN:
        ok = js_throw(J, JS_E_SYNTAX, "Invalid assignment target");
        break;
    case N_ARRAY_PAT: {
        /* Glied für Glied: nur so viele, wie das Muster braucht */
        js_iter it;
        ok = js_iter_open(J, base - 1, &it);
        for (js_node *e = t->a; e && ok; e = e->next) {
            if (e->type == N_REST) {
                js_object *rest = js_array_new(J, 0);
                js_push(J, js_obj(rest));
                for (;;) {
                    bool done;
                    ok = tick(J) && js_iter_next(J, &it, &done);
                    if (!ok || done)
                        break;
                    ok = js_array_push(J, rest, *js_top(J, 0));
                    js_pop(J, 1);
                    if (!ok)
                        break;
                }
                ok = ok && assign_to(J, e->a, env, mode);
                js_pop(J, 1);
                break;
            }
            bool done;
            ok = js_iter_next(J, &it, &done);
            if (!ok)
                break;
            if (done)
                js_push(J, js_undef());
            if (e->type == N_HOLE) {
                js_pop(J, 1);
                continue;
            }
            js_node *target = e;
            if (e->type == N_DEFAULT) {
                target = e->a;
                if (js_top(J, 0)->t == JS_T_UNDEF) {
                    js_pop(J, 1);
                    ok = eval(J, e->b, env);
                }
            }
            ok = ok && assign_to(J, target, env, mode);
            js_pop(J, 1);
        }
        /* das Muster ist zu Ende, die Quelle vielleicht nicht: ihr Bescheid geben */
        if (!it.done)
            ok = js_iter_close(J, &it, !ok);
        break;
    }
    case N_OBJECT_PAT: {
        if (J->stack[base - 1].t <= JS_T_NULL) {
            ok = js_throw(J, JS_E_TYPE, "Cannot destructure '%s'",
                          J->stack[base - 1].t == JS_T_NULL ? "null" : "undefined");
            break;
        }
        js_object *used = js_array_new(J, 0);
        js_push(J, js_obj(used));
        for (js_node *e = t->a; e && ok; e = e->next) {
            if (e->type == N_REST) {
                /* alles Übrige in ein neues Objekt */
                js_object *rest = js_object_new(J, JS_C_OBJECT, J->object_proto);
                js_push(J, js_obj(rest));
                if (J->stack[base - 1].t == JS_T_OBJ) {
                    js_object *keys = js_array_new(J, 0);
                    js_push(J, js_obj(keys));
                    ok = js_own_keys(J, J->stack[base - 1].u.o, JS_KEYS_ENUM | JS_KEYS_BOTH, keys);
                    for (uint32_t k = 0; k < keys->u.arr.len && ok; k++) {
                        js_string *key = keys->u.arr.v[k].u.s;
                        bool taken = false;
                        for (uint32_t u = 0; u < used->u.arr.len; u++)
                            taken |= used->u.arr.v[u].u.s == key;
                        if (taken)
                            continue;
                        ok = js_get(J, base - 1, key);
                        if (ok) {
                            js_define(J, rest, key, *js_top(J, 0), JS_P_ALL);
                            js_pop(J, 1);
                        }
                    }
                    js_pop(J, 1);
                }
                ok = ok && assign_to(J, e->a, env, mode);
                js_pop(J, 1);
                break;
            }
            js_string *key;
            if (e->flags & NF_COMPUTED) {
                ok = eval(J, e->a, env) && js_to_key(J, J->top - 1, &key);
                if (!ok)
                    break;
                js_pop(J, 1);
            } else {
                key = e->a->u.str;
            }
            ok = js_array_push(J, used, js_str(key)) && js_get(J, base - 1, key);
            if (!ok)
                break;
            js_node *target = e->b;
            if (target->type == N_DEFAULT) {
                if (js_top(J, 0)->t == JS_T_UNDEF) {
                    js_pop(J, 1);
                    ok = eval(J, target->b, env);
                }
                target = target->a;
            }
            ok = ok && assign_to(J, target, env, mode);
            js_pop(J, 1);
        }
        break;
    }
    default:
        ok = js_throw(J, JS_E_SYNTAX, "Invalid assignment target");
    }
    if (ok)
        J->top = base;
    LEAVE(ok);
}

/* --- Ausdrücke ------------------------------------------------------------------------------------- */

/* Liest das Ziel einer Zuweisung mit Rechnung: [..] -> [obj?, key?, wert]; merkt sich den Schlüssel */
static bool eval_call(js_state *J, js_node *n, js_object *env)
{
    int base = J->top;
    js_node *callee = n->a;
    if (callee->type == N_SUPER)
        return eval_super_call(J, n, env);
    if (callee->type == N_MEMBER || callee->type == N_INDEX) {
        bool super = callee->a->type == N_SUPER;
        if (!(super ? push_super(J) : eval(J, callee->a, env)))
            return false;
        if (J->short_circuit)
            return true;
        if ((callee->flags & NF_OPTIONAL) && js_top(J, 0)->t <= JS_T_NULL) {
            *js_top(J, 0) = js_undef();
            J->short_circuit = true;
            return true;
        }
        if (callee->type == N_MEMBER) {
            js_string *key;
            if (!member_key(J, callee, env, &key, false) || !js_get(J, J->top - 1, key))
                return false;
        } else {
            js_string *key;
            if (!eval(J, callee->b, env) || !js_to_key(J, J->top - 1, &key))
                return false;
            js_pop(J, 1);
            if (!js_get(J, J->top - 1, key))
                return false;
        }
        /* [obj, fn] -> [fn, this] */
        js_value fn = *js_top(J, 0);
        *js_top(J, 0) = super ? J->stack[J->fbase + 1] : *js_top(J, 1);
        *js_top(J, 1) = fn;
    } else {
        if (!eval(J, callee, env))
            return false;
        if (J->short_circuit)
            return true;
        js_push(J, js_undef());
    }
    if ((n->flags & NF_OPTIONAL) && J->stack[base].t <= JS_T_NULL) {
        J->top = base;
        J->short_circuit = true;
        return js_push(J, js_undef());
    }
    if (!js_is_callable(&J->stack[base])) {
        J->line = n->line;
        return js_throw(J, JS_E_TYPE, "%s is not a function", callee_name(callee));
    }
    int argc = push_arguments(J, n->b, env);
    if (argc < 0)
        return false;
    J->line = n->line;
    return do_call(J, argc, CALL);
}

/*
 * f`text ${wert} text`: ruft f(stücke, wert, ...). Das Feld der Textstücke
 * (mit der rohen Fassung unter "raw") gibt es je Stelle im Quelltext nur
 * einmal - Bibliotheken erkennen daran dieselbe Vorlage wieder.
 */
static bool eval_tagged(js_state *J, js_node *n, js_object *env)
{
    int base = J->top;
    js_node *callee = n->a;
    /* [funktion, this] wie bei einem Aufruf */
    if (callee->type == N_MEMBER || callee->type == N_INDEX) {
        if (!eval(J, callee->a, env))
            return false;
        if (callee->type == N_MEMBER) {
            js_string *key;
            if (!member_key(J, callee, env, &key, false) || !js_get(J, J->top - 1, key))
                return false;
        } else {
            js_string *key;
            if (!eval(J, callee->b, env) || !js_to_key(J, J->top - 1, &key))
                return false;
            js_pop(J, 1);
            if (!js_get(J, J->top - 1, key))
                return false;
        }
        js_value fn = *js_top(J, 0);
        *js_top(J, 0) = *js_top(J, 1);
        *js_top(J, 1) = fn;
    } else {
        if (!eval(J, callee, env))
            return false;
        js_push(J, js_undef());
    }
    if (!js_is_callable(&J->stack[base])) {
        J->line = n->line;
        return js_throw(J, JS_E_TYPE, "%s is not a function", callee_name(callee));
    }
    if (!n->u.obj) {
        js_object *parts = js_array_new(J, 0), *raw = js_array_new(J, 0);
        js_push(J, js_obj(parts));
        js_push(J, js_obj(raw));
        for (js_node *part = n->b; part; part = part->next) {
            if (!js_array_push(J, parts, js_str(part->u.str)))
                return false;
        }
        for (js_node *part = n->c; part; part = part->next) {
            if (!js_array_push(J, raw, js_str(part->u.str)))
                return false;
        }
        js_define(J, parts, js_intern_cstr(J, "raw"), js_obj(raw), 0);
        parts->flags |= JS_F_NOEXT;
        raw->flags |= JS_F_NOEXT;
        /* festhalten: Die Speicherbereinigung findet das Feld über die Liste aller Vorlagen */
        js_string *list_name = js_intern_cstr(J, "templates");
        js_prop *list = js_own(J->registry, list_name);
        if (!list || !js_is_array(&list->v))
            list = js_define(J, J->registry, list_name, js_obj(js_array_new(J, 0)), JS_P_ALL);
        if (!list || !js_array_push(J, list->v.u.o, js_obj(parts)))
            return false;
        n->u.obj = parts;
        js_pop(J, 2);
    }
    js_push(J, js_obj(n->u.obj));
    int argc = 1;
    for (js_node *value = n->d; value; value = value->next, argc++) {
        if (!eval(J, value, env))
            return false;
    }
    J->line = n->line;
    return do_call(J, argc, CALL);
}

static bool eval_assign(js_state *J, js_node *n, js_object *env)
{
    js_node *t = n->a;
    int base = J->top, op = n->op;
    if (op == OP_NONE) {
        if (t->type == N_MEMBER || t->type == N_INDEX) {
            js_push(J, js_undef());     /* Platz für das Ergebnis */
            bool super = t->a->type == N_SUPER;
            js_string *name = t->u.str;
            if (!(super ? js_push(J, J->stack[J->fbase + 1]) : eval(J, t->a, env)))
                return false;
            if (t->type == N_MEMBER && !member_key(J, t, env, &name, true))
                return false;
            if (t->type == N_INDEX && !eval(J, t->b, env))
                return false;
            if (!eval(J, n->b, env))
                return false;
            J->stack[base] = *js_top(J, 0);
            if (!(t->type == N_MEMBER ? js_put(J, J->top - 2, name) : js_put_value(J)))
                return false;
            J->top = base + 1;
            return true;
        }
        if (!eval(J, n->b, env))
            return false;
        /* einer namenlosen Funktion ihren Namen geben: f = function () {} */
        if (t->type == N_IDENT && (n->b->type == N_FUNC || n->b->type == N_CLASS) &&
            js_top(J, 0)->t == JS_T_OBJ && js_top(J, 0)->u.o->cls == JS_C_FUNCTION &&
            !js_top(J, 0)->u.o->u.f.fn->u.fn->name && !js_own(js_top(J, 0)->u.o, J->k[K_name]))
            js_define(J, js_top(J, 0)->u.o, J->k[K_name], js_str(t->u.str), JS_P_CONF);
        return assign_to(J, t, env, A_ASSIGN);
    }

    /* x op= y: erst den alten Wert lesen */
    js_string *key = NULL;
    js_push(J, js_undef());             /* Platz für das Ergebnis */
    if (t->type == N_IDENT) {
        if (!get_var(J, env, t->u.str, false))
            return false;
    } else if (t->type == N_MEMBER || t->type == N_INDEX) {
        if (!eval(J, t->a, env))
            return false;
        if (t->type == N_MEMBER) {
            if (!member_key(J, t, env, &key, true))
                return false;
        } else {
            if (!eval(J, t->b, env) || !js_to_key(J, J->top - 1, &key))
                return false;
        }
        if (!js_get(J, base + 1, key))
            return false;
    } else {
        return js_throw(J, JS_E_SYNTAX, "Invalid assignment target");
    }
    if (op == OP_LAND || op == OP_LOR || op == OP_NULLISH) {
        js_value *old = js_top(J, 0);
        bool keep = op == OP_LAND ? !js_truthy(old) : op == OP_LOR ? js_truthy(old)
                                                                  : old->t > JS_T_NULL;
        if (keep) {
            J->stack[base] = *old;
            J->top = base + 1;
            return true;
        }
        js_pop(J, 1);
        if (!eval(J, n->b, env))
            return false;
    } else if (!eval(J, n->b, env) || !binary(J, op)) {
        return false;
    }
    J->stack[base] = *js_top(J, 0);
    if (t->type == N_IDENT) {
        if (!set_var(J, env, t->u.str))
            return false;
    } else if (!js_put(J, base + 1, key)) {
        return false;
    }
    J->top = base + 1;
    return true;
}

static bool eval_update(js_state *J, js_node *n, js_object *env)
{
    js_node *t = n->a;
    int base = J->top;
    js_string *key = NULL;
    double old;
    js_push(J, js_undef());
    if (t->type == N_IDENT) {
        if (!get_var(J, env, t->u.str, false))
            return false;
    } else if (t->type == N_MEMBER || t->type == N_INDEX) {
        if (!eval(J, t->a, env))
            return false;
        if (t->type == N_MEMBER) {
            if (!member_key(J, t, env, &key, true))
                return false;
        } else if (!eval(J, t->b, env) || !js_to_key(J, J->top - 1, &key)) {
            return false;
        }
        if (!js_get(J, base + 1, key))
            return false;
    } else {
        return js_throw(J, JS_E_SYNTAX, "Invalid increment target");
    }
    if (!js_to_number(J, J->top - 1, &old))
        return false;
    double fresh = n->op == OP_INC ? old + 1 : old - 1;
    J->stack[base] = js_num((n->flags & NF_PREFIX) ? fresh : old);
    *js_top(J, 0) = js_num(fresh);
    if (t->type == N_IDENT) {
        if (!set_var(J, env, t->u.str))
            return false;
    } else if (!js_put(J, base + 1, key)) {
        return false;
    }
    J->top = base + 1;
    return true;
}

static bool eval_object(js_state *J, js_node *n, js_object *env)
{
    js_object *o = js_object_new(J, JS_C_OBJECT, J->object_proto);
    int base = J->top;
    js_push(J, js_obj(o));
    for (js_node *p = n->a; p; p = p->next) {
        if (p->type == N_SPREAD) {
            if (!eval(J, p->a, env))
                return false;
            js_value *src = js_top(J, 0);
            if (src->t == JS_T_OBJ) {
                js_object *keys = js_array_new(J, 0);
                js_push(J, js_obj(keys));
                if (!js_own_keys(J, src->u.o, JS_KEYS_ENUM | JS_KEYS_BOTH, keys))
                    return false;
                for (uint32_t i = 0; i < keys->u.arr.len; i++) {
                    js_string *key = keys->u.arr.v[i].u.s;
                    if (!js_get(J, J->top - 2, key))
                        return false;
                    js_define(J, o, key, *js_top(J, 0), JS_P_ALL);
                    js_pop(J, 1);
                }
            }
            J->top = base + 1;
            continue;
        }
        js_string *key;
        if (p->flags & NF_COMPUTED) {
            if (!eval(J, p->a, env) || !js_to_key(J, J->top - 1, &key))
                return false;
        } else {
            key = p->a->u.str;
            js_push(J, js_str(key));
        }
        if (!eval(J, p->b, env))
            return false;
        js_value *v = js_top(J, 0);
        if (p->flags & (NF_GETTER | NF_SETTER)) {
            js_prop *prop = js_own(o, key);
            if (!prop || !(prop->flags & JS_P_ACCESSOR)) {
                prop = js_define(J, o, key, js_undef(), JS_P_ACCESSOR | JS_P_ENUM | JS_P_CONF);
                if (prop)
                    prop->set = NULL;
            }
            if (prop && (p->flags & NF_GETTER))
                prop->v = *v;
            else if (prop && v->t == JS_T_OBJ)
                prop->set = v->u.o;
        } else if (key->len == 9 && memcmp(key->data, "__proto__", 9) == 0 &&
                   !(p->flags & (NF_COMPUTED | NF_SHORTHAND | NF_METHOD))) {
            if (v->t == JS_T_OBJ || v->t == JS_T_NULL)
                o->proto = v->t == JS_T_OBJ ? v->u.o : NULL;
        } else {
            if ((p->flags & NF_METHOD) && v->t == JS_T_OBJ && v->u.o->cls == JS_C_FUNCTION)
                v->u.o->u.f.home = o;
            js_define(J, o, key, *v, JS_P_ALL);
        }
        J->top = base + 1;
    }
    return true;
}

static bool eval(js_state *J, js_node *n, js_object *env)
{
    if (!enter(J))
        return false;
    bool ok = true;
    int base = J->top;
    switch (n->type) {
    case N_NUM: js_push(J, js_num(n->u.num)); break;
    case N_STR: js_push(J, js_str(n->u.str)); break;
    case N_NULL: js_push(J, js_null()); break;
    case N_TRUE: js_push(J, js_bool(true)); break;
    case N_FALSE: js_push(J, js_bool(false)); break;
    case N_HOLE: js_push(J, js_undef()); break;
    case N_THIS: js_push(J, J->stack[J->fbase + 1]); break;
    case N_IDENT:
        ok = get_var(J, env, n->u.str, false);
        if (!ok)
            J->line = n->line;
        break;
    case N_TEMPLATE:
        js_push(J, js_str(J->empty));
        for (js_node *part = n->a; part && ok; part = part->next) {
            ok = eval(J, part, env) && js_to_string(J, J->top - 1);
            if (ok) {
                *js_top(J, 1) = js_str(js_string_concat(J, js_top(J, 1)->u.s, js_top(J, 0)->u.s));
                js_pop(J, 1);
            }
        }
        break;
    case N_REGEX: {
        js_object *re = js_regex_new(J, n->u.str, n->a->u.str);
        ok = re != NULL && js_push(J, js_obj(re));
        break;
    }
    case N_ARRAY: {
        js_object *a = js_array_new(J, 0);
        js_push(J, js_obj(a));
        for (js_node *e = n->a; e && ok; e = e->next) {
            if (e->type == N_SPREAD) {
                ok = eval(J, e->a, env) && js_iterate(J, J->top - 1, a);
            } else {
                ok = eval(J, e, env) && js_array_push(J, a, *js_top(J, 0));
            }
            J->top = base + 1;
        }
        break;
    }
    case N_OBJECT:
        ok = eval_object(J, n, env);
        break;
    case N_FUNC: {
        js_object *f = js_function_new(J, n, env);
        if (f->flags & JS_F_ARROW)
            f->u.f.self = J->stack[J->fbase + 1];
        js_push(J, js_obj(f));
        break;
    }
    case N_CLASS:
        ok = eval_class(J, n, env);
        break;
    case N_MEMBER:
        ok = n->a->type == N_SUPER ? push_super(J) : eval(J, n->a, env);
        if (!ok || J->short_circuit)
            break;
        if ((n->flags & NF_OPTIONAL) && js_top(J, 0)->t <= JS_T_NULL) {
            *js_top(J, 0) = js_undef();
            J->short_circuit = true;
            break;
        }
        {
            js_string *key;
            ok = member_key(J, n, env, &key, false) && js_get(J, J->top - 1, key);
        }
        if (ok) {
            J->stack[base] = *js_top(J, 0);
            J->top = base + 1;
        } else {
            J->line = n->line;
        }
        break;
    case N_INDEX:
        ok = n->a->type == N_SUPER ? push_super(J) : eval(J, n->a, env);
        if (!ok || J->short_circuit)
            break;
        if ((n->flags & NF_OPTIONAL) && js_top(J, 0)->t <= JS_T_NULL) {
            *js_top(J, 0) = js_undef();
            J->short_circuit = true;
            break;
        }
        ok = eval(J, n->b, env) && js_get_value(J);
        if (!ok)
            J->line = n->line;
        break;
    case N_CHAIN:
        ok = eval(J, n->a, env);
        J->short_circuit = false;
        break;
    case N_CALL:
        ok = eval_call(J, n, env);
        break;
    case N_NEW: {
        ok = eval(J, n->a, env);
        if (!ok)
            break;
        if (!js_is_callable(js_top(J, 0))) {
            J->line = n->line;
            ok = js_throw(J, JS_E_TYPE, "%s is not a constructor", callee_name(n->a));
            break;
        }
        int argc = push_arguments(J, n->b, env);
        J->line = n->line;
        ok = argc >= 0 && js_construct(J, argc);
        break;
    }
    case N_UNARY:
        switch (n->op) {
        case OP_TYPEOF:
            ok = n->a->type == N_IDENT ? get_var(J, env, n->a->u.str, true) : eval(J, n->a, env);
            if (ok) {
                const char *type = js_typeof(js_top(J, 0));
                *js_top(J, 0) = js_str(js_intern_cstr(J, type));
            }
            break;
        case OP_DELETE:
            if (n->a->type == N_MEMBER || n->a->type == N_INDEX) {
                js_string *key = n->a->u.str;
                ok = eval(J, n->a->a, env);
                if (ok && n->a->type == N_INDEX)
                    ok = eval(J, n->a->b, env) && js_to_key(J, J->top - 1, &key);
                if (!ok)
                    break;
                js_value *target = &J->stack[base];
                bool done = true;
                if (target->t == JS_T_OBJ && !js_delete_property(J, target->u.o, key, &done)) {
                    ok = false;
                    break;
                }
                J->top = base;
                js_push(J, js_bool(done));
            } else if (n->a->type == N_IDENT) {
                js_push(J, js_bool(js_delete_own(J, J->global, n->a->u.str) &&
                                   !js_lookup(env, n->a->u.str, NULL)));
            } else {
                ok = eval(J, n->a, env);
                if (ok)
                    *js_top(J, 0) = js_bool(true);
            }
            break;
        case OP_VOID:
            ok = eval(J, n->a, env);
            if (ok)
                *js_top(J, 0) = js_undef();
            break;
        case OP_NOT:
            ok = eval(J, n->a, env);
            if (ok)
                *js_top(J, 0) = js_bool(!js_truthy(js_top(J, 0)));
            break;
        case OP_NEG: case OP_POS: {
            double x;
            ok = eval(J, n->a, env) && js_to_number(J, J->top - 1, &x);
            if (ok)
                *js_top(J, 0) = js_num(n->op == OP_NEG ? -x : x);
            break;
        }
        case OP_BITNOT: {
            int32_t x;
            ok = eval(J, n->a, env) && js_to_int32(J, J->top - 1, &x);
            if (ok)
                *js_top(J, 0) = js_num(~x);
            break;
        }
        default:
            ok = js_throw(J, JS_E_SYNTAX, "Unknown operator");
        }
        break;
    case N_UPDATE:
        ok = eval_update(J, n, env);
        break;
    case N_BINARY:
        if (n->op == OP_IN && n->a->type == N_STR && (n->a->flags & NF_PRIVATE)) {
            /* #name in objekt: Hat das Objekt dieses private Mitglied? */
            js_string *key;
            ok = private_key(J, env, n->a->u.str, &key) && eval(J, n->b, env);
            if (ok && js_top(J, 0)->t != JS_T_OBJ)
                ok = js_throw(J, JS_E_TYPE, "Cannot use 'in' operator on a value that is not an object");
            if (ok)
                *js_top(J, 0) = js_bool(js_lookup(js_top(J, 0)->u.o, key, NULL) != NULL);
            break;
        }
        ok = eval(J, n->a, env) && eval(J, n->b, env) && binary(J, n->op);
        if (!ok && !J->fatal)
            J->line = n->line;
        break;
    case N_NEWTARGET: {
        js_prop *target = js_lookup(env, J->hid[H_newtarget], NULL);
        js_push(J, target ? target->v : js_undef());
        break;
    }
    case N_TAGGED:
        ok = eval_tagged(J, n, env);
        break;
    case N_IMPORT_CALL:
        ok = eval(J, n->a, env) && js_module_import(J, env);
        break;
    case N_IMPORT_META:
        ok = js_module_meta(J, env);
        break;
    case N_LOGICAL:
        ok = eval(J, n->a, env);
        if (ok) {
            js_value *v = js_top(J, 0);
            bool keep = n->op == OP_LAND ? !js_truthy(v) : n->op == OP_LOR ? js_truthy(v)
                                                                             : v->t > JS_T_NULL;
            if (!keep) {
                js_pop(J, 1);
                ok = eval(J, n->b, env);
            }
        }
        break;
    case N_ASSIGN:
        ok = eval_assign(J, n, env);
        break;
    case N_COND:
        ok = eval(J, n->a, env);
        if (ok) {
            bool yes = js_truthy(js_top(J, 0));
            js_pop(J, 1);
            ok = eval(J, yes ? n->b : n->c, env);
        }
        break;
    case N_SEQ:
        for (js_node *e = n->a; e && ok; e = e->next) {
            J->top = base;
            ok = eval(J, e, env);
        }
        break;
    case N_SUPER:
        ok = js_throw(J, JS_E_SYNTAX, "'super' keyword unexpected here");
        break;
    case N_YIELD:
        ok = (n->a ? eval(J, n->a, env) : js_push(J, js_undef())) &&
             js_co_yield(J, n->flags & NF_DELEGATE);
        break;
    case N_AWAIT:
        ok = eval(J, n->a, env) && js_co_await(J);
        break;
    default:
        ok = js_throw(J, JS_E_SYNTAX, "Unexpected expression");
    }
    if (ok && J->top != base + 1) {
        /* sollte nie vorkommen; lieber abbrechen als mit schiefem Stapel weiterrechnen */
        js_fatal(J, "Interner Fehler im Skript (Stapel)");
        ok = false;
    }
    LEAVE(ok);
}

/* --- Anweisungen ------------------------------------------------------------------------------------ */

static bool loop_control(js_state *J, int *c, js_string *label)
{
    /* liefert true, wenn die Schleife enden soll */
    if (*c == C_BREAK) {
        if (!J->label || J->label == label) {
            J->label = NULL;
            *c = C_NORMAL;
        }
        return true;
    }
    if (*c == C_CONTINUE) {
        if (!J->label || J->label == label) {
            J->label = NULL;
            *c = C_NORMAL;
            return false;
        }
        return true;
    }
    return *c != C_NORMAL;
}

/* Bindet den Laufwert einer for-in/for-of-Schleife (liegt oben auf dem Stapel) */
static bool bind_loop_value(js_state *J, js_node *head, js_object *env)
{
    if (head->type == S_VAR) {
        int mode = (head->flags & NF_CONST) ? A_CONST : (head->flags & NF_LET) ? A_LET : A_VAR;
        return assign_to(J, head->a->a, env, mode);
    }
    return assign_to(J, head, env, A_ASSIGN);
}

/*
 * for await (x of quelle) in einer async-Funktion: wie for-of, aber die Quelle
 * darf ihre Glieder als Promises liefern ([Symbol.asyncIterator]); auf jedes
 * wird gewartet.
 */
static int exec_for_await(js_state *J, js_node *n, js_object *env, js_string *label)
{
    int base = J->top, c = C_NORMAL;
    bool lexical = n->a->type == S_VAR && (n->a->flags & (NF_LET | NF_CONST));
    bool open = false, plain = false;
    if (!eval(J, n->b, env))
        return C_THROW;
    /* [quelle] -> [iterator, next] */
    if (js_top(J, 0)->t <= JS_T_NULL) {
        js_throw(J, JS_E_TYPE, "Value is not async iterable");
        return C_THROW;
    }
    if (!js_get(J, base, J->sym[SYM_asyncIterator]))
        return C_THROW;
    if (!js_is_callable(js_top(J, 0))) {
        js_pop(J, 1);
        plain = true;                   /* eine gewöhnliche Quelle: auf jedes Glied wird gewartet */
        if (!js_get(J, base, J->sym[SYM_iterator]))
            return C_THROW;
    }
    if (!js_is_callable(js_top(J, 0))) {
        js_throw(J, JS_E_TYPE, "Value is not async iterable");
        return C_THROW;
    }
    js_push(J, J->stack[base]);
    if (!js_call(J, 0))
        return C_THROW;
    if (js_top(J, 0)->t != JS_T_OBJ) {
        js_throw(J, JS_E_TYPE, "Result of the Symbol.asyncIterator method is not an object");
        return C_THROW;
    }
    J->stack[base] = *js_top(J, 0);
    js_pop(J, 1);
    if (!js_get(J, base, J->k[K_next]))
        return C_THROW;
    int inner = J->top;
    open = true;
    for (;;) {
        if (!tick(J)) {
            c = C_THROW;
            break;
        }
        js_checkpoint(J);
        J->top = inner;
        /* ergebnis = await iterator.next() */
        js_push(J, J->stack[base + 1]);
        js_push(J, J->stack[base]);
        open = false;                   /* scheitert die Quelle selbst, wird sie nicht mehr geschlossen */
        if (!js_call(J, 0) || (!plain && !js_co_await(J))) {
            c = C_THROW;
            break;
        }
        if (js_top(J, 0)->t != JS_T_OBJ) {
            js_throw(J, JS_E_TYPE, "Iterator result is not an object");
            c = C_THROW;
            break;
        }
        if (!js_get(J, J->top - 1, J->k[K_done])) {
            c = C_THROW;
            break;
        }
        bool done = js_truthy(js_top(J, 0));
        js_pop(J, 1);
        if (done)
            break;
        if (!js_get(J, J->top - 1, J->k[K_value])) {
            c = C_THROW;
            break;
        }
        open = true;
        if (plain && !js_co_await(J)) {
            c = C_THROW;
            break;
        }
        js_value item = *js_top(J, 0);
        J->top = inner;
        js_object *scope = lexical ? scope_new(J, env) : env;
        js_push(J, item);
        if (!bind_loop_value(J, n->a, scope)) {
            c = C_THROW;
            break;
        }
        js_pop(J, 1);
        c = exec(J, n->d, scope, NULL);
        if (loop_control(J, &c, label))
            break;
    }
    if (open && !J->fatal) {
        /* vorzeitig verlassen: return() der Quelle rufen und darauf warten */
        bool failing = c == C_THROW;
        bool throwing = J->throwing;
        js_string *target = J->label;
        J->top = inner;
        js_push(J, J->ret);
        js_push(J, J->exception);
        J->throwing = false;
        J->exception = js_undef();
        bool ok = js_get(J, base, J->k[K_return]);
        if (ok && js_is_callable(js_top(J, 0))) {
            js_push(J, J->stack[base]);
            ok = js_call(J, 0) && (plain || js_co_await(J));
        }
        if (failing && !J->fatal) {
            J->exception = J->stack[inner + 1];
            J->throwing = throwing;
        } else if (!ok) {
            c = C_THROW;
        }
        if (c != C_THROW) {
            J->ret = J->stack[inner];
            J->label = target;
        }
    }
    if (c != C_THROW)
        J->top = base;
    return c;
}

/* for (x of quelle): Glied für Glied über das Iterationsprotokoll */
static int exec_for_of(js_state *J, js_node *n, js_object *env, js_string *label)
{
    int base = J->top, c = C_NORMAL;
    bool lexical = n->a->type == S_VAR && (n->a->flags & (NF_LET | NF_CONST));
    js_iter it;
    if (!eval(J, n->b, env) || !js_iter_open(J, J->top - 1, &it))
        return C_THROW;
    int inner = J->top;
    for (;;) {
        bool done;
        if (!tick(J)) {
            c = C_THROW;
            break;
        }
        js_checkpoint(J);
        J->top = inner;
        if (!js_iter_next(J, &it, &done)) {
            c = C_THROW;
            break;
        }
        if (done)
            break;
        js_value item = *js_top(J, 0);
        js_pop(J, 1);
        js_object *scope = lexical ? scope_new(J, env) : env;
        js_push(J, item);
        if (!bind_loop_value(J, n->a, scope)) {
            c = C_THROW;
            break;
        }
        js_pop(J, 1);
        c = exec(J, n->d, scope, NULL);
        if (loop_control(J, &c, label))
            break;
    }
    if (!it.done && !J->fatal) {
        /*
         * Vorzeitig verlassen (break, return, Ausnahme): der Quelle Bescheid
         * geben. Was gerade unterwegs ist - Rückgabewert, Sprungziel -,
         * übersteht den Aufruf von return().
         */
        js_string *target = J->label;
        J->top = inner;
        js_push(J, J->ret);
        if (js_iter_close(J, &it, c == C_THROW)) {
            J->ret = J->stack[inner];
            J->label = target;
        } else {
            c = C_THROW;
        }
    }
    if (c != C_THROW)
        J->top = base;
    return c;
}

/* for (name in objekt) */
static int exec_for_each(js_state *J, js_node *n, js_object *env, js_string *label)
{
    int base = J->top, c = C_NORMAL;
    js_object *items = js_array_new(J, 0);
    bool lexical = n->a->type == S_VAR && (n->a->flags & (NF_LET | NF_CONST));
    js_push(J, js_obj(items));
    if (!eval(J, n->b, env))
        return C_THROW;
    if (js_top(J, 0)->t == JS_T_OBJ) {
        /* alle aufzählbaren Namen, auch die geerbten; jeder nur einmal */
        js_object *seen = js_object_new(J, JS_C_OBJECT, NULL);
        js_push(J, js_obj(seen));
        for (js_object *o = js_top(J, 1)->u.o; o; o = o->proto) {
            js_object *keys = js_array_new(J, 0);
            js_push(J, js_obj(keys));
            if (!js_own_keys(J, o, false, keys))
                return C_THROW;
            for (uint32_t i = 0; i < keys->u.arr.len; i++) {
                js_string *key = keys->u.arr.v[i].u.s;
                if (js_own(seen, key))
                    continue;
                js_define(J, seen, key, js_undef(), 0);
                js_prop *p = js_own(o, key);
                if ((!p || (p->flags & JS_P_ENUM)) && !js_array_push(J, items, js_str(key)))
                    return C_THROW;
            }
            js_pop(J, 1);
        }
        js_pop(J, 1);
    } else if (js_top(J, 0)->t == JS_T_STR) {
        for (uint32_t i = 0; i < js_top(J, 0)->u.s->chars; i++) {
            if (!js_array_push(J, items, js_str(js_index_name(J, i))))
                return C_THROW;
        }
    }
    for (uint32_t i = 0; items->cls == JS_C_ARRAY && i < items->u.arr.len; i++) {
        if (!tick(J))
            return C_THROW;
        js_checkpoint(J);
        J->top = base + 2;
        if (n->type == S_FORIN && js_top(J, 0)->t == JS_T_OBJ) {
            /* inzwischen Gelöschtes überspringen */
            js_object *o = js_top(J, 0)->u.o;
            js_string *key = items->u.arr.v[i].u.s;
            if (!js_lookup(o, key, NULL) &&
                !(o->cls == JS_C_ARRAY && key->index != JS_NO_INDEX && key->index < o->u.arr.len) &&
                o->cls != JS_C_STRING && o->cls != JS_C_HOST && o->cls != JS_C_PROXY)
                continue;
        }
        js_object *scope = lexical ? scope_new(J, env) : env;
        js_push(J, items->u.arr.v[i]);
        if (!bind_loop_value(J, n->a, scope))
            return C_THROW;
        js_pop(J, 1);
        c = exec(J, n->d, scope, NULL);
        if (loop_control(J, &c, label))
            break;
    }
    if (c != C_THROW)
        J->top = base;
    return c;
}

static int exec_for(js_state *J, js_node *n, js_object *env, js_string *label)
{
    int base = J->top, c = C_NORMAL;
    js_object *scope = (n->flags & NF_SCOPE) ? scope_new(J, env) : env;
    if (n->a && exec(J, n->a, scope, NULL) == C_THROW)
        return C_THROW;
    int inner = J->top;
    for (;;) {
        if (!tick(J))
            return C_THROW;
        js_checkpoint(J);
        J->top = inner;
        if (n->b) {
            if (!eval(J, n->b, scope))
                return C_THROW;
            bool go = js_truthy(js_top(J, 0));
            js_pop(J, 1);
            if (!go)
                break;
        }
        c = exec(J, n->d, scope, NULL);
        if (loop_control(J, &c, label))
            break;
        if (n->flags & NF_SCOPE) {
            /* jede Runde bekommt eigene Variablen: Funktionen merken sich "ihre" */
            js_object *fresh = js_object_new(J, JS_C_SCOPE, env);
            for (uint32_t i = 0; i < scope->count; i++) {
                if (scope->props[i].key)
                    js_define(J, fresh, scope->props[i].key, scope->props[i].v,
                              scope->props[i].flags);
            }
            scope = fresh;
            J->stack[base] = js_obj(fresh);
        }
        if (n->c) {
            if (!eval(J, n->c, scope))
                return C_THROW;
            js_pop(J, 1);
        }
    }
    if (c != C_THROW)
        J->top = base;
    return c;
}

static int exec_try(js_state *J, js_node *n, js_object *env)
{
    int base = J->top;
    int c = exec(J, n->a, env, NULL);
    /* (die erzwungene Rückkehr aus einem Generator fängt niemand: nur der Schlussteil läuft) */
    if (c == C_THROW && n->c && !J->fatal &&
        !(J->exception.t == JS_T_OBJ && J->exception.u.o->cls == JS_C_SIGNAL)) {
        js_value thrown = J->exception;
        J->throwing = false;
        J->exception = js_undef();
        J->short_circuit = false;
        J->top = base;
        js_object *scope = scope_new(J, env);
        js_push(J, thrown);
        if (n->b && !assign_to(J, n->b, scope, A_LET)) {
            c = C_THROW;
        } else {
            js_pop(J, 1);
            c = exec(J, n->c, scope, NULL);
        }
        if (c != C_THROW)
            J->top = base;
    }
    if (n->d && !J->fatal) {
        /* Was auch geschah: Der Schlussteil läuft, danach gilt es weiter. */
        bool throwing = J->throwing;
        js_string *label = J->label;
        J->top = base;
        js_push(J, J->exception);
        js_push(J, J->ret);
        J->throwing = false;
        J->exception = js_undef();
        J->label = NULL;
        int f = exec(J, n->d, env, NULL);
        if (f != C_NORMAL) {
            c = f;
        } else {
            J->exception = J->stack[base];
            J->ret = J->stack[base + 1];
            J->throwing = throwing;
            J->label = label;
            J->top = base;
        }
    }
    return c;
}

static int exec_switch(js_state *J, js_node *n, js_object *env)
{
    int base = J->top, c = C_NORMAL;
    if (!eval(J, n->a, env))
        return C_THROW;
    js_object *scope = (n->flags & NF_SCOPE) ? scope_new(J, env) : env;
    js_node *start = NULL, *fallback = NULL;
    for (js_node *k = n->b; k && !start; k = k->next) {
        if (!k->a) {
            fallback = k;
            continue;
        }
        if (!eval(J, k->a, scope))
            return C_THROW;
        if (js_strict_equal(&J->stack[base], js_top(J, 0)))
            start = k;
        js_pop(J, 1);
    }
    for (js_node *k = start ? start : fallback; k && c == C_NORMAL; k = k->next)
        c = exec_list(J, k->b, scope);
    if (c == C_BREAK && !J->label)
        c = C_NORMAL;
    if (c != C_THROW)
        J->top = base;
    return c;
}

static int exec(js_state *J, js_node *n, js_object *env, js_string *label)
{
    if (!enter(J))
        return C_THROW;
    int c = C_NORMAL, base = J->top;
    J->line = n->line;
    if (!tick(J))
        LEAVE(C_THROW);
    switch (n->type) {
    case S_EXPR:
        if (!eval(J, n->a, env)) {
            c = C_THROW;
        } else {
            J->ret = *js_top(J, 0);
            js_pop(J, 1);
        }
        break;
    case S_VAR: {
        int mode = (n->flags & NF_CONST) ? A_CONST : (n->flags & NF_LET) ? A_LET : A_VAR;
        for (js_node *d = n->a; d && c == C_NORMAL; d = d->next) {
            if (d->b) {
                if (!eval(J, d->b, env)) {
                    c = C_THROW;
                    break;
                }
                js_value *v = js_top(J, 0);
                if (d->a->type == N_IDENT && (d->b->type == N_FUNC || d->b->type == N_CLASS) &&
                    v->t == JS_T_OBJ && v->u.o->cls == JS_C_FUNCTION &&
                    !v->u.o->u.f.fn->u.fn->name && !js_own(v->u.o, J->k[K_name]))
                    js_define(J, v->u.o, J->k[K_name], js_str(d->a->u.str), JS_P_CONF);
            } else if (mode == A_VAR) {
                continue;               /* schon beim Betreten der Funktion angelegt */
            } else {
                js_push(J, js_undef());
            }
            if (!assign_to(J, d->a, env, mode))
                c = C_THROW;
            else
                js_pop(J, 1);
        }
        break;
    }
    case S_BLOCK:
        if (n->flags & NF_SCOPE) {
            js_object *scope = scope_new(J, env);
            c = exec_list(J, n->a, scope);
            if (c != C_THROW)
                J->top = base;
        } else {
            c = exec_list(J, n->a, env);
        }
        break;
    case S_IF:
        if (!eval(J, n->a, env)) {
            c = C_THROW;
        } else {
            bool yes = js_truthy(js_top(J, 0));
            js_pop(J, 1);
            if (yes)
                c = exec(J, n->b, env, NULL);
            else if (n->c)
                c = exec(J, n->c, env, NULL);
        }
        break;
    case S_FOR:
        c = exec_for(J, n, env, label);
        break;
    case S_FORIN:
        c = exec_for_each(J, n, env, label);
        break;
    case S_FOROF:
        c = (n->flags & NF_AWAIT) ? exec_for_await(J, n, env, label) : exec_for_of(J, n, env, label);
        break;
    case S_WHILE:
        for (;;) {
            if (!tick(J) || !eval(J, n->a, env)) {
                c = C_THROW;
                break;
            }
            js_checkpoint(J);
            bool go = js_truthy(js_top(J, 0));
            js_pop(J, 1);
            if (!go)
                break;
            c = exec(J, n->d, env, NULL);
            if (loop_control(J, &c, label))
                break;
        }
        break;
    case S_DO:
        for (;;) {
            if (!tick(J)) {
                c = C_THROW;
                break;
            }
            js_checkpoint(J);
            c = exec(J, n->d, env, NULL);
            if (loop_control(J, &c, label))
                break;
            if (!eval(J, n->a, env)) {
                c = C_THROW;
                break;
            }
            bool go = js_truthy(js_top(J, 0));
            js_pop(J, 1);
            if (!go)
                break;
        }
        break;
    case S_RETURN:
        if (n->a) {
            if (!eval(J, n->a, env)) {
                c = C_THROW;
                break;
            }
            J->ret = *js_top(J, 0);
            js_pop(J, 1);
        } else {
            J->ret = js_undef();
        }
        c = C_RETURN;
        break;
    case S_BREAK:
        J->label = n->u.str;
        c = C_BREAK;
        break;
    case S_CONTINUE:
        J->label = n->u.str;
        c = C_CONTINUE;
        break;
    case S_THROW:
        if (eval(J, n->a, env)) {
            J->line = n->line;
            js_throw_value(J, *js_top(J, 0));
        }
        c = C_THROW;
        break;
    case S_TRY:
        c = exec_try(J, n, env);
        break;
    case S_SWITCH:
        c = exec_switch(J, n, env);
        break;
    case S_LABEL:
        c = exec(J, n->a, env, n->u.str);
        if (c == C_BREAK && J->label == n->u.str) {
            J->label = NULL;
            c = C_NORMAL;
        }
        break;
    case S_CLASS:
        if (!eval_class(J, n->a, env)) {
            c = C_THROW;
        } else {
            js_define(J, env, n->a->u.str, *js_top(J, 0), JS_P_ALL);
            js_pop(J, 1);
        }
        break;
    case S_FUNC: case S_EMPTY:
        break;
    default:
        js_throw(J, JS_E_SYNTAX, "Unexpected statement");
        c = C_THROW;
    }
    if (c != C_THROW && (J->fatal || J->throwing))
        c = C_THROW;
    LEAVE(c);
}

static int exec_list(js_state *J, js_node *list, js_object *env)
{
    int c = C_NORMAL;
    for (js_node *s = list; s && c == C_NORMAL; s = s->next)
        c = exec(J, s, env, NULL);
    return c;
}

/* --- Programme ausführen ----------------------------------------------------------------------------- */

bool js_exec_program(js_state *J, js_fninfo *program)
{
    int base = J->top, fbase = J->fbase;
    js_push(J, js_undef());
    js_push(J, js_obj(J->global));
    J->fbase = base;
    hoist(J, program, J->global);
    int c = exec_list(J, program->body, J->global);
    J->fbase = fbase;
    J->top = base;
    J->label = NULL;
    J->short_circuit = false;
    if (c == C_THROW || J->fatal || J->throwing)
        return false;
    return js_push(J, J->ret);
}

/* Beschreibt die anstehende Ausnahme in J->error und räumt sie ab. */
static void report(js_state *J)
{
    char *out = J->error;
    size_t size = sizeof(J->error), n = 0;
    out[0] = '\0';
    if (J->fatal) {
        strlcpy(out, J->fatal_reason ? J->fatal_reason : "Skript abgebrochen", size);
        return;
    }
    if (!J->throwing)
        return;
    js_value e = J->exception;
    const char *name = NULL, *text = NULL;
    int line = 0;
    if (e.t == JS_T_OBJ) {
        js_prop *p = js_lookup(e.u.o, J->k[K_name], NULL);
        if (p && p->v.t == JS_T_STR)
            name = p->v.u.s->data;
        p = js_lookup(e.u.o, J->k[K_message], NULL);
        if (p && p->v.t == JS_T_STR)
            text = p->v.u.s->data;
        p = js_own(e.u.o, J->k[K_line]);
        if (p && p->v.t == JS_T_NUM)
            line = (int)p->v.u.n;
    } else if (e.t == JS_T_STR || e.t == JS_T_SYM) {
        text = e.u.s->data;
    } else if (e.t == JS_T_NUM) {
        static char number[48];
        js_number_to_text(e.u.n, number);
        text = number;
    } else {
        text = e.t == JS_T_NULL ? "null" : e.t == JS_T_UNDEF ? "undefined" : e.u.b ? "true" : "false";
    }
    for (const char *s = name ? name : "Uncaught"; *s && n + 1 < size; s++)
        out[n++] = *s;
    for (const char *s = ": "; *s && n + 1 < size; s++)
        out[n++] = *s;
    for (const char *s = text ? text : "(ohne Text)"; *s && n + 40 < size; s++)
        out[n++] = *s;
    if (line > 0) {
        char digits[12];
        int k = 0;
        for (const char *s = " (Zeile "; *s && n + 1 < size; s++)
            out[n++] = *s;
        do {
            digits[k++] = (char)('0' + line % 10);
            line /= 10;
        } while (line);
        while (k > 0 && n + 2 < size)
            out[n++] = digits[--k];
        out[n++] = ')';
    }
    out[n] = '\0';
    J->throwing = false;
    J->exception = js_undef();
}

void js_enter(js_state *J, uint32_t budget_ms)
{
    J->error[0] = '\0';
    if (js_co_active(J))
        return;                         /* Einstieg aus einer laufenden Koroutine: ihre Grenzen gelten weiter */
    size_t room = J->host.stack_bytes ? J->host.stack_bytes : 256 * 1024;
    uintptr_t here = JS_STACK_HERE();
    J->stack_floor = here > room ? here - room : 0;
    J->deadline = J->host.ticks_ms && budget_ms ? J->host.ticks_ms(J->host.ctx) + budget_ms : 0;
    J->depth = 0;
}

bool js_leave(js_state *J, bool ok)
{
    if (!ok || J->throwing || J->fatal) {
        report(J);
        if (J->host.print && J->error[0])
            J->host.print(J->host.ctx, J->error);
        J->short_circuit = false;
        J->label = NULL;
        return false;
    }
    return true;
}

bool js_run(js_state *J, const char *source, size_t length, const char *name, uint32_t budget_ms)
{
    if (J->fatal) {
        report(J);
        return false;
    }
    int base = J->top;
    js_enter(J, budget_ms);
    js_fninfo *program = js_parse(J, source, length, name);
    bool ok = program && js_exec_program(J, program);
    J->top = base;
    J->ret = js_undef();
    if (ok)
        js_jobs_run(J);
    J->top = base;
    return js_leave(J, ok);
}
