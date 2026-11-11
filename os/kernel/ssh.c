/*
 * SSH-Client, Protokoll 2 (RFC 4251-4254 mit den heute üblichen Verfahren).
 *
 * Ablauf einer Verbindung:
 *   1. Beide Seiten nennen ihre Version und ihre Verfahren (KEXINIT).
 *   2. Schlüsselaustausch (ECDH): Daraus entsteht ein gemeinsames Geheimnis K
 *      und ein Hashwert H über alles bisher Gesagte. Der Server unterschreibt
 *      H mit seinem dauerhaften Schlüssel - das weist ihn aus und zeigt, dass
 *      niemand an der Aushandlung gedreht hat.
 *   3. Aus K und H werden die Schlüssel für beide Richtungen abgeleitet; ab
 *      NEWKEYS ist jedes Paket verschlüsselt und gegen Veränderung geschützt.
 *   4. Anmeldung mit Benutzername und Passwort.
 *   5. Ein Kanal wird geöffnet und darin ein Dienst gestartet (SFTP).
 */
#include "ssh.h"
#include "console.h"
#include "crypto.h"
#include "memory.h"
#include "net.h"
#include "shell.h"
#include "storage.h"
#include "string.h"

#define TIMEOUT_MS    30000
#define IN_SIZE       16384
#define PACKET_MAX    40000             /* größtes Paket, das wir annehmen */
#define PAYLOAD_MAX   34000
#define CHUNK_MAX     32000             /* Kanaldaten pro Paket */
#define LOCAL_WINDOW  (2u * 1024 * 1024)
#define LOCAL_PACKET  32768
#define KNOWN_FILE    "bekannte-server.txt"
#define KNOWN_MAX     (64 * 1024)

enum {
    MSG_DISCONNECT = 1, MSG_IGNORE = 2, MSG_UNIMPLEMENTED = 3, MSG_DEBUG = 4,
    MSG_SERVICE_REQUEST = 5, MSG_SERVICE_ACCEPT = 6,
    MSG_KEXINIT = 20, MSG_NEWKEYS = 21,
    MSG_KEX_ECDH_INIT = 30, MSG_KEX_ECDH_REPLY = 31,
    MSG_USERAUTH_REQUEST = 50, MSG_USERAUTH_FAILURE = 51,
    MSG_USERAUTH_SUCCESS = 52, MSG_USERAUTH_BANNER = 53,
    MSG_USERAUTH_INFO_REQUEST = 60, MSG_USERAUTH_INFO_RESPONSE = 61,
    MSG_GLOBAL_REQUEST = 80, MSG_REQUEST_FAILURE = 82,
    MSG_CHANNEL_OPEN = 90, MSG_CHANNEL_OPEN_CONFIRMATION = 91,
    MSG_CHANNEL_OPEN_FAILURE = 92, MSG_CHANNEL_WINDOW_ADJUST = 93,
    MSG_CHANNEL_DATA = 94, MSG_CHANNEL_EXTENDED_DATA = 95,
    MSG_CHANNEL_EOF = 96, MSG_CHANNEL_CLOSE = 97, MSG_CHANNEL_REQUEST = 98,
    MSG_CHANNEL_SUCCESS = 99, MSG_CHANNEL_FAILURE = 100,
};

/* Unsere Verfahren, das liebste zuerst */
static const char CLIENT_VERSION[] = "SSH-2.0-" OS_NAME "_" OS_VERSION;
static const char KEX_ALGS[] =
    "curve25519-sha256,curve25519-sha256@libssh.org,ecdh-sha2-nistp256";
static const char KEX_STRICT[] = ",kex-strict-c-v00@openssh.com";
static const char HOSTKEY_ALGS[] =
    "ssh-ed25519,ecdsa-sha2-nistp256,ecdsa-sha2-nistp384,rsa-sha2-512,rsa-sha2-256";
static const char CIPHER_ALGS[] =
    "chacha20-poly1305@openssh.com,aes256-ctr,aes192-ctr,aes128-ctr";
static const char MAC_ALGS[] = "hmac-sha2-256";

typedef enum { CIPHER_NONE, CIPHER_CHACHA, CIPHER_AES } cipher_kind_t;

/* Verschlüsselung einer Richtung */
typedef struct {
    cipher_kind_t kind;
    uint8_t key[64];                    /* ChaCha20: erst Daten-, dann Längenschlüssel */
    aes_ctr_t aes;
    uint8_t mac_key[32];
    uint32_t seq;                       /* Paketnummer */
} direction_t;

struct ssh {
    tcp_conn_t *tcp;
    char error[200];
    char server[96];                    /* "name:port" - so wird der Schlüssel gemerkt */
    ssh_trust_t trust;
    void *trust_ctx;

    uint8_t *in;                        /* vom Netz gelesen, noch nicht verarbeitet */
    size_t in_pos, in_len;
    uint8_t *rx, *tx, *out;             /* Paketpuffer */
    direction_t recv, send;

    char server_version[256];
    uint8_t session_id[32];
    bool have_session;
    uint8_t host_hash[32];              /* SHA-256 des Serverschlüssels */
    bool strict;                        /* "strict KEX" gegen Angriffe auf die Paketzählung */

    bool channel_open, channel_closed;
    int open_reply, channel_reply;      /* 0 offen, 1 ja, 2 nein */
    uint32_t remote_channel, remote_window, remote_max;
    uint32_t local_window;
    uint8_t *data;                      /* empfangene Kanaldaten */
    size_t data_pos, data_len, data_cap;
};

static bool fail(ssh_t *ssh, const char *fmt, ...)
{
    if (!ssh->error[0]) {
        va_list ap;
        va_start(ap, fmt);
        kvsnprintf(ssh->error, sizeof(ssh->error), fmt, ap);
        va_end(ap);
    }
    return false;
}

const char *ssh_error(const ssh_t *ssh)
{
    return ssh->error[0] ? ssh->error : "unbekannter Fehler";
}

/* --- Nachrichten zusammenbauen und zerlegen ------------------------------------------------ */

typedef struct {
    uint8_t *data;
    size_t len, cap;
} out_t;

static void put_bytes(out_t *o, const void *data, size_t len)
{
    if (o->len + len > o->cap) {
        o->len = o->cap + 1;            /* markiert den Überlauf */
        return;
    }
    memcpy(o->data + o->len, data, len);
    o->len += len;
}

static void put_u8(out_t *o, uint8_t v)
{
    put_bytes(o, &v, 1);
}

static void put_u32(out_t *o, uint32_t v)
{
    uint8_t b[4];
    put_be32(b, v);
    put_bytes(o, b, 4);
}

