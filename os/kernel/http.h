/* HTTP-Client: holt eine Datei oder Seite von einem Webserver (http und https) */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int status;                         /* z. B. 200 */
    char *body;                         /* mit kfree() freigeben; endet auf '\0' */
    size_t length;
    char content_type[64];
    char error[96];                     /* Klartext, falls etwas schiefging */
} http_response_t;

/*
 * Ruft die URL ab und folgt dabei Weiterleitungen. Liefert true, wenn eine
 * Antwort des Servers vorliegt (auch bei Status 404 o. Ä.); bei false steht
 * der Grund in out->error. Der Inhalt darf höchstens max_body Bytes haben.
 */
bool http_get(const char *url, size_t max_body, http_response_t *out);

/* Für alles, was über ein einfaches GET hinausgeht. */
typedef struct {
    const char *method;                 /* "GET" oder "POST" */
    const char *headers;                /* zusätzliche Kopfzeilen, je mit \r\n */
    const void *body;                   /* Inhalt der Anfrage (oder NULL) */
    size_t body_len;
    size_t max_body;                    /* größte akzeptierte Antwort */
    uint32_t timeout_ms;                /* Geduld beim Warten auf Daten */
} http_options_t;

/* Wie http_get(); Weiterleitungen werden nur bei GET verfolgt. */
bool http_request(const char *url, const http_options_t *options,
                  http_response_t *out);
