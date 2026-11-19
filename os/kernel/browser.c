/*
 * Ein einfacher Browser: holt Seiten über http(s), zeigt sie als Text mit
 * Links, Formularen und Bildern (siehe html.h), merkt sich Cookies für die
 * Dauer des Fensters und kann Formulare absenden - genug zum Lesen, Suchen
 * und Anmelden.
 *
 * Bilder (PNG, JPEG, GIF) lädt ein Helfer im Hintergrund nach: Die Seite ist
 * sofort lesbar, die Bilder füllen ihre Plätze, sobald sie da sind.
 *
 * Skripte (JavaScript) führt er aus, wenn der Benutzer das einschaltet
 * (Knopf rechts oben; siehe script.h): Dann gehört zur gezeigten Seite ein
 * veränderbares Abbild, aus dem sie nach jeder Änderung neu gesetzt wird.
 * Klicks, Eingaben und Zeitgeber gehen zuerst an die Skripte der Seite.
 */
#include "apps.h"
#include "console.h"
#include "font.h"
#include "fs.h"
#include "gui.h"
#include "html.h"
#include "http.h"
#include "image.h"
#include "cpu.h"
#include "css.h"
#include "input.h"
#include "json.h"
#include "memory.h"
#include "script.h"
#include "sound.h"
#include "storage.h"
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
#define ROW_H        20
#define STATUS_H     26
#define STATUS_Y     (HEIGHT - STATUS_H)
#define ROWS         ((STATUS_Y - CONTENT_Y - 4) / ROW_H)
#define CONTENT_H    (ROWS * ROW_H)
#define SCROLL_W     10
#define SCROLL_X     (WIDTH - SCROLL_W - 4)
#define LAYOUT_W     (SCROLL_X - CONTENT_X - 6)      /* Breite des Seiteninhalts in Pixeln */
#define COLUMNS      (LAYOUT_W / FONT_W)             /* ... und in Zeichen der Rasterschrift */
#define HISTORY_MAX  40
#define PAGE_MAX     (8u * 1024 * 1024)
#define REDIRECTS    10
#define START_PAGE   "about:start"
#define MARKS_PAGE   "about:lesezeichen"
#define HISTORY_PAGE "about:verlauf"
#define SOURCE_PAGE  "about:quelltext"
#define MARKS_MAX    200                /* Lesezeichen */
#define MARKS_FILE   "lesezeichen.txt"

/* Bilder */
#define PICTURES_MAX     300
#define PICTURE_FILE_MAX (12u * 1024 * 1024)        /* größte Bilddatei */
#define PICTURE_KEEP_W   1600                       /* breiter wird nichts aufbewahrt */
#define PICTURE_MEMORY   (96u * 1024 * 1024)        /* für alle Bilder eines Fensters */
#define PICTURE_ROWS     34                         /* höchstens so viele Zeilen je Bild */
#define PICTURE_WORKERS  2
#define C_PLACEHOLDER    RGB(238, 238, 234)

/* Stylesheets einer Seite */
#define STYLES_MAX       12                         /* so viele Dateien höchstens */
#define STYLE_FILE_MAX   (2u * 1024 * 1024)
#define STYLE_TIME_MS    20000                      /* Geduld für alle zusammen */

/* Skripte */
#define SCRIPT_FILE_MAX  (4u * 1024 * 1024)
#define SETTINGS         "browser.txt"
#define TOGGLE_W         126                        /* Knopf "JavaScript an/aus" */
#define DIALOG_TEXT      600                        /* so viel zeigt eine Meldung höchstens */

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

/* Ein Bild im Vorrat des Fensters; gesucht wird über die Adresse. */
typedef enum { PIC_WAITING, PIC_LOADING, PIC_READY, PIC_BROKEN } pic_state_t;

typedef struct {
    char *url;
    pic_state_t state;
    bool wanted;                        /* die gezeigte Seite braucht es */
    int width, height;                  /* Größe des Bildes selbst */
    image_t *bitmap;                    /* aufbewahrte Fassung (höchstens so groß) */
    image_t *shown;                     /* auf die zuletzt gezeigte Größe gebracht */
} picture_t;

