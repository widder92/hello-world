/*
 * Test für die Skripte von Webseiten (kernel/dom_js.c, domlib.js) auf dem
 * Entwicklungsrechner: Seiten mit Skripten laden, Klicks und Eingaben
 * hineinreichen, Zeitgeber ablaufen lassen - und nachsehen, was aus der Seite
 * geworden ist. Die Seiten prüfen sich größtenteils selbst und melden über
 * console.log "FEHLER: ..." oder am Ende "GEPRUEFT n".
 */
#include "../kernel/script.h"
#include "../kernel/css.h"
#include "../kernel/html.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#define strlcpy libc_strlcpy
#include <string.h>
#undef strlcpy

/*
 * Das Prüfwerkzeug soll lokale Variablen auf dem echten Stack lassen: Die
 * Koroutinen der Maschine kopieren Stücke davon um (kernel/js_co.c).
 */
const char *__asan_default_options(void) { return "detect_stack_use_after_return=0"; }

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

static uint64_t fake_ms = 5000;
static bool real_clock;
#include <time.h>
uint64_t time_ms(void)
{
    if (!real_clock)
        return fake_ms;
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000;
}
uint64_t time_epoch_ms(void) { return 1794670200000ull + fake_ms; }

static int checks, failures, reported, errors_seen;
static bool quiet;
static char last_log[400], last_alert[200];
static int confirms;

static void on_log(void *ctx, const char *text)
{
    (void)ctx;
    snprintf(last_log, sizeof(last_log), "%s", text);
    if (strncmp(text, "FEHLER", 6) == 0) {
        failures++;
        if (!quiet)
            printf("  %s\n", text);
    } else if (strncmp(text, "GEPRUEFT ", 9) == 0) {
        checks += atoi(text + 9);
        reported++;
    } else {
        errors_seen++;
        if (!quiet && getenv("ZEIGEN"))
            printf("  [Meldung] %s\n", text);
    }
}

static void on_alert(void *ctx, const char *text)
{
    (void)ctx;
    snprintf(last_alert, sizeof(last_alert), "%s", text);
}

static bool on_confirm(void *ctx, const char *text)
{
    (void)ctx;
    snprintf(last_alert, sizeof(last_alert), "%s", text);
    return ++confirms % 2 == 1;         /* abwechselnd Ja und Nein */
}

static bool page_is_private;            /* die nächste Seite kommt "aus dem eigenen Netz" */
static unsigned public_only_seen, public_only_missing;

static char *on_fetch(void *ctx, const char *url, bool public_only, size_t *len)
{
    (void)ctx;
    if (public_only)
        public_only_seen++;
    else
        public_only_missing++;
    const char *code = NULL;
    if (strcmp(url, "https://a.de/lib.js") == 0)
        code = "var ausDatei = 'geladen'; function ausDateiFn(x) { return x * 2; }";
    else if (strcmp(url, "https://a.de/pfad/zwei.js") == 0)
        code = "document.title += ' +zwei';";
    else if (strcmp(url, "https://a.de/kaputt.js") == 0)
        code = "var x = ;";
    else if (strcmp(url, "https://a.de/mod/haupt.js") == 0)
        code = "import gruss, { zahl, erhoehe, Tier as T } from './teile.js';\n"
               "import * as alles from './teile.js';\n"
               "import { kreisA } from './kreis-a.js';\n"
               "import { hilf } from 'werkzeug';\n"
               "import { tief } from 'paket/unter/tief.js';\n"
               "const vorher = zahl; erhoehe(); erhoehe();\n"
               "document.getElementById('m').textContent = [gruss('Welt'), vorher, zahl, new T('Hund').laut(),\n"
               "  Object.keys(alles).join('+'), alles.default === gruss, kreisA(), hilf(), tief,\n"
               "  typeof import.meta.url, import.meta.url.split('/').pop(), this === undefined].join(' | ');\n"
               "export const fertig = true;\n";
    else if (strcmp(url, "https://a.de/mod/teile.js") == 0)
        code = "export let zahl = 1;\nexport function erhoehe() { zahl++; }\n"
               "export class Tier { constructor(n) { this.n = n; } laut() { return this.n + ' macht Krach'; } }\n"
               "export default function gruss(wem) { return 'Hallo ' + wem; }\n"
               "window.geladen = (window.geladen || 0) + 1;\n";
    else if (strcmp(url, "https://a.de/mod/kreis-a.js") == 0)
        code = "import { kreisB } from './kreis-b.js';\nexport function kreisA() { return 'A' + kreisB(); }\nexport const a = 'a';\n";
    else if (strcmp(url, "https://a.de/mod/kreis-b.js") == 0)
        code = "import { a } from './kreis-a.js';\nexport function kreisB() { return 'B' + a; }\n";
    else if (strcmp(url, "https://a.de/lib/werkzeug.js") == 0)
        code = "export { hilf as default, hilf };\nfunction hilf() { return 'geholfen'; }\nexport * from '../mod/teile.js';\n";
    else if (strcmp(url, "https://cdn.de/paket/unter/tief.js") == 0)
        code = "export const tief = 'aus dem Paket';";
    else if (strcmp(url, "https://a.de/mod/spaeter.js") == 0)
        code = "const wert = await Promise.resolve('gewartet');\nexport default wert;\nexport const doppelt = wert + wert;\n";
    else if (strcmp(url, "https://a.de/mod/falsch.js") == 0)
        code = "import { gibtsnicht } from './teile.js';";
    else if (strcmp(url, "https://a.de/mod/wirft.js") == 0)
        code = "throw new Error('mit Absicht');";
    else if (strcmp(url, "https://a.de/alt.js") == 0)
        code = "document.title += ' +alt';";
    else if (strcmp(url, "https://a.de/spaet.js") == 0)
        code = "document.getElementById('aus').textContent += ' spaet:' + (window.zaehler = (window.zaehler || 0) + 1);";
    else if (strncmp(url, "https://cdn.de/jsonp?cb=", 24) == 0)
        code = "antwort({ wert: 42 });";
    if (!code)
        return NULL;
    *len = strlen(code);
    return strdup(code);
}

static void check(bool ok, const char *what)
{
    checks++;
    if (!ok) {
        failures++;
        printf("FEHLER: %s\n", what);
    }
}

/* Anfragen der Skripte (XMLHttpRequest, fetch): antwortet aus dem Stegreif und merkt sich alles */
static char request_seen[1200];
static int request_count;
static uint64_t cancel_at;              /* ab dann gilt: Das Fenster wird geschlossen */

static char request_log[3000];          /* alle Anfragen seit dem letzten Leeren: "VERFAHREN adresse[+cookies];" */

/*
 * Der Server der Tests - ein einzelner Austausch, wie ihn der Browser liefert.
 * a.de ist der Server der Seite; fremd.de antwortet je nach Pfad mit oder
 * ohne Erlaubnis für fremde Seiten.
 */
static bool on_request(void *ctx, const char *method, const char *url, const char *headers,
                       const char *body, size_t body_len, unsigned flags, script_reply_t *reply)
{
    (void)ctx;
    (void)body_len;
    bool credentials = flags & SCRIPT_COOKIES;
    if (flags & SCRIPT_PUBLIC_ONLY)
        public_only_seen++;
    else
        public_only_missing++;
    request_count++;
    snprintf(request_seen, sizeof(request_seen), "%s %s <%s> [%s]%s", method, url, headers,
             body ? body : "-", credentials ? " +cookies" : "");
    size_t used = strlen(request_log);
    snprintf(request_log + used, sizeof(request_log) - used, "%s %s%s;", method, url,
             credentials ? "+cookies" : "");
    if (strstr(url, "/fehlt"))
        return false;
    const char *text = strstr(url, "/daten") ? "{\"a\":1}" : "Text vom Server";
    const char *type = strstr(url, "/daten") ? "application/json" : "text/plain";
    char extra[600] = "";
    reply->status = strstr(url, "/verboten") ? 403 : 200;
    if (strcmp(url, "https://a.de/weiter") == 0) {
        reply->status = 302;
        strcpy(extra, "Location: https://fremd.de/offen\n");
    } else if (strcmp(url, "https://a.de/umweg") == 0) {
        reply->status = 307;
        strcpy(extra, "Location: https://fremd.de/hin\n");
    } else if (strcmp(url, "https://fremd.de/hin") == 0) {
        reply->status = 302;
        strcpy(extra, "Location: https://dritter.de/ziel\nAccess-Control-Allow-Origin: https://a.de\n");
    } else if (strcmp(url, "https://dritter.de/ziel") == 0) {
        /* erlaubt a.de ausdrücklich - aber gefragt wird hier auf Geheiß von fremd.de */
        strcpy(extra, strstr(headers, "Origin: null\r\n") ? "Access-Control-Allow-Origin: null\n"
                                                           : "Access-Control-Allow-Origin: https://a.de\nX-Getaeuscht: ja\n");
    } else if (strcmp(url, "https://a.de/im-haus") == 0) {
        reply->status = 303;
        strcpy(extra, "Location: /daten\n");
    } else if (strcmp(url, "https://a.de/kopf") == 0) {
        strcpy(extra, "X-Eigen-Antwort: sichtbar\nSet-Cookie: geheim=1\n");
    } else if (strncmp(url, "https://fremd.de/", 17) == 0) {
        const char *path = url + 16;
        bool options = strcmp(method, "OPTIONS") == 0;
        if (strcmp(path, "/offen") == 0)
            strcpy(extra, "Access-Control-Allow-Origin: *\nX-Verborgen: nein\n");
        else if (strcmp(path, "/zeig") == 0)
            strcpy(extra, "Access-Control-Allow-Origin: https://a.de\nAccess-Control-Expose-Headers: X-Zeig\n"
                          "X-Zeig: ja\nX-Verborgen: nein\n");
        else if (strcmp(path, "/mitcookies") == 0)
            strcpy(extra, "Access-Control-Allow-Origin: https://a.de\nAccess-Control-Allow-Credentials: true\n");
        else if (strcmp(path, "/stern") == 0)
            strcpy(extra, "Access-Control-Allow-Origin: *\nAccess-Control-Allow-Credentials: true\n");
        else if (strcmp(path, "/anderer") == 0)
            strcpy(extra, "Access-Control-Allow-Origin: https://jemand-anders.de\n");
        else if (strcmp(path, "/api") == 0)
            strcpy(extra, options ? "Access-Control-Allow-Origin: https://a.de\nAccess-Control-Allow-Methods: PUT, DELETE\n"
                                    "Access-Control-Allow-Headers: X-Eigen, Content-Type\n"
                                  : "Access-Control-Allow-Origin: https://a.de\n");
        else if (strcmp(path, "/streng") == 0)
            strcpy(extra, options ? "Access-Control-Allow-Origin: https://a.de\nAccess-Control-Allow-Methods: GET\n"
                                  : "Access-Control-Allow-Origin: https://a.de\n");
        if (options)
            text = "";
    }
    char all[900];
    snprintf(all, sizeof(all), "Content-Type: %s\n%s", type, extra);
    reply->body = strdup(text);
    reply->length = strlen(text);
    reply->headers = strdup(all);
    snprintf(reply->content_type, sizeof(reply->content_type), "%s", type);
    return true;
}

