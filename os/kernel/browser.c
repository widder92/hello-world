/*
 * Ein einfacher Browser: holt Seiten über http(s), zeigt sie als Text mit
 * Links und Formularen (siehe html.h), merkt sich Cookies für die Dauer des
 * Fensters und kann Formulare absenden - genug zum Lesen, Suchen und Anmelden.
 *
 * Was fehlt, ist alles, was moderne Seiten "lebendig" macht: JavaScript,
 * Stylesheets, Bilder. Seiten, die darauf angewiesen sind, bleiben leer oder
 * sagen das selbst.
 */
#include "apps.h"
#include "console.h"
#include "fs.h"
#include "gui.h"
#include "html.h"
#include "http.h"
#include "input.h"
#include "json.h"
#include "memory.h"
#include "sound.h"
#include "string.h"
#include "task.h"
#include "textedit.h"
#include "time.h"
#include "url.h"

/* Das Fenster lässt sich in der Größe ändern - fast alle Maße hängen daran. */
#define WIDTH        (b->width)
#define HEIGHT       (b->height)
#define START_WIDTH  960
#define START_HEIGHT 620
#define TOOLBAR_H    42
#define CONTENT_X    10
#define CONTENT_Y    (TOOLBAR_H + 4)
#define ROW_H        18
#define STATUS_H     26
#define STATUS_Y     (HEIGHT - STATUS_H)
#define ROWS         ((STATUS_Y - CONTENT_Y - 4) / ROW_H)
#define CONTENT_H    (ROWS * ROW_H)
#define SCROLL_W     10
#define SCROLL_X     (WIDTH - SCROLL_W - 4)
#define COLUMNS      ((SCROLL_X - CONTENT_X - 6) / FONT_W)
#define HISTORY_MAX  40
#define PAGE_MAX     (8u * 1024 * 1024)
#define REDIRECTS    10
#define START_PAGE   "about:start"

#define C_BACKGROUND RGB(252, 252, 250)
#define C_TOOLBAR    RGB(232, 230, 224)
#define C_TEXT       RGB(34, 38, 48)
#define C_HEADING    RGB(20, 44, 92)
#define C_MUTED      RGB(128, 132, 142)
#define C_LINK       RGB(30, 90, 190)
#define C_FIELD      RGB(255, 255, 255)
#define C_BORDER     RGB(160, 164, 172)
#define C_FOCUS      RGB(48, 104, 196)
#define C_BUTTON     RGB(60, 70, 92)
#define C_BUTTON_TXT RGB(240, 242, 246)
#define C_ERROR      RGB(196, 56, 56)
#define C_GOOD       RGB(40, 130, 70)
#define C_SELECTED   RGB(206, 222, 248)

typedef struct cookie {
    struct cookie *next;
    char *name, *value;
    char domain[128], path[128];
    bool secure, host_only;
} cookie_t;

typedef enum { OVERLAY_NONE, OVERLAY_PICKER, OVERLAY_TEXT } overlay_t;

typedef struct {
    window_t *window;
    int width, height;
    bool closing;

    char url[URL_SIZE];                 /* Adresse der gezeigten Seite */
    page_t *page;
    char *source;                       /* ihr Quelltext - zum Neu-Umbrechen bei anderer Breite */
    size_t source_len;
    bool source_html;
    int page_columns;                   /* für diese Breite ist die Seite gesetzt */
    int scroll;
    int focus;                          /* Formularfeld mit der Schreibmarke oder -1 */
    char *history[HISTORY_MAX];         /* besuchte Adressen; die oberste ist die aktuelle */
    int history_count;

    char address[URL_SIZE];             /* Text der Adresszeile */
    size_t address_len;
    bool address_focus;

    /* Laden im Helfer-Task */
    bool busy;
    uint64_t busy_since;
    char job_url[URL_SIZE];
    char *job_body;                     /* Formulardaten für POST oder NULL */
    bool job_remember;                  /* nach Erfolg in den Verlauf */
    page_t *incoming;
    char incoming_url[URL_SIZE];
    char *incoming_source;
    size_t incoming_len;
    bool incoming_html;
    int incoming_columns;
    bool incoming_ready;
    cookie_t *cookies;                  /* gehören dem Helfer */

    char status[200];
    bool status_error;

    overlay_t overlay;
    int overlay_widget;
    int picker_selected, picker_top;
    textedit_t edit;

    /* Rückfrage: Passwort über unverschlüsselte Verbindung senden? */
    bool ask_insecure;
    int pending_form, pending_button;
} browser_t;

static void set_status(browser_t *b, bool error, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    kvsnprintf(b->status, sizeof(b->status), fmt, ap);
    va_end(ap);
    b->status_error = error;
}

static bool starts_with(const char *text, const char *prefix)
{
    while (*prefix) {
        if (*text++ != *prefix++)
            return false;
    }
    return true;
}

/* --- Cookies -------------------------------------------------------------------------------- */

static void cookie_free(cookie_t *c)
{
    if (c->value)
        memset(c->value, 0, strlen(c->value));  /* oft ein Anmeldegeheimnis */
    kfree(c->name);
    kfree(c->value);
    kfree(c);
}

static char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

static bool word_at(const char *text, const char *word)
{
    while (*word) {
        if (lower(*text++) != *word++)
            return false;
    }
    return true;
}

/* Gehört der Rechner zur Domain des Cookies? */
static bool domain_matches(const cookie_t *c, const char *host)
{
    size_t h = strlen(host), d = strlen(c->domain);
    if (strcmp(host, c->domain) == 0)
        return true;
    return !c->host_only && h > d && host[h - d - 1] == '.' &&
           strcmp(host + h - d, c->domain) == 0;
}

static bool path_matches(const cookie_t *c, const char *path)
{
    size_t n = strlen(c->path);
    if (memcmp(path, c->path, n) != 0 && !(n == 1 && c->path[0] == '/'))
        return false;
    if (n == 1 && c->path[0] == '/')
        return true;
    return c->path[n - 1] == '/' || path[n] == '\0' || path[n] == '/' || path[n] == '?';
}

