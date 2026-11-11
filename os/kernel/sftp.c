/*
 * SFTP, Version 3 (draft-ietf-secsh-filexfer-02). Jede Anfrage ist ein
 * kleines Paket im SSH-Kanal: Länge, Art, laufende Nummer, Angaben. Der
 * Server antwortet mit derselben Nummer - mit Daten oder einem Status.
 */
#include "sftp.h"
#include "console.h"
#include "memory.h"
#include "net.h"
#include "string.h"

enum {
    FXP_INIT = 1, FXP_VERSION = 2, FXP_OPEN = 3, FXP_CLOSE = 4, FXP_READ = 5,
    FXP_WRITE = 6, FXP_OPENDIR = 11, FXP_READDIR = 12, FXP_REMOVE = 13,
    FXP_MKDIR = 14, FXP_RMDIR = 15, FXP_REALPATH = 16, FXP_STAT = 17,
    FXP_RENAME = 18,
    FXP_STATUS = 101, FXP_HANDLE = 102, FXP_DATA = 103, FXP_NAME = 104,
    FXP_ATTRS = 105,
};

enum { FX_OK = 0, FX_EOF = 1, FX_NO_SUCH_FILE = 2, FX_PERMISSION_DENIED = 3 };

#define ATTR_SIZE        0x00000001u
#define ATTR_UIDGID      0x00000002u
#define ATTR_PERMISSIONS 0x00000004u
#define ATTR_TIMES       0x00000008u
#define ATTR_EXTENDED    0x80000000u

#define OPEN_READ   0x01u
#define OPEN_WRITE  0x02u
#define OPEN_CREATE 0x08u
#define OPEN_TRUNC  0x10u

#define REQUEST_MAX   34000
#define RESPONSE_MAX  (512 * 1024)
#define READ_CHUNK    32768
#define WRITE_CHUNK   30000             /* passt samt Kopf in ein SSH-Paket */
#define PIPELINE      8                 /* so viele Schreibaufträge dürfen offen sein */
#define HANDLE_MAX    256

struct sftp {
    ssh_t *ssh;
    uint32_t next_id;
    char error[200];
    bool dead;
    uint8_t *request;
    size_t request_len;
    uint8_t *response;                  /* Art, Nummer, Angaben */
    size_t response_len, response_cap;
};

const char *sftp_error(const sftp_t *s)
{
    return s->error[0] ? s->error : "unbekannter Fehler";
}

bool sftp_alive(const sftp_t *s)
{
    return !s->dead;
}

/* Die Verbindung ist nicht mehr zu gebrauchen. */
static bool broken(sftp_t *s, const char *text)
{
    s->dead = true;
    strlcpy(s->error, text ? text : ssh_error(s->ssh), sizeof(s->error));
    return false;
}

/* --- Anfragen bauen ------------------------------------------------------------------------- */

static void add_bytes(sftp_t *s, const void *data, size_t len)
{
    if (s->request_len + len > REQUEST_MAX) {
        s->request_len = REQUEST_MAX + 1;
        return;
    }
    memcpy(s->request + s->request_len, data, len);
    s->request_len += len;
}

static void add_u32(sftp_t *s, uint32_t v)
{
    uint8_t b[4];
    put_be32(b, v);
    add_bytes(s, b, 4);
}

static void add_u64(sftp_t *s, uint64_t v)
{
    add_u32(s, (uint32_t)(v >> 32));
    add_u32(s, (uint32_t)v);
}

static void add_string(sftp_t *s, const void *data, size_t len)
{
    add_u32(s, (uint32_t)len);
    add_bytes(s, data, len);
}

static void add_text(sftp_t *s, const char *text)
{
    add_string(s, text, strlen(text));
}

/* Beginnt eine Anfrage; liefert ihre Nummer. */
static uint32_t begin(sftp_t *s, uint8_t type)
{
    uint32_t id = s->next_id++;
    s->request_len = 4;                 /* die Länge wird beim Senden eingetragen */
    add_bytes(s, &type, 1);
    add_u32(s, id);
    return id;
}

