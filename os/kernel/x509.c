#include "x509.h"
#include "string.h"

#define MAX_CHAIN  8
#define MAX_BLOBS  2

/* --- DER: das Datenformat der Zertifikate --------------------------------------------- */

/*
 * DER-Daten bestehen aus verschachtelten Elementen: ein Byte Typ, dann die
 * Länge, dann der Inhalt.
 */
typedef struct {
    const uint8_t *p, *end;
} der_t;

static bool der_read(der_t *d, uint8_t *tag, der_t *content)
{
    if (d->end - d->p < 2)
        return false;
    const uint8_t *p = d->p;
    *tag = *p++;
    size_t len = *p++;
    if (len & 0x80) {                   /* lange Form: die Länge folgt in 1-3 Bytes */
        size_t bytes = len & 0x7F;
        if (bytes == 0 || bytes > 3 || (size_t)(d->end - p) < bytes)
            return false;
        len = 0;
        while (bytes--)
            len = (len << 8) | *p++;
    }
    if ((size_t)(d->end - p) < len)
        return false;
    content->p = p;
    content->end = p + len;
    d->p = p + len;
    return true;
}

static bool der_expect(der_t *d, uint8_t tag, der_t *content)
{
    uint8_t found;
    return der_read(d, &found, content) && found == tag;
}

static bool der_equals(const der_t *d, const uint8_t *bytes, size_t len)
{
    return (size_t)(d->end - d->p) == len && memcmp(d->p, bytes, len) == 0;
}

#define OID(d, ...)                                         \
    der_equals(d, (const uint8_t[]){ __VA_ARGS__ },         \
               sizeof((const uint8_t[]){ __VA_ARGS__ }))

/* Zeitangabe "JJMMTThhmmssZ" oder "JJJJMMTThhmmssZ" als Zahl JJJJMMTThhmmss. */
static bool parse_time(uint8_t tag, const der_t *d, uint64_t *out)
{
    size_t len = (size_t)(d->end - d->p);
    size_t digits = tag == 0x17 ? 12 : tag == 0x18 ? 14 : 0;
    if (digits == 0 || len < digits)
        return false;
    uint64_t value = 0;
    for (size_t i = 0; i < digits; i++) {
        if (d->p[i] < '0' || d->p[i] > '9')
            return false;
        value = value * 10 + (uint64_t)(d->p[i] - '0');
    }
    if (digits == 12)                   /* zweistellige Jahre: 50-99 = 19xx */
        value += value >= 500000000000ull ? 19000000000000ull : 20000000000000ull;
    *out = value;
    return true;
}

/* --- Zertifikat zerlegen ------------------------------------------------------------------ */

static bool parse_key(der_t spki, public_key_t *key)
{
    der_t algorithm, oid, params, bits;
    uint8_t tag;
    key->type = KEY_NONE;
    if (!der_expect(&spki, 0x30, &algorithm) || !der_expect(&algorithm, 0x06, &oid) ||
        !der_expect(&spki, 0x03, &bits) || bits.end - bits.p < 2 || *bits.p != 0)
        return false;
    bits.p++;                           /* Anzahl ungenutzter Bits: immer 0 */

    if (OID(&oid, 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x01)) {
        /* RSA: SEQUENCE { Modul, Exponent } */
        der_t sequence, modulus, exponent;
        if (!der_expect(&bits, 0x30, &sequence) ||
            !der_expect(&sequence, 0x02, &modulus) ||
            !der_expect(&sequence, 0x02, &exponent))
            return false;
        key->type = KEY_RSA;
        key->modulus = modulus.p;
        key->modulus_len = (size_t)(modulus.end - modulus.p);
        key->exponent = exponent.p;
        key->exponent_len = (size_t)(exponent.end - exponent.p);
    } else if (OID(&oid, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x02, 0x01)) {
        /* Elliptische Kurve: Kurvenname als Parameter, Punkt als 04 || X || Y */
        if (!der_read(&algorithm, &tag, &params) || tag != 0x06)
            return true;                /* unbekannte Angabe: Schlüssel unbenutzbar */
        size_t len = (size_t)(bits.end - bits.p);
        if (OID(&params, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x03, 0x01, 0x07) &&
            len == 65 && bits.p[0] == 0x04)
            key->type = KEY_EC_P256;
        else if (OID(&params, 0x2B, 0x81, 0x04, 0x00, 0x22) && len == 97 &&
                 bits.p[0] == 0x04)
            key->type = KEY_EC_P384;
        key->point = bits.p + 1;
    }
    return true;                        /* andere Verfahren: Typ bleibt KEY_NONE */
}