static void put_string(out_t *o, const void *data, size_t len)
{
    put_u32(o, (uint32_t)len);
    put_bytes(o, data, len);
}

static void put_text(out_t *o, const char *text)
{
    put_string(o, text, strlen(text));
}

typedef struct {
    const uint8_t *p, *end;
    bool bad;
} in_t;

static uint8_t get_u8(in_t *i)
{
    if (i->p >= i->end) {
        i->bad = true;
        return 0;
    }
    return *i->p++;
}

static uint32_t get_u32(in_t *i)
{
    if (i->end - i->p < 4) {
        i->bad = true;
        i->p = i->end;
        return 0;
    }
    uint32_t v = get_be32(i->p);
    i->p += 4;
    return v;
}

static void get_string(in_t *i, const uint8_t **data, size_t *len)
{
    uint32_t n = get_u32(i);
    if ((size_t)(i->end - i->p) < n) {
        i->bad = true;
        n = 0;
    }
    *data = i->p;
    *len = n;
    i->p += n;
}

static bool string_is(const uint8_t *data, size_t len, const char *text)
{
    return strlen(text) == len && memcmp(data, text, len) == 0;
}

/* Kopiert einen (nicht nullterminierten) Text des Servers gefahrlos zur Anzeige. */
static void copy_text(char *out, size_t size, const uint8_t *data, size_t len)
{
    size_t n = 0;
    for (size_t i = 0; i < len && n + 1 < size; i++) {
        if (data[i] >= 32 || data[i] == '\n')
            out[n++] = (char)data[i];
    }
    while (n > 0 && out[n - 1] == '\n')
        n--;
    out[n] = '\0';
}

/* Steht name in der kommagetrennten Liste? */
static bool list_has(const uint8_t *list, size_t len, const char *name)
{
    size_t n = strlen(name);
    for (size_t start = 0; start <= len; ) {
        size_t end = start;
        while (end < len && list[end] != ',')
            end++;
        if (end - start == n && memcmp(list + start, name, n) == 0)
            return true;
        start = end + 1;
    }
    return false;
}

/* Das erste unserer Verfahren, das die Gegenseite auch kennt. */
static bool choose(const char *ours, const uint8_t *theirs, size_t len, char *out,
                   size_t size)
{
    while (*ours) {
        size_t n = 0;
        while (ours[n] && ours[n] != ',')
            n++;
        if (n < size) {
            memcpy(out, ours, n);
            out[n] = '\0';
            if (list_has(theirs, len, out))
                return true;
        }
        ours += n;
        if (*ours == ',')
            ours++;
    }
    out[0] = '\0';
    return false;
}

/* --- Pakete --------------------------------------------------------------------------------- */

static bool raw_read(ssh_t *ssh, uint8_t *dst, size_t n)
{
    while (n > 0) {
        if (ssh->in_pos == ssh->in_len) {
            int got = tcp_recv(ssh->tcp, ssh->in, IN_SIZE, TIMEOUT_MS);
            if (got <= 0)
                return fail(ssh, got == 0 ? "Der Server hat die Verbindung geschlossen"
                                          : "Keine Antwort vom Server");
            ssh->in_pos = 0;
            ssh->in_len = (size_t)got;
        }
        size_t take = ssh->in_len - ssh->in_pos;
        if (take > n)
            take = n;
        memcpy(dst, ssh->in + ssh->in_pos, take);
        ssh->in_pos += take;
        dst += take;
        n -= take;
    }
    return true;
}

static bool equal(const uint8_t *a, const uint8_t *b, size_t n)
{
    uint8_t difference = 0;             /* ohne vorzeitigen Abbruch */
    for (size_t i = 0; i < n; i++)
        difference |= a[i] ^ b[i];
    return difference == 0;
}

/* Der Poly1305-Schlüssel eines Pakets: Anfang von Block 0 des Schlüsselstroms. */
static void chacha_poly_key(const direction_t *d, const uint8_t nonce[12],
                            uint8_t key[32])
{
    memset(key, 0, 32);
    chacha20_crypt(d->key, nonce, 0, key, key, 32);
}

/* Liest das nächste Paket; payload zeigt in den Paketpuffer. */
static bool read_packet(ssh_t *ssh, const uint8_t **payload, size_t *len)
{
    direction_t *d = &ssh->recv;
    uint8_t *p = ssh->rx + 4;           /* davor ist Platz für die Paketnummer */
    uint8_t nonce[12] = { 0 };
    uint32_t length;

    put_be32(nonce + 8, d->seq);
    if (d->kind == CIPHER_CHACHA) {
        uint8_t plain[4], poly_key[32], tag[16];
        if (!raw_read(ssh, p, 4))
            return false;
        chacha20_crypt(d->key + 32, nonce, 0, p, plain, 4);
        length = get_be32(plain);
        if (length < 8 || length > PACKET_MAX || length % 8 != 0)
            return fail(ssh, "Beschädigtes Paket vom Server");
        if (!raw_read(ssh, p + 4, (size_t)length + 16))
            return false;
        chacha_poly_key(d, nonce, poly_key);
        poly1305_mac(poly_key, p, 4 + (size_t)length, tag);
        if (!equal(tag, p + 4 + length, 16))
            return fail(ssh, "Ein Paket wurde unterwegs verändert");
        chacha20_crypt(d->key, nonce, 1, p + 4, p + 4, length);
    } else {
        size_t block = d->kind == CIPHER_AES ? 16 : 8;
        if (!raw_read(ssh, p, block))
            return false;
        if (d->kind == CIPHER_AES)
            aes_ctr_crypt(&d->aes, p, p, block);
        length = get_be32(p);
        if (length < 12 || length > PACKET_MAX || (length + 4) % block != 0)
            return fail(ssh, "Beschädigtes Paket vom Server");
        size_t rest = (size_t)length + 4 - block;
        if (!raw_read(ssh, p + block, rest))
            return false;
        if (d->kind == CIPHER_AES) {
            uint8_t mac[32];
            aes_ctr_crypt(&d->aes, p + block, p + block, rest);
            if (!raw_read(ssh, p + 4 + length, 32))
                return false;
            put_be32(ssh->rx, d->seq);
            hmac_sha256(d->mac_key, 32, ssh->rx, 8 + (size_t)length, mac);
            if (!equal(mac, p + 4 + length, 32))
                return fail(ssh, "Ein Paket wurde unterwegs verändert");
        }
    }
    d->seq++;

    uint32_t padding = p[4];
    if (padding + 2 > length)
        return fail(ssh, "Beschädigtes Paket vom Server");
    *payload = p + 5;
    *len = length - padding - 1;
    return true;
}