/* Verarbeitet eine Zeile "name=wert; Path=/; Secure; ..." des Servers. */
static void cookie_set(browser_t *b, const char *host, const char *path, bool secure,
                       const char *line, size_t len)
{
    const char *end = line + len;
    const char *eq = line;
    while (eq < end && *eq != '=' && *eq != ';')
        eq++;
    if (eq == line || eq >= end || *eq != '=')
        return;
    const char *value_end = eq + 1;
    while (value_end < end && *value_end != ';')
        value_end++;

    cookie_t *c = kzalloc(sizeof(*c));
    if (!c)
        return;
    size_t name_len = (size_t)(eq - line), value_len = (size_t)(value_end - eq - 1);
    c->name = kmalloc(name_len + 1);
    c->value = kmalloc(value_len + 1);
    if (!c->name || !c->value || name_len > 255 || value_len > 4096) {
        cookie_free(c);
        return;
    }
    memcpy(c->name, line, name_len);
    c->name[name_len] = '\0';
    memcpy(c->value, eq + 1, value_len);
    c->value[value_len] = '\0';

    /* Vorgaben: nur für diesen Rechner, für den Ordner der Seite */
    strlcpy(c->domain, host, sizeof(c->domain));
    c->host_only = true;
    size_t dir = 0;
    for (size_t i = 0; path[i] && path[i] != '?'; i++) {
        if (path[i] == '/')
            dir = i;
    }
    if (dir == 0 || dir >= sizeof(c->path))
        dir = 1;
    memcpy(c->path, path[0] == '/' ? path : "/", dir);
    c->path[dir] = '\0';

    bool expired = false;
    for (const char *p = value_end; p < end; ) {
        while (p < end && (*p == ';' || *p == ' '))
            p++;
        const char *attr = p;
        while (p < end && *p != ';')
            p++;
        size_t n = (size_t)(p - attr);
        if (n >= 7 && word_at(attr, "domain=")) {
            const char *d = attr + 7;
            size_t dn = n - 7;
            if (dn > 0 && *d == '.') {
                d++;
                dn--;
            }
            if (dn > 0 && dn < sizeof(c->domain)) {
                for (size_t i = 0; i < dn; i++)
                    c->domain[i] = lower(d[i]);
                c->domain[dn] = '\0';
                c->host_only = false;
            }
        } else if (n >= 5 && word_at(attr, "path=") && attr[5] == '/' &&
                   n - 5 < sizeof(c->path)) {
            memcpy(c->path, attr + 5, n - 5);
            c->path[n - 5] = '\0';
        } else if (n == 6 && word_at(attr, "secure")) {
            c->secure = true;
        } else if (n >= 8 && word_at(attr, "max-age=")) {
            expired = attr[8] == '0' || attr[8] == '-';
        } else if (n >= 8 && word_at(attr, "expires=")) {
            /* Ein Datum in der Vergangenheit löscht das Cookie: Jahreszahl vergleichen */
            datetime_t now;
            time_now(&now);
            for (const char *q = attr + 8; q + 4 <= attr + n; q++) {
                if (q[0] >= '1' && q[0] <= '2' && q[1] >= '0' && q[1] <= '9' &&
                    q[2] >= '0' && q[2] <= '9' && q[3] >= '0' && q[3] <= '9' &&
                    (q + 4 == attr + n || q[4] == ' ')) {
                    int year = (q[0] - '0') * 1000 + (q[1] - '0') * 100 +
                               (q[2] - '0') * 10 + (q[3] - '0');
                    expired = expired || year < now.year;
                    break;
                }
            }
        }
    }

    /* Ein Server darf nur für sich und seine Domain Cookies setzen. */
    bool allowed = domain_matches(c, host) && (!c->secure || secure) &&
                   strlen(c->domain) > 0;
    for (cookie_t **link = &b->cookies; *link; ) {
        cookie_t *old = *link;
        if (allowed && strcmp(old->name, c->name) == 0 &&
            strcmp(old->domain, c->domain) == 0 && strcmp(old->path, c->path) == 0) {
            *link = old->next;          /* ersetzt (oder gelöscht) */
            cookie_free(old);
        } else {
            link = &old->next;
        }
    }
    if (allowed && !expired) {
        c->next = b->cookies;
        b->cookies = c;
    } else {
        cookie_free(c);
    }
}

static void cookies_store(browser_t *b, const char *url, const char *lines)
{
    char host[128];
    const char *path;
    bool secure;
    if (!lines || !url_split(url, &secure, host, sizeof(host), &path))
        return;
    while (*lines) {
        const char *end = lines;
        while (*end && *end != '\n')
            end++;
        cookie_set(b, host, path, secure, lines, (size_t)(end - lines));
        lines = *end ? end + 1 : end;
    }
}

/* Hängt die Kopfzeile "Cookie: ..." für diese Adresse an. */
static void cookies_header(const browser_t *b, const char *url, strbuf_t *out)
{
    char host[128];
    const char *path;
    bool secure, first = true;
    if (!url_split(url, &secure, host, sizeof(host), &path))
        return;
    for (const cookie_t *c = b->cookies; c; c = c->next) {
        if (!domain_matches(c, host) || !path_matches(c, path) || (c->secure && !secure))
            continue;
        sb_append(out, first ? "Cookie: " : "; ");
        sb_append(out, c->name);
        sb_append(out, "=");
        sb_append(out, c->value);
        first = false;
    }
    if (!first)
        sb_append(out, "\r\n");
}

/* --- Seiten holen (Helfer-Task) ---------------------------------------------------------------- */

static const char start_page[] =
    "<title>Startseite</title>"
    "<h1>HelloOS-Browser</h1>"
    "<p>Gib oben eine Adresse ein (Strg+L) und drücke Enter - oder suche hier:</p>"
    "<form action=\"https://html.duckduckgo.com/html/\" method=\"get\">"
    "<input name=\"q\" size=\"44\"> <input type=\"submit\" value=\"Im Web suchen\"></form>"
    "<p>Die Suche geht an DuckDuckGo.</p>"
    "<h2>Was dieser Browser kann</h2>"
    "<ul><li>Seiten als Text mit Links anzeigen</li>"
    "<li>Formulare ausfüllen und absenden, auch Anmeldungen (Cookies bleiben, "
    "solange das Fenster offen ist)</li>"
    "<li>Verschlüsselte Verbindungen (https) mit Prüfung des Servers</li>"
    "<li>Dateien, die keine Webseiten sind, bei den eigenen Dateien speichern</li></ul>"
    "<h2>Was er nicht kann</h2>"
    "<ul><li>JavaScript - Seiten, die darauf angewiesen sind, bleiben leer oder "
    "unbedienbar</li><li>Gestaltung (Stylesheets), Bilder, Videos</li>"
    "<li>Dateien hochladen</li></ul>"
    "<h2>Bedienung</h2>"
    "<ul><li>Pfeiltasten, Bild auf/ab, Pos1/Ende: blättern</li>"
    "<li>Tab: zum nächsten Formularfeld; Enter im Feld: absenden</li>"
    "<li>Rücktaste oder Alt+Pfeil links: zurück; Strg+R: neu laden</li></ul>";

/* Wandelt Latin-1 (ISO-8859-1, Windows-1252) in UTF-8 um; liefert einen neuen Puffer. */
static char *latin1_to_utf8(const char *data, size_t len, size_t *out_len)
{
    char *out = kmalloc(len * 2 + 1);
    size_t n = 0;
    if (!out)
        return NULL;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)data[i];
        if (c < 0x80) {
            out[n++] = (char)c;
        } else {
            out[n++] = (char)(0xC0 | (c >> 6));
            out[n++] = (char)(0x80 | (c & 0x3F));
        }
    }
    out[n] = '\0';
    *out_len = n;
    return out;
}

