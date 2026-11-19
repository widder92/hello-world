/*
 * JavaScript: Module (import/export) - siehe js.h
 *
 * Ein Modul ist ein Skript mit eigenem Bereich für seine Namen: Was es mit
 * "export" freigibt, können andere mit "import" benutzen. Die Verbindung ist
 * lebendig - wer importiert, sieht stets den jetzigen Wert der Variable beim
 * anderen, keinen Abzug. Hier steht deshalb unter jedem importierten Namen
 * eine kleine Lesefunktion, die beim Besitzer nachsieht.
 *
 * Woher der Quelltext eines Moduls kommt, weiß die Maschine nicht: Das
 * erledigt ein Rückruf des Gastgebers (die Seite: dom_js.c), der aus der
 * Angabe hinter "from" und der Adresse des fragenden Moduls das Modul macht.
 *
 * Ablauf: erst alle beteiligten Module laden und ihre Namen verbinden, dann
 * jedes einmal ausführen - die benutzten vor denen, die sie benutzen. Der
 * Rumpf läuft als async-Funktion, darum darf "await" auch ganz außen stehen.
 */
#include "js_impl.h"
#include "string.h"

enum { ST_NEW, ST_LINKING, ST_LINKED, ST_RUNNING, ST_DONE, ST_FAILED };

typedef struct module {
    js_fninfo *info;
    js_string *url;
    js_object *self;                    /* das Objekt, an dem dieser Eintrag hängt */
    js_object *env;                     /* der Bereich für die Namen des Moduls */
    js_object *fn;                      /* sein Rumpf als Funktion */
    js_object *ns;                      /* alle Exporte als Objekt (import * as ...), bei Bedarf */
    js_object *meta;                    /* import.meta, bei Bedarf */
    js_object **deps;                   /* die Module hinter den Zeilen mit "from", in deren Reihenfolge */
    int ndeps;
    js_value promise;                   /* wird fertig, wenn der Rumpf durchgelaufen ist */
    js_value error;
    uint8_t state;
} module;

static void module_mark(js_state *J, js_object *o)
{
    module *m = o->u.host.ptr;
    if (!m)
        return;
    js_object *held[] = { m->env, m->fn, m->ns, m->meta };
    for (unsigned i = 0; i < sizeof(held) / sizeof(held[0]); i++) {
        if (held[i]) {
            js_value v = js_obj(held[i]);
            js_gc_mark(J, &v);
        }
    }
    for (int i = 0; i < m->ndeps; i++) {
        if (m->deps[i]) {
            js_value v = js_obj(m->deps[i]);
            js_gc_mark(J, &v);
        }
    }
    js_value url = js_str(m->url);
    js_gc_mark(J, &url);
    js_gc_mark(J, &m->promise);
    js_gc_mark(J, &m->error);
}

static void module_finalize(js_state *J, js_object *o)
{
    module *m = o->u.host.ptr;
    if (!m)
        return;
    js_release(J, m->deps, (size_t)m->ndeps * sizeof(js_object *));
    js_release(J, m, sizeof(module));
    o->u.host.ptr = NULL;
}

static const js_host_class module_class = { "Module Record", NULL, NULL, module_finalize, module_mark };

static module *module_of(const js_object *o)
{
    return o && o->cls == JS_C_HOST && o->u.host.cls == &module_class ? o->u.host.ptr : NULL;
}

void js_module_set_loader(js_state *J, js_module_loader loader, void *ctx)
{
    J->module_loader = loader;
    J->module_ctx = ctx;
}

js_string *js_module_url(const js_object *record)
{
    module *m = module_of(record);
    return m ? m->url : NULL;
}