static bool write_packet(ssh_t *ssh, const uint8_t *payload, size_t len)
{
    direction_t *d = &ssh->send;
    uint8_t *p = ssh->tx + 4;
    uint8_t nonce[12] = { 0 };
    size_t block = d->kind == CIPHER_AES ? 16 : 8;
    /* Bei ChaCha20 zählt das Längenfeld nicht zur Blocklänge. */
    size_t counted = (d->kind == CIPHER_CHACHA ? 1 : 5) + len;
    size_t padding = block - counted % block;

    if (len > PAYLOAD_MAX)
        return fail(ssh, "Nachricht zu groß");
    if (padding < 4)
        padding += block;
    uint32_t length = (uint32_t)(1 + len + padding);
    size_t total = 4 + (size_t)length;

    put_be32(p, length);
    p[4] = (uint8_t)padding;
    memmove(p + 5, payload, len);
    random_bytes(p + 5 + len, padding);

    put_be32(nonce + 8, d->seq);
    if (d->kind == CIPHER_CHACHA) {
        uint8_t poly_key[32];
        chacha20_crypt(d->key + 32, nonce, 0, p, p, 4);
        chacha20_crypt(d->key, nonce, 1, p + 4, p + 4, length);
        chacha_poly_key(d, nonce, poly_key);
        poly1305_mac(poly_key, p, total, p + total);
        total += 16;
    } else if (d->kind == CIPHER_AES) {
        put_be32(ssh->tx, d->seq);
        hmac_sha256(d->mac_key, 32, ssh->tx, 4 + total, p + total);
        aes_ctr_crypt(&d->aes, p, p, total);
        total += 32;
    }
    d->seq++;
    if (!tcp_send(ssh->tcp, p, total))
        return fail(ssh, "Die Verbindung zum Server ist abgebrochen");
    return true;
}

/* Beginnt eine Nachricht im Sendepuffer. */
static out_t begin(ssh_t *ssh, uint8_t type)
{
    out_t o = { ssh->out, 0, PAYLOAD_MAX };
    put_u8(&o, type);
    return o;
}

static bool send_message(ssh_t *ssh, const out_t *o)
{
    if (o->len > o->cap)
        return fail(ssh, "Nachricht zu groß");
    return write_packet(ssh, o->data, o->len);
}

static bool disconnected(ssh_t *ssh, const uint8_t *payload, size_t len)
{
    in_t i = { payload + 1, payload + len, false };
    const uint8_t *text;
    size_t text_len;
    char reason[120];
    get_u32(&i);
    get_string(&i, &text, &text_len);
    copy_text(reason, sizeof(reason), text, text_len);
    return fail(ssh, "Der Server hat die Verbindung beendet%s%s",
                reason[0] ? ": " : "", reason);
}

/*
 * Wartet auf eine bestimmte Nachricht. tolerant: Belanglose Zwischenrufe
 * (IGNORE, DEBUG) werden übersprungen - im strengen Modus sind sie verboten.
 */
static bool expect(ssh_t *ssh, uint8_t type, bool tolerant, const uint8_t **payload,
                   size_t *len)
{
    for (;;) {
        if (!read_packet(ssh, payload, len))
            return false;
        uint8_t got = (*payload)[0];
        if (got == type)
            return true;
        if (got == MSG_DISCONNECT)
            return disconnected(ssh, *payload, *len);
        if (tolerant && (got == MSG_IGNORE || got == MSG_DEBUG ||
                         got == MSG_UNIMPLEMENTED))
            continue;
        return fail(ssh, "Unerwartete Antwort vom Server (Nachricht %u statt %u)",
                    got, type);
    }
}

/* --- Bekannte Server ------------------------------------------------------------------------ */

static char *known;                     /* Zeilen "name:port verfahren fingerabdruck" */
static bool known_loaded;

static void known_refresh(void)
{
    char *text;
    size_t size;
    if (!known_loaded &&
        storage_load(STORAGE_SETTINGS, KNOWN_FILE, KNOWN_MAX, &text, &size)) {
        kfree(known);
        known = text;
        known_loaded = true;
    }
}

/* Liefert den gemerkten Fingerabdruck des Servers. */
static bool known_find(const char *server, char *fingerprint, size_t size)
{
    size_t n = strlen(server);
    known_refresh();
    for (const char *line = known; line && *line; ) {
        const char *end = line;
        while (*end && *end != '\n')
            end++;
        if ((size_t)(end - line) > n + 1 && memcmp(line, server, n) == 0 &&
            line[n] == ' ') {
            const char *last = end;
            while (last > line && last[-1] != ' ')
                last--;
            size_t len = (size_t)(end - last);
            if (len >= size)
                len = size - 1;
            memcpy(fingerprint, last, len);
            fingerprint[len] = '\0';
            return true;
        }
        line = *end ? end + 1 : end;
    }
    return false;
}

static void known_store(const char *server, const char *type, const char *fingerprint)
{
    size_t n = strlen(server);
    size_t old_len = known ? strlen(known) : 0;
    size_t cap = old_len + n + strlen(type) + strlen(fingerprint) + 8;
    char *text = kmalloc(cap);
    size_t len = 0;
    if (!text)
        return;

    /* Alle anderen Zeilen übernehmen, die für diesen Server ersetzen */
    for (const char *line = known; line && *line; ) {
        const char *end = line;
        while (*end && *end != '\n')
            end++;
        bool same = (size_t)(end - line) > n && memcmp(line, server, n) == 0 &&
                    line[n] == ' ';
        if (!same && end > line) {
            memcpy(text + len, line, (size_t)(end - line));
            len += (size_t)(end - line);
            text[len++] = '\n';
        }
        line = *end ? end + 1 : end;
    }
    len += (size_t)ksnprintf(text + len, cap - len, "%s %s %s\n", server, type,
                             fingerprint);
    kfree(known);
    known = text;
    if (len <= KNOWN_MAX)
        storage_save(STORAGE_SETTINGS, KNOWN_FILE, text, len);
}

/* "SHA256:..." wie bei OpenSSH: Base64 ohne Füllzeichen. */
static void fingerprint_text(const uint8_t hash[32], char *out, size_t size)
{
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char text[48];
    size_t n = 0;
    for (int i = 0; i < 32; i += 3) {
        uint32_t v = (uint32_t)hash[i] << 16;
        if (i + 1 < 32)
            v |= (uint32_t)hash[i + 1] << 8;
        if (i + 2 < 32)
            v |= hash[i + 2];
        text[n++] = alphabet[(v >> 18) & 63];
        text[n++] = alphabet[(v >> 12) & 63];
        if (i + 1 < 32)
            text[n++] = alphabet[(v >> 6) & 63];
        if (i + 2 < 32)
            text[n++] = alphabet[v & 63];
    }
    text[n] = '\0';
    ksnprintf(out, size, "SHA256:%s", text);
}