static bool mentions(const char *text, size_t len, const char *word)
{
    size_t n = strlen(word);
    for (size_t i = 0; i + n <= len; i++) {
        if (word_at(text + i, word))
            return true;
    }
    return false;
}

/* Übergibt eine fertige Seite samt Quelltext (kmalloc) an den Fenster-Task. */
static void deliver(browser_t *b, page_t *page, const char *url, char *source,
                    size_t len, bool html, int columns)
{
    while (b->incoming_ready && !b->closing)
        task_sleep(20);
    kfree(b->incoming_source);
    b->incoming_source = source;
    b->incoming_len = len;
    b->incoming_html = html;
    b->incoming_columns = columns;
    b->incoming = page;
    strlcpy(b->incoming_url, url, sizeof(b->incoming_url));
    b->incoming_ready = true;
}

/* Speichert etwas, das keine Webseite ist, bei den eigenen Dateien. */
static void save_download(browser_t *b, const char *url, const http_response_t *response)
{
    char name[FS_NAME_MAX];
    const char *base = url, *end = url;
    while (*end && *end != '?')
        end++;
    for (const char *p = url; p < end; p++) {
        if (*p == '/' && p + 1 < end)
            base = p + 1;
    }
    size_t n = (size_t)(end - base);
    if (n == 0 || n >= sizeof(name)) {
        set_status(b, true, "Das ist keine Webseite (%s) - und der Name eignet sich nicht "
                   "zum Speichern", response->content_type);
        return;
    }
    memcpy(name, base, n);
    name[n] = '\0';
    int err = fs_write(name, response->body, response->length, false);
    if (err == FS_OK)
        set_status(b, false, "Keine Webseite - gespeichert als %s (%zu Bytes)", name,
                   response->length);
    else
        set_status(b, true, "Keine Webseite, und Speichern als %s ging nicht: %s", name,
                   fs_error_text(err));
}

static void worker(void *arg)
{
    browser_t *b = arg;
    char *url = kmalloc(URL_SIZE), *next = kmalloc(URL_SIZE);
    http_extra_t *extra = kmalloc(sizeof(*extra));
    char *body = b->job_body;
    bool post = body != NULL;

    b->job_body = NULL;
    if (!url || !next || !extra) {
        set_status(b, true, "Nicht genug Speicher");
        goto done;
    }
    strlcpy(url, b->job_url, URL_SIZE);

    if (strcmp(url, START_PAGE) == 0) {
        int columns = COLUMNS;
        size_t len = sizeof(start_page) - 1;
        char *copy = kmalloc(len + 1);
        page_t *page = copy ? html_layout(start_page, len, START_PAGE, columns) : NULL;
        if (page) {
            memcpy(copy, start_page, len + 1);
            deliver(b, page, url, copy, len, true, columns);
        } else {
            kfree(copy);
        }
        set_status(b, false, "");
        goto done;
    }

    for (int hop = 0; ; hop++) {
        char host[128];
        const char *path;
        bool secure;
        http_response_t response;
        strbuf_t headers = { 0 };

        if (hop > REDIRECTS || !url_split(url, &secure, host, sizeof(host), &path)) {
            set_status(b, true, hop > REDIRECTS ? "Zu viele Weiterleitungen"
                                                : "Ungültige Adresse");
            break;
        }
        set_status(b, false, "Lade %s ...", host);
        cookies_header(b, url, &headers);
        sb_append(&headers, "Accept-Language: de,en;q=0.7\r\n");
        if (post)
            sb_append(&headers, "Content-Type: application/x-www-form-urlencoded\r\n");
        http_options_t options = {
            .method = post ? "POST" : "GET",
            .headers = headers.data,
            .body = post ? body : NULL,
            .body_len = post ? strlen(body) : 0,
            .max_body = PAGE_MAX,
            .timeout_ms = 30000,
            .extra = extra,
        };
        bool ok = !headers.failed && http_request(url, &options, &response);
        if (headers.data)
            memset(headers.data, 0, headers.len);       /* enthält Anmelde-Cookies */
        sb_free(&headers);
        if (!ok) {
            kfree(extra->cookies);
            extra->cookies = NULL;
            set_status(b, true, "%s", response.error[0] ? response.error
                                                        : "Nicht genug Speicher");
            break;
        }
        cookies_store(b, url, extra->cookies);
        kfree(extra->cookies);
        extra->cookies = NULL;

        bool redirect = (response.status >= 301 && response.status <= 303) ||
                        response.status == 307 || response.status == 308;
        if (redirect && extra->location[0]) {
            kfree(response.body);
            if (!url_resolve(url, extra->location, next, URL_SIZE)) {
                set_status(b, true, "Weiterleitung auf eine Adresse, die hier nicht geht");
                break;
            }
            /* Nach dem Absenden eines Formulars geht es üblicherweise mit GET weiter. */
            if (response.status != 307 && response.status != 308)
                post = false;
            strlcpy(url, next, URL_SIZE);
            continue;
        }

        /* Die Antwort ist da: Webseite, Text oder etwas zum Speichern? */
        const char *type = response.content_type;
        bool html = mentions(type, strlen(type), "html") ||
                    (!type[0] && response.length > 0 && response.body[0] == '<');
        bool text = starts_with(type, "text/") || mentions(type, strlen(type), "json") ||
                    mentions(type, strlen(type), "xml");
        if (!html && !text && response.length > 0) {
            save_download(b, url, &response);
            kfree(response.body);
            break;
        }

        char *content = response.body;
        size_t length = response.length;
        size_t head = length < 2048 ? length : 2048;
        if (mentions(type, strlen(type), "8859-1") || mentions(type, strlen(type), "1252") ||
            (!mentions(type, strlen(type), "utf-8") &&
             (mentions(content, head, "charset=iso-8859-1") ||
              mentions(content, head, "charset=\"iso-8859-1") ||
              mentions(content, head, "charset=windows-1252") ||
              mentions(content, head, "charset=\"windows-1252")))) {
            char *converted = latin1_to_utf8(content, length, &length);
            if (converted) {
                kfree(content);
                content = converted;
            }
        }
        int columns = COLUMNS;
        page_t *page = html ? html_layout(content, length, url, columns)
                            : html_plain_text(content, length, columns);
        if (!page) {
            kfree(content);
            set_status(b, true, "Nicht genug Speicher für diese Seite");
            break;
        }
        if (response.status >= 400)
            set_status(b, true, "Der Server meldet einen Fehler (Status %d)", response.status);
        else if (page->failed)
            set_status(b, true, "Die Seite ist zu groß - sie wird unvollständig gezeigt");
        else
            set_status(b, false, "");
        deliver(b, page, url, content, length, html, columns);
        break;
    }

done:
    if (body) {
        memset(body, 0, strlen(body));  /* kann ein Passwort enthalten */
        kfree(body);
    }
    kfree(url);
    kfree(next);
    kfree(extra);
    b->busy = false;
}

