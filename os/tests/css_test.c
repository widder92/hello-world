/*
 * Test für kernel/css.c auf dem Entwicklungsrechner: Regeln lesen, Elemente
 * zuordnen, Rangfolge, Farben, Variablen, @media - und beschädigte Eingaben.
 *
 *   cc -fsanitize=address,undefined -o css_test tests/css_test.c kernel/css.c
 */
#include "../kernel/css.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *kmalloc(size_t n) { return malloc(n ? n : 1); }
void *kzalloc(size_t n) { return calloc(1, n ? n : 1); }
void kfree(void *p) { free(p); }

static int checks, failures;

#define CHECK(condition) do { \
    checks++; \
    if (!(condition)) { \
        failures++; \
        printf("FEHLER Zeile %d: %s\n", __LINE__, #condition); \
    } \
} while (0)

static css_node_t node(const char *tag, const char *id, const char *classes,
                       const css_node_t *parent)
{
    css_node_t n = { tag, strlen(tag), id, id ? strlen(id) : 0, classes,
                     classes ? strlen(classes) : 0, parent };
    return n;
}

static css_style_t match(const css_sheet_t *sheet, const css_node_t *n, const char *style)
{
    css_style_t out;
    css_match(sheet, n, style, style ? strlen(style) : 0, &out);
    return out;
}

static void test_colors(void)
{
    uint32_t c = 0;
    CHECK(css_color("#fff", 4, &c) && c == 0xFFFFFF);
    CHECK(css_color("#1a2B3c", 7, &c) && c == 0x1A2B3C);
    CHECK(css_color("#1a2b3cff", 9, &c) && c == 0x1A2B3C);
    CHECK(!css_color("#1a2b3c00", 9, &c));                      /* durchsichtig */
    CHECK(css_color("rgb(10, 20, 30)", 15, &c) && c == 0x0A141E);
    CHECK(css_color("rgb(10 20 30 / 80%)", 19, &c) && c == 0x0A141E);
    CHECK(css_color("rgba(255,0,0,0.5)", 17, &c) && c == 0xFF0000);
    CHECK(!css_color("rgba(255,0,0,0)", 15, &c));
    CHECK(css_color("rgb(100%, 50%, 0%)", 18, &c) && c == 0xFF7F00);
    CHECK(css_color("hsl(120, 100%, 25%)", 19, &c) && c == 0x007F00);
    CHECK(css_color("hsl(0 0% 100%)", 14, &c) && c == 0xFFFFFF);
    CHECK(css_color("  DarkRed ", 10, &c) && c == 0x8B0000);
    CHECK(!css_color("transparent", 11, &c));
    CHECK(!css_color("#12", 3, &c) && !css_color("banana", 6, &c) && !css_color("", 0, &c));
}

