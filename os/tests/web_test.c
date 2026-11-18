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

    /* Kästen neben dem Text: der Text fließt daran vorbei */
    static const char float_sheet[] = ".r { float: right; width: 40% } .l { float: left } .unten { clear: both }";
    static const char float_page[] =
        "<div class=r>Kasten rechts mit etwas Text darin, der im Kasten umbricht und so drei oder vier Zeilen ergibt</div>"
        "<p>Erster Absatz neben dem Kasten. Er ist lang genug, um mehrere Zeilen zu ergeben, damit man "
        "sieht, dass die Zeilen neben dem Kasten kürzer sind als die darunter. Noch ein Satz mehr, und "
        "noch einer, bis der Text sicher unter den Kasten reicht und wieder breit wird. Und noch "
        "ein wenig mehr Text, damit es wirklich reicht, ganz sicher, wirklich.</p>"
        "<h1>Titel</h1>"
        "<p><span class=l>LINKS</span>Text neben dem linken Kasten.</p>"
        "<p class=unten>Darunter</p>";
    css_sheet_t *float_css = css_create(600);
    css_add(float_css, float_sheet, sizeof(float_sheet) - 1);
    text.css = float_css;
    p = html_layout_rich(float_page, sizeof(float_page) - 1, "https://a.de/", 600, NULL, &text);
    check(p && !p->failed, "Seite mit Kästen neben dem Text gesetzt");
    if (p) {
        int box_x = -1, box_first = -1, box_last = -1, narrow = 0, wide = 0, line_left_box = -1;
        int x_beside = -1, line_below = -1, x_below = -1;
        bool overlap = false, aside_on_filler = true;
        for (int i = 0; i < p->line_count; i++) {
            const line_t *l = &p->lines[i];
            int text_end = 0, aside_start = 100000;
            for (uint32_t k = 0; k < l->count; k++) {
                const span_t *sp = &p->spans[l->first + k];
                const char *t = p->pool + sp->text;
                if (sp->style & STYLE_ASIDE) {
                    if (sp->column > 300) {                 /* der rechte Kasten */
                        if (box_first < 0) box_first = i;
                        box_last = i;
                        if (box_x < 0 || sp->column < box_x) box_x = sp->column;
                        if (sp->column < aside_start) aside_start = sp->column;
                    }
                    if (IS("LINKS")) line_left_box = i;
                } else {
                    if (sp->column + sp->width > text_end) text_end = sp->column + sp->width;
                    if (IS("Text neben")) x_beside = sp->column;
                    if (IS("Darunter")) { line_below = i; x_below = sp->column; }
                }
            }
            if (aside_start < 100000 && text_end > aside_start)
                overlap = true;
            if (text_end > 0 && i <= box_last && box_first >= 0 && text_end <= box_x)
                narrow++;
            if (text_end > 420)
                wide++;
            if (l->filler && l->tall)
                aside_on_filler = false;
        }
        check(box_first == 0 && box_last >= 2 && box_x >= 600 * 55 / 100,
              "der Kasten steht rechts in der verlangten Breite");
        check(narrow >= 2 && !overlap, "Text neben dem Kasten bleibt links davon");
        check(wide >= 1, "unter dem Kasten wird der Text wieder breit");
        check(line_left_box >= 0 && x_beside > 5 * 7,
              "linker Kasten: der Text beginnt rechts daneben");
        check(line_below > line_left_box && x_below == 0, "clear: beginnt unterhalb, wieder am Rand");
        check(aside_on_filler, "Zeilenaufbau bleibt stimmig");
    }
    page_free(p);
    css_free(float_css);

    /*
     * Kästen, die eine Regel an einem Attribut erkennt (so stehen Wikipedias
     * Bilder neben dem Text), Absatzabstände daneben, und Tabellen: Eine
     * schmale steht neben dem Kasten, eine breite beginnt darunter.
     */
    static const char side_sheet[] = "figure[typeof~='mw:File/Thumb'] { float: right; clear: right; width: 250px }";
    static const char side_page[] =
        "<figure class=x typeof=\"mw:File/Thumb\">KASTEN eins zwei drei vier fünf sechs sieben acht neun "
        "zehn elf zwölf dreizehn vierzehn fünfzehn sechzehn siebzehn achtzehn neunzehn zwanzig und "
        "immer weiter mit Wörtern, damit der Kasten hoch genug wird: einundzwanzig zweiundzwanzig "
        "dreiundzwanzig vierundzwanzig fünfundzwanzig sechsundzwanzig siebenundzwanzig achtundzwanzig "
        "neunundzwanzig dreißig einunddreißig zweiunddreißig dreiunddreißig vierunddreißig "
        "fünfunddreißig sechsunddreißig siebenunddreißig achtunddreißig neununddreißig vierzig "
        "einundvierzig zweiundvierzig dreiundvierzig vierundvierzig fünfundvierzig</figure>"
        "<p>Absatz eins.</p><p>Absatz zwei.</p>"
        "<table><tr><td>KLEIN</td><td>b</td></tr><tr><td>c</td><td>ZELLE</td></tr></table>"
        "<p>Absatz drei.</p>"
        "<table><tr><td>BREIT</td><td>eine Zelle mit so viel Text, dass die Tabelle neben dem Kasten "
        "umbrechen müsste und deshalb erst darunter beginnt, in voller Breite</td></tr></table>"
        "<figure typeof=\"mw:File/Frameless\">OHNE</figure>"
        "<p>Ende</p>";
    float_css = css_create(600);
    css_add(float_css, side_sheet, sizeof(side_sheet) - 1);
    text.css = float_css;
    p = html_layout_rich(side_page, sizeof(side_page) - 1, "https://a.de/", 600, NULL, &text);
    check(p && !p->failed, "Seite mit Kasten per Attribut-Regel gesetzt");
    if (p) {
        int box_x = 100000, box_first = -1, box_last = -1;
        int line_1 = -1, line_2 = -1, line_3 = -1, line_small = -1, line_cell = -1, line_wide = -1;
        int x_small = -1, x_cell = -1, x_wide = -1, line_plain = -1;
        bool overlap = false, plain_aside = false;
        for (int i = 0; i < p->line_count; i++) {
            const line_t *l = &p->lines[i];
            int text_end = 0, aside_start = 100000;
            for (uint32_t k = 0; k < l->count; k++) {
                const span_t *sp = &p->spans[l->first + k];
                const char *t = p->pool + sp->text;
                if (sp->style & STYLE_ASIDE) {
                    if (box_first < 0) box_first = i;
                    box_last = i;
                    if (sp->column < box_x) box_x = sp->column;
                    if (sp->column < aside_start) aside_start = sp->column;
                    if (IS("OHNE")) plain_aside = true;
                    continue;
                }
                if (sp->column + sp->width > text_end) text_end = sp->column + sp->width;
                if (IS("Absatz eins")) line_1 = i;
                if (IS("Absatz zwei")) line_2 = i;
                if (IS("Absatz drei")) line_3 = i;
                if (IS("KLEIN")) { line_small = i; x_small = sp->column; }
                if (IS("ZELLE")) { line_cell = i; x_cell = sp->column; }
                if (IS("BREIT")) { line_wide = i; x_wide = sp->column; }
                if (IS("OHNE")) line_plain = i;
            }
            if (text_end > aside_start)
                overlap = true;
        }
        check(box_first == 0 && box_last >= 12 && box_x >= 600 * 55 / 100 && box_x < 600,
              "Regel mit Attribut: der Kasten steht rechts");
        check(line_1 == 0 && line_2 == 2, "neben dem Kasten bleibt es bei einer Leerzeile zwischen Absätzen");
        check(line_small == line_2 + 2 && line_cell == line_small + 1 && line_cell < box_last &&
              x_small == 0 && x_cell > x_small && x_cell < box_x,
              "schmale Tabelle steht neben dem Kasten");
        check(line_3 == line_cell + 2 && line_3 < box_last, "nach der Tabelle geht der Text daneben weiter");
        check(line_wide > box_last && x_wide == 0, "breite Tabelle beginnt unter dem Kasten");
        check(!overlap, "nichts ragt in den Kasten hinein");
        check(line_plain > line_wide && !plain_aside, "anderer Attributwert: kein Kasten");
    }
    page_free(p);
    css_free(float_css);

    /*
     * Seitenaufbau aus Kästen, wie ihn etwa die WordPress-Verwaltung hat: ein
     * Menü links in fester Breite, der Inhalt als Kasten "100 % breit"
     * daneben - mit eigenen Kästen darin. Dazu Kästen nebeneinander, und was
     * Seiten nur für Vorleseprogramme oder zum Aufklappen bereithalten.
     */
    static const char layout_sheet[] =
        "#menue { float: left; width: 120px } #inhalt { float: left; width: 100% } "
        ".suche { float: right; width: 100px } .filter { float: left } .zeile { clear: both } "
        ".vorleser { position: absolute; clip: rect(1px,1px,1px,1px) } .oben { top: -1000em } "
        ".links { left: -9999em } li.punkt { float: left } input { text-align: center } "
        ".a { float: left; width: 100px } .b { float: left; width: 100px } "
        ".c { float: right; width: 100px; clear: right }";
    static const char layout_page[] =
        "<a class=vorleser href=#x>VORLESER</a><ul class=oben><li>OBEN</li></ul>"
        "<div id=menue>MENUE eins<br>zwei<br>drei<br>vier<br>fünf<br>sechs<br>sieben<br>acht<br>neun<br>zehn</div>"
        "<div id=inhalt><h1>TITEL</h1>"
        "<ul class=filter><li class=punkt>ALLE</li><li class=punkt>ENTWURF</li></ul>"
        "<p class=suche>SUCHE</p>"
        "<div class=zeile>ZEILE darunter <span class=links>AKTION</span></div>"
        "<form><input name=f value=FELD> NEBEN</form>"
        "<a name=marke></a><p>ABSATZ im Inhalt.</p></div>"
        "<p>FUSS</p>"
        "<div class=a>KASTEN-A<br>a2<br>a3</div><div class=b>KASTEN-B</div>"
        "<div class=c>KASTEN-C<br>c2<br>c3<br>c4</div><div class=c>KASTEN-D</div>"
        "<p>DANEBEN text</p>";
    float_css = css_create(800);
    css_add(float_css, layout_sheet, sizeof(layout_sheet) - 1);
    text.css = float_css;
    p = html_layout_rich(layout_page, sizeof(layout_page) - 1, "https://a.de/", 800, NULL, &text);
    check(p && !p->failed, "Seite aus Kästen gesetzt");
    if (p) {
        struct { const char *word; int line, x; } at[] = {
            { "VORLESER", -1, -1 }, { "OBEN", -1, -1 }, { "MENUE", -1, -1 }, { "zehn", -1, -1 },
            { "TITEL", -1, -1 }, { "ALLE", -1, -1 }, { "ENTWURF", -1, -1 }, { "SUCHE", -1, -1 },
            { "ZEILE", -1, -1 }, { "AKTION", -1, -1 }, { "NEBEN", -1, -1 }, { "ABSATZ", -1, -1 },
            { "FUSS", -1, -1 }, { "KASTEN-A", -1, -1 }, { "KASTEN-B", -1, -1 },
            { "KASTEN-C", -1, -1 }, { "KASTEN-D", -1, -1 }, { "DANEBEN", -1, -1 }, { "a3", -1, -1 },
        };
        enum { VORLESER, OBEN, MENUE, ZEHN, TITEL, ALLE, ENTWURF, SUCHE, ZEILE, AKTION, NEBEN, ABSATZ,
               FUSS, KASTEN_A, KASTEN_B, KASTEN_C, KASTEN_D, DANEBEN, A3, WORDS };
        for (int i = 0; i < p->line_count; i++) {
            const line_t *l = &p->lines[i];
            for (uint32_t k = 0; k < l->count; k++) {
                const span_t *sp = &p->spans[l->first + k];
                const char *t = p->pool + sp->text;
                for (int w = 0; w < WORDS; w++) {
                    size_t n = strlen(at[w].word);
                    for (size_t from = 0; at[w].line < 0 && from + n <= sp->bytes; from++) {
                        if (memcmp(t + from, at[w].word, n) == 0) {
                            at[w].line = i;
                            at[w].x = sp->column;   /* Beginn des Stücks, in dem das Wort steht */
                        }
                    }
                }
            }
        }
        int marke = -1;
        for (int i = 0; i < p->anchor_count; i++) {
            if (strcmp(p->anchors[i].id, "marke") == 0)
                marke = p->anchors[i].line;
        }
        check(at[VORLESER].line < 0 && at[OBEN].line < 0,
              "beschnitten oder nach oben aus dem Bild geschoben: nicht zu sehen");
        check(at[AKTION].line >= 0, "nach links aus dem Bild geschoben: bleibt erreichbar");
        check(at[MENUE].line == 0 && at[MENUE].x == 0 && at[ZEHN].line == 9, "Menü steht links");
        check(at[TITEL].line == 0 && at[TITEL].x == 134,
              "Kasten mit 100 % Breite steht schmaler neben dem Menü statt darunter");
        check(at[ALLE].line == at[ENTWURF].line && at[ALLE].line == at[TITEL].line + 2 &&
              at[ALLE].x >= 134 && at[ALLE].x < 200,
              "Listenpunkte mit float stehen nebeneinander");
        check(at[SUCHE].line == at[ALLE].line && at[SUCHE].x == 700,
              "Kasten im Kasten: rechts in derselben Zeile");
        check(at[ZEILE].line == at[ALLE].line + 1 && at[ZEILE].x == 134,
              "clear im Kasten: unter dessen Kästen, nicht unter dem Menü");
        check(p->widget_count == 1 && p->widgets[0].line == at[NEBEN].line &&
              p->widgets[0].column == 134 && at[NEBEN].x > 134,
              "Formularfeld im Kasten; seine Ausrichtung verschiebt die Zeile nicht");
        if (!(marke >= at[NEBEN].line && marke <= at[ABSATZ].line))
            printf("  Marke in Zeile %d, NEBEN %d, ABSATZ %d\n", marke, at[NEBEN].line, at[ABSATZ].line);
        check(marke >= at[NEBEN].line && marke <= at[ABSATZ].line,
              "Sprungmarke im Kasten zeigt auf ihre Zeile");
        check(at[FUSS].line == at[ABSATZ].line + 1 && at[FUSS].x == 134,
              "wo neben den Kästen kein Platz ist, geht der Text erst darunter weiter");
        check(at[KASTEN_A].line > at[FUSS].line && at[KASTEN_A].x == 0 &&
              at[KASTEN_B].line == at[KASTEN_A].line && at[KASTEN_B].x == 114,
              "zwei Kästen links stehen nebeneinander");
        check(at[KASTEN_C].line == at[KASTEN_A].line && at[KASTEN_C].x == 700 &&
              at[KASTEN_D].line == at[KASTEN_C].line + 4 && at[KASTEN_D].x == 700,
              "Kasten mit clear steht unter dem vorigen derselben Seite");
        check(at[DANEBEN].line >= at[KASTEN_A].line && at[DANEBEN].line <= at[A3].line &&
              at[DANEBEN].x >= 114 && at[DANEBEN].x < 600,
              "clear eines Kastens schiebt den Text nicht nach unten");
    }
    page_free(p);

    /* Ein Kasten in voller Breite ohne Nachbarn ist ein gewöhnlicher Block */
    static const char full_page[] =
        "<div style=\"float:left;width:100%\"><h1>GROSS</h1><p>Text im Block.</p></div><p>Danach</p>";
    p = html_layout_rich(full_page, sizeof(full_page) - 1, "https://a.de/", 800, NULL, &text);
    check(p && !p->failed && p->line_count >= 4 && p->lines[0].tall && p->lines[1].filler &&
          p->lines[0].count == 1 && !(p->spans[p->lines[0].first].style & STYLE_ASIDE),
          "Kasten in voller Breite: wie ein Block, Überschrift in voller Größe");
    page_free(p);
    text.css = NULL;
    css_free(float_css);

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

    /* HTML5: aufklappbare Abschnitte, Dialoge, Balken, Medien, Platzhalter - ohne und mit Skripten */
    static const char html5_page[] =
        "<header><h1>Kopf</h1><nav><a href='#a'>Eins</a></nav></header><main><article id=a>"
        "<p>Mit <mark>Marker</mark> und <time>heute</time>.</p>"
        "<details><summary>Mehr</summary><p>Verborgenes</p></details>"
        "<details open><summary>Offen</summary><p>Sichtbares</p></details>"
        "<p>Stand <progress value=30 max=100>dreissig</progress> und <meter value=0.75>viel</meter>"
        " <progress>unbestimmt</progress></p>"
        "<dialog>Geschlossen</dialog><dialog open>Geoeffnet</dialog><template><p>Vorlage</p></template>"
        "<p hidden>Unsichtbar</p></article>"
        "<form action=/x><input name=q placeholder='Suchwort'><input type=email name=m>"
        "<textarea name=t placeholder='Dein Text'></textarea><button type=button>Nur mit Skript</button>"
        "<input type=button value='Auch nur mit Skript'><input type=reset><button>Senden</button></form>"
        "<p><input type=checkbox id=frei> <input type=checkbox role=button id=menue></p>"
        "<video controls><source src='/filme/film.mp4' type='video/mp4'>Kein Video</video>"
        "<audio src='ton.mp3'>Kein Ton</audio><canvas>Keine Zeichenflaeche</canvas>"
        "<iframe src='/karte.html' title='Karte'></iframe><iframe src='javascript:void(0)'></iframe>"
        "<object data=x.swf>Ersatz</object>"
        "<p><a href='javascript:tuwas()'>Skript-Link</a> <a href='#'>Leerer Anker</a></p></main>";
    text.css = NULL;
    for (int with_scripts = 0; with_scripts < 2; with_scripts++) {
        char all[4000] = "";
        text.scripts = with_scripts;
        p = html_layout_rich(html5_page, sizeof(html5_page) - 1, "https://a.de/dir/seite.html", 900,
                             &pictures, &text);
        size_t used = 0;
        for (int i = 0; p && i < p->line_count && used + 700 < sizeof(all); i++)
            used += (size_t)snprintf(all + used, sizeof(all) - used, "%s\n", line_text(p, i));
        const char *mode = with_scripts ? "mit Skripten" : "ohne Skripte";
        #define H5(cond, what) snprintf(note, sizeof(note), "HTML5 %s: %s", mode, what); check(p && (cond), note)
        H5(strstr(all, "Kopf") && strstr(all, "Eins") && strstr(all, "Marker") && strstr(all, "heute"),
           "Abschnitte, mark und time erscheinen");
        H5(strstr(all, "Sichtbares") && strstr(all, "\xE2\x96\xBE Offen"), "offenes details mit Dreieck nach unten");
        if (with_scripts) {
            H5(!strstr(all, "Verborgenes") && strstr(all, "\xE2\x96\xB8 Mehr"),
               "zugeklapptes details zeigt nur die Überschrift");
        } else {
            H5(strstr(all, "Verborgenes") && strstr(all, "\xE2\x96\xBE Mehr"),
               "details steht offen (aufklappen ginge nur mit Skripten)");
        }
        H5(strstr(all, "\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x91") && strstr(all, "30 %") &&
           !strstr(all, "dreissig"), "progress als Balken mit Prozent");
        H5(strstr(all, "75 %") && !strstr(all, "viel"), "meter als Balken");
        H5(strstr(all, "[l\xC3\xA4uft ...]") && !strstr(all, "unbestimmt"), "progress ohne Wert");
        H5(strstr(all, "Geoeffnet") && !strstr(all, "Geschlossen"), "dialog nur, wenn er offen ist");
        H5(!strstr(all, "Vorlage") && !strstr(all, "Unsichtbar"), "template und hidden bleiben weg");
        H5(strstr(all, "[Video: film.mp4]") && !strstr(all, "Kein Video"), "Video: Hinweis aus der source-Angabe");
        H5(strstr(all, "[Tonaufnahme: ton.mp3]") && !strstr(all, "Kein Ton"), "Ton: Hinweis");
        H5(strstr(all, "[Eingebettete Seite: Karte]"), "iframe: Hinweis mit Titel");
        H5(strstr(all, "Keine Zeichenflaeche") && strstr(all, "Ersatz"), "canvas und object zeigen ihren Ersatzinhalt");
        bool video = false, karte = false, script_link = false;
        int iframes = 0;
        for (int i = 0; p && i < p->link_count; i++) {
            video |= strcmp(p->links[i], "https://a.de/filme/film.mp4") == 0;
            karte |= strcmp(p->links[i], "https://a.de/karte.html") == 0;
            script_link |= strcmp(p->links[i], "javascript:tuwas()") == 0;
            iframes += strstr(p->links[i], "karte") != NULL;
        }
        H5(video && karte && iframes == 1, "Hinweise führen zur Datei bzw. Seite");
        H5(script_link, "javascript:-Link bleibt als Link erhalten");
        int buttons = 0, submits = 0, boxes = 0, with_hint = 0, labelled = 0;
        for (int i = 0; p && i < p->widget_count; i++) {
            const widget_t *w = &p->widgets[i];
            buttons += w->kind == W_BUTTON;
            labelled += w->kind == W_BUTTON && w->label &&
                        (strcmp(w->label, "Nur mit Skript") == 0 ||
                         strcmp(w->label, "Auch nur mit Skript") == 0 ||
                         strcmp(w->label, "Zur\xC3\xBC" "cksetzen") == 0);
            submits += w->kind == W_SUBMIT;
            boxes += w->kind == W_CHECKBOX;
            with_hint += w->placeholder && (strcmp(w->placeholder, "Suchwort") == 0 ||
                                            strcmp(w->placeholder, "Dein Text") == 0);
        }
        H5(with_hint == 2, "placeholder an Eingabefeld und Textfeld");
        H5(submits == 1, "ein Knopf zum Absenden");
        if (with_scripts) {
            H5(buttons == 3 && labelled == 3, "Knöpfe für Skripte erscheinen, mit Beschriftung");
            H5(boxes == 1, "Ankreuzfeld ohne Namen erscheint, der Menüschalter nicht");
        } else {
            H5(buttons == 0 && !strstr(all, "Nur mit Skript"), "Knöpfe für Skripte bleiben weg");
            H5(boxes == 0, "Ankreuzfelder ohne Namen bleiben weg");
        }
        page_free(p);
    }
    text.scripts = false;

    /* Beschädigte Seiten: abschneiden, Bytes kippen, Stücke verdoppeln */
    static const char fuzz_sheet[] = ".r, div, table { float: right } .l, span, img { float: left } p { clear: left } "
                                     "figure[typeof~='mw:File/Thumb'], td[x] { float: right; clear: right } "
                                     "#menue { float: left; width: 120px } #inhalt { float: left; width: 100% } "
                                     ".suche { float: right; width: 30% } li { float: left } ul { width: 7em }";
    css_add(sheet, fuzz_sheet, sizeof(fuzz_sheet) - 1);
    text.css = sheet;
    unsigned seed = 12345;
    char *broken = malloc(3 * (sizeof(sample) + sizeof(table_page) + sizeof(float_page) +
                               sizeof(side_page) + sizeof(layout_page) + sizeof(html5_page)));
    for (int round = 0; round < 10800; round++) {
        int which = round % 6;
        const char *source = which == 3 ? table_page : which == 4 ? float_page
                           : which == 2 ? side_page : which == 1 ? layout_page
                           : which == 5 ? html5_page : sample;
        size_t len = which == 3 ? sizeof(table_page) - 1 : which == 4 ? sizeof(float_page) - 1
                   : which == 2 ? sizeof(side_page) - 1 : which == 1 ? sizeof(layout_page) - 1
                   : which == 5 ? sizeof(html5_page) - 1 : sizeof(sample) - 1;
        text.scripts = (round / 6) % 2;
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
    check(1, "10800 beschädigte Seiten verarbeitet");

    printf("%d Prüfungen am Browser-Unterbau, %d Fehler\n", checks, failures);
    return failures != 0;
}