static bool send(sftp_t *s)
{
    if (s->dead)
        return false;
    if (s->request_len > REQUEST_MAX)
        return broken(s, "Anfrage zu groß (Pfad zu lang?)");
    put_be32(s->request, (uint32_t)(s->request_len - 4));
    if (!ssh_write(s->ssh, s->request, s->request_len))
        return broken(s, NULL);
    return true;
}

/* --- Antworten lesen ------------------------------------------------------------------------- */

typedef struct {
    const uint8_t *p, *end;
    bool bad;
} reader_t;

static uint32_t take_u32(reader_t *r)
{
    if (r->end - r->p < 4) {
        r->bad = true;
        r->p = r->end;
        return 0;
    }
    uint32_t v = get_be32(r->p);
    r->p += 4;
    return v;
}

static uint64_t take_u64(reader_t *r)
{
    uint64_t high = take_u32(r);
    return (high << 32) | take_u32(r);
}

static void take_string(reader_t *r, const uint8_t **data, size_t *len)
{
    uint32_t n = take_u32(r);
    if ((size_t)(r->end - r->p) < n) {
        r->bad = true;
        n = 0;
    }
    *data = r->p;
    *len = n;
    r->p += n;
}

/* Liest die nächste Antwort; liefert ihre Art und einen Leser hinter der Nummer. */
static bool receive(sftp_t *s, uint8_t *type, uint32_t *id, reader_t *r)
{
    uint8_t head[4];
    if (s->dead)
        return false;
    if (!ssh_read(s->ssh, head, 4))
        return broken(s, NULL);
    uint32_t length = get_be32(head);
    if (length < 5 || length > RESPONSE_MAX)
        return broken(s, "Unverständliche Antwort des Servers");
    if (length > s->response_cap) {
        kfree(s->response);
        s->response_cap = (size_t)length + 4096;
        s->response = kmalloc(s->response_cap);
        if (!s->response) {
            s->response_cap = 0;
            return broken(s, "Nicht genug Speicher");
        }
    }
    if (!ssh_read(s->ssh, s->response, length))
        return broken(s, NULL);
    s->response_len = length;
    *type = s->response[0];
    *id = get_be32(s->response + 1);
    *r = (reader_t){ s->response + 5, s->response + length, false };
    return true;
}

/* Merkt sich die Erklärung zu einem Status; false, wenn es ein Fehler ist. */
static bool status_ok(sftp_t *s, reader_t *r, bool eof_is_ok)
{
    const uint8_t *text;
    size_t len;
    uint32_t code = take_u32(r);
    take_string(r, &text, &len);
    if (code == FX_OK || (code == FX_EOF && eof_is_ok))
        return true;

    char message[100];
    size_t n = 0;
    for (size_t i = 0; i < len && n + 1 < sizeof(message); i++) {
        if (text[i] >= 32)
            message[n++] = (char)text[i];
    }
    message[n] = '\0';
    switch (code) {
    case FX_EOF:
        strlcpy(s->error, "Unerwartetes Ende der Datei", sizeof(s->error));
        break;
    case FX_NO_SUCH_FILE:
        strlcpy(s->error, "Gibt es auf dem Server nicht", sizeof(s->error));
        break;
    case FX_PERMISSION_DENIED:
        strlcpy(s->error, "Keine Berechtigung auf dem Server", sizeof(s->error));
        break;
    default:
        /* "Failure" sagt nichts - typisch etwa beim Löschen eines nicht leeren Ordners */
        if (n == 0 || strcmp(message, "Failure") == 0)
            strlcpy(s->error, "Der Server hat das abgelehnt", sizeof(s->error));
        else
            ksnprintf(s->error, sizeof(s->error), "Der Server meldet: %s", message);
        break;
    }
    return false;
}

/*
 * Schickt die vorbereitete Anfrage und wartet auf die Antwort der erwarteten
 * Art. Kommt stattdessen ein Status, ist das ein Fehler - außer bei
 * expected == FXP_STATUS, dann zählt "OK".
 */
static bool transact(sftp_t *s, uint32_t id, uint8_t expected, reader_t *r)
{
    uint8_t type;
    uint32_t got;
    if (!send(s) || !receive(s, &type, &got, r))
        return false;
    if (got != id)
        return broken(s, "Der Server antwortet durcheinander");
    if (type == FXP_STATUS)
        return status_ok(s, r, false) && expected == FXP_STATUS;
    if (type != expected)
        return broken(s, "Unerwartete Antwort des Servers");
    return true;
}

