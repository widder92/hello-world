/*
 * Elliptische Kurve 25519: Schlüsselaustausch (X25519) und Prüfen von
 * Signaturen (Ed25519).
 *
 * Gerechnet wird mit Zahlen modulo p = 2^255 - 19. Eine solche Zahl ("fe")
 * besteht aus fünf Ziffern zu je 51 Bit; Zwischenergebnisse dürfen etwas
 * größer werden und werden erst bei Bedarf wieder verkleinert.
 */
#include "crypto.h"
#include "string.h"

typedef unsigned __int128 u128;
typedef uint64_t fe[5];

#define MASK51 ((1ull << 51) - 1)

static uint64_t load64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--)
        v = (v << 8) | p[i];
    return v;
}

static void fe_copy(fe out, const fe a)
{
    memcpy(out, a, sizeof(fe));
}

static void fe_set(fe out, uint64_t value)
{
    memset(out, 0, sizeof(fe));
    out[0] = value;
}

static void fe_frombytes(fe out, const uint8_t s[32])
{
    out[0] = load64(s) & MASK51;
    out[1] = (load64(s + 6) >> 3) & MASK51;
    out[2] = (load64(s + 12) >> 6) & MASK51;
    out[3] = (load64(s + 19) >> 1) & MASK51;
    out[4] = (load64(s + 24) >> 12) & MASK51;
}

static void fe_carry(fe h)
{
    for (int round = 0; round < 2; round++) {
        for (int i = 0; i < 4; i++) {
            h[i + 1] += h[i] >> 51;
            h[i] &= MASK51;
        }
        h[0] += 19 * (h[4] >> 51);
        h[4] &= MASK51;
    }
}

/* Eindeutige Darstellung (0 <= Wert < p) als 32 Bytes. */
static void fe_tobytes(uint8_t out[32], const fe a)
{
    fe h;
    fe_copy(h, a);
    fe_carry(h);

    /* q = 1, falls h >= p: dann p abziehen (= 19 addieren, Bit 255 streichen) */
    uint64_t q = (h[0] + 19) >> 51;
    for (int i = 1; i < 5; i++)
        q = (h[i] + q) >> 51;
    h[0] += 19 * q;
    for (int i = 0; i < 4; i++) {
        h[i + 1] += h[i] >> 51;
        h[i] &= MASK51;
    }
    h[4] &= MASK51;

    /* 5 x 51 Bit dicht in 32 Bytes packen */
    uint64_t words[4] = {
        h[0] | (h[1] << 51),
        (h[1] >> 13) | (h[2] << 38),
        (h[2] >> 26) | (h[3] << 25),
        (h[3] >> 39) | (h[4] << 12),
    };
    for (int i = 0; i < 4; i++) {
        for (int k = 0; k < 8; k++)
            out[8 * i + k] = (uint8_t)(words[i] >> (8 * k));
    }
}

static void fe_add(fe out, const fe a, const fe b)
{
    for (int i = 0; i < 5; i++)
        out[i] = a[i] + b[i];
}

/* a - b; damit nichts negativ wird, kommt vorher 2p dazu. */
static void fe_sub(fe out, const fe a, const fe b)
{
    out[0] = a[0] + 0xFFFFFFFFFFFDAull - b[0];
    for (int i = 1; i < 5; i++)
        out[i] = a[i] + 0xFFFFFFFFFFFFEull - b[i];
    fe_carry(out);
}