js_object *js_module_create(js_state *J, const char *source, size_t len, js_string *url)
{
    js_fninfo *info = js_parse_module(J, source, len, url->data);
    if (!info)
        return NULL;
    module *m = js_alloc(J, sizeof(module));
    js_node *node = js_arena_alloc(J, sizeof(js_node));
    if (!m || !node) {
        js_release(J, m, sizeof(module));
        return NULL;
    }
    memset(m, 0, sizeof(*m));
    js_object *record = js_object_new(J, JS_C_HOST, NULL);
    if (record == J->dummy) {
        js_release(J, m, sizeof(module));
        return NULL;
    }
    record->u.host.cls = &module_class;
    record->u.host.ptr = m;
    js_push(J, js_obj(record));
    node->type = N_FUNC;
    node->op = OP_NONE;
    node->u.fn = info;
    m->info = info;
    m->url = url;
    m->self = record;
    m->promise = js_undef();
    m->error = js_undef();
    m->env = js_object_new(J, JS_C_SCOPE, J->global);
    m->fn = js_function_new(J, node, m->env);
    js_define(J, m->env, J->hid[H_module], js_obj(record), 0);
    js_pop(J, 1);
    return J->fatal ? NULL : record;
}

/* Das Modul hinter der Zeile item (sie hat ein "from") */
static module *dep_of(const module *m, const js_module_item *item)
{
    int k = 0;
    for (const js_module_item *i = m->info->module_items; i; i = i->next) {
        if (!i->specifier)
            continue;
        if (i == item)
            return k < m->ndeps ? module_of(m->deps[k]) : NULL;
        k++;
    }
    return NULL;
}

/*
 * Wem gehört der Export name des Moduls m? Liefert 1 und Besitzer samt dem
 * Namen seiner Variable - oder 2, wenn gemeint sind alle Exporte von *owner
 * zusammen (export * as name from ...) - oder 0: Den Export gibt es nicht.
 */
static int resolve_export(module *m, const js_string *name, int depth, module **owner,
                          js_string **local)
{
    if (!m || depth > 100)
        return 0;
    for (js_module_item *item = m->info->module_items; item; item = item->next) {
        if (item->kind == JS_M_EXPORT && item->local == name) {
            *owner = m;
            *local = item->imported;
            return 1;
        }
        if (item->kind == JS_M_EXPORT_FROM && item->local == name) {
            module *dep = dep_of(m, item);
            if (item->imported->len == 1 && item->imported->data[0] == '*') {
                *owner = dep;
                return dep ? 2 : 0;
            }
            return resolve_export(dep, item->imported, depth + 1, owner, local);
        }
    }
    if (name->len == 7 && memcmp(name->data, "default", 7) == 0)
        return 0;                       /* "export *" reicht alles weiter außer default */
    for (js_module_item *item = m->info->module_items; item; item = item->next) {
        if (item->kind != JS_M_EXPORT_STAR)
            continue;
        int found = resolve_export(dep_of(m, item), name, depth + 1, owner, local);
        if (found)
            return found;
    }
    return 0;
}

static js_object *namespace_of(js_state *J, module *m);

/* Die Lesefunktion hinter einem importierten Namen: sieht beim Besitzer nach */
static bool import_get(js_state *J, int argc)
{
    (void)argc;
    js_object *data = js_callee(J)->u.n.data;
    js_prop *mp = data ? js_own(data, js_intern_cstr(J, "m")) : NULL;
    js_prop *np = data ? js_own(data, js_intern_cstr(J, "n")) : NULL;
    module *dep = mp && mp->v.t == JS_T_OBJ ? module_of(mp->v.u.o) : NULL, *owner = NULL;
    js_string *local = NULL;
    int found = dep && np ? resolve_export(dep, np->v.u.s, 0, &owner, &local) : 0;
    if (found == 2) {
        js_object *ns = namespace_of(J, owner);
        return ns && js_push(J, js_obj(ns));
    }
    js_prop *p = found == 1 ? js_own(owner->env, local) : NULL;
    if (!p)
        return js_push(J, js_undef());  /* (noch) nicht vereinbart */
    if (!(p->flags & JS_P_ACCESSOR))
        return js_push(J, p->v);
    if (p->v.t != JS_T_OBJ)
        return js_push(J, js_undef());
    js_push(J, p->v);                   /* selbst ein importierter Name: weiterfragen */
    js_push(J, js_undef());
    return js_call(J, 0);
}

