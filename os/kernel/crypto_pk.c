/*
 * Signaturen mit öffentlichen Schlüsseln prüfen: RSA (PKCS#1 v1.5 und PSS)
 * und ECDSA über den Kurven P-256 und P-384. Damit werden die Zertifikate
 * von Webservern kontrolliert.
 *
 * Grundlage ist das Rechnen mit sehr großen Zahlen modulo einer ungeraden
 * Zahl. Multipliziert wird nach Montgomery: Statt teurer Divisionen genügen
 * dabei Multiplikationen und Verschiebungen. Zahlen liegen dafür in einer
 * umgerechneten "Montgomery-Form" vor.
 *
 * Signaturen werden nur geprüft. Der Schlüsselaustausch am Ende (ECDH) rechnet
 * dagegen mit einem geheimen Wert - ohne Schutz vor Laufzeitmessungen.
 */
#include "crypto.h"
#include "string.h"

typedef unsigned __int128 u128;

#define LIMBS 66                        /* 64-Bit-Worte: genug für 4096 Bit */

typedef struct {
    uint64_t v[LIMBS];                  /* niederwertigstes Wort zuerst */
} bn_t;

typedef struct {
    int n;                              /* benutzte Worte */
    bn_t m;                             /* der Modul */
    uint64_t m_inv;                     /* -1/m modulo 2^64 */
    bn_t r2;                            /* Hilfswert zum Umrechnen */
    bn_t one;                           /* die Zahl 1 in Montgomery-Form */
} mont_t;

/* --- Große Zahlen -------------------------------------------------------------------- */

/* Liest eine Zahl (höchstwertiges Byte zuerst); false, wenn sie zu groß ist. */
static bool bn_from_bytes(bn_t *out, const uint8_t *data, size_t len)
{
    while (len > 0 && data[0] == 0) {
        data++;
        len--;
    }
    if (len > (LIMBS - 2) * 8)
        return false;
    memset(out, 0, sizeof(*out));
    for (size_t i = 0; i < len; i++)
        out->v[i / 8] |= (uint64_t)data[len - 1 - i] << (8 * (i % 8));
    return true;
}

static void bn_to_bytes(const bn_t *a, uint8_t *out, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        size_t limb = (len - 1 - i) / 8;
        out[i] = limb < LIMBS ? (uint8_t)(a->v[limb] >> (8 * ((len - 1 - i) % 8))) : 0;
    }
}

static int bn_cmp(const bn_t *a, const bn_t *b, int n)
{
    for (int i = n - 1; i >= 0; i--) {
        if (a->v[i] != b->v[i])
            return a->v[i] > b->v[i] ? 1 : -1;
    }
    return 0;
}

static bool bn_is_zero(const bn_t *a, int n)
{
    uint64_t any = 0;
    for (int i = 0; i < n; i++)
        any |= a->v[i];
    return any == 0;
}

static uint64_t bn_add(bn_t *r, const bn_t *a, const bn_t *b, int n)
{
    u128 carry = 0;
    for (int i = 0; i < n; i++) {
        carry += (u128)a->v[i] + b->v[i];
        r->v[i] = (uint64_t)carry;
        carry >>= 64;
    }
    return (uint64_t)carry;
}

static uint64_t bn_sub(bn_t *r, const bn_t *a, const bn_t *b, int n)
{
    uint64_t borrow = 0;
    for (int i = 0; i < n; i++) {
        u128 d = (u128)a->v[i] - b->v[i] - borrow;
        r->v[i] = (uint64_t)d;
        borrow = (uint64_t)(d >> 64) & 1;
    }
    return borrow;
}

/* --- Rechnen modulo m ------------------------------------------------------------------ */

static void mod_add(const mont_t *c, bn_t *r, const bn_t *a, const bn_t *b)
{
    uint64_t carry = bn_add(r, a, b, c->n);
    if (carry || bn_cmp(r, &c->m, c->n) >= 0)
        bn_sub(r, r, &c->m, c->n);
}