static x509_sig_t parse_sig_alg(der_t algorithm)
{
    der_t oid;
    if (!der_expect(&algorithm, 0x06, &oid))
        return SIG_UNSUPPORTED;
    if (OID(&oid, 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x0B))
        return SIG_RSA_SHA256;
    if (OID(&oid, 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x0C))
        return SIG_RSA_SHA384;
    if (OID(&oid, 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x0D))
        return SIG_RSA_SHA512;
    if (OID(&oid, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x04, 0x03, 0x02))
        return SIG_ECDSA_SHA256;
    if (OID(&oid, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x04, 0x03, 0x03))
        return SIG_ECDSA_SHA384;
    if (OID(&oid, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x04, 0x03, 0x04))
        return SIG_ECDSA_SHA512;
    return SIG_UNSUPPORTED;
}

static void parse_extensions(der_t list, x509_cert_t *out)
{
    der_t extension, oid, value;
    while (der_expect(&list, 0x30, &extension)) {
        uint8_t tag;
        if (!der_expect(&extension, 0x06, &oid) ||
            !der_read(&extension, &tag, &value))
            return;
        if (tag == 0x01 && !der_read(&extension, &tag, &value))
            return;                     /* Kennzeichen "kritisch" überspringen */
        if (tag != 0x04)
            return;

        if (OID(&oid, 0x55, 0x1D, 0x11)) {
            /* Alternative Namen: die Rechnernamen, für die das Zertifikat gilt */
            der_t names;
            if (der_expect(&value, 0x30, &names)) {
                out->names = names.p;
                out->names_len = (size_t)(names.end - names.p);
            }
        } else if (OID(&oid, 0x55, 0x1D, 0x13)) {
            /* Grundeinschränkungen: SEQUENCE { CA BOOLEAN DEFAULT FALSE, ... } */
            der_t constraints, flag;
            if (der_expect(&value, 0x30, &constraints) &&
                der_read(&constraints, &tag, &flag) && tag == 0x01 &&
                flag.end - flag.p == 1)
                out->is_ca = *flag.p != 0;
        }
    }
}