/* Überspringt die Angaben zu einer Datei und merkt sich das Wichtige. */
static void take_attributes(reader_t *r, sftp_entry_t *entry)
{
    uint32_t flags = take_u32(r);
    if (flags & ATTR_SIZE)
        entry->size = take_u64(r);
    if (flags & ATTR_UIDGID) {
        take_u32(r);
        take_u32(r);
    }
    if (flags & ATTR_PERMISSIONS) {
        uint32_t mode = take_u32(r) & 0170000;
        entry->directory = mode == 0040000;
        entry->link = mode == 0120000;
    }
    if (flags & ATTR_TIMES) {
        take_u32(r);
        entry->mtime = take_u32(r);
    }
    if (flags & ATTR_EXTENDED) {
        const uint8_t *data;
        size_t len;
        for (uint32_t n = take_u32(r); n > 0 && !r->bad; n--) {
            take_string(r, &data, &len);
            take_string(r, &data, &len);
        }
    }
}

/* --- Verbinden ------------------------------------------------------------------------------- */

sftp_t *sftp_connect(const ssh_options_t *options, char *error, size_t error_size)
{
    sftp_t *s = kzalloc(sizeof(*s));
    if (s)
        s->request = kmalloc(REQUEST_MAX + 4);
    if (!s || !s->request) {
        ksnprintf(error, error_size, "Nicht genug Speicher");
        sftp_close(s);
        return NULL;
    }
    s->ssh = ssh_connect(options, error, error_size);
    if (!s->ssh) {
        sftp_close(s);
        return NULL;
    }

    /* Der Auftakt hat keine laufende Nummer: nur Art und Version. */
    uint8_t init[9] = { 0, 0, 0, 5, FXP_INIT, 0, 0, 0, 3 };
    uint8_t type;
    uint32_t version;
    reader_t r;
    bool ok = ssh_open_subsystem(s->ssh, "sftp") &&
              ssh_write(s->ssh, init, sizeof(init));
    if (!ok)
        broken(s, NULL);
    else if (receive(s, &type, &version, &r) && (type != FXP_VERSION || version < 3))
        broken(s, "Der Server spricht kein passendes SFTP");

    if (s->dead) {
        strlcpy(error, sftp_error(s), error_size);
        sftp_close(s);
        return NULL;
    }
    return s;
}

void sftp_close(sftp_t *s)
{
    if (!s)
        return;
    ssh_close(s->ssh);
    kfree(s->request);
    kfree(s->response);
    kfree(s);
}

/* --- Verzeichnisse ---------------------------------------------------------------------------- */

void sftp_join(const char *directory, const char *name, char *out, size_t size)
{
    if (name[0] == '/') {
        strlcpy(out, name, size);
    } else if (strcmp(name, "..") == 0) {
        strlcpy(out, directory, size);
        size_t n = strlen(out);
        while (n > 1 && out[n - 1] == '/')
            n--;                        /* abschließende Schrägstriche */
        while (n > 1 && out[n - 1] != '/')
            n--;                        /* letzter Teil */
        while (n > 1 && out[n - 1] == '/')
            n--;
        out[n] = '\0';
    } else if (strcmp(name, ".") == 0 || !*name) {
        strlcpy(out, directory, size);
    } else {
        size_t n = strlen(directory);
        ksnprintf(out, size, "%s%s%s", directory,
                  n > 0 && directory[n - 1] == '/' ? "" : "/", name);
    }
}

bool sftp_realpath(sftp_t *s, const char *path, char *out, size_t size)
{
    reader_t r;
    const uint8_t *name;
    size_t len;

    uint32_t id = begin(s, FXP_REALPATH);
    add_text(s, path);
    if (!transact(s, id, FXP_NAME, &r))
        return false;
    uint32_t count = take_u32(&r);
    take_string(&r, &name, &len);
    if (r.bad || count != 1 || len == 0 || len >= size)
        return broken(s, "Unverständliche Antwort des Servers");
    memcpy(out, name, len);
    out[len] = '\0';
    return true;
}