/* Beginnt das Laden; body (kmalloc) macht daraus ein abgesendetes Formular. */
static void load(browser_t *b, const char *url, char *body, bool remember)
{
    if (b->busy) {
        kfree(body);
        return;
    }
    strlcpy(b->job_url, url, sizeof(b->job_url));
    b->job_body = body;
    b->job_remember = remember;
    b->busy = true;
    b->busy_since = time_ms();
    b->ask_insecure = false;
    if (task_create("browser-laden", worker, b, NULL) < 0) {
        b->busy = false;
        kfree(b->job_body);
        b->job_body = NULL;
        set_status(b, true, "Zu viele Tasks - bitte etwas schließen");
    }
}

static void set_address(browser_t *b, const char *text)
{
    strlcpy(b->address, text, sizeof(b->address));
    b->address_len = strlen(b->address);
}

static void clamp_scroll(browser_t *b);

/*
 * Setzt die gezeigte Seite für die jetzige Fensterbreite neu. Was der Benutzer
 * in Formularfelder eingetragen hat, wandert in die neue Fassung mit.
 */
static void relayout(browser_t *b)
{
    page_t *old = b->page;
    if (!old || !b->source || b->page_columns == COLUMNS)
        return;
    page_t *fresh = b->source_html ? html_layout(b->source, b->source_len, b->url, COLUMNS)
                                   : html_plain_text(b->source, b->source_len, COLUMNS);
    if (!fresh)
        return;

    /* Derselbe Quelltext ergibt dieselben Felder in derselben Reihenfolge. */
    bool same = fresh->widget_count == old->widget_count;
    for (int i = 0; same && i < old->widget_count; i++) {
        const widget_t *from = &old->widgets[i];
        widget_t *to = &fresh->widgets[i];
        if (from->kind != to->kind)
            continue;
        if (from->value)
            widget_set_value(to, from->value, strlen(from->value));
        to->checked = from->checked;
        if (from->option_count == to->option_count)
            to->selected = from->selected;
    }
    if (!same) {
        b->focus = -1;
        b->overlay = OVERLAY_NONE;
    }
    b->scroll = old->line_count > 0 ? b->scroll * fresh->line_count / old->line_count : 0;
    b->page = fresh;
    b->page_columns = COLUMNS;
    page_free(old);
    clamp_scroll(b);
}

/* Übernimmt eine fertig geladene Seite. */
static void take_incoming(browser_t *b)
{
    if (!b->incoming_ready)
        return;
    page_free(b->page);
    kfree(b->source);
    b->page = b->incoming;
    b->source = b->incoming_source;
    b->source_len = b->incoming_len;
    b->source_html = b->incoming_html;
    b->page_columns = b->incoming_columns;
    b->incoming_source = NULL;
    b->incoming = NULL;
    strlcpy(b->url, b->incoming_url, sizeof(b->url));
    b->incoming_ready = false;
    b->focus = -1;
    b->overlay = OVERLAY_NONE;
    b->address_focus = false;
    set_address(b, strcmp(b->url, START_PAGE) == 0 ? "" : b->url);

    /* Steht die Hauptüberschrift weit unten (lange Menüs davor), dort beginnen. */
    int heading = b->page->heading_line;
    b->scroll = heading > ROWS / 2 ? heading - 1 : 0;
    if (b->scroll > 0 && !b->status[0])
        set_status(b, false, "Zum Seitenanfang mit Pos1 - dort stehen Menüs und Links");
    relayout(b);                        /* falls das Fenster inzwischen breiter oder schmaler ist */

    if (b->job_remember &&
        (b->history_count == 0 || strcmp(b->history[b->history_count - 1], b->url) != 0)) {
        if (b->history_count == HISTORY_MAX) {
            kfree(b->history[0]);
            memmove(b->history, b->history + 1, (HISTORY_MAX - 1) * sizeof(char *));
            b->history_count--;
        }
        size_t n = strlen(b->url) + 1;
        char *copy = kmalloc(n);
        if (copy) {
            memcpy(copy, b->url, n);
            b->history[b->history_count++] = copy;
        }
    }
}

static void go_back(browser_t *b)
{
    if (b->busy)
        return;
    if (b->history_count < 2) {
        set_status(b, false, "Das ist die erste Seite");
        return;
    }
    kfree(b->history[--b->history_count]);
    load(b, b->history[b->history_count - 1], NULL, false);
}

/* Die Adresszeile abschicken: fehlt "https://", wird es ergänzt. */
static void go_address(browser_t *b)
{
    char url[URL_SIZE];
    if (b->address_len == 0)
        return;
    if (starts_with(b->address, "http://") || starts_with(b->address, "https://") ||
        starts_with(b->address, "about:"))
        strlcpy(url, b->address, sizeof(url));
    else
        ksnprintf(url, sizeof(url), "https://%s", b->address);
    b->address_focus = false;
    load(b, url, NULL, true);
}

/* --- Formulare ----------------------------------------------------------------------------------- */

static void append_pair(strbuf_t *data, const char *name, const char *value)
{
    size_t size = (strlen(name) + strlen(value)) * 6 + 16;
    char *encoded = kmalloc(size);
    if (!encoded) {
        data->failed = true;
        return;
    }
    if (data->len)
        sb_append(data, "&");
    url_encode(encoded, size, name);
    sb_append(data, encoded);
    sb_append(data, "=");
    url_encode(encoded, size, value);
    sb_append(data, encoded);
    memset(encoded, 0, size);
    kfree(encoded);
}

