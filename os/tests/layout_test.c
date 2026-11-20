/*
 * Test für den Seitenaufbau mit Kastenmodell (kernel/layout.c) auf dem
 * Entwicklungsrechner: kleine Seiten setzen und nachmessen, wo ihre Kästen
 * und Zeilen stehen. Die Schrift ist dabei ausgedacht - jedes Zeichen 8 Pixel
 * breit, die Grundschrift 16 Pixel hoch -, damit sich alles ausrechnen lässt.
 * Am Ende viele absichtlich beschädigte Seiten: Sie dürfen den Seitenaufbau
 * nicht aus dem Tritt bringen.
 */
#include "../kernel/layout.h"

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
void call_on_stack(void (*fn)(void *), void *arg, void *top) { (void)top; fn(arg); }

static int checks, failures;
static bool quiet;

#define CHECK(condition) do { \
    checks++; \
    if (!(condition)) { \
        failures++; \
        printf("FEHLER Zeile %d: %s\n", __LINE__, #condition); \
    } \
} while (0)

static int char_width(void *ctx, const char *text, size_t len, uint8_t style, uint8_t size)
{
    (void)ctx; (void)style; (void)size;
    int n = 0;
    for (size_t i = 0; i < len; i++) {
        if (((uint8_t)text[i] & 0xC0) != 0x80)
            n++;
    }
    return n * 8;
}

static picture_state_t lookup(void *ctx, const char *url, int *w, int *h)
{
    (void)ctx;
    if (strstr(url, "fehlt"))
        return PICTURE_BROKEN;
    if (strstr(url, "spaeter"))
        return PICTURE_UNKNOWN;
    *w = 200;
    *h = 100;
    return PICTURE_READY;
}

static void style_found(void *ctx, bool linked, const char *text, size_t len)
{
    if (!linked)
        css_add(ctx, text, len);
}

static page_t *page;
static dom_doc *doc;
static css_sheet_t *css;

static void done(void)
{
    page_free(page);
    dom_free(doc);
    css_free(css);
    page = NULL;
    doc = NULL;
    css = NULL;
}

/* Setzt eine Seite in der Breite width; scripts: als liefen ihre Skripte. */
static bool lay(const char *html, int width, bool scripts)
{
    done();
    size_t len = strlen(html);
    css = css_create(width);
    css_set_viewport_height(css, 600);
    html_find_styles(html, len, "https://a.de/seite.html", style_found, css);
    doc = dom_parse(html, len);
    html_text_t text = { .cell_width = 8, .width = char_width, .base_size = 16, .text_color = 0x222630,
                         .link_color = 0x1E5ABE, .page_color = 0xFCFCFA, .css = css, .scripts = scripts };
    html_pictures_t pictures = { .cell_width = 1, .line_height = 20, .max_rows = 34, .lookup = lookup };
    layout_options_t options = { .width = width, .height = 600, .base_url = "https://a.de/seite.html",
                                 .css = css, .text = &text, .pictures = &pictures, .scripts = scripts,
                                 .geometry = true };
    page = doc ? layout_document(doc, &options) : NULL;
    return page != NULL;
}

/* Der erste Kasten mit diesem Hintergrund (oder NULL) */
static const box_t *colored(uint32_t rgb)
{
    for (int i = 0; page && i < page->box_count; i++) {
        if (page->boxes[i].background == (HTML_COLOR_SET | rgb))
            return &page->boxes[i];
    }
    return NULL;
}

static bool box_is(uint32_t rgb, int x, int y, int w, int h)
{
    const box_t *b = colored(rgb);
    bool ok = b && b->x == x && b->y == y && b->w == w && b->h == h;
    if (!ok && !quiet) {
        if (b)
            printf("  Kasten %06X: %d,%d %dx%d (erwartet %d,%d %dx%d)\n", rgb, b->x, b->y, b->w, b->h, x, y, w, h);
        else
            printf("  Kasten %06X fehlt\n", rgb);
    }
    return ok;
}

/* Das Stück, dessen Text mit text beginnt: seine Lage (x, Oberkante der Zeile); false: gibt es nicht */
static const span_t *find_span(const char *text, int *x, int *y)
{
    size_t n = strlen(text);
    for (int l = 0; page && l < page->line_count; l++) {
        const line_t *line = &page->lines[l];
        for (uint32_t i = 0; i < line->count; i++) {
            const span_t *s = &page->spans[line->first + i];
            if (s->bytes >= n && memcmp(page->pool + s->text, text, n) == 0) {
                if (x)
                    *x = s->column;
                if (y)
                    *y = line->y;
                return s;
            }
        }
    }
    return NULL;
}

static bool text_at(const char *text, int x, int y)
{
    int sx = -1, sy = -1;
    bool ok = find_span(text, &sx, &sy) && sx == x && sy == y;
    if (!ok && !quiet)
        printf("  \"%s\": %d,%d (erwartet %d,%d)\n", text, sx, sy, x, y);
    return ok;
}

static bool has_text(const char *text)
{
    return find_span(text, NULL, NULL) != NULL;
}

static void test_blocks(void)
{
    /* untereinander; aneinanderstoßende Abstände verschmelzen (es gilt der größere) */
    CHECK(lay("<body style='margin:0'><div style='margin:10px;height:50px;background:#111111'></div>"
              "<div style='margin:30px 10px;height:20px;background:#222222'></div>"
              "<div style='margin-top:-5px;height:10px;background:#333333'></div></body>", 800, false));
    CHECK(box_is(0x111111, 10, 10, 780, 50) && box_is(0x222222, 10, 90, 780, 20) && box_is(0x333333, 0, 135, 800, 10));
    CHECK(page->height >= 145 && !page->rows && page->pixels);

    /* der Abstand des ersten Kinds reicht durch den Kasten nach oben hinaus - außer er hat Rand oder Innenabstand */
    CHECK(lay("<body style='margin:0'><div style='background:#111111'><p style='margin:20px 0'>Text</p></div>"
              "<div style='background:#222222;padding:1px'><p style='margin:20px 0'>Zwei</p></div></body>", 800, false));
    CHECK(box_is(0x111111, 0, 20, 800, 20) && text_at("Text", 0, 20));
    CHECK(box_is(0x222222, 0, 60, 800, 62) && text_at("Zwei", 1, 81));

    /* Breite: Angabe, Innenabstand und Rahmen dazu - oder mit box-sizing schon enthalten */
    CHECK(lay("<body style='margin:0'><div style='width:200px;padding:10px 20px;border:5px solid #f00;background:#111111'>a</div>"
              "<div style='width:200px;padding:10px 20px;border:5px solid #f00;background:#222222;box-sizing:border-box'>b</div>"
              "<div style='width:50%;background:#333333'>c</div>"
              "<div style='max-width:300px;margin:0 auto;background:#444444'>d</div>"
              "<div style='width:400px;margin-left:auto;background:#555555'>e</div>"
              "<div style='width:calc(100% - 100px);min-height:30px;background:#666666'>f</div></body>", 800, false));
    CHECK(box_is(0x111111, 0, 0, 250, 50) && box_is(0x222222, 0, 50, 200, 50) && text_at("a", 25, 15) && text_at("b", 25, 65));
    CHECK(colored(0x111111)->border[0] == 5 && colored(0x111111)->border_color[3] == 0xFF0000);
    CHECK(box_is(0x333333, 0, 100, 400, 20) && box_is(0x444444, 250, 120, 300, 20) && box_is(0x555555, 400, 140, 400, 20) &&
          box_is(0x666666, 0, 160, 700, 30));

    /* die Grundregeln: Rand des body, Abstände der Absätze, Größe der Überschriften, Listen */
    CHECK(lay("<h1>Kopf</h1><p>Absatz</p><ul><li>Punkt</li><li>Zwei</li></ul><ol start='4'><li>Vier</li><li value='9'>Neun</li></ol>", 800, false));
    const span_t *h = find_span("Kopf", NULL, NULL);
    CHECK(h && h->size == 32 && (h->style & STYLE_BOLD) && text_at("Kopf", 8, 21));
    CHECK(text_at("Absatz", 8, 82) && text_at("Punkt", 48, 118) && text_at("Zwei", 48, 138));
    CHECK(has_text("\xE2\x80\xA2") && has_text("4.") && has_text("9.") && !has_text("5."));
    int mx, my;
    CHECK(find_span("\xE2\x80\xA2", &mx, &my) && mx < 48 && mx > 20 && my >= 116 && my <= 122);
    CHECK(page->heading_y == 21);
}