static void mod_sub(const mont_t *c, bn_t *r, const bn_t *a, const bn_t *b)
{
    if (bn_sub(r, a, b, c->n))
        bn_add(r, r, &c->m, c->n);
}

/* r = a * b / 2^(64n) mod m - das Herzstück (Verfahren "CIOS"). */
static void mont_mul(const mont_t *c, bn_t *r, const bn_t *a, const bn_t *b)
{
    uint64_t t[LIMBS + 2] = { 0 };
    int n = c->n;

    for (int i = 0; i < n; i++) {
        u128 carry = 0;
        for (int j = 0; j < n; j++) {
            carry += (u128)a->v[j] * b->v[i] + t[j];
            t[j] = (uint64_t)carry;
            carry >>= 64;
        }
        carry += t[n];
        t[n] = (uint64_t)carry;
        t[n + 1] = (uint64_t)(carry >> 64);

        /* Ein Vielfaches von m addieren, das das unterste Wort zu null macht */
        uint64_t q = t[0] * c->m_inv;
        carry = ((u128)q * c->m.v[0] + t[0]) >> 64;
        for (int j = 1; j < n; j++) {
            carry += (u128)q * c->m.v[j] + t[j];
            t[j - 1] = (uint64_t)carry;
            carry >>= 64;
        }
        carry += t[n];
        t[n - 1] = (uint64_t)carry;
        t[n] = t[n + 1] + (uint64_t)(carry >> 64);
    }

    bn_t result;
    memset(&result, 0, sizeof(result));
    memcpy(result.v, t, (size_t)n * sizeof(uint64_t));
    if (t[n] || bn_cmp(&result, &c->m, n) >= 0)
        bn_sub(&result, &result, &c->m, n);
    *r = result;
}

/* Bereitet das Rechnen modulo m vor; m muss ungerade sein. */
static bool mont_init(mont_t *c, const uint8_t *modulus, size_t len)
{
    if (!bn_from_bytes(&c->m, modulus, len))
        return false;
    c->n = 0;
    for (int i = 0; i < LIMBS; i++) {
        if (c->m.v[i])
            c->n = i + 1;
    }
    if (c->n == 0 || !(c->m.v[0] & 1))
        return false;

    /* Kehrwert des untersten Wortes modulo 2^64 (Newton-Verfahren) */
    uint64_t inv = c->m.v[0];
    for (int i = 0; i < 6; i++)
        inv *= 2 - c->m.v[0] * inv;
    c->m_inv = 0 - inv;

    /* r2 = 2^(128n) mod m: mit 1 anfangen und immer wieder verdoppeln */
    memset(&c->r2, 0, sizeof(c->r2));
    c->r2.v[0] = 1;
    for (int i = 0; i < 128 * c->n; i++)
        mod_add(c, &c->r2, &c->r2, &c->r2);

    bn_t plain_one;
    memset(&plain_one, 0, sizeof(plain_one));
    plain_one.v[0] = 1;
    mont_mul(c, &c->one, &c->r2, &plain_one);
    return true;
}

static void to_mont(const mont_t *c, bn_t *r, const bn_t *a)
{
    mont_mul(c, r, a, &c->r2);
}

static void from_mont(const mont_t *c, bn_t *r, const bn_t *a)
{
    bn_t plain_one;
    memset(&plain_one, 0, sizeof(plain_one));
    plain_one.v[0] = 1;
    mont_mul(c, r, a, &plain_one);
}

/* r = base ^ exponent mod m; base und r in Montgomery-Form. */
static void mont_exp(const mont_t *c, bn_t *r, const bn_t *base,
                     const bn_t *exponent)
{
    bn_t result = c->one;
    for (int bit = c->n * 64 - 1; bit >= 0; bit--) {
        mont_mul(c, &result, &result, &result);
        if ((exponent->v[bit / 64] >> (bit % 64)) & 1)
            mont_mul(c, &result, &result, base);
    }
    *r = result;
}