static bool stat_path(sftp_t *s, const char *path, sftp_entry_t *entry)
{
    reader_t r;
    uint32_t id = begin(s, FXP_STAT);
    add_text(s, path);
    if (!transact(s, id, FXP_ATTRS, &r))
        return false;
    take_attributes(&r, entry);
    return !r.bad;
}

static bool close_handle(sftp_t *s, const uint8_t *handle, size_t handle_len)
{
    reader_t r;
    uint32_t id = begin(s, FXP_CLOSE);
    add_string(s, handle, handle_len);
    return transact(s, id, FXP_STATUS, &r);
}

/* Öffnet Datei oder Verzeichnis und kopiert die Kennung, die der Server vergibt. */
static bool open_handle(sftp_t *s, uint32_t id, uint8_t *handle, size_t *handle_len)
{
    reader_t r;
    const uint8_t *data;
    if (!transact(s, id, FXP_HANDLE, &r))
        return false;
    take_string(&r, &data, handle_len);
    if (r.bad || *handle_len == 0 || *handle_len > HANDLE_MAX)
        return broken(s, "Unverständliche Antwort des Servers");
    memcpy(handle, data, *handle_len);
    return true;
}

/* Ordner vor Dateien, sonst nach Namen (Groß/Klein egal) */
static bool before(const sftp_entry_t *a, const sftp_entry_t *b)
{
    if (a->directory != b->directory)
        return a->directory;
    for (const char *x = a->name, *y = b->name; ; x++, y++) {
        unsigned char cx = (unsigned char)*x, cy = (unsigned char)*y;
        if (cx >= 'A' && cx <= 'Z')
            cx += 32;
        if (cy >= 'A' && cy <= 'Z')
            cy += 32;
        if (cx != cy)
            return cx < cy;
        if (!cx)
            return false;
    }
}

bool sftp_list(sftp_t *s, const char *path, sftp_entry_t **entries, int *count)
{
    uint8_t handle[HANDLE_MAX];
    size_t handle_len;
    sftp_entry_t *list = NULL;
    int n = 0, cap = 0;
    bool ok = true;

    *entries = NULL;
    *count = 0;
    uint32_t id = begin(s, FXP_OPENDIR);
    add_text(s, path);
    if (!open_handle(s, id, handle, &handle_len))
        return false;

    while (ok) {
        uint8_t type;
        uint32_t got;
        reader_t r;
        id = begin(s, FXP_READDIR);
        add_string(s, handle, handle_len);
        if (!send(s) || !receive(s, &type, &got, &r)) {
            ok = false;
            break;
        }
        if (type == FXP_STATUS) {       /* "Ende" heißt: alles gelesen */
            ok = status_ok(s, &r, true);
            break;
        }
        if (type != FXP_NAME || got != id) {
            ok = broken(s, "Unerwartete Antwort des Servers");
            break;
        }
        for (uint32_t left = take_u32(&r); left > 0 && ok; left--) {
            const uint8_t *name, *longname;
            size_t name_len, long_len;
            sftp_entry_t entry;
            memset(&entry, 0, sizeof(entry));
            take_string(&r, &name, &name_len);
            take_string(&r, &longname, &long_len);
            /* Die lange Form beginnt wie bei "ls -l" mit der Art: d, l oder - */
            entry.directory = long_len > 0 && longname[0] == 'd';
            entry.link = long_len > 0 && longname[0] == 'l';
            take_attributes(&r, &entry);
            if (r.bad) {
                ok = broken(s, "Unverständliche Antwort des Servers");
                break;
            }
            if (name_len == 0 || name_len >= sizeof(entry.name) ||
                (name_len == 1 && name[0] == '.') ||
                (name_len == 2 && name[0] == '.' && name[1] == '.'))
                continue;
            memcpy(entry.name, name, name_len);

            if (n == cap) {
                int bigger = cap ? cap * 2 : 64;
                sftp_entry_t *grown = kmalloc((size_t)bigger * sizeof(*grown));
                if (!grown) {
                    strlcpy(s->error, "Nicht genug Speicher", sizeof(s->error));
                    ok = false;
                    break;
                }
                if (list)
                    memcpy(grown, list, (size_t)n * sizeof(*list));
                kfree(list);
                list = grown;
                cap = bigger;
            }
            list[n++] = entry;
        }
    }
    if (!s->dead) {
        char keep[sizeof(s->error)];
        strlcpy(keep, s->error, sizeof(keep));
        close_handle(s, handle, handle_len);
        if (!ok)
            strlcpy(s->error, keep, sizeof(s->error));
    }

    /* Verknüpfungen: nachsehen, ob sie auf einen Ordner zeigen */
    for (int i = 0; ok && i < n && !s->dead; i++) {
        if (!list[i].link)
            continue;
        char target[1024];
        sftp_entry_t info;
        memset(&info, 0, sizeof(info));
        sftp_join(path, list[i].name, target, sizeof(target));
        if (stat_path(s, target, &info)) {
            list[i].directory = info.directory;
            list[i].size = info.size;
        }
    }
    if (s->dead)
        ok = false;

    if (!ok) {
        kfree(list);
        return false;
    }
    /* Sortieren durch Einfügen - Verzeichnisse haben selten mehr als ein paar hundert Einträge */
    for (int i = 1; i < n; i++) {
        sftp_entry_t entry = list[i];
        int k = i;
        while (k > 0 && before(&entry, &list[k - 1])) {
            list[k] = list[k - 1];
            k--;
        }
        list[k] = entry;
    }
    *entries = list;
    *count = n;
    return true;
}

