/* Das veränderbare Abbild einer Webseite (DOM) - siehe dom.h */
#include "dom.h"
#include "html.h"
#include "memory.h"
#include "string.h"

#define NODES_MAX   400000
#define BYTES_MAX   (96u * 1024 * 1024)
#define DEPTH_MAX   400                 /* tiefer wird nicht verschachtelt */
#define ATTRS_MAX   200

/* --- Speicher ------------------------------------------------------------------------------- */

static void *take(dom_doc *doc, size_t size)
{
    if (doc->failed || size > BYTES_MAX || doc->bytes + size > BYTES_MAX) {
        doc->failed = true;
        return NULL;
    }
    void *p = kzalloc(size ? size : 1);
    if (!p) {
        doc->failed = true;
        return NULL;
    }
    doc->bytes += size;
    return p;
}

static void give(dom_doc *doc, void *p, size_t size)
{
    if (!p)
        return;
    kfree(p);
    doc->bytes -= size <= doc->bytes ? size : doc->bytes;
}

static char *copy_text(dom_doc *doc, const char *text, size_t len)
{
    char *copy = take(doc, len + 1);
    if (copy && len)
        memcpy(copy, text, len);
    return copy;
}

static char lower(char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c;
}

static bool same(const char *a, const char *b)
{
    return strcmp(a, b) == 0;
}

static bool one_of(const char *name, const char *const *list)
{
    for (; *list; list++) {
        if (same(name, *list))
            return true;
    }
    return false;
}

bool dom_is(const dom_node *node, const char *name)
{
    return node && node->type == DOM_ELEMENT && same(node->name, name);
}

/* --- Knoten ---------------------------------------------------------------------------------- */

static const char *css_attr(const css_node_t *css, const char *name, size_t name_len,
                            size_t *value_len)
{
    const dom_node *node = (const dom_node *)css;
    for (int i = 0; i < node->attr_count; i++) {
        const char *have = node->attrs[i].name;
        size_t k = 0;
        while (k < name_len && have[k] && have[k] == lower(name[k]))
            k++;
        if (k == name_len && !have[k]) {
            *value_len = strlen(node->attrs[i].value);
            return node->attrs[i].value;
        }
    }
    return NULL;
}

/* Hält fest, woran Auswahlregeln das Element erkennen */
static void refresh_css(dom_node *node)
{
    if (node->type != DOM_ELEMENT)
        return;
    const char *id = dom_attr_get(node, "id"), *classes = dom_attr_get(node, "class");
    node->css.tag = node->name;
    node->css.tag_len = strlen(node->name);
    node->css.id = id;
    node->css.id_len = id ? strlen(id) : 0;
    node->css.classes = classes;
    node->css.classes_len = classes ? strlen(classes) : 0;
    node->css.attr = css_attr;
    node->css.parent = node->parent && node->parent->type == DOM_ELEMENT ? &node->parent->css : NULL;
}

static dom_node *node_new(dom_doc *doc, int type)
{
    if (doc->count >= NODES_MAX) {
        doc->failed = true;
        return NULL;
    }
    if (doc->count == doc->cap) {
        uint32_t cap = doc->cap ? doc->cap * 2 : 256;
        dom_node **bigger = take(doc, (size_t)cap * sizeof(dom_node *));
        if (!bigger)
            return NULL;
        if (doc->count)
            memcpy(bigger, doc->nodes, (size_t)doc->count * sizeof(dom_node *));
        give(doc, doc->nodes, (size_t)doc->cap * sizeof(dom_node *));
        doc->nodes = bigger;
        doc->cap = cap;
    }
    dom_node *n = take(doc, sizeof(dom_node));
    if (!n)
        return NULL;
    n->type = (uint8_t)type;
    n->doc = doc;
    n->checked = -1;
    n->selected = -2;
    doc->nodes[doc->count++] = n;
    n->id = doc->count;                 /* Nummern beginnen bei 1 */
    return n;
}

/* Das Abbild hat sich geändert: Was daraus angezeigt und gemessen wurde, ist überholt. */
static void touch(dom_doc *doc)
{
    doc->dirty = true;
    doc->version++;
}

dom_node *dom_node_by_id(const dom_doc *doc, uint32_t id)
{
    return doc && id >= 1 && id <= doc->count ? doc->nodes[id - 1] : NULL;
}

dom_node *dom_create_element(dom_doc *doc, const char *name, size_t name_len)
{
    if (name_len == 0 || name_len > 200)
        return NULL;
    dom_node *n = node_new(doc, DOM_ELEMENT);
    if (!n)
        return NULL;
    n->name = copy_text(doc, name, name_len);
    if (!n->name)
        return NULL;
    for (size_t i = 0; i < name_len; i++)
        n->name[i] = lower(n->name[i]);
    refresh_css(n);
    return n;
}

