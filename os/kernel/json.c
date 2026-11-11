#include "json.h"
#include "memory.h"
#include "string.h"

/* --- Lesen ------------------------------------------------------------------------- */

static const char *skip_space(const char *p, const char *end)
{
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
        p++;
    return p;
}

/* Liefert die Stelle hinter dem Text, der bei p beginnt (p zeigt auf "). */
static const char *skip_string(const char *p, const char *end)
{
    for (p++; p < end; p++) {
        if (*p == '\\')
            p++;                        /* maskiertes Zeichen überspringen */
        else if (*p == '"')
            return p + 1;
    }
    return NULL;
}

/* Liefert die Stelle hinter dem Wert, der bei p beginnt; NULL bei Fehlern. */
static const char *skip_value(const char *p, const char *end, int depth)
{
    p = skip_space(p, end);
    if (p >= end || depth > 64)
        return NULL;

    if (*p == '"')
        return skip_string(p, end);

    if (*p == '{' || *p == '[') {
        char close = *p == '{' ? '}' : ']';
        bool object = *p == '{';
        p = skip_space(p + 1, end);
        if (p < end && *p == close)
            return p + 1;
        for (;;) {
            if (object) {               /* "Schlüssel" : Wert */
                p = skip_space(p, end);
                if (p >= end || *p != '"' || !(p = skip_string(p, end)))
                    return NULL;
                p = skip_space(p, end);
                if (p >= end || *p != ':')
                    return NULL;
                p++;
            }
            p = skip_value(p, end, depth + 1);
            if (!p)
                return NULL;
            p = skip_space(p, end);
            if (p >= end)
                return NULL;
            if (*p == close)
                return p + 1;
            if (*p != ',')
                return NULL;
            p++;
        }
    }

    /* Zahl, true, false oder null: bis zum nächsten Trennzeichen */
    const char *start = p;
    while (p < end && *p != ',' && *p != '}' && *p != ']' && *p != ' ' &&
           *p != '\t' && *p != '\n' && *p != '\r')
        p++;
    return p > start ? p : NULL;
}

bool json_parse(const char *text, size_t len, json_t *out)
{
    const char *end = text + len;
    const char *start = skip_space(text, end);
    const char *after = skip_value(start, end, 0);
    if (!after || skip_space(after, end) != end)
        return false;
    out->p = start;
    out->end = after;
    return true;
}

char json_type(json_t value)
{
    if (!value.p || value.p >= value.end)
        return 0;
    char c = *value.p;
    if (c == '{' || c == '[' || c == '"' || c == 't' || c == 'f')
        return c;
    return c == 'n' ? '0' : 'n';
}

bool json_get(json_t object, const char *key, json_t *out)
{
    if (json_type(object) != '{')
        return false;
    size_t key_len = strlen(key);
    const char *end = object.end - 1;   /* ohne die schließende Klammer */
    const char *p = skip_space(object.p + 1, end);

    while (p < end && *p == '"') {
        const char *name = p + 1;
        const char *after_name = skip_string(p, end);
        if (!after_name)
            return false;
        size_t name_len = (size_t)(after_name - 1 - name);
        p = skip_space(after_name, end);
        if (p >= end || *p != ':')
            return false;
        const char *value = skip_space(p + 1, end);
        const char *after = skip_value(value, end, 0);
        if (!after)
            return false;
        if (name_len == key_len && memcmp(name, key, key_len) == 0) {
            out->p = value;
            out->end = after;
            return true;
        }
        p = skip_space(after, end);
        if (p < end && *p == ',')
            p = skip_space(p + 1, end);
    }
    return false;
}