static void test_inline(void)
{
    /* Umbruch am Zeilenende; Ausrichtung */
    CHECK(lay("<body style='margin:0'><p style='margin:0;width:100px'>eins zwei drei vier</p>"
              "<p style='margin:0;width:100px;text-align:right'>rechts</p>"
              "<p style='margin:0;width:100px;text-align:center'>mitte</p></body>", 800, false));
    CHECK(text_at("eins zwei", 0, 0) && text_at("drei vier", 0, 20) && text_at("rechts", 52, 40) && text_at("mitte", 30, 60));

    /* Zeilenhöhe; größere Schrift macht die Zeile höher; alles auf einer Grundlinie */
    CHECK(lay("<body style='margin:0;line-height:2'><p style='margin:0'>klein <span style='font-size:32px'>GROSS</span> wieder</p>"
              "<p style='margin:0;line-height:30px'>dreißig</p></body>", 800, false));
    CHECK(page->line_count == 2 && page->lines[0].height == 64 && page->lines[1].height == 30 && page->lines[1].y == 64);
    CHECK(find_span("GROSS", NULL, NULL)->size == 32 && find_span("klein", NULL, NULL)->shift == 0);

    /* erzwungene Umbrüche, vorformatierter Text, Großschreibung */
    CHECK(lay("<body style='margin:0'>eins<br>zwei<br><br>vier<pre style='margin:0'>a  b\n  c</pre>"
              "<p style='margin:0;text-transform:uppercase'>größe</p><p style='margin:0;white-space:nowrap;width:40px'>eins zwei drei</p></body>", 800, false));
    CHECK(text_at("eins", 0, 0) && text_at("zwei", 0, 20) && text_at("vier", 0, 60) && text_at("a  b", 0, 80) &&
          text_at("  c", 0, 100) && has_text("GR\xC3\x96\xC3\x9F" "E") && text_at("eins zwei drei", 0, 140));

    /* was zusammengehört, bricht gemeinsam um: geschütztes Leerzeichen, nowrap */
    CHECK(lay("<body style='margin:0'><p style='margin:0;width:110px'>aaaa bbbb cc<span style='white-space:nowrap'> </span>dd</p>"
              "<p style='margin:0;width:110px'>aaaa bbbb cc\xC2\xA0" "dd ee</p></body>", 800, false));
    CHECK(text_at("aaaa bbbb", 0, 0) && text_at("cc", 0, 20) && text_at("aaaa bbbb", 0, 0));
    int x, y;
    CHECK(find_span("cc\xC2\xA0" "dd", &x, &y) && x == 0 && y == 60);

    /* ein überlanges Wort wird zerteilt statt aus dem Fenster zu ragen */
    CHECK(lay("<body style='margin:0'><p style='margin:0;width:80px'>abcdefghijklmnopqrstuvwxyz</p></body>", 800, false));
    CHECK(text_at("abcdefghij", 0, 0) && text_at("klmnopqrst", 0, 20) && text_at("uvwxyz", 0, 40));

    /* Elemente im Text: Hintergrund und Innenabstand laufen mit; inline-block steht als Ganzes in der Zeile */
    CHECK(lay("<body style='margin:0'><p style='margin:0'>vor <span style='background:#111111;padding:0 4px'>mitten</span> nach "
              "<span style='display:inline-block;width:100px;height:40px;background:#222222'></span> ende</p></body>", 800, false));
    CHECK(colored(0x111111) && colored(0x111111)->x == 32 && colored(0x111111)->w == 56 && text_at("mitten", 36, 0));
    CHECK(colored(0x222222) && colored(0x222222)->w == 100 && colored(0x222222)->h == 40 && page->lines[0].height > 40);

    /* Links: unterstrichen, blau (wenn die Seite nichts anderes sagt), mit Ziel */
    CHECK(lay("<a href='/ziel'>Link</a> <a href='#unten' style='color:#ff0000;text-decoration:none'>rot</a>"
              "<p id='unten'>Ziel</p>", 800, false));
    const span_t *link = find_span("Link", NULL, NULL), *red = find_span("rot", NULL, NULL);
    CHECK(link && link->link >= 0 && (link->style & STYLE_UNDERLINE) && (link->style & STYLE_LINK) &&
          strcmp(page->links[link->link], "https://a.de/ziel") == 0);
    CHECK(red && red->link >= 0 && !(red->style & STYLE_UNDERLINE) && red->color == (HTML_COLOR_SET | 0xFF0000) &&
          strcmp(page->links[red->link], "#unten") == 0);
    CHECK(page->anchor_count == 1 && strcmp(page->anchors[0].id, "unten") == 0 && page->anchors[0].y > 20);
}