/* --- Serverschlüssel prüfen ------------------------------------------------------------------- */

/* Prüft die Unterschrift des Servers unter dem Hashwert H des Schlüsselaustauschs. */
static bool verify_signature(const char *algorithm, const uint8_t *key_blob,
                             size_t key_len, const uint8_t *sig_blob, size_t sig_len,
                             const uint8_t exchange_hash[32])
{
    in_t k = { key_blob, key_blob + key_len, false };
    in_t s = { sig_blob, sig_blob + sig_len, false };
    const uint8_t *key_type, *sig_type, *sig;
    size_t key_type_len, sig_type_len, sig_bytes;

    get_string(&k, &key_type, &key_type_len);
    get_string(&s, &sig_type, &sig_type_len);
    get_string(&s, &sig, &sig_bytes);
    if (k.bad || s.bad)
        return false;

    if (strcmp(algorithm, "ssh-ed25519") == 0) {
        const uint8_t *public_key;
        size_t public_len;
        get_string(&k, &public_key, &public_len);
        return !k.bad && string_is(key_type, key_type_len, "ssh-ed25519") &&
               string_is(sig_type, sig_type_len, "ssh-ed25519") &&
               public_len == 32 && sig_bytes == 64 &&
               ed25519_verify(sig, public_key, exchange_hash, 32);
    }

    if (strcmp(algorithm, "rsa-sha2-256") == 0 ||
        strcmp(algorithm, "rsa-sha2-512") == 0) {
        const uint8_t *e, *n;
        size_t e_len, n_len;
        uint8_t digest[HASH_MAX], padded[512];
        get_string(&k, &e, &e_len);
        get_string(&k, &n, &n_len);
        if (k.bad || !string_is(key_type, key_type_len, "ssh-rsa") ||
            !string_is(sig_type, sig_type_len, algorithm))
            return false;
        while (n_len > 0 && n[0] == 0) {
            n++;
            n_len--;
        }
        while (sig_bytes > 0 && sig[0] == 0) {
            sig++;
            sig_bytes--;
        }
        /* Schlüssel unter 2048 Bit gelten nicht mehr als sicher. */
        if (n_len < 256 || n_len > sizeof(padded) || sig_bytes > n_len)
            return false;
        memset(padded, 0, n_len - sig_bytes);
        memcpy(padded + n_len - sig_bytes, sig, sig_bytes);

        hash_alg_t alg = algorithm[9] == '5' ? HASH_SHA512 : HASH_SHA256;
        public_key_t key = { .type = KEY_RSA, .modulus = n, .modulus_len = n_len,
                             .exponent = e, .exponent_len = e_len };
        hash_compute(alg, exchange_hash, 32, digest);
        return rsa_pkcs1_verify(&key, alg, digest, padded, n_len);
    }

    if (strcmp(algorithm, "ecdsa-sha2-nistp256") == 0 ||
        strcmp(algorithm, "ecdsa-sha2-nistp384") == 0) {
        bool p384 = algorithm[16] == '3';       /* ...nistp384 */
        size_t size = p384 ? 48 : 32;
        const uint8_t *curve, *point, *r, *t;
        size_t curve_len, point_len, r_len, t_len;
        uint8_t digest[HASH_MAX], der[160];
        in_t rs = { sig, sig + sig_bytes, false };

        get_string(&k, &curve, &curve_len);
        get_string(&k, &point, &point_len);
        get_string(&rs, &r, &r_len);
        get_string(&rs, &t, &t_len);
        if (k.bad || rs.bad || !string_is(key_type, key_type_len, algorithm) ||
            !string_is(sig_type, sig_type_len, algorithm) ||
            point_len != 1 + 2 * size || point[0] != 0x04 || r_len == 0 ||
            t_len == 0 || r_len > size + 1 || t_len > size + 1)
            return false;

        /* Die Prüffunktion erwartet die Signatur in DER-Form: SEQUENCE { r, s }. */
        size_t n = 0;
        der[n++] = 0x30;
        der[n++] = (uint8_t)(4 + r_len + t_len);
        der[n++] = 0x02;
        der[n++] = (uint8_t)r_len;
        memcpy(der + n, r, r_len);
        n += r_len;
        der[n++] = 0x02;
        der[n++] = (uint8_t)t_len;
        memcpy(der + n, t, t_len);
        n += t_len;

        hash_alg_t alg = p384 ? HASH_SHA384 : HASH_SHA256;
        public_key_t key = { .type = p384 ? KEY_EC_P384 : KEY_EC_P256,
                             .point = point + 1 };
        hash_compute(alg, exchange_hash, 32, digest);
        return ecdsa_verify(&key, digest, hash_size(alg), der, n);
    }
    return false;
}

/* Kennen wir den Schlüssel schon? Sonst entscheidet der Benutzer. */
static bool check_host_key(ssh_t *ssh, const char *algorithm, const uint8_t *blob,
                           size_t len)
{
    uint8_t hash[32];
    char fingerprint[64], stored[64];
    sha256(blob, len, hash);

    if (ssh->have_session) {            /* erneuter Schlüsselaustausch */
        if (!equal(hash, ssh->host_hash, 32))
            return fail(ssh, "Der Server hat mitten in der Sitzung seinen Schlüssel "
                             "gewechselt");
        return true;
    }
    memcpy(ssh->host_hash, hash, 32);
    fingerprint_text(hash, fingerprint, sizeof(fingerprint));

    bool have = known_find(ssh->server, stored, sizeof(stored));
    if (have && strcmp(stored, fingerprint) == 0)
        return true;
    /* Schlüsselart ohne den Zusatz des Signaturverfahrens anzeigen */
    const char *type = algorithm[0] == 'r' ? "ssh-rsa" : algorithm;
    if (!ssh->trust || !ssh->trust(ssh->trust_ctx, ssh->server, type, fingerprint, have))
        return fail(ssh, have ? "Abgebrochen: Der Schlüssel des Servers hat sich geändert"
                              : "Abgebrochen: Dem Schlüssel des Servers wurde nicht vertraut");
    known_store(ssh->server, type, fingerprint);
    return true;
}

/* --- Schlüsselaustausch ------------------------------------------------------------------------ */

static void hash_string(sha256_t *ctx, const void *data, size_t len)
{
    uint8_t length[4];
    put_be32(length, (uint32_t)len);
    sha256_update(ctx, length, 4);
    sha256_update(ctx, data, len);
}