static dom_node *text_node(dom_doc *doc, int type, const char *text, size_t len)
{
    if (len > 0xFFFFFF00u)
        return NULL;
    dom_node *n = node_new(doc, type);
    if (!n)
        return NULL;
    n->text = copy_text(doc, text, len);
    if (!n->text)
        return NULL;
    n->text_len = (uint32_t)len;
    return n;
}

dom_node *dom_create_text(dom_doc *doc, const char *text, size_t len)
{
    return text_node(doc, DOM_TEXT, text, len);
}

dom_node *dom_create_comment(dom_doc *doc, const char *text, size_t len)
{
    return text_node(doc, DOM_COMMENT, text, len);
}

dom_node *dom_create_fragment(dom_doc *doc)
{
    return node_new(doc, DOM_FRAGMENT);
}

bool dom_contains(const dom_node *ancestor, const dom_node *node)
{
    for (int guard = 0; node && guard < 100000; guard++, node = node->parent) {
        if (node == ancestor)
            return true;
    }
    return false;
}

void dom_remove(dom_node *child)
{
    dom_node *parent = child->parent;
    if (!parent)
        return;
    if (child->prev)
        child->prev->next = child->next;
    else
        parent->first = child->next;
    if (child->next)
        child->next->prev = child->prev;
    else
        parent->last = child->prev;
    child->parent = child->prev = child->next = NULL;
    refresh_css(child);
    touch(child->doc);
}

static void link_before(dom_node *parent, dom_node *child, dom_node *ref)
{
    child->parent = parent;
    child->next = ref;
    child->prev = ref ? ref->prev : parent->last;
    if (child->prev)
        child->prev->next = child;
    else
        parent->first = child;
    if (ref)
        ref->prev = child;
    else
        parent->last = child;
    refresh_css(child);
}

bool dom_insert(dom_node *parent, dom_node *child, dom_node *ref)
{
    if (!parent || !child || child->type == DOM_DOCUMENT || parent->type == DOM_TEXT ||
        parent->type == DOM_COMMENT || (ref && ref->parent != parent) ||
        dom_contains(child, parent))
        return false;
    if (child->type == DOM_FRAGMENT) {
        while (child->first) {
            dom_node *item = child->first;
            dom_remove(item);
            link_before(parent, item, ref);
        }
    } else {
        if (ref == child)
            ref = child->next;
        dom_remove(child);
        link_before(parent, child, ref);
    }
    touch(parent->doc);
    return true;
}

/* Der nächste Knoten in Dokumentreihenfolge innerhalb von root; NULL am Ende */
dom_node *dom_next(const dom_node *root, const dom_node *node)
{
    if (node->first)
        return node->first;
    while (node && node != root) {
        if (node->next)
            return node->next;
        node = node->parent;
    }
    return NULL;
}

dom_node *dom_find_id(const dom_node *root, const char *id)
{
    for (dom_node *n = dom_next(root, root); n; n = dom_next(root, n)) {
        if (n->type == DOM_ELEMENT && n->css.id && same(n->css.id, id))
            return n;
    }
    return NULL;
}

/* --- Attribute ------------------------------------------------------------------------------- */

static dom_attr *find_attr(const dom_node *node, const char *name)
{
    for (int i = 0; i < node->attr_count; i++) {
        if (same(node->attrs[i].name, name))
            return &node->attrs[i];
    }
    return NULL;
}

const char *dom_attr_get(const dom_node *node, const char *name)
{
    dom_attr *a = node && node->type == DOM_ELEMENT ? find_attr(node, name) : NULL;
    return a ? a->value : NULL;
}

static bool attr_add(dom_node *node, const char *name, size_t name_len, const char *value,
                     size_t value_len)
{
    dom_doc *doc = node->doc;
    if (node->attr_count >= ATTRS_MAX || name_len == 0 || name_len > 200)
        return false;
    if (node->attr_count == node->attr_cap) {
        uint16_t cap = node->attr_cap ? (uint16_t)(node->attr_cap * 2) : 4;
        dom_attr *bigger = take(doc, (size_t)cap * sizeof(dom_attr));
        if (!bigger)
            return false;
        if (node->attr_count)
            memcpy(bigger, node->attrs, (size_t)node->attr_count * sizeof(dom_attr));
        give(doc, node->attrs, (size_t)node->attr_cap * sizeof(dom_attr));
        node->attrs = bigger;
        node->attr_cap = cap;
    }
    char *n = copy_text(doc, name, name_len), *v = copy_text(doc, value, value_len);
    if (!n || !v) {
        give(doc, n, name_len + 1);
        give(doc, v, value_len + 1);
        return false;
    }
    for (size_t i = 0; i < name_len; i++)
        n[i] = lower(n[i]);
    node->attrs[node->attr_count].name = n;
    node->attrs[node->attr_count].value = v;
    node->attr_count++;
    return true;
}