static void test_side_by_side(void)
{
    /* neben dem Text: Die Zeilen weichen aus, danach gilt wieder die volle Breite */
    CHECK(lay("<body style='margin:0'><div style='float:right;width:100px;height:30px;margin-left:10px;background:#111111'></div>"
              "<p style='margin:0;width:300px'>aaaaaaaaaa bbbbbbbbbb cccccccccc dddddddddd eeeeeeeeee ffffffffff gggggggggg hhhhhhhhhh</p>"
              "<div style='clear:both;background:#222222'>unten</div></body>", 800, false));
    CHECK(box_is(0x111111, 700, 0, 100, 30));
    CHECK(lay("<body style='margin:0;width:300px'><div style='float:left;width:100px;height:30px;background:#111111'></div>"
              "<p style='margin:0'>aaaaaaaaaa bbbbbbbbbb cccccccccc dddddddddd eeeeeeeeee ffffffffff</p>"
              "<div style='clear:both;background:#222222'>unten</div></body>", 800, false));
    CHECK(text_at("aaaaaaaaaa bbbbbbbbbb", 100, 0) && text_at("cccccccccc dddddddddd", 100, 20) &&
          text_at("eeeeeeeeee ffffffffff", 0, 40) && box_is(0x222222, 0, 60, 300, 20));

    /* ein Kasten schließt ein, was in ihm nebeneinander steht, wenn die Seite es so einrichtet (clearfix) */
    CHECK(lay("<style>body{margin:0}.cf::after{content:'';display:table;clear:both}.sp{float:left;width:50%;height:60px}</style>"
              "<div class='cf' style='background:#111111'><div class='sp'>links</div><div class='sp'>rechts</div></div>"
              "<div style='background:#222222'>danach</div>"
              "<div style='background:#333333'><div class='sp'>l</div></div><div style='background:#444444'>daneben</div>", 800, false));
    CHECK(box_is(0x111111, 0, 0, 800, 60) && box_is(0x222222, 0, 60, 800, 20) && text_at("rechts", 400, 0));
    CHECK(!colored(0x333333) && text_at("daneben", 400, 80));

    /* in einer Reihe: gleich breit mit Zwischenraum; verteilt; mittig */
    CHECK(lay("<body style='margin:0'><div style='display:flex;gap:10px;width:620px'>"
              "<div style='flex:1;background:#111111'>a</div><div style='flex:1;background:#222222'>b<br>b</div>"
              "<div style='flex:1;background:#333333'>c</div></div>"
              "<div style='display:flex;justify-content:space-between;align-items:center;width:600px;height:100px'>"
              "<div style='width:100px;height:20px;background:#444444'></div><div style='width:100px;height:40px;background:#555555'></div></div>"
              "<div style='display:flex;flex-wrap:wrap;width:250px'><div style='width:100px;background:#666666'>1</div>"
              "<div style='width:100px;background:#777777'>2</div><div style='width:100px;background:#888888'>3</div></div>"
              "<div style='display:flex;width:400px'><div style='background:#999999'>links</div>"
              "<div style='margin-left:auto;background:#aaaaaa'>rechts</div></div></body>", 800, false));
    CHECK(box_is(0x111111, 0, 0, 200, 40) && box_is(0x222222, 210, 0, 200, 40) && box_is(0x333333, 420, 0, 200, 40));
    CHECK(box_is(0x444444, 0, 80, 100, 20) && box_is(0x555555, 500, 70, 100, 40));
    CHECK(box_is(0x666666, 0, 140, 100, 20) && box_is(0x777777, 100, 140, 100, 20) && box_is(0x888888, 0, 160, 100, 20));
    CHECK(box_is(0x999999, 0, 180, 40, 20) && box_is(0xAAAAAA, 352, 180, 48, 20));

    /* untereinander mit flex; jedes Element einer Reihe ist ein Kasten für sich */
    CHECK(lay("<body style='margin:0'><div style='display:flex;flex-direction:column;gap:5px;width:200px'>"
              "<div style='background:#111111'>a</div><div style='background:#222222;align-self:center'>bb</div></div>"
              "<div style='display:flex;gap:20px'><span>eins</span> <a href='/x'>zwei</a> lose</div></body>", 800, false));
    CHECK(box_is(0x111111, 0, 0, 200, 20) && box_is(0x222222, 92, 25, 16, 20));
    CHECK(text_at("eins", 0, 45) && text_at("zwei", 52, 45) && text_at("lose", 104, 45));

    /* im Raster: feste Breite, Anteile, Spalten zusammenfassen, so viele wie passen */
    CHECK(lay("<body style='margin:0'><div style='display:grid;grid-template-columns:100px 1fr 2fr;gap:10px 0;width:700px'>"
              "<div style='background:#111111'>a</div><div style='background:#222222'>b</div><div style='background:#333333'>c</div>"
              "<div style='grid-column:span 2;background:#444444'>d</div><div style='background:#555555'>e</div>"
              "<div style='grid-column:1 / -1;background:#666666'>f</div></div>"
              "<div style='display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));width:700px'>"
              "<div style='background:#777777'>1</div><div style='background:#888888'>2</div></div></body>", 800, false));
    CHECK(box_is(0x111111, 0, 0, 100, 20) && box_is(0x222222, 100, 0, 200, 20) && box_is(0x333333, 300, 0, 400, 20));
    CHECK(box_is(0x444444, 0, 30, 300, 20) && box_is(0x555555, 300, 30, 400, 20) && box_is(0x666666, 0, 60, 700, 20));
    CHECK(box_is(0x777777, 0, 80, 175, 20) && box_is(0x888888, 175, 80, 175, 20));

    /* benannte Flächen: Wo ein Kind steht, sagt sein Name, nicht die Reihenfolge */
    CHECK(lay("<body style='margin:0'><div style='display:grid;grid-template-columns:200px 1fr;"
              "grid-template-areas:\"kopf kopf\" \"leiste inhalt\";width:800px'>"
              "<div style='grid-area:inhalt;background:#111111'>Inhalt<br>zwei</div><div style='grid-area:leiste;background:#222222'>Leiste</div>"
              "<div style='grid-area:kopf;background:#333333'>Kopf</div></div></body>", 800, false));
    CHECK(box_is(0x333333, 0, 0, 800, 20) && box_is(0x222222, 0, 20, 200, 40) && box_is(0x111111, 200, 20, 600, 40));
}

static void test_tables_and_positions(void)
{
    /* Tabelle: Spalten so breit wie ihr Inhalt, Zellen einer Zeile gleich hoch, mittig darin */
    CHECK(lay("<body style='margin:0'><table style='border-spacing:0'><tr><td style='padding:0;background:#111111'>a</td>"
              "<td style='padding:0;background:#222222'>bbbb<br>b</td></tr>"
              "<tr><td colspan='2' style='padding:0;background:#333333'>cc</td></tr></table></body>", 800, false));
    CHECK(box_is(0x111111, 0, 0, 8, 40) && box_is(0x222222, 8, 0, 32, 40) && box_is(0x333333, 0, 40, 40, 20) && text_at("a", 0, 10));

    /* feste Breite: Der freie Platz verteilt sich; gemeinsame Rahmen überlappen */
    CHECK(lay("<body style='margin:0'><table style='width:400px;border-collapse:collapse'><tr>"
              "<td style='border:1px solid #000;padding:4px;background:#111111'>aaaa</td>"
              "<td style='border:1px solid #000;padding:4px;background:#222222'>bbbb</td></tr></table>"
              "<table border='1' cellpadding='5' width='200'><tr><th bgcolor='#333333'>K</th></tr></table></body>", 800, false));
    const box_t *a = colored(0x111111), *b = colored(0x222222);
    CHECK(a && b && a->x == 0 && a->x + a->w == b->x + 1 && b->x + b->w == 400 && a->border[3] == 1);
    const box_t *th = colored(0x333333);
    CHECK(th && th->border[0] == 1 && th->w > 150 && find_span("K", NULL, NULL) && (find_span("K", NULL, NULL)->style & STYLE_BOLD));

    /* frei platziert: nach dem nächsten Vorfahren mit position; von rechts und unten gemessen */
    CHECK(lay("<body style='margin:0'><div style='position:relative;width:500px;height:100px;margin-left:50px;background:#111111'>"
              "<div style='position:absolute;right:10px;bottom:5px;width:30px;height:20px;background:#222222'></div>"
              "<div style='position:absolute;top:10px;left:20px;background:#333333'>frei</div>"
              "<div style='position:absolute;left:0;right:0;top:50px;height:10px;background:#444444'></div></div>"
              "<div style='position:relative;top:5px;left:7px;width:40px;height:10px;background:#555555'></div>"
              "<p style='margin:0'>danach</p></body>", 800, false));
    CHECK(box_is(0x111111, 50, 0, 500, 100) && box_is(0x222222, 510, 75, 30, 20) && box_is(0x333333, 70, 10, 32, 20) &&
          box_is(0x444444, 50, 50, 500, 10) && box_is(0x555555, 7, 105, 40, 10) && text_at("danach", 0, 110));

    /* Ebenen: Was mit z-index weiter oben liegt, wird später gezeichnet */
    CHECK(lay("<body style='margin:0'><div style='position:absolute;z-index:5;width:10px;height:10px;background:#111111'></div>"
              "<div style='position:absolute;z-index:1;width:10px;height:10px;background:#222222'></div>"
              "<div style='position:absolute;z-index:-1;width:10px;height:10px;background:#333333'></div>"
              "<div style='width:10px;height:10px;background:#444444'></div></body>", 800, false));
    int order[4] = { -1, -1, -1, -1 };
    for (int i = 0; i < page->paint_count; i++) {
        if (page->paint[i].kind != PAINT_BOX)
            continue;
        uint32_t c = page->boxes[page->paint[i].index].background & 0xFFFFFF;
        if (c == 0x111111) order[0] = i;
        if (c == 0x222222) order[1] = i;
        if (c == 0x333333) order[2] = i;
        if (c == 0x444444) order[3] = i;
    }
    CHECK(order[2] >= 0 && order[2] < order[3] && order[3] < order[1] && order[1] < order[0]);
}