/* --- RSA ----------------------------------------------------------------------------------- */

#define RSA_MAX_BYTES 512

/* Wendet den öffentlichen Schlüssel an: em = signature ^ e mod n. */
static bool rsa_public(const public_key_t *key, const uint8_t *signature,
                       size_t len, uint8_t *em, size_t *em_len, size_t *bits)
{
    mont_t ctx;
    bn_t s, e, m;
    if (key->type != KEY_RSA ||
        !mont_init(&ctx, key->modulus, key->modulus_len) ||
        !bn_from_bytes(&s, signature, len) ||
        !bn_from_bytes(&e, key->exponent, key->exponent_len))
        return false;

    int top = 63;
    while (!(ctx.m.v[ctx.n - 1] >> top))
        top--;
    *bits = (size_t)(ctx.n - 1) * 64 + (size_t)top + 1;
    size_t bytes = (*bits + 7) / 8;
    if (*bits < 2048 || bytes > RSA_MAX_BYTES || len != bytes)
        return false;                   /* zu kurze Schlüssel gelten als unsicher */
    if (bn_cmp(&s, &ctx.m, ctx.n) >= 0 || bn_is_zero(&e, ctx.n))
        return false;

    to_mont(&ctx, &s, &s);
    mont_exp(&ctx, &m, &s, &e);
    from_mont(&ctx, &m, &m);
    bn_to_bytes(&m, em, bytes);
    *em_len = bytes;
    return true;
}

bool rsa_pkcs1_verify(const public_key_t *key, hash_alg_t alg,
                      const uint8_t *hash, const uint8_t *signature, size_t len)
{
    /* Feste Vorspänne, die das Hashverfahren benennen (ASN.1 "DigestInfo") */
    static const uint8_t prefix[3][19] = {
        { 0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65,
          0x03, 0x04, 0x02, 0x01, 0x05, 0x00, 0x04, 0x20 },
        { 0x30, 0x41, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65,
          0x03, 0x04, 0x02, 0x02, 0x05, 0x00, 0x04, 0x30 },
        { 0x30, 0x51, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65,
          0x03, 0x04, 0x02, 0x03, 0x05, 0x00, 0x04, 0x40 },
    };
    uint8_t em[RSA_MAX_BYTES], expected[RSA_MAX_BYTES];
    size_t em_len, bits, h = hash_size(alg);

    if (!rsa_public(key, signature, len, em, &em_len, &bits))
        return false;
    if (em_len < 3 + 8 + 19 + h)
        return false;

    /* Erwarteter Aufbau: 00 01 FF ... FF 00 Vorspann Hash */
    size_t pad = em_len - 3 - 19 - h;
    expected[0] = 0x00;
    expected[1] = 0x01;
    memset(expected + 2, 0xFF, pad);
    expected[2 + pad] = 0x00;
    memcpy(expected + 3 + pad, prefix[alg], 19);
    memcpy(expected + 3 + pad + 19, hash, h);
    return memcmp(em, expected, em_len) == 0;
}