bool dom_attr_set(dom_node *node, const char *name, const char *value, size_t value_len)
{
    if (!node || node->type != DOM_ELEMENT)
        return false;
    dom_doc *doc = node->doc;
    char folded[201];
    size_t n = strlen(name);
    if (n == 0 || n > 200)
        return false;
    for (size_t i = 0; i <= n; i++)
        folded[i] = lower(name[i]);
    dom_attr *a = find_attr(node, folded);
    if (a) {
        char *v = copy_text(doc, value, value_len);
        if (!v)
            return false;
        give(doc, a->value, strlen(a->value) + 1);
        a->value = v;
    } else if (!attr_add(node, folded, n, value, value_len)) {
        return false;
    }
    refresh_css(node);
    touch(doc);
    return true;
}

void dom_attr_remove(dom_node *node, const char *name)
{
    if (!node || node->type != DOM_ELEMENT)
        return;
    char folded[201];
    size_t n = strlen(name);
    if (n == 0 || n > 200)
        return;
    for (size_t i = 0; i <= n; i++)
        folded[i] = lower(name[i]);
    dom_attr *a = find_attr(node, folded);
    if (!a)
        return;
    give(node->doc, a->name, strlen(a->name) + 1);
    give(node->doc, a->value, strlen(a->value) + 1);
    *a = node->attrs[--node->attr_count];
    refresh_css(node);
    touch(node->doc);
}

/* --- Ausgabe ---------------------------------------------------------------------------------- */

typedef struct {
    char *text;
    size_t len, cap;
    bool failed;
} out_t;

static void put(out_t *out, const char *text, size_t n)
{
    if (out->failed)
        return;
    if (out->len + n + 1 > out->cap) {
        size_t cap = out->cap ? out->cap * 2 : 1024;
        while (cap < out->len + n + 1)
            cap *= 2;
        char *bigger = cap <= BYTES_MAX ? kmalloc(cap) : NULL;
        if (!bigger) {
            out->failed = true;
            return;
        }
        if (out->len)
            memcpy(bigger, out->text, out->len);
        kfree(out->text);
        out->text = bigger;
        out->cap = cap;
    }
    memcpy(out->text + out->len, text, n);
    out->len += n;
}

static void put_str(out_t *out, const char *text)
{
    put(out, text, strlen(text));
}

static void put_escaped(out_t *out, const char *text, size_t n, bool attribute)
{
    size_t start = 0;
    for (size_t i = 0; i < n; i++) {
        const char *with = NULL;
        char c = text[i];
        if (c == '&') with = "&amp;";
        else if (c == '<' && !attribute) with = "&lt;";
        else if (c == '>' && !attribute) with = "&gt;";
        else if (c == '"' && attribute) with = "&quot;";
        if (!with)
            continue;
        put(out, text + start, i - start);
        put_str(out, with);
        start = i + 1;
    }
    put(out, text + start, n - start);
}

static const char *const void_tags[] = { "br", "hr", "img", "input", "meta", "link", "area", "base",
                                         "col", "embed", "source", "track", "wbr", "param", NULL };
static const char *const raw_tags[] = { "script", "style", NULL };

static void put_number(out_t *out, uint32_t n)
{
    char digits[12];
    int k = 0;
    do {
        digits[k++] = (char)('0' + n % 10);
        n /= 10;
    } while (n);
    while (k > 0)
        put(out, &digits[--k], 1);
}

char *dom_text_content(const dom_node *node, size_t *len)
{
    out_t out = { NULL, 0, 0, false };
    if (node->type == DOM_TEXT || node->type == DOM_COMMENT) {
        put(&out, node->text, node->text_len);
    } else {
        for (dom_node *n = dom_next(node, node); n; n = dom_next(node, n)) {
            if (n->type == DOM_TEXT)
                put(&out, n->text, n->text_len);
        }
    }
    put(&out, "", 0);
    if (out.failed || !out.text) {
        kfree(out.text);
        return NULL;
    }
    out.text[out.len] = '\0';
    *len = out.len;
    return out.text;
}

char *dom_field_value(const dom_node *node, size_t *len)
{
    if (node->has_value || dom_is(node, "input") || dom_is(node, "option") || dom_is(node, "button")) {
        const char *v = node->has_value ? node->value : dom_attr_get(node, "value");
        if (!v && dom_is(node, "option"))
            return dom_text_content(node, len);
        if (!v)
            v = "";
        size_t n = strlen(v);
        char *copy = kmalloc(n + 1);
        if (copy)
            memcpy(copy, v, n + 1);
        *len = n;
        return copy;
    }
    if (dom_is(node, "select")) {
        /* der Wert des gewählten Eintrags */
        int index = 0, wanted = node->selected;
        dom_node *first = NULL, *chosen = NULL;
        for (dom_node *n = dom_next(node, node); n; n = dom_next(node, n)) {
            if (!dom_is(n, "option"))
                continue;
            if (!first)
                first = n;
            if (wanted == -2 ? dom_attr_get(n, "selected") != NULL && !chosen : index == wanted)
                chosen = n;
            index++;
        }
        if (!chosen && wanted == -2)
            chosen = first;
        if (chosen)
            return dom_field_value(chosen, len);
        char *empty = kzalloc(1);
        *len = 0;
        return empty;
    }
    return dom_text_content(node, len);     /* textarea */
}

