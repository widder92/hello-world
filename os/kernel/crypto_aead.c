/*
 * ChaCha20-Poly1305 (RFC 8439): Verschlüsselung, die zugleich jede
 * Veränderung der Daten erkennt. ChaCha20 erzeugt einen Schlüsselstrom, der
 * mit dem Klartext verknüpft wird; Poly1305 berechnet eine Prüfsumme, die
 * nur mit dem Schlüssel stimmt.
 */
#include "crypto.h"
#include "string.h"

static inline uint32_t load32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static inline void store32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/* --- ChaCha20 ------------------------------------------------------------------- */

static inline uint32_t rotl32(uint32_t x, int n)
{
    return (x << n) | (x >> (32 - n));
}

#define QUARTER(a, b, c, d)          \
    do {                             \
        a += b; d = rotl32(d ^ a, 16); \
        c += d; b = rotl32(b ^ c, 12); \
        a += b; d = rotl32(d ^ a, 8);  \
        c += d; b = rotl32(b ^ c, 7);  \
    } while (0)

/* Erzeugt den 64-Byte-Block des Schlüsselstroms mit der Nummer counter. */
static void chacha20_block(const uint8_t key[32], const uint8_t nonce[12],
                           uint32_t counter, uint8_t out[64])
{
    uint32_t state[16] = {
        0x61707865, 0x3320646e, 0x79622d32, 0x6b206574,     /* "expand 32-byte k" */
    };
    for (int i = 0; i < 8; i++)
        state[4 + i] = load32(key + 4 * i);
    state[12] = counter;
    for (int i = 0; i < 3; i++)
        state[13 + i] = load32(nonce + 4 * i);

    uint32_t x[16];
    memcpy(x, state, sizeof(x));
    for (int round = 0; round < 10; round++) {
        QUARTER(x[0], x[4], x[8], x[12]);       /* Spalten */
        QUARTER(x[1], x[5], x[9], x[13]);
        QUARTER(x[2], x[6], x[10], x[14]);
        QUARTER(x[3], x[7], x[11], x[15]);
        QUARTER(x[0], x[5], x[10], x[15]);      /* Diagonalen */
        QUARTER(x[1], x[6], x[11], x[12]);
        QUARTER(x[2], x[7], x[8], x[13]);
        QUARTER(x[3], x[4], x[9], x[14]);
    }
    for (int i = 0; i < 16; i++)
        store32(out + 4 * i, x[i] + state[i]);
}

static void chacha20_xor(const uint8_t key[32], const uint8_t nonce[12],
                         uint32_t counter, const uint8_t *in, uint8_t *out,
                         size_t len)
{
    uint8_t block[64];
    while (len > 0) {
        chacha20_block(key, nonce, counter++, block);
        size_t n = len < 64 ? len : 64;
        for (size_t i = 0; i < n; i++)
            out[i] = in[i] ^ block[i];
        in += n;
        out += n;
        len -= n;
    }
}

/* --- Poly1305 --------------------------------------------------------------------- */

/* Rechnet modulo 2^130 - 5 mit fünf Ziffern zu je 26 Bit. */
typedef struct {
    uint32_t r[5];
    uint32_t h[5];
    uint32_t pad[4];
    uint8_t buffer[16];
    size_t used;
} poly1305_t;

static void poly1305_init(poly1305_t *p, const uint8_t key[32])
{
    p->r[0] = load32(key) & 0x3ffffff;
    p->r[1] = (load32(key + 3) >> 2) & 0x3ffff03;
    p->r[2] = (load32(key + 6) >> 4) & 0x3ffc0ff;
    p->r[3] = (load32(key + 9) >> 6) & 0x3f03fff;
    p->r[4] = (load32(key + 12) >> 8) & 0x00fffff;
    memset(p->h, 0, sizeof(p->h));
    for (int i = 0; i < 4; i++)
        p->pad[i] = load32(key + 16 + 4 * i);
    p->used = 0;
}