/* Leitet einen der sechs Schlüssel ab: HASH(K || H || Buchstabe || Sitzungskennung). */
static void derive(const ssh_t *ssh, const uint8_t *k, size_t k_len,
                   const uint8_t h[32], char letter, uint8_t *out, size_t need)
{
    uint8_t block[32];
    sha256_t ctx;
    size_t have = 0;

    sha256_init(&ctx);
    sha256_update(&ctx, k, k_len);
    sha256_update(&ctx, h, 32);
    sha256_update(&ctx, &letter, 1);
    sha256_update(&ctx, ssh->session_id, 32);
    sha256_final(&ctx, block);
    while (have < need) {
        size_t n = need - have < 32 ? need - have : 32;
        memcpy(out + have, block, n);
        have += n;
        if (have < need) {              /* Verlängerung: HASH(K || H || bisheriges) */
            sha256_init(&ctx);
            sha256_update(&ctx, k, k_len);
            sha256_update(&ctx, h, 32);
            sha256_update(&ctx, out, have);
            sha256_final(&ctx, block);
        }
    }
}

static size_t cipher_key_size(const char *name)
{
    if (name[0] == 'c')
        return 64;
    return name[3] == '1' ? (name[4] == '2' ? 16 : 24) : 32;    /* aes128/192/256 */
}

static void setup_direction(const ssh_t *ssh, direction_t *d, const char *cipher,
                            const uint8_t *k, size_t k_len, const uint8_t h[32],
                            char iv_letter, char key_letter, char mac_letter)
{
    uint8_t iv[16], key[64];
    uint32_t seq = d->seq;
    size_t key_size = cipher_key_size(cipher);

    memset(d, 0, sizeof(*d));
    d->seq = seq;
    derive(ssh, k, k_len, h, key_letter, key, key_size);
    if (cipher[0] == 'c') {
        d->kind = CIPHER_CHACHA;
        memcpy(d->key, key, 64);
    } else {
        d->kind = CIPHER_AES;
        derive(ssh, k, k_len, h, iv_letter, iv, 16);
        aes_ctr_init(&d->aes, key, key_size, iv);
        derive(ssh, k, k_len, h, mac_letter, d->mac_key, 32);
    }
    memset(key, 0, sizeof(key));
}

/* Kopiert eine Nachricht in eigenen Speicher (der Paketpuffer wird wiederverwendet). */
static uint8_t *duplicate(const uint8_t *data, size_t len)
{
    uint8_t *copy = kmalloc(len ? len : 1);
    if (copy)
        memcpy(copy, data, len);
    return copy;
}

/*
 * Führt einen Schlüsselaustausch durch. their_init: die KEXINIT-Nachricht des
 * Servers, wenn er (später in der Sitzung) von sich aus damit begonnen hat.
 */
