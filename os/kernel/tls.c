/*
 * TLS 1.3 (RFC 8446), nur die Client-Seite und nur das Nötigste:
 * Schlüsselaustausch X25519, Verschlüsselung ChaCha20-Poly1305, Hash SHA-256.
 *
 * Ablauf: Wir schicken ein ClientHello mit unserem öffentlichen X25519-Wert,
 * der Server antwortet mit seinem. Aus dem gemeinsamen Geheimnis werden über
 * eine feste Kette von Ableitungen (HKDF) die Schlüssel berechnet. Der Rest
 * des Verbindungsaufbaus und alle Nutzdaten laufen danach verschlüsselt.
 *
 * Siehe tls.h: Das Zertifikat des Servers wird NICHT geprüft.
 */
#include "tls.h"
#include "console.h"
#include "crypto.h"
#include "memory.h"
#include "string.h"

#define RECORD_MAX      (16384 + 256)
#define HANDSHAKE_MAX   (64 * 1024)
#define CHUNK           8192            /* Nutzdaten je Record beim Senden */
#define HANDSHAKE_TIMEOUT 10000

enum { REC_CHANGE_CIPHER = 20, REC_ALERT = 21, REC_HANDSHAKE = 22, REC_DATA = 23 };
enum {
    HS_CLIENT_HELLO = 1, HS_SERVER_HELLO = 2, HS_NEW_SESSION_TICKET = 4,
    HS_ENCRYPTED_EXTENSIONS = 8, HS_CERTIFICATE = 11,
    HS_CERTIFICATE_REQUEST = 13, HS_CERTIFICATE_VERIFY = 15, HS_FINISHED = 20,
    HS_KEY_UPDATE = 24,
};

#define SUITE_CHACHA20_POLY1305_SHA256 0x1303
#define GROUP_X25519                   0x001D
#define VERSION_TLS13                  0x0304

struct keys {
    uint8_t secret[32];
    uint8_t key[32];
    uint8_t iv[12];
    uint64_t sequence;                  /* Nummer des nächsten Records */
};

struct tls {
    tcp_conn_t *tcp;
    sha256_t transcript;                /* Hash über alle Handshake-Nachrichten */
    struct keys tx, rx;
    bool encrypted;

    uint8_t *record;                    /* empfangener Record, roh */
    uint8_t *plain;                     /* dessen entschlüsselter Inhalt */
    size_t plain_pos, plain_len;        /* noch nicht abgeholte Nutzdaten */
    uint8_t *out;                       /* Puffer zum Senden */

    uint8_t *handshake;                 /* zusammengesetzte Handshake-Daten */
    size_t handshake_len;

    bool closed;
    char *error;
    size_t error_size;
};

static void fail(tls_t *t, const char *message)
{
    if (t->error && !t->error[0])
        ksnprintf(t->error, t->error_size, "TLS: %s", message);
}

/* --- Schlüsselableitung ----------------------------------------------------------- */

/* HKDF-Expand-Label aus RFC 8446, Abschnitt 7.1 */
static void expand_label(const uint8_t secret[32], const char *label,
                         const uint8_t *context, size_t context_len,
                         uint8_t *out, size_t out_len)
{
    uint8_t info[2 + 1 + 6 + 32 + 1 + 32];
    size_t label_len = strlen(label);
    size_t n = 0;

    info[n++] = 0;
    info[n++] = (uint8_t)out_len;
    info[n++] = (uint8_t)(6 + label_len);
    memcpy(info + n, "tls13 ", 6);
    n += 6;
    memcpy(info + n, label, label_len);
    n += label_len;
    info[n++] = (uint8_t)context_len;
    if (context_len)
        memcpy(info + n, context, context_len);
    n += context_len;
    hkdf_expand(secret, info, n, out, out_len);
}

static void transcript_hash(const tls_t *t, uint8_t out[32])
{
    sha256_t copy = t->transcript;
    sha256_final(&copy, out);
}

/* Leitet aus einem Geheimnis Schlüssel und Startwert für eine Richtung ab. */
static void set_keys(struct keys *k, const uint8_t secret[32])
{
    memcpy(k->secret, secret, 32);
    expand_label(secret, "key", NULL, 0, k->key, 32);
    expand_label(secret, "iv", NULL, 0, k->iv, 12);
    k->sequence = 0;
}

