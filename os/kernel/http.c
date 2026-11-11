#include "http.h"
#include "console.h"
#include "memory.h"
#include "net.h"
#include "shell.h"
#include "string.h"
#include "tls.h"

#define URL_MAX       2048
#define HOST_MAX      128
#define LINE_MAX      2304
#define REDIRECTS_MAX 5
#define TIMEOUT_MS    15000

/* --- Verbindung: wahlweise direkt über TCP oder verschlüsselt über TLS --------- */

typedef struct {
    tcp_conn_t *tcp;
    tls_t *tls;
    uint8_t buffer[4096];               /* Lesepuffer */
    size_t pos, len;
    uint32_t timeout_ms;
} stream_t;

static bool stream_open(stream_t *s, const char *host, uint16_t port,
                        bool secure, char *error, size_t error_size)
{
    ip4_t ip;
    memset(s, 0, sizeof(*s));
    if (!net_resolve(host, &ip)) {
        ksnprintf(error, error_size, "Name nicht auflösbar: %s", host);
        return false;
    }
    s->tcp = tcp_connect(ip, port, 8000);
    if (!s->tcp) {
        ksnprintf(error, error_size, "Keine Verbindung zu %s", host);
        return false;
    }
    if (secure) {
        s->tls = tls_connect(s->tcp, host, error, error_size);
        if (!s->tls) {
            tcp_close(s->tcp);
            s->tcp = NULL;
            return false;
        }
    }
    return true;
}

static bool stream_write(stream_t *s, const void *data, size_t len)
{
    return s->tls ? tls_send(s->tls, data, len) : tcp_send(s->tcp, data, len);
}

/* Füllt den Lesepuffer nach; false bei Verbindungsende oder Fehler. */
static bool stream_fill(stream_t *s)
{
    int n = s->tls ? tls_recv(s->tls, s->buffer, sizeof(s->buffer), s->timeout_ms)
                   : tcp_recv(s->tcp, s->buffer, sizeof(s->buffer), s->timeout_ms);
    s->pos = 0;
    s->len = n > 0 ? (size_t)n : 0;
    return n > 0;
}

static void stream_close(stream_t *s)
{
    if (s->tls)
        tls_close(s->tls);
    if (s->tcp)
        tcp_close(s->tcp);
    s->tls = NULL;
    s->tcp = NULL;
}

/* Liest eine Zeile ohne das abschließende CRLF. */
static bool read_line(stream_t *s, char *line, size_t size)
{
    size_t n = 0;
    for (;;) {
        if (s->pos == s->len && !stream_fill(s))
            return false;
        char c = (char)s->buffer[s->pos++];
        if (c == '\n')
            break;
        if (c != '\r' && n + 1 < size)
            line[n++] = c;
    }
    line[n] = '\0';
    return true;
}

/* --- Inhalt einsammeln ------------------------------------------------------------ */

typedef struct {
    char *data;
    size_t length, capacity, limit;
} body_t;

static bool body_append(body_t *b, const uint8_t *data, size_t len)
{
    if (b->length + len > b->limit)
        return false;
    if (b->length + len + 1 > b->capacity) {
        size_t capacity = b->capacity ? b->capacity * 2 : 16384;
        while (capacity < b->length + len + 1)
            capacity *= 2;
        char *grown = kmalloc(capacity);
        if (!grown)
            return false;
        if (b->length)
            memcpy(grown, b->data, b->length);
        kfree(b->data);
        b->data = grown;
        b->capacity = capacity;
    }
    memcpy(b->data + b->length, data, len);
    b->length += len;
    b->data[b->length] = '\0';
    return true;
}

/* Liest genau count Bytes (oder bis zum Verbindungsende, wenn count < 0). */
static bool read_body(stream_t *s, body_t *b, long long count)
{
    while (count != 0) {
        if (s->pos == s->len && !stream_fill(s))
            return count < 0;           /* Ende ist nur ohne Längenangabe ok */
        size_t n = s->len - s->pos;
        if (count > 0 && (long long)n > count)
            n = (size_t)count;
        if (!body_append(b, s->buffer + s->pos, n))
            return false;
        s->pos += n;
        if (count > 0)
            count -= (long long)n;
    }
    return true;
}