/* Sendet das Formular ab; button: der gedrückte Knopf oder -1 (Enter im Textfeld). */
static void submit(browser_t *b, int form_index, int button, bool insecure_ok)
{
    page_t *page = b->page;
    if (form_index < 0 || form_index >= page->form_count || !page->forms[form_index].action) {
        set_status(b, true, "Dieses Feld gehört zu keinem Formular, das sich absenden lässt");
        return;
    }
    const form_t *form = &page->forms[form_index];

    /* Ohne ausdrücklich gedrückten Knopf gilt der erste des Formulars. */
    for (int i = 0; button < 0 && i < page->widget_count; i++) {
        if (page->widgets[i].form == form_index && page->widgets[i].kind == W_SUBMIT)
            button = i;
    }

    bool has_password = false;
    strbuf_t data = { 0 };
    for (int i = 0; i < page->widget_count; i++) {
        const widget_t *w = &page->widgets[i];
        if (w->form != form_index || !w->name || !w->name[0])
            continue;
        const char *value = w->value ? w->value : "";
        switch (w->kind) {
        case W_PASSWORD:
            has_password = has_password || value[0];
            append_pair(&data, w->name, value);
            break;
        case W_TEXT: case W_HIDDEN: case W_TEXTAREA:
            append_pair(&data, w->name, value);
            break;
        case W_CHECKBOX: case W_RADIO:
            if (w->checked)
                append_pair(&data, w->name, value);
            break;
        case W_SELECT:
            if (w->option_count > 0) {
                const char *chosen = page->options[w->first_option + w->selected].value;
                append_pair(&data, w->name, chosen ? chosen : "");
            }
            break;
        case W_SUBMIT:
            if (i == button)
                append_pair(&data, w->name, value);
            break;
        }
    }
    if (data.failed) {
        sb_free(&data);
        set_status(b, true, "Nicht genug Speicher");
        return;
    }
    if (!data.data)
        sb_append(&data, "");

    if (has_password && starts_with(form->action, "http://") && !insecure_ok) {
        if (data.data)
            memset(data.data, 0, data.len);
        sb_free(&data);
        b->ask_insecure = true;
        sound_play(SOUND_NOTIFY);
        b->pending_form = form_index;
        b->pending_button = button;
        return;
    }

    if (form->post) {
        load(b, form->action, data.data, true);         /* der Puffer gehört nun dem Helfer */
        return;
    }
    /* GET: Die Daten wandern in die Adresse (eine alte Anfrage darin entfällt). */
    char *url = kmalloc(URL_SIZE);
    if (url) {
        size_t n = 0;
        while (form->action[n] && form->action[n] != '?' && n + 2 < URL_SIZE)
            n++;
        memcpy(url, form->action, n);
        url[n] = '\0';
        if (n + 1 + (data.data ? data.len : 0) < URL_SIZE)
            ksnprintf(url + n, URL_SIZE - n, "?%s", data.data ? data.data : "");
        load(b, url, NULL, true);
        kfree(url);
    }
    sb_free(&data);
}

/* --- Zeichnen ------------------------------------------------------------------------------------ */

static rect_t back_rect(void)
{
    return rect_make(8, 8, 70, 26);
}

static rect_t reload_rect(void)
{
    return rect_make(84, 8, 86, 26);
}

static rect_t address_rect(const browser_t *b)
{
    return rect_make(178, 8, WIDTH - 178 - 8, 26);
}

static rect_t picker_rect(const browser_t *b)
{
    const widget_t *w = &b->page->widgets[b->overlay_widget];
    int rows = w->option_count < 18 ? w->option_count : 18;
    int width = 46 * FONT_W;
    return rect_make((WIDTH - width) / 2, CONTENT_Y + 30, width, rows * ROW_H + 8);
}

static rect_t done_rect(const browser_t *b)
{
    return rect_make(WIDTH - 12 - 150, CONTENT_Y + 4, 150, 24);
}

/* Zeigt von einem Text höchstens columns Zeichen - bei tail das Ende, sonst den Anfang. */
static void draw_clipped(surface_t *s, int x, int y, const char *text, int columns,
                         color_t color, bool tail)
{
    char shown[256];
    int total = gfx_text_width(text) / FONT_W;
    size_t n = 0;
    if (tail) {
        for (int extra = total - columns; extra > 0 && *text; extra--) {
            text++;
            while (((uint8_t)*text & 0xC0) == 0x80)
                text++;
        }
    }
    for (int i = 0; *text && *text != '\n' && i < columns && n + 5 < sizeof(shown); i++) {
        shown[n++] = *text++;
        while (((uint8_t)*text & 0xC0) == 0x80 && n + 5 < sizeof(shown))
            shown[n++] = *text++;
    }
    shown[n] = '\0';
    gfx_text(s, x, y, shown, color, false);
}

static void draw_widget(const browser_t *b, surface_t *s, int index, int x, int y)
{
    const page_t *page = b->page;
    const widget_t *w = &page->widgets[index];
    int width = w->width * FONT_W;
    bool focused = index == b->focus;
    char stars[64];

    switch (w->kind) {
    case W_TEXT: case W_PASSWORD: case W_TEXTAREA: case W_SELECT: {
        gfx_fill(s, x, y, width, ROW_H - 1, C_FIELD);
        gfx_frame(s, x, y, width, ROW_H - 1, focused ? C_FOCUS : C_BORDER);
        const char *text = w->value ? w->value : "";
        int columns = w->width - 2;
        if (w->kind == W_PASSWORD) {
            size_t n = 0;
            for (const char *p = text; *p && n + 1 < sizeof(stars); p++) {
                if (((uint8_t)*p & 0xC0) != 0x80)
                    stars[n++] = '*';
            }
            stars[n] = '\0';
            text = stars;
        } else if (w->kind == W_SELECT) {
            text = w->option_count ? page->options[w->first_option + w->selected].label : "";
            columns -= 2;
            gfx_text(s, x + width - 2 * FONT_W, y, "v", C_MUTED, true);
        } else if (w->kind == W_TEXTAREA && !text[0]) {
            draw_clipped(s, x + FONT_W, y, "(anklicken zum Schreiben)", columns, C_MUTED,
                         false);
            break;
        }
        draw_clipped(s, x + FONT_W, y, text ? text : "", columns, C_TEXT,
                     focused && w->kind != W_SELECT && w->kind != W_TEXTAREA);
        if (focused && (w->kind == W_TEXT || w->kind == W_PASSWORD)) {
            int shown = gfx_text_width(text) / FONT_W;
            if (shown > columns)
                shown = columns;
            gfx_fill(s, x + FONT_W + shown * FONT_W + 1, y + 1, 2, FONT_H - 2, C_TEXT);
        }
        break;
    }
    case W_CHECKBOX: case W_RADIO:
        gfx_fill(s, x + 4, y + 2, 13, 13, C_FIELD);
        gfx_frame(s, x + 4, y + 2, 13, 13, focused ? C_FOCUS : C_BORDER);
        if (w->checked)
            gfx_fill(s, x + 7, y + 5, 7, 7, w->kind == W_RADIO ? C_FOCUS : C_TEXT);
        break;
    case W_SUBMIT:
        gfx_fill(s, x, y, width, ROW_H - 1, focused ? C_FOCUS : C_BUTTON);
        draw_clipped(s, x + 2 * FONT_W, y, w->label ? w->label : "", w->width - 4,
                     C_BUTTON_TXT, false);
        break;
    case W_HIDDEN:
        break;
    }
}