static void test_content(void)
{
    /* Bilder: Maße der Seite oder des Bildes, nie breiter als der Platz; Ersatztext, solange es fehlt */
    CHECK(lay("<body style='margin:0'><img src='a.png'><img src='b.png' width='100'><img src='c.png' style='width:50px;height:60px'>"
              "<div style='width:80px'><img src='d.png' alt='d'></div><img src='fehlt.png' alt='Ersatz'>"
              "<img src='spaeter.png' alt='Katze'><img src='e.png' width='1' height='1'></body>", 800, false));
    CHECK(page->image_count == 5);
    CHECK(page->images[0].width == 200 && page->images[0].height == 100 && page->images[0].x == 0 && page->images[0].y == 0);
    CHECK(page->images[1].width == 100 && page->images[1].height == 50 && page->images[1].x == 200 && page->images[1].y == 50);
    CHECK(page->images[2].width == 50 && page->images[2].height == 60);
    CHECK(page->images[3].width == 80 && page->images[3].height == 40);
    CHECK(has_text("Ersatz") && has_text("[Bild: Katze]") && page->images[4].y < 0 &&
          strcmp(page->images[0].url, "https://a.de/a.png") == 0);

    /* Formular: Felder mit ihrer Lage, Namen und Werten */
    CHECK(lay("<body style='margin:0'><form action='/los' method='post'><input name='q' value='Wert' size='10'>"
              "<input type='hidden' name='h' value='geheim'><input type='checkbox' name='c' checked>"
              "<select name='s'><option>eins<option selected value='2'>zwei</select>"
              "<textarea name='t'>Text</textarea><input type='submit' value='Los'>"
              "<button>Knopf im Formular</button><input type='text' name='breit' style='width:300px;height:40px;display:block'>"
              "</form><button type='button'>ohne Formular</button></body>", 800, false));
    CHECK(page->form_count == 1 && page->forms[0].post && strcmp(page->forms[0].action, "https://a.de/los") == 0);
    CHECK(page->widget_count == 8);
    const widget_t *w = page->widgets;
    CHECK(w[0].kind == W_TEXT && w[0].x == 0 && w[0].w == 96 && w[0].h == 24 && strcmp(w[0].value, "Wert") == 0 && w[0].form == 0);
    CHECK(w[1].kind == W_HIDDEN && w[1].h == 0 && strcmp(w[1].value, "geheim") == 0);
    CHECK(w[2].kind == W_CHECKBOX && w[2].checked && w[2].h > 0 && w[2].x >= 96);
    CHECK(w[3].kind == W_SELECT && w[3].option_count == 2 && w[3].selected == 1 &&
          strcmp(page->options[w[3].first_option + 1].value, "2") == 0);
    CHECK(w[4].kind == W_TEXTAREA && strcmp(w[4].value, "Text") == 0 && w[5].kind == W_SUBMIT && strcmp(w[5].label, "Los") == 0);
    CHECK(w[6].kind == W_SUBMIT && strcmp(w[6].label, "Knopf im Formular") == 0);
    CHECK(w[7].kind == W_TEXT && w[7].w == 300 && w[7].h == 40 && w[7].x == 0 && w[7].y > w[0].y);
    CHECK(!has_text("ohne Formular"));          /* tut ohne Skripte nichts: erscheint nicht */
    CHECK(lay("<button type='button'>ohne Formular</button>", 800, true) && page->widget_count == 1 &&
          page->widgets[0].kind == W_BUTTON);

    /* was nicht zu sehen sein soll */
    CHECK(lay("<title> Der  Titel </title><p hidden>a1</p><p style='display:none'>a2</p><script>a3</script>"
              "<p style='visibility:hidden'>a4</p><p style='position:absolute;clip:rect(0 0 0 0)'>a5</p>"
              "<noscript>ohne Skripte</noscript><details><summary>Mehr</summary>Inhalt</details><p>da</p>", 800, false));
    CHECK(!has_text("a1") && !has_text("a2") && !has_text("a3") && !has_text("a4") && !has_text("a5") && has_text("da"));
    CHECK(has_text("ohne Skripte") && has_text("Inhalt") && strcmp(page->title, "Der Titel") == 0);
    CHECK(lay("<noscript>ohne Skripte</noscript><details><summary>Mehr</summary>Inhalt</details>"
              "<details open><summary>Offen</summary>Sichtbar</details>", 800, true));
    CHECK(!has_text("ohne Skripte") && !has_text("Inhalt") && has_text("\xE2\x96\xB8 Mehr") &&
          has_text("\xE2\x96\xBE Offen") && has_text("Sichtbar"));

    /* unlesbare Farben werden ersetzt: weiß auf weiß, dunkel auf dunkel */
    CHECK(lay("<body style='margin:0'><p style='color:#ffffff'>hell</p><div style='background:#101010'><p>dunkel</p>"
              "<a href='/x'>link</a></div><p style='color:#ff0000'>rot</p></body>", 800, false));
    CHECK(find_span("hell", NULL, NULL)->color == (HTML_COLOR_SET | 0x222630) &&
          find_span("dunkel", NULL, NULL)->color == (HTML_COLOR_SET | 0xFFFFFF) &&
          find_span("rot", NULL, NULL)->color == (HTML_COLOR_SET | 0xFF0000));
    CHECK((find_span("link", NULL, NULL)->color & 0xFFFFFF) != 0x1E5ABE || find_span("link", NULL, NULL)->color == 0);

    /* Hintergrund der ganzen Seite; Stylesheets mit Klassen */
    CHECK(lay("<style>body { background: #204060; margin: 0 } .karte { margin: 10px; padding: 10px; border: 1px solid #fff;"
              " border-radius: 6px; background: #111111 }</style><div class='karte'>Text</div>", 800, false));
    CHECK(page->background == (HTML_COLOR_SET | 0x204060) && box_is(0x111111, 10, 10, 780, 42) &&
          colored(0x111111)->radius == 6 && text_at("Text", 21, 21));

    /* Hinweise für das, was sich nicht zeigen lässt */
    CHECK(lay("<video src='/film.mp4'></video> <iframe src='https://b.de/karte'></iframe> "
              "<progress value='3' max='10'></progress><input type='file'>", 800, false));
    CHECK(has_text("[Video: film.mp4]") && has_text("[Eingebettete Seite: karte]") && has_text("\xE2\x96\x88") && has_text("[Datei"));
}