static bool key_exchange(ssh_t *ssh, const uint8_t *their_init, size_t their_len)
{
    bool initial = !ssh->have_session;
    uint8_t *ours = NULL, *theirs = NULL;
    size_t ours_len = 0;
    uint8_t secret[32], shared[32], k[4 + 33], h[32];
    bool ok = false;

    /* 1. Unsere Verfahren nennen */
    out_t m = begin(ssh, MSG_KEXINIT);
    uint8_t cookie[16];
    char kex_list[160];
    random_bytes(cookie, sizeof(cookie));
    put_bytes(&m, cookie, sizeof(cookie));
    ksnprintf(kex_list, sizeof(kex_list), "%s%s", KEX_ALGS, initial ? KEX_STRICT : "");
    put_text(&m, kex_list);
    put_text(&m, HOSTKEY_ALGS);
    put_text(&m, CIPHER_ALGS);
    put_text(&m, CIPHER_ALGS);
    put_text(&m, MAC_ALGS);
    put_text(&m, MAC_ALGS);
    put_text(&m, "none");               /* Kompression */
    put_text(&m, "none");
    put_text(&m, "");                   /* Sprachen */
    put_text(&m, "");
    put_u8(&m, 0);                      /* kein vorauseilendes Paket */
    put_u32(&m, 0);
    ours = duplicate(m.data, m.len);
    ours_len = m.len;
    if (!ours || !send_message(ssh, &m))
        goto done;

    /* 2. Die des Servers lesen und je das erste gemeinsame wählen */
    if (!their_init) {
        if (!expect(ssh, MSG_KEXINIT, true, &their_init, &their_len))
            goto done;
    }
    theirs = duplicate(their_init, their_len);
    if (!theirs || their_len < 17) {
        fail(ssh, "Nicht genug Speicher");
        goto done;
    }

    in_t i = { theirs + 17, theirs + their_len, false };
    const uint8_t *list[10];
    size_t list_len[10];
    for (int n = 0; n < 10; n++)
        get_string(&i, &list[n], &list_len[n]);
    bool guess_follows = get_u8(&i);
    if (i.bad) {
        fail(ssh, "Unverständliche Antwort vom Server");
        goto done;
    }

    char kex[40], hostkey[32], cipher_out[40], cipher_in[40], mac[20];
    char offered[100];
    if (!choose(KEX_ALGS, list[0], list_len[0], kex, sizeof(kex))) {
        copy_text(offered, sizeof(offered), list[0], list_len[0]);
        fail(ssh, "Kein gemeinsamer Schlüsselaustausch (Server bietet: %s)", offered);
        goto done;
    }
    if (!choose(HOSTKEY_ALGS, list[1], list_len[1], hostkey, sizeof(hostkey))) {
        copy_text(offered, sizeof(offered), list[1], list_len[1]);
        fail(ssh, "Kein unterstützter Serverschlüssel (Server bietet: %s)", offered);
        goto done;
    }
    if (!choose(CIPHER_ALGS, list[2], list_len[2], cipher_out, sizeof(cipher_out)) ||
        !choose(CIPHER_ALGS, list[3], list_len[3], cipher_in, sizeof(cipher_in))) {
        copy_text(offered, sizeof(offered), list[2], list_len[2]);
        fail(ssh, "Keine gemeinsame Verschlüsselung (Server bietet: %s)", offered);
        goto done;
    }
    if ((cipher_out[0] == 'a' && !choose(MAC_ALGS, list[4], list_len[4], mac, sizeof(mac))) ||
        (cipher_in[0] == 'a' && !choose(MAC_ALGS, list[5], list_len[5], mac, sizeof(mac)))) {
        copy_text(offered, sizeof(offered), list[4], list_len[4]);
        fail(ssh, "Keine gemeinsame Prüfsumme (Server bietet: %s)", offered);
        goto done;
    }
    if (!list_has(list[6], list_len[6], "none") || !list_has(list[7], list_len[7], "none")) {
        fail(ssh, "Der Server verlangt Kompression - das kann HelloOS nicht");
        goto done;
    }
    if (initial && list_has(list[0], list_len[0], "kex-strict-s-v00@openssh.com")) {
        ssh->strict = true;
        if (ssh->recv.seq != 1) {       /* KEXINIT muss das allererste Paket gewesen sein */
            fail(ssh, "Unerwartete Pakete vor dem Schlüsselaustausch");
            goto done;
        }
    }
    bool tolerant = !(ssh->strict && initial);

    /* Hat der Server auf Verdacht schon weitergesendet und falsch geraten: verwerfen */
    if (guess_follows) {
        char first[40];
        size_t n = 0;
        while (n < list_len[0] && n + 1 < sizeof(first) && list[0][n] != ',')
            n++;
        memcpy(first, list[0], n);
        first[n] = '\0';
        const uint8_t *skip;
        size_t skip_len;
        if (strcmp(first, kex) != 0 && !read_packet(ssh, &skip, &skip_len))
            goto done;
    }

    /* 3. ECDH: eigenen öffentlichen Wert schicken, den des Servers bekommen */
    bool curve25519 = kex[0] == 'c';
    uint8_t public_value[65];
    size_t public_len;
    if (curve25519) {
        random_bytes(secret, 32);
        x25519_public(public_value, secret);
        public_len = 32;
    } else {
        do {
            random_bytes(secret, 32);
        } while (!ecdh_p256_public(secret, public_value + 1));
        public_value[0] = 0x04;         /* "unkomprimierter Punkt" */
        public_len = 65;
    }
    m = begin(ssh, MSG_KEX_ECDH_INIT);
    put_string(&m, public_value, public_len);
    if (!send_message(ssh, &m))
        goto done;

    const uint8_t *reply, *host_key, *server_value, *signature;
    size_t reply_len, host_key_len, server_len, signature_len;
    if (!expect(ssh, MSG_KEX_ECDH_REPLY, tolerant, &reply, &reply_len))
        goto done;
    i = (in_t){ reply + 1, reply + reply_len, false };
    get_string(&i, &host_key, &host_key_len);
    get_string(&i, &server_value, &server_len);
    get_string(&i, &signature, &signature_len);
    if (i.bad) {
        fail(ssh, "Unverständliche Antwort vom Server");
        goto done;
    }

    bool valid;
    if (curve25519) {
        uint8_t zero[32] = { 0 };
        valid = server_len == 32;
        if (valid) {
            x25519(shared, secret, server_value);
            valid = !equal(shared, zero, 32);
        }
    } else {
        valid = server_len == 65 && server_value[0] == 0x04 &&
                ecdh_p256_shared(secret, server_value + 1, shared);
    }
    if (!valid) {
        fail(ssh, "Der Schlüsselaustausch ist fehlgeschlagen");
        goto done;
    }

    /* Das Geheimnis als Zahl (mpint): ohne führende Nullen, nie "negativ" */
    size_t skip = 0, k_len = 0;
    while (skip < 31 && shared[skip] == 0)
        skip++;
    bool extra = shared[skip] & 0x80;
    put_be32(k, (uint32_t)(32 - skip + (extra ? 1 : 0)));
    k_len = 4;
    if (extra)
        k[k_len++] = 0;
    memcpy(k + k_len, shared + skip, 32 - skip);
    k_len += 32 - skip;

    /* 4. H = Hash über beide Versionen, beide KEXINIT, Serverschlüssel, ECDH-Werte, K */
    sha256_t ctx;
    sha256_init(&ctx);
    hash_string(&ctx, CLIENT_VERSION, strlen(CLIENT_VERSION));
    hash_string(&ctx, ssh->server_version, strlen(ssh->server_version));
    hash_string(&ctx, ours, ours_len);
    hash_string(&ctx, theirs, their_len);
    hash_string(&ctx, host_key, host_key_len);
    hash_string(&ctx, public_value, public_len);
    hash_string(&ctx, server_value, server_len);
    sha256_update(&ctx, k, k_len);
    sha256_final(&ctx, h);

    /* 5. Der Server muss H mit seinem Schlüssel unterschrieben haben */
    if (!verify_signature(hostkey, host_key, host_key_len, signature, signature_len, h)) {
        fail(ssh, "Der Server konnte sich nicht ausweisen (Unterschrift ungültig)");
        goto done;
    }
    if (!check_host_key(ssh, hostkey, host_key, host_key_len))
        goto done;
    if (initial) {
        memcpy(ssh->session_id, h, 32);
        ssh->have_session = true;
    }

    /* 6. Auf die neuen Schlüssel umschalten - jede Richtung für sich */
    const uint8_t *newkeys;
    size_t newkeys_len;
    m = begin(ssh, MSG_NEWKEYS);
    if (!send_message(ssh, &m))
        goto done;
    setup_direction(ssh, &ssh->send, cipher_out, k, k_len, h, 'A', 'C', 'E');
    if (ssh->strict)
        ssh->send.seq = 0;
    if (!expect(ssh, MSG_NEWKEYS, tolerant, &newkeys, &newkeys_len))
        goto done;
    setup_direction(ssh, &ssh->recv, cipher_in, k, k_len, h, 'B', 'D', 'F');
    if (ssh->strict)
        ssh->recv.seq = 0;
    ok = true;

done:
    memset(secret, 0, sizeof(secret));
    memset(shared, 0, sizeof(shared));
    memset(k, 0, sizeof(k));
    kfree(ours);
    kfree(theirs);
    return ok;
}

/* --- Laufende Sitzung: eingehende Nachrichten verteilen ------------------------------------------ */

static bool store_data(ssh_t *ssh, const uint8_t *data, size_t len)
{
    if (len > ssh->local_window)
        return fail(ssh, "Der Server sendet mehr als vereinbart");
    ssh->local_window -= (uint32_t)len;

    if (ssh->data_pos == ssh->data_len)
        ssh->data_pos = ssh->data_len = 0;
    if (ssh->data_len + len > ssh->data_cap) {
        size_t pending = ssh->data_len - ssh->data_pos;
        size_t cap = (pending + len) * 2 + 4096;
        uint8_t *bigger = kmalloc(cap);
        if (!bigger)
            return fail(ssh, "Nicht genug Speicher");
        memcpy(bigger, ssh->data + ssh->data_pos, pending);
        kfree(ssh->data);
        ssh->data = bigger;
        ssh->data_cap = cap;
        ssh->data_pos = 0;
        ssh->data_len = pending;
    }
    memcpy(ssh->data + ssh->data_len, data, len);
    ssh->data_len += len;
    return true;
}