static bool on_cancelled(void *ctx)
{
    (void)ctx;
    return cancel_at && time_ms() >= cancel_at;
}

static void note_style(void *ctx, bool linked, const char *text, size_t len)
{
    if (!linked)
        css_add(ctx, text, len);
}

/* Wie load(), aber die Skripte kennen die Gestaltung der Seite (ihre <style>-Angaben) */
static script_t *load_styled(const char *html, css_sheet_t **css, uint32_t budget)
{
    script_host_t host = { .fetch = NULL, .request = on_request, .log = on_log, .alert = on_alert,
                           .confirm = on_confirm, .cancelled = on_cancelled, .width = 900, .height = 600,
                           .budget_ms = budget };
    script_t *s = script_create(html, strlen(html), "https://a.de/pfad/seite.html?x=1#teil", &host);
    if (!s)
        return NULL;
    *css = css_create(900);
    html_find_styles(html, strlen(html), "https://a.de/pfad/seite.html", note_style, *css);
    script_set_styles(s, *css);
    script_boot(s);
    return s;
}

static script_t *load(const char *html, uint32_t budget)
{
    script_host_t host = { .fetch = on_fetch, .request = on_request, .log = on_log, .alert = on_alert,
                           .confirm = on_confirm, .cancelled = on_cancelled, .private_page = page_is_private,
                           .width = 900, .height = 600, .budget_ms = budget };
    script_t *s = script_create(html, strlen(html), "https://a.de/pfad/seite.html?x=1#teil", &host);
    if (s)
        script_boot(s);
    return s;
}

static bool page_has(script_t *s, const char *needle)
{
    size_t len;
    char *html = script_html(s, &len);
    bool found = html && strstr(html, needle) != NULL;
    if (!found && getenv("ZEIGEN"))
        printf("  Seite: %s\n", html ? html : "(nichts)");
    free(html);
    return found;
}

/* Nummer des ersten Elements, dessen Start-Tag marker enthält (aus data-hx gelesen) */
static uint32_t node_with(script_t *s, const char *marker)
{
    size_t len;
    char *html = script_html(s, &len);
    uint32_t id = 0;
    char *at = html ? strstr(html, marker) : NULL;
    while (at && at > html && *at != '<')
        at--;
    char *hx = at ? strstr(at, "data-hx=") : NULL;
    if (hx)
        id = (uint32_t)atoi(hx + 8);
    free(html);
    return id;
}

static void run_timers(script_t *s, int rounds)
{
    for (int i = 0; i < rounds; i++) {
        int64_t wait = -1;
        script_tick(s, &wait);
        if (wait < 0)
            break;
        fake_ms += (uint64_t)wait;
    }
    script_tick(s, NULL);
}

#define PRE "<script>var n = 0; function check(ok, was) { n++; if (!ok) console.log('FEHLER: ' + was); } " \
            "function eq(ist, soll, was) { n++; if (ist !== soll) console.log('FEHLER: ' + was + ' - ist ' + JSON.stringify(ist) + ', soll ' + JSON.stringify(soll)); } " \
            "function fertig() { console.log('GEPRUEFT ' + n); }</script>"

