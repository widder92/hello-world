/*
 * Test für den HTTP-Client (kernel/http.c) auf dem Entwicklungsrechner - mit
 * einem ausgedachten Netz: Namen, Adressen und Server stehen hier im Test.
 * Geprüft wird, was hinausgeht, was zurückkommt, und vor allem die Sperre
 * "nur öffentliche Adressen" (für Anfragen im Auftrag von Seiten aus dem
 * Internet): Sie muss an der Adresse hängen, zu der wirklich verbunden wird.
 */
#include "../kernel/http.h"
#include "../kernel/net.h"
#include "../kernel/tls.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#define strlcpy libc_strlcpy
#include <string.h>
#undef strlcpy

void *kmalloc(size_t n) { return malloc(n ? n : 1); }
void *kzalloc(size_t n) { return calloc(1, n ? n : 1); }
void kfree(void *p) { free(p); }
void strlcpy(char *d, const char *s, size_t n)
{
    if (!n) return;
    size_t i = 0;
    for (; i + 1 < n && s[i]; i++) d[i] = s[i];
    d[i] = 0;
}
int ksnprintf(char *b, size_t n, const char *f, ...)
{
    va_list ap;
    va_start(ap, f);
    int r = vsnprintf(b, n, f, ap);
    va_end(ap);
    return r;
}

static int checks, failures;

static void check(bool ok, const char *what)
{
    checks++;
    if (!ok) {
        failures++;
        printf("FEHLER: %s\n", what);
    }
}

/* --- das ausgedachte Netz --------------------------------------------------------------------- */

#define OWN_IP IP4(203, 0, 113, 7)

struct tcp_conn {
    ip4_t ip;
    uint16_t port;
    char sent[8192];
    size_t sent_len;
    char reply[8192];
    size_t reply_len, reply_pos;
    bool answered;
};

static int connects;                    /* so viele Verbindungen kamen zustande */
static char connected[400];             /* wohin, der Reihe nach: "a.b.c.d:port;" */
static char last_request[8192];

ip4_t net_local_ip(void) { return OWN_IP; }

bool ip4_parse(const char *text, ip4_t *out)
{
    unsigned a, b, c, d;
    char rest;
    if (sscanf(text, "%u.%u.%u.%u%c", &a, &b, &c, &d, &rest) != 4 || a > 255 || b > 255 || c > 255 || d > 255)
        return false;
    *out = IP4(a, b, c, d);
    return true;
}

bool net_resolve(const char *name, ip4_t *out)
{
    static const struct { const char *name; ip4_t ip; } names[] = {
        { "seite.de", IP4(93, 184, 216, 34) },
        { "harmlos.de", IP4(192, 168, 1, 1) },      /* ein Name, der auf den Router zu Hause zeigt */
        { "selbst.de", OWN_IP },
        { "netz.de", IP4(100, 72, 3, 4) },
        { NULL, 0 },
    };
    if (ip4_parse(name, out))
        return true;
    for (int i = 0; names[i].name; i++) {
        if (strcmp(name, names[i].name) == 0) {
            *out = names[i].ip;
            return true;
        }
    }
    return false;
}

tcp_conn_t *tcp_connect(ip4_t ip, uint16_t port, uint32_t timeout_ms)
{
    (void)timeout_ms;
    tcp_conn_t *c = calloc(1, sizeof(*c));
    c->ip = ip;
    c->port = port;
    connects++;
    size_t used = strlen(connected);
    snprintf(connected + used, sizeof(connected) - used, "%u.%u.%u.%u:%u;", ip >> 24, (ip >> 16) & 255,
             (ip >> 8) & 255, ip & 255, port);
    return c;
}

bool tcp_send(tcp_conn_t *c, const void *data, size_t len)
{
    if (c->sent_len + len >= sizeof(c->sent))
        return false;
    memcpy(c->sent + c->sent_len, data, len);
    c->sent_len += len;
    c->sent[c->sent_len] = '\0';
    return true;
}