/* Der Zeichenschritt, mit dem die Zeile gezeichnet wird, in der text beginnt (oder NULL) */
static const paint_t *paint_of(const char *text)
{
    size_t n = strlen(text);
    for (int i = 0; page && i < page->paint_count; i++) {
        if (page->paint[i].kind != PAINT_LINE)
            continue;
        const line_t *line = &page->lines[page->paint[i].index];
        for (uint32_t k = 0; k < line->count; k++) {
            const span_t *s = &page->spans[line->first + k];
            if (s->bytes >= n && memcmp(page->pool + s->text, text, n) == 0)
                return &page->paint[i];
        }
    }
    return NULL;
}

static void test_decoration(void)
{
    /* Inhalte vor und nach einem Element (::before, ::after) */
    CHECK(lay("<style>body{margin:0} p{margin:0} .q::before{content:'\\201E';color:#ff0000} .q:after{content:' (' attr(data-x) ')'}"
              ".i::before{content:'\\f101'} .b::before{content:'';display:block;height:10px;background:#111111}"
              ".n::before{content:none;display:block;height:10px;background:#222222}"
              ".z::after{content:'\\2192  weiter';display:block;text-align:right}</style>"
              "<p class='q' data-x='Quelle'>Zitat</p><p class='i'>Symbol</p><div class='b'>Kasten</div>"
              "<p class='n'>nichts</p><p class='z'>Zeile</p>", 800, false));
    const span_t *quote = find_span("\xE2\x80\x9E", NULL, NULL);
    CHECK(quote && quote->color == (HTML_COLOR_SET | 0xFF0000) && text_at("Zitat", 8, 0) && has_text(" (Quelle)"));
    CHECK(text_at("Symbol", 0, 20) && box_is(0x111111, 0, 40, 800, 10) && text_at("Kasten", 0, 50) &&
          !colored(0x222222) && text_at("nichts", 0, 70));
    int x, y;
    CHECK(find_span("\xE2\x86\x92 weiter", &x, &y) && x == 800 - 8 * 8 && y == 110);

    /* was übersteht, wird abgeschnitten: Der Kasten behält seine Höhe, der Inhalt bekommt einen Ausschnitt */
    CHECK(lay("<body style='margin:0'><div style='height:40px;overflow:hidden;background:#111111;border:2px solid #000'>"
              "<p style='margin:0'>a1<br>b2<br>c3<br>d4</p></div><p style='margin:0'>danach</p>"
              "<div style='max-height:30px;overflow:hidden;background:#222222'>e5<br>f6<br>g7</div>"
              "<div style='height:30px;background:#333333'>h8<br>i9<br>j0</div><p style='margin:0'>ende</p></body>", 800, false));
    CHECK(box_is(0x111111, 0, 0, 800, 44) && text_at("danach", 0, 44) && box_is(0x222222, 0, 64, 800, 30));
    CHECK(box_is(0x333333, 0, 94, 800, 60) && text_at("ende", 0, 154));     /* ohne overflow wächst der Kasten */
    CHECK(page->clip_count == 2 && page->clips[0].x == 2 && page->clips[0].y == 2 && page->clips[0].w == 796 &&
          page->clips[0].h == 40 && page->clips[1].y == 64 && page->clips[1].h == 30);
    CHECK(paint_of("d4") && paint_of("d4")->clip == 1 && paint_of("g7")->clip == 2 && paint_of("danach")->clip == 0 &&
          paint_of("j0")->clip == 0);
    CHECK(page->height == 174);

    /* Ausschnitte in Ausschnitten werden kleiner; frei Platziertes richtet sich nach seinem Bezugskasten */
    CHECK(lay("<body style='margin:0'><div style='width:200px;height:100px;overflow:hidden'>"
              "<div style='margin-left:150px;width:100px;height:300px;overflow:hidden'><p style='margin:0'>innen</p></div></div>"
              "<div style='position:relative;width:100px;height:50px;overflow:hidden'>"
              "<p style='position:absolute;top:40px;margin:0'>frei</p></div></body>", 800, false));
    CHECK(page->clip_count == 3 && page->clips[1].x == 150 && page->clips[1].w == 50 && page->clips[1].h == 100);
    CHECK(paint_of("innen")->clip == 2 && paint_of("frei")->clip == 3 && page->height == 150);

    /* frei Platziertes richtet sich auch nach einem Element mitten im Text, wenn es "position" hat */
    CHECK(lay("<body style='margin:0'><p style='margin:0'>davor <a href='/x' style='position:relative'>Link"
              "<span style='position:absolute;top:-4px;right:-20px;background:#111111'>neu</span></a> danach</p></body>", 800, false));
    CHECK(text_at("Link", 48, 0) && box_is(0x111111, 48 + 32 + 20 - 24, -4, 24, 20));

    /* Farbverläufe: erste und letzte Farbe, Richtung */
    CHECK(lay("<body style='margin:0'><div style='height:10px;background:linear-gradient(to right, #ff0000, #00ff00 50%, #0000ff)'></div>"
              "<div style='height:10px;background:linear-gradient(#101010, #f0f0f0)'></div>"
              "<div style='height:10px;background-image:linear-gradient(to top, #202020 0%, #e0e0e0 100%)'></div>"
              "<div style='height:10px;background:linear-gradient(90deg, rgb(48, 48, 48), rgba(200, 200, 200, 1))'></div>"
              "<div style='height:10px;background:radial-gradient(#404040, #ffffff)'></div></body>", 800, false));
    const box_t *b = colored(0xFF0000);
    CHECK(b && b->gradient == (HTML_COLOR_SET | 0x0000FF) && b->gradient_side == 1);
    b = colored(0x101010);
    CHECK(b && b->gradient == (HTML_COLOR_SET | 0xF0F0F0) && b->gradient_side == 0);
    b = colored(0xE0E0E0);
    CHECK(b && b->gradient == (HTML_COLOR_SET | 0x202020) && b->gradient_side == 0 && b->y == 20);
    b = colored(0x303030);
    CHECK(b && b->gradient == (HTML_COLOR_SET | 0xC8C8C8) && b->gradient_side == 1);
    b = colored(0x404040);
    CHECK(b && b->gradient == 0);

    /* Hintergrundbilder: Adresse, Wiederholung, Lage, Größe */
    CHECK(lay("<style>.a{background:#222222 url(bild.png) no-repeat right 10px / cover;width:300px;height:100px}"
              ".b{background-image:url('/kachel.gif');background-size:50px auto;background-position:center;"
              "background-repeat:repeat-x;height:20px}.c{background:url(bild.png) -20px -40px;width:16px;height:16px}"
              ".d{background:url(spaeter.png)}.e{background:url(fehlt.png) #333333}</style>"
              "<body style='margin:0'><div class='a'><p style='margin:0;color:#ffffff'>hell</p></div><div class='b'></div>"
              "<div class='c'></div><div class='d'><p style='margin:0;color:#ffffff'>wartet</p></div>"
              "<div class='e'>x</div></body>", 800, false));
    b = colored(0x222222);
    CHECK(b && b->picture > 0 && strcmp(page->images[b->picture - 1].url, "https://a.de/bild.png") == 0 &&
          page->images[b->picture - 1].y < 0 && b->picture_repeat == 0 && b->picture_size == PICTURE_COVER &&
          b->picture_px == 100 && b->picture_y == 10 && b->picture_py == 0);
    CHECK(page->image_count == 4 && page->box_count == 5);
    const box_t *tile = &page->boxes[1], *sprite = &page->boxes[2];
    CHECK(tile->picture > 0 && strcmp(page->images[tile->picture - 1].url, "https://a.de/kachel.gif") == 0 &&
          tile->picture_repeat == 1 && tile->picture_size == PICTURE_GIVEN && tile->picture_w == 50 && tile->picture_h == 0 &&
          tile->picture_px == 50 && tile->picture_py == 50 && tile->background == 0);
    CHECK(sprite->picture == b->picture && sprite->picture_x == -20 && sprite->picture_y == -40 && sprite->picture_repeat == 3 &&
          sprite->w == 16 && sprite->h == 16);
    /* Text auf einem Bild, das da ist: Die Farbe der Seite gilt. Fehlt es noch, wird sie lesbar gemacht. */
    CHECK(find_span("hell", NULL, NULL)->color == (HTML_COLOR_SET | 0xFFFFFF) &&
          find_span("wartet", NULL, NULL)->color == (HTML_COLOR_SET | 0x222630));
}