bool rsa_pss_verify(const public_key_t *key, hash_alg_t alg, const uint8_t *hash,
                    const uint8_t *signature, size_t len)
{
    uint8_t em_full[RSA_MAX_BYTES];
    size_t full_len, bits, h = hash_size(alg);
    if (!rsa_public(key, signature, len, em_full, &full_len, &bits))
        return false;

    /* Die Kodierung nutzt ein Bit weniger als der Modul. */
    size_t em_bits = bits - 1;
    size_t em_len = (em_bits + 7) / 8;
    const uint8_t *em = em_full + (full_len - em_len);
    if (full_len != em_len && em_full[0] != 0)
        return false;
    if (em_len < 2 * h + 2 || em[em_len - 1] != 0xBC)
        return false;

    size_t db_len = em_len - h - 1;
    const uint8_t *digest = em + db_len;
    uint8_t db[RSA_MAX_BYTES];

    /* Maske aus dem Hashwert erzeugen (MGF1) und abziehen */
    for (size_t offset = 0, counter = 0; offset < db_len; counter++) {
        uint8_t seed[HASH_MAX + 4], block[HASH_MAX];
        memcpy(seed, digest, h);
        seed[h] = (uint8_t)(counter >> 24);
        seed[h + 1] = (uint8_t)(counter >> 16);
        seed[h + 2] = (uint8_t)(counter >> 8);
        seed[h + 3] = (uint8_t)counter;
        hash_compute(alg, seed, h + 4, block);
        for (size_t i = 0; i < h && offset < db_len; i++, offset++)
            db[offset] = em[offset] ^ block[i];
    }
    unsigned unused = (unsigned)(8 * em_len - em_bits);
    if (em[0] >> (8 - unused) && unused)
        return false;
    db[0] &= (uint8_t)(0xFF >> unused);

    /* Erwartet: Nullen, eine 1, dann das "Salz" (so lang wie der Hash) */
    size_t zeros = db_len - h - 1;
    for (size_t i = 0; i < zeros; i++) {
        if (db[i] != 0)
            return false;
    }
    if (db[zeros] != 0x01)
        return false;

    uint8_t message[8 + 2 * HASH_MAX] = { 0 }, check[HASH_MAX];
    memcpy(message + 8, hash, h);
    memcpy(message + 8 + h, db + zeros + 1, h);
    hash_compute(alg, message, 8 + 2 * h, check);
    return memcmp(check, digest, h) == 0;
}

/* --- Elliptische Kurven P-256 und P-384 ------------------------------------------------------ */

/* Kurve y^2 = x^3 - 3x + b über den Zahlen modulo p; G erzeugt n Punkte. */
typedef struct {
    int size;                           /* Bytes je Koordinate */
    uint8_t p[48], b[48], gx[48], gy[48], n[48];
} curve_t;

static const curve_t curve_p256 = {
    .size = 32,
    .p = {
        0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    },
    .b = {
        0x5A, 0xC6, 0x35, 0xD8, 0xAA, 0x3A, 0x93, 0xE7, 0xB3, 0xEB, 0xBD, 0x55,
        0x76, 0x98, 0x86, 0xBC, 0x65, 0x1D, 0x06, 0xB0, 0xCC, 0x53, 0xB0, 0xF6,
        0x3B, 0xCE, 0x3C, 0x3E, 0x27, 0xD2, 0x60, 0x4B,
    },
    .gx = {
        0x6B, 0x17, 0xD1, 0xF2, 0xE1, 0x2C, 0x42, 0x47, 0xF8, 0xBC, 0xE6, 0xE5,
        0x63, 0xA4, 0x40, 0xF2, 0x77, 0x03, 0x7D, 0x81, 0x2D, 0xEB, 0x33, 0xA0,
        0xF4, 0xA1, 0x39, 0x45, 0xD8, 0x98, 0xC2, 0x96,
    },
    .gy = {
        0x4F, 0xE3, 0x42, 0xE2, 0xFE, 0x1A, 0x7F, 0x9B, 0x8E, 0xE7, 0xEB, 0x4A,
        0x7C, 0x0F, 0x9E, 0x16, 0x2B, 0xCE, 0x33, 0x57, 0x6B, 0x31, 0x5E, 0xCE,
        0xCB, 0xB6, 0x40, 0x68, 0x37, 0xBF, 0x51, 0xF5,
    },
    .n = {
        0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xBC, 0xE6, 0xFA, 0xAD, 0xA7, 0x17, 0x9E, 0x84,
        0xF3, 0xB9, 0xCA, 0xC2, 0xFC, 0x63, 0x25, 0x51,
    },
};

