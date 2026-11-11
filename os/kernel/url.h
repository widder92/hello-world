/* Web-Adressen: zerlegen, relative Verweise auflösen, Formulardaten kodieren */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#define URL_SIZE 2048

/*
 * Macht aus einem Verweis einer Seite (href="../bild.png", "/pfad", "?seite=2",
 * "https://...") eine vollständige Adresse. Der Teil hinter '#' entfällt.
 * Liefert false für alles, was keine Web-Adresse ist (mailto:, javascript: ...).
 */
bool url_resolve(const char *base, const char *reference, char *out, size_t size);

/* Zerlegt eine vollständige Adresse; host in Kleinbuchstaben, ohne Port. */
bool url_split(const char *url, bool *secure, char *host, size_t host_size,
               const char **path);

/* Hängt text prozentkodiert an (für Formulare: Leerzeichen als '+'). */
size_t url_encode(char *out, size_t size, const char *text);