static js_object *binding_getter(js_state *J, module *dep, js_string *name)
{
    js_object *getter = js_native_new(J, import_get, "", 0, 0);
    js_push(J, js_obj(getter));
    js_object *data = js_object_new(J, JS_C_OBJECT, NULL);
    getter->u.n.data = data;
    js_define(J, data, js_intern_cstr(J, "m"), js_obj(dep->self), JS_P_ALL);
    js_define(J, data, js_intern_cstr(J, "n"), js_str(name), JS_P_ALL);
    js_pop(J, 1);
    return getter;
}

/* Sammelt die Namen aller Exporte von m in names (ohne Doppelte) */
static bool export_names(js_state *J, module *m, js_object *names, int depth, bool with_default)
{
    if (!m || depth > 100)
        return true;
    for (js_module_item *item = m->info->module_items; item; item = item->next) {
        if (item->kind == JS_M_EXPORT_STAR) {
            if (!export_names(J, dep_of(m, item), names, depth + 1, false))
                return false;
            continue;
        }
        if (item->kind != JS_M_EXPORT && item->kind != JS_M_EXPORT_FROM)
            continue;
        if (!with_default && item->local->len == 7 && memcmp(item->local->data, "default", 7) == 0)
            continue;
        bool known = false;
        for (uint32_t i = 0; i < names->u.arr.len && !known; i++)
            known = names->u.arr.v[i].u.s == item->local;
        if (!known && !js_array_push(J, names, js_str(item->local)))
            return false;
    }
    return true;
}

/* import * as ns: ein Objekt mit einer Lesefunktion je Export, nach Namen geordnet */
static js_object *namespace_of(js_state *J, module *m)
{
    if (m->ns)
        return m->ns;
    js_object *names = js_array_new(J, 0);
    js_push(J, js_obj(names));
    js_object *ns = js_object_new(J, JS_C_OBJECT, NULL);
    js_push(J, js_obj(ns));
    if (!export_names(J, m, names, 0, true))
        return NULL;
    for (uint32_t i = 1; i < names->u.arr.len; i++) {
        js_value item = names->u.arr.v[i];
        uint32_t k = i;
        while (k > 0 && js_string_compare(names->u.arr.v[k - 1].u.s, item.u.s) > 0) {
            names->u.arr.v[k] = names->u.arr.v[k - 1];
            k--;
        }
        names->u.arr.v[k] = item;
    }
    for (uint32_t i = 0; i < names->u.arr.len; i++) {
        js_string *name = names->u.arr.v[i].u.s;
        js_object *getter = binding_getter(J, m, name);
        js_prop *p = js_define(J, ns, name, js_obj(getter), JS_P_ACCESSOR | JS_P_ENUM);
        if (p)
            p->set = NULL;
    }
    js_define(J, ns, J->sym[SYM_toStringTag], js_str(js_intern_cstr(J, "Module")), 0);
    ns->flags |= JS_F_NOEXT;
    m->ns = ns;
    js_pop(J, 2);
    return J->fatal ? NULL : ns;
}

