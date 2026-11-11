#include "url.h"
#include "string.h"

static bool starts_with(const char *text, const char *prefix)
{
    while (*prefix) {
        char c = *text++;
        if (c >= 'A' && c <= 'Z')
            c = (char)(c + 32);
        if (c != *prefix++)
            return false;
    }
    return true;
}

/* Länge von "schema://rechner[:port]" am Anfang einer vollständigen Adresse. */
static size_t origin_length(const char *url)
{
    size_t n = starts_with(url, "https://") ? 8 : starts_with(url, "http://") ? 7 : 0;
    if (n == 0)
        return 0;
    while (url[n] && url[n] != '/' && url[n] != '?' && url[n] != '#')
        n++;
    return n;
}

bool url_split(const char *url, bool *secure, char *host, size_t host_size,
               const char **path)
{
    size_t origin = origin_length(url);
    if (origin == 0)
        return false;
    *secure = starts_with(url, "https://");
    const char *p = url + (*secure ? 8 : 7);
    size_t n = 0;
    while (p < url + origin && *p != ':' && n + 1 < host_size) {
        char c = *p++;
        host[n++] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
    }
    host[n] = '\0';
    *path = url[origin] == '/' ? url + origin : "/";
    return n > 0;
}

/* Entfernt "." und ".." aus dem Pfadteil ab path (bis zum '?'). */
static void normalize_path(char *path)
{
    char *query = path;
    while (*query && *query != '?')
        query++;
    char saved[URL_SIZE];
    strlcpy(saved, query, sizeof(saved));
    *query = '\0';

    char *out = path;
    const char *in = path;
    while (*in) {
        /* in zeigt auf '/' am Anfang eines Abschnitts */
        const char *segment = in + 1;
        size_t len = 0;
        while (segment[len] && segment[len] != '/')
            len++;
        if (len == 1 && segment[0] == '.') {
            if (!segment[len])
                *out++ = '/';
        } else if (len == 2 && segment[0] == '.' && segment[1] == '.') {
            while (out > path && *--out != '/')
                ;
            if (!segment[len])
                *out++ = '/';
        } else {
            *out++ = '/';
            memmove(out, segment, len);
            out += len;
        }
        in = segment + len;
    }
    if (out == path)
        *out++ = '/';
    size_t room = URL_SIZE - (size_t)(out - path);
    strlcpy(out, saved, room < sizeof(saved) ? room : sizeof(saved));
}

bool url_resolve(const char *base, const char *reference, char *out, size_t size)
{
    char ref[URL_SIZE];
    size_t n = 0;

    /* Leerraum am Rand weg, Steuerzeichen raus, Leerzeichen kodieren, '#...' abschneiden */
    while (*reference == ' ' || *reference == '\t' || *reference == '\n' ||
           *reference == '\r')
        reference++;
    for (const char *p = reference; *p && *p != '#' && n + 4 < sizeof(ref); p++) {
        unsigned char c = (unsigned char)*p;
        if (c < 32 || c == 127)
            continue;
        if (c == ' ') {
            ref[n++] = '%';
            ref[n++] = '2';
            ref[n++] = '0';
        } else {
            ref[n++] = (char)c;
        }
    }
    while (n >= 3 && ref[n - 3] == '%' && ref[n - 2] == '2' && ref[n - 1] == '0')
        n -= 3;                         /* Leerzeichen am Ende */
    ref[n] = '\0';

    if (size < URL_SIZE)
        return false;
    size_t origin = origin_length(base);

    if (origin_length(ref) > 0) {
        strlcpy(out, ref, size);
    } else {
        /* Ein anderes Schema ("mailto:", "javascript:")? */
        for (const char *p = ref; *p; p++) {
            if (*p == ':')
                return false;
            if (*p == '/' || *p == '?')
                break;
        }
        if (origin == 0)
            return false;
        if (ref[0] == '/' && ref[1] == '/') {
            strlcpy(out, starts_with(base, "https://") ? "https:" : "http:", size);
            strlcpy(out + strlen(out), ref, size - strlen(out));
        } else if (ref[0] == '/') {
            memcpy(out, base, origin);
            strlcpy(out + origin, ref, size - origin);
        } else {
            /* Relativ zur Seite: wie viel von deren Adresse bleibt stehen? */
            size_t end = origin, query = origin, keep;
            while (base[end] && base[end] != '#')
                end++;
            while (query < end && base[query] != '?')
                query++;
            if (ref[0] == '\0') {
                keep = end;             /* dieselbe Seite */
            } else if (ref[0] == '?') {
                keep = query;           /* gleicher Pfad, neue Anfrage */
            } else {
                keep = query;           /* bis einschließlich des letzten '/' im Pfad */
                while (keep > origin && base[keep - 1] != '/')
                    keep--;
            }
            if (keep + 2 >= size)
                return false;
            memcpy(out, base, keep);
            if (keep == origin && ref[0] != '\0' && ref[0] != '?')
                out[keep++] = '/';
            out[keep] = '\0';
            strlcpy(out + keep, ref, size - keep);
        }
    }

    /* Mindestens "/" als Pfad, dann "." und ".." auflösen */
    origin = origin_length(out);
    if (out[origin] == '\0') {
        strlcpy(out + origin, "/", size - origin);
    } else if (out[origin] == '?') {
        memmove(out + origin + 1, out + origin, strlen(out + origin) + 1);
        out[origin] = '/';
    }
    normalize_path(out + origin);
    return true;
}

size_t url_encode(char *out, size_t size, const char *text)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t n = 0;
    for (; *text && n + 4 < size; text++) {
        unsigned char c = (unsigned char)*text;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '*') {
            out[n++] = (char)c;
        } else if (c == ' ') {
            out[n++] = '+';
        } else if (c == '\n') {         /* Zeilenenden gehen als CR LF auf die Reise */
            memcpy(out + n, "%0D%0A", 6 < size - n ? 6 : 0);
            n += 6 < size - n ? 6 : 0;
        } else {
            out[n++] = '%';
            out[n++] = hex[c >> 4];
            out[n++] = hex[c & 15];
        }
    }
    if (size)
        out[n < size ? n : size - 1] = '\0';
    return n;
}
