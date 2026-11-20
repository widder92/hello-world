/*
 * Das veränderbare Abbild einer Webseite (DOM): der Quelltext als Baum aus
 * Elementen und Texten, den Skripte lesen und umbauen können. Aus dem Baum
 * entsteht wieder Quelltext - für die Anzeige (html.c setzt weiterhin Text)
 * und für innerHTML.
 *
 * Elemente, die aus dem Baum genommen werden, bleiben bis zum Ende der Seite
 * bestehen: So kann kein Verweis eines Skripts je ins Leere zeigen. Dafür
 * gibt es Obergrenzen für Knoten und Speicher.
 *
 * Reine Datenstruktur ohne Hardware und ohne Skripte; läuft auch im Test auf
 * dem Entwicklungsrechner (tests/dom_test.c).
 */
#pragma once

#include "css.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { DOM_ELEMENT = 1, DOM_TEXT = 3, DOM_COMMENT = 8, DOM_DOCUMENT = 9, DOM_FRAGMENT = 11 };

typedef struct {
    char *name;                         /* klein geschrieben */
    char *value;
} dom_attr;

typedef struct dom_doc dom_doc;

typedef struct dom_node {
    css_node_t css;                     /* muss vorn stehen: So findet css.c den Knoten wieder */
    uint8_t type;
    bool raw;                           /* Text von <script> und <style>: wird nicht maskiert */
    uint32_t id;                        /* eindeutig in der Seite; verbindet Anzeige und Baum */
    dom_doc *doc;
    struct dom_node *parent, *first, *last, *prev, *next;
    char *name;                         /* Elementname, klein geschrieben */
    char *text;                         /* Inhalt von Text und Kommentar */
    uint32_t text_len;
    dom_attr *attrs;
    uint16_t attr_count, attr_cap;
    /* Formularfelder: was der Benutzer oder ein Skript eingetragen hat */
    char *value;
    bool has_value;
    int8_t checked;                     /* -1: wie im Quelltext */
    int32_t selected;                   /* Auswahllisten: gewählter Eintrag; -2: wie im Quelltext */
    void *wrapper;                      /* das zugehörige Objekt für Skripte */
} dom_node;

struct dom_doc {
    dom_node *root;                     /* der Dokumentknoten */
    dom_node **nodes;                   /* Nummer -> Knoten */
    uint32_t count, cap;
    size_t bytes;
    bool dirty;                         /* seit dem letzten dom_serialize() geändert */
    uint32_t version;                   /* zählt jede Änderung: So lässt sich erkennen, ob etwas noch gilt */
    /*
     * Maus und Fokus - der Browser trägt ein, worüber der Mauszeiger steht und
     * welches Feld gerade bedient wird; danach fragen Regeln mit :hover und :focus.
     */
    struct dom_node *hover, *focus;
    bool failed;                        /* eine Grenze wurde erreicht */
};

dom_doc *dom_parse(const char *html, size_t len);
void dom_free(dom_doc *doc);
dom_node *dom_node_by_id(const dom_doc *doc, uint32_t id);

/* Bauen und Umbauen; false bzw. NULL bei Speichermangel oder Unzulässigem */
dom_node *dom_create_element(dom_doc *doc, const char *name, size_t name_len);
dom_node *dom_create_text(dom_doc *doc, const char *text, size_t len);
dom_node *dom_create_comment(dom_doc *doc, const char *text, size_t len);
dom_node *dom_create_fragment(dom_doc *doc);
/* Hängt child vor ref ein (ref NULL: ans Ende); ein Fragment gibt seine Kinder ab. */
bool dom_insert(dom_node *parent, dom_node *child, dom_node *ref);
void dom_remove(dom_node *child);
dom_node *dom_clone(dom_node *node, bool deep);
bool dom_contains(const dom_node *ancestor, const dom_node *node);

const char *dom_attr_get(const dom_node *node, const char *name);
bool dom_attr_set(dom_node *node, const char *name, const char *value, size_t value_len);
void dom_attr_remove(dom_node *node, const char *name);

/* Text des Knotens samt allem darunter (kmalloc); setzen ersetzt den Inhalt. */
char *dom_text_content(const dom_node *node, size_t *len);
bool dom_set_text(dom_node *node, const char *text, size_t len);
/* Formularfelder: der gerade geltende Inhalt (kmalloc) */
char *dom_field_value(const dom_node *node, size_t *len);
bool dom_set_field_value(dom_node *node, const char *text, size_t len);

/*
 * Quelltext erzeugen (kmalloc). inner: nur der Inhalt, sonst mit dem Element
 * selbst. dom_serialize liefert die ganze Seite für die Anzeige: mit den
 * Nummern der Elemente (Attribut data-hx), dem Stand der Formularfelder und
 * ohne Skripte.
 */
char *dom_html(const dom_node *node, bool inner, size_t *len);
char *dom_serialize(dom_doc *doc, size_t *len);
/* Ersetzt den Inhalt eines Elements durch gelesenen Quelltext. */
bool dom_set_inner_html(dom_node *node, const char *html, size_t len);
/* Liest Quelltext in ein neues Fragment (für insertAdjacentHTML und document.write). */
dom_node *dom_parse_fragment(dom_doc *doc, const char *html, size_t len);

/* Suchen: das erste passende Element unter root (in Dokumentreihenfolge) nach start */
dom_node *dom_next(const dom_node *root, const dom_node *node);     /* nächster Knoten im Baum */
dom_node *dom_find_id(const dom_node *root, const char *id);
bool dom_is(const dom_node *node, const char *name);