bool dom_set_field_value(dom_node *node, const char *text, size_t len)
{
    dom_doc *doc = node->doc;
    if (dom_is(node, "select")) {
        int index = 0;
        node->selected = -1;
        for (dom_node *n = dom_next(node, node); n; n = dom_next(node, n)) {
            if (!dom_is(n, "option"))
                continue;
            size_t have;
            char *v = dom_field_value(n, &have);
            bool match = v && have == len && memcmp(v, text, len) == 0;
            kfree(v);
            if (match) {
                node->selected = index;
                break;
            }
            index++;
        }
        touch(doc);
        return true;
    }
    char *copy = copy_text(doc, text, len);
    if (!copy)
        return false;
    if (node->value)
        give(doc, node->value, strlen(node->value) + 1);
    node->value = copy;
    node->has_value = true;
    touch(doc);
    return true;
}

/* Die Nummer eines Eintrags in seiner Auswahlliste; -1, wenn er in keiner steht */
static int option_index(const dom_node *option, const dom_node **select)
{
    const dom_node *list = option->parent;
    while (list && !dom_is(list, "select"))
        list = list->parent;
    if (!list)
        return -1;
    *select = list;
    int index = 0;
    for (dom_node *n = dom_next(list, list); n; n = dom_next(list, n)) {
        if (n == option)
            return index;
        if (dom_is(n, "option"))
            index++;
    }
    return -1;
}

static void open_tag(out_t *out, const dom_node *node, bool layout)
{
    put(out, "<", 1);
    put_str(out, node->name);
    if (layout) {
        put_str(out, " data-hx=");
        put_number(out, node->id);
    }
    bool input = dom_is(node, "input"), option = dom_is(node, "option");
    const dom_node *select = NULL;
    int index = layout && option ? option_index(node, &select) : -1;
    bool forced = index >= 0 && select->selected != -2;
    for (int i = 0; i < node->attr_count; i++) {
        const dom_attr *a = &node->attrs[i];
        if (layout) {
            if (same(a->name, "data-hx"))
                continue;
            if (input && node->has_value && same(a->name, "value"))
                continue;
            if (input && node->checked >= 0 && same(a->name, "checked"))
                continue;
            if (forced && same(a->name, "selected"))
                continue;
        }
        put(out, " ", 1);
        put_str(out, a->name);
        if (a->value[0]) {
            put(out, "=\"", 2);
            put_escaped(out, a->value, strlen(a->value), true);
            put(out, "\"", 1);
        }
    }
    if (layout && input && node->has_value) {
        put_str(out, " value=\"");
        put_escaped(out, node->value, strlen(node->value), true);
        put(out, "\"", 1);
    }
    if (layout && input && node->checked == 1)
        put_str(out, " checked");
    if (forced && index == select->selected)
        put_str(out, " selected");
    put(out, ">", 1);
}

/* Schreibt den Beginn eines Knotens; true, wenn danach seine Kinder an der Reihe sind */
static bool write_open(out_t *out, const dom_node *n, bool layout)
{
    static const char *const skipped[] = { "script", "noscript", "template", NULL };
    switch (n->type) {
    case DOM_ELEMENT:
        if (layout && one_of(n->name, skipped))
            return false;               /* nichts für die Anzeige */
        open_tag(out, n, layout);
        if (layout && dom_is(n, "textarea") && n->has_value) {
            put_escaped(out, n->value, strlen(n->value), false);
            put_str(out, "</textarea>");
            return false;
        }
        if (one_of(n->name, void_tags))
            return false;               /* kein Inhalt, kein Ende */
        if (n->first)
            return true;
        put(out, "</", 2);
        put_str(out, n->name);
        put(out, ">", 1);
        return false;
    case DOM_TEXT:
        if (n->raw || (n->parent && n->parent->type == DOM_ELEMENT &&
                       one_of(n->parent->name, raw_tags)))
            put(out, n->text, n->text_len);
        else
            put_escaped(out, n->text, n->text_len, false);
        return false;
    case DOM_COMMENT:
        if (!layout) {
            put_str(out, "<!--");
            put(out, n->text, n->text_len);
            put_str(out, "-->");
        }
        return false;
    default:
        return n->first != NULL;        /* Dokument, Fragment */
    }
}

static void write_close(out_t *out, const dom_node *n)
{
    if (n->type != DOM_ELEMENT)
        return;
    put(out, "</", 2);
    put_str(out, n->name);
    put(out, ">", 1);
}

/* Ohne Rekursion: Der Baum kann tief sein. */
static void write_tree(out_t *out, const dom_node *root, bool inner, bool layout)
{
    const dom_node *n = inner ? root->first : root;
    while (n && !out->failed) {
        if (write_open(out, n, layout)) {
            n = n->first;
            continue;
        }
        for (;;) {
            if (n == root) {
                n = NULL;
                break;
            }
            if (n->next) {
                n = n->next;
                break;
            }
            n = n->parent;              /* alle Kinder dieses Knotens sind geschrieben */
            if (!n || n == root) {
                if (n && !inner)
                    write_close(out, n);
                n = NULL;
                break;
            }
            write_close(out, n);
        }
    }
}

