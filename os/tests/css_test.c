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
                     classes ? strlen(classes) : 0, NULL, 0, parent, NULL, NULL };
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
    /* die zwei Regeln mit einem Zustand, den es hier nie gibt (:visited), und einem fremden Scheinelement fallen weg */
    CHECK(css_rule_count(sheet) == 20);

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
        ".sprung:not(.x) { background-color: #f00 } .sprung:not(:focus { color: #f00 }\n"
        ".sprung:not(:checked) { font-weight: bold } .sprung:not(:has(b)) { color: #f00 }\n";
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

    /* Kinder nebeneinander: flex und grid */
    s = match(sheet, &half, "display: flex");
    CHECK((s.has & CSS_DISPLAY) && s.display == CSS_BLOCK && s.layout == CSS_FLEX && !(s.has & CSS_DIRECTION));
    s = match(sheet, &half, "display: inline-flex; flex-direction: column-reverse");
    CHECK(s.display == CSS_INLINE && s.layout == CSS_FLEX && (s.has & CSS_DIRECTION) && s.stacked);
    s = match(sheet, &half, "display: flex; flex-direction: row-reverse; flex-wrap: wrap");
    CHECK(!s.stacked && (s.has & CSS_WRAP) && s.wraps);
    s = match(sheet, &half, "display: flex; flex-flow: column nowrap");
    CHECK(s.stacked && (s.has & CSS_WRAP) && !s.wraps);
    s = match(sheet, &half, "display: flex; flex-flow: wrap");
    CHECK(!(s.has & CSS_DIRECTION) && s.wraps);
    CHECK(match(sheet, &half, "display: block").layout == CSS_FLOW);
    CHECK(match(sheet, &half, "justify-content: space-between").justify == CSS_PACK_BETWEEN &&
          match(sheet, &half, "justify-content: space-around").justify == CSS_PACK_BETWEEN &&
          match(sheet, &half, "justify-content: flex-end").justify == CSS_PACK_END &&
          match(sheet, &half, "justify-content: center").justify == CSS_PACK_CENTER &&
          match(sheet, &half, "justify-content: flex-start").justify == CSS_PACK_START);
    s = match(sheet, &half, "display: grid; grid-template-columns: 1fr 200px repeat(3, 1fr)");
    CHECK(s.layout == CSS_GRID && (s.has & CSS_TRACKS) && s.tracks == 5 && s.track_min_px == 0);
    s = match(sheet, &half, "grid-template-columns: repeat(2, minmax(0, 1fr) 40px) [ende]");
    CHECK(s.tracks == 4);
    s = match(sheet, &half, "grid-template-columns: repeat(auto-fill, minmax(12.5rem, 1fr))");
    CHECK(s.tracks == 0 && s.track_min_px == 200);
    s = match(sheet, &half, "grid-template-columns: repeat( auto-fit , 180px )");
    CHECK(s.tracks == 0 && s.track_min_px == 180);
    CHECK(match(sheet, &half, "grid-template-columns: none").tracks == 0 &&
          match(sheet, &half, "grid-template-columns: subgrid").tracks == 0);
    s = match(sheet, &half, "grid-template: auto 1fr / 15rem minmax(0, 1fr)");
    CHECK(s.tracks == 2 && (s.has & CSS_AREAS) && !s.areas);
    s = match(sheet, &half, "grid-template: 'kopf kopf' auto 'seite inhalt' 1fr / 200px 1fr");
    CHECK((s.has & CSS_AREAS) && s.areas);
    CHECK(match(sheet, &half, "grid-template-columns: 1fr 1fr; grid-template-areas: 'a b'").areas &&
          !match(sheet, &half, "grid-template-areas: none").areas);
    s = match(sheet, &half, "flex: 1");
    CHECK((s.has & CSS_GROW) && s.grows && !(s.has & CSS_BASIS));
    s = match(sheet, &half, "flex: 0 0 33.333%");
    CHECK(!s.grows && (s.has & CSS_BASIS) && s.basis_percent == 33 && s.basis_px == 0);
    s = match(sheet, &half, "flex: 1 1 300px");
    CHECK(s.grows && s.basis_px == 300 && s.basis_percent == 0);
    CHECK(match(sheet, &half, "flex: auto").grows && !match(sheet, &half, "flex: none").grows &&
          match(sheet, &half, "flex-grow: 2").grows && !match(sheet, &half, "flex-grow: 0").grows);
    s = match(sheet, &half, "flex-basis: 20em");
    CHECK((s.has & CSS_BASIS) && s.basis_px == 320);
    CHECK(!(match(sheet, &half, "flex-basis: auto").has & CSS_BASIS));

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

    /* ":not(:focus)" gilt immer; ":not(.x)" prüft wirklich; was sich nicht prüfen lässt, entfällt */
    css_node_t jump = node("a", NULL, "sprung", &body), jump_x = node("a", NULL, "sprung x", &body);
    s = match(sheet, &jump, NULL);
    CHECK(s.clipped && s.color == 0x010203 && (s.has & CSS_BACKGROUND) && (s.has & CSS_WEIGHT));
    s = match(sheet, &jump_x, NULL);
    CHECK(s.clipped && s.color == 0x010203 && !(s.has & CSS_BACKGROUND));
    css_free(sheet);
}