static void fe_mul(fe out, const fe a, const fe b)
{
    /* Schulmultiplikation; Überlauf über 2^255 zählt 19-fach (2^255 = 19 mod p) */
    u128 r0 = (u128)a[0] * b[0] +
              19 * ((u128)a[1] * b[4] + (u128)a[2] * b[3] + (u128)a[3] * b[2] +
                    (u128)a[4] * b[1]);
    u128 r1 = (u128)a[0] * b[1] + (u128)a[1] * b[0] +
              19 * ((u128)a[2] * b[4] + (u128)a[3] * b[3] + (u128)a[4] * b[2]);
    u128 r2 = (u128)a[0] * b[2] + (u128)a[1] * b[1] + (u128)a[2] * b[0] +
              19 * ((u128)a[3] * b[4] + (u128)a[4] * b[3]);
    u128 r3 = (u128)a[0] * b[3] + (u128)a[1] * b[2] + (u128)a[2] * b[1] +
              (u128)a[3] * b[0] + 19 * ((u128)a[4] * b[4]);
    u128 r4 = (u128)a[0] * b[4] + (u128)a[1] * b[3] + (u128)a[2] * b[2] +
              (u128)a[3] * b[1] + (u128)a[4] * b[0];

    r1 += (uint64_t)(r0 >> 51);
    r2 += (uint64_t)(r1 >> 51);
    r3 += (uint64_t)(r2 >> 51);
    r4 += (uint64_t)(r3 >> 51);
    out[0] = ((uint64_t)r0 & MASK51) + 19 * (uint64_t)(r4 >> 51);
    out[1] = (uint64_t)r1 & MASK51;
    out[2] = (uint64_t)r2 & MASK51;
    out[3] = (uint64_t)r3 & MASK51;
    out[4] = (uint64_t)r4 & MASK51;
    out[1] += out[0] >> 51;
    out[0] &= MASK51;
}

static void fe_sq(fe out, const fe a)
{
    fe_mul(out, a, a);
}

/* base hoch exponent (32 Bytes, niederwertigstes zuerst). */
static void fe_pow(fe out, const fe base, const uint8_t exponent[32])
{
    fe result, b;
    fe_set(result, 1);
    fe_copy(b, base);
    for (int bit = 254; bit >= 0; bit--) {
        fe_sq(result, result);
        if ((exponent[bit / 8] >> (bit % 8)) & 1)
            fe_mul(result, result, b);
    }
    fe_copy(out, result);
}

/* Kehrwert: a^(p-2) (kleiner Satz von Fermat). */
static void fe_invert(fe out, const fe a)
{
    static const uint8_t p_minus_2[32] = {
        0xEB, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F,
    };
    fe_pow(out, a, p_minus_2);
}

/* Tauscht a und b, wenn swap = 1 - ohne Verzweigung. */
static void fe_cswap(fe a, fe b, uint64_t swap)
{
    uint64_t mask = 0 - swap;
    for (int i = 0; i < 5; i++) {
        uint64_t t = mask & (a[i] ^ b[i]);
        a[i] ^= t;
        b[i] ^= t;
    }
}

/* --- X25519: Schlüsselaustausch ------------------------------------------------------ */

void x25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t point[32])
{
    uint8_t k[32];
    memcpy(k, scalar, 32);
    k[0] &= 248;                        /* vorgeschriebene Form des Geheimnisses */
    k[31] &= 127;
    k[31] |= 64;

    fe x1, x2, z2, x3, z3, a, aa, b, bb, e, c, d, da, cb, t;
    fe_frombytes(x1, point);
    fe_set(x2, 1);
    fe_set(z2, 0);
    fe_copy(x3, x1);
    fe_set(z3, 1);

    /* Montgomery-Leiter: pro Bit des Geheimnisses ein Schritt */
    uint64_t swap = 0;
    for (int bit = 254; bit >= 0; bit--) {
        uint64_t k_bit = (k[bit / 8] >> (bit % 8)) & 1;
        swap ^= k_bit;
        fe_cswap(x2, x3, swap);
        fe_cswap(z2, z3, swap);
        swap = k_bit;

        fe_add(a, x2, z2);
        fe_sq(aa, a);
        fe_sub(b, x2, z2);
        fe_sq(bb, b);
        fe_sub(e, aa, bb);
        fe_add(c, x3, z3);
        fe_sub(d, x3, z3);
        fe_mul(da, d, a);
        fe_mul(cb, c, b);
        fe_add(t, da, cb);
        fe_sq(x3, t);
        fe_sub(t, da, cb);
        fe_sq(t, t);
        fe_mul(z3, x1, t);
        fe_mul(x2, aa, bb);
        fe_set(t, 121665);
        fe_mul(t, t, e);
        fe_add(t, aa, t);
        fe_mul(z2, e, t);
    }
    fe_cswap(x2, x3, swap);
    fe_cswap(z2, z3, swap);

    fe_invert(z2, z2);
    fe_mul(x2, x2, z2);
    fe_tobytes(out, x2);
}

