/* Skripte einer Webseite: Seitenabbild und JavaScript-Maschine verbinden - siehe script.h */
#include "script.h"
#include "dom.h"
#include "html.h"
#include "js_impl.h"
#include "memory.h"
#include "string.h"
#include "time.h"
#include "url.h"

#include "domlib.inc"                   /* domlib.js als Zeichenfeld domlib_js (vom Makefile erzeugt) */

void call_on_stack(void (*fn)(void *arg), void *arg, void *stack_top);     /* task.h */

#define STACK_BYTES   (1024 * 1024)
#define HEAP_BYTES    (48u * 1024 * 1024)
#define BOOT_SCRIPTS  120               /* so viele Skripte je Seite höchstens */
#define BOOT_MS       30000             /* Rechenzeit für alle Skripte beim Laden zusammen */
#define SCRIPT_MS     10000             /* ... und für ein einzelnes */
#define FETCH_MS      60000             /* so lange wird beim Laden insgesamt auf Skriptdateien gewartet */
#define LATE_FETCH_MS 20000             /* ... und später je Ereignis, wenn eine Seite Skripte nachfordert */
#define LATE_SCRIPTS  2000              /* so viele Skripte führt eine Seite insgesamt höchstens aus */
#define EVENT_MS      2000
#define SCRIPT_BYTES  (4u * 1024 * 1024)
#define REQUEST_BYTES (1024 * 1024)     /* so viel sendet ein Skript höchstens */
#define WISHES        4
#define DEFERRED_MAX  64

#define ARGI(i) (J->bot + 2 + (i))
#define MAGIC (js_callee(J)->u.n.magic)

struct script {
    js_state *J;
    dom_doc *doc;
    const css_sheet_t *css;             /* Stylesheets der Seite (gehören dem Browser), oder NULL */
    script_host_t host;
    char url[URL_SIZE];
    void *stack;
    js_object *protos[5], *tag_protos, *dispatcher, *style_proto;
    dom_node *current, *write_after;
    bool booting;
    uint8_t *done;                      /* je Knoten: Dieses Skript ist erledigt (oder darf nie laufen) */
    uint32_t done_cap;
    struct {
        script_wish_t kind;
        char url[URL_SIZE];
        uint32_t node, button;
    } wishes[WISHES];
    int wish_count;
    int ran, failed, skipped;
    uint64_t boot_until;
    uint64_t fetch_ms;                  /* bisher mit dem Holen von Skriptdateien verbracht */
    uint64_t fetch_limit;
    /* Skripte, die erst nach allen gewöhnlichen laufen: Module und solche mit "defer" */
    struct {
        uint32_t node;
        bool module;
    } deferred[DEFERRED_MAX];
    int deferred_count;
};

enum { PROTO_ELEMENT, PROTO_TEXT, PROTO_COMMENT, PROTO_FRAGMENT, PROTO_DOCUMENT };

static script_t *script_of(js_state *J)
{
    return J->user;
}

/* --- Knoten als Objekte ------------------------------------------------------------------------ */

static const js_host_class node_class = { "Node", NULL, NULL, NULL, NULL };

static dom_node *node_of(const js_value *v)
{
    if (v->t == JS_T_OBJ && v->u.o->cls == JS_C_HOST && v->u.o->u.host.cls == &node_class)
        return v->u.o->u.host.ptr;
    return NULL;
}

static dom_node *need_node(js_state *J, int i)
{
    dom_node *n = node_of(js_arg(J, i));
    if (!n)
        js_throw(J, JS_E_TYPE, "Argument %d is not a Node", i + 1);
    return n;
}

static js_object *registry_list(js_state *J, const char *name)
{
    js_string *key = js_intern_cstr(J, name);
    js_prop *p = js_own(J->registry, key);
    if (p && js_is_array(&p->v))
        return p->v.u.o;
    js_object *a = js_array_new(J, 0);
    js_define(J, J->registry, key, js_obj(a), JS_P_ALL);
    return a;
}

static bool push_node(js_state *J, dom_node *n)
{
    script_t *s = script_of(J);
    if (!n)
        return js_push(J, js_null());
    if (n->wrapper)
        return js_push(J, js_obj(n->wrapper));
    js_object *proto = s->protos[n->type == DOM_ELEMENT ? PROTO_ELEMENT : n->type == DOM_TEXT ? PROTO_TEXT
                                 : n->type == DOM_COMMENT ? PROTO_COMMENT
                                 : n->type == DOM_DOCUMENT ? PROTO_DOCUMENT : PROTO_FRAGMENT];
    if (n->type == DOM_ELEMENT && s->tag_protos) {
        js_prop *p = js_own(s->tag_protos, js_intern_cstr(J, n->name));
        if (p && p->v.t == JS_T_OBJ)
            proto = p->v.u.o;
    }
    js_object *o = js_object_new(J, JS_C_HOST, proto ? proto : J->object_proto);
    if (o == J->dummy)
        return js_push(J, js_null());
    o->u.host.cls = &node_class;
    o->u.host.ptr = n;
    o->u.host.id = n->id;
    js_push(J, js_obj(o));
    /* Das Objekt lebt, solange die Seite lebt: Skripte dürfen ihm Eigenes anhängen. */
    if (!js_array_push(J, registry_list(J, "nodes"), js_obj(o))) {
        js_pop(J, 1);
        return false;
    }
    n->wrapper = o;
    return true;
}

static bool arg_text(js_state *J, int i, const char **text, size_t *len)
{
    if (!js_to_string(J, ARGI(i)))
        return false;
    *text = js_arg(J, i)->u.s->data;
    *len = js_arg(J, i)->u.s->len;
    return true;
}

static bool push_owned(js_state *J, char *text, size_t len)
{
    if (!text)
        return js_fatal(J, "Nicht genug Speicher für das Skript");
    bool ok = js_push_lstr(J, text, len);
    kfree(text);
    return ok;
}

/* Namen von Elementen und Attributen dürfen nichts enthalten, was den Quelltext zerreißt. */
static bool safe_name(const char *text, size_t len, bool element)
{
    if (len == 0 || len > 200)
        return false;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c <= ' ' || c == '"' || c == '\'' || c == '>' || c == '<' || c == '/' || c == '=' || c == 0x7F)
            return false;
        if (element && !((c | 32) >= 'a' && (c | 32) <= 'z') && !(c >= '0' && c <= '9' && i > 0) &&
            c != '-' && c != '_' && c != ':' && c != '.' && c < 0x80)
            return false;
    }
    return true;
}

/* Merkt Skripte unterhalb von root als erledigt vor: Sie dürfen nicht von selbst laufen. */
static void mark_done(script_t *s, dom_node *root, bool self_too)
{
    for (dom_node *n = self_too ? root : dom_next(root, root); n; n = dom_next(root, n)) {
        if (n->id >= s->done_cap) {
            uint32_t cap = s->done_cap ? s->done_cap * 2 : 1024;
            while (cap <= n->id)
                cap *= 2;
            uint8_t *bigger = kzalloc(cap);
            if (!bigger)
                return;
            if (s->done_cap)
                memcpy(bigger, s->done, s->done_cap);
            kfree(s->done);
            s->done = bigger;
            s->done_cap = cap;
        }
        if (dom_is(n, "script"))
            s->done[n->id] = 1;
    }
}

static bool is_done(const script_t *s, const dom_node *n)
{
    return n->id < s->done_cap && s->done[n->id];
}

/* --- style und dataset -------------------------------------------------------------------------- */

/* "backgroundColor" -> "background-color"; mit prefix "data-" für dataset */
static size_t kebab(const js_string *key, const char *prefix, char *out, size_t size)
{
    size_t n = strlen(prefix);
    if (n >= size)
        return 0;
    memcpy(out, prefix, n);
    const char *text = key->data;
    size_t len = key->len;
    if (!prefix[0] && len == 8 && memcmp(text, "cssFloat", 8) == 0) {
        text = "float";
        len = 5;
    }
    for (size_t i = 0; i < len && n + 3 < size; i++) {
        char c = text[i];
        if (c >= 'A' && c <= 'Z') {
            out[n++] = '-';
            out[n++] = (char)(c + 32);
        } else {
            out[n++] = c;
        }
    }
    out[n] = '\0';
    return n;
}