/* Ein kleiner Baum für Regeln, die nach Geschwistern und Zuständen fragen */
static css_node_t kids[12];
static int kid_count;

static const css_node_t *kid_sibling(const css_node_t *n, int direction)
{
    long i = n - kids;
    if (i < 0 || i >= kid_count)
        return NULL;
    for (i += direction; i >= 0 && i < kid_count; i += direction) {
        if (kids[i].parent == n->parent)
            return &kids[i];
    }
    return NULL;
}

static const css_node_t *kid_hover, *kid_focus;     /* worüber die Maus steht, was den Fokus hat */

static bool kid_state(const css_node_t *n, int which)
{
    if (which == CSS_STATE_EMPTY)
        return n == &kids[3];
    if (which == CSS_STATE_CHECKED)
        return n == &kids[9];
    if (which == CSS_STATE_FOCUS)
        return n == kid_focus;
    for (const css_node_t *at = which == CSS_STATE_HOVER ? kid_hover : kid_focus; at; at = at->parent) {
        if (at == n)
            return true;
    }
    return false;
}

static const css_tree_t kid_tree = { kid_sibling, kid_state };

static void test_structure(void)
{
    static const char text[] =
        "li:first-child { color: #010101 } li:last-child { color: #020202 }\n"
        "li:nth-child(2n) { background-color: #030303 } li:nth-child( odd ) { font-weight: bold }\n"
        "li:nth-last-child(2) { font-style: italic } li:nth-child(-n + 2) { text-decoration: underline }\n"
        "li + li { text-align: center } li:nth-child(3) ~ li { float: left }\n"
        "h2 + p { color: #040404 } h2 ~ p { background-color: #050505 } h2 + p + p { font-weight: bold }\n"
        "ul > li:not(:first-child):not(:last-child) { display: none }\n"
        "li:is(.a, .b, :hover) { visibility: hidden } :where(.a) { color: #060606 }\n"
        "li.a { color: #070707 } li:empty { text-align: right } p:only-child { color: #f00 }\n"
        "p:first-of-type { font-style: italic } p:last-of-type { text-decoration: underline }\n"
        "h2:only-of-type { color: #080808 } p:nth-of-type(2) { text-align: right }\n"
        "input:checked + span { color: #090909 } input:enabled { color: #0a0a0a } input:disabled { color: #0b0b0b }\n"
        "div:enabled { color: #f00 } :lang(de) > h2 { font-weight: bold } :lang(en) > h2 { font-style: italic }\n"
        "li:nth-child(2n+1 of .a) { color: #f00 } li:first-child:hover { color: #f00 } li:has(a) { color: #f00 }\n"
        "li:is(:hover, :focus) { color: #f00 } p:dir(rtl) { color: #f00 } p:dir(ltr) { clear: both }\n"
        "span:not(.a, .b) { font-weight: bold } li > :is(b, i):not(.c) { color: #f00 }\n";
    css_sheet_t *sheet = css_create(1000);
    CHECK(sheet && css_add(sheet, text, sizeof(text) - 1));

    css_node_t html = node("html", NULL, NULL, NULL);
    html.attrs = " lang=\"de-AT\"";
    html.attrs_len = strlen(html.attrs);
    css_node_t body = node("body", NULL, NULL, &html), ul = node("ul", NULL, NULL, &body);
    kid_count = 0;
    for (int i = 0; i < 5; i++)         /* kids[0..4]: fünf Listenpunkte; der zweite hat die Klasse a */
        kids[kid_count++] = node("li", NULL, i == 1 ? "a" : i == 4 ? "b" : NULL, &ul);
    kids[kid_count++] = node("h2", NULL, NULL, &body);          /* 5 */
    kids[kid_count++] = node("p", NULL, NULL, &body);           /* 6 */
    kids[kid_count++] = node("p", NULL, NULL, &body);           /* 7 */
    kids[kid_count++] = node_with("input", " disabled", &body); /* 8 */
    kids[kid_count++] = node("input", NULL, NULL, &body);       /* 9: angekreuzt (laut Baum) */
    kids[kid_count++] = node("span", NULL, "b", &body);         /* 10 */
    kids[kid_count++] = node("span", NULL, NULL, &body);        /* 11 */
    for (int i = 0; i < kid_count; i++)
        kids[i].tree = &kid_tree;

    css_style_t s[12];
    for (int i = 0; i < kid_count; i++)
        s[i] = match(sheet, &kids[i], NULL);
    /* Stellung unter den Geschwistern */
    CHECK(s[0].color == 0x010101 && s[4].color == 0x020202 && s[1].color == 0x070707 && !(s[2].has & CSS_COLOR));
    CHECK(!(s[0].has & CSS_BACKGROUND) && (s[1].has & CSS_BACKGROUND) && (s[3].has & CSS_BACKGROUND) &&
          !(s[4].has & CSS_BACKGROUND));
    CHECK(s[0].bold && !s[1].bold && s[2].bold && s[4].bold);
    CHECK(s[3].italic && !s[4].italic && !s[2].italic);
    CHECK(s[0].underline && s[1].underline && !s[2].underline);
    /* Nachbarn */
    CHECK(!(s[0].has & CSS_ALIGN) && s[1].align == CSS_CENTER && s[4].align == CSS_CENTER);
    CHECK(!(s[2].has & CSS_FLOAT) && (s[3].has & CSS_FLOAT) && (s[4].has & CSS_FLOAT));
    CHECK(s[6].color == 0x040404 && !(s[7].has & CSS_COLOR) && (s[6].has & CSS_BACKGROUND) &&
          (s[7].has & CSS_BACKGROUND) && s[7].bold && !s[6].bold);
    /* Verneinung und Auswahl aus mehreren */
    CHECK(!(s[0].has & CSS_DISPLAY) && s[1].display == CSS_NONE && s[3].display == CSS_NONE &&
          !(s[4].has & CSS_DISPLAY));
    CHECK(s[1].hidden && s[4].hidden && !s[0].hidden);
    CHECK(!s[10].bold && s[11].bold);
    /* ohne Inhalt, einziges Kind, Stellung unter Gleichnamigen */
    CHECK(s[3].align == CSS_RIGHT && s[2].align == CSS_CENTER);
    CHECK(s[6].italic && !s[7].italic && s[7].underline && !s[6].underline && s[5].color == 0x080808 &&
          s[7].align == CSS_RIGHT && !(s[6].has & CSS_ALIGN));
    /* Zustände von Feldern, Sprache, Schreibrichtung */
    CHECK(s[10].color == 0x090909 && !(s[11].has & CSS_COLOR));
    CHECK(s[9].color == 0x0A0A0A && s[8].color == 0x0B0B0B);
    CHECK(s[5].bold && !s[5].italic && (s[6].has & CSS_CLEAR));

    /* wer die Geschwister nicht kennt, auf den treffen solche Regeln nicht zu */
    css_node_t alone = node("li", NULL, NULL, &ul);
    css_style_t a = match(sheet, &alone, NULL);
    CHECK(!(a.has & CSS_COLOR) && !a.bold && !(a.has & CSS_ALIGN) && !(a.has & CSS_DISPLAY));
    css_free(sheet);

    /* Maus und Fokus: Die Regeln gelten, sobald das Abbild der Seite den Zustand meldet */
    static const char states[] =
        "li:hover { color: #0c0c0c } ul:hover > li.b { display: none } a:hover, li.a:focus { text-decoration: underline }\n"
        "li:not(:hover) { font-style: italic } ul:focus-within li { font-weight: bold } input:focus { color: #0d0d0d }\n"
        "li:focus-visible { background-color: #0e0e0e } li:active { color: #f00 } p:hover > b { width: 5px }\n";
    sheet = css_create(1000);
    CHECK(sheet && css_add(sheet, states, sizeof(states) - 1) && css_rule_count(sheet) == 9);
    for (int i = 0; i < kid_count; i++)
        kids[i].tree = &kid_tree;
    ul.tree = &kid_tree;
    kid_hover = kid_focus = NULL;
    css_style_t quiet = match(sheet, &kids[1], NULL);
    CHECK(!(quiet.has & CSS_COLOR) && quiet.italic && !quiet.bold && !(match(sheet, &kids[4], NULL).has & CSS_DISPLAY));
    kid_hover = &kids[1];
    css_style_t over = match(sheet, &kids[1], NULL), other = match(sheet, &kids[0], NULL);
    CHECK(over.color == 0x0C0C0C && !over.italic && !(other.has & CSS_COLOR) && other.italic);
    CHECK(match(sheet, &kids[4], NULL).display == CSS_NONE);    /* die Liste gilt als überfahren, wenn es ein Punkt darin ist */
    kid_focus = &kids[1];
    over = match(sheet, &kids[1], NULL);
    CHECK(over.underline && over.bold && (over.has & CSS_BACKGROUND) && match(sheet, &kids[0], NULL).bold);
    kid_focus = &kids[9];
    CHECK(match(sheet, &kids[9], NULL).color == 0x0D0D0D && !match(sheet, &kids[8], NULL).color &&
          !match(sheet, &kids[1], NULL).bold);
    /* ändert sich etwas, wenn die Maus kommt oder geht? 0 nein, 1 nur Farben, 2 auch die Anordnung */
    css_node_t link = node("a", NULL, NULL, &body), bold = node("b", NULL, NULL, &kids[6]);
    link.tree = bold.tree = &kid_tree;
    CHECK(css_state_effect(sheet, &kids[0], CSS_STATE_HOVER) == 2 &&    /* (kursiv ändert die Breite) */
          css_state_effect(sheet, &link, CSS_STATE_HOVER) == 1 &&
          css_state_effect(sheet, &ul, CSS_STATE_HOVER) == 2 &&
          css_state_effect(sheet, &kids[6], CSS_STATE_HOVER) == 2 &&
          css_state_effect(sheet, &bold, CSS_STATE_HOVER) == 0 &&
          css_state_effect(sheet, &kids[10], CSS_STATE_HOVER) == 0);
    CHECK(css_state_effect(sheet, &kids[9], CSS_STATE_FOCUS) == 1 &&
          css_state_effect(sheet, &kids[1], CSS_STATE_FOCUS) == 1 &&
          css_state_effect(sheet, &ul, CSS_STATE_FOCUS_WITHIN) == 2 &&
          css_state_effect(sheet, &kids[6], CSS_STATE_FOCUS) == 0 && css_state_effect(NULL, &link, CSS_STATE_HOVER) == 0);
    kid_hover = kid_focus = NULL;
    css_free(sheet);

    /* dieselben Mittel in querySelector */
    static const char query[] = "UL > LI:nth-child(2), p:last-of-type, li:not(.a):not(:first-child)";
    css_selector_t *sel = css_selector_parse(query, sizeof(query) - 1);
    CHECK(sel && css_selector_matches(sel, &kids[1]) && css_selector_matches(sel, &kids[7]) &&
          css_selector_matches(sel, &kids[2]) && !css_selector_matches(sel, &kids[0]) &&
          !css_selector_matches(sel, &kids[6]));
    css_selector_free(sel);
    CHECK(css_selector_parse("li:nth-child(x)", 15) == NULL && css_selector_parse("li +", 4) == NULL &&
          css_selector_parse("> li", 4) == NULL && css_selector_parse("li:not(", 7) == NULL);
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
        "@supports not (display: grid) { p { color: #000007 } }\n"
        "@layer basis { strong { font-style: italic } }\n"
        "@font-face { font-family: x; src: url(x.woff) }\n"
        "@import url(anderes.css);\n"
        "@media screen { @media (min-width: 100px) { strong { color: #000006 } } }\n"
        "@media screen and (max-width: calc(1120px - 1px)) { em { text-align: right } }\n"
        "@media screen and (min-width: calc(640px - 1px)) and (max-width: calc(1680px - 1px)) { strong { text-decoration: underline } }\n"
        "@media (width >= 500px) and (min-height: 10px) { strong { text-align: center } }\n"
        "@media (prefers-color-scheme: dark), (orientation: portrait) and (min-width: 1px) { em { text-decoration: underline } }\n"
        "em { font-weight: bold }\n";
    css_node_t p = node("p", NULL, NULL, NULL), em = node("em", NULL, NULL, NULL);
    css_node_t strong = node("strong", NULL, NULL, NULL);

    css_sheet_t *wide = css_create(900);
    CHECK(css_add(wide, text, sizeof(text) - 1));
    css_style_t s = match(wide, &p, NULL);
    /* @supports gilt (außer "not"): Was dort gefragt wird, kann der Browser inzwischen meist */
    CHECK(s.color == 0x000005 && s.bold && !(s.has & CSS_SLANT) && s.align == CSS_CENTER &&
          s.underline);
    CHECK(match(wide, &em, NULL).color == 0x000004 && match(wide, &em, NULL).bold);
    CHECK(match(wide, &strong, NULL).color == 0x000006 && match(wide, &strong, NULL).italic);
    /* Bedingungen mit calc(), mehreren Teilen, Vergleichen; Hochformat gilt bei 900x675 nicht */
    CHECK(match(wide, &em, NULL).align == CSS_RIGHT && match(wide, &strong, NULL).underline &&
          match(wide, &strong, NULL).align == CSS_CENTER && !match(wide, &em, NULL).underline);
    css_free(wide);

    css_sheet_t *narrow = css_create(400);
    CHECK(css_add(narrow, text, sizeof(text) - 1));
    s = match(narrow, &p, NULL);
    CHECK(s.color == 0x000005 && !(s.has & CSS_WEIGHT) && s.italic && !(s.has & CSS_ALIGN));
    CHECK(!match(narrow, &strong, NULL).underline && !(match(narrow, &strong, NULL).has & CSS_ALIGN) &&
          match(narrow, &em, NULL).align == CSS_RIGHT);
    css_free(narrow);
}