static bool read_chunked(stream_t *s, body_t *b, char *line)
{
    for (;;) {
        if (!read_line(s, line, LINE_MAX))
            return false;
        long long size = 0;
        for (const char *p = line; ; p++) {
            int digit;
            if (*p >= '0' && *p <= '9')
                digit = *p - '0';
            else if ((*p | 0x20) >= 'a' && (*p | 0x20) <= 'f')
                digit = (*p | 0x20) - 'a' + 10;
            else
                break;
            size = size * 16 + digit;
        }
        if (size == 0)
            return true;
        if (!read_body(s, b, size) || !read_line(s, line, LINE_MAX))
            return false;
    }
}

/* --- URLs und Kopfzeilen ------------------------------------------------------------ */

typedef struct {
    bool secure;
    char host[HOST_MAX];
    uint16_t port;
    const char *path;                   /* zeigt in die URL */
} url_t;

static bool starts_with(const char *text, const char *prefix)
{
    while (*prefix) {
        if (*text++ != *prefix++)
            return false;
    }
    return true;
}

static bool parse_url(const char *url, url_t *out)
{
    if (starts_with(url, "https://")) {
        out->secure = true;
        out->port = 443;
        url += 8;
    } else if (starts_with(url, "http://")) {
        out->secure = false;
        out->port = 80;
        url += 7;
    } else {
        return false;
    }

    size_t n = 0;
    while (*url && *url != '/' && *url != ':' && *url != '?') {
        if (n + 1 >= HOST_MAX)
            return false;
        out->host[n++] = *url++;
    }
    out->host[n] = '\0';
    if (n == 0)
        return false;

    if (*url == ':') {
        uint32_t port = 0;
        for (url++; *url >= '0' && *url <= '9'; url++)
            port = port * 10 + (uint32_t)(*url - '0');
        if (port == 0 || port > 65535)
            return false;
        out->port = (uint16_t)port;
    }
    out->path = url;                    /* "", "/..." oder "?..." */
    return true;
}

/* Vergleicht den Namen einer Kopfzeile ohne Rücksicht auf Groß/Klein. */
static const char *header_value(const char *line, const char *name)
{
    while (*name) {
        if ((*line | 0x20) != (*name | 0x20))
            return NULL;
        line++;
        name++;
    }
    if (*line != ':')
        return NULL;
    line++;
    while (*line == ' ' || *line == '\t')
        line++;
    return line;
}

/* --- Eine Anfrage ---------------------------------------------------------------------- */

