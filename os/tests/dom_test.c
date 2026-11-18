/*
 * Test für kernel/dom.c auf dem Entwicklungsrechner: Quelltext in den Baum
 * lesen, umbauen, wieder ausgeben - und prüfen, dass die Anzeige (html.c) aus
 * dem wieder ausgegebenen Quelltext dieselbe Seite macht wie aus dem Original.
 */
#include "../kernel/dom.h"
#include "../kernel/html.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#define strlcpy libc_strlcpy
#include <string.h>
#undef strlcpy

void *kmalloc(size_t n) { return malloc(n ? n : 1); }
void *kzalloc(size_t n) { return calloc(1, n ? n : 1); }
void kfree(void *p) { free(p); }
void strlcpy(char *d, const char *s, size_t n)
{
    if (!n) return;
    size_t i = 0;
    for (; i + 1 < n && s[i]; i++) d[i] = s[i];
    d[i] = 0;
}
int ksnprintf(char *b, size_t n, const char *f, ...)
{
    va_list ap;
    va_start(ap, f);
    int r = vsnprintf(b, n, f, ap);
    va_end(ap);
    return r;
}
void claude_plain_text(char *text) { (void)text; }

static int checks, failures;

static void check(bool ok, const char *what)
{
    checks++;
    if (!ok) {
        failures++;
        printf("FEHLER: %s\n", what);
    }
}

static dom_node *find(dom_doc *doc, const char *name)
{
    for (dom_node *n = dom_next(doc->root, doc->root); n; n = dom_next(doc->root, n)) {
        if (dom_is(n, name))
            return n;
    }
    return NULL;
}

static bool html_is(dom_node *n, bool inner, const char *want)
{
    size_t len;
    char *got = dom_html(n, inner, &len);
    bool same = got && strcmp(got, want) == 0;
    if (!same)
        printf("  ist:  %s\n  soll: %s\n", got ? got : "(nichts)", want);
    free(got);
    return same;
}

/* Die Seite als Text, wie der Browser sie setzt - zum Vergleich zweier Quelltexte */
static char *page_text(const char *html, size_t len)
{
    page_t *p = html_layout(html, len, "https://a.de/", 70);
    size_t cap = 1 << 20, n = 0;
    char *out = malloc(cap);
    for (int l = 0; p && l < p->line_count; l++) {
        const line_t *line = &p->lines[l];
        for (uint32_t s = 0; s < line->count; s++) {
            const span_t *sp = &p->spans[line->first + s];
            if (n + sp->bytes + 40 < cap) {
                if (sp->widget >= 0) {
                    const widget_t *w = &p->widgets[sp->widget];
                    n += (size_t)snprintf(out + n, cap - n, "<%d:%s%s>", (int)w->kind,
                                          w->value ? w->value : "", w->checked ? " X" : "");
                } else {
                    memcpy(out + n, p->pool + sp->text, sp->bytes);
                    n += sp->bytes;
                }
                out[n++] = ' ';
            }
        }
        out[n++] = '\n';
    }
    n += (size_t)snprintf(out + n, cap - n, "[%d Links, %d Felder, %d Formulare]", p ? p->link_count : -1,
                          p ? p->widget_count : -1, p ? p->form_count : -1);
    out[n] = 0;
    page_free(p);
    return out;
}

static bool same_page(const char *html, size_t len, const char *what)
{
    dom_doc *doc = dom_parse(html, len);
    size_t out_len = 0;
    char *again = doc ? dom_serialize(doc, &out_len) : NULL;
    bool same = false;
    if (again) {
        char *a = page_text(html, len), *b = page_text(again, out_len);
        same = strcmp(a, b) == 0;
        if (!same) {
            size_t i = 0;
            while (a[i] && a[i] == b[i])
                i++;
            size_t from = i > 60 ? i - 60 : 0;
            printf("  %s: Anzeige weicht ab bei Zeichen %zu\n  Original: ...%.160s\n  Baum:     ...%.160s\n",
                   what, i, a + from, b + from);
        }
        free(a);
        free(b);
    }
    free(again);
    dom_free(doc);
    return same;
}