/* Unsinn darf nichts zum Absturz bringen (der Test läuft mit Speicherprüfung). */
static css_style_t full(const css_sheet_t *sheet, const css_node_t *n, const char *hints,
                        const char *style)
{
    css_style_t s;
    css_match_full(sheet, n, hints, hints ? strlen(hints) : 0, style, style ? strlen(style) : 0, &s);
    return s;
}

static bool len_is(css_len_t l, int px, int percent, int em)
{
    return l.kind == CSS_LEN_VALUE && l.px == px && l.percent == percent && l.em == em;
}

/* Die Angaben für Kästen: Abstände, Rahmen, Maße - und die Grundregeln des Browsers */
static void test_box_model(void)
{
    css_node_t div = node("div", NULL, "karte", NULL), p = node("p", NULL, NULL, NULL);
    css_node_t h1 = node("h1", NULL, NULL, NULL), span = node("span", NULL, NULL, NULL);
    css_node_t li = node("li", NULL, NULL, NULL), td = node("td", NULL, NULL, NULL);
    css_node_t script = node("script", NULL, NULL, NULL), pre = node("pre", NULL, NULL, NULL);
    css_sheet_t *sheet = css_create(1000);
    css_set_viewport_height(sheet, 600);
    CHECK(css_rule_count(sheet) == 0);          /* die Grundregeln zählen nicht als Regeln der Seite */

    /* Grundregeln: nur css_match_full sieht sie */
    css_style_t s = full(sheet, &p, NULL, NULL);
    CHECK(s.display_kind == CSS_D_BLOCK && len_is(s.margin[0], 0, 0, 1000) && len_is(s.margin[2], 0, 0, 1000) &&
          s.margin[1].kind == CSS_LEN_UNSET);
    CHECK(match(sheet, &p, NULL).has == 0 && match(sheet, &h1, NULL).has == 0);
    s = full(sheet, &h1, NULL, NULL);
    CHECK(s.display_kind == CSS_D_BLOCK && len_is(s.font_size, 0, 0, 2000) && s.font_weight == CSS_YES &&
          len_is(s.margin[0], 0, 0, 670));
    CHECK(full(sheet, &span, NULL, NULL).display_kind == 0 && full(sheet, &li, NULL, NULL).display_kind == CSS_D_LIST_ITEM &&
          full(sheet, &td, NULL, NULL).display_kind == CSS_D_TABLE_CELL && full(sheet, &script, NULL, NULL).display_kind == CSS_D_NONE);
    s = full(sheet, &pre, NULL, NULL);
    CHECK(s.white_space == CSS_WS_PRE && s.font_family == CSS_FF_MONO);

    static const char text[] =
        ".karte { margin: 10px 20px; padding: 1em 2% 3px; border: 1px solid #ccc; border-radius: 8px;"
        " width: calc(100% - 40px); max-width: 60rem; min-height: 50vh; box-sizing: border-box }\n"
        ".karte { margin-top: auto; border-left: 4px dashed red; border-bottom-color: transparent }\n"
        "p { margin: 0 }\n"
        "* { line-height: 1.5 }\n"
        "h1 { font: italic 700 24px/30px Georgia, serif; text-transform: uppercase }\n"
        "span { display: inline-block; vertical-align: middle; white-space: nowrap; position: relative;"
        " top: -2px; left: 1em; z-index: 5; overflow: hidden auto }\n"
        "li { display: flex; flex-flow: column wrap; justify-content: space-between; align-items: center;"
        " gap: 10px 2em; flex: 1 0 200px; order: 2 }\n"
        "td { display: grid; grid-template-columns: repeat(2, 1fr) 200px minmax(100px, 2fr) auto;"
        " grid-column: span 3; padding: 0 !important }\n"
        "pre { grid-template-columns: repeat(auto-fill, minmax(12em, 1fr)); grid-column: 1 / -1;"
        " font-size: clamp(12px, 2vw, 18px); width: min(500px, 80%); height: max(10px, 2em);"
        " background: linear-gradient(to right, #102030, #fff) }\n";
    CHECK(css_add(sheet, text, sizeof(text) - 1));

    s = full(sheet, &div, NULL, NULL);
    CHECK(s.margin[0].kind == CSS_LEN_AUTO && len_is(s.margin[1], 20000, 0, 0) && len_is(s.margin[2], 10000, 0, 0) &&
          len_is(s.margin[3], 20000, 0, 0));
    CHECK(len_is(s.padding[0], 0, 0, 1000) && len_is(s.padding[1], 0, 2000, 0) && len_is(s.padding[2], 3000, 0, 0) &&
          len_is(s.padding[3], 0, 2000, 0));
    CHECK(len_is(s.border_width[0], 1000, 0, 0) && s.border_style[0] == CSS_BS_SOLID && s.border_color[0] == 0xCCCCCC &&
          s.border_color_kind[0] == CSS_COLOR_VALUE);
    CHECK(len_is(s.border_width[3], 4000, 0, 0) && s.border_style[3] == CSS_BS_DASHED && s.border_color[3] == 0xFF0000 &&
          s.border_color_kind[2] == CSS_COLOR_TRANSPARENT && s.border_style[2] == CSS_BS_SOLID);
    CHECK(len_is(s.width, -40000, 100000, 0) && len_is(s.max_width, 960000, 0, 0) && len_is(s.min_height, 300000, 0, 0) &&
          len_is(s.radius, 8000, 0, 0) && s.box_sizing == CSS_BOX_BORDER && len_is(s.line_height, 0, 0, 1500));

    /* Rangfolge: Regel der Seite schlägt Grundregel und Attribut; style schlägt die Regel */
    s = full(sheet, &p, "margin-top: 7px; width: 300px", NULL);
    CHECK(len_is(s.margin[0], 0, 0, 0) && len_is(s.width, 300000, 0, 0));
    s = full(sheet, &p, "margin-top: 7px", "margin: 3px 4px");
    CHECK(len_is(s.margin[0], 3000, 0, 0) && len_is(s.margin[3], 4000, 0, 0));
    s = full(sheet, &td, NULL, "padding: 9px");
    CHECK(len_is(s.padding[0], 0, 0, 0));       /* !important der Regel geht dem style-Attribut vor */
    s = full(sheet, &h1, "font-size: 10px", NULL);
    CHECK(len_is(s.font_size, 24000, 0, 0) && len_is(s.line_height, 30000, 0, 0) && s.font_weight == CSS_YES &&
          s.font_slant == CSS_YES && s.font_family == CSS_FF_SANS && s.text_transform == CSS_TT_UPPER);

    s = full(sheet, &span, NULL, NULL);
    CHECK(s.display_kind == CSS_D_INLINE_BLOCK && s.vertical_align == CSS_VA_MIDDLE && s.white_space == CSS_WS_NOWRAP &&
          s.position == CSS_POS_RELATIVE && len_is(s.inset[0], -2000, 0, 0) && len_is(s.inset[3], 0, 0, 1000) &&
          s.inset[1].kind == CSS_LEN_UNSET && s.z_set && s.z_index == 5 && s.overflow == CSS_OV_SCROLL);
    s = full(sheet, &li, NULL, NULL);
    CHECK(s.display_kind == CSS_D_FLEX && s.flex_direction == CSS_FD_COLUMN && s.flex_wrap == CSS_FW_WRAP &&
          s.justify_content == CSS_JU_BETWEEN && s.align_items == CSS_AL_CENTER && len_is(s.gap_row, 10000, 0, 0) &&
          len_is(s.gap_column, 0, 0, 2000));
    CHECK(s.grow_set && s.flex_grow == 1000 && s.shrink_set && s.flex_shrink == 0 && len_is(s.flex_basis, 200000, 0, 0) &&
          s.order_set && s.order == 2);
    s = full(sheet, &td, NULL, NULL);
    CHECK(s.display_kind == CSS_D_GRID && s.grid_set && s.grid_count == 5 && s.grid[0].fr == 1000 && s.grid[1].fr == 1000 &&
          len_is(s.grid[2].size, 200000, 0, 0) && s.grid[2].fr == 0 && s.grid[3].fr == 2000 &&
          len_is(s.grid[3].size, 100000, 0, 0) && s.grid[4].automatic && !s.grid_auto_fill && s.grid_span == 3);
    s = full(sheet, &pre, NULL, NULL);
    CHECK(s.grid_auto_fill && s.grid_count == 1 && s.grid[0].fr == 1000 && len_is(s.grid[0].size, 0, 0, 12000) &&
          s.grid_full);
    CHECK(len_is(s.font_size, 18000, 0, 0) && len_is(s.width, 500000, 0, 0) && len_is(s.height, 0, 0, 2000) &&
          s.background_image == CSS_YES && s.gradient_set && s.gradient_color == 0x102030);

    /* einzelne Schreibweisen im style-Attribut */
    s = full(sheet, &span, NULL, "flex: none; margin: 0 auto; padding: 4px 8px 2px; border: none; inset: 0 auto;"
                                 " font-size: 120%; line-height: 150%; width: fit-content; opacity: 0;"
                                 " overflow-wrap: anywhere; border-collapse: collapse; list-style: square inside");
    CHECK(s.flex_grow == 0 && s.flex_shrink == 0 && s.flex_basis.kind == CSS_LEN_AUTO && s.margin[1].kind == CSS_LEN_AUTO &&
          len_is(s.margin[0], 0, 0, 0) && len_is(s.padding[2], 2000, 0, 0) && len_is(s.padding[3], 8000, 0, 0));
    CHECK(s.border_style[0] == CSS_BS_NONE && len_is(s.inset[0], 0, 0, 0) && s.inset[1].kind == CSS_LEN_AUTO &&
          len_is(s.font_size, 0, 0, 1200) && len_is(s.line_height, 0, 0, 1500) && s.width.kind == CSS_LEN_CONTENT &&
          s.transparent == CSS_YES && s.break_words == CSS_YES && s.border_collapse == CSS_YES && s.list_type == CSS_LT_SQUARE);
    s = full(sheet, &span, NULL, "margin: 1px 2px 3px 4px 5px; padding: -3px; width: blau; border-width: thin thick;"
                                 " font: caption; height: calc(2 * (10px + 5%)); max-width: none; min-width: calc(10px + 2)");
    CHECK(s.margin[0].kind == CSS_LEN_UNSET && s.padding[0].kind == CSS_LEN_UNSET && s.width.kind == CSS_LEN_UNSET &&
          len_is(s.border_width[0], 1000, 0, 0) && len_is(s.border_width[1], 5000, 0, 0) && s.font_size.kind == CSS_LEN_UNSET &&
          len_is(s.height, 20000, 10000, 0) && s.max_width.kind == CSS_LEN_NONE && s.min_width.kind == CSS_LEN_UNSET);

    /* Raster mit benannten Flächen, Platz eines Kinds darin, Symbole über Masken */
    s = full(sheet, &div, NULL, "grid-template-areas: 'kopf kopf' 'leiste inhalt' '. fuss'; grid-area: inhalt;"
                                " grid-row: 2 / 4; grid-column: 2; -webkit-mask-image: url(x.svg)");
    CHECK(s.area_rows == 3 && s.area_columns == 2 && s.area_map[0][0] == s.area_map[0][1] && s.area_map[0][0] != 0 &&
          s.area_map[1][0] != s.area_map[1][1] && s.area_map[2][0] == 0 && s.grid_area == s.area_map[1][1] &&
          s.grid_row == 2 && s.grid_row_span == 2 && s.grid_column == 2 && s.masked == CSS_YES);
    s = full(sheet, &div, NULL, "grid-template: \"a a\" min-content \"b c\" 1fr / 12rem minmax(0, 1fr)");
    CHECK(s.area_rows == 2 && s.area_columns == 2 && s.area_map[1][0] != s.area_map[1][1] && s.grid_count == 2 &&
          len_is(s.grid[0].size, 192000, 0, 0) && s.grid[1].fr == 1000);

    /* Scheinelemente: Die Regel gilt nicht dem Element; ihr "content" gibt es auf Nachfrage */
    static const char pseudo[] =
        ".q::before { content: '\\201E'; color: #ff0000; margin-right: 4px }\n"
        ".q:after { content: ' - ' attr(title); display: block }\n"
        ".q { color: #00ff00 }\n"
        "p::before { content: none }\n";
    css_node_t q = node("q", NULL, "q", NULL);
    CHECK(css_add(sheet, pseudo, sizeof(pseudo) - 1));
    s = full(sheet, &q, NULL, NULL);
    CHECK(s.has_before && s.has_after && s.color == 0x00FF00 && s.margin[1].kind == CSS_LEN_UNSET && s.display_kind == 0);
    CHECK(!match(sheet, &q, NULL).has_before && match(sheet, &q, NULL).color == 0x00FF00);
    const char *content = NULL;
    size_t content_length = 0;
    css_match_pseudo(sheet, &q, 1, &s, &content, &content_length);
    CHECK(content && content_length == 7 && memcmp(content, "'\\201E'", 7) == 0 && s.color == 0xFF0000 &&
          len_is(s.margin[1], 4000, 0, 0));
    css_match_pseudo(sheet, &q, 2, &s, &content, &content_length);
    CHECK(content && content_length == 17 && s.display_kind == CSS_D_BLOCK && !(s.has & CSS_COLOR));
    css_match_pseudo(sheet, &div, 1, &s, &content, &content_length);
    CHECK(!content && !full(sheet, &div, NULL, NULL).has_before && full(sheet, &p, NULL, NULL).has_before);

    /* Hintergrundbild: Adresse, Wiederholung, Lage, Größe; Farbverlauf mit Richtung */
    static const char back[] = "background: #102030 url( \"bilder/a b.png\" ) no-repeat right 10px / 50% auto";
    s = full(sheet, &div, NULL, back);
    CHECK(s.background_url && s.background_url_length == 14 && memcmp(s.background_url, "bilder/a b.png", 14) == 0 &&
          s.background_url >= back && s.background_url < back + sizeof(back));
    CHECK(s.background == 0x102030 && s.background_image == CSS_YES && s.background_repeat == CSS_REPEAT_NONE &&
          len_is(s.background_x, 0, 100000, 0) && len_is(s.background_y, 10000, 0, 0) && s.background_size == CSS_BG_GIVEN &&
          len_is(s.background_w, 0, 50000, 0) && s.background_h.kind == CSS_LEN_AUTO);
    s = full(sheet, &div, NULL, "background-image: url(x.png); background-size: cover; background-position: center bottom;"
                                " background-repeat: repeat-y");
    CHECK(s.background_size == CSS_BG_COVER && len_is(s.background_x, 0, 50000, 0) && len_is(s.background_y, 0, 100000, 0) &&
          s.background_repeat == CSS_REPEAT_Y && s.background_url_length == 5);
    s = full(sheet, &div, NULL, "background: url(x.png); background: none");
    CHECK(!s.background_url && s.background_image == CSS_NO);
    s = full(sheet, &div, NULL, "background: linear-gradient(to left, #111111, #222222 40%, #333333)");
    CHECK(s.gradient_set && s.gradient_color == 0x333333 && s.gradient_end == 0x111111 && s.gradient_across && !s.background_url);
    s = full(sheet, &div, NULL, "background-image: linear-gradient(180deg, rgba(0,0,0,.5), #444444)");
    CHECK(s.gradient_set && s.gradient_end == 0x444444 && !s.gradient_across);
    s = full(sheet, &div, NULL, "background: repeating-linear-gradient(45deg, #555555, #666666 10px)");
    CHECK(s.gradient_set && s.gradient_color == 0x555555 && s.gradient_end == 0x555555);

    css_len_t l;
    CHECK(css_length(sheet, "12.5pt", 6, &l) && l.px == 16666 && css_length(sheet, "50vw", 4, &l) && l.px == 500000 &&
          css_length(sheet, "10vh", 4, &l) && l.px == 60000 && css_length(sheet, "auto", 4, &l) && l.kind == CSS_LEN_AUTO &&
          !css_length(sheet, "12 px", 5, &l) && !css_length(sheet, "calc(", 5, &l) && !css_length(sheet, "", 0, &l));
    css_free(sheet);
}

