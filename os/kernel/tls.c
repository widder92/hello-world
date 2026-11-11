#include "tls.h"
#include "console.h"

/* Platzhalter - wird in der nächsten Etappe ersetzt. */

tls_t *tls_connect(tcp_conn_t *tcp, const char *host, char *error,
                   size_t error_size)
{
    (void)tcp;
    (void)host;
    ksnprintf(error, error_size, "https wird noch nicht unterstützt");
    return NULL;
}

bool tls_send(tls_t *tls, const void *data, size_t len)
{
    (void)tls;
    (void)data;
    (void)len;
    return false;
}

int tls_recv(tls_t *tls, void *buffer, size_t max, uint32_t timeout_ms)
{
    (void)tls;
    (void)buffer;
    (void)max;
    (void)timeout_ms;
    return -1;
}

void tls_close(tls_t *tls)
{
    (void)tls;
}