bool x509_parse(const uint8_t *der, size_t len, x509_cert_t *out)
{
    der_t top = { der, der + len };
    der_t cert, tbs, item, validity, time, algorithm, signature;
    uint8_t tag;

    memset(out, 0, sizeof(*out));
    if (!der_expect(&top, 0x30, &cert))
        return false;

    out->tbs = cert.p;
    if (!der_expect(&cert, 0x30, &tbs))
        return false;
    out->tbs_len = (size_t)(cert.p - out->tbs);

    /* Version (optional), Seriennummer, Signaturverfahren */
    if (tbs.p < tbs.end && *tbs.p == 0xA0 && !der_read(&tbs, &tag, &item))
        return false;
    if (!der_expect(&tbs, 0x02, &item) || !der_expect(&tbs, 0x30, &item))
        return false;

    out->issuer = tbs.p;
    if (!der_expect(&tbs, 0x30, &item))
        return false;
    out->issuer_len = (size_t)(tbs.p - out->issuer);

    if (!der_expect(&tbs, 0x30, &validity) ||
        !der_read(&validity, &tag, &time) || !parse_time(tag, &time, &out->not_before) ||
        !der_read(&validity, &tag, &time) || !parse_time(tag, &time, &out->not_after))
        return false;

    out->subject = tbs.p;
    if (!der_expect(&tbs, 0x30, &item))
        return false;
    out->subject_len = (size_t)(tbs.p - out->subject);

    if (!der_expect(&tbs, 0x30, &item) || !parse_key(item, &out->key))
        return false;

    /* Der Rest ist optional; uns interessieren nur die Erweiterungen [3]. */
    while (der_read(&tbs, &tag, &item)) {
        der_t list;
        if (tag == 0xA3 && der_expect(&item, 0x30, &list))
            parse_extensions(list, out);
    }

    if (!der_expect(&cert, 0x30, &algorithm) || !der_expect(&cert, 0x03, &signature) ||
        signature.end - signature.p < 2 || *signature.p != 0)
        return false;
    out->sig_alg = parse_sig_alg(algorithm);
    out->signature = signature.p + 1;
    out->signature_len = (size_t)(signature.end - signature.p - 1);
    return true;
}

bool x509_check_signature(const x509_cert_t *cert, const public_key_t *issuer_key)
{
    uint8_t hash[HASH_MAX];
    hash_alg_t alg;
    bool rsa;

    switch (cert->sig_alg) {
    case SIG_RSA_SHA256:   alg = HASH_SHA256; rsa = true;  break;
    case SIG_RSA_SHA384:   alg = HASH_SHA384; rsa = true;  break;
    case SIG_RSA_SHA512:   alg = HASH_SHA512; rsa = true;  break;
    case SIG_ECDSA_SHA256: alg = HASH_SHA256; rsa = false; break;
    case SIG_ECDSA_SHA384: alg = HASH_SHA384; rsa = false; break;
    case SIG_ECDSA_SHA512: alg = HASH_SHA512; rsa = false; break;
    default:
        return false;
    }
    hash_compute(alg, cert->tbs, cert->tbs_len, hash);
    if (rsa)
        return issuer_key->type == KEY_RSA &&
               rsa_pkcs1_verify(issuer_key, alg, hash, cert->signature,
                                cert->signature_len);
    return (issuer_key->type == KEY_EC_P256 || issuer_key->type == KEY_EC_P384) &&
           ecdsa_verify(issuer_key, hash, hash_size(alg), cert->signature,
                        cert->signature_len);
}

/* --- Servername ----------------------------------------------------------------------------- */

static char lower(char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c;
}

static bool equal_ignore_case(const char *a, size_t a_len, const uint8_t *b,
                              size_t b_len)
{
    if (a_len != b_len)
        return false;
    for (size_t i = 0; i < a_len; i++) {
        if (lower(a[i]) != lower((char)b[i]))
            return false;
    }
    return true;
}

/* Liest "1.2.3.4"; für den Vergleich mit Adressen im Zertifikat. */
static bool parse_ipv4(const char *text, uint8_t out[4])
{
    for (int part = 0; part < 4; part++) {
        unsigned value = 0, digits = 0;
        while (*text >= '0' && *text <= '9' && digits < 4) {
            value = value * 10 + (unsigned)(*text++ - '0');
            digits++;
        }
        if (digits == 0 || value > 255)
            return false;
        out[part] = (uint8_t)value;
        if (part < 3 && *text++ != '.')
            return false;
    }
    return *text == '\0';
}