/* Liest ein Paket und verarbeitet es. */
static bool pump(ssh_t *ssh)
{
    const uint8_t *payload, *text;
    size_t len, text_len;
    out_t m;

    if (!read_packet(ssh, &payload, &len))
        return false;
    in_t i = { payload + 1, payload + len, false };

    switch (payload[0]) {
    case MSG_DISCONNECT:
        return disconnected(ssh, payload, len);
    case MSG_KEXINIT:                   /* der Server möchte frische Schlüssel */
        return key_exchange(ssh, payload, len);
    case MSG_GLOBAL_REQUEST:
        get_string(&i, &text, &text_len);
        if (get_u8(&i)) {               /* Antwort gewünscht: kennen wir nicht */
            m = begin(ssh, MSG_REQUEST_FAILURE);
            return send_message(ssh, &m);
        }
        return true;
    case MSG_CHANNEL_OPEN_CONFIRMATION:
        get_u32(&i);
        ssh->remote_channel = get_u32(&i);
        ssh->remote_window = get_u32(&i);
        ssh->remote_max = get_u32(&i);
        ssh->open_reply = i.bad ? 2 : 1;
        return true;
    case MSG_CHANNEL_OPEN_FAILURE:
        ssh->open_reply = 2;
        return true;
    case MSG_CHANNEL_WINDOW_ADJUST:
        get_u32(&i);
        ssh->remote_window += get_u32(&i);
        return true;
    case MSG_CHANNEL_DATA:
        get_u32(&i);
        get_string(&i, &text, &text_len);
        if (i.bad)
            return fail(ssh, "Beschädigtes Paket vom Server");
        return store_data(ssh, text, text_len);
    case MSG_CHANNEL_EXTENDED_DATA:     /* Fehlerausgabe des Dienstes: nur mitzählen */
        get_u32(&i);
        get_u32(&i);
        get_string(&i, &text, &text_len);
        if (!i.bad && text_len <= ssh->local_window)
            ssh->local_window -= (uint32_t)text_len;
        return true;
    case MSG_CHANNEL_EOF:
    case MSG_CHANNEL_CLOSE:
        ssh->channel_closed = true;
        return true;
    case MSG_CHANNEL_REQUEST:
        get_u32(&i);
        get_string(&i, &text, &text_len);
        if (get_u8(&i) && !i.bad) {
            m = begin(ssh, MSG_CHANNEL_FAILURE);
            put_u32(&m, ssh->remote_channel);
            return send_message(ssh, &m);
        }
        return true;
    case MSG_CHANNEL_SUCCESS:
        ssh->channel_reply = 1;
        return true;
    case MSG_CHANNEL_FAILURE:
        ssh->channel_reply = 2;
        return true;
    default:                            /* IGNORE, DEBUG und Unbekanntes */
        return true;
    }
}

/* --- Anmeldung ---------------------------------------------------------------------------------- */

static bool authenticate(ssh_t *ssh, const char *user, const char *password)
{
    const uint8_t *payload;
    size_t len;
    bool interactive = false;           /* schon auf "keyboard-interactive" gewechselt */

    out_t m = begin(ssh, MSG_SERVICE_REQUEST);
    put_text(&m, "ssh-userauth");
    if (!send_message(ssh, &m) ||
        !expect(ssh, MSG_SERVICE_ACCEPT, true, &payload, &len))
        return false;

    m = begin(ssh, MSG_USERAUTH_REQUEST);
    put_text(&m, user);
    put_text(&m, "ssh-connection");
    put_text(&m, "password");
    put_u8(&m, 0);
    put_text(&m, password);
    bool sent = send_message(ssh, &m);
    memset(ssh->out, 0, PAYLOAD_MAX);   /* das Passwort nicht im Puffer stehen lassen */
    if (!sent)
        return false;

    for (;;) {
        if (!read_packet(ssh, &payload, &len))
            return false;
        in_t i = { payload + 1, payload + len, false };
        const uint8_t *text;
        size_t text_len;

        switch (payload[0]) {
        case MSG_USERAUTH_SUCCESS:
            return true;
        case MSG_DISCONNECT:
            return disconnected(ssh, payload, len);
        case MSG_USERAUTH_BANNER:
        case MSG_IGNORE:
        case MSG_DEBUG:
            continue;
        case MSG_USERAUTH_FAILURE:
            get_string(&i, &text, &text_len);
            if (!interactive && !i.bad &&
                list_has(text, text_len, "keyboard-interactive")) {
                /* Manche Server fragen das Passwort nur auf diesem Weg ab. */
                interactive = true;
                m = begin(ssh, MSG_USERAUTH_REQUEST);
                put_text(&m, user);
                put_text(&m, "ssh-connection");
                put_text(&m, "keyboard-interactive");
                put_text(&m, "");
                put_text(&m, "");
                if (!send_message(ssh, &m))
                    return false;
                continue;
            }
            if (!i.bad && !list_has(text, text_len, "password") &&
                !list_has(text, text_len, "keyboard-interactive"))
                return fail(ssh, "Der Server erlaubt keine Anmeldung mit Passwort "
                                 "(nur mit Schlüsseldatei)");
            return fail(ssh, "Anmeldung abgelehnt - Benutzername oder Passwort falsch");
        case MSG_USERAUTH_INFO_REQUEST: {
            if (!interactive)           /* Nummer 60 heißt hier: Passwort abgelaufen */
                return fail(ssh, "Der Server verlangt ein neues Passwort - bitte "
                                 "zuerst dort ändern");
            get_string(&i, &text, &text_len);       /* Name */
            get_string(&i, &text, &text_len);       /* Anweisung */
            get_string(&i, &text, &text_len);       /* Sprache */
            uint32_t prompts = get_u32(&i);
            if (i.bad || prompts > 8)
                return fail(ssh, "Unverständliche Rückfrage des Servers");
            /* Jede Frage (üblich ist genau eine: "Password:") bekommt das Passwort. */
            m = begin(ssh, MSG_USERAUTH_INFO_RESPONSE);
            put_u32(&m, prompts);
            for (uint32_t n = 0; n < prompts; n++)
                put_text(&m, password);
            sent = send_message(ssh, &m);
            memset(ssh->out, 0, PAYLOAD_MAX);
            if (!sent)
                return false;
            continue;
        }
        default:
            return fail(ssh, "Unerwartete Antwort bei der Anmeldung (Nachricht %u)",
                        payload[0]);
        }
    }
}

/* --- Öffentliche Schnittstelle ----------------------------------------------------------------- */