/* --- Dateien ---------------------------------------------------------------------------------- */

bool sftp_download(sftp_t *s, const char *path, size_t max, char **data,
                   size_t *size, sftp_progress_t progress, void *ctx)
{
    uint8_t handle[HANDLE_MAX];
    size_t handle_len;
    sftp_entry_t info;
    char *buffer = NULL;
    size_t len = 0, cap = 0;
    bool ok = true;

    *data = NULL;
    *size = 0;
    memset(&info, 0, sizeof(info));
    if (!stat_path(s, path, &info))
        return false;
    if (info.directory) {
        strlcpy(s->error, "Das ist ein Ordner", sizeof(s->error));
        return false;
    }
    if (info.size > max) {
        ksnprintf(s->error, sizeof(s->error), "Die Datei ist zu groß (%llu MiB, "
                  "möglich sind %zu MiB)", info.size >> 20, max >> 20);
        return false;
    }

    uint32_t id = begin(s, FXP_OPEN);
    add_text(s, path);
    add_u32(s, OPEN_READ);
    add_u32(s, 0);                      /* keine Angaben zur Datei */
    if (!open_handle(s, id, handle, &handle_len))
        return false;

    cap = (size_t)info.size + 1;
    buffer = kmalloc(cap);
    if (!buffer) {
        strlcpy(s->error, "Nicht genug Speicher", sizeof(s->error));
        ok = false;
    }
    while (ok) {
        uint8_t type;
        uint32_t got;
        reader_t r;
        const uint8_t *chunk;
        size_t chunk_len;

        id = begin(s, FXP_READ);
        add_string(s, handle, handle_len);
        add_u64(s, len);
        add_u32(s, READ_CHUNK);
        if (!send(s) || !receive(s, &type, &got, &r)) {
            ok = false;
            break;
        }
        if (type == FXP_STATUS) {
            ok = status_ok(s, &r, true);        /* "Ende": fertig */
            break;
        }
        take_string(&r, &chunk, &chunk_len);
        if (type != FXP_DATA || got != id || r.bad || chunk_len == 0) {
            ok = broken(s, "Unerwartete Antwort des Servers");
            break;
        }
        if (len + chunk_len > max) {
            strlcpy(s->error, "Die Datei ist zu groß", sizeof(s->error));
            ok = false;
            break;
        }
        if (len + chunk_len + 1 > cap) {        /* größer als angekündigt */
            size_t bigger = (len + chunk_len) * 2 + 1;
            char *grown = kmalloc(bigger);
            if (!grown) {
                strlcpy(s->error, "Nicht genug Speicher", sizeof(s->error));
                ok = false;
                break;
            }
            memcpy(grown, buffer, len);
            kfree(buffer);
            buffer = grown;
            cap = bigger;
        }
        memcpy(buffer + len, chunk, chunk_len);
        len += chunk_len;
        if (progress)
            progress(ctx, len, info.size);
    }

    if (!s->dead) {
        char keep[sizeof(s->error)];
        strlcpy(keep, s->error, sizeof(keep));
        close_handle(s, handle, handle_len);
        if (!ok)
            strlcpy(s->error, keep, sizeof(s->error));
    }
    if (!ok || s->dead) {
        kfree(buffer);
        return false;
    }
    buffer[len] = '\0';
    *data = buffer;
    *size = len;
    return true;
}