static void draw_page(const browser_t *b, surface_t *s)
{
    const page_t *page = b->page;
    gfx_set_clip(s, rect_make(0, CONTENT_Y, SCROLL_X - 2, CONTENT_H));
    for (int row = 0; row < ROWS && b->scroll + row < page->line_count; row++) {
        const line_t *line = &page->lines[b->scroll + row];
        int y = CONTENT_Y + row * ROW_H;
        for (uint32_t i = 0; i < line->count; i++) {
            const span_t *span = &page->spans[line->first + i];
            int x = CONTENT_X + span->column * FONT_W;
            if (span->widget >= 0) {
                draw_widget(b, s, span->widget, x, y);
                continue;
            }
            color_t color = span->style & STYLE_LINK ? C_LINK
                          : span->style & STYLE_HEADING ? C_HEADING
                          : span->style & STYLE_MUTED ? C_MUTED : C_TEXT;
            const char *text = page->pool + span->text;
            const char *end = text + span->bytes;
            int cx = x;
            while (text < end) {
                uint32_t cp = utf8_next(&text);
                gfx_glyph(s, cx, y, cp, color, span->style & STYLE_BOLD);
                cx += FONT_W;
            }
            if (span->style & STYLE_LINK)
                gfx_fill(s, x, y + FONT_H - 1, span->width * FONT_W, 1, C_LINK);
        }
    }
    gfx_reset_clip(s);

    /* Rollbalken */
    gfx_fill(s, SCROLL_X, CONTENT_Y, SCROLL_W, CONTENT_H, C_TOOLBAR);
    if (page->line_count > ROWS) {
        int h = CONTENT_H * ROWS / page->line_count;
        if (h < 16)
            h = 16;
        int y = CONTENT_Y + (CONTENT_H - h) * b->scroll / (page->line_count - ROWS);
        gfx_fill(s, SCROLL_X, y, SCROLL_W, h, C_BORDER);
    }
}

static void draw_overlay(browser_t *b, surface_t *s)
{
    const widget_t *w = &b->page->widgets[b->overlay_widget];
    if (b->overlay == OVERLAY_PICKER) {
        rect_t r = picker_rect(b);
        int rows = (r.h - 8) / ROW_H;
        gfx_darken(s, 0, CONTENT_Y, WIDTH, CONTENT_H, 35);
        gfx_fill(s, r.x, r.y, r.w, r.h, C_FIELD);
        gfx_frame(s, r.x, r.y, r.w, r.h, C_FOCUS);
        for (int i = 0; i < rows && b->picker_top + i < w->option_count; i++) {
            int index = b->picker_top + i;
            const char *label = b->page->options[w->first_option + index].label;
            int y = r.y + 4 + i * ROW_H;
            if (index == b->picker_selected)
                gfx_fill(s, r.x + 2, y, r.w - 4, ROW_H, C_SELECTED);
            draw_clipped(s, r.x + 8, y + 1, label ? label : "", r.w / FONT_W - 2, C_TEXT,
                         false);
        }
    } else if (b->overlay == OVERLAY_TEXT) {
        gfx_fill(s, 0, CONTENT_Y, WIDTH, CONTENT_H, C_TOOLBAR);
        gfx_text(s, 12, CONTENT_Y + 8, "Text des Formularfelds bearbeiten", C_TEXT, true);
        ui_button(s, done_rect(b), "Fertig (Esc)", true);
        textedit_draw(&b->edit, s, 8, CONTENT_Y + 34, false);
    }
}

static void draw(browser_t *b)
{
    surface_t *s = window_surface(b->window);
    char text[260];

    gfx_fill(s, 0, 0, WIDTH, HEIGHT, C_BACKGROUND);
    gfx_fill(s, 0, 0, WIDTH, TOOLBAR_H, C_TOOLBAR);
    ui_button(s, back_rect(), "Zurück", false);
    ui_button(s, reload_rect(), "Neu laden", false);

    rect_t a = address_rect(b);
    gfx_fill(s, a.x, a.y, a.w, a.h, C_FIELD);
    gfx_frame(s, a.x, a.y, a.w, a.h, b->address_focus ? C_FOCUS : C_BORDER);
    int columns = (a.w - 16) / FONT_W;
    if (b->address_len == 0 && !b->address_focus) {
        gfx_text(s, a.x + 8, a.y + 5, "Adresse eingeben (Strg+L), z. B. example.com",
                 C_MUTED, false);
    } else {
        draw_clipped(s, a.x + 8, a.y + 5, b->address, columns,
                     starts_with(b->address, "http://") ? C_ERROR : C_TEXT,
                     b->address_focus);
        if (b->address_focus) {
            int shown = gfx_text_width(b->address) / FONT_W;
            gfx_fill(s, a.x + 9 + (shown > columns ? columns : shown) * FONT_W, a.y + 5, 2,
                     FONT_H, C_TEXT);
        }
    }

    if (b->page)
        draw_page(b, s);
    if (b->page && b->overlay != OVERLAY_NONE)
        draw_overlay(b, s);

    /* Statuszeile: Rückfrage, Fortschritt, Meldung - oder Titel und Sicherheit der Seite */
    gfx_fill(s, 0, STATUS_Y, WIDTH, STATUS_H, C_TOOLBAR);
    int y = STATUS_Y + (STATUS_H - FONT_H) / 2;
    if (b->ask_insecure) {
        gfx_text(s, 10, y, "ACHTUNG: Das Passwort würde unverschlüsselt gesendet (kein https). "
                           "Trotzdem senden?  J / N", C_ERROR, true);
    } else if (b->busy) {
        ksnprintf(text, sizeof(text), "%s  (%llu s)", b->status[0] ? b->status : "Lade ...",
                  (time_ms() - b->busy_since) / 1000);
        gfx_text(s, 10, y, text, C_TEXT, false);
    } else if (b->status[0]) {
        draw_clipped(s, 10, y, b->status, (WIDTH - 20) / FONT_W,
                     b->status_error ? C_ERROR : C_GOOD, false);
    } else if (b->page) {
        bool secure = starts_with(b->url, "https://");
        const char *note = secure ? "verschlüsselt, Server geprüft"
                         : starts_with(b->url, "http://") ? "NICHT verschlüsselt" : "";
        int note_x = WIDTH - 10 - gfx_text_width(note);
        draw_clipped(s, 10, y, b->page->title ? b->page->title : "",
                     (note_x - 30) / FONT_W, C_TEXT, false);
        gfx_text(s, note_x, y, note, secure ? C_GOOD : C_ERROR, false);
    }
    window_invalidate(b->window, 0, 0, WIDTH, HEIGHT);
}

/* --- Eingaben ------------------------------------------------------------------------------------ */

static void clamp_scroll(browser_t *b)
{
    int max = b->page && b->page->line_count > ROWS ? b->page->line_count - ROWS : 0;
    if (b->scroll > max)
        b->scroll = max;
    if (b->scroll < 0)
        b->scroll = 0;
}

