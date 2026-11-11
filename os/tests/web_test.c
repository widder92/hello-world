/*
 * Prüfprogramm für den Browser-Unterbau: Web-Adressen (url.c) und die Umsetzung
 * von HTML in Textzeilen (html.c). Läuft auf dem Entwicklungsrechner; mit
 * -fsanitize=address,undefined übersetzt, fallen auch Speicherfehler auf.
 * Am Ende wird eine Beispielseite tausendfach beschädigt und erneut verarbeitet -
 * abstürzen darf dabei nichts.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "../kernel/html.h"
#include "../kernel/url.h"

void *memset(void *, int, size_t);
void *memcpy(void *, const void *, size_t);
int memcmp(const void *, const void *, size_t);
int strcmp(const char *, const char *);
size_t strlen(const char *);

/* Was der Kernel sonst bereitstellt */
void *kmalloc(size_t n) { return malloc(n ? n : 1); }
void *kzalloc(size_t n) { return calloc(1, n ? n : 1); }
void kfree(void *p) { free(p); }
void strlcpy(char *d, const char *s, size_t n)
{
    size_t i = 0;
    if (!n)
        return;
    for (; i + 1 < n && s[i]; i++)
        d[i] = s[i];
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

static int failures, checks;

static void check(int ok, const char *what)
{
    checks++;
    if (!ok) {
        failures++;
        printf("FEHLER: %s\n", what);
    }
}

/* Text einer Zeile der fertigen Seite (Felder erscheinen als "[]") */
static const char *line_text(const page_t *p, int index)
{
    static char text[600];
    size_t n = 0;
    int column = 0;
    text[0] = 0;
    if (index >= p->line_count)
        return text;
    for (unsigned i = 0; i < p->lines[index].count; i++) {
        const span_t *s = &p->spans[p->lines[index].first + i];
        while (column < s->column && n + 1 < sizeof(text)) {
            text[n++] = ' ';
            column++;
        }
        if (s->widget >= 0) {
            if (n + 3 < sizeof(text)) {
                text[n++] = '[';
                text[n++] = ']';
            }
        } else if (n + s->bytes + 1 < sizeof(text)) {
            memcpy(text + n, p->pool + s->text, s->bytes);
            n += s->bytes;
        }
        column += s->width;
    }
    text[n] = 0;
    return text;
}

static const char sample[] =
    "<!DOCTYPE html><html><head><title>Test &amp; Probe</title>"
    "<style>p { color: red }</style><script>if (a < b) { document.write('<p>nein</p>'); }</script>"
    "</head><body><h1 id=\"oben\">Titel</h1>"
    "<p>Ein <b>fetter</b> Text mit <a href=\"../seite.html?a=1&amp;b=2\">Link</a> und "
    "Umlauten: &auml;&ouml;&uuml; &#8364;.</p>"
    "<div class=\"hidden\">unsichtbar</div><div style=\"display: none\">auch</div>"
    "<ul><li>eins<li>zwei</li></ul>"
    "<table><tr><th>A<th>B</tr><tr><td>1</td><td>2</td></tr></table>"
    "<form action=\"/anmelden\" method=\"post\">"
    "<input type=\"hidden\" name=\"geheim\" value=\"x&quot;y\">"
    "<input name=\"name\" value=\"Erika\"> <input type=\"password\" name=\"pw\">"
    "<input type=\"checkbox\" name=\"merken\" checked>"
    "<select name=\"land\"><option value=\"de\">Deutschland<option value=\"at\" selected>&Ouml;sterreich</select>"
    "<textarea name=\"text\">\nZeile 1\nZeile &lt;2&gt;</textarea>"
    "<button type=\"button\">nur mit Skript</button><button name=\"los\" value=\"1\">Ab <i>damit</i></button>"
    "</form><p>Ende</body></html>";

int main(void)
{
    static const char *urls[][3] = {
        { "https://a.de/x/y/z.html?q=1#f", "b.html", "https://a.de/x/y/b.html" },
        { "https://a.de/x/y/z.html", "../b.html", "https://a.de/x/b.html" },
        { "https://a.de/x/y/z.html", "../../../b.html", "https://a.de/b.html" },
        { "https://a.de/x/y/", "./c/", "https://a.de/x/y/c/" },
        { "https://a.de/x/y/z.html", "/root?a=b", "https://a.de/root?a=b" },
        { "https://a.de/x/y/z.html?old=1", "?new=2", "https://a.de/x/y/z.html?new=2" },
        { "https://a.de/x/y/z.html?old=1", "", "https://a.de/x/y/z.html?old=1" },
        { "https://a.de/x/y/z.html?old=1", "#top", "https://a.de/x/y/z.html?old=1" },
        { "https://a.de", "wp-login.php", "https://a.de/wp-login.php" },
        { "https://a.de", "", "https://a.de/" },
        { "http://a.de:8080/wp-admin/", "post.php?post=3&action=edit",
          "http://a.de:8080/wp-admin/post.php?post=3&action=edit" },
        { "http://a.de:8080/wp-admin/x.php?a=../../b", "edit.php",
          "http://a.de:8080/wp-admin/edit.php" },
        { "https://a.de/x/", "//cdn.b.de/s.css", "https://cdn.b.de/s.css" },
        { "https://a.de/x/", "HTTP://B.de/Pfad mit Leer", "HTTP://B.de/Pfad%20mit%20Leer" },
        { "https://a.de/x/", "mailto:a@b.de", NULL },
        { "https://a.de/x/", "javascript:void(0)", NULL },
        { "https://a.de/x/y?u=http://z/", "a?next=/b/../c", "https://a.de/x/a?next=/b/../c" },
        { "https://a.de/a/b/c", "..", "https://a.de/a/" },
        { "https://a.de?x=1", "y", "https://a.de/y" },
    };
    char out[URL_SIZE], note[400];

    for (unsigned i = 0; i < sizeof(urls) / sizeof(urls[0]); i++) {
        bool ok = url_resolve(urls[i][0], urls[i][1], out, sizeof(out));
        snprintf(note, sizeof(note), "%s + %s -> %s", urls[i][0], urls[i][1], ok ? out : "-");
        check(urls[i][2] ? ok && strcmp(out, urls[i][2]) == 0 : !ok, note);
    }
    url_encode(out, sizeof(out), "a b&c=\xC3\xA4\n");
    check(strcmp(out, "a+b%26c%3D%C3%A4%0D%0A") == 0, "Formulardaten kodieren");
    bool secure;
    char host[64];
    const char *path;
    check(url_split("HTTPS://Shop.Example:8443/a?b", &secure, host, sizeof(host), &path) &&
          secure && strcmp(host, "shop.example") == 0 && strcmp(path, "/a?b") == 0,
          "Adresse zerlegen");

    /* Eine Seite mit allem, was vorkommt */
    page_t *p = html_layout(sample, sizeof(sample) - 1, "https://a.de/x/y/z.html", 60);
    check(p && !p->failed, "Seite aufgebaut");
    check(p->title && strcmp(p->title, "Test & Probe") == 0, "Titel");
    check(strcmp(line_text(p, 0), "Titel") == 0, "Überschrift");
    check(p->heading_line == 0, "Zeile der Hauptüberschrift");
    check(strcmp(line_text(p, 2), "Ein fetter Text mit Link und Umlauten: \xC3\xA4\xC3\xB6\xC3\xBC \xE2\x82\xAC.") == 0,
          "Absatz mit Zeichenangaben");
    check(p->link_count == 1 && strcmp(p->links[0], "https://a.de/x/seite.html?a=1&b=2") == 0,
          "Link aufgelöst");
    check(strcmp(line_text(p, 4), "- eins") == 0 && strcmp(line_text(p, 5), "- zwei") == 0,
          "Liste");
    check(strcmp(line_text(p, 7), "A | B") == 0 && strcmp(line_text(p, 8), "1 | 2") == 0,
          "Tabelle");
    bool leaked = false;
    for (int i = 0; i < p->line_count; i++) {
        const char *t = line_text(p, i);
        for (const char *q = t; *q; q++)
            leaked = leaked || memcmp(q, "unsichtbar", 10) == 0 || memcmp(q, "nein", 4) == 0 ||
                     memcmp(q, "color", 5) == 0 || memcmp(q, "nur mit", 7) == 0;
    }
    check(!leaked, "Verstecktes, Skripte und Stylesheets bleiben draußen");
    check(p->form_count == 1 && p->forms[0].post &&
          strcmp(p->forms[0].action, "https://a.de/anmelden") == 0, "Formular");
    check(p->widget_count == 7, "sieben Felder");
    if (p->widget_count == 7) {
        const widget_t *w = p->widgets;
        check(w[0].kind == W_HIDDEN && strcmp(w[0].value, "x\"y") == 0 && w[0].line < 0,
              "verstecktes Feld");
        check(w[1].kind == W_TEXT && strcmp(w[1].value, "Erika") == 0 && w[1].line >= 0,
              "Textfeld");
        check(w[2].kind == W_PASSWORD && w[3].kind == W_CHECKBOX && w[3].checked &&
              strcmp(w[3].value, "on") == 0, "Passwort und Häkchen");
        check(w[4].kind == W_SELECT && w[4].option_count == 2 && w[4].selected == 1 &&
              strcmp(p->options[w[4].first_option + 1].value, "at") == 0 &&
              strcmp(p->options[w[4].first_option + 1].label, "\xC3\x96sterreich") == 0,
              "Auswahlliste");
        check(w[5].kind == W_TEXTAREA && strcmp(w[5].value, "Zeile 1\nZeile <2>") == 0,
              "mehrzeiliges Feld");
        check(w[6].kind == W_SUBMIT && strcmp(w[6].label, "Ab damit") == 0 &&
              strcmp(w[6].name, "los") == 0, "Knopf");
    }
    check(p->anchor_count == 1 && strcmp(p->anchors[0].id, "oben") == 0, "Sprungmarke");
    page_free(p);

    p = html_plain_text("a\n  b\tc", 7, 40);
    check(p && p->line_count >= 2 && strcmp(line_text(p, 0), "a") == 0, "reiner Text");
    page_free(p);

    /* Beschädigte Seiten: abschneiden, Bytes kippen, Stücke verdoppeln */
    unsigned seed = 12345;
    char *broken = malloc(3 * sizeof(sample));
    for (int round = 0; round < 3000; round++) {
        size_t len = sizeof(sample) - 1;
        memcpy(broken, sample, len);
        seed = seed * 1103515245u + 12345u;
        unsigned r = seed >> 8;
        if (round % 3 == 0) {
            len = r % len;
        } else if (round % 3 == 1) {
            for (int k = 0; k < 12; k++) {
                seed = seed * 1103515245u + 12345u;
                broken[(seed >> 8) % len] = (char)(seed >> 20);
            }
        } else {
            size_t from = r % len, n = (r >> 7) % (len - from);
            memcpy(broken + len, sample + from, n);
            memcpy(broken + len + n, sample + from, n);
            len += 2 * n;
        }
        p = html_layout(broken, len, "https://a.de/", 20 + (int)(r % 200));
        if (!p)
            failures++;
        page_free(p);
    }
    free(broken);
    check(1, "3000 beschädigte Seiten verarbeitet");

    printf("%d Prüfungen am Browser-Unterbau, %d Fehler\n", checks, failures);
    return failures != 0;
}