static const char sample[] =
    "<!DOCTYPE html><html lang=de><head><meta charset=utf-8><title>Titel &amp; mehr</title>"
    "<style>p { color: red } a > b { x: y }</style><script>if (1 < 2) { var s = '</b>'; }</script></head>"
    "<body class='a b' id=haupt><h1>Gr&ouml;&szlig;e &lt;wichtig&gt;</h1>"
    "<p>Erster Absatz<p>Zweiter mit <a href=\"/x?a=1&amp;b=2\">Link &amp; Text</a><br>und Umbruch"
    "<ul><li>eins<li>zwei <b>fett</b></ul>"
    "<table><tr><th>Kopf<td>Zelle 1<tr><td colspan=2>Zelle 2</table>"
    "<form action=/senden method=post><input name=n value='W&auml;rt \"x\"'><input type=checkbox name=c checked>"
    "<select name=s><option value=1>Eins<option value=2 selected>Zwei</select>"
    "<textarea name=t>\nZeile 1\n&lt;Zeile 2&gt;</textarea><button>Los</button></form>"
    "<noscript>Ohne Skripte</noscript><!-- Kommentar --><div hidden>versteckt</div>"
    "<p>Ende</p></body></html>";

int main(void)
{
    dom_doc *doc = dom_parse(sample, sizeof(sample) - 1);
    check(doc && !doc->failed, "Seite gelesen");
    if (!doc)
        return 1;
    dom_node *html = doc->root->first, *head = html ? html->first : NULL, *body = head ? head->next : NULL;
    check(dom_is(html, "html") && dom_is(head, "head") && dom_is(body, "body") && !body->next,
          "Gerüst: html mit head und body");
    check(strcmp(dom_attr_get(html, "lang"), "de") == 0 && strcmp(dom_attr_get(body, "id"), "haupt") == 0,
          "Attribute gelesen");
    size_t len;
    char *text = dom_text_content(find(doc, "title"), &len);
    check(text && strcmp(text, "Titel & mehr") == 0, "Zeichenangaben im Titel aufgelöst");
    free(text);
    text = dom_text_content(find(doc, "h1"), &len);
    check(text && strcmp(text, "Größe <wichtig>") == 0, "Zeichenangaben im Text aufgelöst");
    free(text);
    check(html_is(find(doc, "h1"), false, "<h1>Größe &lt;wichtig&gt;</h1>"), "Ausgabe maskiert wieder");
    check(html_is(find(doc, "script"), true, "if (1 < 2) { var s = '</b>'; }"), "Skripttext bleibt unverändert");
    dom_node *p1 = find(doc, "p");
    check(p1 && dom_is(p1->next, "p") && html_is(p1, false, "<p>Erster Absatz</p>"),
          "ein neuer Absatz schließt den vorigen");
    check(html_is(find(doc, "ul"), false, "<ul><li>eins</li><li>zwei <b>fett</b></li></ul>"),
          "Listenpunkte schließen einander");
    check(html_is(find(doc, "table"), false,
                  "<table><tr><th>Kopf</th><td>Zelle 1</td></tr><tr><td colspan=\"2\">Zelle 2</td></tr></table>"),
          "Tabellenzellen und -zeilen schließen einander");
    check(html_is(find(doc, "a"), false, "<a href=\"/x?a=1&amp;b=2\">Link &amp; Text</a>"), "Attributwerte maskiert");
    dom_node *input = find(doc, "input"), *select = find(doc, "select"), *area = find(doc, "textarea");
    text = dom_field_value(input, &len);
    check(text && strcmp(text, "Wärt \"x\"") == 0, "Wert eines Felds");
    free(text);
    text = dom_field_value(select, &len);
    check(text && strcmp(text, "2") == 0, "Wert der Auswahlliste: der gewählte Eintrag");
    free(text);
    text = dom_field_value(area, &len);
    check(text && strcmp(text, "Zeile 1\n<Zeile 2>") == 0, "Inhalt des mehrzeiligen Felds");
    free(text);

    /* Anzeige: aus dem Baum dieselbe Seite wie aus dem Original */
    /* (ohne den <noscript>-Teil: Der gilt nur, solange keine Skripte laufen) */
    char *plain = strdup(sample);
    char *ns = strstr(plain, "<noscript>"), *ne = strstr(plain, "</noscript>");
    memmove(ns, ne + 11, strlen(ne + 11) + 1);
    check(same_page(plain, strlen(plain), "Beispielseite"), "Anzeige aus dem Baum gleicht dem Original");
    free(plain);

    /* Umbauen */
    dom_node *list = find(doc, "ul"), *item = dom_create_element(doc, "LI", 2);
    dom_node *t = dom_create_text(doc, "drei & <vier>", 13);
    check(item && t && dom_insert(item, t, NULL) && dom_insert(list, item, list->first), "Element vorn eingehängt");
    check(html_is(list, true, "<li>drei &amp; &lt;vier&gt;</li><li>eins</li><li>zwei <b>fett</b></li>"), "Reihenfolge stimmt");
    check(dom_insert(list, list->first, NULL) && html_is(list, true, "<li>eins</li><li>zwei <b>fett</b></li><li>drei &amp; &lt;vier&gt;</li>"),
          "umhängen ans Ende");
    check(!dom_insert(item, list, NULL), "kein Kreis: Elternknoten nicht in sein Kind");
    dom_remove(list->first);
    check(html_is(list, true, "<li>zwei <b>fett</b></li><li>drei &amp; &lt;vier&gt;</li>"), "entfernt");
    check(dom_set_inner_html(list, "<li class=neu>x<li>y</ul><p>z", 29) &&
          html_is(list, true, "<li class=\"neu\">x</li><li>y<p>z</p></li>"), "innerHTML: liest und schließt nichts außerhalb");
    check(dom_attr_set(list, "CLASS", "eins zwei", 9) && strcmp(dom_attr_get(list, "class"), "eins zwei") == 0 &&
          list->css.classes_len == 9, "Attribut setzen, Name ohne Rücksicht auf Großschreibung");
    dom_attr_remove(list, "class");
    check(!dom_attr_get(list, "class") && list->css.classes_len == 0, "Attribut entfernen");
    dom_node *copy = dom_clone(find(doc, "table"), true);
    check(copy && !copy->parent && html_is(copy, false,
          "<table><tr><th>Kopf</th><td>Zelle 1</td></tr><tr><td colspan=\"2\">Zelle 2</td></tr></table>"),
          "tiefe Kopie");
    check(dom_set_text(find(doc, "h1"), "Neu <b>", 7) && html_is(find(doc, "h1"), false, "<h1>Neu &lt;b&gt;</h1>"),
          "Text setzen ersetzt den Inhalt");
    dom_node *frag = dom_parse_fragment(doc, "<i>a</i>b", 9);
    check(frag && dom_insert(body, frag, body->first) && dom_is(body->first, "i") && !frag->first,
          "Fragment gibt seine Kinder ab");

    /* Auswahlen */
    css_selector_t *sel = css_selector_parse("FORM > input[type=checkbox], li.neu", 35);
    int hits = 0;
    for (dom_node *n = dom_next(doc->root, doc->root); sel && n; n = dom_next(doc->root, n))
        hits += n->type == DOM_ELEMENT && css_selector_matches(sel, &n->css);
    check(sel && hits == 2, "Auswahl findet Elemente im Baum");
    css_selector_free(sel);
    check(!css_selector_parse("a:hover", 7) && !css_selector_parse("", 0), "nicht prüfbare Auswahl wird abgelehnt");
    check(dom_find_id(doc->root, "haupt") == body && !dom_find_id(doc->root, "fehlt"), "Suche nach id");

    /* Formularstand in der Anzeige */
    dom_set_field_value(input, "vom Benutzer", 12);
    find(doc, "input")->next->checked = 0;
    dom_set_field_value(select, "1", 1);
    dom_set_field_value(area, "geändert", 9);
    char *out = dom_serialize(doc, &len);
    check(out && strstr(out, "value=\"vom Benutzer\"") && !strstr(out, "checked") &&
          strstr(out, "<option data-hx=") && strstr(out, " value=\"1\" selected>") &&
          !strstr(out, "value=\"2\" selected") && strstr(out, ">geändert</textarea>") &&
          !strstr(out, "noscript") && !strstr(out, "var s") && !strstr(out, "Kommentar"),
          "Ausgabe für die Anzeige: Stand der Felder, ohne Skripte und Kommentare");
    check(out && strncmp(out, "<!DOCTYPE html><html data-hx=", 29) == 0, "jedes Element trägt seine Nummer");
    free(out);
    check(dom_node_by_id(doc, body->id) == body && !dom_node_by_id(doc, 0) && !dom_node_by_id(doc, 99999999),
          "Nummer führt zum Knoten");
    dom_free(doc);

    /* Seiten ohne Gerüst, mit Resten davor und danach */
    static const char *const bare[] = {
        "Nur Text", "<p>Absatz</p>", "<title>T</title><p>x", "<html><p>ohne body</p></html>nach",
        "<body><p>x</body></html><p>danach", "</p></div>text<br/>", "<table><td>ohne tr</table>x",
        "<head><title>a</title></head><div>b</div>", "<select><option>a<option>b</select>",
        "<p><b>fett<p>neuer absatz</b>", "<ul><li>a<ul><li>b</ul><li>c</ul>", "",
    };
    bool all = true;
    for (unsigned i = 0; i < sizeof(bare) / sizeof(bare[0]); i++)
        all &= same_page(bare[i], strlen(bare[i]), bare[i]);
    check(all, "Seiten ohne Gerüst: Anzeige aus dem Baum gleicht dem Original");

    /* Beschädigte Seiten: abschneiden, Bytes kippen, Stücke verdoppeln */
    unsigned seed = 2026;
    char *broken = malloc(3 * sizeof(sample));
    for (int round = 0; round < 6000; round++) {
        size_t n = sizeof(sample) - 1;
        memcpy(broken, sample, n);
        seed = seed * 1103515245u + 12345u;
        unsigned r = seed >> 8;
        if (round % 3 == 0) {
            n = r % n;
        } else if (round % 3 == 1) {
            for (int k = 0; k < 12; k++) {
                seed = seed * 1103515245u + 12345u;
                broken[(seed >> 8) % n] = "<>/\"'=& \n!-ab"[(seed >> 20) % 14];
            }
        } else {
            size_t from = r % n, count = (r >> 7) % (n - from);
            memcpy(broken + n, sample + from, count);
            memcpy(broken + n + count, sample + from, count);
            n += 2 * count;
        }
        dom_doc *d = dom_parse(broken, n);
        if (!d) {
            failures++;
            continue;
        }
        size_t out_len;
        char *text2 = dom_serialize(d, &out_len);
        dom_node *any = dom_node_by_id(d, 1 + (r % d->count));
        if (any && any->type == DOM_ELEMENT) {
            dom_set_inner_html(any, broken + (r % (n + 1)) / 2, n / 3);
            dom_node *c = dom_clone(any, true);
            if (c && d->root->first)
                dom_insert(d->root->first, c, NULL);
            char *inner = dom_html(any, true, &out_len);
            free(inner);
        }
        free(text2);
        text2 = dom_serialize(d, &out_len);
        if (text2) {
            page_t *p = html_layout(text2, out_len, "https://a.de/", 60);
            page_free(p);
        }
        free(text2);
        dom_free(d);
    }
    free(broken);
    check(1, "6000 beschädigte Seiten verarbeitet");

    printf("%d Prüfungen am Seitenabbild (DOM), %d Fehler\n", checks, failures);
    return failures != 0;
}