static const curve_t curve_p384 = {
    .size = 48,
    .p = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
    },
    .b = {
        0xB3, 0x31, 0x2F, 0xA7, 0xE2, 0x3E, 0xE7, 0xE4, 0x98, 0x8E, 0x05, 0x6B,
        0xE3, 0xF8, 0x2D, 0x19, 0x18, 0x1D, 0x9C, 0x6E, 0xFE, 0x81, 0x41, 0x12,
        0x03, 0x14, 0x08, 0x8F, 0x50, 0x13, 0x87, 0x5A, 0xC6, 0x56, 0x39, 0x8D,
        0x8A, 0x2E, 0xD1, 0x9D, 0x2A, 0x85, 0xC8, 0xED, 0xD3, 0xEC, 0x2A, 0xEF,
    },
    .gx = {
        0xAA, 0x87, 0xCA, 0x22, 0xBE, 0x8B, 0x05, 0x37, 0x8E, 0xB1, 0xC7, 0x1E,
        0xF3, 0x20, 0xAD, 0x74, 0x6E, 0x1D, 0x3B, 0x62, 0x8B, 0xA7, 0x9B, 0x98,
        0x59, 0xF7, 0x41, 0xE0, 0x82, 0x54, 0x2A, 0x38, 0x55, 0x02, 0xF2, 0x5D,
        0xBF, 0x55, 0x29, 0x6C, 0x3A, 0x54, 0x5E, 0x38, 0x72, 0x76, 0x0A, 0xB7,
    },
    .gy = {
        0x36, 0x17, 0xDE, 0x4A, 0x96, 0x26, 0x2C, 0x6F, 0x5D, 0x9E, 0x98, 0xBF,
        0x92, 0x92, 0xDC, 0x29, 0xF8, 0xF4, 0x1D, 0xBD, 0x28, 0x9A, 0x14, 0x7C,
        0xE9, 0xDA, 0x31, 0x13, 0xB5, 0xF0, 0xB8, 0xC0, 0x0A, 0x60, 0xB1, 0xCE,
        0x1D, 0x7E, 0x81, 0x9D, 0x7A, 0x43, 0x1D, 0x7C, 0x90, 0xEA, 0x0E, 0x5F,
    },
    .n = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xC7, 0x63, 0x4D, 0x81, 0xF4, 0x37, 0x2D, 0xDF, 0x58, 0x1A, 0x0D, 0xB2,
        0x48, 0xB0, 0xA7, 0x7A, 0xEC, 0xEC, 0x19, 0x6A, 0xCC, 0xC5, 0x29, 0x73,
    },
};

/* Punkt in "Jacobi-Koordinaten": (X/Z^2, Y/Z^3); Z = 0 ist der Fernpunkt. */
typedef struct {
    bn_t x, y, z;
} point_t;

static void point_double(const mont_t *f, point_t *r, const point_t *p)
{
    bn_t delta, gamma, beta, alpha, t1, t2;

    mont_mul(f, &delta, &p->z, &p->z);
    mont_mul(f, &gamma, &p->y, &p->y);
    mont_mul(f, &beta, &p->x, &gamma);
    mod_sub(f, &t1, &p->x, &delta);
    mod_add(f, &t2, &p->x, &delta);
    mont_mul(f, &alpha, &t1, &t2);
    mod_add(f, &t1, &alpha, &alpha);
    mod_add(f, &alpha, &alpha, &t1);    /* alpha = 3 (x - delta)(x + delta) */

    mod_add(f, &t1, &p->y, &p->z);      /* z3 = (y + z)^2 - gamma - delta */
    mont_mul(f, &t1, &t1, &t1);
    mod_sub(f, &t1, &t1, &gamma);
    mod_sub(f, &r->z, &t1, &delta);

    mod_add(f, &beta, &beta, &beta);    /* beta = 4 x gamma */
    mod_add(f, &beta, &beta, &beta);
    mont_mul(f, &t1, &alpha, &alpha);   /* x3 = alpha^2 - 8 x gamma */
    mod_sub(f, &t1, &t1, &beta);
    mod_sub(f, &r->x, &t1, &beta);

    mont_mul(f, &gamma, &gamma, &gamma);    /* 8 gamma^2 */
    mod_add(f, &gamma, &gamma, &gamma);
    mod_add(f, &gamma, &gamma, &gamma);
    mod_add(f, &gamma, &gamma, &gamma);
    mod_sub(f, &t1, &beta, &r->x);      /* y3 = alpha (4 x gamma - x3) - 8 gamma^2 */
    mont_mul(f, &t1, &alpha, &t1);
    mod_sub(f, &r->y, &t1, &gamma);
}

