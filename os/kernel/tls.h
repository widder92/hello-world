/*
 * TLS 1.3: verschlüsselte Verbindungen (das "s" in https).
 *
 * Der Server muss sich mit einem Zertifikat ausweisen, das zum Namen passt,
 * gültig ist und auf eine der eingebauten Zertifizierungsstellen zurückgeht
 * (siehe x509.h). Sonst kommt keine Verbindung zustande.
 */
#pragma once

#include "net.h"

typedef struct tls tls_t;

/* Meldet die eingebauten Wurzelzertifikate an (einmal beim Start). */
void tls_init(void);

/*
 * Baut über eine bestehende TCP-Verbindung eine TLS-Sitzung auf. Bei einem
 * Fehler: NULL und eine Erklärung in error. Die TCP-Verbindung bleibt Sache
 * des Aufrufers.
 */
tls_t *tls_connect(tcp_conn_t *tcp, const char *host, char *error,
                   size_t error_size);
bool tls_send(tls_t *tls, const void *data, size_t len);

/* Wie tcp_recv(): Anzahl Bytes, 0 bei ordentlichem Ende, -1 bei Fehler. */
int tls_recv(tls_t *tls, void *buffer, size_t max, uint32_t timeout_ms);
void tls_close(tls_t *tls);
