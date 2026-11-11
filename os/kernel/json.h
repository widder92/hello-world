/*
 * JSON lesen und schreiben - gerade so viel, wie die Claude-Anbindung braucht.
 *
 * Gelesen wird ohne Zwischenbaum: Ein json_t bezeichnet einfach den
 * Textabschnitt eines Wertes; die Funktionen suchen darin weiter.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    const char *p, *end;                /* p == NULL: kein Wert */
} json_t;

/* Prüft den Text und liefert den äußersten Wert. */
bool json_parse(const char *text, size_t len, json_t *out);

/* Art des Wertes: '{' Objekt, '[' Liste, '"' Text, 'n' Zahl, 't'/'f', '0' null */
char json_type(json_t value);

/* Sucht in einem Objekt den Wert zu einem Schlüssel. */
bool json_get(json_t object, const char *key, json_t *out);

/* Geht die Elemente einer Liste durch; *cursor anfangs auf NULL setzen. */
bool json_each(json_t array, const char **cursor, json_t *element);

/*
 * Wandelt einen JSON-Text in gewöhnlichen Text (UTF-8) um und löst dabei
 * Schreibweisen wie \n oder ä auf. Liefert die Länge; bei zu kleinem
 * Puffer wird abgeschnitten.
 */
size_t json_string(json_t value, char *out, size_t size);
bool json_string_equals(json_t value, const char *text);

/* --- Schreiben: ein Textpuffer, der bei Bedarf wächst ---------------------------- */

typedef struct {
    char *data;                         /* endet immer auf '\0'; mit kfree() freigeben */
    size_t len, capacity;
    bool failed;                        /* Speicher ging aus */
} strbuf_t;

void sb_append(strbuf_t *sb, const char *text);
void sb_append_n(strbuf_t *sb, const char *text, size_t len);
/* Hängt text als JSON-Zeichenkette an: in Anführungszeichen, Sonderzeichen maskiert. */
void sb_append_json(strbuf_t *sb, const char *text);
void sb_truncate(strbuf_t *sb, size_t len);
void sb_free(strbuf_t *sb);