static const char page_dom[] =
    "<!DOCTYPE html><html lang=de><head><title>Start</title>" PRE "</head><body class='a b'>"
    "<h1 id=kopf>Hallo <b>Welt</b></h1><ul id=liste><li class=x>eins</li><li class='x y'>zwei</li><li>drei</li></ul>"
    "<form id=f action=/senden><input name=wer value=Erika><input type=checkbox name=c checked>"
    "<select name=s><option value=a>A<option value=b selected>B</select><textarea name=t>lang</textarea>"
    "<button id=knopf>Los</button></form><p id=p style='color: red; margin: 0'>Absatz</p>"
    "<a id=link href='../x.html?a=1'>Link</a><div id=daten data-user-id=7 hidden>versteckt</div>"
    "<script>\n"
    "eq(document.title, 'Start', 'Titel lesen'); document.title = 'Neu'; eq(document.title, 'Neu', 'Titel setzen');\n"
    "eq(document.documentElement.tagName, 'HTML', 'documentElement'); eq(document.body.className, 'a b', 'body');\n"
    "eq(document.documentElement.lang, 'de', 'gespiegeltes Attribut'); eq(document.readyState, 'loading', 'readyState');\n"
    "var kopf = document.getElementById('kopf'); check(kopf === document.getElementById('kopf'), 'dasselbe Objekt');\n"
    "eq(kopf.textContent, 'Hallo Welt', 'textContent'); eq(kopf.innerHTML, 'Hallo <b>Welt</b>', 'innerHTML');\n"
    "eq(kopf.outerHTML, '<h1 id=\"kopf\">Hallo <b>Welt</b></h1>', 'outerHTML'); eq(kopf.nodeType, 1, 'nodeType');\n"
    "eq(kopf.firstChild.nodeType, 3, 'Textknoten'); eq(kopf.firstChild.nodeValue, 'Hallo ', 'nodeValue');\n"
    "eq(kopf.lastChild.tagName, 'B', 'lastChild'); eq(kopf.childNodes.length, 2, 'childNodes'); eq(kopf.children.length, 1, 'children');\n"
    "check(kopf.parentNode === document.body && kopf.parentElement === document.body, 'parentNode');\n"
    "eq(kopf.nextElementSibling.id, 'liste', 'nextElementSibling'); eq(kopf.previousElementSibling, null, 'previousElementSibling');\n"
    "check(kopf instanceof HTMLElement && kopf instanceof Element && kopf instanceof Node, 'instanceof');\n"
    "check(kopf instanceof HTMLHeadingElement && !(kopf instanceof HTMLInputElement), 'instanceof nach Elementart');\n"
    "check(document instanceof Document && kopf.ownerDocument === document && kopf.isConnected, 'document');\n"
    "var liste = document.querySelector('#liste'), punkte = document.querySelectorAll('ul > li.x');\n"
    "eq(punkte.length, 2, 'querySelectorAll'); eq(document.querySelectorAll('li.x.y')[0].textContent, 'zwei', 'zwei Klassen');\n"
    "eq(liste.getElementsByTagName('LI').length, 3, 'getElementsByTagName'); eq(document.getElementsByClassName('x y').length, 1, 'getElementsByClassName');\n"
    "eq(document.querySelector('input[type=checkbox]').name, 'c', 'Attribut-Auswahl'); eq(document.querySelector('.fehlt'), null, 'nichts gefunden');\n"
    "check(punkte[0].matches('li.x') && !punkte[0].matches('p'), 'matches'); check(punkte[0].closest('ul') === liste && punkte[0].closest('table') === null, 'closest');\n"
    "try { document.querySelector('li:hover'); check(false, 'ungültige Auswahl'); } catch (e) { check(e instanceof SyntaxError, 'SyntaxError bei ungültiger Auswahl'); }\n"
    "var neu = document.createElement('li'); neu.textContent = 'vier & <fünf>'; neu.className = 'neu'; liste.appendChild(neu);\n"
    "eq(liste.children.length, 4, 'appendChild'); eq(liste.lastElementChild.innerHTML, 'vier &amp; &lt;fünf&gt;', 'Text wird maskiert');\n"
    "liste.insertBefore(neu, liste.firstChild); eq(liste.firstElementChild.className, 'neu', 'insertBefore verschiebt'); eq(liste.children.length, 4, 'nicht verdoppelt');\n"
    "liste.removeChild(neu); eq(liste.children.length, 3, 'removeChild'); check(neu.parentNode === null && !neu.isConnected, 'entfernt');\n"
    "try { liste.removeChild(neu); check(false, 'removeChild zweimal'); } catch (e) { check(true, 'Fehler bei fremdem Kind'); }\n"
    "try { neu.appendChild(neu); check(false, 'in sich selbst'); } catch (e) { check(true, 'kein Kreis'); }\n"
    "liste.insertAdjacentHTML('beforeend', '<li>fünf</li><li>sechs</li>'); eq(liste.children.length, 5, 'insertAdjacentHTML');\n"
    "liste.children[4].remove(); liste.prepend('vorn'); liste.append(neu, 'hinten'); eq(liste.firstChild.nodeValue, 'vorn', 'prepend'); eq(liste.lastChild.nodeValue, 'hinten', 'append');\n"
    "var kopie = liste.cloneNode(true); eq(kopie.children.length, liste.children.length, 'cloneNode tief'); check(kopie !== liste && kopie.parentNode === null, 'Kopie ist frei');\n"
    "eq(liste.cloneNode(false).childNodes.length, 0, 'cloneNode flach'); check(liste.contains(neu) && !neu.contains(liste), 'contains');\n"
    "var frag = document.createDocumentFragment(); frag.appendChild(document.createElement('i')); frag.appendChild(document.createTextNode('t'));\n"
    "kopf.appendChild(frag); eq(kopf.childNodes.length, 4, 'Fragment gibt Kinder ab'); eq(frag.childNodes.length, 0, 'Fragment leer');\n"
    "kopf.replaceChild(document.createElement('u'), kopf.lastChild); eq(kopf.lastChild.tagName, 'U', 'replaceChild');\n"
    "var p = document.getElementById('p'); eq(p.getAttribute('style'), 'color: red; margin: 0', 'getAttribute'); eq(p.getAttribute('fehlt'), null, 'fehlendes Attribut');\n"
    "p.setAttribute('TITLE', 'Tipp'); eq(p.title, 'Tipp', 'setAttribute'); check(p.hasAttribute('title'), 'hasAttribute'); p.removeAttribute('title'); check(!p.hasAttribute('title'), 'removeAttribute');\n"
    "try { p.setAttribute('a b', 'x'); check(false, 'ungültiger Name'); } catch (e) { check(true, 'ungültiger Attributname abgelehnt'); }\n"
    "eq(p.style.color, 'red', 'style lesen'); p.style.backgroundColor = '#ff0'; p.style.color = 'blue'; eq(p.getAttribute('style'), 'margin: 0; background-color: #ff0; color: blue;', 'style setzen');\n"
    "p.style.display = 'none'; eq(p.style.display, 'none', 'display'); p.style.display = ''; eq(p.style.display, '', 'style entfernen'); eq(p.style.fehlt, '', 'unbekannt ist leer');\n"
    "p.style.setProperty('font-size', '12px'); eq(p.style.getPropertyValue('font-size'), '12px', 'setProperty'); p.style.cssText = 'top: 1px'; eq(p.style.top, '1px', 'cssText');\n"
    "p.classList.add('eins', 'zwei'); check(p.classList.contains('zwei'), 'classList.add'); p.classList.remove('eins'); eq(p.className, 'zwei', 'classList.remove');\n"
    "eq(p.classList.toggle('drei'), true, 'toggle an'); eq(p.classList.toggle('drei'), false, 'toggle aus'); eq(p.classList.length, 1, 'classList.length');\n"
    "var daten = document.getElementById('daten'); eq(daten.dataset.userId, '7', 'dataset lesen'); daten.dataset.neuerWert = 'x'; eq(daten.getAttribute('data-neuer-wert'), 'x', 'dataset setzen');\n"
    "eq(daten.dataset.fehlt, undefined, 'dataset: fehlt'); check(daten.hidden, 'hidden'); daten.hidden = false; check(!daten.hasAttribute('hidden'), 'hidden aus');\n"
    "var link = document.getElementById('link'); eq(link.href, 'https://a.de/x.html?a=1', 'href vollständig'); eq(link.getAttribute('href'), '../x.html?a=1', 'href wie geschrieben');\n"
    "var f = document.forms[0], wer = f.elements.wer; eq(f.elements.length, 5, 'Felder des Formulars'); eq(wer.value, 'Erika', 'value'); wer.value = 'Max'; eq(wer.value, 'Max', 'value setzen');\n"
    "eq(wer.getAttribute('value'), 'Erika', 'Attribut bleibt'); eq(wer.defaultValue, 'Erika', 'defaultValue'); check(wer.form === f, 'form');\n"
    "var c = f.elements.c; eq(c.checked, true, 'checked'); c.checked = false; eq(c.checked, false, 'checked setzen'); check(c.hasAttribute('checked'), 'Attribut bleibt');\n"
    "var s = f.elements.s; eq(s.value, 'b', 'Auswahlliste'); eq(s.selectedIndex, 1, 'selectedIndex'); s.value = 'a'; eq(s.selectedIndex, 0, 'value setzt Auswahl'); eq(s.options.length, 2, 'options');\n"
    "check(s.options[0].selected && !s.options[1].selected, 'selected'); s.selectedIndex = 1; eq(s.value, 'b', 'selectedIndex setzen');\n"
    "var t = f.elements.t; eq(t.value, 'lang', 'textarea'); t.value = 'neu'; eq(t.value, 'neu', 'textarea setzen'); eq(f.action, 'https://a.de/senden', 'action');\n"
    "eq(location.href, 'https://a.de/pfad/seite.html?x=1#teil', 'location.href'); eq(location.protocol, 'https:', 'protocol'); eq(location.hostname, 'a.de', 'hostname');\n"
    "eq(location.pathname, '/pfad/seite.html', 'pathname'); eq(location.search, '?x=1', 'search'); eq(location.hash, '#teil', 'hash'); eq(location.origin, 'https://a.de', 'origin');\n"
    "check(window === this && window.window === window && self === window && window.document === document, 'window'); eq(innerWidth, 900, 'innerWidth');\n"
    "check(/HelloOS/.test(navigator.userAgent), 'navigator'); eq(typeof localStorage.getItem, 'function', 'localStorage'); localStorage.setItem('k', 5); eq(localStorage.getItem('k'), '5', 'Speicher');\n"
    "eq(new URL('/a?b=1&c=2#d', location.href).searchParams.get('c'), '2', 'URL'); eq(new URLSearchParams('a=1&b=%C3%A4').get('b'), 'ä', 'URLSearchParams'); eq(atob(btoa('Hallo!')), 'Hallo!', 'btoa/atob');\n"
    "check(matchMedia('(min-width: 800px)').matches && !matchMedia('(max-width: 800px)').matches, 'matchMedia'); eq(getComputedStyle(p).top, '1px', 'getComputedStyle');\n"
    "check(matchMedia('screen and (min-width: 50em) and (max-width: 1200px)').matches && !matchMedia('print').matches &&"
    " matchMedia('(width >= 900px)').matches && !matchMedia('(width < 900px)').matches && matchMedia('not (max-width: 500px)').matches &&"
    " matchMedia('(max-width: 100px), (min-height: 600px)').matches && !matchMedia('(prefers-color-scheme: dark)').matches &&"
    " matchMedia('(orientation: landscape)').matches, 'matchMedia: Verknüpfungen, em, Vergleiche');\n"
    "eq(document.documentElement.clientWidth, 900, 'Breite des Fensters'); eq(document.body.clientHeight, 600, 'Höhe des Fensters');"
    " eq(document.documentElement.getBoundingClientRect().width, 900, 'Fläche der Seite');\n"
    "document.cookie = 'a=1; path=/'; document.cookie = 'b=2'; eq(document.cookie, 'a=1; b=2', 'cookie'); eq(document.currentScript.tagName, 'SCRIPT', 'currentScript');\n"
    "eq(document.getElementsByName('wer').length, 1, 'getElementsByName'); eq(document.images.length, 0, 'images'); eq(document.links.length, 1, 'links');\n"
    "var bild = new Image(10, 20); eq(bild.tagName, 'IMG', 'Image'); eq(bild.width, '10', 'Breite'); eq(new Option('T', 'w').value, 'w', 'Option');\n"
    "fertig();\n"
    "</script></body></html>";

