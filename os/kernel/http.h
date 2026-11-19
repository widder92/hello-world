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

/*
 * Für den Browser: Wer das mitgibt, bekommt Weiterleitungen und Cookies selbst
 * in die Hand - es wird dann keiner Weiterleitung von allein gefolgt.
 */
typedef struct {
    char location[2048];                /* Ziel einer Weiterleitung, wie vom Server genannt */
    char *cookies;                      /* alle Set-Cookie-Zeilen, je mit \n; kfree() */
    size_t cookies_len;
    char *headers;                      /* alle übrigen Kopfzeilen der Antwort ("Name: Wert"), je mit \n; kfree() */
    size_t headers_len;
    bool peer_private;                  /* der Server steht im eigenen Netz (oder ist dieser Rechner) */
} http_extra_t;

/* Für alles, was über ein einfaches GET hinausgeht. */
typedef struct {
    const char *method;                 /* "GET", "POST" - oder ein anderes Verfahren (HEAD, PUT, DELETE ...) */
    const char *headers;                /* zusätzliche Kopfzeilen, je mit \r\n */
    const void *body;                   /* Inhalt der Anfrage (oder NULL) */
    size_t body_len;
    size_t max_body;                    /* größte akzeptierte Antwort */
    uint32_t timeout_ms;                /* Geduld beim Warten auf Daten */
    http_extra_t *extra;                /* siehe oben; sonst NULL */
    bool keep_headers;                  /* mit extra: auch die Kopfzeilen der Antwort aufheben */
    bool public_only;                   /* keine Verbindung zu Rechnern im eigenen Netz (Router, Drucker ...):
                                           für Anfragen im Auftrag von Seiten aus dem Internet */
} http_options_t;

/* Liegt die Adresse im eigenen Netz, ist sie dieser Rechner oder sonst keine öffentliche? */
bool http_private_address(uint32_t ip);

/* Wie http_get(); Weiterleitungen werden nur bei GET verfolgt. */
bool http_request(const char *url, const http_options_t *options,
                  http_response_t *out);