static bool blank(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/* Sucht in "a: b; c: d" die Festlegung name; liefert ihren Wert (ohne Leerraum) */
static bool find_declaration(const char *style, const char *name, const char **value, size_t *len)
{
    size_t name_len = strlen(name);
    const char *p = style;
    bool found = false;
    while (*p) {
        while (blank(*p) || *p == ';')
            p++;
        const char *key = p;
        while (*p && *p != ':' && *p != ';')
            p++;
        const char *key_end = p;
        while (key_end > key && blank(key_end[-1]))
            key_end--;
        if (*p != ':')
            continue;
        p++;
        while (blank(*p))
            p++;
        const char *v = p;
        int level = 0;
        char quote = 0;
        while (*p && (quote || level > 0 || *p != ';')) {
            if (quote) {
                if (*p == quote)
                    quote = 0;
            } else if (*p == '"' || *p == '\'') {
                quote = *p;
            } else if (*p == '(') {
                level++;
            } else if (*p == ')' && level > 0) {
                level--;
            }
            p++;
        }
        const char *v_end = p;
        while (v_end > v && blank(v_end[-1]))
            v_end--;
        bool same = (size_t)(key_end - key) == name_len;
        for (size_t i = 0; same && i < name_len; i++)
            same = (key[i] | 32) == (name[i] | 32) || key[i] == name[i];
        if (same) {                     /* die letzte gilt */
            *value = v;
            *len = (size_t)(v_end - v);
            found = true;
        }
    }
    return found;
}

/* Setzt (value != NULL) oder entfernt eine Festlegung im style-Attribut */
static bool set_declaration(dom_node *node, const char *name, const char *value, size_t value_len)
{
    const char *style = dom_attr_get(node, "style");
    size_t old = style ? strlen(style) : 0, name_len = strlen(name);
    /*
     * Jede übernommene Festlegung wird um höchstens zwei Zeichen länger
     * ("a:b" -> " a:b;"), und mehr als (old + 1) / 2 davon passen nicht in old.
     */
    size_t cap = 2 * old + name_len + value_len + 16;
    char *out = kmalloc(cap);
    if (!out)
        return false;
    size_t n = 0;
    /* alle anderen Festlegungen übernehmen */
    const char *p = style ? style : "";
    while (*p) {
        while (blank(*p) || *p == ';')
            p++;
        const char *start = p;
        int level = 0;
        char quote = 0;
        while (*p && (quote || level > 0 || *p != ';')) {
            if (quote) {
                if (*p == quote)
                    quote = 0;
            } else if (*p == '"' || *p == '\'') {
                quote = *p;
            } else if (*p == '(') {
                level++;
            } else if (*p == ')' && level > 0) {
                level--;
            }
            p++;
        }
        const char *end = p;
        while (end > start && blank(end[-1]))
            end--;
        const char *colon = start;
        while (colon < end && *colon != ':')
            colon++;
        const char *key_end = colon;
        while (key_end > start && blank(key_end[-1]))
            key_end--;
        bool same = colon < end && (size_t)(key_end - start) == name_len;
        for (size_t i = 0; same && i < name_len; i++)
            same = (start[i] | 32) == (name[i] | 32) || start[i] == name[i];
        if (same || end == start)
            continue;
        if (n)
            out[n++] = ' ';
        memcpy(out + n, start, (size_t)(end - start));
        n += (size_t)(end - start);
        out[n++] = ';';
    }
    if (value && value_len) {
        if (n)
            out[n++] = ' ';
        memcpy(out + n, name, name_len);
        n += name_len;
        out[n++] = ':';
        out[n++] = ' ';
        memcpy(out + n, value, value_len);
        n += value_len;
        out[n++] = ';';
    }
    bool ok = true;
    if (n == 0)
        dom_attr_remove(node, "style");
    else
        ok = dom_attr_set(node, "style", out, n);
    kfree(out);
    return ok;
}

static bool style_get(js_state *J, js_object *o, js_string *key, bool *found)
{
    dom_node *node = o->u.host.ptr;
    if (js_lookup(o, key, NULL))
        return true;                    /* Methoden und was Skripte selbst angehängt haben */
    const char *style = dom_attr_get(node, "style");
    *found = true;
    if (key->len == 7 && memcmp(key->data, "cssText", 7) == 0)
        return js_push_cstr(J, style ? style : "");
    if (key == J->k[K_length])
        return js_push(J, js_num(0));
    char name[220];
    const char *value;
    size_t len;
    if (!kebab(key, "", name, sizeof(name)) || !style || !find_declaration(style, name, &value, &len))
        return js_push(J, js_str(J->empty));
    return js_push_lstr(J, value, len);
}

static bool style_put(js_state *J, js_object *o, js_string *key, const js_value *v, bool *done)
{
    dom_node *node = o->u.host.ptr;
    char name[220];
    *done = true;
    js_push(J, *v);
    bool remove = v->t <= JS_T_NULL;
    if (!remove && !js_to_string(J, J->top - 1))
        return false;
    const js_string *text = remove ? J->empty : js_top(J, 0)->u.s;
    bool ok = true;
    if (key->len == 7 && memcmp(key->data, "cssText", 7) == 0) {
        if (text->len)
            ok = dom_attr_set(node, "style", text->data, text->len);
        else
            dom_attr_remove(node, "style");
    } else if (kebab(key, "", name, sizeof(name))) {
        /* was die Angabe sprengen würde, wird nicht übernommen */
        bool clean = true;
        for (uint32_t i = 0; i < text->len; i++)
            clean &= text->data[i] != ';' && text->data[i] != '{' && text->data[i] != '}';
        if (clean)
            ok = set_declaration(node, name, text->len ? text->data : NULL, text->len);
    }
    js_pop(J, 1);
    return ok ? true : js_fatal(J, "Nicht genug Speicher für das Skript");
}

static const js_host_class style_class = { "CSSStyleDeclaration", style_get, style_put, NULL, NULL };

/* berechnete Stile (getComputedStyle): weiter unten */
static const js_host_class computed_class;
static bool push_computed(js_state *J, const script_t *s, const dom_node *n, const char *name);

static bool style_method(js_state *J, int argc)
{
    js_value *self = js_this(J);
    if (self->t != JS_T_OBJ || self->u.o->cls != JS_C_HOST ||
        (self->u.o->u.host.cls != &style_class && self->u.o->u.host.cls != &computed_class))
        return js_throw(J, JS_E_TYPE, "Illegal invocation");
    dom_node *node = self->u.o->u.host.ptr;
    const char *name, *value = NULL;
    size_t name_len, value_len = 0;
    if (!arg_text(J, 0, &name, &name_len))
        return false;
    if (name_len == 0 || name_len > 200)
        return js_push(J, js_str(J->empty));
    if (self->u.o->u.host.cls == &computed_class) {     /* berechnete Stile: nur lesen */
        char lowered[204];
        for (size_t i = 0; i < name_len; i++)
            lowered[i] = name[i] >= 'A' && name[i] <= 'Z' ? (char)(name[i] + 32) : name[i];
        lowered[name_len] = '\0';
        return MAGIC == 0 ? push_computed(J, script_of(J), node, lowered) : js_push(J, js_str(J->empty));
    }
    const char *style = dom_attr_get(node, "style");
    switch (MAGIC) {
    case 0:                             /* getPropertyValue */
        if (style && find_declaration(style, name, &value, &value_len))
            return js_push_lstr(J, value, value_len);
        return js_push(J, js_str(J->empty));
    case 1:                             /* setProperty */
        if (argc > 1 && js_arg(J, 1)->t > JS_T_NULL && !arg_text(J, 1, &value, &value_len))
            return false;
        for (size_t i = 0; i < value_len; i++) {
            if (value[i] == ';' || value[i] == '{' || value[i] == '}')
                return js_push(J, js_undef());
        }
        set_declaration(node, name, value, value_len);
        return js_push(J, js_undef());
    default:                            /* removeProperty */
        set_declaration(node, name, NULL, 0);
        return js_push(J, js_str(J->empty));
    }
}

/* --- Was gilt laut Gestaltung? (getComputedStyle, offsetWidth ...) ----------------------------- */

/* Verbirgt die Gestaltung dieses eine Element? Dieselben Regeln wie beim Seitenaufbau (html.c). */
static bool node_hidden(const script_t *s, const dom_node *n, css_style_t *cs)
{
    const char *style = dom_attr_get(n, "style");
    css_match(s->css, &n->css, style, style ? strlen(style) : 0, cs);
    if (((cs->has & CSS_DISPLAY) && cs->display == CSS_NONE) ||
        ((cs->has & CSS_VISIBILITY) && cs->hidden) || ((cs->has & CSS_CLIP) && cs->clipped) ||
        cs->off_top)
        return true;
    if (cs->has & CSS_DISPLAY)
        return false;                   /* ausdrücklich gezeigt */
    const char *aria = dom_attr_get(n, "aria-hidden"), *class = dom_attr_get(n, "class");
    return dom_attr_get(n, "hidden") || (aria && strcmp(aria, "true") == 0) ||
           (class && html_class_hides(class, strlen(class), true));
}

static bool name_in(const dom_node *n, const char *const *names)
{
    for (int i = 0; names[i]; i++) {
        if (dom_is(n, names[i]))
            return true;
    }
    return false;
}

static const char *const unseen[] = { "head", "script", "style", "meta", "link", "title", "template",
                                      "noscript", "base", NULL };

/* Steht das Element in der Seite und ist es samt allem, worin es steckt, zu sehen? */
static bool node_shown(const script_t *s, const dom_node *n)
{
    css_style_t cs;
    if (n->type != DOM_ELEMENT)
        return false;
    for (const dom_node *p = n; ; p = p->parent) {
        if (!p)
            return false;               /* hängt nicht in der Seite */
        if (p == s->doc->root)
            return true;
        if (p->type == DOM_ELEMENT && (name_in(p, unseen) || node_hidden(s, p, &cs)))
            return false;
    }
}

/* Der Wert, den getComputedStyle für name liefert - soweit er sich hier bestimmen lässt */
static bool push_computed(js_state *J, const script_t *s, const dom_node *n, const char *name)
{
    static const char *const blocks[] = {
        "address", "article", "aside", "blockquote", "body", "center", "details", "dialog", "dd",
        "div", "dl", "dt", "fieldset", "figcaption", "figure", "footer", "form", "h1", "h2", "h3",
        "h4", "h5", "h6", "header", "hgroup", "hr", "html", "main", "menu", "nav", "ol", "p", "pre",
        "section", "summary", "ul", NULL };
    static const char *const boxes[] = { "img", "input", "button", "select", "textarea", NULL };
    static const char *const groups[] = { "thead", "tbody", "tfoot", NULL };
    static const char *const cells[] = { "td", "th", NULL };
    css_style_t cs;
    bool hidden = node_hidden(s, n, &cs);
    if (strcmp(name, "display") == 0) {
        bool invisible = (cs.has & CSS_VISIBILITY) && cs.hidden && !((cs.has & CSS_DISPLAY) && cs.display == CSS_NONE);
        bool moved = (((cs.has & CSS_CLIP) && cs.clipped) || cs.off_top) && !((cs.has & CSS_DISPLAY) && cs.display == CSS_NONE);
        const char *value = "inline";
        if (name_in(n, unseen) || (hidden && !invisible && !moved))
            value = "none";
        else if (cs.has & CSS_DISPLAY)
            value = cs.display == CSS_INLINE ? "inline" : "block";
        else if (name_in(n, blocks))
            value = "block";
        else if (dom_is(n, "li"))
            value = "list-item";
        else if (dom_is(n, "table"))
            value = "table";
        else if (dom_is(n, "tr"))
            value = "table-row";
        else if (name_in(n, cells))
            value = "table-cell";
        else if (name_in(n, groups))
            value = "table-row-group";
        else if (name_in(n, boxes))
            value = "inline-block";
        return js_push_cstr(J, value);
    }
    if (strcmp(name, "visibility") == 0)
        return js_push_cstr(J, (cs.has & CSS_VISIBILITY) && cs.hidden ? "hidden" : "visible");
    if (strcmp(name, "float") == 0)
        return js_push_cstr(J, cs.float_side == CSS_SIDE_LEFT ? "left" : cs.float_side == CSS_SIDE_RIGHT ? "right" : "none");
    if (strcmp(name, "opacity") == 0 || strcmp(name, "position") == 0) {
        const char *style = dom_attr_get(n, "style"), *value;
        size_t len;
        if (style && find_declaration(style, name, &value, &len))
            return js_push_lstr(J, value, len);
        return js_push_cstr(J, name[0] == 'o' ? "1" : "static");
    }
    /* alles andere: was am Element selbst steht */
    const char *style = dom_attr_get(n, "style"), *value;
    size_t len;
    if (style && find_declaration(style, name, &value, &len))
        return js_push_lstr(J, value, len);
    return js_push(J, js_str(J->empty));
}

static bool computed_get(js_state *J, js_object *o, js_string *key, bool *found)
{
    dom_node *node = o->u.host.ptr;
    char name[220];
    if (js_lookup(o, key, NULL))
        return true;                    /* getPropertyValue und Verwandte */
    *found = true;
    if (key == J->k[K_length])
        return js_push(J, js_num(0));
    if (!kebab(key, "", name, sizeof(name)))
        return js_push(J, js_str(J->empty));
    return push_computed(J, script_of(J), node, name);
}

static bool computed_put(js_state *J, js_object *o, js_string *key, const js_value *v, bool *done)
{
    (void)J; (void)o; (void)key; (void)v;
    *done = true;                       /* nur zum Lesen */
    return true;
}

static const js_host_class computed_class = { "CSSStyleDeclaration", computed_get, computed_put, NULL, NULL };

static bool dataset_get(js_state *J, js_object *o, js_string *key, bool *found)
{
    char name[220];
    const char *value = kebab(key, "data-", name, sizeof(name))
                            ? dom_attr_get(o->u.host.ptr, name) : NULL;
    if (!value)
        return true;
    *found = true;
    return js_push_cstr(J, value);
}

static bool dataset_put(js_state *J, js_object *o, js_string *key, const js_value *v, bool *done)
{
    char name[220];
    *done = true;
    js_push(J, *v);
    if (!js_to_string(J, J->top - 1))
        return false;
    if (kebab(key, "data-", name, sizeof(name)) && safe_name(name, strlen(name), false))
        dom_attr_set(o->u.host.ptr, name, js_top(J, 0)->u.s->data, js_top(J, 0)->u.s->len);
    js_pop(J, 1);
    return true;
}

static const js_host_class dataset_class = { "DOMStringMap", dataset_get, dataset_put, NULL, NULL };

/* --- Die Grundfunktionen für domlib.js ------------------------------------------------------------ */

enum {
    D_DOC, D_TYPE, D_NAME, D_PARENT, D_FIRST, D_LAST, D_NEXT, D_PREV, D_KIDS, D_ATTR, D_SETATTR,
    D_DELATTR, D_ATTRS, D_TEXT, D_SETTEXT, D_HTML, D_SETHTML, D_EL, D_TEXTNODE, D_COMMENT, D_FRAG,
    D_PARSE, D_INSERT, D_REMOVE, D_CLONE, D_CONTAINS, D_QUERY, D_MATCHES, D_BYID, D_BYTAG, D_BYCLASS,
    D_VALUE, D_SETVALUE, D_CHECKED, D_SETCHECKED, D_SELECTED, D_SETSELECTED, D_STYLE, D_DATASET,
    D_URL, D_RESOLVE, D_WISH, D_ALERT, D_CONFIRM, D_WRITE, D_SCRIPT, D_SIZE, D_PROTOS, D_DISPATCHER,
    D_SHOWN, D_COMPUTED, D_REQUEST,
};

static bool lower_name(js_state *J, int i, char *out, size_t size)
{
    const char *text;
    size_t len;
    if (!arg_text(J, i, &text, &len))
        return false;
    if (len >= size) {
        out[0] = '\0';                  /* so lange Namen gibt es nicht */
        return true;
    }
    for (size_t k = 0; k < len; k++)
        out[k] = text[k] >= 'A' && text[k] <= 'Z' ? (char)(text[k] + 32) : text[k];
    out[len] = '\0';
    return true;
}

static void add_wish(script_t *s, script_wish_t kind, const char *url, uint32_t node, uint32_t button)
{
    if (s->wish_count >= WISHES) {
        /* der neueste Wunsch zählt: den ältesten fallen lassen */
        memmove(&s->wishes[0], &s->wishes[1], sizeof(s->wishes[0]) * (WISHES - 1));
        s->wish_count--;
    }
    s->wishes[s->wish_count].kind = kind;
    strlcpy(s->wishes[s->wish_count].url, url ? url : "", URL_SIZE);
    s->wishes[s->wish_count].node = node;
    s->wishes[s->wish_count].button = button;
    s->wish_count++;
}

static int selected_index(const dom_node *select)
{
    int index = 0, chosen = -1;
    for (dom_node *n = dom_next(select, select); n; n = dom_next(select, n)) {
        if (!dom_is(n, "option"))
            continue;
        if (select->selected != -2 ? index == select->selected
                                   : chosen < 0 && dom_attr_get(n, "selected") != NULL)
            chosen = index;
        index++;
    }
    if (chosen < 0 && select->selected == -2 && index > 0 && !dom_attr_get(select, "multiple"))
        chosen = 0;
    return chosen;
}

/* --- Daten nachladen (XMLHttpRequest, fetch) ---------------------------------------------------- */

/* Länge von "schema://rechner[:port]" - 0, wenn url so nicht beginnt */
static size_t origin_of(const char *url)
{
    size_t n = 0;
    while (url[n] && url[n] != ':' && url[n] != '/')
        n++;
    if (n == 0 || url[n] != ':' || url[n + 1] != '/' || url[n + 2] != '/')
        return 0;
    n += 3;
    while (url[n] && url[n] != '/' && url[n] != '?' && url[n] != '#')
        n++;
    return n;
}

static bool same_origin(const char *a, const char *b)
{
    size_t n = origin_of(a);
    if (n == 0 || n != origin_of(b))
        return false;
    for (size_t i = 0; i < n; i++) {
        char x = a[i], y = b[i];
        if ((x >= 'A' && x <= 'Z' ? x + 32 : x) != (y >= 'A' && y <= 'Z' ? y + 32 : y))
            return false;
    }
    return true;
}

/* Kopfzeilen, die ein Skript nicht selbst bestimmen darf */
static bool header_forbidden(const char *name, size_t len)
{
    static const char *const names[] = { "host", "cookie", "cookie2", "content-length", "connection",
                                         "transfer-encoding", "upgrade", "origin", "referer",
                                         "user-agent", "accept-encoding", "te", "trailer", "via",
                                         "keep-alive", "expect", "date", NULL };
    for (int i = 0; names[i]; i++) {
        size_t k = 0;
        while (k < len && names[i][k] && (name[k] | 32) == names[i][k])
            k++;
        if (k == len && !names[i][k])
            return true;
    }
    static const char *const prefixes[] = { "proxy-", "sec-", "access-control-", NULL };
    for (int i = 0; prefixes[i]; i++) {
        size_t k = 0;
        while (k < len && prefixes[i][k] && (name[k] | 32) == prefixes[i][k])
            k++;
        if (!prefixes[i][k])
            return true;
    }
    return false;
}

/*
 * Baut die Kopfzeilen der Anfrage aus dem, was das Skript wünscht (Zeilen
 * "Name: Wert", getrennt durch \n): nur ordentliche Namen und Werte, nichts
 * Verbotenes. Liefert einen Puffer (kmalloc) oder NULL.
 */
static char *request_headers(const char *text, size_t len)
{
    char *out = kmalloc(len * 2 + 4);
    size_t n = 0;
    if (!out)
        return NULL;
    for (size_t at = 0; at < len;) {
        size_t end = at;
        while (end < len && text[end] != '\n')
            end++;
        size_t colon = at;
        bool ok = true;
        while (colon < end && text[colon] != ':') {
            char c = text[colon++];
            ok &= (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                  c == '-' || c == '_';
        }
        ok &= colon < end && colon > at && !header_forbidden(text + at, colon - at);
        for (size_t i = colon; ok && i < end; i++)
            ok &= (unsigned char)text[i] >= 32 && text[i] != 127;
        if (ok) {
            memcpy(out + n, text + at, end - at);
            n += end - at;
            out[n++] = '\r';
            out[n++] = '\n';
        }
        at = end + 1;
    }
    out[n] = '\0';
    return out;
}

/* --- Daten nachladen: was ein Skript von welchem Server holen und sehen darf ------------------------ */

static char fold(char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c;
}

static bool same_text(const char *a, size_t an, const char *b)
{
    size_t bn = strlen(b);
    if (an != bn)
        return false;
    for (size_t i = 0; i < an; i++) {
        if (fold(a[i]) != fold(b[i]))
            return false;
    }
    return true;
}

/*
 * Der Wert der Kopfzeile name in einem Block aus Zeilen "Name: Wert" (Ende
 * mit \n oder \r\n), ohne Leerraum am Rand; NULL, wenn es sie nicht gibt.
 */
static const char *header_find(const char *block, const char *name, size_t *len)
{
    for (const char *line = block; line && *line;) {
        const char *end = line;
        while (*end && *end != '\n')
            end++;
        const char *colon = line;
        while (colon < end && *colon != ':')
            colon++;
        if (colon < end && same_text(line, (size_t)(colon - line), name)) {
            const char *v = colon + 1, *e = end;
            while (v < e && (*v == ' ' || *v == '\t'))
                v++;
            while (e > v && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r'))
                e--;
            *len = (size_t)(e - v);
            return v;
        }
        line = *end ? end + 1 : end;
    }
    return NULL;
}

/* Steht token in der Liste "a, b, c"? (Groß/Klein ist gleich.) */
static bool listed(const char *list, size_t list_len, const char *token, size_t token_len)
{
    for (size_t at = 0; at < list_len;) {
        size_t end = at;
        while (end < list_len && list[end] != ',')
            end++;
        size_t from = at, to = end;
        while (from < to && list[from] == ' ')
            from++;
        while (to > from && list[to - 1] == ' ')
            to--;
        if (to - from == token_len) {
            size_t i = 0;
            while (i < token_len && fold(list[from + i]) == fold(token[i]))
                i++;
            if (i == token_len)
                return true;
        }
        at = end + 1;
    }
    return false;
}

static bool is_star(const char *value, size_t len)
{
    return len == 1 && value[0] == '*';
}

/*
 * Zeigt die Adresse auf den eigenen Rechner oder ins eigene Netz (Router,
 * Drucker ...)? Solche Ziele darf eine Seite aus dem Internet nicht ansprechen.
 * Erkannt wird das am Namen: Zahlenadressen der privaten Bereiche, localhost.
 */
static bool private_host(const char *url)
{
    const char *h = url;
    while (*h && !(h[0] == ':' && h[1] == '/' && h[2] == '/'))
        h++;
    if (!*h)
        return false;
    h += 3;
    size_t n = 0;
    if (h[0] == '[') {
        /* IPv6: ::1, fc00::/7, fe80::/10 */
        return (h[1] == ':' && h[2] == ':' && h[3] == '1' && h[4] == ']') ||
               ((fold(h[1]) == 'f') && (fold(h[2]) == 'c' || fold(h[2]) == 'd')) ||
               (fold(h[1]) == 'f' && fold(h[2]) == 'e' && (fold(h[3]) >= '8' && fold(h[3]) <= 'b'));
    }
    while (h[n] && h[n] != '/' && h[n] != ':' && h[n] != '?' && h[n] != '#')
        n++;
    while (n > 0 && h[n - 1] == '.')
        n--;                            /* "localhost." ist derselbe Rechner */
    if (same_text(h, n, "localhost") || (n > 10 && same_text(h + n - 10, 10, ".localhost")) ||
        (n > 6 && same_text(h + n - 6, 6, ".local")))
        return true;
    /* a.b.c.d */
    unsigned part[4] = { 0, 0, 0, 0 };
    int parts = 0;
    for (size_t i = 0; i < n; i++) {
        if (h[i] == '.') {
            if (++parts > 3)
                return false;
        } else if (h[i] >= '0' && h[i] <= '9') {
            part[parts] = part[parts] * 10 + (unsigned)(h[i] - '0');
            if (part[parts] > 255)
                return false;
        } else {
            return false;               /* ein Name, keine Zahlenadresse */
        }
    }
    if (parts != 3)
        return false;
    return part[0] == 10 || part[0] == 127 || part[0] == 0 || (part[0] == 192 && part[1] == 168) ||
           (part[0] == 172 && part[1] >= 16 && part[1] <= 31) || (part[0] == 169 && part[1] == 254);
}

static bool starts(const char *text, const char *prefix)
{
    size_t n = strlen(prefix);
    return strlen(text) >= n && memcmp(text, prefix, n) == 0;
}

/* Hängt Text an einen Puffer an, in dem genug Platz ist */
static void put(char *out, size_t *n, const char *text)
{
    size_t len = strlen(text);
    memcpy(out + *n, text, len + 1);
    *n += len;
}

/* Gehört die Kopfzeile zu denen, die jede Seite an jeden Server schicken darf? */
static bool simple_header(const char *name, size_t n, const char *value, size_t vn)
{
    if (vn > 128)
        return false;
    if (same_text(name, n, "accept") || same_text(name, n, "accept-language") ||
        same_text(name, n, "content-language"))
        return true;
    if (!same_text(name, n, "content-type"))
        return false;
    size_t end = 0;
    while (end < vn && value[end] != ';')
        end++;
    while (end > 0 && value[end - 1] == ' ')
        end--;
    return same_text(value, end, "application/x-www-form-urlencoded") ||
           same_text(value, end, "multipart/form-data") || same_text(value, end, "text/plain");
}

/*
 * Sammelt die Namen der Kopfzeilen einer Anfrage, die über das Übliche
 * hinausgehen (klein geschrieben, mit Komma getrennt); true, wenn es keine gibt.
 */
static bool only_simple_headers(const char *lines, char *names, size_t size)
{
    size_t out = 0;
    names[0] = '\0';
    for (const char *line = lines; *line;) {
        const char *end = line;
        while (*end && *end != '\n')
            end++;
        const char *colon = line;
        while (colon < end && *colon != ':')
            colon++;
        const char *v = colon < end ? colon + 1 : end, *e = end;
        while (v < e && *v == ' ')
            v++;
        while (e > v && (e[-1] == '\r' || e[-1] == ' '))
            e--;
        size_t n = (size_t)(colon - line);
        if (colon < end && !simple_header(line, n, v, (size_t)(e - v)) && out + n + 2 < size &&
            !listed(names, out, line, n)) {
            if (out)
                names[out++] = ',';
            for (size_t i = 0; i < n; i++)
                names[out++] = fold(line[i]);
            names[out] = '\0';
        }
        line = *end ? end + 1 : end;
    }
    return out == 0;
}

/* Streicht aus den Kopfzeilen einer Anfrage (je mit \r\n) alle, die ihren Inhalt beschreiben. */
static void drop_content_headers(char *lines)
{
    char *out = lines;
    for (const char *line = lines; *line;) {
        const char *end = line;
        while (*end && *end != '\n')
            end++;
        if (*end)
            end++;
        size_t n = (size_t)(end - line);
        bool content = n >= 8 && same_text(line, 8, "content-");
        if (!content) {
            memmove(out, line, n);
            out += n;
        }
        line = end;
    }
    *out = '\0';
}

/* Erlaubt die Antwort des fremden Servers der Seite mit diesem Ursprung den Zugriff? */
static bool cors_allowed(const char *headers, const char *origin, bool credentials)
{
    size_t len = 0;
    const char *v = headers ? header_find(headers, "access-control-allow-origin", &len) : NULL;
    if (!v)
        return false;
    if (is_star(v, len))
        return !credentials;            /* "jeder" gilt nicht, wenn Cookies im Spiel sind */
    if (!same_text(v, len, origin))
        return false;
    if (!credentials)
        return true;
    v = header_find(headers, "access-control-allow-credentials", &len);
    return v && len == 4 && memcmp(v, "true", 4) == 0;
}

static void reply_free(script_reply_t *reply)
{
    kfree(reply->body);
    kfree(reply->headers);
    memset(reply, 0, sizeof(*reply));
}

/* Ein Austausch mit dem Server; die Wartezeit zählt nicht gegen das Skript. */
static bool exchange(js_state *J, script_t *s, const char *method, const char *url,
                     const char *lines, const char *body, size_t body_len, bool credentials,
                     script_reply_t *reply)
{
    uint64_t before = time_ms();
    memset(reply, 0, sizeof(*reply));
    unsigned flags = (credentials ? SCRIPT_COOKIES : 0) | (s->host.private_page ? 0 : SCRIPT_PUBLIC_ONLY);
    bool ok = s->host.request(s->host.ctx, method, url, lines, body, body_len, flags, reply);
    uint64_t waited = time_ms() - before;
    if (J->deadline)
        J->deadline += waited;
    if (s->boot_until)
        s->boot_until += waited;
    if (!ok)
        reply_free(reply);
    return ok;
}

/*
 * Die Voranfrage: Ehe eine Seite einem fremden Server etwas Ungewöhnliches
 * schickt (PUT, DELETE, eigene Kopfzeilen), fragt der Browser ihn mit OPTIONS,
 * ob er das von dieser Seite annimmt.
 */
static bool preflight(js_state *J, script_t *s, const char *url, const char *origin,
                      const char *method, const char *names, bool credentials)
{
    char *lines = kmalloc(strlen(origin) + strlen(names) + 160);
    script_reply_t reply;
    if (!lines)
        return false;
    size_t n = 0;
    put(lines, &n, "Origin: ");
    put(lines, &n, origin);
    put(lines, &n, "\r\nAccess-Control-Request-Method: ");
    put(lines, &n, method);
    put(lines, &n, "\r\n");
    if (names[0]) {
        put(lines, &n, "Access-Control-Request-Headers: ");
        put(lines, &n, names);
        put(lines, &n, "\r\n");
    }
    bool ok = exchange(J, s, "OPTIONS", url, lines, NULL, 0, false, &reply);
    kfree(lines);
    if (!ok)
        return false;
    ok = reply.status >= 200 && reply.status < 300 && cors_allowed(reply.headers, origin, credentials);
    size_t len = 0;
    const char *allowed;
    if (ok && strcmp(method, "GET") != 0 && strcmp(method, "HEAD") != 0 && strcmp(method, "POST") != 0) {
        allowed = header_find(reply.headers, "access-control-allow-methods", &len);
        ok = allowed && (listed(allowed, len, method, strlen(method)) || (is_star(allowed, len) && !credentials));
    }
    if (ok && names[0]) {
        allowed = header_find(reply.headers, "access-control-allow-headers", &len);
        ok = allowed != NULL;
        for (const char *name = names; ok && *name;) {
            const char *end = name;
            while (*end && *end != ',')
                end++;
            ok = listed(allowed, len, name, (size_t)(end - name)) || (is_star(allowed, len) && !credentials);
            name = *end ? end + 1 : end;
        }
    }
    reply_free(&reply);
    return ok;
}

/* Die Kopfzeilen der Antwort, soweit das Skript sie sehen darf (Zeilen "name: wert\n") */
static char *visible_headers(const script_reply_t *reply, bool foreign, bool credentials)
{
    static const char *const always[] = { "cache-control", "content-language", "content-length",
                                          "content-type", "expires", "last-modified", "pragma", NULL };
    const char *all = reply->headers ? reply->headers : "";
    size_t expose_len = 0;
    const char *expose = foreign ? header_find(all, "access-control-expose-headers", &expose_len) : NULL;
    char *out = kmalloc(strlen(all) + 2);
    size_t n = 0;
    if (!out)
        return NULL;
    for (const char *line = all; *line;) {
        const char *end = line;
        while (*end && *end != '\n')
            end++;
        const char *colon = line;
        while (colon < end && *colon != ':')
            colon++;
        size_t name = (size_t)(colon - line);
        bool show = colon < end && !same_text(line, name, "set-cookie") && !same_text(line, name, "set-cookie2");
        if (show && foreign) {
            show = false;
            for (int i = 0; always[i] && !show; i++)
                show = same_text(line, name, always[i]);
            if (!show && expose)
                show = listed(expose, expose_len, line, name) || (is_star(expose, expose_len) && !credentials);
        }
        if (show) {
            for (const char *p = line; p < end; p++) {
                if (*p != '\r')
                    out[n++] = p < colon ? fold(*p) : *p;
            }
            out[n++] = '\n';
        }
        line = *end ? end + 1 : end;
    }
    out[n] = '\0';
    return out;
}

enum { MODE_CORS, MODE_SAME_ORIGIN, MODE_NO_CORS };
enum { CRED_OMIT, CRED_SAME_ORIGIN, CRED_INCLUDE };

/*
 * XMLHttpRequest und fetch: request(verfahren, adresse, kopfzeilen, inhalt,
 * cookies, art) -> [status, typ, text, adresse, kopfzeilen, sicht, umgeleitet]
 * oder null, wenn die Anfrage nicht erlaubt ist oder scheitert.
 *
 * Zum Server der Seite geht alles. Einen fremden Server darf ein Skript
 * fragen, die Antwort aber nur sehen, wenn der Server es dieser Seite
 * ausdrücklich erlaubt (Access-Control-Allow-Origin); die Cookies des
 * Benutzers gehen dorthin nur auf ausdrücklichen Wunsch und mit eigener
 * Erlaubnis des Servers mit. Weiterleitungen werden hier verfolgt und bei
 * jedem Schritt neu geprüft.
 */
static bool request(js_state *J, script_t *s)
{
    static const char *const methods[] = { "GET", "HEAD", "POST", "PUT", "PATCH", "DELETE", "OPTIONS", NULL };
    const char *given, *target, *headers = "", *body = NULL, *method = NULL;
    size_t given_len, target_len, headers_len = 0, body_len = 0;
    double number = 0;
    if (!arg_text(J, 0, &given, &given_len) || !arg_text(J, 1, &target, &target_len))
        return false;
    if (js_arg(J, 2)->t > JS_T_NULL && !arg_text(J, 2, &headers, &headers_len))
        return false;
    if (js_arg(J, 3)->t > JS_T_NULL && !arg_text(J, 3, &body, &body_len))
        return false;
    int credentials = CRED_SAME_ORIGIN, mode = MODE_CORS;
    if (js_arg(J, 4)->t == JS_T_NUM && js_to_number(J, ARGI(4), &number))
        credentials = number == 0 ? CRED_OMIT : number == 2 ? CRED_INCLUDE : CRED_SAME_ORIGIN;
    if (js_arg(J, 5)->t == JS_T_NUM && js_to_number(J, ARGI(5), &number))
        mode = number == 1 ? MODE_SAME_ORIGIN : number == 2 ? MODE_NO_CORS : MODE_CORS;
    for (int i = 0; methods[i] && !method; i++) {
        if (same_text(given, given_len, methods[i]))
            method = methods[i];
    }

    char *url = kmalloc(URL_SIZE), *next = kmalloc(URL_SIZE), *origin = kmalloc(URL_SIZE);
    char *from = kmalloc(URL_SIZE);     /* die Adresse vor der letzten Weiterleitung */
    char *lines = request_headers(headers, headers_len), *sent = NULL, *names = kmalloc(1024);
    script_reply_t reply;
    memset(&reply, 0, sizeof(reply));
    bool ok = s->host.request && method && url && next && origin && from && lines && names &&
              headers_len < 8192 && body_len <= REQUEST_BYTES &&
              url_resolve(s->url, target, url, URL_SIZE);
    bool foreign = false, redirected = false, opaque = false, with_cookies = false;
    if (ok) {
        size_t n = origin_of(s->url);
        for (size_t i = 0; i < n; i++)
            origin[i] = fold(s->url[i]);
        origin[n] = '\0';
        if (strcmp(method, "GET") == 0 || strcmp(method, "HEAD") == 0)
            body = NULL;
        sent = kmalloc(strlen(lines) + strlen(origin) + 16);
        ok = sent != NULL;
    }
    for (int hop = 0; ok; hop++) {
        ok = hop < 8 && (starts(url, "http://") || starts(url, "https://"));
        if (!ok)
            break;
        if (!same_origin(s->url, url)) {
            /* über einen fremden Server zu einem dritten: Der soll nicht glauben, die Seite
               habe ihn gezielt angesprochen - die Herkunft heißt dann "null" */
            if (foreign && !same_origin(from, url))
                strlcpy(origin, "null", URL_SIZE);
            foreign = true;             /* ab hier gilt die Antwort als die eines fremden Servers */
        }
        /* nichts im eigenen Netz von einer Seite aus dem Internet (hier am Namen erkannt; an der
           Adresse, zu der wirklich verbunden wird, prüft es der Browser noch einmal) */
        if (!s->host.private_page && private_host(url)) {
            ok = false;
            break;
        }
        /* nichts Fremdes, wenn die Seite das ausschließt; nichts Unverschlüsseltes von einer
           verschlüsselten Seite aus */
        if (foreign && (mode == MODE_SAME_ORIGIN || (starts(s->url, "https://") && !starts(url, "https://")))) {
            ok = false;
            break;
        }
        with_cookies = credentials == CRED_INCLUDE || (credentials == CRED_SAME_ORIGIN && !foreign);
        bool simple = only_simple_headers(lines, names, 1024) &&
                      (strcmp(method, "GET") == 0 || strcmp(method, "HEAD") == 0 || strcmp(method, "POST") == 0);
        if (foreign && mode == MODE_NO_CORS && !simple) {
            ok = false;
            break;
        }
        if (foreign && mode == MODE_CORS && !simple &&
            !preflight(J, s, url, origin, method, names, with_cookies)) {
            ok = false;
            break;
        }
        size_t sent_len = 0;
        put(sent, &sent_len, lines);
        if (foreign) {
            put(sent, &sent_len, "Origin: ");
            put(sent, &sent_len, origin);
            put(sent, &sent_len, "\r\n");
        }
        ok = exchange(J, s, method, url, sent, body, body ? body_len : 0, with_cookies, &reply);
        if (!ok)
            break;
        if (foreign && mode == MODE_CORS && !cors_allowed(reply.headers, origin, with_cookies)) {
            ok = false;
            break;
        }
        size_t len = 0;
        const char *location = reply.headers ? header_find(reply.headers, "location", &len) : NULL;
        bool redirect = (reply.status >= 301 && reply.status <= 303) || reply.status == 307 ||
                        reply.status == 308;
        if (!redirect || !location || len == 0 || len >= URL_SIZE)
            break;
        /* weitergeschickt: dem Ziel folgen - und es im nächsten Durchgang wie jede Adresse prüfen */
        memcpy(next, location, len);
        next[len] = '\0';
        strlcpy(from, url, URL_SIZE);
        ok = url_resolve(from, next, url, URL_SIZE);
        if (reply.status == 303 || ((reply.status == 301 || reply.status == 302) && strcmp(method, "POST") == 0)) {
            if (strcmp(method, "HEAD") != 0)
                method = "GET";
            body = NULL;
            drop_content_headers(lines);    /* ohne Inhalt auch keine Angaben zum Inhalt */
        }
        reply_free(&reply);
        redirected = true;
    }
    if (ok && foreign && mode == MODE_NO_CORS)
        opaque = true;                  /* abgeschickt, aber die Antwort bleibt dem Skript verborgen */
    char *shown = ok && !opaque ? visible_headers(&reply, foreign, with_cookies) : NULL;
    if (ok && !opaque && (!shown || reply.length > JS_STRING_MAX / 2))
        ok = false;
    if (ok) {
        js_object *out = js_array_new(J, 7);
        js_push(J, js_obj(out));
        reply.content_type[sizeof(reply.content_type) - 1] = '\0';
        const char *type = opaque ? "" : reply.content_type;
        js_array_push(J, out, js_num(opaque ? 0 : reply.status));
        js_array_push(J, out, js_str(js_string_new(J, type, strlen(type))));
        js_array_push(J, out, js_str(js_string_new(J, !opaque && reply.body ? reply.body : "",
                                                   !opaque && reply.body ? reply.length : 0)));
        js_array_push(J, out, js_str(js_string_new(J, opaque ? "" : url, opaque ? 0 : strlen(url))));
        js_array_push(J, out, js_str(js_string_new(J, shown ? shown : "", shown ? strlen(shown) : 0)));
        js_array_push(J, out, js_str(js_intern_cstr(J, opaque ? "opaque" : foreign ? "cors" : "basic")));
        js_array_push(J, out, js_bool(redirected && !opaque));
    }
    reply_free(&reply);
    kfree(shown);
    kfree(url);
    kfree(next);
    kfree(origin);
    kfree(from);
    kfree(lines);
    kfree(sent);
    kfree(names);
    return ok ? !J->fatal : js_push(J, js_null());
}

static bool dom_fn(js_state *J, int argc)
{
    script_t *s = script_of(J);
    dom_doc *doc = s->doc;
    dom_node *n = NULL, *other;
    const char *text;
    size_t len;
    char name[204];
    int kind = MAGIC;

    /* fast alle Funktionen beginnen mit einem Knoten */
    switch (kind) {
    case D_DOC: case D_EL: case D_TEXTNODE: case D_COMMENT: case D_FRAG: case D_PARSE: case D_URL:
    case D_RESOLVE: case D_WISH: case D_ALERT: case D_CONFIRM: case D_WRITE: case D_SCRIPT:
    case D_SIZE: case D_PROTOS: case D_DISPATCHER: case D_REQUEST:
        break;
    default:
        n = need_node(J, 0);
        if (!n)
            return false;
    }

    switch (kind) {
    case D_DOC:
        return push_node(J, doc->root);
    case D_TYPE:
        return js_push(J, js_num(n->type));
    case D_NAME:
        return n->type == DOM_ELEMENT ? js_push_cstr(J, n->name) : js_push(J, js_null());
    case D_PARENT: return push_node(J, n->parent);
    case D_FIRST: return push_node(J, n->first);
    case D_LAST: return push_node(J, n->last);
    case D_NEXT: return push_node(J, n->next);
    case D_PREV: return push_node(J, n->prev);
    case D_KIDS: {
        bool elements = js_truthy(js_arg(J, 1));
        js_object *out = js_array_new(J, 0);
        js_push(J, js_obj(out));
        for (dom_node *c = n->first; c; c = c->next) {
            if (elements && c->type != DOM_ELEMENT)
                continue;
            if (!push_node(J, c) || !js_array_push(J, out, *js_top(J, 0)))
                return false;
            js_pop(J, 1);
        }
        return true;
    }
    case D_ATTR: {
        if (!lower_name(J, 1, name, sizeof(name)))
            return false;
        const char *value = n->type == DOM_ELEMENT ? dom_attr_get(n, name) : NULL;
        return value ? js_push_cstr(J, value) : js_push(J, js_null());
    }
    case D_SETATTR:
        if (!lower_name(J, 1, name, sizeof(name)) || !arg_text(J, 2, &text, &len))
            return false;
        if (n->type != DOM_ELEMENT || !safe_name(name, strlen(name), false))
            return js_throw(J, JS_E_ERROR, "InvalidCharacterError: '%s' is not a valid attribute name", name);
        if (!dom_attr_set(n, name, text, len))
            return js_fatal(J, "Die Seite ist für die Skripte zu groß geworden");
        return js_push(J, js_undef());
    case D_DELATTR:
        if (!lower_name(J, 1, name, sizeof(name)))
            return false;
        dom_attr_remove(n, name);
        return js_push(J, js_undef());
    case D_ATTRS: {
        js_object *out = js_array_new(J, n->attr_count);
        js_push(J, js_obj(out));
        for (int i = 0; i < n->attr_count; i++) {
            js_object *pair = js_array_new(J, 2);
            js_push(J, js_obj(pair));
            js_array_push(J, pair, js_str(js_string_new(J, n->attrs[i].name, strlen(n->attrs[i].name))));
            js_array_push(J, pair, js_str(js_string_new(J, n->attrs[i].value, strlen(n->attrs[i].value))));
            if (!js_array_push(J, out, js_obj(pair)))
                return false;
            js_pop(J, 1);
        }
        return true;
    }
    case D_TEXT:
        text = dom_text_content(n, &len);
        return push_owned(J, (char *)text, len);
    case D_SETTEXT:
        if (!arg_text(J, 1, &text, &len))
            return false;
        if (!dom_set_text(n, text, len))
            return js_fatal(J, "Die Seite ist für die Skripte zu groß geworden");
        return js_push(J, js_undef());
    case D_HTML:
        text = dom_html(n, js_truthy(js_arg(J, 1)), &len);
        return push_owned(J, (char *)text, len);
    case D_SETHTML:
        if (!arg_text(J, 1, &text, &len))
            return false;
        if (n->type != DOM_ELEMENT && n->type != DOM_FRAGMENT)
            return js_push(J, js_undef());
        if (!dom_set_inner_html(n, text, len))
            return js_fatal(J, "Die Seite ist für die Skripte zu groß geworden");
        mark_done(s, n, false);         /* so eingefügte Skripte laufen nicht */
        return js_push(J, js_undef());
    case D_EL:
        if (!arg_text(J, 0, &text, &len))
            return false;
        if (!safe_name(text, len, true))
            return js_throw(J, JS_E_ERROR, "InvalidCharacterError: not a valid element name");
        n = dom_create_element(doc, text, len);
        break;
    case D_TEXTNODE: case D_COMMENT:
        if (!arg_text(J, 0, &text, &len))
            return false;
        n = kind == D_TEXTNODE ? dom_create_text(doc, text, len) : dom_create_comment(doc, text, len);
        break;
    case D_FRAG:
        n = dom_create_fragment(doc);
        break;
    case D_PARSE:
        if (!arg_text(J, 0, &text, &len))
            return false;
        n = dom_parse_fragment(doc, text, len);
        if (n)
            mark_done(s, n, false);
        break;
    case D_INSERT: {
        other = need_node(J, 1);
        if (!other)
            return false;
        dom_node *ref = js_arg(J, 2)->t <= JS_T_NULL ? NULL : need_node(J, 2);
        if (js_arg(J, 2)->t > JS_T_NULL && !ref)
            return false;
        if (!dom_insert(n, other, ref))
            return js_throw(J, JS_E_ERROR, "HierarchyRequestError: The node cannot be inserted here");
        return js_push(J, js_undef());
    }
    case D_REMOVE:
        dom_remove(n);
        return js_push(J, js_undef());
    case D_CLONE:
        n = dom_clone(n, js_truthy(js_arg(J, 1)));
        if (n)
            mark_done(s, n, true);
        break;
    case D_CONTAINS:
        other = node_of(js_arg(J, 1));
        return js_push(J, js_bool(other && dom_contains(n, other)));
    case D_QUERY: case D_MATCHES: case D_BYCLASS: {
        if (!arg_text(J, 1, &text, &len))
            return false;
        css_selector_t *sel = NULL;
        if (kind == D_BYCLASS) {
            /* "a b" -> ".a.b" */
            char *dotted = kmalloc(len * 2 + 2);
            size_t k = 0;
            bool start = true;
            for (size_t i = 0; dotted && i < len; i++) {
                if (blank(text[i])) {
                    start = true;
                    continue;
                }
                if (start)
                    dotted[k++] = '.';
                start = false;
                dotted[k++] = text[i];
            }
            if (dotted && k)
                sel = css_selector_parse(dotted, k);
            kfree(dotted);
            if (!sel) {
                js_push(J, js_obj(js_array_new(J, 0)));
                return true;
            }
        } else {
            sel = css_selector_parse(text, len);
            if (!sel)
                return js_throw(J, JS_E_SYNTAX, "'%s' is not a valid selector (or not supported)", text);
        }
        if (kind == D_MATCHES) {
            bool hit = n->type == DOM_ELEMENT && css_selector_matches(sel, &n->css);
            css_selector_free(sel);
            return js_push(J, js_bool(hit));
        }
        bool all = kind == D_BYCLASS || js_truthy(js_arg(J, 2));
        js_object *out = all ? js_array_new(J, 0) : NULL;
        if (out)
            js_push(J, js_obj(out));
        bool ok = true;
        dom_node *hit = NULL;
        for (dom_node *c = dom_next(n, n); c && ok; c = dom_next(n, c)) {
            if (c->type != DOM_ELEMENT || !css_selector_matches(sel, &c->css))
                continue;
            if (!all) {
                hit = c;
                break;
            }
            ok = push_node(J, c) && js_array_push(J, out, *js_top(J, 0));
            if (ok)
                js_pop(J, 1);
        }
        css_selector_free(sel);
        if (!ok)
            return false;
        return all ? true : push_node(J, hit);
    }
    case D_BYID:
        if (!arg_text(J, 1, &text, &len))
            return false;
        return push_node(J, len ? dom_find_id(n, text) : NULL);
    case D_BYTAG: {
        if (!lower_name(J, 1, name, sizeof(name)))
            return false;
        bool any = name[0] == '*' && !name[1];
        js_object *out = js_array_new(J, 0);
        js_push(J, js_obj(out));
        for (dom_node *c = dom_next(n, n); c; c = dom_next(n, c)) {
            if (c->type != DOM_ELEMENT || (!any && strcmp(c->name, name) != 0))
                continue;
            if (!push_node(J, c) || !js_array_push(J, out, *js_top(J, 0)))
                return false;
            js_pop(J, 1);
        }
        return true;
    }
    case D_VALUE:
        if (n->type != DOM_ELEMENT)
            return js_push(J, js_undef());
        text = dom_field_value(n, &len);
        return push_owned(J, (char *)text, len);
    case D_SETVALUE:
        if (!arg_text(J, 1, &text, &len))
            return false;
        if (n->type == DOM_ELEMENT && !dom_set_field_value(n, text, len))
            return js_fatal(J, "Die Seite ist für die Skripte zu groß geworden");
        return js_push(J, js_undef());
    case D_CHECKED:
        return js_push(J, js_bool(n->checked >= 0 ? n->checked == 1
                                                  : n->type == DOM_ELEMENT && dom_attr_get(n, "checked") != NULL));
    case D_SETCHECKED:
        n->checked = js_truthy(js_arg(J, 1)) ? 1 : 0;
        doc->dirty = true;
        return js_push(J, js_undef());
    case D_SELECTED:
        return js_push(J, js_num(selected_index(n)));
    case D_SETSELECTED: {
        int32_t index;
        if (!js_to_int32(J, ARGI(1), &index))
            return false;
        n->selected = index < -2 ? -1 : index;
        doc->dirty = true;
        return js_push(J, js_undef());
    }
    case D_STYLE: case D_DATASET: case D_COMPUTED: {
        js_object *o = js_object_new(J, JS_C_HOST, kind != D_DATASET && s->style_proto ? s->style_proto
                                                                                      : J->object_proto);
        if (o != J->dummy) {
            o->u.host.cls = kind == D_STYLE ? &style_class : kind == D_DATASET ? &dataset_class
                                                                                : &computed_class;
            o->u.host.ptr = n;
        }
        return js_push(J, js_obj(o));
    }
    case D_SHOWN:
        return js_push(J, js_bool(node_shown(s, n)));
    case D_URL:
        return js_push_cstr(J, s->url);
    case D_RESOLVE: {
        char *out = kmalloc(URL_SIZE);
        const char *base = s->url;
        if (!out || !arg_text(J, 0, &text, &len)) {
            kfree(out);
            return false;
        }
        if (argc > 1 && js_arg(J, 1)->t == JS_T_STR)
            base = js_arg(J, 1)->u.s->data;
        bool ok = url_resolve(base, text, out, URL_SIZE) ? js_push_cstr(J, out)
                                                         : js_push(J, *js_arg(J, 0));
        kfree(out);
        return ok;
    }
    case D_REQUEST:
        return request(J, s);
    case D_WISH: {
        int32_t what;
        if (!js_to_int32(J, ARGI(0), &what))
            return false;
        if (what == 1) {
            if (!arg_text(J, 1, &text, &len))
                return false;
            /* "javascript:..." ist kein Ziel; alles andere löst der Browser auf */
            if (len < URL_SIZE && !(len >= 11 && memcmp(text, "javascript:", 11) == 0))
                add_wish(s, SCRIPT_NAVIGATE, text, 0, 0);
        } else if (what == 2) {
            dom_node *form = need_node(J, 1), *button = node_of(js_arg(J, 2));
            if (!form)
                return false;
            add_wish(s, SCRIPT_SUBMIT, NULL, form->id, button ? button->id : 0);
        } else if (what == 3 || what == 4) {
            add_wish(s, what == 3 ? SCRIPT_BACK : SCRIPT_RELOAD, NULL, 0, 0);
        }
        return js_push(J, js_undef());
    }
    case D_ALERT: case D_CONFIRM: {
        if (!arg_text(J, 0, &text, &len))
            return false;
        bool yes = false;
        uint64_t before = time_ms();
        if (kind == D_ALERT && s->host.alert)
            s->host.alert(s->host.ctx, text);
        else if (kind == D_CONFIRM && s->host.confirm)
            yes = s->host.confirm(s->host.ctx, text);
        /* die Rückfrage hat gedauert: Die Zeit des Benutzers zählt nicht gegen das Skript. */
        uint64_t waited = time_ms() - before;
        if (J->deadline)
            J->deadline += waited;
        if (s->boot_until)
            s->boot_until += waited;
        return js_push(J, kind == D_ALERT ? js_undef() : js_bool(yes));
    }
    case D_WRITE: {
        if (!arg_text(J, 0, &text, &len))
            return false;
        dom_node *after = s->write_after;
        if (!s->booting || !after || !after->parent)
            return js_push(J, js_undef());
        dom_node *fragment = dom_parse_fragment(doc, text, len);
        if (!fragment)
            return js_fatal(J, "Die Seite ist für die Skripte zu groß geworden");
        dom_node *last = fragment->last;
        if (last && dom_insert(after->parent, fragment, after->next))
            s->write_after = last;
        return js_push(J, js_undef());
    }
    case D_SCRIPT:
        return push_node(J, s->current);
    case D_SIZE: {
        js_object *out = js_array_new(J, 2);
        js_push(J, js_obj(out));
        js_array_push(J, out, js_num(s->host.width > 0 ? s->host.width : 1024));
        js_array_push(J, out, js_num(s->host.height > 0 ? s->host.height : 768));
        return true;
    }
    case D_PROTOS: {
        static const char *const names[5] = { "element", "text", "comment", "fragment", "document" };
        js_value *table = js_arg(J, 0);
        if (table->t != JS_T_OBJ)
            return js_throw(J, JS_E_TYPE, "protos needs an object");
        for (int i = 0; i < 5; i++) {
            js_prop *p = js_own(table->u.o, js_intern_cstr(J, names[i]));
            s->protos[i] = p && p->v.t == JS_T_OBJ ? p->v.u.o : NULL;
        }
        js_prop *tags = js_own(table->u.o, js_intern_cstr(J, "tags"));
        s->tag_protos = tags && tags->v.t == JS_T_OBJ ? tags->v.u.o : NULL;
        js_define(J, J->registry, js_intern_cstr(J, "protos"), *table, JS_P_ALL);
        return js_push(J, js_undef());
    }
    case D_DISPATCHER:
        if (!js_is_callable(js_arg(J, 0)))
            return js_throw(J, JS_E_TYPE, "dispatcher needs a function");
        s->dispatcher = js_arg(J, 0)->u.o;
        js_define(J, J->registry, js_intern_cstr(J, "dispatcher"), *js_arg(J, 0), JS_P_ALL);
        return js_push(J, js_undef());
    default:
        return js_push(J, js_undef());
    }
    /* hierher kommen die Funktionen, die einen neuen Knoten liefern */
    if (!n)
        return js_fatal(J, "Die Seite ist für die Skripte zu groß geworden");
    return push_node(J, n);
}

static const struct {
    const char *name;
    uint8_t nargs;
    int magic;
} dom_functions[] = {
    { "doc", 0, D_DOC }, { "type", 1, D_TYPE }, { "name", 1, D_NAME }, { "parent", 1, D_PARENT },
    { "first", 1, D_FIRST }, { "last", 1, D_LAST }, { "next", 1, D_NEXT }, { "prev", 1, D_PREV },
    { "kids", 2, D_KIDS }, { "attr", 2, D_ATTR }, { "setAttr", 3, D_SETATTR }, { "delAttr", 2, D_DELATTR },
    { "attrs", 1, D_ATTRS }, { "text", 1, D_TEXT }, { "setText", 2, D_SETTEXT }, { "html", 2, D_HTML },
    { "setHtml", 2, D_SETHTML }, { "el", 1, D_EL }, { "textNode", 1, D_TEXTNODE },
    { "comment", 1, D_COMMENT }, { "frag", 0, D_FRAG }, { "parse", 1, D_PARSE }, { "insert", 3, D_INSERT },
    { "remove", 1, D_REMOVE }, { "clone", 2, D_CLONE }, { "contains", 2, D_CONTAINS },
    { "query", 3, D_QUERY }, { "matches", 2, D_MATCHES }, { "byId", 2, D_BYID }, { "byTag", 2, D_BYTAG },
    { "byClass", 2, D_BYCLASS }, { "value", 1, D_VALUE }, { "setValue", 2, D_SETVALUE },
    { "checked", 1, D_CHECKED }, { "setChecked", 2, D_SETCHECKED }, { "selected", 1, D_SELECTED },
    { "setSelected", 2, D_SETSELECTED }, { "style", 1, D_STYLE }, { "dataset", 1, D_DATASET },
    { "url", 0, D_URL }, { "resolve", 2, D_RESOLVE }, { "wish", 3, D_WISH }, { "alert", 1, D_ALERT },
    { "confirm", 1, D_CONFIRM }, { "write", 1, D_WRITE }, { "script", 0, D_SCRIPT }, { "size", 0, D_SIZE },
    { "protos", 1, D_PROTOS }, { "dispatcher", 1, D_DISPATCHER }, { "shown", 1, D_SHOWN },
    { "computed", 1, D_COMPUTED }, { "request", 6, D_REQUEST },
};

/* --- Anlegen ------------------------------------------------------------------------------------- */

static int64_t host_clock(void *ctx)
{
    (void)ctx;
    return (int64_t)time_epoch_ms();
}

static uint64_t host_ticks(void *ctx)
{
    (void)ctx;
    return time_ms();
}

/* Das Fenster wird geschlossen: Dann soll kein Skript der Seite mehr weiterlaufen. */
static bool host_cancelled(void *ctx)
{
    script_t *s = ctx;
    return s->host.cancelled && s->host.cancelled(s->host.ctx);
}

static void host_print(void *ctx, const char *text)
{
    script_t *s = ctx;
    if (s->host.log)
        s->host.log(s->host.ctx, text);
}

static js_object *module_load(js_state *J, void *ctx, js_string *referrer, js_string *specifier);

static void setup(void *arg)
{
    script_t *s = arg;
    js_host_t host = { .heap_max = HEAP_BYTES, .stack_bytes = STACK_BYTES - 96 * 1024,
                       .clock_ms = host_clock, .ticks_ms = host_ticks, .print = host_print,
                       .cancelled = host_cancelled, .ctx = s };
    js_state *J = js_create(&host);
    if (!J)
        return;
    J->user = s;
    js_object *dom = js_object_new(J, JS_C_OBJECT, J->object_proto);
    js_def_value(J, J->global, "__dom", js_obj(dom), JS_P_ALL);
    for (unsigned i = 0; i < sizeof(dom_functions) / sizeof(dom_functions[0]); i++)
        js_def_native(J, dom, dom_functions[i].name, dom_fn, dom_functions[i].nargs,
                      dom_functions[i].magic);
    s->style_proto = js_object_new(J, JS_C_OBJECT, J->object_proto);
    js_define(J, J->registry, js_intern_cstr(J, "style"), js_obj(s->style_proto), JS_P_ALL);
    js_def_native(J, s->style_proto, "getPropertyValue", style_method, 1, 0);
    js_def_native(J, s->style_proto, "setProperty", style_method, 2, 1);
    js_def_native(J, s->style_proto, "removeProperty", style_method, 1, 2);
    s->J = J;
    js_module_set_loader(J, module_load, s);
    if (!js_run(J, domlib_js, sizeof(domlib_js) - 1, "domlib", 0) || !s->dispatcher || !s->protos[0]) {
        js_destroy(J);
        s->J = NULL;
    }
}

script_t *script_create(const char *html, size_t len, const char *url, const script_host_t *host)
{
    script_t *s = kzalloc(sizeof(script_t));
    if (!s)
        return NULL;
    if (host)
        s->host = *host;
    strlcpy(s->url, url ? url : "", sizeof(s->url));
    s->stack = kmalloc(STACK_BYTES);
    s->doc = dom_parse(html, len);
    if (!s->stack || !s->doc || s->doc->failed) {
        script_destroy(s);
        return NULL;
    }
    call_on_stack(setup, s, (char *)s->stack + STACK_BYTES);
    if (!s->J) {
        script_destroy(s);
        return NULL;
    }
    return s;
}

void script_destroy(script_t *s)
{
    if (!s)
        return;
    js_destroy(s->J);
    dom_free(s->doc);
    kfree(s->stack);
    kfree(s->done);
    kfree(s);
}

/* --- Ausführen ------------------------------------------------------------------------------------ */

/* Ruft die Ereignisverteilung in domlib.js; true, wenn die übliche Wirkung eintreten darf */
static bool fire(script_t *s, dom_node *target, const char *type, dom_node *detail, const char *info)
{
    js_state *J = s->J;
    if (js_dead(J) || !s->dispatcher)
        return true;
    int base = J->top;
    js_enter(J, s->host.budget_ms ? s->host.budget_ms : EVENT_MS);
    js_push(J, js_obj(s->dispatcher));
    js_push(J, js_undef());
    bool ok = target ? push_node(J, target) : js_push(J, js_null());
    ok = ok && js_push_cstr(J, type) && (detail ? push_node(J, detail) : js_push(J, js_null()));
    ok = ok && (info ? js_push_cstr(J, info) : js_push(J, js_null()));
    ok = ok && js_call(J, 4);
    bool allowed = ok ? js_truthy(js_top(J, 0)) : true;
    J->top = base;
    if (ok)
        js_jobs_run(J);
    J->top = base;
    js_leave(J, ok);
    return allowed;
}

static bool javascript_type(const char *type)
{
    static const char *const known[] = { "", "text/javascript", "application/javascript",
                                         "text/ecmascript", "application/ecmascript",
                                         "application/x-javascript", "text/jscript", NULL };
    char folded[64];
    size_t n = 0;
    while (*type == ' ')
        type++;
    for (; type[n] && type[n] != ';' && type[n] != ' ' && n < sizeof(folded) - 1; n++)
        folded[n] = type[n] >= 'A' && type[n] <= 'Z' ? (char)(type[n] + 32) : type[n];
    folded[n] = '\0';
    for (int i = 0; known[i]; i++) {
        if (strcmp(folded, known[i]) == 0)
            return true;
    }
    return false;
}

/* Holt eine Skriptdatei (oder NULL). Das Warten aufs Netz zählt nicht als Rechenzeit - hat aber seine eigene Grenze. */
static char *fetch_script(script_t *s, const char *address, size_t *len)
{
    if (!s->host.fetch || s->fetch_ms >= s->fetch_limit)
        return NULL;
    uint64_t before = time_ms();
    char *code = s->host.fetch(s->host.ctx, address, !s->host.private_page, len);
    uint64_t waited = time_ms() - before;
    s->fetch_ms += waited;
    if (s->boot_until)
        s->boot_until += waited;
    if (code && *len > SCRIPT_BYTES) {
        kfree(code);
        code = NULL;
    }
    return code;
}

static uint32_t script_budget(const script_t *s)
{
    uint32_t budget = s->host.budget_ms ? s->host.budget_ms : SCRIPT_MS;
    if (s->boot_until) {
        uint64_t now = time_ms();
        uint64_t left = s->boot_until > now ? s->boot_until - now : 1;
        if (left < budget)
            budget = (uint32_t)left;
    }
    return budget;
}

/* --- Module ---------------------------------------------------------------------------------------- */

static js_object *registry_object(js_state *J, const char *name)
{
    js_string *key = js_intern_cstr(J, name);
    js_prop *p = js_own(J->registry, key);
    if (p && p->v.t == JS_T_OBJ)
        return p->v.u.o;
    js_object *o = js_object_new(J, JS_C_OBJECT, NULL);
    js_define(J, J->registry, key, js_obj(o), JS_P_ALL);
    return o;
}

/*
 * Die Übersetzungstabelle der Seite (<script type="importmap">): Steht die
 * Angabe dort - ganz oder mit einem Anfang, der auf "/" endet -, kommt die
 * hinterlegte Adresse heraus.
 */
static bool import_map(js_state *J, const char *spec, char *out, size_t size)
{
    js_prop *map = js_own(J->registry, js_intern_cstr(J, "importmap"));
    js_prop *list = map && map->v.t == JS_T_OBJ ? js_own(map->v.u.o, js_intern_cstr(J, "imports")) : NULL;
    if (!list || list->v.t != JS_T_OBJ)
        return false;
    js_object *imports = list->v.u.o;
    size_t n = strlen(spec), best = 0;
    const js_string *target = NULL;
    for (uint32_t i = 0; i < imports->count; i++) {
        const js_prop *p = &imports->props[i];
        if (!p->key || p->key->symbol || p->v.t != JS_T_STR || p->key->len == 0)
            continue;
        const js_string *k = p->key;
        bool whole = k->len == n && memcmp(k->data, spec, n) == 0;
        bool prefix = k->data[k->len - 1] == '/' && k->len < n && memcmp(k->data, spec, k->len) == 0;
        if ((whole || prefix) && k->len > best) {
            best = k->len;
            target = p->v.u.s;
        }
    }
    if (!target || target->len + (n - best) + 1 > size)
        return false;
    memcpy(out, target->data, target->len);
    memcpy(out + target->len, spec + best, n - best + 1);
    return true;
}

/* Für die Maschine: macht aus der Angabe hinter "from" (oder in import(...)) ein Modul. */
static js_object *module_load(js_state *J, void *ctx, js_string *referrer, js_string *specifier)
{
    script_t *s = ctx;
    char *address = kmalloc(URL_SIZE), *mapped = kmalloc(URL_SIZE);
    js_object *record = NULL;
    const char *spec = specifier->data;
    if (!address || !mapped) {
        js_fatal(J, "Nicht genug Speicher für das Skript");
        goto out;
    }
    bool translated = import_map(J, spec, mapped, URL_SIZE);
    if (translated)
        spec = mapped;
    bool local = spec[0] == '/' || (spec[0] == '.' && (spec[1] == '/' || (spec[1] == '.' && spec[2] == '/')));
    size_t spec_len = strlen(spec);
    bool full = (spec_len > 7 && memcmp(spec, "http://", 7) == 0) ||
                (spec_len > 8 && memcmp(spec, "https://", 8) == 0);
    if (!local && !full) {
        js_throw(J, JS_E_TYPE, "Failed to resolve module specifier \"%s\"", specifier->data);
        goto out;
    }
    /* Angaben aus der Tabelle gelten von der Seite aus, alle anderen vom fragenden Modul aus */
    if (!url_resolve(referrer && !translated ? referrer->data : s->url, spec, address, URL_SIZE)) {
        js_throw(J, JS_E_TYPE, "Failed to resolve module specifier \"%s\"", specifier->data);
        goto out;
    }
    js_object *map = registry_object(J, "modules");
    js_string *key = js_intern_cstr(J, address);
    js_prop *known = js_own(map, key);
    if (known && known->v.t == JS_T_OBJ) {
        record = known->v.u.o;
        goto out;
    }
    size_t len = 0;
    char *code = fetch_script(s, address, &len);
    if (!code) {
        js_throw(J, JS_E_TYPE, "Failed to fetch module %s", address);
        goto out;
    }
    js_push(J, js_str(key));
    record = js_module_create(J, code, len, key);
    kfree(code);
    if (record)
        js_define(J, map, key, js_obj(record), JS_P_ALL);
    js_pop(J, 1);
out:
    kfree(address);
    kfree(mapped);
    return record;
}

/* <script type="importmap">: als JSON lesen und hinterlegen (nur die erste zählt) */
static void read_import_map(script_t *s, dom_node *script)
{
    js_state *J = s->J;
    js_string *name = js_intern_cstr(J, "importmap");
    size_t len = 0;
    char *text = js_own(J->registry, name) ? NULL : dom_text_content(script, &len);
    if (!text)
        return;
    int base = J->top;
    js_enter(J, EVENT_MS);
    js_push(J, js_obj(J->global));
    bool ok = js_get_cstr(J, J->top - 1, "JSON") && js_get_cstr(J, J->top - 1, "parse");
    if (ok) {
        js_push(J, js_undef());
        js_push_lstr(J, text, len);
        ok = js_call(J, 1);
    }
    if (ok && js_top(J, 0)->t == JS_T_OBJ)
        js_define(J, J->registry, name, *js_top(J, 0), JS_P_ALL);
    J->top = base;
    js_leave(J, ok);
    kfree(text);
}

/* --- Skripte der Seite ausführen ---------------------------------------------------------------------- */

/* Führt ein <script> aus: als gewöhnliches Skript oder als Modul. */
static void run_one(script_t *s, dom_node *script, bool module)
{
    js_state *J = s->J;
    const char *src = dom_attr_get(script, "src");
    char *code = NULL, *address = NULL;
    size_t len = 0;
    if (s->ran >= (s->booting ? BOOT_SCRIPTS : LATE_SCRIPTS) ||
        (s->boot_until && time_ms() > s->boot_until)) {
        s->skipped++;
        return;
    }
    if (src) {
        address = kmalloc(URL_SIZE);
        if (!address || !url_resolve(s->url, src, address, URL_SIZE)) {
            kfree(address);
            s->failed++;
            return;
        }
    }
    if (module) {
        /* ein Modul: Es holt sich, was es braucht, selbst (module_load) */
        js_object *record = NULL;
        int base = J->top;
        js_enter(J, script_budget(s));
        if (src) {
            record = module_load(J, s, NULL, js_intern_cstr(J, address));
        } else {
            code = dom_text_content(script, &len);
            if (code && len <= SCRIPT_BYTES)
                record = js_module_create(J, code, len, js_intern_cstr(J, s->url));
            kfree(code);
        }
        if (record)
            js_push(J, js_obj(record));
        else
            js_leave(J, false);         /* die Fehlermeldung ausgeben */
        bool ok = record && js_module_run(J, record, script_budget(s));
        J->top = base;
        s->ran++;
        if (!ok)
            s->failed++;
        if (src && !js_dead(J))
            fire(s, script, ok ? "load" : "error", NULL, NULL);
        kfree(address);
        return;
    }
    if (src) {
        code = fetch_script(s, address, &len);
        if (!code) {
            s->failed++;
            kfree(address);
            s->current = script;
            fire(s, script, "error", NULL, NULL);
            s->current = NULL;
            return;
        }
    } else {
        code = dom_text_content(script, &len);
        if (!code)
            return;
        if (len > SCRIPT_BYTES) {
            s->skipped++;
            kfree(code);
            return;
        }
    }
    s->current = script;
    s->write_after = script;
    bool ok = js_run(J, code, len, address ? address : "inline", script_budget(s));
    s->ran++;
    if (!ok)
        s->failed++;
    kfree(code);
    kfree(address);
    if (src && !js_dead(J))
        fire(s, script, ok ? "load" : "error", NULL, NULL);
    s->current = NULL;
    s->write_after = NULL;
}

static bool same_word(const char *a, const char *b)
{
    while (*a == ' ')
        a++;
    for (; *b; a++, b++) {
        if ((*a >= 'A' && *a <= 'Z' ? *a + 32 : *a) != *b)
            return false;
    }
    return *a == '\0' || *a == ' ' || *a == ';';
}

/*
 * Führt alle Skripte der Seite aus, die noch nicht dran waren - auch solche,
 * die dabei entstehen: erst die gewöhnlichen in der Reihenfolge der Seite,
 * dann die aufgeschobenen (Module, "defer").
 */
static void run_scripts(script_t *s)
{
    js_state *J = s->J;
    static const char *const never[] = { "noscript", "template", "textarea", NULL };
    for (int guard = 0; guard < 600 && !js_dead(J); guard++) {
        dom_node *script = NULL;
        for (dom_node *n = dom_next(s->doc->root, s->doc->root); n; n = dom_next(s->doc->root, n)) {
            if (dom_is(n, "script") && !is_done(s, n)) {
                script = n;
                break;
            }
        }
        if (!script) {
            if (s->deferred_count == 0)
                return;
            uint32_t id = s->deferred[0].node;
            bool module = s->deferred[0].module;
            s->deferred_count--;
            memmove(s->deferred, s->deferred + 1, (size_t)s->deferred_count * sizeof(s->deferred[0]));
            dom_node *waiting = dom_node_by_id(s->doc, id);
            if (waiting)
                run_one(s, waiting, module);
            continue;
        }
        mark_done(s, script, true);
        bool inert = false;
        for (dom_node *p = script->parent; p && !inert; p = p->parent) {
            for (int i = 0; never[i]; i++)
                inert |= dom_is(p, never[i]);
        }
        const char *type = dom_attr_get(script, "type"), *src = dom_attr_get(script, "src");
        if (inert)
            continue;
        bool module = type && same_word(type, "module");
        if (type && same_word(type, "importmap")) {
            read_import_map(s, script);
            continue;
        }
        if (type && !module && !javascript_type(type))
            continue;                   /* Daten und Vorlagen in <script>: nichts zum Ausführen */
        if (!module && dom_attr_get(script, "nomodule"))
            continue;                   /* die Ersatzfassung für Browser ohne Module */
        bool later = module || (s->booting && src && dom_attr_get(script, "defer") &&
                                !dom_attr_get(script, "async"));
        if (later && s->deferred_count < DEFERRED_MAX) {
            s->deferred[s->deferred_count].node = script->id;
            s->deferred[s->deferred_count].module = module;
            s->deferred_count++;
            continue;
        }
        run_one(s, script, module);
    }
}

typedef struct {
    script_t *s;
    int op;
    uint32_t node, button;
    const char *value, *type;
    int checked, selected;
    bool finished, result;
    int64_t wait;
} call_t;

enum { OP_BOOT, OP_CLICK, OP_SUBMIT, OP_FIELD, OP_TICK, OP_EVENT, OP_EVAL };

static void entry(void *arg)
{
    call_t *c = arg;
    script_t *s = c->s;
    js_state *J = s->J;
    dom_node *n = dom_node_by_id(s->doc, c->node);
    if (c->op != OP_BOOT) {
        /* nach dem Laden: Fordert die Seite Skriptdateien nach, hat jedes Ereignis seine eigene Geduld */
        s->fetch_ms = 0;
        s->fetch_limit = LATE_FETCH_MS;
    }
    switch (c->op) {
    case OP_BOOT:
        s->booting = true;
        s->boot_until = time_ms() + BOOT_MS;
        s->fetch_limit = FETCH_MS;
        run_scripts(s);
        s->booting = false;
        s->boot_until = 0;
        if (!js_dead(J)) {
            /* (auch was Skripte jetzt noch an Skriptdateien nachfordern, wird geholt) */
            fire(s, s->doc->root, "DOMContentLoaded", NULL, NULL);
            run_scripts(s);
            fire(s, NULL, "load", NULL, NULL);
            run_scripts(s);
            /* was sich Skripte für "sofort" vorgenommen haben, gehört noch zum Laden */
            js_timers_run(J, EVENT_MS);
            run_scripts(s);
        }
        break;
    case OP_CLICK:
        c->result = !n || fire(s, n, "click", NULL, NULL);
        run_scripts(s);
        break;
    case OP_SUBMIT:
        c->result = !n || fire(s, n, "submit", dom_node_by_id(s->doc, c->button), NULL);
        run_scripts(s);
        break;
    case OP_FIELD: {
        if (!n || n->type != DOM_ELEMENT)
            break;
        /* Der Stand kommt von der Anzeige: Das allein ist kein Grund, sie neu zu setzen. */
        bool dirty = s->doc->dirty;
        bool set = c->value || c->checked >= 0 || c->selected >= -1;
        if (c->value)
            dom_set_field_value(n, c->value, strlen(c->value));
        if (c->checked >= 0)
            n->checked = (int8_t)(c->checked ? 1 : 0);
        if (c->selected >= -1)
            n->selected = c->selected;
        s->doc->dirty = dirty;
        if (set)
            fire(s, n, "input", NULL, NULL);
        if (c->finished)
            fire(s, n, "change", NULL, NULL);
        run_scripts(s);
        break;
    }
    case OP_EVENT:
        c->result = !n || fire(s, n, c->type, NULL, c->value);
        run_scripts(s);
        break;
    case OP_EVAL:
        if (c->value)
            js_run(J, c->value, strlen(c->value), "javascript:", s->host.budget_ms ? s->host.budget_ms : EVENT_MS);
        run_scripts(s);
        break;
    case OP_TICK:
        c->result = js_timers_run(J, EVENT_MS);
        if (c->result)
            run_scripts(s);
        c->wait = js_timers_due(J);
        break;
    default:
        break;
    }
}

static void run(script_t *s, call_t *c)
{
    c->s = s;
    if (s && s->J && !js_dead(s->J))
        call_on_stack(entry, c, (char *)s->stack + STACK_BYTES);
}

void script_boot(script_t *s)
{
    call_t c = { .op = OP_BOOT };
    run(s, &c);
}

bool script_click(script_t *s, uint32_t node)
{
    call_t c = { .op = OP_CLICK, .node = node, .result = true };
    run(s, &c);
    return c.result;
}

bool script_submit(script_t *s, uint32_t form, uint32_t button)
{
    call_t c = { .op = OP_SUBMIT, .node = form, .button = button, .result = true };
    run(s, &c);
    return c.result;
}

void script_field(script_t *s, uint32_t node, const char *value, int checked, int selected,
                  bool finished)
{
    call_t c = { .op = OP_FIELD, .node = node, .value = value, .checked = checked,
                 .selected = selected, .finished = finished };
    run(s, &c);
}

bool script_event(script_t *s, uint32_t node, const char *type, const char *info)
{
    call_t c = { .op = OP_EVENT, .node = node, .type = type, .value = info, .result = true };
    run(s, &c);
    return c.result;
}

void script_eval(script_t *s, const char *code)
{
    call_t c = { .op = OP_EVAL, .value = code };
    run(s, &c);
}

void script_set_styles(script_t *s, const struct css_sheet *css)
{
    if (s)
        s->css = css;
}

bool script_stopped(const script_t *s)
{
    return s && s->J && js_dead(s->J);
}

bool script_tick(script_t *s, int64_t *wait_ms)
{
    call_t c = { .op = OP_TICK, .wait = -1 };
    run(s, &c);
    if (wait_ms)
        *wait_ms = c.wait;
    return c.result;
}

bool script_changed(const script_t *s)
{
    return s && s->doc->dirty;
}

const struct dom_doc *script_document(const script_t *s)
{
    return s ? s->doc : NULL;
}

char *script_html(script_t *s, size_t *len)
{
    return dom_serialize(s->doc, len);
}

script_wish_t script_wish(script_t *s, char *url, size_t url_size, uint32_t *node, uint32_t *button)
{
    if (!s || s->wish_count == 0)
        return SCRIPT_NOTHING;
    script_wish_t kind = s->wishes[0].kind;
    if (url && url_size)
        strlcpy(url, s->wishes[0].url, url_size);
    if (node)
        *node = s->wishes[0].node;
    if (button)
        *button = s->wishes[0].button;
    memmove(&s->wishes[0], &s->wishes[1], sizeof(s->wishes[0]) * (WISHES - 1));
    s->wish_count--;
    return kind;
}

void script_counts(const script_t *s, int *ran, int *failed, int *skipped)
{
    *ran = s ? s->ran : 0;
    *failed = s ? s->failed : 0;
    *skipped = s ? s->skipped : 0;
}