static void test_fuzz(void)
{
    static const char sample[] =
        ":root{--a:#123;--b:var(--a)}@media screen and (min-width:10px){a.b#c > d e,f{color:var(--b,red)!important;"
        "background:url('x;y}') rgb(1 2 3 / 4%);font-size:1.2em}}g:hover{x:y}e[i=\"j\"]{color:red}"
        "[a~=b],e[c|='d' i][e^=f],d[g$=h] e[i*=j]{float:left;width:12.5em;clip:rect(0 0 0 0);top:-99em}"
        "e{width:50%;clip-path:inset(50%);clear:both}"
        "/* m */ n{display:none;visibility:hidden;list-style:none;text-align:center}"
        "e{margin:1px 2em;padding:calc(1px + 2%);border:1px solid red;width:min(1px,2%);font:bold 12px/1.2 a,b;"
        "grid-template-columns:repeat(2,minmax(1px,1fr)) [x] auto;flex:1 1 0;gap:1px 2px;grid-column:1 / span 2;"
        "background:linear-gradient(red,blue);height:clamp(1px,2vw,3em);border-color:red blue;inset:0 auto}"
        "e::before{content:'a\\201C' attr(x);background:url(\"a.png\") no-repeat 1px 2% / cover #fff;display:block}"
        "d:after{content:none;background-position:right top;background-size:1px auto;background-image:linear-gradient(to top,red,#000)}";
    css_node_t root = node("html", NULL, NULL, NULL);
    css_node_t mid = node("d", "c", "b x y", &root);
    css_node_t leaf = node("e", "c", "b", &mid);
    mid.attrs = "g=\"xh\" a='c b'";
    mid.attrs_len = strlen(mid.attrs);
    srand(777);
    for (int round = 0; round < 30000; round++) {
        char text[1500];
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
        css_match_full(sheet, &leaf, copy, length, copy, length, &out);
        css_match_full(sheet, &mid, NULL, 0, NULL, 0, &out);
        const char *content;
        size_t content_length;
        css_match_pseudo(sheet, &leaf, 1 + rand() % 2, &out, &content, &content_length);
        css_match_pseudo(sheet, &mid, 2, &out, &content, &content_length);
        css_len_t l;
        css_length(sheet, copy, length > 60 ? 60 : length, &l);
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
    test_structure();
    test_media();
    test_box_model();
    test_fuzz();
    printf("%d Prüfungen an den Stylesheets, %d Fehler\n", checks, failures);
    return failures ? 1 : 0;
}