/* Jeder Record bekommt einen eigenen Einmalwert: Startwert XOR Zählerstand. */
static void make_nonce(struct keys *k, uint8_t nonce[12])
{
    memcpy(nonce, k->iv, 12);
    for (int i = 0; i < 8; i++)
        nonce[11 - i] ^= (uint8_t)(k->sequence >> (8 * i));
    k->sequence++;
}

/* --- Records senden und empfangen ---------------------------------------------------- */

static bool read_exact(tls_t *t, uint8_t *buffer, size_t len, uint32_t timeout_ms)
{
    size_t got = 0;
    while (got < len) {
        int n = tcp_recv(t->tcp, buffer + got, len - got, timeout_ms);
        if (n <= 0)
            return false;
        got += (size_t)n;
    }
    return true;
}

static bool send_plain_record(tls_t *t, uint8_t type, const uint8_t *data,
                              size_t len)
{
    uint8_t *out = t->out;
    out[0] = type;
    out[1] = 0x03;
    out[2] = 0x01;
    out[3] = (uint8_t)(len >> 8);
    out[4] = (uint8_t)len;
    memcpy(out + 5, data, len);
    return tcp_send(t->tcp, out, 5 + len);
}

/* Verschlüsselter Record: Der wahre Typ steht als letztes Byte im Geheimtext. */
static bool send_record(tls_t *t, uint8_t type, const uint8_t *data, size_t len)
{
    uint8_t *out = t->out;
    uint8_t nonce[12];
    size_t body = len + 1 + 16;

    out[0] = REC_DATA;
    out[1] = 0x03;
    out[2] = 0x03;
    out[3] = (uint8_t)(body >> 8);
    out[4] = (uint8_t)body;
    memcpy(out + 5, data, len);
    out[5 + len] = type;

    make_nonce(&t->tx, nonce);
    chacha20poly1305_encrypt(t->tx.key, nonce, out, 5, out + 5, len + 1, out + 5,
                             out + 5 + len + 1);
    return tcp_send(t->tcp, out, 5 + body);
}

/*
 * Liest den nächsten Record; sein Inhalt liegt danach in t->plain.
 * Liefert 1 bei Erfolg, 0 wenn die Gegenseite die Verbindung beendet hat,
 * -1 bei Fehler.
 */
static int read_record(tls_t *t, uint8_t *type, size_t *len, uint32_t timeout_ms)
{
    for (;;) {
        uint8_t header[5];
        int n = tcp_recv(t->tcp, header, 1, timeout_ms);
        if (n == 0)
            return 0;
        if (n < 0 || !read_exact(t, header + 1, 4, timeout_ms)) {
            fail(t, "Verbindung abgebrochen");
            return -1;
        }
        size_t length = ((size_t)header[3] << 8) | header[4];
        if (length > RECORD_MAX) {
            fail(t, "Record zu groß");
            return -1;
        }
        if (!read_exact(t, t->record, length, timeout_ms)) {
            fail(t, "Verbindung abgebrochen");
            return -1;
        }

        if (header[0] == REC_CHANGE_CIPHER)
            continue;                   /* Überbleibsel aus TLS 1.2: ignorieren */

        if (header[0] == REC_DATA && t->encrypted) {
            uint8_t nonce[12];
            if (length < 17) {
                fail(t, "Record zu kurz");
                return -1;
            }
            make_nonce(&t->rx, nonce);
            if (!chacha20poly1305_decrypt(t->rx.key, nonce, header, 5, t->record,
                                          length - 16, t->record + length - 16,
                                          t->plain)) {
                fail(t, "Daten wurden unterwegs verändert");
                return -1;
            }
            /* Hinten: Typ, davor evtl. Füllbytes (Nullen) */
            size_t end = length - 16;
            while (end > 0 && t->plain[end - 1] == 0)
                end--;
            if (end == 0) {
                fail(t, "leerer Record");
                return -1;
            }
            *type = t->plain[end - 1];
            *len = end - 1;
        } else {
            memcpy(t->plain, t->record, length);
            *type = header[0];
            *len = length;
        }

        if (*type == REC_ALERT) {
            if (*len >= 2 && t->plain[1] == 0)
                return 0;               /* close_notify: ordentliches Ende */
            if (t->error && !t->error[0])
                ksnprintf(t->error, t->error_size,
                          "TLS: Server meldet Fehler %u",
                          *len >= 2 ? t->plain[1] : 0);
            return -1;
        }
        return 1;
    }
}