void x25519_public(uint8_t out[32], const uint8_t scalar[32])
{
    static const uint8_t base_point[32] = { 9 };
    x25519(out, scalar, base_point);
}

/* --- Ed25519: Signaturen prüfen -------------------------------------------------------- */

/* Punkt auf der Kurve -x^2 + y^2 = 1 + d x^2 y^2, als (X:Y:Z:T) mit T = XY/Z. */
typedef struct {
    fe x, y, z, t;
} point_t;

static fe curve_d, curve_2d, sqrt_minus_1;
static point_t base_point;
static bool initialized;

static bool fe_equal(const fe a, const fe b)
{
    uint8_t x[32], y[32];
    fe_tobytes(x, a);
    fe_tobytes(y, b);
    return memcmp(x, y, 32) == 0;
}

static void fe_neg(fe out, const fe a)
{
    fe zero;
    fe_set(zero, 0);
    fe_sub(out, zero, a);
}

/* Addition zweier Punkte; funktioniert auch für P + P. */
static void point_add(point_t *out, const point_t *p, const point_t *q)
{
    fe a, b, c, d, e, f, g, h, t;
    fe_sub(a, p->y, p->x);
    fe_sub(t, q->y, q->x);
    fe_mul(a, a, t);
    fe_add(b, p->y, p->x);
    fe_add(t, q->y, q->x);
    fe_mul(b, b, t);
    fe_mul(c, p->t, q->t);
    fe_mul(c, c, curve_2d);
    fe_mul(d, p->z, q->z);
    fe_add(d, d, d);
    fe_sub(e, b, a);
    fe_sub(f, d, c);
    fe_add(g, d, c);
    fe_add(h, b, a);
    fe_mul(out->x, e, f);
    fe_mul(out->y, g, h);
    fe_mul(out->t, e, h);
    fe_mul(out->z, f, g);
}

/* Vielfaches eines Punktes: scalar hat bits Bit, niederwertigstes Byte zuerst. */
static void point_multiply(point_t *out, const point_t *p,
                           const uint8_t *scalar, int bits)
{
    point_t result;
    fe_set(result.x, 0);                /* neutrales Element (0, 1) */
    fe_set(result.y, 1);
    fe_set(result.z, 1);
    fe_set(result.t, 0);
    for (int bit = bits - 1; bit >= 0; bit--) {
        point_add(&result, &result, &result);
        if ((scalar[bit / 8] >> (bit % 8)) & 1)
            point_add(&result, &result, p);
    }
    *out = result;
}