static void point_add(const mont_t *f, point_t *r, const point_t *p,
                      const point_t *q)
{
    if (bn_is_zero(&p->z, f->n)) {
        *r = *q;
        return;
    }
    if (bn_is_zero(&q->z, f->n)) {
        *r = *p;
        return;
    }

    bn_t z1z1, z2z2, u1, u2, s1, s2, h, i, j, rr, v, t;
    mont_mul(f, &z1z1, &p->z, &p->z);
    mont_mul(f, &z2z2, &q->z, &q->z);
    mont_mul(f, &u1, &p->x, &z2z2);
    mont_mul(f, &u2, &q->x, &z1z1);
    mont_mul(f, &s1, &p->y, &q->z);
    mont_mul(f, &s1, &s1, &z2z2);
    mont_mul(f, &s2, &q->y, &p->z);
    mont_mul(f, &s2, &s2, &z1z1);
    mod_sub(f, &h, &u2, &u1);
    mod_sub(f, &rr, &s2, &s1);

    if (bn_is_zero(&h, f->n)) {
        if (bn_is_zero(&rr, f->n)) {
            point_double(f, r, p);      /* derselbe Punkt */
        } else {
            memset(r, 0, sizeof(*r));   /* P + (-P): Fernpunkt */
        }
        return;
    }

    mod_add(f, &rr, &rr, &rr);
    mod_add(f, &i, &h, &h);
    mont_mul(f, &i, &i, &i);
    mont_mul(f, &j, &h, &i);
    mont_mul(f, &v, &u1, &i);

    mod_add(f, &t, &p->z, &q->z);       /* z3 = ((z1 + z2)^2 - z1z1 - z2z2) h */
    mont_mul(f, &t, &t, &t);
    mod_sub(f, &t, &t, &z1z1);
    mod_sub(f, &t, &t, &z2z2);
    mont_mul(f, &r->z, &t, &h);

    mont_mul(f, &t, &rr, &rr);          /* x3 = r^2 - j - 2v */
    mod_sub(f, &t, &t, &j);
    mod_sub(f, &t, &t, &v);
    mod_sub(f, &r->x, &t, &v);

    mod_sub(f, &t, &v, &r->x);          /* y3 = r (v - x3) - 2 s1 j */
    mont_mul(f, &t, &rr, &t);
    mont_mul(f, &s1, &s1, &j);
    mod_add(f, &s1, &s1, &s1);
    mod_sub(f, &r->y, &t, &s1);
}

static void point_multiply(const mont_t *f, point_t *r, const point_t *p,
                           const bn_t *scalar)
{
    point_t result;
    memset(&result, 0, sizeof(result));
    for (int bit = f->n * 64 - 1; bit >= 0; bit--) {
        point_double(f, &result, &result);
        if ((scalar->v[bit / 64] >> (bit % 64)) & 1)
            point_add(f, &result, &result, p);
    }
    *r = result;
}

/* Liest eine DER-kodierte Zahl ("INTEGER") und rückt *p weiter. */
static bool der_integer(const uint8_t **p, const uint8_t *end, bn_t *out)
{
    if (end - *p < 2 || (*p)[0] != 0x02 || (*p)[1] >= 0x80)
        return false;
    size_t len = (*p)[1];
    if ((size_t)(end - *p) < 2 + len || len == 0)
        return false;
    bool ok = bn_from_bytes(out, *p + 2, len);
    *p += 2 + len;
    return ok;
}