bool json_each(json_t array, const char **cursor, json_t *element)
{
    if (json_type(array) != '[')
        return false;
    const char *end = array.end - 1;
    const char *p = *cursor ? *cursor : array.p + 1;

    p = skip_space(p, end);
    if (p < end && *p == ',')
        p = skip_space(p + 1, end);
    if (p >= end)
        return false;
    const char *after = skip_value(p, end, 0);
    if (!after)
        return false;
    element->p = p;
    element->end = after;
    *cursor = after;
    return true;
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    c |= 0x20;
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

static bool read_hex4(const char *p, const char *end, unsigned *out)
{
    if (end - p < 4)
        return false;
    unsigned value = 0;
    for (int i = 0; i < 4; i++) {
        int digit = hex_digit(p[i]);
        if (digit < 0)
            return false;
        value = value * 16 + (unsigned)digit;
    }
    *out = value;
    return true;
}

size_t json_string(json_t value, char *out, size_t size)
{
    size_t n = 0;
    if (size == 0)
        return 0;
    if (json_type(value) != '"') {
        out[0] = '\0';
        return 0;
    }
    const char *p = value.p + 1, *end = value.end - 1;

    while (p < end) {
        unsigned cp = (unsigned char)*p++;
        if (cp == '\\' && p < end) {
            char escape = *p++;
            switch (escape) {
            case 'n': cp = '\n'; break;
            case 't': cp = '\t'; break;
            case 'r': cp = '\r'; break;
            case 'b': cp = '\b'; break;
            case 'f': cp = '\f'; break;
            case 'u': {
                unsigned low;
                if (!read_hex4(p, end, &cp)) {
                    cp = '?';
                    break;
                }
                p += 4;
                /* Zeichen jenseits von U+FFFF kommen als Paar aus zwei \u-Angaben */
                if (cp >= 0xD800 && cp < 0xDC00 && end - p >= 6 && p[0] == '\\' &&
                    p[1] == 'u' && read_hex4(p + 2, end, &low) && low >= 0xDC00 &&
                    low < 0xE000) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    p += 6;
                }
                break;
            }
            default:
                cp = (unsigned char)escape;     /* \" \\ \/ */
                break;
            }
            /* Als UTF-8 ausgeben */
            char utf8[4];
            size_t bytes;
            if (cp < 0x80) {
                utf8[0] = (char)cp;
                bytes = 1;
            } else if (cp < 0x800) {
                utf8[0] = (char)(0xC0 | (cp >> 6));
                utf8[1] = (char)(0x80 | (cp & 0x3F));
                bytes = 2;
            } else if (cp < 0x10000) {
                utf8[0] = (char)(0xE0 | (cp >> 12));
                utf8[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
                utf8[2] = (char)(0x80 | (cp & 0x3F));
                bytes = 3;
            } else {
                utf8[0] = (char)(0xF0 | (cp >> 18));
                utf8[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
                utf8[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
                utf8[3] = (char)(0x80 | (cp & 0x3F));
                bytes = 4;
            }
            if (n + bytes >= size)
                break;
            memcpy(out + n, utf8, bytes);
            n += bytes;
            continue;
        }
        if (n + 1 >= size)
            break;
        out[n++] = (char)cp;            /* gewöhnliches Byte (auch UTF-8) */
    }
    out[n] = '\0';
    return n;
}

bool json_string_equals(json_t value, const char *text)
{
    size_t len = strlen(text);
    return json_type(value) == '"' && (size_t)(value.end - value.p) == len + 2 &&
           memcmp(value.p + 1, text, len) == 0;
}

/* --- Schreiben ------------------------------------------------------------------------ */

void sb_append_n(strbuf_t *sb, const char *text, size_t len)
{
    if (sb->failed)
        return;
    if (sb->len + len + 1 > sb->capacity) {
        size_t capacity = sb->capacity ? sb->capacity * 2 : 1024;
        while (capacity < sb->len + len + 1)
            capacity *= 2;
        char *grown = kmalloc(capacity);
        if (!grown) {
            sb->failed = true;
            return;
        }
        if (sb->len)
            memcpy(grown, sb->data, sb->len);
        kfree(sb->data);
        sb->data = grown;
        sb->capacity = capacity;
    }
    memcpy(sb->data + sb->len, text, len);
    sb->len += len;
    sb->data[sb->len] = '\0';
}

void sb_append(strbuf_t *sb, const char *text)
{
    sb_append_n(sb, text, strlen(text));
}

void sb_append_json(strbuf_t *sb, const char *text)
{
    static const char hex[] = "0123456789abcdef";
    sb_append_n(sb, "\"", 1);
    for (const char *p = text; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '"' || c == '\\') {
            char escaped[2] = { '\\', (char)c };
            sb_append_n(sb, escaped, 2);
        } else if (c == '\n') {
            sb_append_n(sb, "\\n", 2);
        } else if (c == '\t') {
            sb_append_n(sb, "\\t", 2);
        } else if (c < 0x20) {
            char escaped[6] = { '\\', 'u', '0', '0', hex[c >> 4], hex[c & 15] };
            sb_append_n(sb, escaped, 6);
        } else {
            sb_append_n(sb, p, 1);      /* UTF-8 darf unverändert stehen bleiben */
        }
    }
    sb_append_n(sb, "\"", 1);
}

void sb_truncate(strbuf_t *sb, size_t len)
{
    if (len < sb->len) {
        sb->len = len;
        sb->data[len] = '\0';
    }
}

void sb_free(strbuf_t *sb)
{
    kfree(sb->data);
    memset(sb, 0, sizeof(*sb));
}