static bool name_matches(const x509_cert_t *cert, const char *host)
{
    der_t names = { cert->names, cert->names + cert->names_len };
    der_t name;
    uint8_t tag, address[4];
    size_t host_len = strlen(host);
    bool is_address = parse_ipv4(host, address);

    while (der_read(&names, &tag, &name)) {
        size_t len = (size_t)(name.end - name.p);
        if (tag == 0x87 && is_address) {                /* IP-Adresse */
            if (len == 4 && memcmp(name.p, address, 4) == 0)
                return true;
        } else if (tag == 0x82 && !is_address) {        /* Rechnername */
            if (equal_ignore_case(host, host_len, name.p, len))
                return true;
            /* "*.example.com" passt auf genau eine Ebene davor */
            if (len > 2 && name.p[0] == '*' && name.p[1] == '.') {
                const char *dot = host;
                while (*dot && *dot != '.')
                    dot++;
                if (dot != host && *dot &&
                    equal_ignore_case(dot, host_len - (size_t)(dot - host),
                                      name.p + 1, len - 1))
                    return true;
            }
        }
    }
    return false;
}

/* --- Vertrauenskette --------------------------------------------------------------------------- */

static const uint8_t *root_blobs[MAX_BLOBS];

void x509_add_roots(const uint8_t *blob)
{
    for (int i = 0; i < MAX_BLOBS; i++) {
        if (!root_blobs[i]) {
            root_blobs[i] = blob;
            return;
        }
    }
}

/* Sucht ein Wurzelzertifikat, das dieses Zertifikat ausgestellt hat. */
static bool signed_by_root(const x509_cert_t *cert, uint64_t now)
{
    for (int b = 0; b < MAX_BLOBS && root_blobs[b]; b++) {
        const uint8_t *p = root_blobs[b];
        for (;;) {
            size_t len = ((size_t)p[0] << 8) | p[1];
            if (len == 0)
                break;
            x509_cert_t root;
            if (x509_parse(p + 2, len, &root) &&
                root.subject_len == cert->issuer_len &&
                memcmp(root.subject, cert->issuer, cert->issuer_len) == 0 &&
                now >= root.not_before && now <= root.not_after &&
                x509_check_signature(cert, &root.key))
                return true;
            p += 2 + len;
        }
    }
    return false;
}

const char *x509_verify_chain(const uint8_t *const *certs, const size_t *lens,
                              int count, const char *host, uint64_t now,
                              public_key_t *server_key)
{
    x509_cert_t chain[MAX_CHAIN];
    bool used[MAX_CHAIN] = { false };

    if (count < 1)
        return "Server hat kein Zertifikat geschickt";
    if (count > MAX_CHAIN)
        count = MAX_CHAIN;
    for (int i = 0; i < count; i++) {
        if (!x509_parse(certs[i], lens[i], &chain[i]))
            return "Zertifikat nicht lesbar";
    }

    const x509_cert_t *current = &chain[0];
    used[0] = true;
    if (!name_matches(current, host))
        return "Zertifikat gilt nicht für diesen Servernamen";

    for (int depth = 0; depth < MAX_CHAIN; depth++) {
        if (now < current->not_before || now > current->not_after)
            return "Zertifikat ist abgelaufen oder noch nicht gültig "
                   "(stimmt die Systemuhr?)";
        if (current->sig_alg == SIG_UNSUPPORTED)
            return "Zertifikat nutzt ein nicht unterstütztes Signaturverfahren";

        if (signed_by_root(current, now)) {
            *server_key = chain[0].key;
            return NULL;                /* Kette endet bei einer bekannten Wurzel */
        }

        /* Sonst: ein mitgeschicktes Zwischenzertifikat als Aussteller suchen */
        const x509_cert_t *issuer = NULL;
        for (int i = 1; i < count && !issuer; i++) {
            if (used[i] || !chain[i].is_ca ||
                chain[i].subject_len != current->issuer_len ||
                memcmp(chain[i].subject, current->issuer, current->issuer_len) != 0)
                continue;
            if (x509_check_signature(current, &chain[i].key)) {
                issuer = &chain[i];
                used[i] = true;
            }
        }
        if (!issuer)
            break;
        current = issuer;
    }
    return "Aussteller des Zertifikats ist nicht vertrauenswürdig";
}