static char *finish(out_t *out, size_t *len)
{
    put(out, "", 0);
    if (out->failed || !out->text) {
        kfree(out->text);
        return NULL;
    }
    out->text[out->len] = '\0';
    *len = out->len;
    return out->text;
}

char *dom_html(const dom_node *node, bool inner, size_t *len)
{
    out_t out = { NULL, 0, 0, false };
    if (!inner && node->type != DOM_ELEMENT && node->type != DOM_DOCUMENT &&
        node->type != DOM_FRAGMENT) {
        if (node->type == DOM_TEXT)
            put_escaped(&out, node->text, node->text_len, false);
    } else {
        write_tree(&out, node, inner, false);
    }
    return finish(&out, len);
}

char *dom_serialize(dom_doc *doc, size_t *len)
{
    out_t out = { NULL, 0, 0, false };
    put_str(&out, "<!DOCTYPE html>");
    write_tree(&out, doc->root, true, true);
    doc->dirty = false;
    return finish(&out, len);
}

/* --- Quelltext lesen --------------------------------------------------------------------------- */

typedef struct {
    dom_doc *doc;
    dom_node *cur;                      /* hier wird gerade eingehängt */
    dom_node *top;                      /* über diesen Knoten hinaus wird nichts geschlossen */
    dom_node *html, *head, *body;
    bool fragment;
    int depth;
} builder;

static bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

static int depth_of(const dom_node *n)
{
    int d = 0;
    for (; n && d < 100000; n = n->parent)
        d++;
    return d;
}

static dom_node *append(builder *b, dom_node *child)
{
    if (!child)
        return NULL;
    link_before(b->cur, child, NULL);
    return child;
}

/* Sorgt im ganzen Dokument für <html>, <head> und <body> */
static void ensure_html(builder *b)
{
    if (b->fragment || b->html)
        return;
    b->html = dom_create_element(b->doc, "html", 4);
    if (b->html)
        link_before(b->doc->root, b->html, NULL);
}

static void ensure_head(builder *b)
{
    ensure_html(b);
    if (b->fragment || b->head || !b->html)
        return;
    b->head = dom_create_element(b->doc, "head", 4);
    if (b->head)
        link_before(b->html, b->head, b->html->first);
}

static void ensure_body(builder *b)
{
    ensure_html(b);
    if (b->fragment || b->body || !b->html)
        return;
    b->body = dom_create_element(b->doc, "body", 4);
    if (b->body)
        link_before(b->html, b->body, NULL);
}

/* Wo landet Inhalt, wenn gerade nur das Gerüst offen ist? */
static void place_for(builder *b, bool head_content)
{
    if (b->fragment)
        return;
    if (b->cur == b->doc->root || b->cur == b->html) {
        if (head_content && !b->body) {
            ensure_head(b);
            if (b->head)
                b->cur = b->head;
        } else {
            ensure_body(b);
            if (b->body)
                b->cur = b->body;
        }
    } else if (b->cur == b->head && !head_content) {
        ensure_body(b);
        if (b->body)
            b->cur = b->body;
    }
}

/* Schließt Elemente bis einschließlich dem nächsten mit einem der Namen - falls keines der
 * Grenz-Elemente dazwischenliegt. */
static void close_nearest(builder *b, const char *const *names, const char *const *limits)
{
    for (dom_node *n = b->cur; n && n != b->top && n->type == DOM_ELEMENT; n = n->parent) {
        if (one_of(n->name, names)) {
            b->cur = n->parent ? n->parent : b->cur;
            return;
        }
        if (one_of(n->name, limits))
            return;
    }
}

static void close_implied(builder *b, const char *name)
{
    static const char *const list_limits[] = { "ul", "ol", "menu", "table", "div", "body", NULL };
    static const char *const table_limits[] = { "table", NULL };
    static const char *const item[] = { "li", NULL }, *const term[] = { "dd", "dt", NULL };
    static const char *const def_limits[] = { "dl", "table", NULL };
    static const char *const row[] = { "tr", NULL }, *const cell[] = { "td", "th", NULL };
    static const char *const section[] = { "thead", "tbody", "tfoot", NULL };
    static const char *const option[] = { "option", NULL }, *const select[] = { "select", NULL };
    static const char *const para[] = { "p", NULL };
    static const char *const para_limits[] = { "table", "td", "th", "button", "body", "div", "li",
                                               "section", "article", "form", NULL };
    static const char *const blocks[] = {
        "address", "article", "aside", "blockquote", "div", "dl", "fieldset", "footer", "form",
        "h1", "h2", "h3", "h4", "h5", "h6", "header", "hr", "main", "nav", "ol", "p", "pre",
        "section", "table", "ul", "li", "dd", "dt", "figure", "figcaption", "details", NULL };
    if (same(name, "li"))
        close_nearest(b, item, list_limits);
    else if (same(name, "dd") || same(name, "dt"))
        close_nearest(b, term, def_limits);
    else if (same(name, "tr"))
        close_nearest(b, row, table_limits);
    else if (same(name, "td") || same(name, "th"))
        close_nearest(b, cell, table_limits);
    else if (one_of(name, section))
        close_nearest(b, section, table_limits);
    else if (same(name, "option") || same(name, "optgroup"))
        close_nearest(b, option, select);
    if (one_of(name, blocks))
        close_nearest(b, para, para_limits);
}

