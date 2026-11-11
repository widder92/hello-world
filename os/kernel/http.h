/* HTTP-Client: holt eine Datei oder Seite von einem Webserver (http und https) */
#pragma once

#include <stdbool.h>
#include <stddef.h>

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
