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
                     classes ? strlen(classes) : 0, NULL, 0, parent };
    return n;
}

/* Wie node(), dazu der Text der Attribute, wie er im Start-Tag steht */
static css_node_t node_with(const char *tag, const char *attrs, const css_node_t *parent)
{
    css_node_t n = node(tag, NULL, NULL, parent);
    n.attrs = attrs;
    n.attrs_len = strlen(attrs);
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
    /* die vier Regeln mit Zuständen und Nachbarn fallen weg */
    CHECK(css_rule_count(sheet) == 18);

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

/* Bedingungen an Attribute: [name], [name=wert] und die Teilvergleiche */
static void test_attributes(void)
{
    static const char text[] =
        "figure[typeof~='mw:File/Thumb'] { float: right; clear: right }\n"
        ".inhalt figure[typeof~=\"mw:File\"].links { float: left }\n"
        "[hidden] { display: none }\n"
        "a[href^=\"http\"] { color: #010101 }\n"
        "a[href$='.pdf'] { color: #020202 }\n"
        "a[href*=beispiel] { font-weight: bold }\n"
        "p[lang|=de] { font-style: italic }\n"
        "input[type=CHECKBOX i] { visibility: hidden }\n"
        "input[type=radio] { color: #030303 }\n"
        "td[data-x=\"a,b]\"], th[ title ] { color: #040404 }\n"
        "div[class=\"\\41\"] { color: #f00 }\n"       /* maskiert: entfällt */
        "div[x { color: #f00 } div[x=] { color: #f00 } div[=x] { color: #f00 }\n"
        "span[a][b=c] { color: #050505 }\n";
    css_sheet_t *sheet = css_create(1000);
    CHECK(sheet && css_add(sheet, text, sizeof(text) - 1));
    CHECK(css_rule_count(sheet) == 12);

    css_node_t body = node("body", NULL, "inhalt", NULL);
    css_node_t thumb = node_with("figure", "class=\"mw-default-size\" typeof=\"mw:File/Thumb\" id=x",
                                 &body);
    css_style_t s = match(sheet, &thumb, NULL);
    CHECK((s.has & CSS_FLOAT) && s.float_side == CSS_SIDE_RIGHT && s.clear == CSS_SIDE_RIGHT);
    css_node_t frameless = node_with("figure", "typeof=\"mw:File/Frameless\"", &body);
    CHECK(!(match(sheet, &frameless, NULL).has & CSS_FLOAT));   /* ganzes Wort gefragt */
    css_node_t two = node_with("figure", "TYPEOF='x  mw:File mw:File/Thumb'", &body);
    two.classes = "links";
    two.classes_len = 5;
    CHECK(match(sheet, &two, NULL).float_side == CSS_SIDE_LEFT); /* genauere Regel gewinnt */
    css_node_t bare = node("figure", NULL, NULL, &body);
    CHECK(!(match(sheet, &bare, NULL).has & CSS_FLOAT));

    css_node_t hidden = node_with("div", "id=a hidden", &body);
    CHECK(match(sheet, &hidden, NULL).display == CSS_NONE);
    css_node_t hidden2 = node_with("div", "hidden=\"\" class=b", &body);
    CHECK(match(sheet, &hidden2, NULL).display == CSS_NONE);
    css_node_t shown = node_with("div", "title=\"hidden\" data-hidden=1", &body);
    CHECK(!(match(sheet, &shown, NULL).has & CSS_DISPLAY));     /* Wert oder Namensteil zählt nicht */

    css_node_t a1 = node_with("a", "href=\"https://www.beispiel.de/x.pdf\"", &body);
    s = match(sheet, &a1, NULL);
    CHECK(s.color == 0x020202 && s.bold);                       /* spätere Regel bei Gleichstand */
    css_node_t a2 = node_with("a", "href=/lokal.html", &body);
    CHECK(!(match(sheet, &a2, NULL).has & CSS_COLOR));
    css_node_t a3 = node_with("a", "href='http://x/'", &body);
    CHECK(match(sheet, &a3, NULL).color == 0x010101);

    css_node_t de = node_with("p", "lang=de-AT", &body), den = node_with("p", "lang=den", &body);
    CHECK(match(sheet, &de, NULL).italic && !(match(sheet, &den, NULL).has & CSS_SLANT));
    css_node_t box = node_with("input", "type=\"checkbox\" name=n", &body);
    CHECK(match(sheet, &box, NULL).hidden);
    css_node_t radio = node_with("input", "type=Radio", &body);
    CHECK(!(match(sheet, &radio, NULL).has & CSS_COLOR));       /* ohne \"i\" zählt die Schreibweise */
    css_node_t td = node_with("td", "data-x=\"a,b]\"", &body), th = node_with("th", "title", &body);
    CHECK(match(sheet, &td, NULL).color == 0x040404 && match(sheet, &th, NULL).color == 0x040404);
    css_node_t span = node_with("span", "b=c a", &body), half = node_with("span", "b=c", &body);
    CHECK(match(sheet, &span, NULL).color == 0x050505);
    CHECK(!(match(sheet, &half, NULL).has & CSS_COLOR));
    css_node_t div = node_with("div", "x class=A", &body);
    CHECK(!(match(sheet, &div, NULL).has & CSS_COLOR));         /* kaputte Regeln gelten nicht */

    /* beschädigte Attributtexte dürfen nichts kaputt machen */
    static const char *const broken[] = { "", "=", "a=", "a='", "a=\"b", "= = =", "a = 'b' c",
                                          "/ / a", "a=b=c", "'", "\"\"\"" };
    for (size_t i = 0; i < sizeof(broken) / sizeof(broken[0]); i++) {
        css_node_t n = node_with("span", broken[i], &body);
        (void)match(sheet, &n, NULL);
    }
    css_free(sheet);
}

/* Breite, und was Seiten nur scheinbar zeigen: beschnitten oder aus dem Bild geschoben */
static void test_boxes(void)
{
    static const char text[] =
        "#menue { float: left; width: 160px }\n"
        ".inhalt { float: left; width: 100% }\n"
        ".halb { width: 50.4% } .em { width: 10em } .vw { width: 25vw } .null { width: 0 }\n"
        ".auto { width: 300px } div.auto { width: auto }\n"
        ".rechnung { width: calc(100% - 20px) } .wort { width: fit-content } .minus { width: -5px }\n"
        ".vorleser { position: absolute; clip: rect(1px, 1px, 1px, 1px); clip-path: inset(50%) }\n"
        ".alt { clip: rect(0 0 0 0) } .sichtbar { clip: rect(0, 100px, 50px, 0) }\n"
        ".doch { clip: rect(0 0 0 0) } p.doch { clip: auto }\n"
        ".pfad { clip-path: inset(10%) } .kreis { clip-path: circle(0) }\n"
        ".oben { position: absolute; top: -1000em } .oben.offen { top: auto }\n"
        ".links { position: relative; left: -9999em } .knapp { top: -40px } .px { top: -9999px }\n"
        ".sprung:not(:focus) { clip: rect(1px,1px,1px,1px) } a.sprung:not( :hover ):not(:active) { color: #010203 }\n"
        ".sprung:not(.x) { color: #f00 } .sprung:not(:focus { color: #f00 } .sprung:not(:checked) { color: #f00 }\n";
    css_sheet_t *sheet = css_create(1200);
    CHECK(sheet && css_add(sheet, text, sizeof(text) - 1));
    css_node_t body = node("body", NULL, NULL, NULL);

    css_node_t menu = node("div", "menue", NULL, &body);
    css_style_t s = match(sheet, &menu, NULL);
    CHECK((s.has & CSS_WIDTH) && s.width_px == 160 && s.width_percent == 0 &&
          s.float_side == CSS_SIDE_LEFT);
    css_node_t content = node("div", NULL, "inhalt", &body);
    s = match(sheet, &content, NULL);
    CHECK((s.has & CSS_WIDTH) && s.width_px == 0 && s.width_percent == 100);
    css_node_t half = node("div", NULL, "halb", &body), em = node("div", NULL, "em", &body);
    css_node_t vw = node("div", NULL, "vw", &body), zero = node("div", NULL, "null", &body);
    CHECK(match(sheet, &half, NULL).width_percent == 50 && match(sheet, &em, NULL).width_px == 160);
    CHECK(match(sheet, &vw, NULL).width_px == 300);             /* ein Viertel von 1200 */
    s = match(sheet, &zero, NULL);
    CHECK((s.has & CSS_WIDTH) && s.width_px == 0 && s.width_percent == 0);
    css_node_t automatic = node("div", NULL, "auto", &body), fixed = node("p", NULL, "auto", &body);
    CHECK(!(match(sheet, &automatic, NULL).has & CSS_WIDTH));   /* genauere Regel nimmt sie zurück */
    CHECK(match(sheet, &fixed, NULL).width_px == 300);
    css_node_t calc = node("div", NULL, "rechnung", &body), word = node("div", NULL, "wort", &body);
    css_node_t minus = node("div", NULL, "minus", &body);
    CHECK(!(match(sheet, &calc, NULL).has & CSS_WIDTH) && !(match(sheet, &word, NULL).has & CSS_WIDTH) &&
          !(match(sheet, &minus, NULL).has & CSS_WIDTH));
    s = match(sheet, &menu, "width: 75%");
    CHECK(s.width_px == 0 && s.width_percent == 75);            /* style-Attribut geht vor */

    css_node_t reader = node("span", NULL, "vorleser", &body), old = node("span", NULL, "alt", &body);
    css_node_t seen = node("span", NULL, "sichtbar", &body), back = node("p", NULL, "doch", &body);
    css_node_t path = node("span", NULL, "pfad", &body), circle = node("span", NULL, "kreis", &body);
    s = match(sheet, &reader, NULL);
    CHECK((s.has & CSS_CLIP) && s.clipped);
    CHECK(match(sheet, &old, NULL).clipped && !match(sheet, &seen, NULL).clipped);
    CHECK(!match(sheet, &back, NULL).clipped && !match(sheet, &path, NULL).clipped &&
          !match(sheet, &circle, NULL).clipped);

    css_node_t up = node("ul", NULL, "oben", &body), open = node("ul", NULL, "oben offen", &body);
    css_node_t left = node("div", NULL, "links", &body), near = node("div", NULL, "knapp", &body);
    css_node_t px = node("div", NULL, "px", &body);
    CHECK(match(sheet, &up, NULL).off_top && !match(sheet, &open, NULL).off_top);
    CHECK(!match(sheet, &left, NULL).off_top && !match(sheet, &near, NULL).off_top &&
          match(sheet, &px, NULL).off_top);

    /* ":not(:focus)" gilt immer; andere Verneinungen entfallen */
    css_node_t jump = node("a", NULL, "sprung", &body);
    s = match(sheet, &jump, NULL);
    CHECK(s.clipped && s.color == 0x010203);
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
        "background:url('x;y}') rgb(1 2 3 / 4%);font-size:1.2em}}g:hover{x:y}e[i=\"j\"]{color:red}"
        "[a~=b],e[c|='d' i][e^=f],d[g$=h] e[i*=j]{float:left;width:12.5em;clip:rect(0 0 0 0);top:-99em}"
        "e{width:50%;clip-path:inset(50%);clear:both}"
        "/* m */ n{display:none;visibility:hidden;list-style:none;text-align:center}";
    css_node_t root = node("html", NULL, NULL, NULL);
    css_node_t mid = node("d", "c", "b x y", &root);
    css_node_t leaf = node("e", "c", "b", &mid);
    mid.attrs = "g=\"xh\" a='c b'";
    mid.attrs_len = strlen(mid.attrs);
    srand(777);
    for (int round = 0; round < 30000; round++) {
        char text[700];
        size_t length = sizeof(sample) - 1;
        memcpy(text, sample, length);
        if (round % 3 == 0) {
            length = (size_t)rand() % sizeof(text);
            for (size_t i = 0; i < length; i++)
                text[i] = "{}:;,.#>()@!-_ abc019\"'/*\\%[]=~^$|"[rand() % 35];
        } else {
            for (int i = 0; i < 1 + rand() % 6; i++)
                text[(size_t)rand() % length] = "{}:;,.#>()@!- a1\"'/*[]=~^$|"[rand() % 27];
            if (rand() % 4 == 0)
                length = (size_t)rand() % length;
        }
        char *copy = malloc(length ? length : 1);       /* genau passend: Überläufe fallen auf */
        memcpy(copy, text, length);
        css_sheet_t *sheet = css_create(rand() % 2000);
        css_add(sheet, copy, length);
        css_style_t out;
        leaf.attrs = copy;                              /* derselbe Unsinn auch als Attributtext */
        leaf.attrs_len = length;
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
    test_attributes();
    test_boxes();
    test_cascade_and_values();
    test_media();
    test_fuzz();
    printf("%d Prüfungen an den Stylesheets, %d Fehler\n", checks, failures);
    return failures ? 1 : 0;
}