static void test_selectors(void)
{
    static const char text[] =
        "/* Kommentar */ p { color: red }\n"
        ".hinweis { color: #00f; background-color: #ffe }\n"
        "#kopf { color: green !important; }\n"
        "div p { font-weight: bold }\n"
        "ul > li { list-style: none }\n"
        "ul.menu li a { text-decoration: none; color: rgb(1,2,3) }\n"
        "p.hinweis.wichtig { font-style: italic }\n"
        "a:hover { color: #f00 }  a:visited { color: #800080 }\n"
        "a:link { color: #0645ad }\n"
        "input[type=text] { color: #f00 }\n"
        "h1 + p { color: #f00 }\n"
        "p::first-line { color: #f00 }\n"
        "h1, h2,h3 { font-size: 24px; text-align: center }\n"
        ".weg { display: none } .zeile { display: inline-block } .unsichtbar { visibility: hidden }\n"
        "span { color: #111 } span.x { color: #222 } div span.x { color: #333 }\n";
    css_sheet_t *sheet = css_create(1000);
    CHECK(sheet && css_add(sheet, text, sizeof(text) - 1));
    /* die fünf Regeln mit Zuständen, Attributen, Nachbarn fallen weg */
    CHECK(css_rule_count(sheet) == 17);

    css_node_t html = node("html", NULL, NULL, NULL);
    css_node_t body = node("BODY", NULL, NULL, &html);
    css_node_t div = node("div", "kopf", NULL, &body);
    css_node_t p = node("p", NULL, "hinweis wichtig", &div);
    css_node_t plain = node("p", NULL, NULL, &body);

    css_style_t s = match(sheet, &plain, NULL);
    CHECK((s.has & CSS_COLOR) && s.color == 0xFF0000 && !(s.has & CSS_WEIGHT));
    s = match(sheet, &p, NULL);
    CHECK(s.color == 0x0000FF);                                 /* Klasse schlägt Elementname */
    CHECK((s.has & CSS_BACKGROUND) && s.background == 0xFFFFEE);
    CHECK((s.has & CSS_WEIGHT) && s.bold && (s.has & CSS_SLANT) && s.italic);
    s = match(sheet, &div, "color: blue");
    CHECK(s.color == 0x008000);                                 /* !important schlägt das style-Attribut */
    s = match(sheet, &div, "color: blue !important");
    CHECK(s.color == 0x0000FF);
    s = match(sheet, &plain, "color:#abc;font-weight:700; background: url(x.png) #eee no-repeat");
    CHECK(s.color == 0xAABBCC && s.bold && s.background == 0xEEEEEE);

    css_node_t ul = node("ul", NULL, "menu gross", &body);
    css_node_t li = node("li", NULL, NULL, &ul);
    css_node_t a = node("a", NULL, NULL, &li);
    css_node_t deep = node("li", NULL, NULL, &a);
    s = match(sheet, &li, NULL);
    CHECK((s.has & CSS_LIST) && s.list_plain);
    s = match(sheet, &deep, NULL);
    CHECK(!(s.has & CSS_LIST));                                 /* kein direktes Kind von ul */
    s = match(sheet, &a, NULL);
    CHECK(s.color == 0x010203 && (s.has & CSS_UNDERLINE) && !s.underline);
    css_node_t other = node("a", NULL, NULL, &body);
    s = match(sheet, &other, NULL);
    CHECK(s.color == 0x0645AD);                                 /* a:link gilt, :hover und :visited nicht */
    css_node_t not_link = node("span", NULL, NULL, &body);
    s = match(sheet, &not_link, NULL);
    CHECK(s.color == 0x111111);                                 /* :link meint nur Links */

    css_node_t h2 = node("H2", NULL, NULL, &body);
    s = match(sheet, &h2, NULL);
    CHECK((s.has & CSS_SIZE) && s.size_px == 24 && s.align == CSS_CENTER);
    css_node_t gone = node("div", NULL, "x weg", &body);
    s = match(sheet, &gone, NULL);
    CHECK((s.has & CSS_DISPLAY) && s.display == CSS_NONE);
    css_node_t row = node("li", NULL, "zeile", &ul);
    s = match(sheet, &row, NULL);
    CHECK(s.display == CSS_INLINE);
    css_node_t ghost = node("p", NULL, "unsichtbar", &body);
    s = match(sheet, &ghost, NULL);
    CHECK((s.has & CSS_VISIBILITY) && s.hidden);

    /* Genauigkeit: div span.x > span.x > span */
    css_node_t span_x = node("span", NULL, "x", &div);
    css_node_t span_y = node("span", NULL, "x", &body);
    CHECK(match(sheet, &span_x, NULL).color == 0x333333);
    CHECK(match(sheet, &span_y, NULL).color == 0x222222);
    css_free(sheet);
}

