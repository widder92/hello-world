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

#include "../kernel/css.h"
#include "../kernel/html.h"
#include "../kernel/url.h"

void *memset(void *, int, size_t);
void *memcpy(void *, const void *, size_t);
int memcmp(const void *, const void *, size_t);
int strcmp(const char *, const char *);
char *strstr(const char *, const char *);
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

/* Auskunft über Bilder für den Test: zwei sind "geladen", eines ist kaputt */
static picture_state_t picture_stub(void *ctx, const char *url, int *width, int *height)
{
    (void)ctx;
    if (strstr(url, "da.png")) {
        *width = 400;
        *height = 200;
        return PICTURE_READY;
    }
    if (strstr(url, "kaputt"))
        return PICTURE_BROKEN;
    return PICTURE_UNKNOWN;
}

/* Schriftauskunft für den Test: 7 Pixel je Zeichen, fett 9, Überschrift 1 doppelt, Raster 8 */
static int width_stub(void *ctx, const char *text, size_t len, uint8_t style, uint8_t size)
{
    (void)ctx;
    int chars = 0;
    for (size_t i = 0; i < len; i++) {
        if (((unsigned char)text[i] & 0xC0) != 0x80)
            chars++;
    }
    if (style & STYLE_MONO)
        return chars * 8;
    if (size >= 26)
        return chars * 14;
    return chars * ((style & STYLE_BOLD) ? 9 : 7);
}

/* Merkt sich, welche Stylesheets html_find_styles() gemeldet hat */
static int found_count;
static char found_text[400];

static void note_style(void *ctx, bool linked, const char *text, size_t len)
{
    (void)ctx;
    size_t used = strlen(found_text);
    snprintf(found_text + used, sizeof(found_text) - used, "|%c:%.*s", linked ? 'L' : 'S',
             (int)len, text);
    found_count++;
}