static const char page_events[] =
    "<html><head>" PRE "</head><body onload=\"folge.push('body-onload')\"><div id=aussen><p id=mitte><button id=knopf onclick=\"folge.push('attr:' + event.type + ':' + (this === event.target))\">Klick</button></p></div>"
    "<a id=link href=/ziel>Link</a><a id=halt href=/nicht>Halt</a><form id=f action=/senden><input id=feld name=q><input type=checkbox id=haken></form>"
    "<p id=ausgabe></p>"
    "<script>\n"
    "var folge = [], knopf = document.getElementById('knopf'), aussen = document.getElementById('aussen');\n"
    "document.addEventListener('DOMContentLoaded', function (e) { folge.push('DCL:' + document.readyState + ':' + (e.target === document)); });\n"
    "window.addEventListener('load', function () { folge.push('load:' + document.readyState); });\n"
    "window.addEventListener('DOMContentLoaded', function () { folge.push('DCL am Fenster'); });\n"
    "knopf.addEventListener('click', function (e) { folge.push('knopf'); eq(e.target, knopf, 'target'); eq(e.currentTarget, knopf, 'currentTarget'); eq(this, knopf, 'this'); check(e.bubbles && e.cancelable, 'bubbles'); });\n"
    "document.getElementById('mitte').addEventListener('click', function (e) { folge.push('mitte'); eq(e.currentTarget.id, 'mitte', 'currentTarget steigt auf'); });\n"
    "aussen.addEventListener('click', function (e) { folge.push('aussen'); e.stopPropagation(); });\n"
    "document.addEventListener('click', function (e) { folge.push('dokument:' + e.target.id); });\n"
    "var einmal = 0; aussen.addEventListener('einmal', function () { einmal++; }, { once: true });\n"
    "function weg() { folge.push('NIE'); } knopf.addEventListener('click', weg); knopf.removeEventListener('click', weg);\n"
    "document.getElementById('halt').onclick = function (e) { folge.push('halt'); return false; };\n"
    "document.getElementById('f').addEventListener('submit', function (e) { folge.push('submit'); if (document.getElementById('feld').value === '') e.preventDefault(); });\n"
    "document.getElementById('feld').addEventListener('input', function (e) { document.getElementById('ausgabe').textContent = 'getippt: ' + e.target.value; });\n"
    "document.getElementById('feld').addEventListener('change', function (e) { folge.push('change:' + this.value); });\n"
    "document.getElementById('haken').addEventListener('change', function () { folge.push('haken:' + this.checked); });\n"
    "function pruefe() {\n"
    "  eq(folge.join(' '), 'DCL:interactive:true DCL am Fenster body-onload load:complete', 'Reihenfolge beim Laden');\n"
    "  folge = []; knopf.click(); eq(folge.join(' '), 'attr:click:true knopf mitte aussen', 'Klick steigt auf bis stopPropagation');\n"
    "  var ev = new CustomEvent('einmal', { detail: 5, bubbles: true }); aussen.dispatchEvent(ev); aussen.dispatchEvent(ev); eq(einmal, 1, 'once');\n"
    "  var gehoert = null; document.addEventListener('eigen', function (e) { gehoert = e.detail; }); knopf.dispatchEvent(new CustomEvent('eigen', { detail: { a: 1 }, bubbles: true })); eq(gehoert && gehoert.a, 1, 'eigenes Ereignis mit detail');\n"
    "  var nicht = 0; document.addEventListener('still', function () { nicht++; }); knopf.dispatchEvent(new Event('still')); eq(nicht, 0, 'ohne bubbles steigt nichts auf');\n"
    "  var e2 = new Event('x', { cancelable: true }); knopf.addEventListener('x', function (e) { e.preventDefault(); }); eq(knopf.dispatchEvent(e2), false, 'dispatchEvent meldet preventDefault'); check(e2.defaultPrevented, 'defaultPrevented');\n"
    "  var zwei = []; knopf.addEventListener('y', function (e) { zwei.push(1); e.stopImmediatePropagation(); }); knopf.addEventListener('y', function () { zwei.push(2); }); knopf.dispatchEvent(new Event('y')); eq(zwei.join(), '1', 'stopImmediatePropagation');\n"
    "  knopf.addEventListener('wirft', function () { throw new Error('im Handler'); }); knopf.addEventListener('wirft', function () { zwei.push(3); }); knopf.dispatchEvent(new Event('wirft')); eq(zwei.join(), '1,3', 'ein Fehler hält die anderen nicht auf');\n"
    "  folge = []; fertig();\n"
    "}\n"
    "window.addEventListener('load', pruefe);\n"
    "</script></body></html>";

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    /* 1. Das Abbild aus Sicht der Skripte */
    script_t *s = load(page_dom, 0);
    check(s != NULL, "Seite mit Skripten geladen");
    if (s) {
        int ran, failed, skipped;
        script_counts(s, &ran, &failed, &skipped);
        check(ran == 2 && failed == 0 && reported == 1, "beide Skripte liefen ohne Fehler durch");
        if (failed || reported != 1)
            printf("  letzte Meldung: %s\n", last_log);
        check(script_changed(s), "die Seite gilt als geändert");
        check(page_has(s, "<title data-hx=") && page_has(s, ">Neu</title>"), "neuer Titel steht in der Seite");
        check(page_has(s, "value=\"Max\"") && page_has(s, ">neu</textarea>") && !page_has(s, " checked"),
              "Stand der Formularfelder kommt in der Anzeige an");
        check(!script_changed(s), "nach dem Abholen gilt sie als unverändert");
        script_destroy(s);
    }

    /* 2. Ereignisse */
    reported = 0;
    s = load(page_events, 0);
    check(s && reported == 1, "Seite mit Ereignissen geladen und geprüft");
    if (s) {
        if (reported != 1)
            printf("  letzte Meldung: %s\n", last_log);
        char url[2048];
        uint32_t node = 0;
        while (script_wish(s, url, sizeof(url), &node, NULL) != SCRIPT_NOTHING)
            ;
        check(script_click(s, node_with(s, "id=\"link\"")), "Klick auf einen Link: nichts spricht dagegen");
        check(!script_click(s, node_with(s, "id=\"halt\"")), "onclick liefert false: der Link wird nicht verfolgt");
        check(!script_submit(s, node_with(s, "id=\"f\""), 0), "leeres Formular: Absenden unterbunden");
        script_field(s, node_with(s, "id=\"feld\""), "Blumen", -1, -2, false);
        check(page_has(s, ">getippt: Blumen</p>"), "Eingabe erreicht das Skript, das Skript ändert die Seite");
        check(page_has(s, "value=\"Blumen\""), "die Eingabe bleibt beim Neusetzen erhalten");
        script_field(s, node_with(s, "id=\"feld\""), "Blumen", -1, -2, true);
        check(script_submit(s, node_with(s, "id=\"f\""), 0), "ausgefülltes Formular darf abgesendet werden");
        script_field(s, node_with(s, "id=\"haken\""), NULL, 1, -2, true);
        check(page_has(s, "id=\"haken\" checked"), "Häkchen kommt im Abbild an");
        script_destroy(s);
    }

    /* 3. Laden: Dateien, document.write, nachträglich eingefügte Skripte, Zeitgeber, Wünsche */
    static const char page_load[] =
        "<html><head><title>T</title><script src='/lib.js'></script><script src=zwei.js></script>"
        "<script src=/fehlt.js onerror=\"document.title += ' +fehlt'\"></script><script src=/kaputt.js></script>"
        "<script type='application/ld+json'>{\"kein\": \"Skript\"}</script><script type=module>import x from 'y';</script>"
        "</head><body><p id=a>vor</p><script>document.write('<b id=w>geschrieben</b>' + '<i>' + ausDatei + ausDateiFn(21) + '</i>');</script><p id=b>nach</p>"
        "<noscript><p>OHNE-SKRIPT</p><script>document.title = 'FALSCH';</script></noscript>"
        "<div id=ziel></div><script>\n"
        "var d = document.getElementById('ziel'); d.innerHTML = '<script>document.title = \"FALSCH\"<\\/script><u>innen</u>';\n"
        "var spaet = document.createElement('script'); spaet.textContent = 'document.title += \" +dynamisch\"'; document.body.appendChild(spaet);\n"
        "setTimeout(function () { document.getElementById('a').textContent = 'nach 50 ms'; }, 50);\n"
        "setTimeout(function () { d.setAttribute('data-sofort', 'ja'); }, 0);\n"
        "var z = 0, takt = setInterval(function () { d.setAttribute('data-takt', ++z); if (z == 3) clearInterval(takt); }, 100);\n"
        "window.addEventListener('load', function () { alert('geladen: ' + document.title); });\n"
        "</script></body></html>";
    s = load(page_load, 0);
    check(s != NULL, "Seite mit Skriptdateien geladen");
    if (s) {
        int ran, failed, skipped;
        script_counts(s, &ran, &failed, &skipped);
        check(ran == 7 && failed == 3 && skipped == 0,
              "Dateien geladen; fehlende und kaputte zählen als Fehler - auch das Modul, dessen Import ins Leere geht");
        if (!(ran == 7 && failed == 3 && skipped == 0))
            printf("  gelaufen %d, Fehler %d, übersprungen %d\n", ran, failed, skipped);
        check(page_has(s, ">T +zwei +fehlt +dynamisch</title>"), "Skripte liefen in der Reihenfolge der Seite; onerror; nachträglich eingefügtes Skript");
        check(page_has(s, ">vor</p><b data-hx=") && page_has(s, ">geschrieben</b><i data-hx=") &&
              page_has(s, ">geladen42</i><p data-hx="), "document.write fügt an der Stelle des Skripts ein");
        check(!page_has(s, "OHNE-SKRIPT") && !page_has(s, "FALSCH"), "<noscript> entfällt; Skripte darin und per innerHTML laufen nicht");
        check(page_has(s, "<u data-hx=") && page_has(s, ">innen</u>"), "innerHTML baut die Elemente ein");
        check(strcmp(last_alert, "geladen: T +zwei +fehlt +dynamisch") == 0, "alert kommt beim Browser an");
        check(page_has(s, "data-sofort=\"ja\""), "setTimeout(…, 0) gehört noch zum Laden");
        check(!page_has(s, "nach 50 ms"), "spätere Zeitgeber warten");
        int64_t wait = -1;
        script_tick(s, &wait);
        check(wait > 0 && wait <= 50, "der nächste Zeitgeber ist in höchstens 50 ms fällig");
        run_timers(s, 20);
        check(page_has(s, ">nach 50 ms</p>") && page_has(s, "data-takt=\"3\""), "Zeitgeber laufen ab, setInterval endet nach clearInterval");
        script_tick(s, &wait);
        check(wait < 0, "danach steht kein Zeitgeber mehr an");
        script_destroy(s);
    }

    static const char page_wish[] =
        "<html><body><a id=l href='weiter.html'>x</a><form id=f><input name=a></form><script>"
        "document.getElementById('l').addEventListener('click', function (e) { e.preventDefault(); location.href = '/woanders?x=1'; });"
        "function los() { document.getElementById('f').submit(); }"
        "function frage() { if (confirm('Wirklich?')) location.assign('ja.html'); else history.back(); }"
        "</script><button id=b1 onclick='los()'>a</button><button id=b2 onclick='frage()'>b</button>"
        "<button id=b3 onclick=\"location.reload()\">c</button><button id=b4 onclick=\"document.getElementById('l').click()\">d</button></body></html>";
    s = load(page_wish, 0);
    if (s) {
        char url[2048];
        uint32_t node = 0;
        check(script_wish(s, url, sizeof(url), &node, NULL) == SCRIPT_NOTHING, "anfangs kein Wunsch");
        check(!script_click(s, node_with(s, "id=\"l\"")) &&
              script_wish(s, url, sizeof(url), &node, NULL) == SCRIPT_NAVIGATE && strcmp(url, "/woanders?x=1") == 0,
              "location.href = ... wird zum Wunsch, die Seite zu wechseln");
        script_click(s, node_with(s, "id=\"b1\""));
        check(script_wish(s, url, sizeof(url), &node, NULL) == SCRIPT_SUBMIT && node == node_with(s, "id=\"f\""),
              "form.submit() wird zum Wunsch, das Formular abzusenden");
        script_click(s, node_with(s, "id=\"b2\""));
        check(script_wish(s, url, sizeof(url), &node, NULL) == SCRIPT_NAVIGATE && strcmp(url, "ja.html") == 0 &&
              strcmp(last_alert, "Wirklich?") == 0, "confirm: Ja");
        script_click(s, node_with(s, "id=\"b2\""));
        check(script_wish(s, url, sizeof(url), &node, NULL) == SCRIPT_BACK, "confirm: Nein, dann history.back()");
        script_click(s, node_with(s, "id=\"b3\""));
        check(script_wish(s, url, sizeof(url), &node, NULL) == SCRIPT_RELOAD, "location.reload()");
        script_click(s, node_with(s, "id=\"b4\""));
        check(script_wish(s, url, sizeof(url), &node, NULL) == SCRIPT_NAVIGATE && strcmp(url, "/woanders?x=1") == 0 &&
              script_wish(s, url, sizeof(url), &node, NULL) == SCRIPT_NOTHING, "link.click() aus einem Skript");
        script_destroy(s);
    }

    /* 4. Grenzen: Endlosschleifen und Speicherfresser werden abgebrochen, die Seite bleibt */
    real_clock = true;
    s = load("<html><body><p id=a>vorher</p><script>document.getElementById('a').textContent = 'geändert'; while (true) {}</script>"
             "<script>document.getElementById('a').textContent = 'NIE';</script></body></html>", 200);
    check(s && page_has(s, ">geändert</p>"), "Endlosschleife: abgebrochen, die Seite bleibt, wie sie bis dahin war");
    if (s) {
        check(script_click(s, 3) && !script_tick(s, NULL), "danach läuft für diese Seite nichts mehr - ohne Schaden");
        script_destroy(s);
    }
    s = load("<html><body><script>var a = []; for (;;) a.push(new Array(1000).join('x'));</script><p>Text</p></body></html>", 3000);
    check(s && page_has(s, ">Text</p>"), "Speicherfresser: abgebrochen");
    script_destroy(s);
    s = load("<html><body><script>function f() { f(); } try { f(); } catch (e) { document.title = e.name; }</script></body></html>", 2000);
    check(s && page_has(s, ">RangeError</title>"), "endlose Rekursion: als Fehler auffangbar");
    script_destroy(s);
    s = load("<html><body><div id=d></div><script>var d = document.getElementById('d'), t = '<b>x</b>';"
             "try { for (var i = 0; i < 40; i++) { t += t; } d.innerHTML = t; } catch (e) {}</script><p>Ende</p></body></html>", 3000);
    check(s && page_has(s, ">Ende</p>"), "riesiges innerHTML: begrenzt");
    script_destroy(s);
    real_clock = false;

    /* 4b. Bedienung: Tasten, Ankreuzfelder, Aufklappen; Gestaltung; Daten nachladen */
    static const char page_use[] =
        "<!DOCTYPE html><html><head><title>B</title>" PRE
        "<style>.weg { display: none } .unsichtbar { visibility: hidden } #block span { display: block }</style></head><body>"
        "<p id=p1 class=weg>a</p><p id=p2 style='display:none'>b</p><div id=aussen class=weg><b id=innen>c</b></div>"
        "<p id=p3>d <span id=s1>e</span></p><div id=block><span id=s2>f</span></div><p id=p4 class=unsichtbar>g</p><p id=p5 hidden>h</p>"
        "<form id=f action=/senden><input id=t name=t value=alt><input type=checkbox id=c name=c><label id=lab for=c>Haken</label>"
        "<input type=radio name=r id=r1 value=1 checked><input type=radio name=r id=r2 value=2>"
        "<select id=sel name=sel><option value=a>A<option value=b>B</select><input type=reset id=rs><button id=go>Los</button></form>"
        "<details id=det><summary id=sum>Mehr</summary><p>Inhalt</p></details><dialog id=dlg>Dialog</dialog>"
        "<p id=log></p>"
        "<script>\n"
        "function g(id) { return document.getElementById(id); }\n"
        "var cs = getComputedStyle;\n"
        "eq(cs(g('p1')).display, 'none', 'Stylesheet: display none'); eq(cs(g('p2')).display, 'none', 'style: display none');\n"
        "eq(cs(g('p3')).display, 'block', 'Absatz: block'); eq(cs(g('s1')).display, 'inline', 'span: inline');\n"
        "eq(cs(g('s2')).display, 'block', 'span laut Stylesheet block'); eq(cs(g('p4')).visibility, 'hidden', 'visibility');\n"
        "eq(cs(g('p3')).visibility, 'visible', 'sichtbar'); eq(cs(g('p5')).display, 'none', 'hidden-Attribut');\n"
        "eq(cs(g('p3')).getPropertyValue('display'), 'block', 'getPropertyValue'); eq(cs(g('f')).display, 'block', 'Formular: block');\n"
        "eq(cs(g('t')).display, 'inline-block', 'Eingabefeld: inline-block'); eq(cs(document.querySelector('title')).display, 'none', 'Kopf: none');\n"
        "eq(g('p1').offsetWidth, 0, 'verborgen: keine Breite'); check(g('p3').offsetWidth > 0, 'sichtbar: Breite');\n"
        "eq(g('innen').offsetHeight, 0, 'in Verborgenem: keine Höhe'); eq(g('innen').getClientRects().length, 0, 'keine Kästen');\n"
        "eq(g('s1').getClientRects().length, 1, 'ein Kasten'); eq(g('p3').offsetParent, document.body, 'offsetParent');\n"
        "eq(g('p1').offsetParent, null, 'verborgen: kein offsetParent'); eq(g('p4').offsetWidth, 0, 'unsichtbar: keine Breite');\n"
        "var los = document.createElement('p'); eq(los.offsetWidth, 0, 'nicht in der Seite: keine Breite');\n"
        "g('p1').style.display = 'block'; eq(cs(g('p1')).display, 'block', 'style schlägt Stylesheet'); check(g('p1').offsetWidth > 0, 'jetzt sichtbar');\n"
        "g('aussen').className = ''; check(g('innen').offsetWidth > 0, 'Klasse entfernt: Inhalt sichtbar');\n"
        "cs(g('p3')).display = 'none'; eq(cs(g('p3')).display, 'block', 'berechnete Stile sind nur zum Lesen');\n"
        "eq(g('t').type, 'text', 'type ohne Attribut'); eq(g('go').type, 'submit', 'button: submit'); eq(g('sel').type, 'select-one', 'select-one');\n"
        "eq(g('p3').elements, undefined, 'elements nur an Formularen'); eq(g('f').elements.length, 7, 'Formularfelder');\n"
        "eq(g('p3').options, undefined, 'options nur an Auswahllisten'); eq(g('sel').options.length, 2, 'options');\n"
        "var fd = new FormData(g('f')); eq(fd.get('t') + '|' + fd.get('r') + '|' + fd.has('c') + '|' + fd.get('sel'), 'alt|1|false|a', 'FormData');\n"
        "eq(typeof Uint8Array + typeof ArrayBuffer + typeof DataView + typeof DOMException, 'functionfunctionfunctionfunction', 'typisierte Felder und DOMException');\n"
        "var u = new Uint8Array([1, 2, 300]); eq(u[2], 44, 'Uint8Array in der Seite');\n"
        "var doc2 = document.implementation.createHTMLDocument('x'); doc2.body.innerHTML = '<form></form><form></form>'; eq(doc2.body.childNodes.length, 2, 'createHTMLDocument');\n"
        "g('dlg').showModal(); eq(g('dlg').open, true, 'Dialog geöffnet'); g('dlg').close(); eq(g('dlg').hasAttribute('open'), false, 'Dialog geschlossen');\n"
        "var seen = [];\n"
        "['keydown', 'keypress', 'keyup', 'input', 'change', 'focus', 'blur'].forEach(function (n) { g('t').addEventListener(n, function (e) { seen.push(n + (e.key ? ':' + e.key + ':' + e.keyCode : '')); if (n === 'keydown' && e.key === 'x') e.preventDefault(); }); });\n"
        "g('c').addEventListener('click', function (e) { seen.push('click:' + this.checked); if (window.sperre) e.preventDefault(); });\n"
        "g('c').addEventListener('change', function () { seen.push('change:' + this.checked); });\n"
        "g('det').addEventListener('toggle', function () { seen.push('toggle:' + this.open); });\n"
        "function zeig(mehr) { g('log').textContent = seen.join(' ') + (mehr === undefined ? '' : ' ' + mehr); seen = []; }\n"
        "function probe(n) { return n * 2; }\n"
        "fertig();\n"
        "</script></body></html>";
    css_sheet_t *use_css = NULL;
    reported = 0;
    s = load_styled(page_use, &use_css, 0);
    check(s && reported == 1, "Seite zur Bedienung geladen, Gestaltung und Formulare geprüft");
    if (s) {
        if (reported != 1)
            printf("  letzte Meldung: %s\n", last_log);
        uint32_t t = node_with(s, "id=\"t\""), c = node_with(s, "id=\"c\""), lab = node_with(s, "id=\"lab\"");
        uint32_t r2 = node_with(s, "id=\"r2\""), rs = node_with(s, "id=\"rs\""), sum = node_with(s, "id=\"sum\"");
        check(!script_stopped(s), "nichts wurde angehalten");
        script_event(s, t, "focus", NULL);
        check(script_event(s, t, "keydown", "a") && script_event(s, t, "keypress", "a"), "Taste a: erlaubt");
        script_field(s, t, "alta", -1, -2, false);
        script_event(s, t, "keyup", "a");
        check(!script_event(s, t, "keydown", "x"), "Taste x: die Seite lehnt ab (preventDefault)");
        check(script_event(s, t, "keydown", "Enter"), "Enter: erlaubt");
        script_field(s, t, NULL, -1, -2, true);
        script_event(s, t, "blur", NULL);
        script_eval(s, "zeig()");
        check(page_has(s, ">focus keydown:a:65 keypress:a:97 input keyup:a:65 keydown:x:88 keydown:Enter:13 change blur</p>"),
              "Tasten, Eingabe, Fokus: Reihenfolge und Tastencodes");

        check(!script_click(s, c), "Klick aufs Ankreuzfeld: Die Seite schaltet es selbst um");
        script_eval(s, "zeig(g('c').checked)");
        check(page_has(s, ">click:true change:true true</p>"), "erst umgeschaltet, dann click, dann change");
        script_eval(s, "window.sperre = true");
        script_click(s, c);
        script_eval(s, "zeig(g('c').checked)");
        check(page_has(s, ">click:false true</p>"), "abgelehnter Klick: Der alte Stand gilt wieder, kein change");
        script_eval(s, "window.sperre = false");
        check(!script_click(s, lab), "Klick auf die Beschriftung gilt dem Ankreuzfeld");
        script_eval(s, "zeig(g('c').checked)");
        check(page_has(s, ">click:false change:false false</p>"), "Beschriftung: Feld umgeschaltet, samt Ereignissen");
        script_click(s, r2);
        script_eval(s, "zeig(g('r1').checked + '/' + g('r2').checked)");
        check(page_has(s, "> false/true</p>"), "Auswahlfeld: Das andere der Gruppe geht aus");
        script_field(s, node_with(s, "id=\"sel\""), NULL, -1, 1, true);
        script_eval(s, "zeig(g('sel').value)");
        check(page_has(s, "> b</p>"), "Auswahlliste: Wahl kommt an");
        check(!script_click(s, rs), "Zurücksetzen erledigt die Seite");
        script_eval(s, "zeig(g('t').value + '/' + g('r1').checked + '/' + g('sel').value)");
        check(page_has(s, "> alt/true/a</p>"), "Formular zurückgesetzt");
        check(!script_click(s, sum), "Klick auf die Überschrift eines details: klappt auf");
        script_eval(s, "zeig(g('det').hasAttribute('open'))");
        check(page_has(s, ">toggle:true true</p>"), "details: geöffnet, toggle gemeldet");
        script_click(s, sum);
        script_eval(s, "zeig(g('det').open)");
        check(page_has(s, ">toggle:false false</p>"), "details: wieder zu");
        script_eval(s, "g('log').textContent = probe(21)");
        check(page_has(s, ">42</p>"), "script_eval führt Skripttext der Seite aus");

        /* Daten nachladen */
        request_count = 0;
        request_log[0] = '\0';
        script_eval(s, "var x = new XMLHttpRequest(); x.open('POST', '/daten?q=1'); x.setRequestHeader('X-Eigen', 'ja');"
                       "x.setRequestHeader('Cookie', 'boese=1'); x.setRequestHeader('X-Zwei', 'a\\r\\nHost: boese');"
                       "x.onreadystatechange = function () { seen.push(x.readyState); };"
                       "x.onload = function () { zeig(x.status + ' ' + x.responseText + ' ' + x.getResponseHeader('Content-Type')); };"
                       "x.send('a=1&b=2'); seen.push('gesendet');");
        check(page_has(s, ">42</p>"), "XMLHttpRequest: Die Antwort kommt erst nach dem laufenden Skript");
        run_timers(s, 3);
        check(page_has(s, ">gesendet 2 3 4 200 {\"a\":1} application/json</p>"), "XMLHttpRequest: Zustände, Status, Text, Art");
        check(request_count == 1 && strstr(request_seen, "POST https://a.de/daten?q=1 <") &&
              strstr(request_seen, "X-Eigen: ja\r\n") && strstr(request_seen, "[a=1&b=2]") &&
              strstr(request_seen, "Content-Type: text/plain"), "Anfrage: Adresse aufgelöst, eigene Kopfzeile, Inhalt");
        check(!strstr(request_seen, "boese"), "verbotene und zerbrochene Kopfzeilen kommen nicht durch");
        request_count = 0;
        request_log[0] = '\0';
        script_eval(s, "fetch('https://b.de/geheim').then(function () { zeig('ERLAUBT'); }, function (e) { zeig('abgelehnt ' + e.name); });"
                       "var y = new XMLHttpRequest(); y.open('GET', '//a.de.b.de/x'); y.onerror = function () { seen.push('auch'); }; y.send();");
        run_timers(s, 3);
        check(request_count == 2 && page_has(s, "> abgelehnt TypeError</p>") && !strstr(request_log, "+cookies"),
              "fremder Server ohne Erlaubnis: Das Skript sieht die Antwort nicht, und Cookies gingen nicht mit");
        script_eval(s, "fetch('/daten', { headers: { 'X-Drei': '3' } }).then(function (r) { return r.ok && r.headers.get('content-type') === 'application/json' ? r.json() : null; })"
                       ".then(function (d) { zeig('json ' + d.a); });");
        run_timers(s, 3);
        check(page_has(s, ">auch json 1</p>") && strstr(request_seen, "GET https://a.de/daten <") && strstr(request_seen, "x-drei: 3"),
              "fetch: Antwort als JSON (und der fremde Server meldete auch dem XMLHttpRequest einen Fehler)");
        script_eval(s, "fetch('/fehlt').then(null, function () { return fetch('/verboten'); }).then(function (r) { return r.text().then(function (t) { zeig(r.status + ' ' + r.ok + ' ' + t); }); });");
        run_timers(s, 3);
        check(page_has(s, "> 403 false Text vom Server</p>"), "fetch: Netzfehler und Fehlerstatus");
        script_eval(s, "var z = new XMLHttpRequest(); z.open('GET', 'text', false); z.send(); zeig(z.readyState + ' ' + z.responseText);");
        check(page_has(s, "> 4 Text vom Server</p>") && strstr(request_seen, "GET https://a.de/pfad/text <"), "XMLHttpRequest ohne Warten (synchron)");
        script_eval(s, "var fd = new FormData(g('f')); fd.append('neu', 'ä ö'); var w = new XMLHttpRequest(); w.open('POST', '/senden'); w.send(fd);");
        check(strstr(request_seen, "[t=alt&r=1&sel=a&neu=%C3%A4%20%C3%B6]") && strstr(request_seen, "application/x-www-form-urlencoded"),
              "FormData wird wie ein Formular gesendet");
        script_destroy(s);
        css_free(use_css);
    }
    /* Die neueren Sprachmittel auf einer Seite: async/await mit fetch, Generatoren, Klassen, Proxy */
    static const char page_modern[] =
        "<html><head><title>M</title></head><body><ul id=liste></ul><p id=stand>-</p><button id=knopf>k</button>"
        "<script>\n"
        "const liste = document.getElementById('liste');\n"
        "function zeile(t) { const li = document.createElement('li'); li.textContent = t; liste.appendChild(li); }\n"
        "function* zahlen(n) { for (let i = 1; i <= n; i++) yield i; }\n"
        "zeile('gen ' + [...zahlen(4)].join(''));\n"
        "class Lager { #dinge = new Map(); static #anzahl = 0; static { Lager.name2 = 'L'; }\n"
        "  lege(k, v) { this.#dinge.set(k, v); Lager.#anzahl++; return this; }\n"
        "  get groesse() { return this.#dinge.size; } static get anzahl() { return Lager.#anzahl; }\n"
        "  *[Symbol.iterator]() { yield* this.#dinge.keys(); } }\n"
        "const lager = new Lager().lege('a', 1).lege('b', 2);\n"
        "zeile('klasse ' + lager.groesse + Lager.anzahl + [...lager].join('') + Lager.name2);\n"
        "const zustand = new Proxy({ n: 0 }, { set(t, k, v) { t[k] = v; document.getElementById('stand').textContent = k + '=' + v; return true; } });\n"
        "document.getElementById('knopf').addEventListener('click', async () => { zustand.n++; await null; zeile('klick ' + zustand.n); });\n"
        "async function lade() {\n"
        "  const antwort = await fetch('/daten.json');\n"
        "  const daten = await antwort.json();\n"
        "  zeile('fetch ' + antwort.status + ' a=' + daten.a);\n"
        "  try { await fetch('/fehlt'); zeile('FALSCH'); } catch (e) { zeile('fehler ' + e.constructor.name); }\n"
        "  const teile = [];\n"
        "  for await (const t of (async function* () { yield 'x'; yield await Promise.resolve('y'); })()) teile.push(t);\n"
        "  return teile.join('');\n"
        "}\n"
        "lade().then(t => { zeile('fertig ' + t); document.title = 'geladen'; });\n"
        "zeile('sofort');\n"
        "</script></body></html>";
    reported = 0;
    errors_seen = 0;
    s = load(page_modern, 0);
    check(s != NULL, "Seite mit neueren Sprachmitteln geladen");
    if (s) {
        int ran, failed, skipped;
        script_counts(s, &ran, &failed, &skipped);
        check(ran == 1 && failed == 0 && skipped == 0, "das Skript lief ohne Fehler durch");
        if (failed)
            printf("  letzte Meldung: %s\n", last_log);
        run_timers(s, 50);
        check(page_has(s, ">gen 1234</li>"), "Generator auf einer Seite");
        check(page_has(s, ">klasse 22abL</li>"), "Klasse mit privaten Feldern, static-Block und eigenem Iterator");
        check(page_has(s, ">fetch 200 a=1</li>"), "await fetch(): die Antwort kommt an");
        check(page_has(s, ">fehler TypeError</li>") && !page_has(s, "FALSCH"), "await auf eine gescheiterte Anfrage wirft");
        check(page_has(s, ">fertig xy</li>") && page_has(s, ">geladen</title>"), "for await und das Ergebnis der async-Funktion");
        script_click(s, node_with(s, "id=\"knopf\""));
        run_timers(s, 10);
        check(page_has(s, ">n=1</p>") && page_has(s, ">klick 1</li>"), "Klick: Proxy meldet die Änderung, der async-Rückruf läuft zu Ende");
        script_click(s, node_with(s, "id=\"knopf\""));
        run_timers(s, 10);
        check(page_has(s, ">n=2</p>") && page_has(s, ">klick 2</li>"), "... und beim zweiten Klick wieder");
        script_destroy(s);
    }

    /* Skripte, die eine Seite erst später nachfordert, und Stylesheets, die Skripte einfügen */
    static const char page_late[] =
        "<html><head><title>S</title><style id=st>p { color: red }</style></head><body><p id=aus>-</p>"
        "<button id=mehr>mehr</button><button id=farbe>farbe</button><script>\n"
        "function hole(src, dann) { var s = document.createElement('script'); s.src = src;\n"
        "  s.onload = function () { dann('ok'); }; s.onerror = function () { dann('fehler'); }; document.head.appendChild(s); }\n"
        "function antwort(d) { document.getElementById('aus').textContent += ' jsonp:' + d.wert; }\n"
        "document.getElementById('mehr').onclick = function () {\n"
        "  hole('/spaet.js', function (wie) { document.getElementById('aus').textContent += ' ' + wie; });\n"
        "  hole('https://cdn.de/jsonp?cb=antwort', function () {});\n"
        "  hole('/gibtsnicht.js', function (wie) { document.getElementById('aus').textContent += ' ' + wie; });\n"
        "};\n"
        "document.getElementById('farbe').onclick = function () {\n"
        "  var sheet = document.getElementById('st').sheet; sheet.insertRule('.neu { display: none }', sheet.cssRules.length);\n"
        "  var st = document.createElement('style'); st.textContent = '#aus { font-weight: bold }'; document.head.appendChild(st);\n"
        "  document.title = document.styleSheets.length + ' Blaetter, ' + sheet.cssRules.length + ' Regel';\n"
        "};\n"
        "setTimeout(function () { hole('/spaet.js', function () {}); }, 30);\n"
        "</script></body></html>";
    s = load(page_late, 0);
    check(s != NULL, "Seite, die Skripte nachfordert, geladen");
    if (s) {
        int ran, failed, skipped;
        run_timers(s, 10);
        check(page_has(s, ">- spaet:1</p>"), "ein Zeitgeber fordert eine Skriptdatei nach: sie wird geholt und läuft");
        script_click(s, node_with(s, "id=\"mehr\""));
        check(page_has(s, " spaet:2 ok jsonp:42 fehler</p>"),
              "Klick: nachgeforderte Skripte laufen in ihrer Reihenfolge, onload und onerror melden sich");
        script_counts(s, &ran, &failed, &skipped);
        check(ran == 4 && failed == 1 && skipped == 0, "gezählt: vier gelaufen, eine Datei fehlte");
        script_click(s, node_with(s, "id=\"farbe\""));
        check(page_has(s, ">2 Blaetter, 1 Regel</title>"), "Stylesheets als Objekte: styleSheets, cssRules");
        check(page_has(s, ".neu { display: none }</style>") && page_has(s, "#aus { font-weight: bold }</style>"),
              "insertRule() und ein eingefügtes <style> stehen in der Seite, wo der Browser sie liest");
        script_destroy(s);
    }

    /* Module: import/export über mehrere Dateien, im Kreis, mit Übersetzungstabelle und import() */
    static const char page_modules[] =
        "<html><head><title>T</title>"
        "<script type=importmap>{ \"imports\": { \"werkzeug\": \"/lib/werkzeug.js\", \"paket/\": \"https://cdn.de/paket/\" } }</script>"
        "<script type=module src=/mod/haupt.js></script>"
        "<script nomodule src=/alt.js></script>"
        "<script defer src=/alt.js></script>"
        "<script>document.title += ' +zuerst';</script>"
        "</head><body><p id=m>-</p><p id=d>-</p><p id=e>-</p>"
        "<script type=module>\n"
        "import wert, { doppelt } from '/mod/spaeter.js';\n"
        "import { hilf, zahl } from 'werkzeug';\n"
        "document.title += ' +modul:' + wert + ':' + doppelt + ':' + hilf() + ':' + zahl + ':' + window.geladen;\n"
        "const m = await import('/mod/teile.js');\n"
        "document.getElementById('d').textContent = 'dyn ' + m.default('du') + ' ' + m.zahl;\n"
        "import('/mod/falsch.js').catch(e => { document.getElementById('e').textContent = 'e1 ' + e.name; return import('/mod/wirft.js'); })\n"
        "  .catch(e => { document.getElementById('e').textContent += ' e2 ' + e.message; return import('nirgends'); })\n"
        "  .catch(e => { document.getElementById('e').textContent += ' e3 ' + e.name; });\n"
        "</script>"
        "<script>document.title += ' +klassisch'; import('/mod/teile.js').then(m => { document.title += ' +dyn' + m.zahl; });</script>"
        "</body></html>";
    reported = 0;
    s = load(page_modules, 0);
    check(s != NULL, "Seite mit Modulen geladen");
    if (s) {
        int ran, failed, skipped;
        run_timers(s, 20);
        script_counts(s, &ran, &failed, &skipped);
        check(failed == 0 && skipped == 0 && ran == 5, "alle Skripte liefen (das für Browser ohne Module nicht)");
        if (failed || ran != 5)
            printf("  gelaufen %d, Fehler %d, ausgelassen %d; letzte Meldung: %s\n", ran, failed, skipped, last_log);
        check(page_has(s, ">Hallo Welt | 1 | 3 | Hund macht Krach | Tier+default+erhoehe+zahl | true | ABa | "
                          "geholfen | aus dem Paket | string | haupt.js | true</p>"),
              "import: Standard-Export, benannte, alles zusammen, lebendige Verbindung, Kreis, Tabelle");
        /* (das import() im gewöhnlichen Skript ist sofort fertig: +dyn1 kommt vor den aufgeschobenen) */
        check(page_has(s, ">T +zuerst +klassisch +dyn1 +alt +modul:gewartet:gewartetgewartet:geholfen:3:1</title>"),
              "Reihenfolge: gewöhnliche Skripte, dann aufgeschobene und Module; jedes Modul läuft nur einmal");
        if (!page_has(s, ":3:1</title>")) {
            size_t n = 0;
            char *html = script_html(s, &n);
            char *t = html ? strstr(html, "<title") : NULL;
            printf("  Titel: %.200s\n", t ? t : "?");
            free(html);
        }
        check(page_has(s, ">dyn Hallo du 3</p>"), "await und import() ganz außen im Modul");
        check(page_has(s, ">e1 SyntaxError e2 mit Absicht e3 TypeError</p>"),
              "import(): fehlender Export, Fehler im Modul, unbekannte Angabe - jeweils als gescheitertes Promise");
        script_destroy(s);
    }

    /* Daten von fremden Servern: nur mit deren Erlaubnis, Cookies nur auf Wunsch, Voranfrage */
    static const char page_cors[] =
        "<html><head><title>C</title></head><body><p id=aus>-</p><script>\n"
        "function zeig(t) { document.getElementById('aus').textContent = t; }\n"
        "async function probe(name, url, init) {\n"
        "  try { const r = await fetch(url, init); const t = await r.text();\n"
        "    return name + ':' + r.status + ':' + r.type + ':' + t.slice(0, 9) + ':' + [...r.headers.keys()].join(',') + (r.redirected ? ':umgeleitet' : ''); }\n"
        "  catch (e) { return name + ':' + e.name; }\n"
        "}\n"
        "async function alle(liste) { const out = []; for (const p of liste) out.push(await probe(...p)); zeig(out.join(' | ')); }\n"
        "</script></body></html>";
    s = load(page_cors, 0);
    check(s != NULL, "Seite für Anfragen an fremde Server geladen");
    if (s) {
        request_log[0] = '\0';
        script_eval(s, "alle([['offen', 'https://fremd.de/offen'], ['zu', 'https://fremd.de/zu'], ['anderer', 'https://fremd.de/anderer'],"
                       " ['zeig', 'https://fremd.de/zeig'], ['eigen', '/kopf']])");
        run_timers(s, 5);
        check(page_has(s, ">offen:200:cors:Text vom :content-type | zu:TypeError | anderer:TypeError | "
                          "zeig:200:cors:Text vom :content-type,x-zeig | eigen:200:basic:Text vom :content-type,x-eigen-antwort</p>"),
              "fremder Server: Antwort nur mit Erlaubnis sichtbar; Kopfzeilen nur die üblichen und die freigegebenen; Cookies nie");
        check(!strstr(request_log, "fremd.de/offen+cookies") && strstr(request_log, "GET https://a.de/kopf+cookies;") &&
              strstr(request_seen, "GET https://a.de/kopf <") && !strstr(request_seen, "Origin:"),
              "Cookies gehen nur an den Server der Seite; dorthin ohne Origin-Zeile");
        request_log[0] = '\0';
        script_eval(s, "alle([['mit', 'https://fremd.de/mitcookies', { credentials: 'include' }], ['stern', 'https://fremd.de/stern', { credentials: 'include' }],"
                       " ['ohne', 'https://fremd.de/mitcookies', { credentials: 'omit' }], ['nur-eigen', 'https://fremd.de/offen', { mode: 'same-origin' }]])");
        run_timers(s, 5);
        check(page_has(s, ">mit:200:cors:Text vom :content-type | stern:TypeError | ohne:200:cors:Text vom :content-type | nur-eigen:TypeError</p>"),
              "Cookies zum fremden Server nur auf Wunsch und mit dessen ausdrücklicher Erlaubnis (\"*\" genügt dann nicht)");
        check(strstr(request_log, "GET https://fremd.de/mitcookies+cookies;") && strstr(request_log, "GET https://fremd.de/mitcookies;") &&
              !strstr(request_log, "fremd.de/offen"), "mode same-origin: Es geht nichts hinaus");
        request_log[0] = '\0';
        script_eval(s, "alle([['put', 'https://fremd.de/api', { method: 'PUT', headers: { 'X-Eigen': '1', 'Content-Type': 'application/json' }, body: '{}' }],"
                       " ['streng', 'https://fremd.de/streng', { method: 'DELETE' }], ['kopf', 'https://fremd.de/streng', { headers: { 'X-Fremd': '1' } }],"
                       " ['einfach', 'https://fremd.de/api', { method: 'POST', body: 'a=1' }]])");
        run_timers(s, 5);
        check(page_has(s, ">put:200:cors:Text vom :content-type | streng:TypeError | kopf:TypeError | einfach:200:cors:Text vom :content-type</p>"),
              "Ungewöhnliches (PUT, eigene Kopfzeilen) erst nach Rückfrage beim fremden Server");
        check(strstr(request_log, "OPTIONS https://fremd.de/api;PUT https://fremd.de/api;OPTIONS https://fremd.de/streng;"
                                  "OPTIONS https://fremd.de/streng;POST https://fremd.de/api;") &&
              !strstr(request_log, "DELETE"),
              "Voranfrage: OPTIONS zuerst; ohne Erlaubnis geht die eigentliche Anfrage nicht hinaus");
        request_count = 0;
        script_eval(s, "fetch('https://fremd.de/api', { method: 'PUT', headers: { 'X-Eigen': '1' }, body: 'x' })");
        run_timers(s, 3);
        check(strstr(request_seen, "PUT https://fremd.de/api <") && strstr(request_seen, "Origin: https://a.de\r\n") &&
              strstr(request_seen, "[x]"), "die eigentliche Anfrage nennt ihre Herkunft (Origin)");
        request_log[0] = '\0';
        script_eval(s, "alle([['weiter', '/weiter'], ['haus', '/im-haus', { method: 'POST', body: 'x' }], ['dunkel', 'https://fremd.de/zu', { mode: 'no-cors' }],"
                       " ['router', 'https://192.168.1.1/'], ['klartext', 'http://fremd.de/offen'], ['datei', 'file:///etc/passwd']])");
        run_timers(s, 5);
        check(page_has(s, ">weiter:200:cors:Text vom :content-type:umgeleitet | haus:200:basic:{\"a\":1}:content-type:umgeleitet | "
                          "dunkel:0:opaque:: | router:TypeError | klartext:TypeError | datei:TypeError</p>"),
              "Weiterleitungen werden verfolgt und neu geprüft; kein Blick ins eigene Netz, nichts Unverschlüsseltes, keine Dateien");
        check(strstr(request_log, "GET https://a.de/weiter+cookies;GET https://fremd.de/offen;POST https://a.de/im-haus+cookies;"
                                  "GET https://a.de/daten+cookies;GET https://fremd.de/zu;") &&
              !strstr(request_log, "192.168") && !strstr(request_log, "http://fremd.de") && !strstr(request_log, "file:"),
              "nach der Weiterleitung zum fremden Server ohne Cookies; 303 macht aus POST ein GET");
        script_eval(s, "var x = new XMLHttpRequest(); x.open('DELETE', '/kopf'); x.onload = function () {"
                       " zeig(x.status + ' ' + x.getResponseHeader('X-Eigen-Antwort') + ' ' + x.getResponseHeader('Set-Cookie') + ' ' + /x-eigen-antwort: sichtbar/.test(x.getAllResponseHeaders())); }; x.send();");
        run_timers(s, 3);
        check(page_has(s, ">200 sichtbar null true</p>") && strstr(request_seen, "DELETE https://a.de/kopf <"),
              "XMLHttpRequest: weitere Verfahren (DELETE) und die Kopfzeilen der Antwort");
        script_eval(s, "(async function () { const r = await fetch('https://fremd.de/offen');"
                       " const b = new Uint8Array(await r.arrayBuffer()); const c = new AbortController(); c.abort();"
                       " let ab = '-'; try { await fetch('/kopf', { signal: c.signal }); } catch (e) { ab = e.name; }"
                       " zeig(b.length + ' ' + b[0] + ' ' + new TextDecoder().decode(b).slice(0, 4) + ' ' + ab); })()");
        run_timers(s, 3);
        check(page_has(s, ">15 84 Text AbortError</p>"), "fetch: Antwort als Bytes; ein schon abgebrochenes Signal bricht ab");
        /* über einen fremden Server zu einem dritten: Die Herkunft heißt dann "null" */
        request_log[0] = '\0';
        script_eval(s, "alle([['umweg', '/umweg', { method: 'POST', headers: { 'Content-Type': 'text/plain' }, body: 'x' }]])");
        run_timers(s, 3);
        check(page_has(s, ">umweg:200:cors:Text vom :content-type:umgeleitet</p>") &&
              strstr(request_log, "POST https://a.de/umweg+cookies;POST https://fremd.de/hin;GET https://dritter.de/ziel;") &&
              strstr(request_seen, "Origin: null\r\n") && !strstr(request_seen, "Content-Type") && strstr(request_seen, "[-]"),
              "Umweg über einen fremden Server: Herkunft \"null\"; nach 302 ohne Inhalt und ohne dessen Kopfzeilen");
        /* eine Seite aus dem Internet: nichts im eigenen Netz, auch nicht unter harmlosem Namen */
        request_log[0] = '\0';
        public_only_missing = 0;
        script_eval(s, "alle([['selbst', 'https://localhost./x'], ['drucker', 'https://drucker.local/'], ['zehn', 'https://10.1.2.3:8080/'],"
                       " ['v6', 'https://[::1]/'], ['offen', 'https://fremd.de/offen']])");
        run_timers(s, 3);
        check(page_has(s, ">selbst:TypeError | drucker:TypeError | zehn:TypeError | v6:TypeError | offen:200:cors:Text vom :content-type</p>") &&
              strcmp(request_log, "GET https://fremd.de/offen;") == 0 && public_only_missing == 0,
              "Seite aus dem Internet: keine Anfrage ins eigene Netz; alle anderen nur an öffentliche Adressen");
        script_destroy(s);
    }
    /* eine Seite aus dem eigenen Netz darf dort auch nachladen */
    page_is_private = true;
    s = load(page_cors, 0);
    page_is_private = false;
    check(s != NULL, "Seite aus dem eigenen Netz geladen");
    if (s) {
        request_log[0] = '\0';
        public_only_seen = 0;
        script_eval(s, "alle([['zehn', 'https://10.1.2.3:8080/offen-nicht'], ['eigen', '/kopf']])");
        run_timers(s, 3);
        check(page_has(s, ">zehn:TypeError | eigen:200:basic:Text vom :content-type,x-eigen-antwort</p>") &&
              strstr(request_log, "GET https://10.1.2.3:8080/offen-nicht;") && public_only_seen == 0,
              "Seite aus dem eigenen Netz: Anfragen dorthin gehen hinaus (und brauchen wie überall die Erlaubnis des Servers)");
        script_destroy(s);
    }

    /* Das Fenster wird geschlossen: Laufende Skripte enden sofort */
    real_clock = true;
    uint64_t began = time_ms();
    cancel_at = began + 300;
    s = load("<html><body><p id=a>x</p><script>while (true) {}</script></body></html>", 60000);
    check(s && script_stopped(s) && time_ms() - began < 3000, "Abbruch von außen beendet eine Endlosschleife sofort");
    script_destroy(s);
    cancel_at = 0;
    real_clock = false;

    /* 5. Beschädigte Seiten mit Skripten: nichts darf abstürzen */
    quiet = true;
    int before = failures, counted = checks;
    unsigned seed = 99;
    size_t total = sizeof(page_dom) + sizeof(page_events) + sizeof(page_load) + sizeof(page_use) +
                   sizeof(page_modern) + sizeof(page_late) + sizeof(page_modules);
    char *broken = malloc(total * 2);
    int rounds = getenv("RUNDEN") ? atoi(getenv("RUNDEN")) : 400;
    real_clock = true;
    for (int r = 0; r < rounds; r++) {
        const char *source = r % 7 == 0 ? page_dom : r % 7 == 1 ? page_events
                           : r % 7 == 2 ? page_load : r % 7 == 3 ? page_use
                           : r % 7 == 4 ? page_modern : r % 7 == 5 ? page_late : page_modules;
        size_t n = strlen(source);
        memcpy(broken, source, n);
        for (int k = 0; k < 1 + r % 9; k++) {
            seed = seed * 1103515245u + 12345u;
            size_t at = (seed >> 8) % n;
            seed = seed * 1103515245u + 12345u;
            switch ((seed >> 12) % 4) {
            case 0: broken[at] = "<>/\"'=(){};,.&+-! \n$_x0"[(seed >> 16) % 24]; break;
            case 1: n = at > 40 ? at : n; break;
            case 2: memmove(broken + at, broken + at + 1, n - at - 1); n--; break;
            default: if (at + 1 < n) { char t = broken[at]; broken[at] = broken[at + 1]; broken[at + 1] = t; } break;
            }
        }
        broken[n] = '\0';
        css_sheet_t *css = NULL;
        script_t *b = r % 2 ? load_styled(broken, &css, 150) : load(broken, 150);
        if (!b)
            continue;
        for (uint32_t id = 1; id < 60; id += 7) {
            script_click(b, id);
            script_field(b, id + 1, "x", 1, 0, true);
            script_submit(b, id + 2, id);
            script_event(b, id + 3, "keydown", "Enter");
            script_event(b, id + 4, "focus", NULL);
        }
        script_eval(b, "zeig(g('c').checked); var q = new XMLHttpRequest(); q.open('GET', '/daten'); q.send();");
        run_timers(b, 5);
        size_t len;
        char *html = script_html(b, &len);
        free(html);
        script_destroy(b);
        css_free(css);
    }
    free(broken);
    failures = before;                  /* in diesem Teil zählt nur, dass nichts abstürzt */
    checks = counted;
    quiet = false;
    check(1, "beschädigte Seiten mit Skripten verarbeitet");

    printf("%d Prüfungen an den Skripten von Webseiten, %d Fehler\n", checks, failures);
    return failures != 0;
}