static void test_cascade_and_values(void)
{
    static const char first[] =
        ":root { --text: #202122; --akzent: var(--fehlt, #36c); --kette: var(--text) }\n"
        ".nacht { --text: #fff }\n"                              /* gilt nicht der ganzen Seite */
        "body { color: var(--text); background: var(--grund, white) }\n"
        "a { color: var(--akzent) } em { color: var(--kette) } b { color: var(--gibtsnicht) }\n"
        "p { color: red } p { color: blue }\n"                   /* die spätere Regel gilt */
        "small { font-size: 80% } big { font-size: 1.5em } h4 { font-size: 12pt } h5 { font-size: 2rem }\n"
        "i { background: transparent } u { background: linear-gradient(#000, #fff) }\n";
    static const char second[] = "p { color: #123456 }  td { background: #f8f9fa; text-align: right }";
    css_sheet_t *sheet = css_create(1000);
    CHECK(css_add(sheet, first, sizeof(first) - 1) && css_add(sheet, second, sizeof(second) - 1));

    css_node_t html = node("html", NULL, "nacht", NULL);
    css_node_t body = node("body", NULL, NULL, &html);
    css_style_t s = match(sheet, &body, NULL);
    CHECK(s.color == 0x202122 && s.background == 0xFFFFFF);
    css_node_t a = node("a", NULL, NULL, &body), em = node("em", NULL, NULL, &body);
    css_node_t b = node("b", NULL, NULL, &body), p = node("p", NULL, NULL, &body);
    CHECK(match(sheet, &a, NULL).color == 0x3366CC);            /* Ersatzwert */
    CHECK(match(sheet, &em, NULL).color == 0x202122);           /* Variable in Variable */
    CHECK(!(match(sheet, &b, NULL).has & CSS_COLOR));           /* unbekannt: Angabe entfällt */
    CHECK(match(sheet, &p, NULL).color == 0x123456);            /* späteres Stylesheet gilt */

    css_node_t small = node("small", NULL, NULL, &body), big = node("big", NULL, NULL, &body);
    css_node_t h4 = node("h4", NULL, NULL, &body), h5 = node("h5", NULL, NULL, &body);
    CHECK(match(sheet, &small, NULL).size_percent == 80 && match(sheet, &small, NULL).size_px == 0);
    CHECK(match(sheet, &big, NULL).size_percent == 150);
    CHECK(match(sheet, &h4, NULL).size_px == 16 && match(sheet, &h5, NULL).size_px == 32);

    css_node_t i = node("i", NULL, NULL, &body), u = node("u", NULL, NULL, &body);
    s = match(sheet, &i, NULL);
    CHECK(!(s.has & CSS_BACKGROUND) && s.background_clear);
    s = match(sheet, &u, NULL);
    CHECK(!(s.has & CSS_BACKGROUND) && s.background_clear);     /* Verlauf: kein einfarbiger Grund */
    static const char third[] = ".rechts { float: right; clear: right } .links { float: left } "
                                ".doch-nicht { float: none } hr { clear: both }";
    CHECK(css_add(sheet, third, sizeof(third) - 1));
    css_node_t fr = node("figure", NULL, "rechts", &body), fl = node("img", NULL, "links", &body);
    css_node_t fn = node("div", NULL, "links doch-nicht", &body), hr = node("hr", NULL, NULL, &body);
    s = match(sheet, &fr, NULL);
    CHECK((s.has & CSS_FLOAT) && s.float_side == CSS_SIDE_RIGHT && s.clear == CSS_SIDE_RIGHT);
    CHECK(match(sheet, &fl, NULL).float_side == CSS_SIDE_LEFT);
    CHECK(match(sheet, &fn, NULL).float_side == CSS_SIDE_NONE);
    CHECK(match(sheet, &hr, NULL).clear == CSS_SIDE_BOTH);

    css_node_t td = node("td", NULL, NULL, &body);
    s = match(sheet, &td, "background:none");
    CHECK(!(s.has & CSS_BACKGROUND) && s.align == CSS_RIGHT);
    css_free(sheet);
}