typedef struct {
    window_t *window;
    int width, height;
    bool closing;

    char url[URL_SIZE];                 /* Adresse der gezeigten Seite */
    page_t *page;
    css_sheet_t *css;                   /* ihre Stylesheets (oder NULL) */
    char *source;                       /* ihr Quelltext - zum Neu-Umbrechen bei anderer Breite */
    size_t source_len;
    bool source_html;
    int page_columns;                   /* für diese Breite ist die Seite gesetzt */
    int scroll;
    int focus;                          /* Formularfeld mit der Schreibmarke oder -1 */
    char *history[HISTORY_MAX];         /* besuchte Adressen; die oberste ist die aktuelle */
    int history_count;
    char *forward[HISTORY_MAX];         /* wohin "Vor" führt: von wo "Zurück" kam (das letzte zuerst) */
    int forward_count;
    bool keep_forward;                  /* der nächste Seitenwechsel ist ein Schritt vorwärts */
    int text_size;                      /* Schriftgröße des Fließtexts (Strg + und Strg -) */

    /* Suchen auf der Seite (Strg+F) */
    bool find_active;
    char find_text[80];
    size_t find_len;
    int find_line, find_span, find_offset;  /* der Treffer, bei dem die Suche steht (Zeile -1: keiner) */
    int find_hits, find_number;

    char address[URL_SIZE];             /* Text der Adresszeile */
    size_t address_len;
    bool address_focus;

    /* Laden im Helfer-Task */
    bool busy;
    uint64_t busy_since;
    char job_url[URL_SIZE];
    char *job_body;                     /* Formulardaten für POST oder NULL */
    char *job_text;                     /* Text, den eine eingebaute Seite zeigen soll (Quelltext) */
    bool job_remember;                  /* nach Erfolg in den Verlauf */
    page_t *incoming;
    char incoming_url[URL_SIZE];
    char *incoming_source;
    css_sheet_t *incoming_css;
    size_t incoming_len;
    bool incoming_html;
    int incoming_columns;
    bool incoming_ready;
    cookie_t *cookies;                  /* Zugriff nur mit cookie_busy: Helfer und Skripte teilen sie */
    bool cookie_busy;

    /*
     * Skripte: Das Abbild der gezeigten Seite gehört dem Fenster; das einer
     * Seite, die gerade geladen wird, bis zur Übergabe dem Helfer.
     */
    bool javascript;                    /* Skripte der Seiten ausführen? */
    script_t *script;                   /* zur gezeigten Seite, oder NULL */
    script_t *incoming_script;
    uint32_t incoming_submit;           /* beim Laden wollte ein Skript dieses Formular absenden */
    uint32_t incoming_button;
    bool script_loading;                /* der Helfer führt gerade Skripte aus */
    uint64_t script_next;               /* dann sind die nächsten Zeitgeber fällig */
    uint64_t script_show_after;         /* Änderungen durch Zeitgeber: frühestens dann neu setzen */
    bool field_changed;                 /* im Feld mit der Schreibmarke wurde getippt */
    bool script_reported;               /* "angehalten" ist schon gemeldet */
    bool close_wanted;

    /* Meldung oder Frage eines Skripts (alert, confirm) */
    char *dialog;                       /* ihr Text, oder NULL */
    uint32_t dialog_number;             /* zählt die Meldungen: Das Fenster zeichnet nur bei einer neuen */
    bool dialog_question, dialog_answer;
    bool dialog_quiet;                  /* der Benutzer will von dieser Seite keine mehr sehen */

    char status[200];
    bool status_error;

    /*
     * Bilder: Die Liste teilen sich Fenster und Helfer; jeder Zugriff
     * geschieht bei gesperrten Unterbrechungen (kurz, nie während des Ladens).
     */
    picture_t pictures[PICTURES_MAX];
    int picture_count;
    size_t picture_bytes;
    int picture_workers;
    bool pictures_changed;              /* seit dem letzten Setzen der Seite kam etwas an */
    uint64_t pictures_settle;           /* frühestens dann neu setzen */

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

/*
 * Die Cookies teilen sich der Helfer, der Seiten lädt, und die Skripte der
 * gezeigten Seite (die im Fenster selbst Daten nachladen): Wer sie liest oder
 * ändert, hält so lange diese Sperre.
 */
static void cookies_lock(browser_t *b)
{
    while (__atomic_exchange_n(&b->cookie_busy, true, __ATOMIC_ACQUIRE))
        task_sleep(1);
}

static void cookies_unlock(browser_t *b)
{
    __atomic_store_n(&b->cookie_busy, false, __ATOMIC_RELEASE);
}

static void cookies_store(browser_t *b, const char *url, const char *lines)
{
    char host[128];
    const char *path;
    bool secure;
    if (!lines || !url_split(url, &secure, host, sizeof(host), &path))
        return;
    cookies_lock(b);
    while (*lines) {
        const char *end = lines;
        while (*end && *end != '\n')
            end++;
        cookie_set(b, host, path, secure, lines, (size_t)(end - lines));
        lines = *end ? end + 1 : end;
    }
    cookies_unlock(b);
}

/* Hängt die Kopfzeile "Cookie: ..." für diese Adresse an. */
static void cookies_header(browser_t *b, const char *url, strbuf_t *out)
{
    char host[128];
    const char *path;
    bool secure, first = true;
    if (!url_split(url, &secure, host, sizeof(host), &path))
        return;
    cookies_lock(b);
    for (const cookie_t *c = b->cookies; c; c = c->next) {
        if (!domain_matches(c, host) || !path_matches(c, path) || (c->secure && !secure))
            continue;
        sb_append(out, first ? "Cookie: " : "; ");
        sb_append(out, c->name);
        sb_append(out, "=");
        sb_append(out, c->value);
        first = false;
    }
    cookies_unlock(b);
    if (!first)
        sb_append(out, "\r\n");
}

/* --- Lesezeichen --------------------------------------------------------------------------------- */

/*
 * Die Lesezeichen gelten für alle Browserfenster und stehen auf dem Stick
 * (eine Zeile je Eintrag: Adresse, Tabulator, Titel). Wer sie liest oder
 * ändert, hält so lange die Sperre.
 */
typedef struct {
    char *url, *title;
} bookmark_t;

static bookmark_t marks[MARKS_MAX];
static int mark_count;
static bool marks_loaded, marks_busy;

static void marks_lock(void)
{
    while (__atomic_exchange_n(&marks_busy, true, __ATOMIC_ACQUIRE))
        task_sleep(1);
}

static void marks_unlock(void)
{
    __atomic_store_n(&marks_busy, false, __ATOMIC_RELEASE);
}

static char *copy_text(const char *text, size_t len)
{
    char *copy = kmalloc(len + 1);
    if (copy) {
        memcpy(copy, text, len);
        copy[len] = '\0';
    }
    return copy;
}

/* Hängt ein Lesezeichen an (mit gehaltener Sperre). */
static void mark_append(const char *url, size_t url_len, const char *title, size_t title_len)
{
    if (mark_count >= MARKS_MAX || url_len == 0 || url_len >= URL_SIZE)
        return;
    if (title_len > 120)
        title_len = 120;
    char *u = copy_text(url, url_len), *t = copy_text(title, title_len);
    if (!u || !t) {
        kfree(u);
        kfree(t);
        return;
    }
    for (char *c = t; *c; c++) {
        if ((unsigned char)*c < 32)
            *c = ' ';
    }
    marks[mark_count].url = u;
    marks[mark_count].title = t;
    mark_count++;
}

static void marks_load(void)
{
    char *text;
    size_t size;
    marks_lock();
    if (!marks_loaded && storage_available()) {
        marks_loaded = true;
        if (storage_load(STORAGE_SETTINGS, MARKS_FILE, 256 * 1024, &text, &size)) {
            for (size_t at = 0; at < size;) {
                size_t end = at, tab = at;
                while (end < size && text[end] != '\n')
                    end++;
                while (tab < end && text[tab] != '\t')
                    tab++;
                size_t stop = end > at && text[end - 1] == '\r' ? end - 1 : end;
                if (tab < stop)
                    mark_append(text + at, tab - at, text + tab + 1, stop - tab - 1);
                else if (stop > at)
                    mark_append(text + at, stop - at, "", 0);
                at = end + 1;
            }
            kfree(text);
        }
    }
    marks_unlock();
}

/* Schreibt die Lesezeichen auf den Stick (mit gehaltener Sperre). */
static void marks_save(void)
{
    strbuf_t out = { 0 };
    for (int i = 0; i < mark_count; i++) {
        sb_append(&out, marks[i].url);
        sb_append(&out, "\t");
        sb_append(&out, marks[i].title);
        sb_append(&out, "\n");
    }
    if (!out.failed)
        storage_save(STORAGE_SETTINGS, MARKS_FILE, out.data ? out.data : "", out.data ? out.len : 0);
    sb_free(&out);
}

/* Nummer des Lesezeichens für diese Adresse oder -1 (mit gehaltener Sperre). */
static int mark_find(const char *url)
{
    for (int i = 0; i < mark_count; i++) {
        if (strcmp(marks[i].url, url) == 0)
            return i;
    }
    return -1;
}

static void mark_remove(int index)
{
    if (index < 0 || index >= mark_count)
        return;
    kfree(marks[index].url);
    kfree(marks[index].title);
    memmove(&marks[index], &marks[index + 1], (size_t)(mark_count - index - 1) * sizeof(bookmark_t));
    mark_count--;
}

/* Hängt Text an, in dem Zeichen mit Bedeutung in HTML entschärft sind. */
static void sb_append_html(strbuf_t *out, const char *text)
{
    for (const char *p = text; *p; p++) {
        if (*p == '<')
            sb_append(out, "&lt;");
        else if (*p == '>')
            sb_append(out, "&gt;");
        else if (*p == '&')
            sb_append(out, "&amp;");
        else if (*p == '"')
            sb_append(out, "&quot;");
        else
            sb_append_n(out, p, 1);
    }
}

static void marks_list(strbuf_t *out, bool removable)
{
    marks_lock();
    if (mark_count == 0)
        sb_append(out, "<p>Noch keine. Der Stern oben rechts (oder Strg+D) merkt sich die gezeigte Seite.</p>");
    else
        sb_append(out, "<ul>");
    for (int i = 0; i < mark_count; i++) {
        char number[16];
        ksnprintf(number, sizeof(number), "%d", i);
        sb_append(out, "<li><a href=\"");
        sb_append_html(out, marks[i].url);
        sb_append(out, "\">");
        sb_append_html(out, marks[i].title[0] ? marks[i].title : marks[i].url);
        sb_append(out, "</a>");
        if (removable) {
            sb_append(out, " &ndash; <a href=\"" MARKS_PAGE "?weg=");
            sb_append(out, number);
            sb_append(out, "\">entfernen</a>");
        }
        sb_append(out, "</li>");
    }
    if (mark_count > 0)
        sb_append(out, "</ul>");
    marks_unlock();
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
    "<ul><li>Seiten mit Schriften, Farben, Bildern, Tabellen, Spalten und Kästen neben dem "
    "Text anzeigen</li>"
    "<li>Formulare ausfüllen und absenden, auch Anmeldungen (Cookies bleiben, "
    "solange das Fenster offen ist)</li>"
    "<li>Verschlüsselte Verbindungen (https) mit Prüfung des Servers</li>"
    "<li>Dateien, die keine Webseiten sind, bei den eigenen Dateien speichern</li>"
    "<li>JavaScript ausführen, wenn du es einschaltest (Knopf oben rechts) - "
    "einfache und ältere Seiten laufen damit, große Web-Anwendungen noch nicht</li></ul>"
    "<h2>Was er nicht kann</h2>"
    "<ul><li>Videos, Ton und gezeichnete Flächen (Canvas)</li>"
    "<li>Skripte mit den neuesten Sprachmitteln (async/await, Module) und große "
    "Web-Anwendungen</li><li>Abstände, Rahmen und frei platzierte Elemente einer Seite</li>"
    "<li>Dateien hochladen</li></ul>"
    "<h2>Bedienung</h2>"
    "<ul><li>Pfeiltasten, Bild auf/ab, Pos1/Ende: blättern</li>"
    "<li>Tab: zum nächsten Formularfeld; Enter im Feld: absenden</li>"
    "<li>Rücktaste oder Alt+Pfeil links: zurück; Alt+Pfeil rechts: vor; Strg+R: neu laden</li>"
    "<li>Strg+F: auf der Seite suchen; Strg+D: Lesezeichen setzen oder entfernen; "
    "Strg+B: Lesezeichen zeigen</li>"
    "<li>Strg und +, - oder 0: Schrift größer, kleiner, wie üblich; Strg+U: Quelltext der Seite</li></ul>";

/*
 * Eingebaute Seiten: Start, Lesezeichen, Verlauf. Liefert ihren Quelltext
 * (kmalloc) oder NULL, wenn es die Seite nicht gibt.
 */
static char *internal_page(browser_t *b, const char *url, size_t *len)
{
    strbuf_t out = { 0 };
    if (strcmp(url, START_PAGE) == 0) {
        sb_append(&out, start_page);
        sb_append(&out, "<h2>Lesezeichen</h2>");
        marks_list(&out, false);
        sb_append(&out, "<p><a href=\"" MARKS_PAGE "\">Lesezeichen verwalten</a> &ndash; "
                        "<a href=\"" HISTORY_PAGE "\">Verlauf dieses Fensters</a></p>");
    } else if (starts_with(url, MARKS_PAGE)) {
        const char *weg = url + sizeof(MARKS_PAGE) - 1;
        uint32_t index;
        if (starts_with(weg, "?weg=") && parse_uint(weg + 5, &index)) {
            marks_lock();
            mark_remove((int)index);
            marks_save();
            marks_unlock();
        }
        sb_append(&out, "<title>Lesezeichen</title><h1>Lesezeichen</h1>");
        marks_list(&out, true);
        sb_append(&out, "<p><a href=\"" START_PAGE "\">Startseite</a> &ndash; "
                        "<a href=\"" HISTORY_PAGE "\">Verlauf dieses Fensters</a></p>");
    } else if (strcmp(url, HISTORY_PAGE) == 0) {
        sb_append(&out, "<title>Verlauf</title><h1>Verlauf dieses Fensters</h1>");
        if (b->history_count == 0)
            sb_append(&out, "<p>Noch nichts besucht.</p>");
        else
            sb_append(&out, "<p>Die zuletzt besuchte Seite steht oben. Der Verlauf gilt, "
                            "solange das Fenster offen ist.</p><ul>");
        for (int i = b->history_count - 1; i >= 0; i--) {
            if (starts_with(b->history[i], "about:"))
                continue;
            sb_append(&out, "<li><a href=\"");
            sb_append_html(&out, b->history[i]);
            sb_append(&out, "\">");
            sb_append_html(&out, b->history[i]);
            sb_append(&out, "</a></li>");
        }
        if (b->history_count > 0)
            sb_append(&out, "</ul>");
        sb_append(&out, "<p><a href=\"" START_PAGE "\">Startseite</a></p>");
    } else {
        return NULL;
    }
    if (out.failed || !out.data) {
        sb_free(&out);
        return NULL;
    }
    *len = out.len;
    return out.data;
}

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
/* --- Bilder ------------------------------------------------------------------------------------ */

/* Sucht ein Bild im Vorrat. Nur bei gesperrten Unterbrechungen aufrufen. */
static picture_t *picture_find(browser_t *b, const char *url)
{
    for (int i = 0; i < b->picture_count; i++) {
        if (strcmp(b->pictures[i].url, url) == 0)
            return &b->pictures[i];
    }
    return NULL;
}

/* Auskunft für den Seitenaufbau: Ist das Bild da, und wie groß ist es? */
static picture_state_t picture_lookup(void *ctx, const char *url, int *width, int *height)
{
    browser_t *b = ctx;
    picture_state_t result = PICTURE_UNKNOWN;
    uint64_t flags = irq_save();
    const picture_t *pic = picture_find(b, url);
    if (pic && pic->state == PIC_READY) {
        *width = pic->width;
        *height = pic->height;
        result = PICTURE_READY;
    } else if (pic && pic->state == PIC_BROKEN) {
        result = PICTURE_BROKEN;
    }
    irq_restore(flags);
    return result;
}

static html_pictures_t picture_source(browser_t *b)
{
    return (html_pictures_t){
        .cell_width = 1, .line_height = ROW_H, .max_rows = PICTURE_ROWS,
        .lookup = picture_lookup, .ctx = b,
    };
}

/* --- Schrift der Seite ---------------------------------------------------------------------- */

#define TEXT_SIZE 15                    /* Fließtext */

static int text_flags(uint8_t style)
{
    return ((style & STYLE_BOLD) ? FONT_BOLD : 0) | ((style & STYLE_ITALIC) ? FONT_ITALIC : 0);
}

/* Wird dieses Stück in der Rasterschrift gezeichnet (Code, vorformatierter Text)? */
static bool text_is_grid(uint8_t style)
{
    return (style & STYLE_MONO) || !font_available();
}

/* Auskunft für den Seitenaufbau: wie breit ein Stück Text wird */
static int page_text_width(void *ctx, const char *text, size_t len, uint8_t style,
                           uint8_t size)
{
    (void)ctx;
    if (text_is_grid(style)) {
        int count = 0;
        for (size_t i = 0; i < len; i++) {
            if (((uint8_t)text[i] & 0xC0) != 0x80)
                count++;
        }
        return count * FONT_W;
    }
    return font_width(text, len, size ? size : TEXT_SIZE, text_flags(style));
}

/* Setzt eine Seite für die jetzige Fensterbreite. */
static page_t *layout_page(browser_t *b, const char *source, size_t len, const char *url,
                           bool html, const css_sheet_t *css, bool scripts)
{
    if (!html)
        return html_plain_text(source, len, COLUMNS);
    html_pictures_t pictures = picture_source(b);
    html_text_t text = {
        .cell_width = FONT_W, .width = page_text_width,
        .base_size = (uint8_t)b->text_size,
        .heading_size = { (uint8_t)(26 * b->text_size / TEXT_SIZE), (uint8_t)(21 * b->text_size / TEXT_SIZE),
                          (uint8_t)(17 * b->text_size / TEXT_SIZE) },
        .text_color = C_TEXT, .link_color = C_LINK, .page_color = C_BACKGROUND,
        .css = css, .scripts = scripts,
    };
    return html_layout_rich(source, len, url, LAYOUT_W, &pictures, &text);
}

/* --- Skripte: Einstellung, Dateien, Rückfragen ---------------------------------------------- */

static bool javascript_wanted;          /* gilt für alle Browserfenster; steht auf dem Stick */
static bool settings_loaded;

static void load_settings(void)
{
    char *text;
    size_t size;
    if (settings_loaded || !storage_available())
        return;
    settings_loaded = true;
    if (!storage_load(STORAGE_SETTINGS, SETTINGS, 64, &text, &size))
        return;
    javascript_wanted = mentions(text, size, "javascript=1");
    kfree(text);
}

static void save_settings(void)
{
    const char *text = javascript_wanted ? "javascript=1\n" : "javascript=0\n";
    storage_save(STORAGE_SETTINGS, SETTINGS, text, strlen(text));
}

/* Holt eine Skriptdatei der Seite (nur im Helfer, während die Seite lädt). */
static char *script_fetch(void *ctx, const char *url, size_t *len)
{
    browser_t *b = ctx;
    http_response_t response;
    strbuf_t headers = { 0 };
    if (b->closing)
        return NULL;
    cookies_header(b, url, &headers);
    sb_append(&headers, "Accept: */*\r\n");
    http_options_t options = {
        .method = "GET",
        .headers = headers.data,
        .max_body = SCRIPT_FILE_MAX,
        .timeout_ms = 10000,
    };
    bool ok = !headers.failed && http_request(url, &options, &response);
    if (headers.data)
        memset(headers.data, 0, headers.len);
    sb_free(&headers);
    if (!ok)
        return NULL;
    if (response.status != 200 || !response.body) {
        kfree(response.body);
        return NULL;
    }
    *len = response.length;
    return response.body;
}

static void draw(browser_t *b);

/* Beginnen zwei Adressen mit demselben "schema://rechner[:port]"? */
static bool same_server(const char *a, const char *b)
{
    int slashes = 0;
    for (size_t i = 0; a[i] && b[i]; i++) {
        if (lower(a[i]) != lower(b[i]))
            return false;
        if (a[i] == '/' && ++slashes == 3)
            return true;
        if (slashes == 2 && (a[i + 1] == '\0' || a[i + 1] == '?' || a[i + 1] == '#'))
            return b[i + 1] == '\0' || b[i + 1] == '/' || b[i + 1] == '?' || b[i + 1] == '#';
        if (slashes == 2 && (b[i + 1] == '\0' || b[i + 1] == '?' || b[i + 1] == '#'))
            return a[i + 1] == '/';
    }
    return false;
}

/*
 * Ein Skript lädt Daten nach oder sendet welche (XMLHttpRequest, fetch) - nur
 * an den Server seiner Seite, auch über Weiterleitungen hinweg. Die Cookies
 * der Seite gehen mit. Währenddessen wartet das Skript - und, wenn es im
 * Fenster läuft, auch das Fenster.
 */
static bool script_request(void *ctx, const char *page, const char *method, const char *url,
                           const char *headers, const char *body, size_t body_len,
                           script_reply_t *reply)
{
    browser_t *b = ctx;
    char *at = kmalloc(URL_SIZE), *next = kmalloc(URL_SIZE);
    http_extra_t *extra = kmalloc(sizeof(*extra));
    bool post = strcmp(method, "POST") == 0, done = false;

    if (at && next && extra && !b->closing) {
        if (!b->script_loading) {       /* im Fenster: sagen, worauf gewartet wird */
            set_status(b, false, "Die Seite lädt Daten nach ...");
            draw(b);
        }
        strlcpy(at, url, URL_SIZE);
        for (int hop = 0; hop < 6 && same_server(page, at); hop++) {
            http_response_t response;
            strbuf_t lines = { 0 };
            cookies_header(b, at, &lines);
            sb_append(&lines, "Accept-Language: de,en;q=0.7\r\n");
            if (headers)
                sb_append(&lines, headers);
            http_options_t options = {
                .method = post ? "POST" : "GET",
                .headers = lines.data,
                .body = post ? body : NULL,
                .body_len = post ? body_len : 0,
                .max_body = SCRIPT_FILE_MAX,
                .timeout_ms = 15000,
                .extra = extra,
            };
            bool ok = !lines.failed && http_request(at, &options, &response);
            if (lines.data)
                memset(lines.data, 0, lines.len);       /* enthält Anmelde-Cookies */
            sb_free(&lines);
            if (!ok) {
                kfree(extra->cookies);
                extra->cookies = NULL;
                break;
            }
            cookies_store(b, at, extra->cookies);
            kfree(extra->cookies);
            extra->cookies = NULL;
            bool redirect = (response.status >= 301 && response.status <= 303) ||
                            response.status == 307 || response.status == 308;
            if (redirect && extra->location[0] && url_resolve(at, extra->location, next, URL_SIZE)) {
                kfree(response.body);
                if (response.status != 307 && response.status != 308)
                    post = false;
                strlcpy(at, next, URL_SIZE);
                continue;
            }
            reply->status = response.status;
            reply->body = response.body;
            reply->length = response.length;
            strlcpy(reply->content_type, response.content_type, sizeof(reply->content_type));
            done = true;
            break;
        }
        if (!b->script_loading)
            set_status(b, false, "");
    }
    kfree(at);
    kfree(next);
    kfree(extra);
    return done;
}

static bool script_cancelled(void *ctx)
{
    const browser_t *b = ctx;
    return b->closing;
}

static void dialog_loop(browser_t *b);

/* alert und confirm: zeigt den Text und wartet auf die Antwort des Benutzers. */
static bool script_ask(browser_t *b, const char *text, bool question)
{
    if (b->closing || b->dialog_quiet || b->dialog)
        return false;
    size_t n = strlen(text);
    if (n > DIALOG_TEXT) {
        n = DIALOG_TEXT;
        while (n > 0 && ((uint8_t)text[n] & 0xC0) == 0x80)
            n--;                        /* nicht mitten in einem Zeichen abschneiden */
    }
    char *copy = kmalloc(n + 1);
    if (!copy)
        return false;
    memcpy(copy, text, n);
    copy[n] = '\0';
    b->dialog_question = question;
    b->dialog_answer = false;
    b->dialog_number++;
    b->dialog = copy;
    sound_play(SOUND_NOTIFY);
    if (b->script_loading) {
        while (b->dialog && !b->closing)        /* das Fenster zeigt die Frage und antwortet */
            task_sleep(30);
    } else {
        dialog_loop(b);
    }
    return b->dialog_answer;
}

static void script_alert(void *ctx, const char *text)
{
    script_ask(ctx, text, false);
}

static bool script_confirm(void *ctx, const char *text)
{
    return script_ask(ctx, text, true);
}

static script_host_t script_host(browser_t *b)
{
    script_host_t host = {
        .fetch = script_fetch, .request = script_request, .alert = script_alert,
        .confirm = script_confirm,
        .cancelled = script_cancelled, .ctx = b,
        .width = LAYOUT_W, .height = CONTENT_H,
    };
    return host;
}

/* --- Stylesheets der Seite laden -------------------------------------------------------------- */

typedef struct {
    browser_t *b;
    css_sheet_t *css;
    int files;
    uint64_t deadline;
} styles_ctx_t;

static void style_found(void *ctx, bool linked, const char *text, size_t len)
{
    styles_ctx_t *c = ctx;
    if (!linked) {
        css_add(c->css, text, len);
        return;
    }
    if (c->files >= STYLES_MAX || time_ms() >= c->deadline || c->b->closing)
        return;
    c->files++;
    http_response_t response;
    http_options_t options = {
        .method = "GET",
        .headers = "Accept: text/css,*/*;q=0.1\r\n",
        .max_body = STYLE_FILE_MAX,
        .timeout_ms = 10000,
    };
    if (!http_request(text, &options, &response))
        return;
    if (response.status == 200 && response.body)
        css_add(c->css, response.body, response.length);
    kfree(response.body);
}

/*
 * Kennzahl der Stylesheets einer Seite: Ändert sie sich, haben die Skripte
 * Stylesheets ergänzt oder entfernt, und die Gestaltung wird neu geladen.
 */
static void style_mark(void *ctx, bool linked, const char *text, size_t len)
{
    uint64_t *mark = ctx;
    *mark = (*mark ^ (linked ? 0x9E : 0x37)) * 1099511628211ull;
    for (size_t i = 0; i < len; i++)
        *mark = (*mark ^ (uint8_t)text[i]) * 1099511628211ull;
}

static uint64_t styles_mark(const char *html, size_t len, const char *url)
{
    uint64_t mark = 14695981039346656037ull;
    html_find_styles(html, len, url, style_mark, &mark);
    return mark;
}

/* Sammelt die Stylesheets einer Seite ein; NULL bei Speichermangel. */
static css_sheet_t *load_styles(browser_t *b, const char *html, size_t len, const char *url)
{
    css_sheet_t *css = css_create(LAYOUT_W);
    if (!css)
        return NULL;
    styles_ctx_t ctx = { b, css, 0, time_ms() + STYLE_TIME_MS };
    html_find_styles(html, len, url, style_found, &ctx);
    return css;
}

static void picture_release(browser_t *b, picture_t *pic)
{
    if (pic->bitmap)
        b->picture_bytes -= (size_t)pic->bitmap->width * (size_t)pic->bitmap->height * 4;
    image_free(pic->bitmap);
    image_free(pic->shown);
    kfree(pic->url);
}

/*
 * Gleicht den Vorrat mit der gezeigten Seite ab: Was sie braucht, wird
 * vorgemerkt; was keine Seite mehr braucht, verschwindet.
 */
static void pictures_sync(browser_t *b, bool keep)
{
    const page_t *page = b->page;
    /* keep: Die Seite hat sich nur geändert - Bilder, die gerade fehlen, kommen oft wieder. */
    picture_t *doomed = keep && b->picture_count < PICTURES_MAX * 3 / 4
                            ? NULL : kmalloc(PICTURES_MAX * sizeof(picture_t));
    int doomed_count = 0;

    uint64_t flags = irq_save();
    for (int i = 0; i < b->picture_count; i++)
        b->pictures[i].wanted = false;
    irq_restore(flags);

    for (int i = 0; page && i < page->image_count; i++) {
        const char *url = page->images[i].url;
        size_t size = strlen(url) + 1;
        char *copy = kmalloc(size);     /* außerhalb der Sperre besorgen */
        if (!copy)
            break;
        memcpy(copy, url, size);

        flags = irq_save();
        picture_t *pic = picture_find(b, url);
        if (!pic && b->picture_count < PICTURES_MAX) {
            pic = &b->pictures[b->picture_count++];
            memset(pic, 0, sizeof(*pic));
            pic->url = copy;
            pic->state = PIC_WAITING;
            copy = NULL;
        }
        if (pic)
            pic->wanted = true;
        irq_restore(flags);
        kfree(copy);
    }

    /* Aussortieren; was gerade geladen wird, bleibt bis zum nächsten Mal */
    flags = irq_save();
    int kept = 0;
    for (int i = 0; i < b->picture_count; i++) {
        picture_t *pic = &b->pictures[i];
        if (!pic->wanted && pic->state != PIC_LOADING && doomed) {
            if (pic->bitmap)
                b->picture_bytes -= (size_t)pic->bitmap->width * (size_t)pic->bitmap->height * 4;
            doomed[doomed_count++] = *pic;
        } else {
            b->pictures[kept++] = *pic;
        }
    }
    b->picture_count = kept;
    irq_restore(flags);

    for (int i = 0; i < doomed_count; i++) {
        image_free(doomed[i].bitmap);
        image_free(doomed[i].shown);
        kfree(doomed[i].url);
    }
    kfree(doomed);
}

/* Holt ein Bild und bringt es in die Form, in der es aufbewahrt wird. */
static image_t *picture_fetch(const char *url, int *width, int *height)
{
    http_response_t response;
    http_options_t options = {
        .method = "GET",
        .headers = "Accept: image/png,image/jpeg,image/gif;q=0.9,*/*;q=0.1\r\n",
        .max_body = PICTURE_FILE_MAX,
        .timeout_ms = 15000,
    };
    if (!http_request(url, &options, &response))
        return NULL;
    image_t *result = NULL;
    if (response.status == 200 && response.body) {
        const char *error;
        image_t *image = image_decode((const uint8_t *)response.body, response.length, &error);
        kfree(response.body);           /* die Datei wird nicht mehr gebraucht */
        response.body = NULL;
        if (image) {
            *width = image->width;
            *height = image->height;
            int keep_w = image->width, keep_h = image->height;
            if (keep_w > PICTURE_KEEP_W) {
                keep_h = (int)((int64_t)keep_h * PICTURE_KEEP_W / keep_w);
                keep_w = PICTURE_KEEP_W;
                if (keep_h < 1)
                    keep_h = 1;
            }
            /* verkleinert und mit dem Seitenhintergrund unterlegt aufbewahren */
            result = image_scale(image, keep_w, keep_h, C_BACKGROUND);
            image_free(image);
        }
    }
    kfree(response.body);
    return result;
}

/* Helfer-Task: arbeitet die vorgemerkten Bilder ab. */
static void picture_worker(void *arg)
{
    browser_t *b = arg;
    char *url = kmalloc(URL_SIZE);

    while (url && !b->closing) {
        bool found = false;
        uint64_t flags = irq_save();
        for (int i = 0; i < b->picture_count && !found; i++) {
            picture_t *pic = &b->pictures[i];
            if (pic->state == PIC_WAITING && pic->wanted) {
                pic->state = PIC_LOADING;
                strlcpy(url, pic->url, URL_SIZE);
                found = true;
            }
        }
        irq_restore(flags);
        if (!found)
            break;

        int width = 0, height = 0;
        image_t *bitmap = picture_fetch(url, &width, &height);
        size_t bytes = bitmap ? (size_t)bitmap->width * (size_t)bitmap->height * 4 : 0;

        flags = irq_save();
        picture_t *pic = picture_find(b, url);
        if (pic && pic->state == PIC_LOADING) {
            if (bitmap && b->picture_bytes + bytes <= PICTURE_MEMORY) {
                pic->bitmap = bitmap;
                pic->width = width;
                pic->height = height;
                pic->state = PIC_READY;
                b->picture_bytes += bytes;
                bitmap = NULL;
            } else {
                pic->state = PIC_BROKEN;
            }
            b->pictures_changed = true;
        }
        irq_restore(flags);
        image_free(bitmap);             /* nicht mehr gefragt oder kein Platz */
    }
    kfree(url);

    uint64_t flags = irq_save();
    b->picture_workers--;
    irq_restore(flags);
}

/* Startet Helfer, solange Bilder warten. Liefert, wie viele noch ausstehen. */
static int pictures_pending(browser_t *b, bool start)
{
    int waiting = 0, loading = 0;
    uint64_t flags = irq_save();
    for (int i = 0; i < b->picture_count; i++) {
        if (!b->pictures[i].wanted)
            continue;
        if (b->pictures[i].state == PIC_WAITING)
            waiting++;
        else if (b->pictures[i].state == PIC_LOADING)
            loading++;
    }
    int workers = b->picture_workers;
    irq_restore(flags);

    while (start && waiting > 0 && workers < PICTURE_WORKERS && workers < waiting + loading) {
        flags = irq_save();
        b->picture_workers++;
        irq_restore(flags);
        if (task_create("browser-bild", picture_worker, b, NULL) < 0) {
            flags = irq_save();
            b->picture_workers--;
            irq_restore(flags);
            break;
        }
        workers++;
    }
    return waiting + loading;
}

static void deliver(browser_t *b, page_t *page, const char *url, char *source,
                    size_t len, bool html, int columns, css_sheet_t *css, script_t *script)
{
    while (b->incoming_ready && !b->closing)
        task_sleep(20);
    kfree(b->incoming_source);
    css_free(b->incoming_css);
    script_destroy(b->incoming_script);
    b->incoming_script = script;
    b->incoming_css = css;
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
    char *url = kmalloc(URL_SIZE), *next = kmalloc(URL_SIZE), *wish = kmalloc(URL_SIZE);
    http_extra_t *extra = kmalloc(sizeof(*extra));
    char *body = b->job_body;
    bool post = body != NULL;

    b->job_body = NULL;
    b->incoming_submit = 0;
    if (!url || !next || !wish || !extra) {
        set_status(b, true, "Nicht genug Speicher");
        goto done;
    }
    strlcpy(url, b->job_url, URL_SIZE);

    if (starts_with(url, "about:")) {
        /* eingebaute Seiten - und der Quelltext der gezeigten Seite als reiner Text */
        int columns = LAYOUT_W;
        size_t len = 0;
        bool source = strcmp(url, SOURCE_PAGE) == 0 && b->job_text;
        char *text = source ? b->job_text : internal_page(b, url, &len);
        if (source) {
            b->job_text = NULL;
            len = strlen(text);
        }
        /* "?weg=..." war ein Auftrag, keine Adresse: Die Seite heißt danach wieder wie üblich */
        if (starts_with(url, MARKS_PAGE))
            strlcpy(url, MARKS_PAGE, URL_SIZE);
        page_t *page = text ? layout_page(b, text, len, url, !source, NULL, false) : NULL;
        if (page) {
            deliver(b, page, url, text, len, !source, columns, NULL, NULL);
            set_status(b, false, "");
        } else {
            set_status(b, true, text ? "Nicht genug Speicher" : "Diese eingebaute Seite gibt es nicht");
            kfree(text);
        }
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
        int columns = LAYOUT_W;
        css_sheet_t *css = NULL;
        script_t *script = NULL;
        bool wanted = html && b->javascript, away = false;
        if (wanted) {
            /*
             * Die Skripte der Seite laufen, bevor sie gezeigt wird: Was sie
             * dabei am Abbild ändern, ist die Seite. Angezeigt wird danach
             * der Quelltext, den das Abbild liefert.
             */
            script_host_t sh = script_host(b);
            script = script_create(content, length, url, &sh);
        }
        if (script) {
            /*
             * Die Gestaltung zuerst: Skripte fragen danach, was zu sehen ist.
             * Gelesen wird sie aus dem Abbild - dort fehlt schon, was nur
             * für Browser ohne Skripte gedacht ist (<noscript>).
             */
            size_t early_len = 0;
            char *early = script_html(script, &early_len);
            uint64_t mark = 0;
            if (early) {
                set_status(b, false, "Lade Gestaltung von %s ...", host);
                css = load_styles(b, early, early_len, url);
                mark = styles_mark(early, early_len, url);
                kfree(early);
            }
            script_set_styles(script, css);
            set_status(b, false, "Führe die Skripte von %s aus ...", host);
            b->script_loading = true;
            script_boot(script);
            b->script_loading = false;
            uint32_t node, button;
            for (;;) {
                script_wish_t kind = script_wish(script, wish, URL_SIZE, &node, &button);
                if (kind == SCRIPT_NOTHING)
                    break;
                if (kind == SCRIPT_SUBMIT) {
                    b->incoming_submit = node;
                    b->incoming_button = button;
                } else if (kind == SCRIPT_NAVIGATE && wish[0] != '#' &&
                           url_resolve(url, wish, next, URL_SIZE) && strcmp(next, url) != 0) {
                    away = true;        /* das Skript schickt uns gleich weiter */
                }
            }
            size_t out_len = 0;
            char *out = away || b->closing ? NULL : script_html(script, &out_len);
            if (out) {
                kfree(content);
                content = out;
                length = out_len;
                if (css && styles_mark(content, length, url) != mark) {
                    /* die Skripte haben an den Stylesheets gedreht: neu einlesen */
                    script_set_styles(script, NULL);
                    css_free(css);
                    css = NULL;
                }
            } else {
                script_destroy(script);
                script = NULL;
                css_free(css);
                css = NULL;
            }
        }
        if (away) {
            kfree(content);
            post = false;
            strlcpy(url, next, URL_SIZE);
            continue;
        }
        if (html && !css) {
            set_status(b, false, "Lade Gestaltung von %s ...", host);
            css = load_styles(b, content, length, url);
            script_set_styles(script, css);
        }
        page_t *page = layout_page(b, content, length, url, html, css, script != NULL);
        if (!page) {
            kfree(content);
            css_free(css);
            script_destroy(script);
            set_status(b, true, "Nicht genug Speicher für diese Seite");
            break;
        }
        int ran, failed, skipped;
        script_counts(script, &ran, &failed, &skipped);
        if (response.status >= 400)
            set_status(b, true, "Der Server meldet einen Fehler (Status %d)", response.status);
        else if (page->failed)
            set_status(b, true, "Die Seite ist zu groß - sie wird unvollständig gezeigt");
        else if (wanted && !script)
            set_status(b, true, "Die Skripte dieser Seite ließen sich nicht starten - "
                       "sie wird ohne sie gezeigt");
        else if (script && script_stopped(script))
            set_status(b, true, "Ein Skript dieser Seite wurde angehalten (zu lange gerechnet "
                       "oder zu viel Speicher) - gezeigt wird, was bis dahin entstanden ist");
        else if (failed + skipped > 0)
            set_status(b, false, "Geladen - von %d Skripten der Seite liefen %d nicht "
                       "(Fehler: %d, hier nicht möglich: %d)", ran + skipped, failed + skipped,
                       failed, skipped);
        else
            set_status(b, false, "");
        deliver(b, page, url, content, length, html, columns, css, script);
        break;
    }

done:
    if (body) {
        memset(body, 0, strlen(body));  /* kann ein Passwort enthalten */
        kfree(body);
    }
    kfree(b->job_text);
    b->job_text = NULL;
    kfree(url);
    kfree(next);
    kfree(wish);
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
    /* Ein neuer Weg: Was vor uns lag, gilt nicht mehr - außer dies ist selbst der Schritt nach vorn */
    if (remember && !b->keep_forward) {
        while (b->forward_count > 0)
            kfree(b->forward[--b->forward_count]);
    }
    b->keep_forward = false;
    b->find_active = false;
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

/* Zählt die Textzeilen vor einer Zeile (Bildzeilen zählen nicht mit). */
static int text_lines_before(const page_t *page, int line)
{
    int count = 0;
    for (int i = 0; i < line && i < page->line_count; i++) {
        if (page->lines[i].image < 0)
            count++;
    }
    return count;
}

/*
 * Setzt die gezeigte Seite neu: für eine andere Fensterbreite oder (force)
 * weil Bilder angekommen sind. Was der Benutzer in Formularfelder eingetragen
 * hat, wandert in die neue Fassung mit, und er bleibt an derselben Stelle.
 */
static void relayout(browser_t *b, bool force)
{
    page_t *old = b->page;
    if (!old || !b->source || (b->page_columns == LAYOUT_W && !force))
        return;
    page_t *fresh = layout_page(b, b->source, b->source_len, b->url, b->source_html, b->css,
                                b->script != NULL);
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

    if (b->page_columns == LAYOUT_W && b->scroll < old->line_count) {
        /*
         * Gleiche Breite: Der Text ist derselbe geblieben, nur Bilder haben
         * ihre Größe geändert. Die oberste sichtbare Zeile wiederfinden.
         */
        const line_t *top = &old->lines[b->scroll];
        if (top->image >= 0 && top->image < fresh->image_count) {
            const page_image_t *image = &fresh->images[top->image];
            int row = top->image_row < image->rows ? top->image_row : image->rows - 1;
            b->scroll = image->line + (row > 0 ? row : 0);
        } else {
            int wanted = text_lines_before(old, b->scroll), seen = 0, line = 0;
            for (; line < fresh->line_count; line++) {
                if (fresh->lines[line].image >= 0)
                    continue;
                if (seen == wanted)
                    break;
                seen++;
            }
            b->scroll = line;
        }
    } else {
        b->scroll = old->line_count > 0 ? b->scroll * fresh->line_count / old->line_count : 0;
    }
    b->page = fresh;
    b->page_columns = LAYOUT_W;
    page_free(old);
    clamp_scroll(b);
}

/* --- Skripte: die geänderte Seite zeigen ---------------------------------------------------- */

static int widget_by_node(const page_t *page, uint32_t node)
{
    for (int i = 0; node && i < page->widget_count; i++) {
        if (page->widgets[i].node == node)
            return i;
    }
    return -1;
}

static int form_by_node(const page_t *page, uint32_t node)
{
    for (int i = 0; node && i < page->form_count; i++) {
        if (page->forms[i].node == node)
            return i;
    }
    return -1;
}

/* Die erste Zeile, in der etwas vom Element steht, oder -1. */
static int first_line_of(const page_t *page, uint32_t node)
{
    for (int line = 0; line < page->line_count; line++) {
        const line_t *l = &page->lines[line];
        for (uint32_t i = 0; i < l->count; i++) {
            if (page->spans[l->first + i].node == node)
                return line;
        }
    }
    return -1;
}

/* Wo steht in der neu gesetzten Seite, was bisher ganz oben zu sehen war? */
static int same_place(const page_t *old, const page_t *fresh, int scroll)
{
    if (scroll <= 0)
        return 0;
    for (int line = scroll; line < old->line_count && line < scroll + 30; line++) {
        const line_t *l = &old->lines[line];
        uint32_t node = 0;
        for (uint32_t i = 0; i < l->count && !node; i++)
            node = old->spans[l->first + i].node;
        if (!node)
            continue;
        int before = first_line_of(old, node), after = first_line_of(fresh, node);
        if (after >= 0)
            return after + (scroll - before);
    }
    return scroll;
}

/*
 * Die Skripte haben das Abbild der Seite geändert: Sie wird daraus neu
 * gesetzt. Der Benutzer bleibt an derselben Stelle, und die Schreibmarke
 * bleibt in ihrem Feld - beides findet sich über die Nummern der Elemente.
 */
static void script_show(browser_t *b)
{
    page_t *old = b->page;
    if (!b->script || !old || !script_changed(b->script))
        return;
    uint64_t start = time_ms();
    size_t len = 0;
    char *source = script_html(b->script, &len);
    page_t *fresh = source ? layout_page(b, source, len, b->url, true, b->css, true) : NULL;
    if (!fresh) {
        kfree(source);
        return;
    }

    uint32_t focus = b->focus >= 0 && b->focus < old->widget_count ? old->widgets[b->focus].node : 0;
    b->focus = widget_by_node(fresh, focus);
    if (b->focus < 0)
        b->field_changed = false;
    if (b->overlay != OVERLAY_NONE) {
        const widget_t *was = &old->widgets[b->overlay_widget];
        int index = widget_by_node(fresh, was->node);
        if (index < 0 || fresh->widgets[index].kind != was->kind ||
            (b->overlay == OVERLAY_PICKER && fresh->widgets[index].option_count == 0)) {
            b->overlay = OVERLAY_NONE;
        } else {
            b->overlay_widget = index;
            if (b->picker_selected >= fresh->widgets[index].option_count)
                b->picker_selected = fresh->widgets[index].option_count - 1;
            if (b->picker_top > b->picker_selected)
                b->picker_top = b->picker_selected;
        }
    }
    if (b->ask_insecure) {
        /* die offene Rückfrage meint ein Formular der alten Fassung */
        int form = b->pending_form < old->form_count
                       ? form_by_node(fresh, old->forms[b->pending_form].node) : -1;
        int button = b->pending_button >= 0 && b->pending_button < old->widget_count
                         ? widget_by_node(fresh, old->widgets[b->pending_button].node) : -1;
        if (form < 0)
            b->ask_insecure = false;
        b->pending_form = form;
        b->pending_button = button;
    }

    if (script_stopped(b->script) && old->widget_count <= 2000) {
        /*
         * Die Skripte sind angehalten: Was der Benutzer seither eingetragen
         * hat, steht nur in der alten Fassung der Seite - mitnehmen.
         */
        for (int i = 0; i < fresh->widget_count; i++) {
            widget_t *to = &fresh->widgets[i];
            int from_index = widget_by_node(old, to->node);
            const widget_t *from = from_index >= 0 ? &old->widgets[from_index] : NULL;
            if (!from || from->kind != to->kind)
                continue;
            if (from->value)
                widget_set_value(to, from->value, strlen(from->value));
            to->checked = from->checked;
            if (from->option_count == to->option_count)
                to->selected = from->selected;
        }
    }

    b->scroll = same_place(old, fresh, b->scroll);
    kfree(b->source);
    b->source = source;
    b->source_len = len;
    b->source_html = true;
    b->page = fresh;
    b->page_columns = LAYOUT_W;
    page_free(old);
    clamp_scroll(b);
    pictures_sync(b, true);
    pictures_pending(b, true);
    /* Seiten, die sich ständig ändern, sollen den Rechner nicht mit Neusetzen auslasten. */
    uint64_t took = time_ms() - start;
    b->script_show_after = time_ms() + (took > 60 ? took * 4 : 250);
}

/* Meldet den Skripten einen Klick; false: Die Seite will nicht, dass er sonst etwas bewirkt. */
static bool page_click(browser_t *b, uint32_t node)
{
    return !b->script || !node || script_click(b->script, node);
}

/* Der Benutzer ist mit dem Feld fertig: Hat er darin getippt, erfährt es die Seite ("change"). */
static void commit_field(browser_t *b)
{
    if (!b->script || !b->field_changed || !b->page || b->focus < 0 ||
        b->focus >= b->page->widget_count)
        return;
    b->field_changed = false;
    script_field(b->script, b->page->widgets[b->focus].node, NULL, -1, -2, true);
}

/* Setzt die Schreibmarke in ein Feld (oder mit -1 in keines) und sagt es den Skripten. */
static void set_focus(browser_t *b, int index)
{
    page_t *page = b->page;
    if (index == b->focus)
        return;
    if (b->script && page && b->focus >= 0 && b->focus < page->widget_count) {
        commit_field(b);
        script_event(b->script, page->widgets[b->focus].node, "blur", NULL);
    }
    b->focus = index;
    b->field_changed = false;
    if (b->script && page && index >= 0 && index < page->widget_count)
        script_event(b->script, page->widgets[index].node, "focus", NULL);
}

/* Übernimmt eine fertig geladene Seite. */
static void take_incoming(browser_t *b)
{
    if (!b->incoming_ready)
        return;
    page_free(b->page);
    kfree(b->source);
    css_free(b->css);
    script_destroy(b->script);
    b->script = b->incoming_script;
    b->incoming_script = NULL;
    b->script_next = 0;
    b->script_show_after = 0;
    b->field_changed = false;
    b->script_reported = b->script && script_stopped(b->script);
    b->dialog_quiet = false;
    b->page = b->incoming;
    b->css = b->incoming_css;
    b->incoming_css = NULL;
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
    relayout(b, false);                 /* falls das Fenster inzwischen breiter oder schmaler ist */
    pictures_sync(b, false);
    b->pictures_changed = false;
    pictures_pending(b, true);

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
    /* die Seite, die wir verlassen, ist das nächste Ziel von "Vor" */
    char *left = b->history[--b->history_count];
    if (b->forward_count < HISTORY_MAX)
        b->forward[b->forward_count++] = left;
    else
        kfree(left);
    load(b, b->history[b->history_count - 1], NULL, false);
}

static void go_forward(browser_t *b)
{
    if (b->busy)
        return;
    if (b->forward_count == 0) {
        set_status(b, false, "Weiter vorn gibt es nichts");
        return;
    }
    char *url = b->forward[--b->forward_count];
    b->keep_forward = true;
    load(b, url, NULL, true);
    kfree(url);
}

/* Setzt ein Lesezeichen für die gezeigte Seite - oder nimmt es wieder weg. */
static void toggle_mark(browser_t *b)
{
    if (!b->page || !b->url[0] || starts_with(b->url, "about:")) {
        set_status(b, false, "Für diese Seite gibt es kein Lesezeichen");
        return;
    }
    marks_load();
    marks_lock();
    int index = mark_find(b->url);
    if (index >= 0) {
        mark_remove(index);
        set_status(b, false, "Lesezeichen entfernt");
    } else if (mark_count >= MARKS_MAX) {
        set_status(b, true, "Mehr als %d Lesezeichen gehen nicht", MARKS_MAX);
    } else {
        const char *title = b->page->title ? b->page->title : "";
        mark_append(b->url, strlen(b->url), title, strlen(title));
        set_status(b, false, "Lesezeichen gesetzt - Strg+B zeigt alle");
    }
    marks_save();
    marks_unlock();
}

/* Zeigt den Quelltext der Seite als reinen Text. */
static void show_source(browser_t *b)
{
    if (b->busy || !b->source || strcmp(b->url, SOURCE_PAGE) == 0)
        return;
    kfree(b->job_text);
    b->job_text = copy_text(b->source, b->source_len);
    if (b->job_text)
        load(b, SOURCE_PAGE, NULL, true);
}

/* --- Suchen auf der Seite ------------------------------------------------------------------------ */

/* Kleinbuchstabe eines Bytes; die Umlaute und Akzentbuchstaben (zweites Byte nach 0xC3) mit */
static char fold(char c, char before)
{
    if ((unsigned char)before == 0xC3 && (unsigned char)c >= 0x80 && (unsigned char)c <= 0x9E &&
        (unsigned char)c != 0x97)
        return (char)(c + 0x20);
    return lower(c);
}

/*
 * Die nächste Stelle ab from, an der needle (schon klein geschrieben) in text
 * steht, oder -1. Groß- und Kleinschreibung ist gleich.
 */
static int text_find(const char *text, size_t len, size_t from, const char *needle, size_t n)
{
    for (size_t i = from; n > 0 && i + n <= len; i++) {
        size_t k = 0;
        while (k < n && fold(text[i + k], i + k > 0 ? text[i + k - 1] : 0) == needle[k])
            k++;
        if (k == n)
            return (int)i;
    }
    return -1;
}

/* Wird in diesem Stück gesucht? (Formularfelder und Bilder tragen keinen Seitentext.) */
static bool span_searched(const browser_t *b, const span_t *span)
{
    return b->find_active && b->find_len > 0 && span->widget < 0 && span->image < 0;
}

/*
 * Geht zum nächsten (step 1) oder vorigen (-1) Treffer; 0 sucht ab der
 * gezeigten Stelle neu. Ein Treffer ist eine Fundstelle innerhalb eines
 * Stücks: Begriffe über einen Zeilenumbruch oder Schriftwechsel hinweg findet
 * die Suche nicht.
 */
static void find_step(browser_t *b, int step)
{
    const page_t *page = b->page;
    int from[3] = { step == 0 ? b->scroll : b->find_line, step == 0 ? -1 : b->find_span,
                    step == 0 ? -1 : b->find_offset };
    int first[3] = { -1, 0, 0 }, prev[3] = { -1, 0, 0 }, last[3] = { -1, 0, 0 }, next[3] = { -1, 0, 0 };
    int hits = 0, prev_number = 0, next_number = 0;
    b->find_hits = 0;
    b->find_number = 0;
    b->find_line = -1;
    if (!page || b->find_len == 0)
        return;
    for (int l = 0; l < page->line_count; l++) {
        const line_t *line = &page->lines[l];
        for (uint32_t i = 0; i < line->count; i++) {
            const span_t *span = &page->spans[line->first + i];
            if (!span_searched(b, span))
                continue;
            const char *text = page->pool + span->text;
            for (int at = text_find(text, span->bytes, 0, b->find_text, b->find_len); at >= 0;
                 at = text_find(text, span->bytes, (size_t)at + b->find_len, b->find_text, b->find_len)) {
                int here[3] = { l, (int)i, at };
                int order = 0;          /* liegt dieser Treffer vor (-1) oder hinter (1) der Suchstelle? */
                for (int k = 0; k < 3 && order == 0; k++)
                    order = here[k] < from[k] ? -1 : here[k] > from[k] ? 1 : 0;
                hits++;
                if (first[0] < 0)
                    memcpy(first, here, sizeof(first));
                memcpy(last, here, sizeof(last));
                if (order < 0) {
                    memcpy(prev, here, sizeof(prev));
                    prev_number = hits;
                }
                if (order > 0 && next[0] < 0) {
                    memcpy(next, here, sizeof(next));
                    next_number = hits;
                }
            }
        }
    }
    b->find_hits = hits;
    if (hits == 0)
        return;
    const int *chosen;
    if (step < 0) {                     /* rückwärts: der letzte davor - oder ganz hinten weiter */
        chosen = prev[0] >= 0 ? prev : last;
        b->find_number = prev[0] >= 0 ? prev_number : hits;
    } else {                            /* vorwärts: der nächste - oder am Ende vorn weiter */
        chosen = next[0] >= 0 ? next : first;
        b->find_number = next[0] >= 0 ? next_number : 1;
    }
    b->find_line = chosen[0];
    b->find_span = chosen[1];
    b->find_offset = chosen[2];
    if (b->find_line < b->scroll || b->find_line >= b->scroll + ROWS - 1)
        b->scroll = b->find_line > 3 ? b->find_line - 3 : 0;
    clamp_scroll(b);
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

/*
 * Sendet die Daten des Formulars; button: der gedrückte Knopf, -1 für den
 * ersten Knopf des Formulars (Enter im Textfeld) oder -2 für keinen (ein
 * Skript sendet ab).
 */
static void send_form(browser_t *b, int form_index, int button, bool insecure_ok)
{
    page_t *page = b->page;
    if (form_index < 0 || form_index >= page->form_count || !page->forms[form_index].action) {
        set_status(b, true, "Dieses Feld gehört zu keinem Formular, das sich absenden lässt");
        return;
    }
    const form_t *form = &page->forms[form_index];

    /* Ohne ausdrücklich gedrückten Knopf gilt der erste des Formulars. */
    for (int i = 0; button == -1 && i < page->widget_count; i++) {
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
        case W_BUTTON:                  /* wird nie gesendet */
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

/* Sendet das Formular ab; button: der gedrückte Knopf oder -1 (Enter im Textfeld). */
static void submit(browser_t *b, int form_index, int button, bool insecure_ok)
{
    page_t *page = b->page;
    if (b->script && !insecure_ok && form_index >= 0 && form_index < page->form_count) {
        /* Erst die Skripte fragen: Sie prüfen Eingaben, ergänzen Felder - oder lehnen ab. */
        uint32_t form_node = page->forms[form_index].node;
        uint32_t button_node = button >= 0 && button < page->widget_count
                                   ? page->widgets[button].node : 0;
        commit_field(b);
        if (!script_submit(b->script, form_node, button_node))
            return;
        script_show(b);
        page = b->page;
        form_index = form_by_node(page, form_node);
        if (button >= 0)
            button = widget_by_node(page, button_node);
        if (form_index < 0)
            return;
    }
    send_form(b, form_index, button, insecure_ok);
}

/* --- Zeichnen ------------------------------------------------------------------------------------ */

static rect_t back_rect(void)
{
    return rect_make(8, 8, 70, 26);
}

static rect_t forward_rect(void)
{
    return rect_make(82, 8, 46, 26);
}

static rect_t reload_rect(void)
{
    return rect_make(132, 8, 86, 26);
}

static rect_t toggle_rect(const browser_t *b)
{
    return rect_make(WIDTH - 8 - TOGGLE_W, 8, TOGGLE_W, 26);
}

/* "Lesezeichen": die Liste; in schmalen Fenstern fehlt der Knopf (Strg+B geht immer) */
static rect_t marks_rect(const browser_t *b)
{
    int width = WIDTH >= 760 ? 104 : 0;
    return rect_make(toggle_rect(b).x - (width ? width + 6 : 0), 8, width, 26);
}

/* der Stern: Lesezeichen für die gezeigte Seite setzen oder entfernen */
static rect_t star_rect(const browser_t *b)
{
    return rect_make(marks_rect(b).x - 34, 8, 28, 26);
}

static rect_t address_rect(const browser_t *b)
{
    return rect_make(226, 8, star_rect(b).x - 226 - 6, 26);
}

/* Meldung oder Frage eines Skripts: ein Kasten über der Seite */
static rect_t dialog_rect(const browser_t *b)
{
    int width = WIDTH - 60 > 580 ? 580 : WIDTH - 60;
    return rect_make((WIDTH - width) / 2, CONTENT_Y + 30, width, 204);
}

static rect_t dialog_yes_rect(const browser_t *b)
{
    rect_t r = dialog_rect(b);
    return rect_make(r.x + r.w - (b->dialog_question ? 2 * 118 : 118) - 6, r.y + r.h - 62, 110, 26);
}

static rect_t dialog_no_rect(const browser_t *b)
{
    rect_t r = dialog_rect(b);
    return rect_make(r.x + r.w - 118 - 6, r.y + r.h - 62, 110, 26);
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
    int box_y = y;                      /* Rahmen: ganze Zeile */
    y += (ROW_H - 1 - FONT_H) / 2;      /* Text: mittig darin */

    switch (w->kind) {
    case W_TEXT: case W_PASSWORD: case W_TEXTAREA: case W_SELECT: {
        gfx_fill(s, x, box_y, width, ROW_H - 1, C_FIELD);
        gfx_frame(s, x, box_y, width, ROW_H - 1, focused ? C_FOCUS : C_BORDER);
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
            draw_clipped(s, x + FONT_W, y, w->placeholder && w->placeholder[0]
                             ? w->placeholder : "(anklicken zum Schreiben)", columns, C_MUTED, false);
            break;
        } else if (!text[0] && w->placeholder && w->placeholder[0]) {
            /* leeres Feld: der Hinweis der Seite, was hineingehört */
            draw_clipped(s, x + FONT_W, y, w->placeholder, columns, C_MUTED, false);
            if (focused)
                gfx_fill(s, x + FONT_W + 1, y + 1, 2, FONT_H - 2, C_TEXT);
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
    case W_SUBMIT: case W_BUTTON:
        gfx_fill(s, x, box_y, width, ROW_H - 1, focused ? C_FOCUS : C_BUTTON);
        draw_clipped(s, x + 2 * FONT_W, y, w->label ? w->label : "", w->width - 4,
                     C_BUTTON_TXT, false);
        break;
    case W_HIDDEN:
        break;
    }
}

/*
 * Zeichnet die Zeile row eines Bildblocks an die Stelle y: einen Streifen des
 * Bildes - oder, solange es fehlt, den freigehaltenen Platz mit Ersatztext.
 */
static void draw_picture_row(browser_t *b, surface_t *s, const page_image_t *image,
                             int row, int x, int y)
{
    int right = SCROLL_X - 2;

    /* Den Zeiger auf das Bild holen; freigegeben wird es nur von diesem Task. */
    uint64_t flags = irq_save();
    picture_t *pic = picture_find(b, image->url);
    image_t *bitmap = pic && pic->state == PIC_READY ? pic->bitmap : NULL;
    irq_restore(flags);

    if (bitmap && image->width > 0 && image->height > 0) {
        if (bitmap->width != image->width || bitmap->height != image->height) {
            /* andere Größe gefragt: einmal umrechnen und merken */
            if (!pic->shown || pic->shown->width != image->width ||
                pic->shown->height != image->height) {
                image_free(pic->shown);
                pic->shown = image_scale(bitmap, image->width, image->height, C_BACKGROUND);
            }
            bitmap = pic->shown;
        }
        if (bitmap) {
            int first = row * ROW_H;
            for (int line = 0; line < ROW_H && first + line < bitmap->height; line++) {
                int ty = y + line;
                if (ty < CONTENT_Y || ty >= CONTENT_Y + CONTENT_H || ty >= s->height)
                    continue;
                int count = bitmap->width;
                if (x + count > right)
                    count = right - x;
                if (count > 0)
                    memcpy(s->pixels + (size_t)ty * s->stride + x,
                           bitmap->pixels + (size_t)(first + line) * bitmap->width,
                           (size_t)count * sizeof(uint32_t));
            }
            return;
        }
    }

    /* Platzhalter */
    if (image->width > 0) {
        int height = image->height - row * ROW_H;
        gfx_fill(s, x, y, image->width, height > ROW_H ? ROW_H : height, C_PLACEHOLDER);
    }
    if (row == 0) {
        char text[160];
        if (image->alt)
            ksnprintf(text, sizeof(text), "[Bild: %s]", image->alt);
        else
            strlcpy(text, "[Bild]", sizeof(text));
        draw_clipped(s, x + (image->width > 0 ? 4 : 0), y, text, (right - x - 8) / FONT_W,
                     image->link >= 0 ? C_LINK : C_MUTED, false);
    }
}

static void draw_page(browser_t *b, surface_t *s)
{
    const page_t *page = b->page;
    gfx_set_clip(s, rect_make(0, CONTENT_Y, SCROLL_X - 2, CONTENT_H));
    /*
     * Zeilen mit großer Schrift sind zwei Zeilen hoch - in Spalten auch
     * einzelne Stücke. Damit deren untere Hälfte nicht fehlt, wird die Zeile
     * über dem Ausschnitt immer mitgezeichnet (ihr oberer Teil fällt ihm zum
     * Opfer).
     */
    int first_row = b->scroll > 0 && b->scroll < page->line_count ? -1 : 0;
    int unit = page->pixels ? 1 : FONT_W;

    /*
     * Erst alle Hintergründe, dann der Inhalt: Hohe Stücke reichen in die
     * nächste Zeile, und deren Hintergrund darf sie nicht übermalen.
     */
    for (int row = first_row; row < ROWS && b->scroll + row < page->line_count; row++) {
        const line_t *line = &page->lines[b->scroll + row];
        int y = CONTENT_Y + row * ROW_H;
        if (line->background && !line->filler && line->image < 0)   /* hinterlegter Abschnitt */
            gfx_fill(s, 0, y, SCROLL_X - 2, line->tall ? 2 * ROW_H : ROW_H,
                     line->background & 0xFFFFFF);
    }
    for (int row = first_row; row < ROWS && b->scroll + row < page->line_count; row++) {
        const line_t *line = &page->lines[b->scroll + row];
        int y = CONTENT_Y + row * ROW_H;
        for (uint32_t i = 0; i < line->count; i++) {
            const span_t *span = &page->spans[line->first + i];
            int x = CONTENT_X + span->column * unit;
            bool aside = span->style & STYLE_ASIDE;
            int height = (line->tall || span->tall) && !aside ? 2 * ROW_H : ROW_H;
            if (span->widget >= 0 || span->image >= 0)
                continue;
            if (span->background)
                gfx_fill(s, x, y, span->width * unit, height, span->background & 0xFFFFFF);
            /* Suchen auf der Seite: jede Fundstelle gelb, die aktuelle kräftiger */
            if (span_searched(b, span)) {
                const char *text = page->pool + span->text;
                for (int at = text_find(text, span->bytes, 0, b->find_text, b->find_len); at >= 0;
                     at = text_find(text, span->bytes, (size_t)at + b->find_len, b->find_text,
                                    b->find_len)) {
                    bool current = b->scroll + row == b->find_line && (int)i == b->find_span &&
                                   at == b->find_offset;
                    int left = page_text_width(NULL, text, (size_t)at, span->style, span->size);
                    int wide = page_text_width(NULL, text + at, b->find_len, span->style, span->size);
                    gfx_fill(s, x + left - 1, y, wide + 2, height,
                             current ? RGB(255, 170, 60) : RGB(255, 240, 130));
                }
            }
        }
    }

    for (int row = first_row; row < ROWS && b->scroll + row < page->line_count; row++) {
        const line_t *line = &page->lines[b->scroll + row];
        int y = CONTENT_Y + row * ROW_H;
        /*
         * Die untere Hälfte einer hohen Zeile und Zeilen eines Bildes tragen
         * selbst keinen Text - wohl aber Stücke von Kästen neben dem Text.
         */
        if (line->image >= 0 && line->image < page->image_count)
            draw_picture_row(b, s, &page->images[line->image], line->image_row,
                             CONTENT_X + page->images[line->image].column * unit, y);

        /* Alle Stücke einer Zeile stehen auf derselben Grundlinie: der der größten Schrift */
        int largest = TEXT_SIZE;
        for (uint32_t i = 0; i < line->count; i++) {
            const span_t *span = &page->spans[line->first + i];
            if (span->widget < 0 && !(span->style & (STYLE_MONO | STYLE_ASIDE)) && !span->tall &&
                span->size > largest)
                largest = span->size;
        }
        int line_baseline = line->tall ? y + 4 + (largest * 9 + 5) / 10
                                       : y + (largest >= 17 ? 16 : 15);

        for (uint32_t i = 0; i < line->count; i++) {
            const span_t *span = &page->spans[line->first + i];
            int x = CONTENT_X + span->column * unit;
            if (span->widget >= 0) {
                draw_widget(b, s, span->widget, x, y);
                continue;
            }
            if (span->image >= 0 && span->image < page->image_count) {
                /* Bild in einer Tabellenzelle oder einem Kasten neben dem Text */
                draw_picture_row(b, s, &page->images[span->image], span->image_row, x, y);
                continue;
            }
            /* Stücke eines Kastens neben dem Text stehen immer in einfacher Zeilenhöhe */
            bool aside = span->style & STYLE_ASIDE;
            int baseline = aside ? y + (span->size >= 17 ? 16 : 15) : line_baseline;
            /* ... und Stücke aus einer Spalte so hoch wie ihre Zeile dort */
            if (span->tall && !aside)
                baseline = y + 4 + (span->tall * 9 + 5) / 10;
            color_t color = span->color ? (span->color & 0xFFFFFF)
                          : span->style & STYLE_LINK ? C_LINK
                          : span->style & STYLE_HEADING ? C_HEADING
                          : span->style & STYLE_MUTED ? C_MUTED : C_TEXT;
            const char *text = page->pool + span->text;
            if (!page->pixels || text_is_grid(span->style)) {
                /* Rasterschrift: Zeichen für Zeichen in festen Schritten */
                const char *end = text + span->bytes;
                int cx = x;
                while (text < end) {
                    uint32_t cp = utf8_next(&text);
                    gfx_glyph(s, cx, baseline - 12, cp, color, span->style & STYLE_BOLD);
                    cx += FONT_W;
                }
            } else {
                font_draw(s, x, baseline, text, span->bytes, span->size ? span->size : TEXT_SIZE,
                          text_flags(span->style), color);
            }
            if (span->style & STYLE_UNDERLINE)
                gfx_fill(s, x, baseline + 2, span->width * unit, 1, color);
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

/* Zeigt die Meldung eines Skripts: umbrochen, höchstens fünf Zeilen. */
static void draw_dialog(browser_t *b, surface_t *s)
{
    rect_t r = dialog_rect(b);
    const char *p = b->dialog;
    int y = r.y + 38, lines = 0;

    gfx_fill(s, r.x + 5, r.y + 5, r.w, r.h, C_BORDER);
    gfx_fill(s, r.x, r.y, r.w, r.h, C_FIELD);
    gfx_frame(s, r.x, r.y, r.w, r.h, C_FOCUS);
    gfx_set_clip(s, rect_make(r.x + 2, r.y + 2, r.w - 4, r.h - 4));
    ui_text(s, r.x + 14, r.y + 12, b->dialog_question ? "Die Seite fragt:" : "Die Seite meldet:",
            C_HEADING, true);
    while (*p && lines < 5) {
        char row[200];
        size_t n = 0;
        row[0] = '\0';
        while (*p == ' ')
            p++;
        while (*p && *p != '\n') {
            size_t word = 0, before = n;
            while (p[word] && p[word] != ' ' && p[word] != '\n')
                word++;
            if (n + word + 2 >= sizeof(row)) {
                if (n > 0)
                    break;
                word = sizeof(row) - 8;                 /* ein überlanges Wort: teilen */
                while (word > 0 && ((uint8_t)p[word] & 0xC0) == 0x80)
                    word--;
            }
            if (n)
                row[n++] = ' ';
            memcpy(row + n, p, word);
            n += word;
            row[n] = '\0';
            if (before > 0 && ui_text_width(row, false) > r.w - 28) {
                n = before;
                row[n] = '\0';
                break;
            }
            p += word;
            while (*p == ' ')
                p++;
        }
        if (*p == '\n')
            p++;
        ui_text(s, r.x + 14, y, row, C_TEXT, false);
        y += ROW_H;
        lines++;
    }
    if (*p)
        ui_text(s, r.x + r.w - 30, y - ROW_H, "...", C_MUTED, false);
    gfx_reset_clip(s);

    if (b->dialog_question) {
        ui_button(s, dialog_yes_rect(b), "Ja (J)", true);
        ui_button(s, dialog_no_rect(b), "Nein (N)", false);
    } else {
        ui_button(s, dialog_yes_rect(b), "OK (Enter)", true);
    }
    ui_text(s, r.x + 14, r.y + r.h - 26, "Esc: keine weiteren Meldungen von dieser Seite",
            C_MUTED, false);
}

static void draw(browser_t *b)
{
    surface_t *s = window_surface(b->window);
    char text[260];

    gfx_fill(s, 0, 0, WIDTH, HEIGHT, C_BACKGROUND);
    gfx_fill(s, 0, 0, WIDTH, TOOLBAR_H, C_TOOLBAR);
    ui_button(s, back_rect(), "Zurück", false);
    ui_button(s, forward_rect(), "Vor", false);
    ui_button(s, reload_rect(), "Neu laden", false);
    ui_button(s, toggle_rect(b), b->javascript ? "JavaScript an" : "JavaScript aus", b->javascript);
    if (marks_rect(b).w > 0)
        ui_button(s, marks_rect(b), "Lesezeichen", false);
    marks_lock();
    bool marked = b->url[0] && mark_find(b->url) >= 0;
    marks_unlock();
    ui_button(s, star_rect(b), marked ? "\xE2\x98\x85" : "\xE2\x98\x86", marked);

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

    if (b->page && b->page->background)     /* die Seite hat eine eigene Hintergrundfarbe */
        gfx_fill(s, 0, CONTENT_Y - 4, SCROLL_X - 2, STATUS_Y - CONTENT_Y + 4,
                 b->page->background & 0xFFFFFF);
    if (b->page)
        draw_page(b, s);
    if (b->page && b->overlay != OVERLAY_NONE)
        draw_overlay(b, s);
    if (b->dialog)
        draw_dialog(b, s);

    /* Statuszeile: Rückfrage, Fortschritt, Meldung - oder Titel und Sicherheit der Seite */
    gfx_fill(s, 0, STATUS_Y, WIDTH, STATUS_H, C_TOOLBAR);
    int y = STATUS_Y + (STATUS_H - FONT_H) / 2;
    if (b->ask_insecure) {
        ui_text(s, 10, y, "ACHTUNG: Das Passwort würde unverschlüsselt gesendet (kein https). "
                          "Trotzdem senden?  J / N", C_ERROR, true);
    } else if (b->find_active) {
        int end = ui_text(s, 10, y, "Suchen: ", C_TEXT, true);
        end = ui_text(s, end, y, b->find_text, C_TEXT, false);
        gfx_fill(s, end + 1, y + 1, 2, FONT_H - 2, C_TEXT);
        if (b->find_len == 0)
            ksnprintf(text, sizeof(text), "Enter: weiter   Umschalt+Enter: zurück   Esc: schließen");
        else if (b->find_hits == 0)
            ksnprintf(text, sizeof(text), "nicht gefunden");
        else
            ksnprintf(text, sizeof(text), "Treffer %d von %d   (Enter: weiter, Esc: schließen)",
                      b->find_number, b->find_hits);
        ui_text(s, WIDTH - 10 - ui_text_width(text, false), y, text,
                b->find_len > 0 && b->find_hits == 0 ? C_ERROR : C_MUTED, false);
    } else if (b->busy) {
        ksnprintf(text, sizeof(text), "%s  (%llu s)", b->status[0] ? b->status : "Lade ...",
                  (time_ms() - b->busy_since) / 1000);
        ui_text(s, 10, y, text, C_TEXT, false);
    } else if (b->page && !b->status_error && pictures_pending(b, false) > 0) {
        ksnprintf(text, sizeof(text), "Lade Bilder ... noch %d von %d",
                  pictures_pending(b, false), b->page->image_count);
        ui_text(s, 10, y, text, C_TEXT, false);
    } else if (b->status[0]) {
        gfx_set_clip(s, rect_make(10, STATUS_Y, WIDTH - 20, STATUS_H));
        ui_text(s, 10, y, b->status, b->status_error ? C_ERROR : C_GOOD, false);
        gfx_reset_clip(s);
    } else if (b->page) {
        bool secure = starts_with(b->url, "https://");
        const char *note = secure ? "verschlüsselt, Server geprüft"
                         : starts_with(b->url, "http://") ? "NICHT verschlüsselt" : "";
        int note_x = WIDTH - 10 - ui_text_width(note, false);
        /* Der Titel der Seite; was nicht passt, wird abgeschnitten */
        gfx_set_clip(s, rect_make(10, STATUS_Y, note_x - 30, STATUS_H));
        ui_text(s, 10, y, b->page->title ? b->page->title : "", C_TEXT, false);
        gfx_reset_clip(s);
        ui_text(s, note_x, y, note, secure ? C_GOOD : C_ERROR, false);
    }
    window_invalidate(b->window, 0, 0, WIDTH, HEIGHT);
}

/* --- Meldungen der Skripte ------------------------------------------------------------------- */

static void dialog_close(browser_t *b, bool yes, bool quiet)
{
    char *text = b->dialog;
    b->dialog_answer = yes;
    if (quiet)
        b->dialog_quiet = true;
    b->dialog = NULL;                   /* darauf wartet, wer gefragt hat */
    kfree(text);
}

static void dialog_key(browser_t *b, int key)
{
    if (key == 27)
        dialog_close(b, false, true);
    else if (!b->dialog_question && (key == '\n' || key == ' '))
        dialog_close(b, true, false);
    else if (b->dialog_question && (key == 'j' || key == 'J' || key == '\n'))
        dialog_close(b, true, false);
    else if (b->dialog_question && (key == 'n' || key == 'N'))
        dialog_close(b, false, false);
}

static void dialog_click(browser_t *b, int x, int y)
{
    if (rect_contains(dialog_yes_rect(b), x, y))
        dialog_close(b, true, false);
    else if (b->dialog_question && rect_contains(dialog_no_rect(b), x, y))
        dialog_close(b, false, false);
}

/*
 * Ein Skript fragt, während das Fenster selbst es ausführt (Klick, Zeitgeber):
 * Dann bedient diese Schleife die Rückfrage, bis sie beantwortet ist.
 */
static void dialog_loop(browser_t *b)
{
    bool redraw = true;
    while (b->dialog) {
        event_t ev;
        if (redraw)
            draw(b);
        redraw = false;
        if (!window_get_event(b->window, &ev, 400))
            continue;
        redraw = true;
        if (ev.type == EV_CLOSE) {
            b->close_wanted = true;
            dialog_close(b, false, true);
        } else if (ev.type == EV_KEY) {
            dialog_key(b, ev.key);
        } else if (ev.type == EV_MOUSE_DOWN) {
            dialog_click(b, ev.x, ev.y);
        }
    }
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
    widget_t *w = &b->page->widgets[b->overlay_widget];
    widget_set_value(w, b->edit.text, b->edit.len);
    b->overlay = OVERLAY_NONE;
    if (b->script)
        script_field(b->script, w->node, w->value ? w->value : "", -1, -2, true);
}

/* Ein Eintrag der Auswahlliste wurde gewählt. */
static void pick_option(browser_t *b, widget_t *w, int index)
{
    bool changed = w->selected != index;
    w->selected = index;
    if (b->script && changed)
        script_field(b->script, w->node, NULL, -1, index, true);
}

/* Meldet den Skripten eine Taste im Feld; false: Die Seite will nicht, dass sie wirkt. */
static bool field_key(browser_t *b, const char *type, int key)
{
    char name[12];
    size_t n = 0;
    if (!b->script || !b->page || b->focus < 0 || b->focus >= b->page->widget_count)
        return true;
    if (key == '\n')
        strlcpy(name, "Enter", sizeof(name));
    else if (key == '\b')
        strlcpy(name, "Backspace", sizeof(name));
    else if (!type_into(name, &n, sizeof(name), key))
        return true;
    return script_event(b->script, b->page->widgets[b->focus].node, type, name);
}

/* Ein Formularfeld wurde angeklickt oder mit Enter/Leertaste betätigt. */
static void activate_widget(browser_t *b, int index)
{
    page_t *page = b->page;
    widget_t *w = &page->widgets[index];
    set_focus(b, index);
    b->address_focus = false;
    /*
     * Mit Skripten erfährt zuerst die Seite vom Klick. Ankreuzfelder schaltet
     * sie dabei selbst um (ihr Abbild gilt) - und sie kann den Klick ablehnen.
     */
    if (b->script && w->node && !script_click(b->script, w->node))
        return;
    switch (w->kind) {
    case W_BUTTON:
        if (!b->script)
            set_status(b, false, "Dieser Knopf braucht die Skripte der Seite - sie laufen nicht");
        break;
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

static int hex_digit(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : lower(c) >= 'a' && lower(c) <= 'f' ? lower(c) - 'a' + 10 : -1;
}

static void follow_link(browser_t *b, const char *target)
{
    if (word_at(target, "javascript:")) {       /* der Link ist selbst ein Skript */
        if (!b->script) {
            set_status(b, false, b->javascript
                           ? "Dieser Link führt ein Skript aus - die Skripte dieser Seite laufen aber nicht"
                           : "Dieser Link braucht JavaScript - das ist ausgeschaltet (Knopf oben rechts)");
            return;
        }
        size_t n = 0;
        char *code = kmalloc(strlen(target) + 1);
        if (!code)
            return;
        for (const char *p = target + 11; *p; p++) {
            if (*p == '%' && hex_digit(p[1]) >= 0 && hex_digit(p[2]) >= 0) {
                code[n++] = (char)(hex_digit(p[1]) * 16 + hex_digit(p[2]));
                p += 2;
            } else {
                code[n++] = *p;
            }
        }
        code[n] = '\0';
        script_eval(b->script, code);
        kfree(code);
        return;
    }

    /* Sprung innerhalb der Seite: "#marke" - oder ihre eigene Adresse mit einer Marke */
    const char *mark = NULL;
    if (target[0] == '#') {
        mark = target + 1;
    } else {
        size_t n = 0;
        while (b->url[n] && b->url[n] != '#' && b->url[n] == target[n])
            n++;
        if ((b->url[n] == '\0' || b->url[n] == '#') && target[n] == '#')
            mark = target + n + 1;
    }
    if (mark) {
        for (int i = 0; b->page && i < b->page->anchor_count; i++) {
            if (strcmp(b->page->anchors[i].id, mark) == 0) {
                b->scroll = b->page->anchors[i].line;
                clamp_scroll(b);
                return;
            }
        }
        if (!mark[0] || strcmp(mark, "top") == 0)
            b->scroll = 0;
        return;
    }
    load(b, target, NULL, true);
}

/* Was die Skripte vom Browser wollen: Seite wechseln, Formular absenden ... */
static void script_wishes(browser_t *b)
{
    char *target = kmalloc(URL_SIZE), *full = kmalloc(URL_SIZE);
    uint32_t node, button;
    script_wish_t kind;
    while (target && full && b->script &&
           (kind = script_wish(b->script, target, URL_SIZE, &node, &button)) != SCRIPT_NOTHING) {
        if (kind == SCRIPT_NAVIGATE) {
            if (target[0] == '#')
                follow_link(b, target);
            else if (url_resolve(b->url, target, full, URL_SIZE))
                follow_link(b, full);
        } else if (kind == SCRIPT_SUBMIT) {
            script_show(b);             /* gesendet wird, was die Skripte eingetragen haben */
            int form = form_by_node(b->page, node);
            if (form >= 0)
                send_form(b, form, button ? widget_by_node(b->page, button) : -2, false);
        } else if (kind == SCRIPT_BACK) {
            go_back(b);
        } else if (kind == SCRIPT_RELOAD && b->url[0]) {
            load(b, b->url, NULL, false);
        }
    }
    kfree(target);
    kfree(full);
    if (b->script && script_stopped(b->script) && !b->script_reported) {
        b->script_reported = true;
        set_status(b, true, "Ein Skript dieser Seite wurde angehalten (zu lange gerechnet oder "
                   "zu viel Speicher)");
    }
}

/* Nach jeder Eingabe des Benutzers: zeigen, was die Skripte daraufhin geändert haben. */
static void script_settle(browser_t *b)
{
    if (!b->script)
        return;
    b->script_next = 0;                 /* sie können Zeitgeber gestellt haben */
    script_show(b);
    script_wishes(b);
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
            set_focus(b, index);
            b->address_focus = false;
            show_line(b, page->widgets[index].line);
            return;
        }
    }
}

static void key_pressed(browser_t *b, int key, int mods)
{
    page_t *page = b->page;

    if (b->dialog) {                    /* ein Skript der ladenden Seite fragt */
        dialog_key(b, key);
        return;
    }
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
            b->overlay = OVERLAY_NONE;
            pick_option(b, w, b->picker_selected);
        } else if (key == 27) {
            b->overlay = OVERLAY_NONE;
        }
        if (b->picker_selected < b->picker_top)
            b->picker_top = b->picker_selected;
        if (b->picker_selected >= b->picker_top + rows)
            b->picker_top = b->picker_selected - rows + 1;
        return;
    }

    /* Suchen auf der Seite: Die Eingabe geht in das Suchfeld der Statuszeile */
    if (b->find_active) {
        if (key == 27) {
            b->find_active = false;
        } else if (key == '\n') {
            find_step(b, mods & MOD_SHIFT ? -1 : 1);
        } else if (key == KEY_UP || key == KEY_DOWN || key == KEY_PAGE_UP || key == KEY_PAGE_DOWN) {
            b->scroll += key == KEY_UP ? -3 : key == KEY_DOWN ? 3
                       : key == KEY_PAGE_UP ? -(ROWS - 2) : ROWS - 2;
            clamp_scroll(b);
        } else if (!(mods & MOD_CTRL) && type_into(b->find_text, &b->find_len, sizeof(b->find_text), key)) {
            /* gesucht wird ohne Rücksicht auf Groß- und Kleinschreibung: klein merken */
            for (size_t i = 0; i < b->find_len; i++)
                b->find_text[i] = fold(b->find_text[i], i > 0 ? b->find_text[i - 1] : 0);
            find_step(b, 0);
        }
        return;
    }

    /* Kürzel, die überall gelten */
    if ((mods & MOD_CTRL) && key == 6) {                /* Strg+F */
        if (page) {
            b->find_active = true;
            b->address_focus = false;
            set_focus(b, -1);
            find_step(b, 0);
        }
        return;
    }
    if ((mods & MOD_CTRL) && key == 4) {                /* Strg+D */
        toggle_mark(b);
        return;
    }
    if ((mods & MOD_CTRL) && key == 2) {                /* Strg+B */
        load(b, MARKS_PAGE, NULL, true);
        return;
    }
    if ((mods & MOD_CTRL) && key == 21) {               /* Strg+U */
        show_source(b);
        return;
    }
    if ((mods & MOD_CTRL) && (key == '+' || key == '=' || key == '-' || key == '0')) {
        /* Schriftgröße: vier Stufen - mehr geht bei fester Zeilenhöhe nicht */
        static const int sizes[] = { 13, 15, 17, 18 };
        int at = 1;
        for (int i = 0; i < 4; i++) {
            if (sizes[i] == b->text_size)
                at = i;
        }
        at = key == '0' ? 1 : key == '-' ? at - 1 : at + 1;
        at = at < 0 ? 0 : at > 3 ? 3 : at;
        if (sizes[at] != b->text_size) {
            b->text_size = sizes[at];
            relayout(b, true);
        }
        set_status(b, false, "Schriftgröße: %d Punkt", b->text_size);
        return;
    }
    if ((mods & MOD_ALT) && key == KEY_RIGHT) {
        go_forward(b);
        return;
    }
    if ((mods & MOD_CTRL) && key == 12) {               /* Strg+L */
        b->address_focus = true;
        set_focus(b, -1);
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
            /* Die Seite erfährt von der Taste zuerst - viele werten Enter selbst aus. */
            if (!field_key(b, "keydown", key) || !field_key(b, "keypress", key))
                return;
            if (w->form >= 0 || !b->script)
                submit(b, w->form, -1, false);
            else
                field_key(b, "keyup", key);
            return;
        }
        if (key == 27) {
            set_focus(b, -1);
            return;
        }
        if (key == '\b' || (key >= 32 && key < 0x800 && key != 127)) {
            if (!field_key(b, "keydown", key) || (key != '\b' && !field_key(b, "keypress", key)))
                return;
            if (b->script && script_changed(b->script)) {
                script_show(b);         /* die Seite hat auf die Taste hin etwas umgebaut */
                page = b->page;
                w = b->focus >= 0 ? &page->widgets[b->focus] : NULL;
                if (!w || (w->kind != W_TEXT && w->kind != W_PASSWORD))
                    return;
            }
            type_into_widget(w, key);
            if (b->script) {
                script_field(b->script, w->node, w->value ? w->value : "", -1, -2, false);
                b->field_changed = true;
                field_key(b, "keyup", key);
            }
            return;
        }
    } else if (w && (key == '\n' || key == ' ')) {
        activate_widget(b, b->focus);
        return;
    } else if (w && key == 27) {
        set_focus(b, -1);
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

    if (b->dialog) {
        dialog_click(b, x, y);
        return;
    }
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
            b->overlay = OVERLAY_NONE;
            if (index >= 0 && index < w->option_count)
                pick_option(b, w, index);
        }
        b->overlay = OVERLAY_NONE;
        return;
    }

    if (rect_contains(back_rect(), x, y)) {
        go_back(b);
    } else if (rect_contains(forward_rect(), x, y)) {
        go_forward(b);
    } else if (rect_contains(star_rect(b), x, y)) {
        toggle_mark(b);
    } else if (marks_rect(b).w > 0 && rect_contains(marks_rect(b), x, y)) {
        load(b, MARKS_PAGE, NULL, true);
    } else if (rect_contains(reload_rect(), x, y)) {
        if (b->url[0])
            load(b, b->url, NULL, false);
    } else if (rect_contains(toggle_rect(b), x, y)) {
        if (!b->busy) {
            /* gilt ab sofort für alle Seiten; die gezeigte wird dafür neu geladen */
            b->javascript = javascript_wanted = !b->javascript;
            save_settings();
            if (b->url[0])
                load(b, b->url, NULL, false);
        }
    } else if (rect_contains(address_rect(b), x, y)) {
        b->address_focus = true;
        set_focus(b, -1);
    } else if (page && x >= SCROLL_X - 2 && y >= CONTENT_Y && y < CONTENT_Y + CONTENT_H) {
        scroll_to_mouse(b, y);
    } else if (page && y >= CONTENT_Y && y < CONTENT_Y + CONTENT_H && !b->busy) {
        int line = b->scroll + (y - CONTENT_Y) / ROW_H;
        int unit = page->pixels ? 1 : FONT_W;
        int column = (x - CONTENT_X) / unit;
        b->address_focus = false;
        if (line >= page->line_count || x < CONTENT_X) {
            set_focus(b, -1);
            return;
        }
        const line_t *l = &page->lines[line];
        if (l->image >= 0 && l->image < page->image_count) {
            /* Bild in einem Link: ein Klick darauf folgt dem Link */
            const page_image_t *image = &page->images[l->image];
            int left = CONTENT_X + image->column * unit;
            int width = image->width > 120 ? image->width : 120;
            set_focus(b, -1);
            if (x >= left && x < left + width && page_click(b, image->node) && image->link >= 0)
                follow_link(b, page->links[image->link]);
            return;
        }
        for (uint32_t i = 0; i < l->count; i++) {
            const span_t *span = &page->spans[l->first + i];
            if (column < span->column || column >= span->column + span->width)
                continue;
            if (span->bytes == 0 && span->widget < 0 && span->image < 0)
                continue;               /* nur der farbige Kasten einer Tabellenzelle */
            if (span->widget >= 0) {
                activate_widget(b, span->widget);
                return;
            }
            /* Die Skripte der Seite erfahren von jedem Klick - und können ihn für sich behalten. */
            set_focus(b, -1);
            if (page_click(b, span->node) && span->link >= 0)
                follow_link(b, page->links[span->link]);
            return;
        }
        set_focus(b, -1);
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
    b->text_size = TEXT_SIZE;
    b->find_line = -1;
    load_settings();
    marks_load();
    b->javascript = javascript_wanted;
    if (start && *start) {
        set_address(b, start);
        go_address(b);
    } else {
        load(b, START_PAGE, NULL, true);
    }
    kfree(start);

    bool dragging = false;              /* Maustaste auf dem Rollbalken gedrückt */
    bool was_busy = false, redraw = true;
    int was_pending = 0;
    uint32_t dialog_shown = 0;          /* diese Meldung steht schon auf dem Schirm */
    for (;;) {
        /*
         * Sekundenanzeige, neue Seite, Fehlermeldung. Steht eine Meldung der
         * ladenden Seite da, bleibt das Bild, wie es ist - ständiges
         * Neuzeichnen ließe es flackern.
         */
        bool waiting = b->dialog && dialog_shown == b->dialog_number;
        if ((b->incoming_ready || b->busy || was_busy) && !waiting)
            redraw = true;
        was_busy = b->busy;
        take_incoming(b);
        if (b->close_wanted)
            break;

        /* Skripte: Formular, das eine Seite beim Laden selbst absenden wollte */
        if (b->incoming_submit && b->script && !b->busy && !b->incoming_ready) {
            int form = form_by_node(b->page, b->incoming_submit);
            int button = b->incoming_button ? widget_by_node(b->page, b->incoming_button) : -2;
            b->incoming_submit = 0;
            if (form >= 0)
                send_form(b, form, button, false);
        }
        /* ... ihre fälligen Zeitgeber - und danach zeigen, was sich geändert hat */
        if (b->script && !b->dialog) {
            uint64_t now = time_ms();
            if (now >= b->script_next) {
                int64_t wait = -1;
                bool ran = script_tick(b->script, &wait);
                now = time_ms();
                b->script_next = wait < 0 ? UINT64_MAX : now + (wait < 40 ? 40 : (uint64_t)wait);
                if (ran) {
                    script_wishes(b);
                    redraw = true;
                }
            }
            if (script_changed(b->script) && now >= b->script_show_after) {
                script_show(b);
                redraw = true;
            }
        }
        if (b->dialog ? dialog_shown != b->dialog_number : dialog_shown != 0) {
            dialog_shown = b->dialog ? b->dialog_number : 0;
            redraw = true;              /* eine Meldung ist gekommen oder gegangen */
        }

        /*
         * Bilder: Sind welche angekommen, die Seite neu setzen - aber nicht
         * bei jedem einzelnen, sonst springt sie ständig.
         */
        int pending = pictures_pending(b, true);
        if (b->pictures_changed && !b->busy && b->overlay == OVERLAY_NONE &&
            time_ms() >= b->pictures_settle) {
            b->pictures_changed = false;
            b->pictures_settle = time_ms() + (pending > 0 ? 900 : 0);
            relayout(b, true);
            redraw = true;
        }
        if ((pending > 0 || was_pending > 0) && !waiting)
            redraw = true;              /* Fortschritt in der Statuszeile */
        was_pending = pending;

        if (redraw)
            draw(b);
        redraw = false;

        /* Immer mit Frist warten: Die Helfer können jederzeit fertig werden. */
        event_t ev;
        uint32_t patience = b->busy || pending > 0 ? 200 : 400;
        if (b->script) {                /* rechtzeitig für Zeitgeber und Änderungen aufwachen */
            uint64_t now = time_ms(), due = b->script_next;
            if (script_changed(b->script) && b->script_show_after < due)
                due = b->script_show_after;
            if (due <= now)
                patience = 10;
            else if (due - now < patience)
                patience = (uint32_t)(due - now);
        }
        if (!window_get_event(b->window, &ev, patience))
            continue;
        redraw = true;
        if (ev.type == EV_CLOSE)
            break;
        if (ev.type == EV_RESIZE && window_resize(b->window, ev.x, ev.y)) {
            b->width = ev.x;
            b->height = ev.y;
            textedit_resize(&b->edit, (WIDTH - 16 - 12) / FONT_W,
                            (CONTENT_H - 42) / TEXTEDIT_LINE_H);
            relayout(b, false);
            clamp_scroll(b);
        }
        if (ev.type == EV_KEY) {
            key_pressed(b, ev.key, ev.mods);
            script_settle(b);
        } else if (ev.type == EV_SCROLL && b->overlay == OVERLAY_NONE) {
            b->scroll -= 3 * ev.y;      /* Mausrad: drei Zeilen je Raste */
            clamp_scroll(b);
        } else if (ev.type == EV_MOUSE_DOWN) {
            dragging = ev.x >= SCROLL_X - 2 && b->overlay == OVERLAY_NONE;
            mouse_pressed(b, ev.x, ev.y);
            script_settle(b);
        } else if (ev.type == EV_MOUSE_MOVE && dragging) {
            scroll_to_mouse(b, ev.y);
        } else if (ev.type == EV_MOUSE_UP) {
            dragging = false;
        }
    }

    /* Fenster sofort schließen; auf einen noch laufenden Helfer aber warten. */
    b->closing = true;
    window_destroy(b->window);
    while (b->busy || b->picture_workers > 0)
        task_sleep(100);
    for (int i = 0; i < b->picture_count; i++)
        picture_release(b, &b->pictures[i]);
    script_destroy(b->script);
    script_destroy(b->incoming_script);
    kfree(b->dialog);
    page_free(b->page);
    page_free(b->incoming);
    kfree(b->source);
    kfree(b->incoming_source);
    css_free(b->css);
    css_free(b->incoming_css);
    for (int i = 0; i < b->history_count; i++)
        kfree(b->history[i]);
    for (int i = 0; i < b->forward_count; i++)
        kfree(b->forward[i]);
    kfree(b->job_text);
    while (b->cookies) {
        cookie_t *next = b->cookies->next;
        cookie_free(b->cookies);
        b->cookies = next;
    }
    textedit_free(&b->edit);
    kfree(b);
}