/* --- Handshake ------------------------------------------------------------------------ */

/*
 * Liefert die nächste vollständige Handshake-Nachricht (Kopf + Inhalt).
 * Nachrichten können über mehrere Records verteilt sein oder zu mehreren in
 * einem Record stehen - daher der Zwischenpuffer.
 */
static bool next_message(tls_t *t, uint8_t **message, size_t *len)
{
    for (;;) {
        if (t->handshake_len >= 4) {
            size_t body = ((size_t)t->handshake[1] << 16) |
                          ((size_t)t->handshake[2] << 8) | t->handshake[3];
            if (4 + body > HANDSHAKE_MAX) {
                fail(t, "Handshake-Nachricht zu groß");
                return false;
            }
            if (t->handshake_len >= 4 + body) {
                *message = t->handshake;
                *len = 4 + body;
                return true;
            }
        }
        uint8_t type;
        size_t n;
        if (read_record(t, &type, &n, HANDSHAKE_TIMEOUT) <= 0) {
            fail(t, "Verbindung beim Aufbau beendet");
            return false;
        }
        if (type != REC_HANDSHAKE || t->handshake_len + n > HANDSHAKE_MAX) {
            fail(t, "unerwartete Daten beim Aufbau");
            return false;
        }
        memcpy(t->handshake + t->handshake_len, t->plain, n);
        t->handshake_len += n;
    }
}

static void consume_message(tls_t *t, size_t len)
{
    memmove(t->handshake, t->handshake + len, t->handshake_len - len);
    t->handshake_len -= len;
}

static size_t put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
    return 2;
}

static size_t build_client_hello(uint8_t *m, const char *host,
                                 const uint8_t public_key[32])
{
    static const uint16_t signature_algorithms[] = {
        0x0403, 0x0503, 0x0603,         /* ECDSA mit SHA-256/384/512 */
        0x0804, 0x0805, 0x0806,         /* RSA-PSS */
        0x0401, 0x0501, 0x0601,         /* RSA PKCS#1 */
        0x0807,                         /* Ed25519 */
    };
    size_t n = 4;                       /* Kopf wird am Ende gefüllt */

    n += put16(m + n, 0x0303);          /* "TLS 1.2" - die echte Version steht unten */
    random_bytes(m + n, 32);
    n += 32;
    m[n++] = 32;                        /* Sitzungs-ID: nur noch Attrappe */
    random_bytes(m + n, 32);
    n += 32;
    n += put16(m + n, 2);
    n += put16(m + n, SUITE_CHACHA20_POLY1305_SHA256);
    m[n++] = 1;                         /* keine Kompression */
    m[n++] = 0;

    size_t extensions = n;
    n += 2;

    /* Servername (SNI) - damit der Server weiß, welche Seite gemeint ist */
    ip4_t literal;
    size_t host_len = strlen(host);
    if (!ip4_parse(host, &literal)) {
        n += put16(m + n, 0x0000);
        n += put16(m + n, (uint16_t)(host_len + 5));
        n += put16(m + n, (uint16_t)(host_len + 3));
        m[n++] = 0;
        n += put16(m + n, (uint16_t)host_len);
        memcpy(m + n, host, host_len);
        n += host_len;
    }

    n += put16(m + n, 0x000A);          /* unterstützte Kurven */
    n += put16(m + n, 4);
    n += put16(m + n, 2);
    n += put16(m + n, GROUP_X25519);

    n += put16(m + n, 0x000D);          /* Signaturverfahren, die der Server nutzen darf */
    n += put16(m + n, sizeof(signature_algorithms) + 2);
    n += put16(m + n, sizeof(signature_algorithms));
    for (size_t i = 0; i < sizeof(signature_algorithms) / 2; i++)
        n += put16(m + n, signature_algorithms[i]);

    n += put16(m + n, 0x002B);          /* unterstützte Versionen: nur TLS 1.3 */
    n += put16(m + n, 3);
    m[n++] = 2;
    n += put16(m + n, VERSION_TLS13);

    n += put16(m + n, 0x0033);          /* unser Beitrag zum Schlüsselaustausch */
    n += put16(m + n, 38);
    n += put16(m + n, 36);
    n += put16(m + n, GROUP_X25519);
    n += put16(m + n, 32);
    memcpy(m + n, public_key, 32);
    n += 32;

    put16(m + extensions, (uint16_t)(n - extensions - 2));
    m[0] = HS_CLIENT_HELLO;
    m[1] = 0;
    put16(m + 2, (uint16_t)(n - 4));
    return n;
}