bool sftp_upload(sftp_t *s, const char *path, const void *data, size_t size,
                 sftp_progress_t progress, void *ctx)
{
    uint8_t handle[HANDLE_MAX];
    size_t handle_len;
    const uint8_t *bytes = data;
    size_t sent = 0;
    int waiting = 0;                    /* Schreibaufträge ohne Antwort */
    bool ok = true;

    uint32_t id = begin(s, FXP_OPEN);
    add_text(s, path);
    add_u32(s, OPEN_WRITE | OPEN_CREATE | OPEN_TRUNC);
    add_u32(s, 0);
    if (!open_handle(s, id, handle, &handle_len))
        return false;

    /* Mehrere Aufträge hintereinander schicken und die Antworten nachholen:
     * so wartet nicht jedes Stück einzeln auf seinen Weg hin und zurück. */
    while (ok && (sent < size || waiting > 0)) {
        if (sent < size && waiting < PIPELINE) {
            size_t n = size - sent < WRITE_CHUNK ? size - sent : WRITE_CHUNK;
            begin(s, FXP_WRITE);
            add_string(s, handle, handle_len);
            add_u64(s, sent);
            add_string(s, bytes + sent, n);
            if (!send(s)) {
                ok = false;
                break;
            }
            sent += n;
            waiting++;
            continue;
        }
        uint8_t type;
        uint32_t got;
        reader_t r;
        if (!receive(s, &type, &got, &r)) {
            ok = false;
        } else if (type != FXP_STATUS) {
            ok = broken(s, "Unerwartete Antwort des Servers");
        } else if (!status_ok(s, &r, false)) {
            ok = false;
        } else {
            waiting--;
            /* Bestätigt ist alles bis auf die noch offenen (vollen) Stücke. */
            if (progress)
                progress(ctx, sent - (size_t)waiting * WRITE_CHUNK, size);
        }
    }
    /* Nach einem Fehler stehen noch Antworten aus - die Verbindung wäre aus dem Tritt. */
    char keep[sizeof(s->error)];
    strlcpy(keep, s->error, sizeof(keep));
    while (!s->dead && !ok && waiting > 1) {
        uint8_t type;
        uint32_t got;
        reader_t r;
        if (!receive(s, &type, &got, &r))
            break;
        waiting--;
    }
    if (!s->dead) {
        bool closed = close_handle(s, handle, handle_len);
        if (ok && !closed)
            return false;               /* Fehler beim Abschließen der Datei */
    }
    if (!ok)
        strlcpy(s->error, keep, sizeof(s->error));
    return ok && !s->dead;
}

static bool simple(sftp_t *s, uint8_t type, const char *path, const char *second,
                   bool attributes)
{
    reader_t r;
    uint32_t id = begin(s, type);
    add_text(s, path);
    if (second)
        add_text(s, second);
    if (attributes)
        add_u32(s, 0);
    return transact(s, id, FXP_STATUS, &r);
}

bool sftp_remove(sftp_t *s, const char *path)
{
    return simple(s, FXP_REMOVE, path, NULL, false);
}

bool sftp_mkdir(sftp_t *s, const char *path)
{
    return simple(s, FXP_MKDIR, path, NULL, true);
}

bool sftp_rmdir(sftp_t *s, const char *path)
{
    return simple(s, FXP_RMDIR, path, NULL, false);
}

bool sftp_rename(sftp_t *s, const char *from, const char *to)
{
    return simple(s, FXP_RENAME, from, to, false);
}