/* Lädt alles, worauf m sich bezieht, und verbindet die Namen. */
static bool link(js_state *J, module *m, int depth)
{
    if (m->state != ST_NEW)
        return m->state != ST_FAILED || js_throw_value(J, m->error);
    if (depth > 200 || JS_STACK_HERE() < J->stack_floor)
        return js_throw(J, JS_E_RANGE, "Modules are nested too deeply");
    if (!J->module_loader)
        return js_throw(J, JS_E_TYPE, "Modules cannot be loaded here");
    m->state = ST_LINKING;
    int count = 0;
    for (js_module_item *item = m->info->module_items; item; item = item->next)
        count += item->specifier != NULL;
    if (count) {
        m->deps = js_alloc(J, (size_t)count * sizeof(js_object *));
        if (!m->deps)
            return false;
        memset(m->deps, 0, (size_t)count * sizeof(js_object *));
        m->ndeps = count;
    }
    int k = 0;
    for (js_module_item *item = m->info->module_items; item; item = item->next) {
        if (!item->specifier)
            continue;
        js_object *dep = J->module_loader(J, J->module_ctx, m->url, item->specifier);
        if (!dep || !module_of(dep) || !link(J, module_of(dep), depth + 1)) {
            if (!J->throwing && !J->fatal)
                js_throw(J, JS_E_TYPE, "Failed to load module '%s'", item->specifier->data);
            m->state = ST_FAILED;
            m->error = J->exception;
            return false;
        }
        m->deps[k++] = dep;
    }
    /* Funktionen des Moduls gelten von Anfang an - auch für Module, die im Kreis aufeinander zeigen */
    js_hoist(J, m->info, m->env);
    for (js_module_item *item = m->info->module_items; item; item = item->next) {
        if (item->kind != JS_M_IMPORT || !item->local)
            continue;
        module *dep = dep_of(m, item), *owner = NULL;
        js_string *local = NULL;
        if (item->imported->len == 1 && item->imported->data[0] == '*') {
            js_object *ns = namespace_of(J, dep);
            if (!ns)
                return false;
            js_define(J, m->env, item->local, js_obj(ns), JS_P_ENUM | JS_P_CONST);
            continue;
        }
        if (dep->state != ST_LINKING && !resolve_export(dep, item->imported, 0, &owner, &local)) {
            js_throw(J, JS_E_SYNTAX, "The requested module '%s' does not provide an export named '%s'",
                     item->specifier->data, item->imported->data);
            m->state = ST_FAILED;
            m->error = J->exception;
            return false;
        }
        js_object *getter = binding_getter(J, dep, item->imported);
        js_prop *p = js_define(J, m->env, item->local, js_obj(getter), JS_P_ACCESSOR | JS_P_ENUM);
        if (p)
            p->set = NULL;
    }
    if (J->fatal)
        return false;
    m->state = ST_LINKED;
    return true;
}

/* 0: läuft noch, 1: fertig, 2: gescheitert (dann liegt der Grund in *value) */
static int promise_state(js_state *J, const js_value *promise, js_value *value)
{
    if (promise->t != JS_T_OBJ)
        return 1;
    js_prop *state = js_own(promise->u.o, js_intern_cstr(J, "_s"));
    js_prop *v = js_own(promise->u.o, js_intern_cstr(J, "_v"));
    if (!state || state->v.t != JS_T_NUM)
        return 1;
    if (v)
        *value = v->v;
    return (int)state->v.u.n;
}

/* Führt m aus - vorher alles, was es benutzt. */
static bool evaluate(js_state *J, module *m, int depth)
{
    if (m->state == ST_DONE || m->state == ST_RUNNING)
        return true;
    if (m->state == ST_FAILED)
        return js_throw_value(J, m->error);
    if (depth > 200 || JS_STACK_HERE() < J->stack_floor)
        return js_throw(J, JS_E_RANGE, "Modules are nested too deeply");
    m->state = ST_RUNNING;
    for (int i = 0; i < m->ndeps; i++) {
        if (!evaluate(J, module_of(m->deps[i]), depth + 1)) {
            m->state = ST_FAILED;
            m->error = J->exception;
            return false;
        }
    }
    int base = J->top, fbase = J->fbase;
    js_value result = js_undef();
    js_push(J, js_obj(m->fn));
    js_push(J, js_undef());
    J->fbase = base;
    bool ok = js_co_call(J, base, m->env, &result);
    J->fbase = fbase;
    if (ok) {
        m->promise = result;
        J->top = base;
        js_value reason = js_undef();
        int state = promise_state(J, &m->promise, &reason);
        if (state == 0) {
            /* der Rumpf wartet auf etwas (await ganz außen): so weit bringen, wie es sofort geht */
            js_jobs_run(J);
            state = promise_state(J, &m->promise, &reason);
        }
        if (state == 2)
            ok = js_throw_value(J, reason);
    }
    if (!ok) {
        m->state = ST_FAILED;
        m->error = J->exception;
        return false;
    }
    m->state = ST_DONE;
    return true;
}