static void read_attributes(builder *b, dom_node *node, const char **cursor, const char *end)
{
    const char *p = *cursor;
    dom_doc *doc = b->doc;
    while (p < end && *p != '>') {
        if (is_space(*p) || *p == '/') {
            p++;
            continue;
        }
        const char *name = p;
        while (p < end && !is_space(*p) && *p != '=' && *p != '>' && *p != '/')
            p++;
        size_t name_len = (size_t)(p - name);
        const char *value = "";
        size_t value_len = 0;
        while (p < end && is_space(*p))
            p++;
        if (p < end && *p == '=') {
            p++;
            while (p < end && is_space(*p))
                p++;
            if (p < end && (*p == '"' || *p == '\'')) {
                char quote = *p++;
                value = p;
                while (p < end && *p != quote)
                    p++;
                value_len = (size_t)(p - value);
                p += p < end;
            } else {
                value = p;
                while (p < end && !is_space(*p) && *p != '>')
                    p++;
                value_len = (size_t)(p - value);
            }
        }
        if (name_len == 0) {
            p++;
            continue;
        }
        if (!node || name_len > 200)
            continue;
        /* derselbe Name zweimal: der erste gilt */
        char folded[201];
        for (size_t i = 0; i < name_len; i++)
            folded[i] = lower(name[i]);
        folded[name_len] = '\0';
        if (find_attr(node, folded))
            continue;
        char *decoded = kmalloc(value_len + 8);
        if (!decoded) {
            doc->failed = true;
            break;
        }
        size_t n = html_decode_text(value, value_len, decoded, value_len + 8);
        attr_add(node, folded, name_len, decoded, n);
        kfree(decoded);
    }
    p += p < end;
    *cursor = p;
    if (node)
        refresh_css(node);
}

static const char *find_close(const char *p, const char *end, const char *name)
{
    size_t n = strlen(name);
    for (; p + n + 2 <= end; p++) {
        if (p[0] != '<' || p[1] != '/')
            continue;
        size_t i = 0;
        while (i < n && lower(p[2 + i]) == name[i])
            i++;
        if (i == n && (p + 2 + n >= end || is_space(p[2 + n]) || p[2 + n] == '>' || p[2 + n] == '/'))
            return p;
    }
    return end;
}

static void add_text(builder *b, const char *text, size_t len, bool decode)
{
    if (len == 0)
        return;
    bool blank = true;
    for (size_t i = 0; i < len && blank; i++)
        blank = is_space(text[i]);
    if (!b->fragment && blank &&
        (b->cur == b->doc->root || b->cur == b->html || b->cur == b->head))
        return;                         /* Leerraum zwischen den Teilen des Gerüsts */
    if (!blank)
        place_for(b, false);
    if (!decode) {
        dom_node *t = append(b, dom_create_text(b->doc, text, len));
        if (t)
            t->raw = true;
        return;
    }
    char *decoded = kmalloc(len + 8);
    if (!decoded) {
        b->doc->failed = true;
        return;
    }
    size_t n = html_decode_text(text, len, decoded, len + 8);
    /* an einen Text davor anhängen, statt zwei daraus zu machen */
    dom_node *last = b->cur->last;
    if (last && last->type == DOM_TEXT && !last->raw && (size_t)last->text_len + n < 0xFFFFFF00u) {
        char *joined = take(b->doc, (size_t)last->text_len + n + 1);
        if (joined) {
            memcpy(joined, last->text, last->text_len);
            memcpy(joined + last->text_len, decoded, n);
            give(b->doc, last->text, (size_t)last->text_len + 1);
            last->text = joined;
            last->text_len += (uint32_t)n;
        }
    } else {
        append(b, dom_create_text(b->doc, decoded, n));
    }
    kfree(decoded);
}