bool ecdsa_verify(const public_key_t *key, const uint8_t *hash, size_t hash_len,
                  const uint8_t *signature, size_t len)
{
    const curve_t *curve = key->type == KEY_EC_P256 ? &curve_p256
                         : key->type == KEY_EC_P384 ? &curve_p384 : NULL;
    if (!curve)
        return false;
    size_t size = (size_t)curve->size;

    /* Signatur: SEQUENCE { r, s } */
    const uint8_t *p = signature, *end = signature + len;
    if (len < 8 || p[0] != 0x30)
        return false;
    if (p[1] < 0x80) {
        p += 2;
    } else if (p[1] == 0x81) {
        p += 3;
    } else {
        return false;
    }
    bn_t r, s;
    if (!der_integer(&p, end, &r) || !der_integer(&p, end, &s) || p != end)
        return false;

    mont_t field, order;
    if (!mont_init(&field, curve->p, size) || !mont_init(&order, curve->n, size))
        return false;
    if (bn_is_zero(&r, LIMBS) || bn_is_zero(&s, LIMBS) ||
        bn_cmp(&r, &order.m, LIMBS) >= 0 || bn_cmp(&s, &order.m, LIMBS) >= 0)
        return false;

    /* Der öffentliche Punkt muss wirklich auf der Kurve liegen. */
    point_t q, g;
    bn_t b, t, lhs, rhs, three;
    if (!bn_from_bytes(&q.x, key->point, size) ||
        !bn_from_bytes(&q.y, key->point + size, size) ||
        bn_cmp(&q.x, &field.m, LIMBS) >= 0 || bn_cmp(&q.y, &field.m, LIMBS) >= 0)
        return false;
    to_mont(&field, &q.x, &q.x);
    to_mont(&field, &q.y, &q.y);
    q.z = field.one;
    bn_from_bytes(&b, curve->b, size);
    to_mont(&field, &b, &b);
    mont_mul(&field, &lhs, &q.y, &q.y);
    mont_mul(&field, &rhs, &q.x, &q.x);
    mont_mul(&field, &rhs, &rhs, &q.x);
    mod_add(&field, &three, &q.x, &q.x);
    mod_add(&field, &three, &three, &q.x);
    mod_sub(&field, &rhs, &rhs, &three);
    mod_add(&field, &rhs, &rhs, &b);
    if (bn_cmp(&lhs, &rhs, field.n) != 0)
        return false;

    bn_from_bytes(&g.x, curve->gx, size);
    bn_from_bytes(&g.y, curve->gy, size);
    to_mont(&field, &g.x, &g.x);
    to_mont(&field, &g.y, &g.y);
    g.z = field.one;

    /* e = Hashwert, auf die Länge der Kurve gekürzt */
    bn_t e, w, u1, u2, exponent;
    if (hash_len > size)
        hash_len = size;
    bn_from_bytes(&e, hash, hash_len);
    while (bn_cmp(&e, &order.m, LIMBS) >= 0)
        bn_sub(&e, &e, &order.m, LIMBS);

    /* w = 1/s mod n, über s^(n-2) (n ist eine Primzahl) */
    memset(&t, 0, sizeof(t));
    t.v[0] = 2;
    bn_sub(&exponent, &order.m, &t, LIMBS);
    to_mont(&order, &w, &s);
    mont_exp(&order, &w, &w, &exponent);
    to_mont(&order, &u1, &e);
    mont_mul(&order, &u1, &u1, &w);
    from_mont(&order, &u1, &u1);
    to_mont(&order, &u2, &r);
    mont_mul(&order, &u2, &u2, &w);
    from_mont(&order, &u2, &u2);

    /* Punkt R = u1 G + u2 Q; gültig, wenn dessen x-Koordinate gleich r ist */
    point_t a, c, sum;
    point_multiply(&field, &a, &g, &u1);
    point_multiply(&field, &c, &q, &u2);
    point_add(&field, &sum, &a, &c);
    if (bn_is_zero(&sum.z, field.n))
        return false;

    /* x = X / Z^2, den Kehrwert wieder über eine Potenz (p ist prim) */
    bn_t z2, inverse, x;
    mont_mul(&field, &z2, &sum.z, &sum.z);
    bn_sub(&exponent, &field.m, &t, LIMBS);
    mont_exp(&field, &inverse, &z2, &exponent);
    mont_mul(&field, &x, &sum.x, &inverse);
    from_mont(&field, &x, &x);
    while (bn_cmp(&x, &order.m, LIMBS) >= 0)
        bn_sub(&x, &x, &order.m, LIMBS);
    return bn_cmp(&x, &r, LIMBS) == 0;
}