static void note_style_quiet(void *ctx, bool linked, const char *text, size_t len)
{
    (void)ctx;
    (void)linked;
    (void)text;
    (void)len;
}

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

    /* Bilder als Blöcke: Größe, Platzhalter, Zählpixel, doppelt genannte Bilder */
    static const char pictures_page[] =
        "<p>Davor</p>"
        "<img src=\"da.png\" alt=\"Geladen\">"                       /* 400x200 -> auf 320 Pixel */
        "<p>Mitte <img src=\"pixel.gif\" width=1 height=1> weiter</p>"
        "<a href=\"ziel.html\"><img src=\"kommt.jpg\" alt=\"Kommt noch\" width=\"160\" height=\"90\"></a>"
        "<img src=\"unbekannt.jpg\">"
        "<img data-src=\"faul.png\" src=\"data:image/gif;base64,R0lG\" alt=\"Faul\">"
        "<noscript><img src=\"faul.png\" alt=\"Faul\"></noscript>"
        "<p>Kaputt: <img src=\"kaputt.png\" alt=\"Ersatz\"> Ende</p>"
        "<ul><li><img src=\"da.png\" alt=\"In Liste\"> Punkt</li></ul>";
    html_pictures_t pictures = { .cell_width = 8, .line_height = 18, .max_rows = 30,
                                 .lookup = picture_stub };
    p = html_layout_pictures(pictures_page, sizeof(pictures_page) - 1, "https://a.de/x/",
                             40, &pictures);
    check(p && !p->failed && p->image_count == 5, "fünf Bilder eingeplant (Zählpixel, Doppeltes und Kaputtes nicht)");
    if (p && p->image_count == 5) {
        const page_image_t *im = p->images;
        check(strcmp(im[0].url, "https://a.de/x/da.png") == 0 && im[0].ready &&
              im[0].width == 320 && im[0].height == 160 && im[0].rows == 9,
              "geladenes Bild: auf die Breite verkleinert, neun Zeilen");
        bool rows_ok = true;
        for (int r = 0; r < im[0].rows; r++) {
            const line_t *l = &p->lines[im[0].line + r];
            rows_ok = rows_ok && l->image == 0 && l->image_row == r && l->count == 0;
        }
        check(rows_ok, "die Zeilen des Blocks verweisen auf das Bild");
        check(strcmp(line_text(p, im[0].line - 1), "") == 0 || strcmp(line_text(p, im[0].line - 2), "Davor") == 0,
              "Text davor bleibt stehen");
        check(!im[1].ready && im[1].width == 160 && im[1].height == 90 && im[1].rows == 5 &&
              im[1].link >= 0 && strcmp(p->links[im[1].link], "https://a.de/x/ziel.html") == 0,
              "noch nicht geladen, Größe angegeben: Platz freigehalten, Link gemerkt");
        check(!im[2].ready && im[2].rows == 1 && im[2].width == 0 && !im[2].alt,
              "noch nicht geladen, Größe unbekannt: eine Zeile");
        check(strcmp(im[3].url, "https://a.de/x/faul.png") == 0 && im[3].alt &&
              strcmp(im[3].alt, "Faul") == 0, "nachladendes Bild: Adresse aus data-src, nur einmal");
        check(im[4].column == 2 && im[4].width == 304, "Bild in einer Liste: eingerückt und schmaler");
        bool found = false, bullet_alone = false;
        for (int i = 0; i < p->line_count; i++) {
            if (strstr(line_text(p, i), "Kaputt: [Bild: Ersatz] Ende"))
                found = true;
            if (strcmp(line_text(p, i), "-") == 0 || strcmp(line_text(p, i), "- ") == 0)
                bullet_alone = true;
        }
        check(found, "nicht ladbares Bild: Ersatztext im Lauftext");
        check(!bullet_alone, "kein einsamer Listenpunkt über dem Bild");
    }
    page_free(p);

    /* Echte Schrift: Breiten in Pixeln, große Überschriften, Sonderzeichen bleiben */
    static const char rich_page[] =
        "<h1>Titel</h1><p>Ein \xE2\x80\x9EZitat\xE2\x80\x9C mit <b>fettem</b> und <i>schr\xC3\xA4gem</i> Text, "
        "der lang genug ist, um umzubrechen, damit die Breite gepr\xC3\xBC" "ft werden kann.</p>"
        "<ul><li>Punkt</li></ul><p>Feld: <input name=a size=10> <code>abc</code></p>"
        "<h3>Klein</h3>";
    html_text_t text = { .cell_width = 8, .width = width_stub, .base_size = 15,
                         .heading_size = { 26, 21, 17 }, .text_color = 0x222630,
                         .link_color = 0x1E5ABE, .page_color = 0xFCFCFA };
    p = html_layout_rich(rich_page, sizeof(rich_page) - 1, "https://a.de/", 300, NULL, &text);
    check(p && !p->failed && p->pixels, "Seite in echter Schrift gesetzt");
    if (p) {
        bool widths_ok = true, quote_kept = false, italic = false, mono = false, bullet = false;
        int tall_lines = 0, fillers = 0, wrapped = 0;
        for (int i = 0; i < p->line_count; i++) {
            const line_t *l = &p->lines[i];
            tall_lines += l->tall;
            fillers += l->filler;
            if (l->tall)
                widths_ok = widths_ok && i + 1 < p->line_count && p->lines[i + 1].filler;
            if (l->count > 0 && i > 2 && i < 8)
                wrapped++;
            for (uint32_t k = 0; k < l->count; k++) {
                const span_t *sp = &p->spans[l->first + k];
                widths_ok = widths_ok && sp->column + sp->width <= 300;
                const char *t = p->pool + sp->text;
                for (int at = 0; at + 3 <= sp->bytes; at++) {
                    if (memcmp(t + at, "\xE2\x80\x9E", 3) == 0)
                        quote_kept = true;
                }
                if (sp->bytes >= 3 && memcmp(t, "\xE2\x80\xA2", 3) == 0)
                    bullet = true;
                if (sp->style & STYLE_ITALIC)
                    italic = true;
                if ((sp->style & STYLE_MONO) && sp->width == 3 * 8)
                    mono = true;
                if (sp->size == 26)
                    widths_ok = widths_ok && sp->width == 5 * 14;     /* "Titel" in doppelter Breite */
            }
        }
        check(widths_ok, "kein Stück ragt über den Rand; Überschrift mit eigener Breite");
        check(tall_lines == 1 && fillers == 1, "Stufe 1 belegt zwei Zeilen, Stufe 3 eine");
        check(wrapped >= 2, "der Absatz bricht nach Pixelbreite um");
        check(quote_kept && bullet, "typografische Zeichen bleiben, Listenpunkt ist rund");
        check(italic && mono, "kursiv und Rasterschrift sind gekennzeichnet");
        check(p->widget_count == 1 && p->widgets[0].width == 12 && p->widgets[0].column % 1 == 0 &&
              p->widgets[0].column > 30, "Formularfeld: Breite in Zeichen, Platz in Pixeln");
    }
    page_free(p);

    /* Gestaltung: Stylesheet, style-Attribute, Lesbarkeit */
    static const char sheet_text[] =
        ".weg { display: none } .rot { color: #c00 } .mitte { text-align: center }\n"
        ".kasten { background: #e8f0fe } ul.menu { list-style: none } ul.menu li { display: inline }\n"
        "h2 { font-size: 16px; font-weight: normal } a { text-decoration: none }\n"
        ".hell { color: #fffff0 } .dunkel { background: #101418 } .gross { font-size: 32px }";
    static const char styled_page[] =
        "<p class=rot>Rot</p><p class=weg>Unsichtbar</p><p class=mitte>Mitte</p>"
        "<div class=kasten><p>Im Kasten</p></div>"
        "<ul class=menu><li>Eins</li><li>Zwei</li></ul>"
        "<h2>Klein</h2><p><a href=x>Link</a> <span style=\"color:#00f;background:yellow\">markiert</span></p>"
        "<p class=hell>Helle Schrift auf hellem Grund</p>"
        "<div class=dunkel><p>Dunkler Kasten ohne Schriftfarbe</p></div>"
        "<p class=gross>Gross</p><p style=\"display:none\">Auch weg</p>";
    css_sheet_t *sheet = css_create(300);
    css_add(sheet, sheet_text, sizeof(sheet_text) - 1);
    text.css = sheet;
    p = html_layout_rich(styled_page, sizeof(styled_page) - 1, "https://a.de/", 300, NULL, &text);
    check(p && !p->failed, "Seite mit Stylesheet gesetzt");
    if (p) {
        bool red = false, hidden = false, centered = false, boxed = false, menu_row = false;
        bool small_heading = false, plain_link = false, marked = false, rescued = false;
        bool light_on_dark = false, large = false, bullet = false;
        for (int i = 0; i < p->line_count; i++) {
            const line_t *l = &p->lines[i];
            const char *t = line_text(p, i);
            if (strstr(t, "nsichtbar") || strstr(t, "Auch weg"))
                hidden = true;
            if (strstr(t, "Eins Zwei"))
                menu_row = true;
            for (uint32_t k = 0; k < l->count; k++) {
                const span_t *sp = &p->spans[l->first + k];
                const char *text_at = p->pool + sp->text;
                if (sp->bytes >= 3 && memcmp(text_at, "\xE2\x80\xA2", 3) == 0)
                    bullet = true;
                if (sp->bytes == 3 && memcmp(text_at, "Rot", 3) == 0)
                    red = sp->color == (HTML_COLOR_SET | 0xCC0000);
                if (sp->bytes == 5 && memcmp(text_at, "Mitte", 5) == 0)
                    centered = sp->column == (300 - 5 * 7) / 2;
                if (sp->bytes >= 9 && memcmp(text_at, "Im Kasten", 9) == 0)
                    boxed = l->background == (HTML_COLOR_SET | 0xE8F0FE);
                if (sp->bytes == 5 && memcmp(text_at, "Klein", 5) == 0)
                    small_heading = sp->size == 15 && !(sp->style & STYLE_BOLD) && !l->tall;
                if (sp->bytes == 4 && memcmp(text_at, "Link", 4) == 0)
                    plain_link = (sp->style & STYLE_LINK) && !(sp->style & STYLE_UNDERLINE);
                if (sp->bytes == 8 && memcmp(text_at, "markiert", 8) == 0)
                    marked = sp->color == (HTML_COLOR_SET | 0x0000FF) &&
                             sp->background == (HTML_COLOR_SET | 0xFFFF00);
                if (sp->bytes >= 5 && memcmp(text_at, "Helle", 5) == 0)
                    rescued = sp->color == (HTML_COLOR_SET | 0x222630);
                if (sp->bytes >= 7 && memcmp(text_at, "Dunkler", 7) == 0)
                    light_on_dark = sp->color == (HTML_COLOR_SET | 0xF2F2F2) &&
                                    l->background == (HTML_COLOR_SET | 0x101418);
                if (sp->bytes == 5 && memcmp(text_at, "Gross", 5) == 0)
                    large = sp->size == 28 && l->tall;
            }
        }
        check(red && marked, "Farben aus Stylesheet und style-Attribut");
        check(!hidden, "display:none blendet aus");
        check(centered, "text-align: center rückt die Zeile in die Mitte");
        check(boxed, "Hintergrund eines Abschnitts gilt für seine Zeilen");
        check(menu_row && !bullet, "Liste ohne Punkte, Einträge nebeneinander");
        check(small_heading && large, "Schriftgrößen aus dem Stylesheet (mit Obergrenze)");
        check(plain_link, "Link ohne Unterstreichung");
        check(rescued && light_on_dark, "unlesbare Farbkombinationen werden korrigiert");
    }
    page_free(p);
    text.css = NULL;

    /* Tabellen: Zellen nebeneinander in Spalten */
    static const char table_page[] =
        "<p>Davor</p><table><caption>Titel der Tabelle</caption>"
        "<tr><th>Nr</th><th>Name</th><th>Text</th></tr>"
        "<tr><td>1</td><td>Anna</td><td>Ein langer Text, der in seiner Zelle mehrfach umbrechen muss, "
        "weil die Seite schmal ist und die anderen Spalten Platz brauchen</td></tr>"
        "<tr><td>22</td><td style=\"background:#ff0\">Bernd <a href=z id=marke>Link</a></td><td><input name=q size=6></td></tr>"
        "<tr><td colspan=2>Zusammen</td><td><table><tr><td>in A</td><td>in B</td></tr></table></td></tr>"
        "</table><p>Danach</p>";
    p = html_layout_rich(table_page, sizeof(table_page) - 1, "https://a.de/", 420, NULL, &text);
    check(p && !p->failed, "Seite mit Tabelle gesetzt");
    if (p) {
        int x_nr = -1, x_name = -1, x_text = -1, x_1 = -1, x_anna = -1, x_bernd = -1, x_zus = -1;
        int line_1 = -1, line_anna = -1, line_long = -1, line_in_a = -1, line_in_b = -1;
        int x_in_a = -1, x_in_b = -1, long_lines = 0, line_title = -1, line_before = -1;
        int x_long = -1;                /* Anfang der dritten Spalte (Kopfzellen stehen mittig) */
        bool box = false, right_edge = true;
        for (int i = 0; i < p->line_count; i++) {
            const line_t *l = &p->lines[i];
            for (uint32_t k = 0; k < l->count; k++) {
                const span_t *sp = &p->spans[l->first + k];
                const char *t = p->pool + sp->text;
                right_edge = right_edge && sp->column + sp->width <= 420 + 8;
                if (sp->bytes == 0 && sp->widget < 0 && sp->background == (HTML_COLOR_SET | 0xFFFF00))
                    box = true;
                #define IS(word) (sp->bytes >= sizeof(word) - 1 && memcmp(t, word, sizeof(word) - 1) == 0)
                if (IS("Nr")) x_nr = sp->column;
                if (IS("Name")) x_name = sp->column;
                if (IS("Text")) x_text = sp->column;
                if (IS("1") && sp->bytes == 1) { x_1 = sp->column; line_1 = i; }
                if (IS("Anna")) { x_anna = sp->column; line_anna = i; }
                if (IS("Bernd")) x_bernd = sp->column;
                if (IS("Zusammen")) x_zus = sp->column;
                if (IS("Ein langer")) { line_long = i; x_long = sp->column; }
                if (IS("in A")) { x_in_a = sp->column; line_in_a = i; }
                if (IS("in B")) { x_in_b = sp->column; line_in_b = i; }
                if (IS("Titel der")) line_title = i;
                if (IS("Davor")) line_before = i;
                if (line_long >= 0 && sp->column == x_long && i > line_long && l->count == 1 &&
                    sp->bytes > 0 && sp->widget < 0 && !IS("in A"))
                    long_lines = i - line_long + 1;
            }
        }
        check(x_1 >= 0 && x_1 < x_anna && line_1 == line_anna && line_anna == line_long,
              "Zellen einer Zeile stehen nebeneinander");
        check(x_name > x_nr && x_text > x_name && x_bernd == x_anna && x_zus == x_1,
              "Spalten stehen untereinander; colspan beginnt in der ersten Spalte");
        check(long_lines >= 2 && right_edge, "langer Text bricht in seiner Zelle um, nichts ragt hinaus");
        check(box, "farbige Zelle bekommt einen Kasten");
        check(line_in_a >= 0 && line_in_a == line_in_b && x_in_b > x_in_a && x_in_a == x_long,
              "verschachtelte Tabelle steht in ihrer Zelle");
        check(line_title > line_before && line_title < line_1, "Überschrift der Tabelle steht darüber");
        check(p->widget_count == 1 && p->widgets[0].line > line_anna &&
              p->widgets[0].column == x_long, "Formularfeld in der Zelle an der richtigen Stelle");
        check(p->anchor_count == 1 && p->anchors[0].line == p->widgets[0].line,
              "Sprungmarke zeigt auf die endgültige Zeile");
    }
    page_free(p);

    /* Tabellen im Zeichenraster bleiben zeilenweise */
    p = html_layout(table_page, sizeof(table_page) - 1, "https://a.de/", 60);
    bool linear = false;
    for (int i = 0; p && i < p->line_count; i++) {
        if (strstr(line_text(p, i), "Nr | Name | Text"))
            linear = true;
    }
    check(linear, "ohne echte Schrift: Tabelle zeilenweise mit Trennstrichen");
    page_free(p);

    /* Stylesheets einer Seite finden */
    static const char head[] =
        "<html><head><style>a{color:red}</style><!-- <style>x</style> -->"
        "<link rel=\"stylesheet\" href=\"/s/haupt.css?a=1&amp;b=2\">"
        "<link rel=stylesheet media=print href=druck.css><link rel=icon href=bild.ico>"
        "<script>var s = '<style>nein</style>';</script>"
        "<style media=\"screen\">b{color:blue}</style></head></html>";
    found_count = 0;
    found_text[0] = '\0';
    html_find_styles(head, sizeof(head) - 1, "https://a.de/x/y.html", note_style, NULL);
    check(found_count == 3 &&
          strcmp(found_text, "|S:a{color:red}|L:https://a.de/s/haupt.css?a=1&b=2|S:b{color:blue}") == 0,
          "Stylesheets: zwei eingebettete und ein verlinktes, in der Reihenfolge der Seite");

    /* Ohne Bildquelle bleibt alles beim Alten */
    p = html_layout(pictures_page, sizeof(pictures_page) - 1, "https://a.de/x/", 40);
    check(p && p->image_count == 0, "ohne Bildanzeige keine Bildblöcke");
    page_free(p);

    /* Beschädigte Seiten: abschneiden, Bytes kippen, Stücke verdoppeln */
    unsigned seed = 12345;
    char *broken = malloc(3 * (sizeof(sample) + sizeof(table_page)));
    for (int round = 0; round < 3000; round++) {
        const char *source = round % 4 == 3 ? table_page : sample;
        size_t len = round % 4 == 3 ? sizeof(table_page) - 1 : sizeof(sample) - 1;
        memcpy(broken, source, len);
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
            memcpy(broken + len, source + from, n);
            memcpy(broken + len + n, source + from, n);
            len += 2 * n;
        }
        if (round % 3 == 0)
            p = html_layout(broken, len, "https://a.de/", 20 + (int)(r % 200));
        else if (round % 3 == 1)
            p = html_layout_pictures(broken, len, "https://a.de/", 20 + (int)(r % 200),
                                     &pictures);
        else
            p = html_layout_rich(broken, len, "https://a.de/", 160 + (int)(r % 900), &pictures,
                                 &text);
        if (round % 7 == 0)
            html_find_styles(broken, len, "https://a.de/", note_style_quiet, NULL);
        if (!p)
            failures++;
        page_free(p);
    }
    free(broken);
    css_free(sheet);
    check(1, "3000 beschädigte Seiten verarbeitet");

    printf("%d Prüfungen am Browser-Unterbau, %d Fehler\n", checks, failures);
    return failures != 0;
}