static void poly1305_block(poly1305_t *p, const uint8_t *m, uint32_t high_bit)
{
    const uint32_t r0 = p->r[0], r1 = p->r[1], r2 = p->r[2], r3 = p->r[3],
                   r4 = p->r[4];
    const uint32_t s1 = r1 * 5, s2 = r2 * 5, s3 = r3 * 5, s4 = r4 * 5;

    /* h += Block */
    uint32_t h0 = p->h[0] + (load32(m) & 0x3ffffff);
    uint32_t h1 = p->h[1] + ((load32(m + 3) >> 2) & 0x3ffffff);
    uint32_t h2 = p->h[2] + ((load32(m + 6) >> 4) & 0x3ffffff);
    uint32_t h3 = p->h[3] + ((load32(m + 9) >> 6) & 0x3ffffff);
    uint32_t h4 = p->h[4] + ((load32(m + 12) >> 8) | high_bit);

    /* h *= r (die Faktoren s = 5*r falten den Überlauf über 2^130 zurück) */
    uint64_t d0 = (uint64_t)h0 * r0 + (uint64_t)h1 * s4 + (uint64_t)h2 * s3 +
                  (uint64_t)h3 * s2 + (uint64_t)h4 * s1;
    uint64_t d1 = (uint64_t)h0 * r1 + (uint64_t)h1 * r0 + (uint64_t)h2 * s4 +
                  (uint64_t)h3 * s3 + (uint64_t)h4 * s2;
    uint64_t d2 = (uint64_t)h0 * r2 + (uint64_t)h1 * r1 + (uint64_t)h2 * r0 +
                  (uint64_t)h3 * s4 + (uint64_t)h4 * s3;
    uint64_t d3 = (uint64_t)h0 * r3 + (uint64_t)h1 * r2 + (uint64_t)h2 * r1 +
                  (uint64_t)h3 * r0 + (uint64_t)h4 * s4;
    uint64_t d4 = (uint64_t)h0 * r4 + (uint64_t)h1 * r3 + (uint64_t)h2 * r2 +
                  (uint64_t)h3 * r1 + (uint64_t)h4 * r0;

    /* Überträge weiterreichen */
    uint64_t c = d0 >> 26;
    h0 = (uint32_t)d0 & 0x3ffffff;
    d1 += c;
    c = d1 >> 26;
    h1 = (uint32_t)d1 & 0x3ffffff;
    d2 += c;
    c = d2 >> 26;
    h2 = (uint32_t)d2 & 0x3ffffff;
    d3 += c;
    c = d3 >> 26;
    h3 = (uint32_t)d3 & 0x3ffffff;
    d4 += c;
    c = d4 >> 26;
    h4 = (uint32_t)d4 & 0x3ffffff;
    h0 += (uint32_t)c * 5;
    h1 += h0 >> 26;
    h0 &= 0x3ffffff;

    p->h[0] = h0;
    p->h[1] = h1;
    p->h[2] = h2;
    p->h[3] = h3;
    p->h[4] = h4;
}

static void poly1305_update(poly1305_t *p, const uint8_t *data, size_t len)
{
    while (len > 0) {
        size_t n = 16 - p->used;
        if (n > len)
            n = len;
        memcpy(p->buffer + p->used, data, n);
        p->used += n;
        data += n;
        len -= n;
        if (p->used == 16) {
            poly1305_block(p, p->buffer, 1u << 24);
            p->used = 0;
        }
    }
}