/* Absichtlich beschädigte Seiten: Es darf nichts abstürzen, hängen oder außerhalb des Speichers lesen. */
static void test_fuzz(void)
{
    static const char sample[] =
        "<!DOCTYPE html><html><head><title>T</title><style>body{margin:0;font:14px/1.5 sans-serif}"
        ".r{display:flex;gap:1em;flex-wrap:wrap}.r>div{flex:1 1 100px;padding:4px;border:1px solid #ccc}"
        ".g{display:grid;grid-template-columns:repeat(3,1fr);grid-template-areas:'a a b' 'c d d'}.x{grid-area:d;position:relative;z-index:2}"
        ".f{float:right;width:30%;margin:0 0 1em 1em}.abs{position:absolute;right:0;bottom:0}table{border-collapse:collapse}"
        "p::before{content:'\\201C' attr(class) ' ';color:red}.r::after{content:'';display:table;clear:both}"
        ".g{overflow:hidden;max-height:90px;background:linear-gradient(to right,#eee,#ccc)}"
        ".r{background:url(k.png) center / cover no-repeat #fff;overflow:hidden;height:5em}"
        "td{border:1px solid;padding:2px}.n{white-space:nowrap}.ib{display:inline-block;vertical-align:middle;width:50px;height:30px}"
        "</style></head><body><div class=r><div>eins <b>zwei</b></div><div>drei<br>vier</div><span>lose</span></div>"
        "<div class=g><p>a</p><p class=x>b<span class=abs>c</span></p><ul><li>d<ol><li>e</ol></ul></div>"
        "<div class=f><img src=a.png width=40 alt=x><img src=spaeter.png></div><p>Text mit <a href=#z>Link</a>, <code>code</code>, "
        "<span class=n>nicht umbrechen</span> und <span class=ib>kasten</span> langemwortohneleerzeichenundumbruchmoeglichkeit</p>"
        "<table><caption>Titel</caption><thead><tr><th>A<th colspan=2>B</thead><tr><td rowspan=2>1<td>2<td>3<tr><td>4</table>"
        "<form action=/a><input name=q><select><option>1<optgroup><option selected>2</select><textarea>t</textarea>"
        "<button>ok</button><input type=checkbox checked></form><details><summary>s</summary>d</details>"
        "<h1 id=z style='margin:-10px auto;max-width:50%;text-align:center'>Ende</h1><pre>a\n\tb</pre></body></html>";
    quiet = true;
    srand(4711);
    for (int round = 0; round < 8000; round++) {
        size_t length = sizeof(sample) - 1;
        char *text = malloc(length + 1);
        memcpy(text, sample, length);
        int kind = round % 4;
        if (kind == 0) {
            /* einzelne Zeichen verfälschen */
            for (int i = 0; i < 1 + rand() % 12; i++)
                text[(size_t)rand() % length] = "<>/='\" ;:{}()#.-0123456789%abct\n\xC3\xA4"[rand() % 34];
        } else if (kind == 1) {
            /* abschneiden */
            length = (size_t)rand() % length;
        } else if (kind == 2) {
            /* ein Stück verdoppeln oder vertauschen */
            size_t a = (size_t)rand() % length, b = (size_t)rand() % length, n = (size_t)rand() % 60;
            for (size_t i = 0; i < n && a + i < length && b + i < length; i++) {
                char swap = text[a + i];
                text[a + i] = text[b + i];
                text[b + i] = swap;
            }
        } else {
            /* Zahlen in der Gestaltung übertreiben */
            for (size_t i = 0; i + 1 < length; i++) {
                if (text[i] >= '1' && text[i] <= '9' && rand() % 6 == 0)
                    text[i] = "09"[rand() % 2];
                else if (text[i] == 'p' && text[i + 1] == 'x' && rand() % 5 == 0)
                    text[i] = '%';
            }
        }
        text[length] = '\0';
        if (lay(text, 40 + rand() % 1400, rand() % 2)) {
            /* was herauskommt, muss in sich stimmen */
            bool sound = page->height >= 0 && page->height < 50000000;
            for (int i = 0; sound && i < page->box_count; i++)
                sound = page->boxes[i].picture <= page->image_count;
            for (int i = 0; sound && i < page->paint_count; i++) {
                const paint_t *p = &page->paint[i];
                sound = p->clip <= page->clip_count && p->index >= 0 &&
                        p->index < (p->kind == PAINT_BOX ? page->box_count : p->kind == PAINT_LINE ? page->line_count
                                    : p->kind == PAINT_IMAGE ? page->image_count : page->widget_count);
            }
            for (int i = 0; sound && i < page->line_count; i++) {
                const line_t *line = &page->lines[i];
                sound = line->first + line->count <= (uint32_t)page->span_count;
                for (uint32_t k = 0; sound && k < line->count; k++) {
                    const span_t *s = &page->spans[line->first + k];
                    sound = s->text + s->bytes <= page->pool_len && s->link < page->link_count;
                }
            }
            if (!sound) {
                failures++;
                printf("FEHLER: Runde %d ergibt eine Seite, die nicht stimmt\n", round);
            }
        }
        free(text);
    }
    checks++;
    quiet = false;
    done();
}

static const widget_t *widget_of(const char *id)
{
    const dom_node *n = doc ? dom_find_id(doc->root, id) : NULL;
    for (int i = 0; n && page && i < page->widget_count; i++) {
        if (page->widgets[i].node == n->id)
            return &page->widgets[i];
    }
    return NULL;
}

static bool rect_of_button_ok(void)
{
    const dom_node *n = dom_find_id(doc->root, "s");
    return n && page->rects && page->rects[n->id - 1].w == 22 && page->rects[n->id - 1].h == 22;
}