/* Stellt aus den 32 Bytes (y und das Vorzeichen von x) den Punkt wieder her. */
static bool point_decode(point_t *out, const uint8_t s[32])
{
    static const uint8_t exponent[32] = {       /* (p - 5) / 8 */
        0xFD, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x0F,
    };
    fe u, v, v3, x, check, one;
    fe_set(one, 1);
    fe_frombytes(out->y, s);
    fe_set(out->z, 1);

    /* x^2 = (y^2 - 1) / (d y^2 + 1) = u / v */
    fe_sq(u, out->y);
    fe_mul(v, u, curve_d);
    fe_sub(u, u, one);
    fe_add(v, v, one);

    /* Kandidat für die Wurzel: x = u v^3 (u v^7)^((p-5)/8) */
    fe_sq(v3, v);
    fe_mul(v3, v3, v);
    fe_sq(x, v3);
    fe_mul(x, x, v);
    fe_mul(x, x, u);
    fe_pow(x, x, exponent);
    fe_mul(x, x, v3);
    fe_mul(x, x, u);

    fe_sq(check, x);
    fe_mul(check, check, v);
    if (!fe_equal(check, u)) {
        fe_neg(u, u);
        if (!fe_equal(check, u))
            return false;               /* kein Punkt der Kurve */
        fe_mul(x, x, sqrt_minus_1);
    }

    uint8_t bytes[32];
    fe_tobytes(bytes, x);
    if ((bytes[0] & 1) != (s[31] >> 7))
        fe_neg(x, x);
    fe_copy(out->x, x);
    fe_mul(out->t, out->x, out->y);
    return true;
}

static void point_encode(uint8_t out[32], const point_t *p)
{
    fe inverse, x, y;
    uint8_t x_bytes[32];
    fe_invert(inverse, p->z);
    fe_mul(x, p->x, inverse);
    fe_mul(y, p->y, inverse);
    fe_tobytes(out, y);
    fe_tobytes(x_bytes, x);
    out[31] |= (uint8_t)((x_bytes[0] & 1) << 7);
}

/* Die Konstanten der Kurve werden einmalig ausgerechnet statt abgetippt. */
static void curve_init(void)
{
    static const uint8_t quarter[32] = {        /* (p - 1) / 4 */
        0xFB, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x1F,
    };
    /* Basispunkt: y = 4/5, x positiv */
    static const uint8_t base[32] = {
        0x58, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66,
        0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66,
        0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66,
    };
    fe a, b;

    fe_set(a, 121665);                  /* d = -121665 / 121666 */
    fe_neg(a, a);
    fe_set(b, 121666);
    fe_invert(b, b);
    fe_mul(curve_d, a, b);
    fe_add(curve_2d, curve_d, curve_d);

    fe_set(a, 2);                       /* Wurzel aus -1: 2^((p-1)/4) */
    fe_pow(sqrt_minus_1, a, quarter);

    point_decode(&base_point, base);
    initialized = true;
}

bool ed25519_verify(const uint8_t signature[64], const uint8_t public_key[32],
                    const void *message, size_t len)
{
    /* Ordnung der Punktgruppe; der Teil s der Signatur muss kleiner sein. */
    static const uint8_t order[32] = {
        0xED, 0xD3, 0xF5, 0x5C, 0x1A, 0x63, 0x12, 0x58, 0xD6, 0x9C, 0xF7,
        0xA2, 0xDE, 0xF9, 0xDE, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10,
    };
    if (!initialized)
        curve_init();

    const uint8_t *r = signature;
    const uint8_t *s = signature + 32;
    for (int i = 31; i >= 0; i--) {
        if (s[i] < order[i])
            break;
        if (s[i] > order[i] || i == 0)
            return false;
    }

    point_t a;
    if (!point_decode(&a, public_key))
        return false;

    /* h = SHA-512(R || A || Nachricht) */
    uint8_t h[64];
    sha512_t hash;
    sha512_init(&hash);
    sha512_update(&hash, r, 32);
    sha512_update(&hash, public_key, 32);
    sha512_update(&hash, message, len);
    sha512_final(&hash, h);

    /* Gültig, wenn s*B = R + h*A, also s*B + h*(-A) = R. */
    point_t sb, ha, sum;
    fe_neg(a.x, a.x);
    fe_neg(a.t, a.t);
    point_multiply(&sb, &base_point, s, 256);
    point_multiply(&ha, &a, h, 512);
    point_add(&sum, &sb, &ha);

    uint8_t encoded[32];
    point_encode(encoded, &sum);
    return memcmp(encoded, r, 32) == 0;
}