/* Liest die Versionszeile des Servers ("SSH-2.0-..."), davor darf Text stehen. */
static bool read_version(ssh_t *ssh)
{
    for (int lines = 0; lines < 20; lines++) {
        size_t n = 0;
        for (;;) {
            uint8_t c;
            if (!raw_read(ssh, &c, 1))
                return false;
            if (c == '\n')
                break;
            if (n + 1 < sizeof(ssh->server_version))
                ssh->server_version[n++] = (char)c;
        }
        if (n > 0 && ssh->server_version[n - 1] == '\r')
            n--;
        ssh->server_version[n] = '\0';
        if (memcmp(ssh->server_version, "SSH-", 4) == 0) {
            if (memcmp(ssh->server_version, "SSH-2.0-", 8) != 0 &&
                memcmp(ssh->server_version, "SSH-1.99-", 9) != 0)
                return fail(ssh, "Der Server spricht nur eine veraltete SSH-Version");
            return true;
        }
    }
    return fail(ssh, "Dort antwortet kein SSH-Server");
}

ssh_t *ssh_connect(const ssh_options_t *options, char *error, size_t error_size)
{
    uint16_t port = options->port ? options->port : 22;
    ip4_t ip;
    ssh_t *ssh = kzalloc(sizeof(*ssh));

    error[0] = '\0';
    if (ssh) {
        ssh->in = kmalloc(IN_SIZE);
        ssh->rx = kmalloc(PACKET_MAX + 64);
        ssh->tx = kmalloc(PACKET_MAX + 64);
        ssh->out = kmalloc(PAYLOAD_MAX);
    }
    if (!ssh || !ssh->in || !ssh->rx || !ssh->tx || !ssh->out) {
        ksnprintf(error, error_size, "Nicht genug Speicher");
        ssh_close(ssh);
        return NULL;
    }
    ssh->trust = options->trust;
    ssh->trust_ctx = options->trust_ctx;
    ksnprintf(ssh->server, sizeof(ssh->server), "%s:%u", options->host, port);

    char line[sizeof(CLIENT_VERSION) + 2];
    ksnprintf(line, sizeof(line), "%s\r\n", CLIENT_VERSION);

    bool ok = false;
    if (!net_resolve(options->host, &ip))
        fail(ssh, "Name nicht auflösbar: %s", options->host);
    else if (!(ssh->tcp = tcp_connect(ip, port, 10000)))
        fail(ssh, "Keine Verbindung zu %s (Port %u)", options->host, port);
    else if (!tcp_send(ssh->tcp, line, strlen(line)))
        fail(ssh, "Die Verbindung zum Server ist abgebrochen");
    else
        ok = read_version(ssh) && key_exchange(ssh, NULL, 0) &&
             authenticate(ssh, options->user, options->password);

    if (!ok) {
        strlcpy(error, ssh_error(ssh), error_size);
        ssh_close(ssh);
        return NULL;
    }
    return ssh;
}

bool ssh_open_subsystem(ssh_t *ssh, const char *name)
{
    out_t m = begin(ssh, MSG_CHANNEL_OPEN);
    put_text(&m, "session");
    put_u32(&m, 0);                     /* unsere Kanalnummer */
    put_u32(&m, LOCAL_WINDOW);
    put_u32(&m, LOCAL_PACKET);
    ssh->local_window = LOCAL_WINDOW;
    ssh->open_reply = 0;
    if (!send_message(ssh, &m))
        return false;
    while (ssh->open_reply == 0) {
        if (!pump(ssh))
            return false;
    }
    if (ssh->open_reply != 1)
        return fail(ssh, "Der Server hat keinen Kanal geöffnet");
    ssh->channel_open = true;

    m = begin(ssh, MSG_CHANNEL_REQUEST);
    put_u32(&m, ssh->remote_channel);
    put_text(&m, "subsystem");
    put_u8(&m, 1);                      /* Antwort erwünscht */
    put_text(&m, name);
    ssh->channel_reply = 0;
    if (!send_message(ssh, &m))
        return false;
    while (ssh->channel_reply == 0 && !ssh->channel_closed) {
        if (!pump(ssh))
            return false;
    }
    if (ssh->channel_reply != 1)
        return fail(ssh, "Der Server bietet den Dienst \"%s\" nicht an", name);
    return true;
}

bool ssh_write(ssh_t *ssh, const void *data, size_t len)
{
    const uint8_t *p = data;
    while (len > 0) {
        if (ssh->channel_closed)
            return fail(ssh, "Der Server hat die Sitzung beendet");
        if (ssh->remote_window == 0) {  /* warten, bis der Server wieder Platz meldet */
            if (!pump(ssh))
                return false;
            continue;
        }
        size_t n = len;
        if (n > CHUNK_MAX)
            n = CHUNK_MAX;
        if (n > ssh->remote_window)
            n = ssh->remote_window;
        if (ssh->remote_max >= 1024 && n > ssh->remote_max)
            n = ssh->remote_max;

        out_t m = begin(ssh, MSG_CHANNEL_DATA);
        put_u32(&m, ssh->remote_channel);
        put_string(&m, p, n);
        if (!send_message(ssh, &m))
            return false;
        ssh->remote_window -= (uint32_t)n;
        p += n;
        len -= n;
    }
    return true;
}

bool ssh_read(ssh_t *ssh, void *buffer, size_t len)
{
    uint8_t *out = buffer;
    while (len > 0) {
        size_t available = ssh->data_len - ssh->data_pos;
        if (available == 0) {
            if (ssh->channel_closed)
                return fail(ssh, "Der Server hat die Sitzung beendet");
            if (!pump(ssh))
                return false;
            continue;
        }
        size_t n = available < len ? available : len;
        memcpy(out, ssh->data + ssh->data_pos, n);
        ssh->data_pos += n;
        out += n;
        len -= n;
    }
    /* Dem Server wieder Platz melden, bevor sein Fenster zu klein wird */
    if (ssh->local_window < LOCAL_WINDOW / 2) {
        out_t m = begin(ssh, MSG_CHANNEL_WINDOW_ADJUST);
        put_u32(&m, ssh->remote_channel);
        put_u32(&m, LOCAL_WINDOW - ssh->local_window);
        if (!send_message(ssh, &m))
            return false;
        ssh->local_window = LOCAL_WINDOW;
    }
    return true;
}

void ssh_close(ssh_t *ssh)
{
    if (!ssh)
        return;
    if (ssh->tcp) {
        if (ssh->have_session && ssh->out && ssh->tx) {
            /* Höflich verabschieden; Fehler dabei sind egal. */
            out_t m = begin(ssh, MSG_DISCONNECT);
            put_u32(&m, 11);            /* "von der Anwendung beendet" */
            put_text(&m, "");
            put_text(&m, "");
            write_packet(ssh, m.data, m.len);
        }
        tcp_close(ssh->tcp);
    }
    kfree(ssh->in);
    kfree(ssh->rx);
    kfree(ssh->tx);
    kfree(ssh->out);
    kfree(ssh->data);
    memset(ssh, 0, sizeof(*ssh));       /* auch die Sitzungsschlüssel */
    kfree(ssh);
}
