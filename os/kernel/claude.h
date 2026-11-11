/*
 * Claude in HelloOS: Gespräche mit dem KI-Assistenten von Anthropic über
 * dessen Programmierschnittstelle (https://api.anthropic.com).
 *
 * Voraussetzung ist ein API-Schlüssel von console.anthropic.com. Gespräche -
 * samt den Ausgaben von Befehlen, die Claude hier ausführt - gehen an
 * Anthropic; die Nutzung wird über diesen Schlüssel abgerechnet.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct claude_chat claude_chat_t;

/* Wie die Oberfläche (Fenster oder Terminal) am Gespräch beteiligt wird. */
typedef struct {
    void (*text)(void *ctx, const char *text);      /* Claude antwortet */
    void (*note)(void *ctx, const char *text);      /* Hinweis, z. B. "führt aus: mem" */
    /* Claude möchte einen Befehl ausführen, der etwas verändert: erlauben? */
    bool (*confirm)(void *ctx, const char *command);
    void *ctx;
} claude_ui_t;

claude_chat_t *claude_chat_new(void);
void claude_chat_free(claude_chat_t *chat);
bool claude_chat_busy(const claude_chat_t *chat);

/*
 * Schickt eine Nachricht und wartet auf die Antwort; Claude kann dabei
 * HelloOS-Befehle ausführen. Bei false steht der Grund in error.
 */
bool claude_send(claude_chat_t *chat, const char *message, const claude_ui_t *ui,
                 char *error, size_t error_size);

/* --- Einstellungen (gelten für alle Gespräche) ----------------------------------- */

/* Der Schlüssel bleibt nur im Arbeitsspeicher. */
void claude_set_key(const char *key);
/* Liest ihn aus CLAUDE.KEY oder CLAUDE.TXT im Hauptverzeichnis eines USB-Sticks. */
bool claude_load_key(void);
bool claude_has_key(void);
/* Kurzform zum Anzeigen, ohne den Schlüssel preiszugeben. */
void claude_key_hint(char *out, size_t size);

const char *claude_model(void);
void claude_set_model(const char *model);
const char *claude_effort(void);
bool claude_set_effort(const char *effort);     /* low, medium, high, xhigh, max */

/* Ersetzt Zeichen, die HelloOS nicht darstellen kann (z. B. typografische
 * Anführungszeichen), an Ort und Stelle durch einfache Entsprechungen. */
void claude_plain_text(char *text);