/* Liest aus dem ServerHello dessen X25519-Wert. */
static bool parse_server_hello(tls_t *t, const uint8_t *m, size_t len,
                               uint8_t server_key[32])
{
    static const uint8_t retry_marker[4] = { 0xCF, 0x21, 0xAD, 0x74 };
    const uint8_t *p = m + 4, *end = m + len;

    if (m[0] != HS_SERVER_HELLO || len < 4 + 2 + 32 + 1) {
        fail(t, "unerwartete Antwort");
        return false;
    }
    if (memcmp(p + 2, retry_marker, 4) == 0) {
        fail(t, "Server unterstützt X25519 nicht");
        return false;
    }
    p += 2 + 32;
    p += 1 + p[0];                      /* Sitzungs-ID */
    if (p + 5 > end) {
        fail(t, "Antwort unvollständig");
        return false;
    }
    uint16_t suite = (uint16_t)((p[0] << 8) | p[1]);
    p += 3;                             /* Verfahren + Kompression */
    size_t extensions = (size_t)((p[0] << 8) | p[1]);
    p += 2;
    if (suite != SUITE_CHACHA20_POLY1305_SHA256 || p + extensions > end) {
        fail(t, "Server wählt ein nicht unterstütztes Verfahren");
        return false;
    }

    bool version_ok = false, have_key = false;
    end = p + extensions;
    while (p + 4 <= end) {
        uint16_t type = (uint16_t)((p[0] << 8) | p[1]);
        size_t size = (size_t)((p[2] << 8) | p[3]);
        p += 4;
        if (p + size > end)
            break;
        if (type == 0x002B && size == 2)
            version_ok = ((p[0] << 8) | p[1]) == VERSION_TLS13;
        if (type == 0x0033 && size == 36 &&
            ((p[0] << 8) | p[1]) == GROUP_X25519) {
            memcpy(server_key, p + 4, 32);
            have_key = true;
        }
        p += size;
    }
    if (!version_ok) {
        fail(t, "Server unterstützt kein TLS 1.3");
        return false;
    }
    if (!have_key) {
        fail(t, "Schlüsselaustausch fehlgeschlagen");
        return false;
    }
    return true;
}