/* <button> ist ein Kasten mit eigenem Inhalt und dem Aussehen, das die Seite ihm gibt */
static void test_buttons(void)
{
    lay("<style>.sym{width:22px;height:22px;padding:0;border:0;background:transparent} .sym .weg{display:none}"
        " #g{background:#008000;color:#ffffff;padding:10px 30px;border:0;font-size:20px}</style>"
        "<body style='margin:0'><button id=k>Mehr <b>jetzt</b></button><button id=s class=sym><span></span>"
        "<span class=weg>Lange Beschriftung</span></button><p style='margin:0'><button id=g>Los</button></p>"
        "<form action=/x><button id=f>Senden</button></form>", 400, true);
    const widget_t *k = widget_of("k"), *sym = widget_of("s"), *g = widget_of("g"), *f = widget_of("f");
    /* (der leere Knopf daneben steht mit seiner Unterkante auf der Grundlinie: Das schiebt die Zeile etwas tiefer) */
    CHECK(k && k->kind == W_BUTTON && k->boxed && k->x == 0 && k->y == 4 && k->w == 102 && k->h > 20);
    CHECK(text_at("Mehr", 11, 8) && find_span("jetzt", NULL, NULL) && (find_span("jetzt", NULL, NULL)->style & STYLE_BOLD));
    CHECK(sym && sym->w == 22 && sym->h == 22 && sym->x == 102 && !has_text("Lange"));
    CHECK(g && g->w == 3 * 8 + 60 && g->x == 0 && colored(0x008000) && colored(0x008000)->w == g->w);
    CHECK(find_span("Los", NULL, NULL) && find_span("Los", NULL, NULL)->size == 20 &&
          find_span("Los", NULL, NULL)->color == (HTML_COLOR_SET | 0xFFFFFF));
    CHECK(f && f->kind == W_SUBMIT && f->boxed && f->form == 0 && f->label && strcmp(f->label, "Senden") == 0);
    /* über dem Inhalt in der Zeichenfolge: So trifft der Klick den Knopf */
    int seen = 0;
    for (int i = 0; i < page->paint_count; i++)
        seen += page->paint[i].kind == PAINT_WIDGET;
    CHECK(seen == 4 && rect_of_button_ok());
    /* ohne Skripte und ohne Formular tut ein Knopf nichts: Er erscheint nicht */
    lay("<body><button id=k>Mehr</button><form action=/x><button id=f>Senden</button></form>", 400, false);
    CHECK(!widget_of("k") && widget_of("f") && !has_text("Mehr") && has_text("Senden"));
    done();
}

/* Die Lage eines Elements, wie Skripte sie erfahren */
static const node_rect_t *rect(const char *id)
{
    const dom_node *n = doc ? dom_find_id(doc->root, id) : NULL;
    return n && page && page->rects && n->id <= page->rect_count ? &page->rects[n->id - 1] : NULL;
}

static bool rect_is(const char *id, int x, int y, int w, int h)
{
    const node_rect_t *r = rect(id);
    bool ok = r && r->x == x && r->y == y && r->w == w && r->h == h;
    if (!ok) {
        if (r)
            printf("  #%s liegt bei %d,%d mit %dx%d (erwartet %d,%d mit %dx%d)\n", id, r->x, r->y, r->w, r->h,
                   x, y, w, h);
        else
            printf("  #%s hat keinen Eintrag\n", id);
    }
    return ok;
}

/* Der Rahmen, in dem das Zeichenstück für diesen Kasten liegt (plus 1), oder -1 */
static int frame_of_box(uint32_t rgb)
{
    const box_t *b = colored(rgb);
    for (int i = 0; b && i < page->paint_count; i++) {
        if (page->paint[i].kind == PAINT_BOX && &page->boxes[page->paint[i].index] == b)
            return page->paint[i].frame;
    }
    return -1;
}

/* Am Fenster befestigt (fixed) und oben haftend (sticky) */
static void test_frames(void)
{
    /* fixed: Lage im Fenster (hier 600 hoch), nicht auf der Seite; die Seite wird davon nicht länger */
    lay("<body style='margin:0'><div id=k style='position:fixed;top:0;left:0;right:0;height:40px;background:#111111'>"
        "<b style='position:absolute;right:5px;top:5px;background:#131313'>x</b>Kopf</div>"
        "<div id=u style='position:fixed;bottom:0;right:10px;width:100px;height:30px;background:#121212'>unten</div>"
        "<div style='height:2000px;background:#eeeeee'>Inhalt</div>", 400, false);
    CHECK(page->frame_count == 2 && page->frames[0].kind == FRAME_FIXED && page->frames[1].kind == FRAME_FIXED);
    CHECK(box_is(0x111111, 0, 0, 400, 40) && box_is(0x121212, 290, 570, 100, 30) && page->height == 2000);
    CHECK(frame_of_box(0x111111) == 1 && frame_of_box(0x121212) == 2 && frame_of_box(0xEEEEEE) == 0);
    CHECK(frame_of_box(0x131313) == 1);         /* frei Platziertes darin wandert mit */
    CHECK(rect("k") && rect("k")->frame == 1 && rect("u")->frame == 2 && rect("u")->y == 570);
    CHECK(frame_shift(page->frames, page->frame_count, 1, 700) == 700 &&
          frame_shift(page->frames, page->frame_count, 0, 700) == 0);
    /* gezeichnet wird das Befestigte über dem Inhalt */
    int content = -1, bar = -1;
    for (int i = 0; i < page->paint_count; i++) {
        if (page->paint[i].kind != PAINT_BOX)
            continue;
        if (page->boxes[page->paint[i].index].background == (HTML_COLOR_SET | 0xEEEEEE))
            content = i;
        if (page->boxes[page->paint[i].index].background == (HTML_COLOR_SET | 0x111111))
            bar = i;
    }
    CHECK(content >= 0 && bar > content);

    /* sticky: rollt mit, bleibt 10 Pixel unter dem Fensterrand haften - bis sein Kasten zu Ende ist */
    lay("<body style='margin:0'><div style='height:100px'></div><div id=c>"
        "<div id=s style='position:sticky;top:10px;height:20px;background:#222222'>Leiste</div>"
        "<div style='height:500px;background:#dddddd'></div></div><div style='height:1000px'></div>", 400, false);
    CHECK(page->frame_count == 1 && page->frames[0].kind == FRAME_STICKY && page->frames[0].y == 100 &&
          page->frames[0].h == 20 && page->frames[0].top == 10 && page->frames[0].limit == 620);
    CHECK(box_is(0x222222, 0, 100, 400, 20) && frame_of_box(0x222222) == 1 && frame_of_box(0xDDDDDD) == 0);
    CHECK(frame_shift(page->frames, 1, 1, 0) == 0 && frame_shift(page->frames, 1, 1, 50) == 0 &&
          frame_shift(page->frames, 1, 1, 200) == 110 && frame_shift(page->frames, 1, 1, 5000) == 500);
    CHECK(page->height == 1620 && rect("s")->frame == 1 && text_at("Leiste", 0, 100));

    /* Kopfzellen einer Tabelle haften, solange die Tabelle zu sehen ist; ohne "top" haftet nichts */
    lay("<body style='margin:0'><table cellspacing=0 cellpadding=0><tr><th id=h style='position:sticky;top:0;"
        "background:#333333'>Kopf</th></tr><tr><td style='height:300px'>a</td></tr></table>"
        "<p id=n style='position:sticky;margin:0;background:#444444'>ohne</p>", 400, false);
    CHECK(page->frame_count == 1 && page->frames[0].limit >= 300 && frame_of_box(0x444444) == 0);
    CHECK(frame_shift(page->frames, 1, 1, 150) == 150 && frame_shift(page->frames, 1, 1, 900) == page->frames[0].limit - page->frames[0].h);

    /* ein Ausschnitt im Befestigten bewegt sich mit ihm */
    lay("<body style='margin:0'><div style='position:fixed;top:20px;left:0;width:100px;height:30px;overflow:hidden'>"
        "<div style='height:90px;background:#555555'>lang</div></div><div style='height:900px'></div>", 400, false);
    const box_t *inner = colored(0x555555);
    int clip = -1;
    for (int i = 0; inner && i < page->paint_count; i++) {
        if (page->paint[i].kind == PAINT_BOX && &page->boxes[page->paint[i].index] == inner)
            clip = page->paint[i].clip;
    }
    CHECK(clip > 0 && page->clips[clip - 1].frame == 1 && page->clips[clip - 1].y == 20 && page->clips[clip - 1].h == 30);
    done();
}