/* Der Server: antwortet je nach Anfragezeile */
static void answer(tcp_conn_t *c)
{
    const char *text;
    snprintf(last_request, sizeof(last_request), "%s", c->sent);
    if (strncmp(c->sent, "HEAD /seite ", 12) == 0)
        text = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 5\r\nX-Eins: a\r\n\r\n";
    else if (strncmp(c->sent, "GET /seite ", 11) == 0)
        text = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: 5\r\n"
               "X-Eins: a\r\nSet-Cookie: sitzung=1\r\nX-Zwei:  b \r\n\r\nhallo";
    else if (strncmp(c->sent, "GET /zum-router ", 16) == 0)
        text = "HTTP/1.1 302 Found\r\nLocation: http://192.168.1.1/geheim\r\nContent-Length: 0\r\n\r\n";
    else if (strncmp(c->sent, "GET /zum-namen ", 15) == 0)
        text = "HTTP/1.1 301 Moved\r\nLocation: http://harmlos.de/geheim\r\nContent-Length: 0\r\n\r\n";
    else if (strncmp(c->sent, "GET /geheim ", 12) == 0)
        text = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 6\r\n\r\nGEHEIM";
    else if (strncmp(c->sent, "DELETE /ding ", 13) == 0)
        text = "HTTP/1.1 204 No Content\r\nX-Weg: ja\r\n\r\n";
    else if (strncmp(c->sent, "PUT /ding ", 10) == 0)
        text = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n2\r\nde\r\n0\r\n\r\n";
    else
        text = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
    c->reply_len = strlen(text);
    memcpy(c->reply, text, c->reply_len);
    c->answered = true;
}

int tcp_recv(tcp_conn_t *c, void *buffer, size_t max, uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (!c->answered)
        answer(c);
    size_t n = c->reply_len - c->reply_pos;
    if (n > max)
        n = max;
    if (n > 7)
        n = 7;                          /* in kleinen Stücken, wie es im Netz auch kommt */
    memcpy(buffer, c->reply + c->reply_pos, n);
    c->reply_pos += n;
    return (int)n;
}

void tcp_close(tcp_conn_t *c) { free(c); }

/* Verschlüsselung: hier durchsichtig */
struct tls { tcp_conn_t *tcp; };
tls_t *tls_connect(tcp_conn_t *tcp, const char *host, char *error, size_t error_size)
{
    (void)host; (void)error; (void)error_size;
    tls_t *t = calloc(1, sizeof(*t));
    t->tcp = tcp;
    return t;
}
bool tls_send(tls_t *t, const void *data, size_t len) { return tcp_send(t->tcp, data, len); }
int tls_recv(tls_t *t, void *buffer, size_t max, uint32_t timeout_ms) { return tcp_recv(t->tcp, buffer, max, timeout_ms); }
void tls_close(tls_t *t) { free(t); }

static void reset(void)
{
    connects = 0;
    connected[0] = '\0';
    last_request[0] = '\0';
}