static bool handshake(tls_t *t, const char *host)
{
    static const uint8_t zeros[32];
    uint8_t private_key[32], public_key[32], server_key[32], shared[32];
    uint8_t early[32], derived[32], handshake_secret[32], master[32];
    uint8_t secret[32], hash[32], finished_key[32], verify[32];
    uint8_t *message;
    size_t len;

    /* 1. ClientHello */
    random_bytes(private_key, 32);
    x25519_public(public_key, private_key);
    uint8_t hello[512];
    if (strlen(host) > 200) {
        fail(t, "Servername zu lang");
        return false;
    }
    len = build_client_hello(hello, host, public_key);
    sha256_init(&t->transcript);
    sha256_update(&t->transcript, hello, len);
    if (!send_plain_record(t, REC_HANDSHAKE, hello, len)) {
        fail(t, "Senden fehlgeschlagen");
        return false;
    }

    /* 2. ServerHello -> gemeinsames Geheimnis */
    if (!next_message(t, &message, &len) ||
        !parse_server_hello(t, message, len, server_key))
        return false;
    sha256_update(&t->transcript, message, len);
    consume_message(t, len);
    x25519(shared, private_key, server_key);
    if (memcmp(shared, zeros, 32) == 0) {
        fail(t, "ungültiger Schlüssel vom Server");
        return false;
    }

    /* 3. Schlüssel für den Rest des Handshakes (RFC 8446, Abschnitt 7.1) */
    sha256(NULL, 0, hash);
    hkdf_extract(zeros, 32, zeros, 32, early);
    expand_label(early, "derived", hash, 32, derived, 32);
    hkdf_extract(derived, 32, shared, 32, handshake_secret);
    transcript_hash(t, hash);
    expand_label(handshake_secret, "s hs traffic", hash, 32, secret, 32);
    set_keys(&t->rx, secret);
    expand_label(handshake_secret, "c hs traffic", hash, 32, secret, 32);
    set_keys(&t->tx, secret);
    t->encrypted = true;

    sha256(NULL, 0, hash);
    expand_label(handshake_secret, "derived", hash, 32, derived, 32);
    hkdf_extract(derived, 32, zeros, 32, master);

    /* 4. Verschlüsselte Nachrichten des Servers bis zu seinem "Finished" */
    bool certificate_requested = false;
    uint8_t request_context[256];
    size_t request_context_len = 0;
    for (;;) {
        if (!next_message(t, &message, &len))
            return false;
        uint8_t type = message[0];

        if (type == HS_FINISHED) {
            /* Prüfsumme über den gesamten bisherigen Ablauf */
            transcript_hash(t, hash);
            expand_label(t->rx.secret, "finished", NULL, 0, finished_key, 32);
            hmac_sha256(finished_key, 32, hash, 32, verify);
            if (len != 4 + 32 || memcmp(message + 4, verify, 32) != 0) {
                fail(t, "Prüfsumme des Verbindungsaufbaus stimmt nicht");
                return false;
            }
            sha256_update(&t->transcript, message, len);
            consume_message(t, len);
            break;
        }
        if (type == HS_CERTIFICATE_REQUEST && len >= 5) {
            certificate_requested = true;
            request_context_len = message[4];
            if (5 + request_context_len <= len)
                memcpy(request_context, message + 5, request_context_len);
        } else if (type != HS_ENCRYPTED_EXTENSIONS && type != HS_CERTIFICATE &&
                   type != HS_CERTIFICATE_VERIFY) {
            fail(t, "unerwartete Nachricht beim Aufbau");
            return false;
        }
        /* Das Zertifikat wird nur zur Kenntnis genommen, nicht geprüft. */
        sha256_update(&t->transcript, message, len);
        consume_message(t, len);
    }

    /* 5. Schlüssel für die Nutzdaten - abgeleitet vom Stand nach "Finished" */
    uint8_t client_app[32], server_app[32];
    transcript_hash(t, hash);
    expand_label(master, "c ap traffic", hash, 32, client_app, 32);
    expand_label(master, "s ap traffic", hash, 32, server_app, 32);

    /* 6. Unsere Antwort: evtl. ein leeres Zertifikat, dann "Finished" */
    if (certificate_requested) {
        uint8_t cert[4 + 1 + 255 + 3];
        size_t n = 4;
        cert[n++] = (uint8_t)request_context_len;
        memcpy(cert + n, request_context, request_context_len);
        n += request_context_len;
        cert[n++] = 0;
        cert[n++] = 0;
        cert[n++] = 0;
        cert[0] = HS_CERTIFICATE;
        cert[1] = 0;
        put16(cert + 2, (uint16_t)(n - 4));
        sha256_update(&t->transcript, cert, n);
        if (!send_record(t, REC_HANDSHAKE, cert, n)) {
            fail(t, "Senden fehlgeschlagen");
            return false;
        }
        transcript_hash(t, hash);
    }
    uint8_t finished[4 + 32] = { HS_FINISHED, 0, 0, 32 };
    expand_label(t->tx.secret, "finished", NULL, 0, finished_key, 32);
    hmac_sha256(finished_key, 32, hash, 32, finished + 4);
    if (!send_record(t, REC_HANDSHAKE, finished, sizeof(finished))) {
        fail(t, "Senden fehlgeschlagen");
        return false;
    }

    set_keys(&t->tx, client_app);
    set_keys(&t->rx, server_app);
    return true;
}

/* --- Schnittstelle ---------------------------------------------------------------------- */