/* "inherit": Was schwächere Regeln für Schrift und Farbe festgelegt haben, gilt nicht mehr */
static void test_inherit(void)
{
    lay("<style>.w,h2{font-size:150%;font-weight:normal;font-style:italic} .w h2{font:inherit;margin:0}"
        " a{color:#0000ff} .knopf{color:#ffffff;background:#333333} .knopf a{color:inherit}"
        " .x h3{font-size:inherit;font-weight:inherit} b.n{font-weight:inherit}</style>"
        "<div class=w><h2>Titel</h2></div><div class=knopf><a href=x>Knopf</a> <a href=y style='color:#ffff00'>rot</a></div>"
        "<div class=x><h3>Klein</h3></div><p><a href=z>Verweis</a> <b class=n>normal</b> <b>fett</b></p>", 400, false);
    const span_t *t = find_span("Titel", NULL, NULL), *k = find_span("Knopf", NULL, NULL);
    const span_t *r = find_span("rot", NULL, NULL), *h = find_span("Klein", NULL, NULL);
    const span_t *v = find_span("Verweis", NULL, NULL), *n = find_span("normal", NULL, NULL);
    const span_t *f = find_span("fett", NULL, NULL);
    CHECK(t && t->size == 24 && (t->style & STYLE_ITALIC) && !(t->style & STYLE_BOLD));
    CHECK(k && k->color == (HTML_COLOR_SET | 0xFFFFFF) && k->link >= 0);
    CHECK(r && r->color == (HTML_COLOR_SET | 0xFFFF00));
    CHECK(h && h->size == 16 && !(h->style & STYLE_BOLD));
    CHECK(v && v->color == (HTML_COLOR_SET | 0x0000FF));
    CHECK(n && !(n->style & STYLE_BOLD) && f && (f->style & STYLE_BOLD));
    done();
}

static void test_geometry(void)
{
    /* Kästen: Außenkante des Rahmens; Abstände außen zählen nicht mit */
    lay("<body style='margin:0'><div id=a style='margin:10px 20px;padding:5px;border:2px solid #000;height:30px'>"
        "<p id=b style='margin:0'>Text</p></div><div id=c style='width:100px;height:40px'></div>", 400, false);
    CHECK(rect_is("a", 20, 10, 360, 44));
    CHECK(rect_is("b", 27, 17, 346, 20));
    CHECK(rect_is("c", 0, 64, 100, 40));
    CHECK(page->rect_count == doc->count && page->width == 400);
    /* (der Abstand über dem ersten Kasten reicht durch <body> hindurch: Es beginnt erst darunter) */
    const dom_node *root = doc->root->first, *body = root->last;
    CHECK(page->rects[body->id - 1].y == 10 && page->rects[body->id - 1].w == 400 && page->rects[body->id - 1].h == 94);
    CHECK(page->rects[root->id - 1].y == 0 && page->rects[root->id - 1].w == 400 &&
          page->rects[root->id - 1].h == page->height);

    /* Elemente im Text: die Fläche um ihre Zeichen - auch über einen Umbruch hinweg */
    lay("<body style='margin:0'><p style='margin:0'>Ein <b id=w>Wort</b> und <a id=l href=x>ein langer "
        "Verweis</a> <i id=leer></i>.</p>", 160, false);
    CHECK(rect_is("w", 32, 0, 32, 20));
    const node_rect_t *l = rect("l");
    CHECK(l && l->y == 0 && l->h == 40 && l->x == 0 && l->w >= 64);
    const node_rect_t *e = rect("leer");
    CHECK(e && e->w == 0 && e->h == 0 && e->y >= 20);

    /* Was nicht zu sehen ist, hat keinen Platz; ein Zusatz (::before) verstellt die Lage nicht */
    lay("<style>#z::before{content:'x';display:block;height:50px}</style><body style='margin:0'>"
        "<div id=n style='display:none'><b id=n2>nie</b></div><div id=z style='height:80px'>da</div>"
        "<img id=bild width=30 height=20 src=b.png>", 300, false);
    CHECK(rect("n") && rect("n")->w < 0 && rect("n2")->w < 0);
    CHECK(rect_is("z", 0, 0, 300, 80));
    CHECK(rect("bild") && rect("bild")->w == 30 && rect("bild")->h == 20 && rect("bild")->y == 80);

    /* Frei platziert, nebeneinander, in Tabellen und Feldern */
    lay("<body style='margin:0'><div style='position:relative;height:100px'>"
        "<div id=p style='position:absolute;right:10px;top:20px;width:50px;height:30px'></div></div>"
        "<div style='display:flex'><div id=f1 style='width:100px;height:10px'></div>"
        "<div id=f2 style='flex:1;height:10px'></div></div>"
        "<table cellspacing=0 cellpadding=0 style='border-collapse:collapse'><tr><td id=t1 style='width:60px'>a</td>"
        "<td id=t2 style='width:90px'>b</td></tr></table><input id=in type=text>", 400, false);
    CHECK(rect_is("p", 340, 20, 50, 30));
    CHECK(rect_is("f1", 0, 100, 100, 10));
    CHECK(rect_is("f2", 100, 100, 300, 10));
    CHECK(rect("t1") && rect("t2") && rect("t1")->y == 110 && rect("t2")->x == rect("t1")->x + rect("t1")->w);
    CHECK(rect("in") && rect("in")->w > 50 && rect("in")->h > 10 && rect("in")->y >= 110);

    /* Raster: Eine Spalte mit Spanne (minmax) gibt nach, wenn es eng wird - und nur dann */
    static const char raster[] =
        "<body style='margin:0'><div style='display:grid;grid-template-columns:minmax(0,300px) 100px'>"
        "<div id=a style='height:10px'></div><div id=b style='height:10px'></div></div>"
        "<div style='display:grid;grid-template-columns:minmax(120px,300px) 100px'>"
        "<div id=c style='height:10px'></div><div id=d style='height:10px'></div></div>";
    lay(raster, 250, false);
    CHECK(rect_is("a", 0, 0, 150, 10));
    CHECK(rect_is("b", 150, 0, 100, 10));
    CHECK(rect_is("c", 0, 10, 150, 10));
    lay(raster, 180, false);
    CHECK(rect_is("a", 0, 0, 80, 10));
    CHECK(rect_is("c", 0, 10, 120, 10));
    CHECK(rect_is("d", 120, 10, 100, 10));
    lay(raster, 600, false);
    CHECK(rect_is("a", 0, 0, 300, 10));
    CHECK(rect_is("b", 300, 0, 100, 10));
    done();
}

int main(void)
{
    test_blocks();
    test_inline();
    test_side_by_side();
    test_tables_and_positions();
    test_content();
    test_decoration();
    test_frames();
    test_inherit();
    test_buttons();
    test_geometry();
    test_fuzz();
    done();
    printf("%d Prüfungen am Seitenaufbau mit Kastenmodell, %d Fehler\n", checks, failures);
    return failures ? 1 : 0;
}