static void build(builder *b, const char *html, size_t len)
{
    static const char *const head_tags[] = { "title", "meta", "link", "base", "style", "script",
                                             "noscript", "template", NULL };
    static const char *const rcdata[] = { "textarea", "title", NULL };
    const char *p = html, *end = html + len;
    dom_doc *doc = b->doc;
    while (p < end && !doc->failed) {
        if (*p != '<') {
            const char *start = p;
            while (p < end && *p != '<')
                p++;
            add_text(b, start, (size_t)(p - start), true);
            continue;
        }
        if (end - p >= 4 && memcmp(p, "<!--", 4) == 0) {
            const char *start = p + 4;
            p = start;
            while (p + 3 <= end && memcmp(p, "-->", 3) != 0)
                p++;
            size_t n = p + 3 <= end ? (size_t)(p - start) : (size_t)(end - start);
            if (b->fragment || b->cur != doc->root)
                append(b, dom_create_comment(doc, start, n));
            p = p + 3 <= end ? p + 3 : end;
            continue;
        }
        if (p + 1 < end && (p[1] == '!' || p[1] == '?')) {
            while (p < end && *p != '>')
                p++;
            p += p < end;
            continue;
        }
        bool closing = p + 1 < end && p[1] == '/';
        const char *name = p + (closing ? 2 : 1);
        if (name >= end || !((*name | 32) >= 'a' && (*name | 32) <= 'z')) {
            add_text(b, p, 1, true);    /* ein '<', das nur Text ist */
            p++;
            continue;
        }
        size_t name_len = 0;
        while (name + name_len < end && !is_space(name[name_len]) && name[name_len] != '>' &&
               name[name_len] != '/')
            name_len++;
        char tag[64];
        size_t n = name_len < sizeof(tag) - 1 ? name_len : sizeof(tag) - 1;
        for (size_t i = 0; i < n; i++)
            tag[i] = lower(name[i]);
        tag[n] = '\0';
        p = name + name_len;

        if (closing) {
            read_attributes(b, NULL, &p, end);
            if (same(tag, "br")) {      /* </br> gilt als <br> */
                place_for(b, false);
                append(b, dom_create_element(doc, "br", 2));
                continue;
            }
            if (!b->fragment && (same(tag, "html") || same(tag, "body")))
                continue;               /* was danach noch kommt, gehört trotzdem in die Seite */
            if (!b->fragment && same(tag, "head")) {
                if (b->cur == b->head)
                    b->cur = b->html;
                continue;
            }
            static const char *const table_parts[] = { "table", "tr", "td", "th", "thead", "tbody",
                                                       "tfoot", "caption", NULL };
            bool part = one_of(tag, table_parts);
            for (dom_node *e = b->cur; e && e != b->top && e->type == DOM_ELEMENT; e = e->parent) {
                if (e == b->html || e == b->body)
                    break;
                if (same(e->name, tag)) {
                    b->cur = e->parent ? e->parent : b->cur;
                    break;
                }
                /* über Zellen und Tabellen hinweg schließen nur die Teile einer Tabelle selbst */
                if (same(e->name, "table") || (!part && (same(e->name, "td") || same(e->name, "th"))))
                    break;
            }
            continue;
        }

        /* das Gerüst: vorhandene Teile wiederverwenden */
        if (!b->fragment && (same(tag, "html") || same(tag, "head") || same(tag, "body"))) {
            dom_node *part;
            if (same(tag, "html")) {
                ensure_html(b);
                part = b->html;
            } else if (same(tag, "head")) {
                ensure_head(b);
                part = b->head;
            } else {
                ensure_body(b);
                part = b->body;
            }
            read_attributes(b, part, &p, end);
            if (part && !same(tag, "html") && !(same(tag, "head") && b->body))
                b->cur = part;
            continue;
        }
        if (b->fragment && (same(tag, "html") || same(tag, "head") || same(tag, "body"))) {
            read_attributes(b, NULL, &p, end);
            continue;
        }

        place_for(b, one_of(tag, head_tags));
        close_implied(b, tag);
        dom_node *e = dom_create_element(doc, tag, n);
        if (!e)
            break;
        read_attributes(b, e, &p, end);
        append(b, e);
        if (one_of(tag, void_tags))
            continue;
        if (one_of(tag, raw_tags) || one_of(tag, rcdata)) {
            const char *stop = find_close(p, end, tag);
            dom_node *outer = b->cur;
            b->cur = e;
            const char *text = p;
            size_t text_len = (size_t)(stop - p);
            if (same(tag, "textarea") && text_len && *text == '\n') {
                text++;                 /* ein Zeilenwechsel direkt am Anfang zählt nicht */
                text_len--;
            }
            if (text_len) {
                if (one_of(tag, raw_tags)) {
                    dom_node *t = dom_create_text(doc, text, text_len);
                    if (t) {
                        t->raw = true;
                        link_before(e, t, NULL);
                    }
                } else {
                    char *decoded = kmalloc(text_len + 8);
                    if (decoded) {
                        size_t dn = html_decode_text(text, text_len, decoded, text_len + 8);
                        dom_node *t = dom_create_text(doc, decoded, dn);
                        if (t)
                            link_before(e, t, NULL);
                        kfree(decoded);
                    } else {
                        doc->failed = true;
                    }
                }
            }
            b->cur = outer;
            p = stop;
            if (p < end) {
                while (p < end && *p != '>')
                    p++;
                p += p < end;
            }
            continue;
        }
        /* sehr tiefe Verschachtelung: das Element bleibt leer daneben stehen */
        if (b->depth + depth_of(e) < DEPTH_MAX)
            b->cur = e;
    }
}

