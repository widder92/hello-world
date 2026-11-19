/*
 * Skripte einer Webseite: verbindet das Seitenabbild (dom.h) mit der
 * JavaScript-Maschine (js.h). Der Browser legt für eine Seite ein script_t
 * an, lässt ihre Skripte laufen und holt sich danach den Quelltext für die
 * Anzeige. Klicks, Eingaben und Zeitgeber reicht er hier herein; was die
 * Skripte daraufhin am Abbild ändern, zeigt er neu an.
 *
 * Gerechnet wird auf einem eigenen, großen Stack; ein Skript, das eine Grenze
 * überschreitet (Zeit, Speicher), wird abgebrochen, und die Seite bleibt, wie
 * sie bis dahin war.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct script script_t;

enum { SCRIPT_COOKIES = 1, SCRIPT_PUBLIC_ONLY = 2 };

/* Antwort auf eine Anfrage eines Skripts */
typedef struct {
    int status;
    char *body;                         /* mit kmalloc angelegt */
    size_t length;
    char content_type[64];
    char *headers;                      /* alle Kopfzeilen der Antwort außer den Cookies, je
                                           "Name: Wert\n"; mit kmalloc angelegt oder NULL */
} script_reply_t;

typedef struct {
    /*
     * Holt eine Datei der Seite (ein Skript); url ist vollständig. Liefert
     * mit kmalloc angelegten Text oder NULL. Wird nur während script_boot()
     * gerufen.
     */
    char *(*fetch)(void *ctx, const char *url, bool public_only, size_t *len);
    /*
     * Ein einzelner Austausch mit einem Server im Auftrag eines Skripts
     * (XMLHttpRequest, fetch): url ist vollständig; Weiterleitungen folgt
     * diese Funktion NICHT - das und die Prüfung, ob ein Skript die Antwort
     * eines fremden Servers überhaupt sehen darf, erledigt der Aufrufer
     * (dom_js.c). headers: zusätzliche Kopfzeilen, je mit \r\n (oder NULL).
     * flags: SCRIPT_COOKIES - die Cookies des Benutzers für diesen Server
     * gehen mit, und Cookies der Antwort werden angenommen (sonst weder noch);
     * SCRIPT_PUBLIC_ONLY - nur verbinden, wenn der Server eine öffentliche
     * Adresse hat (auch bei fetch oben). Liefert false, wenn keine Antwort
     * zustande kam.
     */
    bool (*request)(void *ctx, const char *method, const char *url, const char *headers,
                    const char *body, size_t body_len, unsigned flags, script_reply_t *reply);
    /* console.log und Fehlermeldungen der Skripte */
    void (*log)(void *ctx, const char *text);
    /* Rückfragen; ohne sie zeigt alert nichts, und confirm antwortet mit Nein */
    void (*alert)(void *ctx, const char *text);
    bool (*confirm)(void *ctx, const char *text);
    /* true: Das Fenster wird geschlossen - laufende Skripte sollen sofort enden. */
    bool (*cancelled)(void *ctx);
    void *ctx;
    /*
     * Die Seite kam von einem Rechner im eigenen Netz: Dann dürfen ihre
     * Skripte auch dorthin Anfragen schicken. Die Skripte einer Seite aus dem
     * Internet erreichen nur öffentliche Adressen - sonst könnte jede Webseite
     * den Router oder den Drucker zu Hause ansprechen.
     */
    bool private_page;
    int width, height;                  /* Größe des Fensters für die Skripte */
    uint32_t budget_ms;                 /* Zeit je Skript (0: die übliche) - für Tests */
} script_host_t;

/* Was ein Skript vom Browser will */
typedef enum { SCRIPT_NOTHING, SCRIPT_NAVIGATE, SCRIPT_SUBMIT, SCRIPT_BACK, SCRIPT_RELOAD } script_wish_t;

/* Liest die Seite ein; NULL, wenn der Speicher nicht reicht. */
script_t *script_create(const char *html, size_t len, const char *url, const script_host_t *host);
void script_destroy(script_t *s);

/*
 * Die Stylesheets der Seite (oder NULL): Damit erfahren Skripte, was laut
 * Gestaltung zu sehen ist (getComputedStyle, offsetWidth). Sie gehören dem
 * Aufrufer und müssen so lange bestehen wie hier eingetragen.
 */
struct css_sheet;
void script_set_styles(script_t *s, const struct css_sheet *css);

/* Führt die Skripte der Seite aus und meldet ihr, dass sie geladen ist. */
void script_boot(script_t *s);

/*
 * Das Abbild der Seite, wie es jetzt ist (dom.h): Daraus setzt der Browser die
 * Seite. Es gehört weiter den Skripten und darf nicht verändert werden.
 */
struct dom_doc;
const struct dom_doc *script_document(const script_t *s);

/* Hat sich das Abbild seit dem letzten script_html() geändert? */
bool script_changed(const script_t *s);
/* Quelltext für die Anzeige (kmalloc); jedes Element trägt seine Nummer in data-hx. */
char *script_html(script_t *s, size_t *len);

/*
 * Ereignisse vom Benutzer. node ist die Nummer des Elements. Liefern false,
 * wenn ein Skript die übliche Wirkung unterbunden hat (preventDefault).
 */
bool script_click(script_t *s, uint32_t node);
bool script_submit(script_t *s, uint32_t form, uint32_t button);
/*
 * Ein Formularfeld wurde geändert: value NULL lässt den Text, checked -1 und
 * selected -2 lassen den Stand. Was gesetzt wird, meldet die Seite als "input";
 * finished meldet zusätzlich "change" (der Benutzer ist mit dem Feld fertig).
 */
void script_field(script_t *s, uint32_t node, const char *value, int checked, int selected,
                  bool finished);
/*
 * Ein sonstiges Ereignis: focus, blur, keydown, keypress, keyup. info nennt
 * bei Tasten die Taste ("a", "Enter", "Backspace").
 */
bool script_event(script_t *s, uint32_t node, const char *type, const char *info);
/* Führt Skripttext aus - für Links der Form javascript:... */
void script_eval(script_t *s, const char *code);
/* Sind die Skripte der Seite angehalten, weil eines eine Grenze überschritten hat? */
bool script_stopped(const script_t *s);

/* Fällige Zeitgeber ausführen; true, wenn etwas lief. wait_ms: Zeit bis zum nächsten (-1: keiner). */
bool script_tick(script_t *s, int64_t *wait_ms);

/*
 * Holt den nächsten Wunsch ab. SCRIPT_NAVIGATE: url ist die Adresse, wie das
 * Skript sie angab (noch nicht aufgelöst). SCRIPT_SUBMIT: node ist das
 * Formular, button der Knopf, der mitgesendet werden soll (oder 0).
 */
script_wish_t script_wish(script_t *s, char *url, size_t url_size, uint32_t *node, uint32_t *button);

/* Zahlen für die Statuszeile: ausgeführte Skripte, davon mit Fehler; übersprungene */
void script_counts(const script_t *s, int *ran, int *failed, int *skipped);