/* --- Schlüsselaustausch über P-256 ------------------------------------------------------------ */

/* Rechnet secret * P aus und liefert den Punkt in gewöhnlichen Koordinaten. */
static bool p256_multiply(const uint8_t secret[32], const uint8_t *peer,
                          uint8_t out_x[32], uint8_t out_y[32])
{
    const curve_t *curve = &curve_p256;
    mont_t field, order;
    bn_t scalar, two;
    point_t p, result;

    if (!mont_init(&field, curve->p, 32) || !mont_init(&order, curve->n, 32) ||
        !bn_from_bytes(&scalar, secret, 32))
        return false;
    if (bn_is_zero(&scalar, LIMBS) || bn_cmp(&scalar, &order.m, LIMBS) >= 0)
        return false;

    const uint8_t *px = peer ? peer : curve->gx;
    const uint8_t *py = peer ? peer + 32 : curve->gy;
    if (!bn_from_bytes(&p.x, px, 32) || !bn_from_bytes(&p.y, py, 32) ||
        bn_cmp(&p.x, &field.m, LIMBS) >= 0 || bn_cmp(&p.y, &field.m, LIMBS) >= 0)
        return false;
    to_mont(&field, &p.x, &p.x);
    to_mont(&field, &p.y, &p.y);
    p.z = field.one;

    /* Liegt der Punkt auf der Kurve? y^2 = x^3 - 3x + b */
    bn_t b, lhs, rhs, three;
    bn_from_bytes(&b, curve->b, 32);
    to_mont(&field, &b, &b);
    mont_mul(&field, &lhs, &p.y, &p.y);
    mont_mul(&field, &rhs, &p.x, &p.x);
    mont_mul(&field, &rhs, &rhs, &p.x);
    mod_add(&field, &three, &p.x, &p.x);
    mod_add(&field, &three, &three, &p.x);
    mod_sub(&field, &rhs, &rhs, &three);
    mod_add(&field, &rhs, &rhs, &b);
    if (bn_cmp(&lhs, &rhs, field.n) != 0)
        return false;

    point_multiply(&field, &result, &p, &scalar);
    if (bn_is_zero(&result.z, field.n))
        return false;

    /* x = X / Z^2, y = Y / Z^3; Kehrwert über die Potenz p - 2 */
    bn_t exponent, inverse, z2, z3, x, y;
    memset(&two, 0, sizeof(two));
    two.v[0] = 2;
    bn_sub(&exponent, &field.m, &two, LIMBS);
    mont_exp(&field, &inverse, &result.z, &exponent);
    mont_mul(&field, &z2, &inverse, &inverse);
    mont_mul(&field, &z3, &z2, &inverse);
    mont_mul(&field, &x, &result.x, &z2);
    mont_mul(&field, &y, &result.y, &z3);
    from_mont(&field, &x, &x);
    from_mont(&field, &y, &y);
    bn_to_bytes(&x, out_x, 32);
    bn_to_bytes(&y, out_y, 32);
    return true;
}

bool ecdh_p256_public(const uint8_t secret[32], uint8_t point[64])
{
    return p256_multiply(secret, NULL, point, point + 32);
}

bool ecdh_p256_shared(const uint8_t secret[32], const uint8_t peer[64],
                      uint8_t shared[32])
{
    uint8_t y[32];
    return p256_multiply(secret, peer, shared, y);
}