static void show_line(browser_t *b, int line)
{
    if (line < b->scroll)
        b->scroll = line > 2 ? line - 2 : 0;
    else if (line >= b->scroll + ROWS)
        b->scroll = line - ROWS + 3;
    clamp_scroll(b);
}

/* Tippen in einen kurzen Text (Adresszeile, Eingabefeld); false: Taste gehört nicht dazu. */
static bool type_into(char *text, size_t *len, size_t size, int key)
{
    if (key == '\b') {
        while (*len > 0 && ((uint8_t)text[*len - 1] & 0xC0) == 0x80)
            (*len)--;
        if (*len > 0)
            (*len)--;
    } else if (key >= 32 && key < 0x800 && key != 127) {
        if (*len + 3 >= size)
            return true;
        if (key < 0x80) {
            text[(*len)++] = (char)key;
        } else {
            text[(*len)++] = (char)(0xC0 | (key >> 6));
            text[(*len)++] = (char)(0x80 | (key & 0x3F));
        }
    } else {
        return false;
    }
    text[*len] = '\0';
    return true;
}

static void type_into_widget(widget_t *w, int key)
{
    size_t len = w->value ? strlen(w->value) : 0;
    char *text = kmalloc(len + 4);
    if (!text)
        return;
    if (len)
        memcpy(text, w->value, len);
    text[len] = '\0';
    if (len < 2000)
        type_into(text, &len, len + 4, key);
    else if (key == '\b')
        type_into(text, &len, len + 4, key);
    if (w->value)
        memset(w->value, 0, strlen(w->value));
    kfree(w->value);
    w->value = text;
}

static void open_text_overlay(browser_t *b, int index)
{
    const widget_t *w = &b->page->widgets[index];
    const char *text = w->value ? w->value : "";
    if (!textedit_set(&b->edit, text, strlen(text))) {
        set_status(b, true, "Nicht genug Speicher");
        return;
    }
    b->overlay = OVERLAY_TEXT;
    b->overlay_widget = index;
}

static void close_text_overlay(browser_t *b)
{
    widget_set_value(&b->page->widgets[b->overlay_widget], b->edit.text, b->edit.len);
    b->overlay = OVERLAY_NONE;
}

/* Ein Formularfeld wurde angeklickt oder mit Enter/Leertaste betätigt. */
static void activate_widget(browser_t *b, int index)
{
    page_t *page = b->page;
    widget_t *w = &page->widgets[index];
    b->focus = index;
    b->address_focus = false;
    switch (w->kind) {
    case W_CHECKBOX:
        w->checked = !w->checked;
        break;
    case W_RADIO:
        for (int i = 0; i < page->widget_count; i++) {
            widget_t *other = &page->widgets[i];
            if (other->kind == W_RADIO && other->form == w->form && other->name &&
                w->name && strcmp(other->name, w->name) == 0)
                other->checked = false;
        }
        w->checked = true;
        break;
    case W_SELECT:
        if (w->option_count > 0) {
            b->overlay = OVERLAY_PICKER;
            b->overlay_widget = index;
            b->picker_selected = w->selected;
            b->picker_top = w->selected > 8 ? w->selected - 8 : 0;
        }
        break;
    case W_TEXTAREA:
        open_text_overlay(b, index);
        break;
    case W_SUBMIT:
        submit(b, w->form, index, false);
        break;
    default:
        break;
    }
}

static void follow_link(browser_t *b, const char *target)
{
    if (target[0] == '#') {             /* Sprung innerhalb der Seite */
        for (int i = 0; i < b->page->anchor_count; i++) {
            if (strcmp(b->page->anchors[i].id, target + 1) == 0) {
                b->scroll = b->page->anchors[i].line;
                clamp_scroll(b);
                return;
            }
        }
        return;
    }
    load(b, target, NULL, true);
}

static void focus_next(browser_t *b, int step)
{
    page_t *page = b->page;
    int n = page ? page->widget_count : 0;
    int index = b->focus;
    for (int tries = 0; tries < n; tries++) {
        index += step;
        if (index >= n)
            index = 0;
        if (index < 0)
            index = n - 1;
        if (page->widgets[index].line >= 0) {
            b->focus = index;
            b->address_focus = false;
            show_line(b, page->widgets[index].line);
            return;
        }
    }
}

static void key_pressed(browser_t *b, int key, int mods)
{
    page_t *page = b->page;

    if (b->ask_insecure) {
        bool yes = key == 'j' || key == 'J';
        b->ask_insecure = false;
        if (yes)
            submit(b, b->pending_form, b->pending_button, true);
        else
            set_status(b, false, "Nicht gesendet");
        return;
    }
    if (b->overlay == OVERLAY_TEXT) {
        if (key == 27)
            close_text_overlay(b);
        else
            textedit_key(&b->edit, key, mods);
        return;
    }
    if (b->overlay == OVERLAY_PICKER) {
        widget_t *w = &page->widgets[b->overlay_widget];
        int rows = (picker_rect(b).h - 8) / ROW_H;
        if (key == KEY_UP && b->picker_selected > 0)
            b->picker_selected--;
        else if (key == KEY_DOWN && b->picker_selected + 1 < w->option_count)
            b->picker_selected++;
        else if (key == KEY_PAGE_UP)
            b->picker_selected = b->picker_selected > rows ? b->picker_selected - rows : 0;
        else if (key == KEY_PAGE_DOWN)
            b->picker_selected = b->picker_selected + rows < w->option_count
                                     ? b->picker_selected + rows : w->option_count - 1;
        else if (key == '\n') {
            w->selected = b->picker_selected;
            b->overlay = OVERLAY_NONE;
        } else if (key == 27) {
            b->overlay = OVERLAY_NONE;
        }
        if (b->picker_selected < b->picker_top)
            b->picker_top = b->picker_selected;
        if (b->picker_selected >= b->picker_top + rows)
            b->picker_top = b->picker_selected - rows + 1;
        return;
    }

    /* Kürzel, die überall gelten */
    if ((mods & MOD_CTRL) && key == 12) {               /* Strg+L */
        b->address_focus = true;
        b->focus = -1;
        set_address(b, "");
        return;
    }
    if ((mods & MOD_CTRL) && key == 18) {               /* Strg+R */
        if (b->url[0])
            load(b, b->url, NULL, false);
        return;
    }
    if ((mods & MOD_ALT) && key == KEY_LEFT) {
        go_back(b);
        return;
    }
    if (key == '\t') {
        focus_next(b, mods & MOD_SHIFT ? -1 : 1);
        return;
    }

    if (b->address_focus) {
        if (key == '\n') {
            go_address(b);
        } else if (key == 27) {
            b->address_focus = false;
            set_address(b, strcmp(b->url, START_PAGE) == 0 ? "" : b->url);
        } else if (key != ' ') {
            type_into(b->address, &b->address_len, sizeof(b->address), key);
        }
        return;
    }

    widget_t *w = page && b->focus >= 0 ? &page->widgets[b->focus] : NULL;
    if (w && (w->kind == W_TEXT || w->kind == W_PASSWORD)) {
        if (key == '\n') {
            submit(b, w->form, -1, false);
            return;
        }
        if (key == 27) {
            b->focus = -1;
            return;
        }
        if (key == '\b' || (key >= 32 && key < 0x800 && key != 127)) {
            type_into_widget(w, key);
            return;
        }
    } else if (w && (key == '\n' || key == ' ')) {
        activate_widget(b, b->focus);
        return;
    } else if (w && key == 27) {
        b->focus = -1;
        return;
    }

    switch (key) {
    case KEY_UP:        b->scroll -= 3; break;
    case KEY_DOWN:      b->scroll += 3; break;
    case KEY_PAGE_UP:   b->scroll -= ROWS - 2; break;
    case KEY_PAGE_DOWN:
    case ' ':           b->scroll += ROWS - 2; break;
    case KEY_HOME:      b->scroll = 0; break;
    case KEY_END:       b->scroll = page ? page->line_count : 0; break;
    case '\b':          go_back(b); return;
    default:            return;
    }
    clamp_scroll(b);
}