dom_doc *dom_parse(const char *html, size_t len)
{
    dom_doc *doc = kzalloc(sizeof(dom_doc));
    if (!doc)
        return NULL;
    doc->root = node_new(doc, DOM_DOCUMENT);
    if (!doc->root) {
        dom_free(doc);
        return NULL;
    }
    builder b = { doc, doc->root, doc->root, NULL, NULL, NULL, false, 0 };
    build(&b, html, len);
    ensure_head(&b);
    ensure_body(&b);
    doc->dirty = false;
    if (doc->failed && doc->count < 2) {
        dom_free(doc);
        return NULL;
    }
    return doc;
}

dom_node *dom_parse_fragment(dom_doc *doc, const char *html, size_t len)
{
    dom_node *fragment = dom_create_fragment(doc);
    if (!fragment)
        return NULL;
    builder b = { doc, fragment, fragment, NULL, NULL, NULL, true, 0 };
    build(&b, html, len);
    return fragment;
}

bool dom_set_inner_html(dom_node *node, const char *html, size_t len)
{
    if (node->type != DOM_ELEMENT && node->type != DOM_FRAGMENT)
        return false;
    dom_doc *doc = node->doc;
    while (node->first)
        dom_remove(node->first);
    if (node->type == DOM_ELEMENT && (one_of(node->name, raw_tags) || dom_is(node, "textarea") ||
                                      dom_is(node, "title"))) {
        dom_node *t = dom_create_text(doc, html, len);
        if (!t)
            return false;
        t->raw = one_of(node->name, raw_tags);
        link_before(node, t, NULL);
        touch(doc);
        return true;
    }
    builder b = { doc, node, node, NULL, NULL, NULL, true, depth_of(node) };
    build(&b, html, len);
    touch(doc);
    return !doc->failed;
}

bool dom_set_text(dom_node *node, const char *text, size_t len)
{
    dom_doc *doc = node->doc;
    if (node->type == DOM_TEXT || node->type == DOM_COMMENT) {
        if (len > 0xFFFFFF00u)
            return false;
        char *copy = copy_text(doc, text, len);
        if (!copy)
            return false;
        give(doc, node->text, (size_t)node->text_len + 1);
        node->text = copy;
        node->text_len = (uint32_t)len;
        touch(doc);
        return true;
    }
    while (node->first)
        dom_remove(node->first);
    touch(doc);
    if (len == 0)
        return true;
    dom_node *t = dom_create_text(doc, text, len);
    if (!t)
        return false;
    t->raw = node->type == DOM_ELEMENT && one_of(node->name, raw_tags);
    link_before(node, t, NULL);
    return true;
}

dom_node *dom_clone(dom_node *node, bool deep)
{
    dom_doc *doc = node->doc;
    dom_node *copy;
    if (node->type == DOM_ELEMENT) {
        copy = dom_create_element(doc, node->name, strlen(node->name));
        for (int i = 0; copy && i < node->attr_count; i++)
            attr_add(copy, node->attrs[i].name, strlen(node->attrs[i].name), node->attrs[i].value,
                     strlen(node->attrs[i].value));
        if (copy) {
            copy->checked = node->checked;
            copy->selected = node->selected;
            if (node->has_value)
                dom_set_field_value(copy, node->value, strlen(node->value));
            refresh_css(copy);
        }
    } else if (node->type == DOM_TEXT || node->type == DOM_COMMENT) {
        copy = text_node(doc, node->type, node->text, node->text_len);
        if (copy)
            copy->raw = node->raw;
    } else {
        copy = dom_create_fragment(doc);
    }
    if (!copy || !deep)
        return copy;
    /* die Kinder nachbauen - ohne Rekursion; "to" ist stets die Kopie von "from" */
    const dom_node *from = node;
    dom_node *to = copy, *parent;
    for (;;) {
        if (from->first) {
            from = from->first;
            parent = to;
        } else {
            while (from != node && !from->next) {
                from = from->parent;
                to = to->parent;
            }
            if (from == node)
                break;
            from = from->next;
            parent = to->parent;
        }
        dom_node *child = dom_clone((dom_node *)from, false);
        if (!child || !parent)
            return copy;
        link_before(parent, child, NULL);
        to = child;
    }
    return copy;
}

void dom_free(dom_doc *doc)
{
    if (!doc)
        return;
    for (uint32_t i = 0; i < doc->count; i++) {
        dom_node *n = doc->nodes[i];
        for (int a = 0; a < n->attr_count; a++) {
            kfree(n->attrs[a].name);
            kfree(n->attrs[a].value);
        }
        kfree(n->attrs);
        kfree(n->name);
        kfree(n->text);
        kfree(n->value);
        kfree(n);
    }
    kfree(doc->nodes);
    kfree(doc);
}