tls_t *tls_connect(tcp_conn_t *tcp, const char *host, char *error,
                   size_t error_size)
{
    tls_t *t = kzalloc(sizeof(*t));
    if (error_size)
        error[0] = '\0';
    if (t) {
        t->tcp = tcp;
        t->error = error;
        t->error_size = error_size;
        t->record = kmalloc(RECORD_MAX);
        t->plain = kmalloc(RECORD_MAX);
        t->out = kmalloc(RECORD_MAX + 5);
        t->handshake = kmalloc(HANDSHAKE_MAX);
    }
    if (!t || !t->record || !t->plain || !t->out || !t->handshake) {
        ksnprintf(error, error_size, "TLS: nicht genug Speicher");
        tls_close(t);
        return NULL;
    }

    if (!handshake(t, host)) {
        fail(t, "Verbindungsaufbau fehlgeschlagen");
        t->error = NULL;
        tls_close(t);
        return NULL;
    }
    t->error = NULL;
    return t;
}

bool tls_send(tls_t *t, const void *data, size_t len)
{
    const uint8_t *p = data;
    while (len > 0) {
        size_t n = len < CHUNK ? len : CHUNK;
        if (!send_record(t, REC_DATA, p, n))
            return false;
        p += n;
        len -= n;
    }
    return true;
}

/* Nachrichten, die der Server nach dem Aufbau noch schicken darf. */
static bool post_handshake(tls_t *t, const uint8_t *fragment, size_t len)
{
    /* Auch diese Nachrichten können über mehrere Records verteilt sein. */
    if (t->handshake_len + len > HANDSHAKE_MAX)
        return false;
    memcpy(t->handshake + t->handshake_len, fragment, len);
    t->handshake_len += len;

    while (t->handshake_len >= 4) {
        const uint8_t *m = t->handshake;
        size_t body = ((size_t)m[1] << 16) | ((size_t)m[2] << 8) | m[3];
        if (4 + body > HANDSHAKE_MAX)
            return false;
        if (4 + body > t->handshake_len)
            break;                      /* Rest kommt im nächsten Record */
        if (m[0] == HS_KEY_UPDATE && body == 1) {
            /* Der Server wechselt seinen Schlüssel - und bittet uns evtl. darum */
            uint8_t next[32];
            bool requested = m[4] == 1;
            expand_label(t->rx.secret, "traffic upd", NULL, 0, next, 32);
            set_keys(&t->rx, next);
            if (requested) {
                static const uint8_t update[5] = { HS_KEY_UPDATE, 0, 0, 1, 0 };
                if (!send_record(t, REC_HANDSHAKE, update, sizeof(update)))
                    return false;
                expand_label(t->tx.secret, "traffic upd", NULL, 0, next, 32);
                set_keys(&t->tx, next);
            }
        } else if (m[0] != HS_NEW_SESSION_TICKET) {
            return false;
        }
        consume_message(t, 4 + body);
    }
    return true;
}

int tls_recv(tls_t *t, void *buffer, size_t max, uint32_t timeout_ms)
{
    while (t->plain_pos == t->plain_len) {
        if (t->closed)
            return 0;
        uint8_t type;
        size_t len;
        int result = read_record(t, &type, &len, timeout_ms);
        if (result == 0) {
            t->closed = true;
            return 0;
        }
        if (result < 0)
            return -1;
        if (type == REC_DATA) {
            t->plain_pos = 0;
            t->plain_len = len;
        } else if (type != REC_HANDSHAKE || !post_handshake(t, t->plain, len)) {
            return -1;
        }
    }
    size_t n = t->plain_len - t->plain_pos;
    if (n > max)
        n = max;
    memcpy(buffer, t->plain + t->plain_pos, n);
    t->plain_pos += n;
    return (int)n;
}

void tls_close(tls_t *t)
{
    if (!t)
        return;
    if (t->encrypted && t->out && !t->closed) {
        static const uint8_t close_notify[2] = { 1, 0 };
        send_record(t, REC_ALERT, close_notify, sizeof(close_notify));
    }
    kfree(t->record);
    kfree(t->plain);
    kfree(t->out);
    kfree(t->handshake);
    kfree(t);
}
