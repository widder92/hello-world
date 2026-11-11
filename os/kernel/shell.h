/* Die Kommandozeile von HelloOS */
#pragma once

#define OS_NAME    "HelloOS"
#define OS_VERSION "0.8"

#include <stdbool.h>
#include <stddef.h>

/*
 * Führt eine Shell auf der Konsole des laufenden Tasks aus. Kehrt zurück,
 * wenn die Konsole geschlossen wird.
 */
void shell_run(void);

/* --- Für den eingebauten Assistenten (claude.c) ------------------------------- */

typedef enum {
    SHELL_SAFE,                         /* liest nur oder ist harmlos */
    SHELL_ASK,                          /* verändert etwas: vorher nachfragen */
    SHELL_FORBIDDEN,                    /* darf der Assistent nie ausführen */
    SHELL_UNKNOWN,                      /* kein solcher Befehl */
} shell_risk_t;

/* Wie heikel wäre es, diese Befehlszeile auszuführen? */
shell_risk_t shell_command_risk(const char *line);

/*
 * Führt eine Befehlszeile aus und fängt ihre Ausgabe als Text ein, statt sie
 * anzuzeigen. Liefert die Länge der (evtl. gekürzten) Ausgabe.
 */
size_t shell_run_captured(const char *line, char *out, size_t size);

/* Befehl Nummer index mit Aufruf und Beschreibung; false am Ende der Liste. */
bool shell_command_info(int index, const char **usage, const char **help);