/* Liefert true bei einer vollständigen Antwort; location wird bei Weiterleitung gefüllt. */
static bool request(const char *url, const http_options_t *options,
                    http_response_t *out, char *location)
{
    size_t max_body = options->max_body;
    url_t u;
    if (!parse_url(url, &u)) {
        strlcpy(out->error, "Ungültige Adresse (http:// oder https:// ...)",
                sizeof(out->error));
        return false;
    }

    stream_t *s = kmalloc(sizeof(*s));
    char *line = kmalloc(LINE_MAX);
    body_t body = { NULL, 0, 0, max_body };
    bool ok = false;
    if (!s || !line) {
        strlcpy(out->error, "Nicht genug Speicher", sizeof(out->error));
        goto done;
    }
    if (!stream_open(s, u.host, u.port, u.secure, out->error, sizeof(out->error)))
        goto done;
    s->timeout_ms = options->timeout_ms;

    /* Anfrage senden */
    const char *path = u.path;
    char port[8] = "";
    if (u.port != (u.secure ? 443 : 80))
        ksnprintf(port, sizeof(port), ":%u", u.port);
    char length[48] = "";
    if (options->body)
        ksnprintf(length, sizeof(length), "Content-Length: %zu\r\n",
                  options->body_len);
    int n = ksnprintf(line, LINE_MAX,
                      "%s %s%s HTTP/1.1\r\n"
                      "Host: %s%s\r\n"
                      "User-Agent: " OS_NAME "/" OS_VERSION "\r\n"
                      "Accept: */*\r\n"
                      "%s%s"
                      "Connection: close\r\n\r\n",
                      options->method, *path == '/' ? "" : "/", path, u.host,
                      port, options->headers ? options->headers : "", length);
    if (n >= LINE_MAX || !stream_write(s, line, (size_t)n) ||
        (options->body && options->body_len &&
         !stream_write(s, options->body, options->body_len))) {
        strlcpy(out->error, "Anfrage konnte nicht gesendet werden",
                sizeof(out->error));
        goto close;
    }

    /* Statuszeile: "HTTP/1.1 200 OK" */
    if (!read_line(s, line, LINE_MAX) || !starts_with(line, "HTTP/")) {
        strlcpy(out->error, "Keine gültige Antwort vom Server",
                sizeof(out->error));
        goto close;
    }
    const char *p = line;
    while (*p && *p != ' ')
        p++;
    out->status = 0;
    for (p += *p == ' '; *p >= '0' && *p <= '9'; p++)
        out->status = out->status * 10 + (*p - '0');

    /* Kopfzeilen */
    long long content_length = -1;
    bool chunked = false;
    location[0] = '\0';
    for (;;) {
        if (!read_line(s, line, LINE_MAX)) {
            strlcpy(out->error, "Verbindung während der Antwort abgebrochen",
                    sizeof(out->error));
            goto close;
        }
        if (line[0] == '\0')
            break;
        const char *value;
        if ((value = header_value(line, "content-length")) != NULL) {
            content_length = 0;
            for (; *value >= '0' && *value <= '9'; value++)
                content_length = content_length * 10 + (*value - '0');
        } else if ((value = header_value(line, "transfer-encoding")) != NULL) {
            chunked = starts_with(value, "chunked");
        } else if ((value = header_value(line, "content-type")) != NULL) {
            strlcpy(out->content_type, value, sizeof(out->content_type));
        } else if ((value = header_value(line, "location")) != NULL) {
            strlcpy(location, value, URL_MAX);
        }
    }

    bool redirect = out->status == 301 || out->status == 302 ||
                    out->status == 303 || out->status == 307 ||
                    out->status == 308;
    if (redirect && location[0]) {
        /* Relative Ziele ("/pfad") auf denselben Server beziehen */
        if (location[0] == '/') {
            char *absolute = kmalloc(URL_MAX);
            if (absolute) {
                ksnprintf(absolute, URL_MAX, "%s://%s:%u%s",
                          u.secure ? "https" : "http", u.host, u.port, location);
                strlcpy(location, absolute, URL_MAX);
                kfree(absolute);
            }
        }
        ok = true;
        goto close;
    }
    location[0] = '\0';

    bool complete;
    if (out->status == 204 || out->status == 304)
        complete = true;
    else if (chunked)
        complete = read_chunked(s, &body, line);
    else
        complete = read_body(s, &body, content_length);
    if (!complete) {
        if (body.length >= max_body || content_length > (long long)max_body)
            strlcpy(out->error, "Inhalt ist zu groß", sizeof(out->error));
        else
            strlcpy(out->error, "Übertragung unvollständig", sizeof(out->error));
        goto close;
    }
    if (!body.data && !body_append(&body, (const uint8_t *)"", 0)) {
        strlcpy(out->error, "Nicht genug Speicher", sizeof(out->error));
        goto close;
    }
    out->body = body.data;
    out->length = body.length;
    body.data = NULL;
    ok = true;

close:
    stream_close(s);
done:
    kfree(body.data);
    kfree(line);
    kfree(s);
    return ok;
}

bool http_get(const char *url, size_t max_body, http_response_t *out)
{
    http_options_t options = {
        .method = "GET", .max_body = max_body, .timeout_ms = TIMEOUT_MS,
    };
    return http_request(url, &options, out);
}

bool http_request(const char *url, const http_options_t *options,
                  http_response_t *out)
{
    bool follow = strcmp(options->method, "GET") == 0;
    char *current = kmalloc(URL_MAX);
    char *location = kmalloc(URL_MAX);
    bool ok = false;

    memset(out, 0, sizeof(*out));
    if (!current || !location) {
        strlcpy(out->error, "Nicht genug Speicher", sizeof(out->error));
        goto done;
    }
    if (strlen(url) >= URL_MAX) {
        strlcpy(out->error, "Adresse ist zu lang", sizeof(out->error));
        goto done;
    }
    strlcpy(current, url, URL_MAX);

    for (int hop = 0; hop <= REDIRECTS_MAX; hop++) {
        if (!request(current, options, out, location))
            goto done;
        if (!location[0] || !follow) {
            ok = true;
            goto done;
        }
        strlcpy(current, location, URL_MAX);    /* Weiterleitung folgen */
    }
    strlcpy(out->error, "Zu viele Weiterleitungen", sizeof(out->error));

done:
    kfree(current);
    kfree(location);
    return ok;
}