bool js_module_run(js_state *J, js_object *record, uint32_t budget_ms)
{
    module *m = module_of(record);
    if (!m || J->fatal)
        return false;
    int base = J->top;
    js_enter(J, budget_ms);
    js_push(J, js_obj(record));
    bool ok = link(J, m, 0) && evaluate(J, m, 0);
    J->top = base;
    J->ret = js_undef();
    if (ok)
        js_jobs_run(J);
    J->top = base;
    return js_leave(J, ok);
}

/* Das Modul, in dessen Quelltext der Bereich env liegt - oder NULL (ein gewöhnliches Skript) */
static module *module_at(js_state *J, js_object *env)
{
    js_prop *p = env ? js_lookup(env, J->hid[H_module], NULL) : NULL;
    return p && p->v.t == JS_T_OBJ ? module_of(p->v.u.o) : NULL;
}

static bool call_helper(js_state *J, const char *name, js_value a, js_value b)
{
    js_prop *helper = js_own(J->registry, js_intern_cstr(J, name));
    if (!helper || !js_is_callable(&helper->v))
        return js_throw(J, JS_E_TYPE, "Modules are not available");
    js_push(J, helper->v);
    js_push(J, js_undef());
    js_push(J, a);
    js_push(J, b);
    return js_call(J, 2);
}

/* import("..."): [angabe] -> [promise auf die Exporte des Moduls] */
bool js_module_import(js_state *J, js_object *env)
{
    int base = J->top - 1;
    module *from = module_at(J, env);
    bool ok = js_to_string(J, base);
    js_object *record = NULL;
    if (ok && !J->module_loader)
        ok = js_throw(J, JS_E_TYPE, "Modules cannot be loaded here");
    if (ok) {
        js_string *specifier = js_intern_string(J, J->stack[base].u.s);
        record = J->module_loader(J, J->module_ctx, from ? from->url : NULL, specifier);
        ok = record && module_of(record);
        if (!ok && !J->throwing && !J->fatal)
            js_throw(J, JS_E_TYPE, "Failed to load module '%s'", specifier->data);
    }
    if (ok) {
        js_push(J, js_obj(record));
        module *m = module_of(record);
        ok = link(J, m, 0) && evaluate(J, m, 0);
        js_object *ns = ok ? namespace_of(J, m) : NULL;
        ok = ok && ns && call_helper(J, "importResult", m->promise, js_obj(ns));
    }
    if (!ok) {
        /* ein Fehlschlag ist hier keine Ausnahme, sondern ein gescheitertes Promise */
        if (J->fatal || !J->throwing)
            return false;
        js_value reason = J->exception;
        J->throwing = false;
        J->exception = js_undef();
        js_push(J, reason);
        if (!call_helper(J, "importFailed", reason, js_undef()))
            return false;
    }
    J->stack[base] = *js_top(J, 0);
    J->top = base + 1;
    return true;
}

/* import.meta: legt das Auskunftsobjekt des Moduls auf den Stapel */
bool js_module_meta(js_state *J, js_object *env)
{
    module *m = module_at(J, env);
    if (!m)
        return js_throw(J, JS_E_SYNTAX, "Cannot use 'import.meta' outside a module");
    if (!m->meta) {
        m->meta = js_object_new(J, JS_C_OBJECT, NULL);
        js_define(J, m->meta, js_intern_cstr(J, "url"), js_str(m->url), JS_P_ALL);
    }
    return js_push(J, js_obj(m->meta));
}