static void test_media(void)
{
    static const char text[] =
        "p { color: #000001 }\n"
        "@media print { p { color: #000002 } }\n"
        "@media screen and (min-width: 600px) { p { font-weight: bold } }\n"
        "@media (max-width: 599px) { p { font-style: italic } .mobil { display: none } }\n"
        "@media screen and (min-width: 50em) { p { text-align: center } }\n"
        "@media (prefers-color-scheme: dark) { p { color: #000003 } }\n"
        "@media not print { p { text-decoration: underline } }\n"
        "@media print, screen { em { color: #000004 } }\n"
        "@supports (display: grid) { p { color: #000005 } }\n"
        "@font-face { font-family: x; src: url(x.woff) }\n"
        "@import url(anderes.css);\n"
        "@media screen { @media (min-width: 100px) { strong { color: #000006 } } }\n"
        "em { font-weight: bold }\n";
    css_node_t p = node("p", NULL, NULL, NULL), em = node("em", NULL, NULL, NULL);
    css_node_t strong = node("strong", NULL, NULL, NULL);

    css_sheet_t *wide = css_create(900);
    CHECK(css_add(wide, text, sizeof(text) - 1));
    css_style_t s = match(wide, &p, NULL);
    CHECK(s.color == 0x000001 && s.bold && !(s.has & CSS_SLANT) && s.align == CSS_CENTER &&
          s.underline);
    CHECK(match(wide, &em, NULL).color == 0x000004 && match(wide, &em, NULL).bold);
    CHECK(match(wide, &strong, NULL).color == 0x000006);
    css_free(wide);

    css_sheet_t *narrow = css_create(400);
    CHECK(css_add(narrow, text, sizeof(text) - 1));
    s = match(narrow, &p, NULL);
    CHECK(s.color == 0x000001 && !(s.has & CSS_WEIGHT) && s.italic && !(s.has & CSS_ALIGN));
    css_free(narrow);
}

/* Unsinn darf nichts zum Absturz bringen (der Test läuft mit Speicherprüfung). */
static void test_fuzz(void)
{
    static const char sample[] =
        ":root{--a:#123;--b:var(--a)}@media screen and (min-width:10px){a.b#c > d e,f{color:var(--b,red)!important;"
        "background:url('x;y}') rgb(1 2 3 / 4%);font-size:1.2em}}g:hover{x:y}h[i=\"j\"]{k:l}"
        "/* m */ n{display:none;visibility:hidden;list-style:none;text-align:center}";
    css_node_t root = node("html", NULL, NULL, NULL);
    css_node_t mid = node("d", "c", "b x y", &root);
    css_node_t leaf = node("e", "c", "b", &mid);
    srand(777);
    for (int round = 0; round < 30000; round++) {
        char text[400];
        size_t length = sizeof(sample) - 1;
        memcpy(text, sample, length);
        if (round % 3 == 0) {
            length = (size_t)rand() % sizeof(text);
            for (size_t i = 0; i < length; i++)
                text[i] = "{}:;,.#>()@!-_ abc019\"'/*\\%"[rand() % 28];
        } else {
            for (int i = 0; i < 1 + rand() % 6; i++)
                text[(size_t)rand() % length] = "{}:;,.#>()@!- a1\"'/*"[rand() % 20];
            if (rand() % 4 == 0)
                length = (size_t)rand() % length;
        }
        char *copy = malloc(length ? length : 1);       /* genau passend: Überläufe fallen auf */
        memcpy(copy, text, length);
        css_sheet_t *sheet = css_create(rand() % 2000);
        css_add(sheet, copy, length);
        css_style_t out;
        css_match(sheet, &leaf, copy, length, &out);
        css_match(sheet, &root, NULL, 0, &out);
        uint32_t color;
        css_color(copy, length, &color);
        css_free(sheet);
        free(copy);
    }
    CHECK(1);
}

int main(void)
{
    test_colors();
    test_selectors();
    test_cascade_and_values();
    test_media();
    test_fuzz();
    printf("%d Prüfungen an den Stylesheets, %d Fehler\n", checks, failures);
    return failures ? 1 : 0;
}