int main(void)
{
    http_response_t r;
    http_extra_t extra;

    /* --- Adressen: was als "eigenes Netz" gilt --- */
    static const struct { ip4_t ip; bool private; } addresses[] = {
        { IP4(10, 0, 0, 1), true }, { IP4(10, 255, 255, 255), true }, { IP4(127, 0, 0, 1), true },
        { IP4(127, 9, 9, 9), true }, { IP4(0, 0, 0, 0), true }, { IP4(169, 254, 1, 1), true },
        { IP4(172, 16, 0, 1), true }, { IP4(172, 31, 255, 255), true }, { IP4(192, 168, 0, 1), true },
        { IP4(192, 168, 178, 1), true }, { IP4(100, 64, 0, 1), true }, { IP4(100, 127, 255, 255), true },
        { IP4(224, 0, 0, 1), true }, { IP4(255, 255, 255, 255), true }, { OWN_IP, true },
        { IP4(9, 255, 255, 255), false }, { IP4(11, 0, 0, 0), false }, { IP4(172, 15, 255, 255), false },
        { IP4(172, 32, 0, 0), false }, { IP4(192, 167, 1, 1), false }, { IP4(192, 169, 1, 1), false },
        { IP4(100, 63, 255, 255), false }, { IP4(100, 128, 0, 0), false }, { IP4(169, 253, 1, 1), false },
        { IP4(126, 255, 255, 255), false }, { IP4(128, 0, 0, 1), false }, { IP4(93, 184, 216, 34), false },
        { IP4(223, 255, 255, 255), false }, { IP4(8, 8, 8, 8), false }, { IP4(203, 0, 113, 8), false },
    };
    bool right = true;
    for (size_t i = 0; i < sizeof(addresses) / sizeof(addresses[0]); i++) {
        if (http_private_address(addresses[i].ip) != addresses[i].private) {
            right = false;
            printf("  Adresse %u.%u.%u.%u falsch eingeordnet\n", addresses[i].ip >> 24, (addresses[i].ip >> 16) & 255,
                   (addresses[i].ip >> 8) & 255, addresses[i].ip & 255);
        }
    }
    check(right, "Einordnung der Adressen: eigenes Netz oder öffentlich");

    /* --- gewöhnliche Anfragen --- */
    reset();
    check(http_get("http://seite.de/seite", 1000, &r) && r.status == 200 && r.length == 5 &&
          strcmp(r.body, "hallo") == 0 && strncmp(r.content_type, "text/html", 9) == 0,
          "GET: Status, Inhalt, Art");
    check(strcmp(connected, "93.184.216.34:80;") == 0 && strstr(last_request, "Host: seite.de\r\n") &&
          strstr(last_request, "Connection: close\r\n\r\n"), "GET: eine Verbindung, ordentliche Anfrage");
    kfree(r.body);

    reset();
    http_options_t with_headers = { .method = "GET", .max_body = 1000, .timeout_ms = 1000, .extra = &extra,
                                    .keep_headers = true, .headers = "X-Frage: 1\r\n" };
    check(http_request("https://seite.de:8443/seite", &with_headers, &r) && r.status == 200 &&
          strcmp(connected, "93.184.216.34:8443;") == 0 && strstr(last_request, "Host: seite.de:8443\r\n") &&
          strstr(last_request, "X-Frage: 1\r\n"), "https mit Port und eigener Kopfzeile");
    check(extra.headers && strstr(extra.headers, "X-Eins: a\n") && strstr(extra.headers, "X-Zwei:  b \n") &&
          strstr(extra.headers, "Content-Type: text/html; charset=utf-8\n") && !strstr(extra.headers, "sitzung") &&
          !strchr(extra.headers, '\r'), "Kopfzeilen der Antwort werden aufgehoben - ohne die Cookies");
    check(extra.cookies && strcmp(extra.cookies, "sitzung=1\n") == 0 && !extra.peer_private,
          "Cookies getrennt davon; der Server gilt als öffentlich");
    kfree(r.body);
    kfree(extra.headers);
    kfree(extra.cookies);

    reset();
    http_options_t plain = { .method = "GET", .max_body = 1000, .timeout_ms = 1000, .extra = &extra };
    check(http_request("http://seite.de/seite", &plain, &r) && !extra.headers && extra.cookies,
          "ohne Wunsch werden keine Kopfzeilen aufgehoben");
    kfree(r.body);
    kfree(extra.cookies);

    reset();
    http_options_t head = { .method = "HEAD", .max_body = 1000, .timeout_ms = 1000, .extra = &extra, .keep_headers = true };
    check(http_request("http://seite.de/seite", &head, &r) && r.status == 200 && r.length == 0 &&
          strncmp(last_request, "HEAD /seite HTTP/1.1\r\n", 22) == 0 && strstr(extra.headers, "Content-Length: 5\n"),
          "HEAD: Antwort ohne Inhalt, obwohl eine Länge genannt ist");
    kfree(r.body);
    kfree(extra.headers);

    reset();
    http_options_t del = { .method = "DELETE", .max_body = 1000, .timeout_ms = 1000, .extra = &extra, .keep_headers = true };
    check(http_request("http://seite.de/ding", &del, &r) && r.status == 204 && r.length == 0 &&
          strstr(extra.headers, "X-Weg: ja\n"), "DELETE: Antwort 204 ohne Inhalt");
    kfree(r.body);
    kfree(extra.headers);

    reset();
    http_options_t put = { .method = "PUT", .max_body = 1000, .timeout_ms = 1000, .body = "neu!", .body_len = 4,
                           .headers = "Content-Type: text/plain\r\n" };
    check(http_request("http://seite.de/ding", &put, &r) && r.status == 200 && strcmp(r.body, "abcde") == 0 &&
          strstr(last_request, "Content-Length: 4\r\n") && strstr(last_request, "\r\n\r\nneu!"),
          "PUT: Inhalt geht mit; Antwort in Stücken (chunked)");
    kfree(r.body);

    /* --- nur öffentliche Adressen --- */
    http_options_t public_only = { .method = "GET", .max_body = 1000, .timeout_ms = 1000, .public_only = true };
    reset();
    check(http_request("http://seite.de/seite", &public_only, &r) && r.status == 200 && connects == 1,
          "nur öffentlich: Ein Server im Internet ist erreichbar");
    kfree(r.body);
    static const char *const refused[] = {
        "http://192.168.1.1/geheim", "http://10.0.0.138/geheim", "http://127.0.0.1:8080/geheim",
        "http://harmlos.de/geheim", "https://harmlos.de/geheim", "http://selbst.de/geheim",
        "http://netz.de/geheim", "http://169.254.169.254/geheim", "http://0.0.0.0/geheim", NULL,
    };
    for (int i = 0; refused[i]; i++) {
        reset();
        memset(&r, 0, sizeof(r));
        bool got = http_request(refused[i], &public_only, &r);
        char what[200];
        snprintf(what, sizeof(what), "nur öffentlich: keine Verbindung zu %s", refused[i]);
        check(!got && connects == 0 && !r.body && strstr(r.error, "eigenen Netz"), what);
        kfree(r.body);
    }
    reset();
    check(!http_request("http://seite.de/zum-router", &public_only, &r) && strcmp(connected, "93.184.216.34:80;") == 0,
          "nur öffentlich: auch nicht über eine Weiterleitung auf eine Adresse im eigenen Netz");
    kfree(r.body);
    reset();
    check(!http_request("http://seite.de/zum-namen", &public_only, &r) && strcmp(connected, "93.184.216.34:80;") == 0,
          "nur öffentlich: auch nicht über eine Weiterleitung auf einen Namen, der dorthin zeigt");
    kfree(r.body);

    /* --- ohne die Sperre (Adresszeile, Seiten aus dem eigenen Netz) geht es wie immer --- */
    reset();
    check(http_request("http://harmlos.de/geheim", &plain, &r) && r.status == 200 && strcmp(r.body, "GEHEIM") == 0 &&
          extra.peer_private && strcmp(connected, "192.168.1.1:80;") == 0,
          "ohne Sperre: Der Router ist erreichbar - und als \"eigenes Netz\" gemeldet");
    kfree(r.body);
    kfree(extra.cookies);
    reset();
    check(http_get("http://seite.de/zum-router", 1000, &r) && r.status == 200 && strcmp(r.body, "GEHEIM") == 0 &&
          strcmp(connected, "93.184.216.34:80;192.168.1.1:80;") == 0, "ohne Sperre: Weiterleitungen dorthin werden verfolgt");
    kfree(r.body);

    printf("%d Prüfungen am HTTP-Client, %d Fehler\n", checks, failures);
    return failures ? 1 : 0;
}