static void scroll_to_mouse(browser_t *b, int y)
{
    if (!b->page || b->page->line_count <= ROWS)
        return;
    b->scroll = (y - CONTENT_Y) * b->page->line_count / CONTENT_H - ROWS / 2;
    clamp_scroll(b);
}

static void mouse_pressed(browser_t *b, int x, int y)
{
    page_t *page = b->page;

    if (b->ask_insecure)
        return;
    if (b->overlay == OVERLAY_TEXT) {
        if (rect_contains(done_rect(b), x, y))
            close_text_overlay(b);
        else if (y >= CONTENT_Y + 34 && y < CONTENT_Y + 34 + textedit_height(&b->edit))
            textedit_click(&b->edit, x - 8, y - (CONTENT_Y + 34));
        return;
    }
    if (b->overlay == OVERLAY_PICKER) {
        rect_t r = picker_rect(b);
        if (rect_contains(r, x, y)) {
            int index = b->picker_top + (y - r.y - 4) / ROW_H;
            widget_t *w = &page->widgets[b->overlay_widget];
            if (index >= 0 && index < w->option_count)
                w->selected = index;
        }
        b->overlay = OVERLAY_NONE;
        return;
    }

    if (rect_contains(back_rect(), x, y)) {
        go_back(b);
    } else if (rect_contains(reload_rect(), x, y)) {
        if (b->url[0])
            load(b, b->url, NULL, false);
    } else if (rect_contains(address_rect(b), x, y)) {
        b->address_focus = true;
        b->focus = -1;
    } else if (page && x >= SCROLL_X - 2 && y >= CONTENT_Y && y < CONTENT_Y + CONTENT_H) {
        scroll_to_mouse(b, y);
    } else if (page && y >= CONTENT_Y && y < CONTENT_Y + CONTENT_H && !b->busy) {
        int line = b->scroll + (y - CONTENT_Y) / ROW_H;
        int column = (x - CONTENT_X) / FONT_W;
        b->address_focus = false;
        b->focus = -1;
        if (line >= page->line_count || x < CONTENT_X)
            return;
        const line_t *l = &page->lines[line];
        for (uint32_t i = 0; i < l->count; i++) {
            const span_t *span = &page->spans[l->first + i];
            if (column < span->column || column >= span->column + span->width)
                continue;
            if (span->widget >= 0)
                activate_widget(b, span->widget);
            else if (span->link >= 0)
                follow_link(b, page->links[span->link]);
            return;
        }
    }
}

void browser_app(void *arg)
{
    char *start = arg;                  /* Adresse (kmalloc) oder NULL */
    browser_t *b = kzalloc(sizeof(*b));
    if (b) {
        b->width = START_WIDTH;
        b->height = START_HEIGHT;
        textedit_init(&b->edit, (WIDTH - 16 - 12) / FONT_W,
                      (CONTENT_H - 42) / TEXTEDIT_LINE_H);
        b->window = window_create("Browser", WIDTH, HEIGHT);
    }
    if (b && b->window)
        window_set_resizable(b->window, 480, 300);
    if (!b || !b->window) {
        kfree(b);
        kfree(start);
        return;
    }
    b->focus = -1;
    if (start && *start) {
        set_address(b, start);
        go_address(b);
    } else {
        load(b, START_PAGE, NULL, true);
    }
    kfree(start);

    bool dragging = false;              /* Maustaste auf dem Rollbalken gedrückt */
    bool was_busy = false, redraw = true;
    for (;;) {
        if (b->incoming_ready || b->busy || was_busy)
            redraw = true;              /* Sekundenanzeige, neue Seite, Fehlermeldung */
        was_busy = b->busy;
        take_incoming(b);
        if (redraw)
            draw(b);
        redraw = false;

        /* Immer mit Frist warten: Der Helfer kann jederzeit fertig werden. */
        event_t ev;
        if (!window_get_event(b->window, &ev, b->busy ? 200 : 400))
            continue;
        redraw = true;
        if (ev.type == EV_CLOSE)
            break;
        if (ev.type == EV_RESIZE && window_resize(b->window, ev.x, ev.y)) {
            b->width = ev.x;
            b->height = ev.y;
            textedit_resize(&b->edit, (WIDTH - 16 - 12) / FONT_W,
                            (CONTENT_H - 42) / TEXTEDIT_LINE_H);
            relayout(b);
            clamp_scroll(b);
        }
        if (ev.type == EV_KEY) {
            key_pressed(b, ev.key, ev.mods);
        } else if (ev.type == EV_MOUSE_DOWN) {
            dragging = ev.x >= SCROLL_X - 2 && b->overlay == OVERLAY_NONE;
            mouse_pressed(b, ev.x, ev.y);
        } else if (ev.type == EV_MOUSE_MOVE && dragging) {
            scroll_to_mouse(b, ev.y);
        } else if (ev.type == EV_MOUSE_UP) {
            dragging = false;
        }
    }

    /* Fenster sofort schließen; auf einen noch laufenden Helfer aber warten. */
    b->closing = true;
    window_destroy(b->window);
    while (b->busy)
        task_sleep(100);
    page_free(b->page);
    page_free(b->incoming);
    kfree(b->source);
    kfree(b->incoming_source);
    for (int i = 0; i < b->history_count; i++)
        kfree(b->history[i]);
    while (b->cookies) {
        cookie_t *next = b->cookies->next;
        cookie_free(b->cookies);
        b->cookies = next;
    }
    textedit_free(&b->edit);
    kfree(b);
}