static void poly1305_final(poly1305_t *p, uint8_t tag[16])
{
    if (p->used > 0) {                  /* letzter, unvollständiger Block */
        p->buffer[p->used] = 1;
        memset(p->buffer + p->used + 1, 0, 15 - p->used);
        poly1305_block(p, p->buffer, 0);
    }

    uint32_t h0 = p->h[0], h1 = p->h[1], h2 = p->h[2], h3 = p->h[3], h4 = p->h[4];
    uint32_t c = h1 >> 26;
    h1 &= 0x3ffffff;
    h2 += c;
    c = h2 >> 26;
    h2 &= 0x3ffffff;
    h3 += c;
    c = h3 >> 26;
    h3 &= 0x3ffffff;
    h4 += c;
    c = h4 >> 26;
    h4 &= 0x3ffffff;
    h0 += c * 5;
    c = h0 >> 26;
    h0 &= 0x3ffffff;
    h1 += c;

    /* g = h - (2^130 - 5); ist g nicht negativ, war h zu groß: g nehmen */
    uint32_t g0 = h0 + 5;
    c = g0 >> 26;
    g0 &= 0x3ffffff;
    uint32_t g1 = h1 + c;
    c = g1 >> 26;
    g1 &= 0x3ffffff;
    uint32_t g2 = h2 + c;
    c = g2 >> 26;
    g2 &= 0x3ffffff;
    uint32_t g3 = h3 + c;
    c = g3 >> 26;
    g3 &= 0x3ffffff;
    uint32_t g4 = h4 + c - (1u << 26);

    uint32_t take_g = (g4 >> 31) - 1;   /* alle Bits 1, wenn g4 nicht negativ */
    h0 = (h0 & ~take_g) | (g0 & take_g);
    h1 = (h1 & ~take_g) | (g1 & take_g);
    h2 = (h2 & ~take_g) | (g2 & take_g);
    h3 = (h3 & ~take_g) | (g3 & take_g);
    h4 = (h4 & ~take_g) | (g4 & take_g);

    /* In vier 32-Bit-Worte umpacken und den zweiten Schlüsselteil addieren */
    uint64_t f0 = ((h0) | (h1 << 26)) + (uint64_t)p->pad[0];
    uint64_t f1 = ((h1 >> 6) | (h2 << 20)) + (uint64_t)p->pad[1] + (f0 >> 32);
    uint64_t f2 = ((h2 >> 12) | (h3 << 14)) + (uint64_t)p->pad[2] + (f1 >> 32);
    uint64_t f3 = ((h3 >> 18) | (h4 << 8)) + (uint64_t)p->pad[3] + (f2 >> 32);
    store32(tag, (uint32_t)f0);
    store32(tag + 4, (uint32_t)f1);
    store32(tag + 8, (uint32_t)f2);
    store32(tag + 12, (uint32_t)f3);
}

void chacha20_crypt(const uint8_t key[32], const uint8_t nonce[12],
                    uint32_t counter, const uint8_t *in, uint8_t *out, size_t len)
{
    chacha20_xor(key, nonce, counter, in, out, len);
}

void poly1305_mac(const uint8_t key[32], const void *data, size_t len,
                  uint8_t tag[16])
{
    poly1305_t poly;
    poly1305_init(&poly, key);
    poly1305_update(&poly, data, len);
    poly1305_final(&poly, tag);
}

/* --- Beides zusammen --------------------------------------------------------------- */

static void compute_tag(const uint8_t key[32], const uint8_t nonce[12],
                        const uint8_t *aad, size_t aad_len,
                        const uint8_t *cipher, size_t len, uint8_t tag[16])
{
    static const uint8_t zeros[16];
    uint8_t poly_key[64];
    uint8_t lengths[16];
    poly1305_t poly;

    /* Der Poly1305-Schlüssel ist der Anfang von Schlüsselstrom-Block 0. */
    chacha20_block(key, nonce, 0, poly_key);
    poly1305_init(&poly, poly_key);

    poly1305_update(&poly, aad, aad_len);
    poly1305_update(&poly, zeros, (16 - aad_len % 16) % 16);
    poly1305_update(&poly, cipher, len);
    poly1305_update(&poly, zeros, (16 - len % 16) % 16);
    for (int i = 0; i < 8; i++) {
        lengths[i] = (uint8_t)((uint64_t)aad_len >> (8 * i));
        lengths[8 + i] = (uint8_t)((uint64_t)len >> (8 * i));
    }
    poly1305_update(&poly, lengths, 16);
    poly1305_final(&poly, tag);
}

void chacha20poly1305_encrypt(const uint8_t key[32], const uint8_t nonce[12],
                              const uint8_t *aad, size_t aad_len,
                              const uint8_t *plain, size_t len, uint8_t *cipher,
                              uint8_t tag[16])
{
    chacha20_xor(key, nonce, 1, plain, cipher, len);
    compute_tag(key, nonce, aad, aad_len, cipher, len, tag);
}

bool chacha20poly1305_decrypt(const uint8_t key[32], const uint8_t nonce[12],
                              const uint8_t *aad, size_t aad_len,
                              const uint8_t *cipher, size_t len,
                              const uint8_t tag[16], uint8_t *plain)
{
    uint8_t expected[16];
    compute_tag(key, nonce, aad, aad_len, cipher, len, expected);

    uint8_t difference = 0;             /* ohne vorzeitigen Abbruch vergleichen */
    for (int i = 0; i < 16; i++)
        difference |= expected[i] ^ tag[i];
    if (difference)
        return false;

    chacha20_xor(key, nonce, 1, cipher, plain, len);
    return true;
}
