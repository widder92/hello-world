/*
 * Hashfunktionen SHA-256 und SHA-512 (FIPS 180-4), dazu HMAC und HKDF.
 *
 * Die Tabellen sind die Nachkommastellen der Kubik- bzw. Quadratwurzeln der
 * ersten Primzahlen - so schreibt es der Standard vor.
 */
#include "crypto.h"
#include "string.h"

/* --- SHA-256 ------------------------------------------------------------------- */

static const uint32_t k256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static inline uint32_t rotr32(uint32_t x, int n)
{
    return (x >> n) | (x << (32 - n));
}

static void sha256_block(sha256_t *ctx, const uint8_t *p)
{
    uint32_t w[64];
    for (int i = 0; i < 16; i++, p += 4)
        w[i] = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
               ((uint32_t)p[2] << 8) | p[3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = ctx->state[0], b = ctx->state[1], c = ctx->state[2];
    uint32_t d = ctx->state[3], e = ctx->state[4], f = ctx->state[5];
    uint32_t g = ctx->state[6], h = ctx->state[7];
    for (int i = 0; i < 64; i++) {
        uint32_t s1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        uint32_t t1 = h + s1 + ((e & f) ^ (~e & g)) + k256[i] + w[i];
        uint32_t s0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        uint32_t t2 = s0 + ((a & b) ^ (a & c) ^ (b & c));
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

void sha256_init(sha256_t *ctx)
{
    static const uint32_t initial[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
    };
    memcpy(ctx->state, initial, sizeof(initial));
    ctx->length = 0;
    ctx->used = 0;
}

void sha256_update(sha256_t *ctx, const void *data, size_t len)
{
    const uint8_t *p = data;
    ctx->length += len;
    while (len > 0) {
        size_t n = 64 - ctx->used;
        if (n > len)
            n = len;
        memcpy(ctx->buffer + ctx->used, p, n);
        ctx->used += n;
        p += n;
        len -= n;
        if (ctx->used == 64) {
            sha256_block(ctx, ctx->buffer);
            ctx->used = 0;
        }
    }
}

void sha256_final(sha256_t *ctx, uint8_t out[32])
{
    /* Abschluss: ein 1-Bit, Nullen, dann die Länge in Bits */
    uint64_t bits = ctx->length * 8;
    uint8_t pad[72] = { 0x80 };
    size_t pad_len = (ctx->used < 56 ? 56 : 120) - ctx->used;
    for (int i = 0; i < 8; i++)
        pad[pad_len + i] = (uint8_t)(bits >> (56 - 8 * i));
    sha256_update(ctx, pad, pad_len + 8);

    for (int i = 0; i < 8; i++) {
        out[4 * i] = (uint8_t)(ctx->state[i] >> 24);
        out[4 * i + 1] = (uint8_t)(ctx->state[i] >> 16);
        out[4 * i + 2] = (uint8_t)(ctx->state[i] >> 8);
        out[4 * i + 3] = (uint8_t)ctx->state[i];
    }
}

void sha256(const void *data, size_t len, uint8_t out[32])
{
    sha256_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, out);
}

/* --- SHA-512 ------------------------------------------------------------------- */

static const uint64_t k512[80] = {
    0x428a2f98d728ae22ull, 0x7137449123ef65cdull, 0xb5c0fbcfec4d3b2full,
    0xe9b5dba58189dbbcull, 0x3956c25bf348b538ull, 0x59f111f1b605d019ull,
    0x923f82a4af194f9bull, 0xab1c5ed5da6d8118ull, 0xd807aa98a3030242ull,
    0x12835b0145706fbeull, 0x243185be4ee4b28cull, 0x550c7dc3d5ffb4e2ull,
    0x72be5d74f27b896full, 0x80deb1fe3b1696b1ull, 0x9bdc06a725c71235ull,
    0xc19bf174cf692694ull, 0xe49b69c19ef14ad2ull, 0xefbe4786384f25e3ull,
    0x0fc19dc68b8cd5b5ull, 0x240ca1cc77ac9c65ull, 0x2de92c6f592b0275ull,
    0x4a7484aa6ea6e483ull, 0x5cb0a9dcbd41fbd4ull, 0x76f988da831153b5ull,
    0x983e5152ee66dfabull, 0xa831c66d2db43210ull, 0xb00327c898fb213full,
    0xbf597fc7beef0ee4ull, 0xc6e00bf33da88fc2ull, 0xd5a79147930aa725ull,
    0x06ca6351e003826full, 0x142929670a0e6e70ull, 0x27b70a8546d22ffcull,
    0x2e1b21385c26c926ull, 0x4d2c6dfc5ac42aedull, 0x53380d139d95b3dfull,
    0x650a73548baf63deull, 0x766a0abb3c77b2a8ull, 0x81c2c92e47edaee6ull,
    0x92722c851482353bull, 0xa2bfe8a14cf10364ull, 0xa81a664bbc423001ull,
    0xc24b8b70d0f89791ull, 0xc76c51a30654be30ull, 0xd192e819d6ef5218ull,
    0xd69906245565a910ull, 0xf40e35855771202aull, 0x106aa07032bbd1b8ull,
    0x19a4c116b8d2d0c8ull, 0x1e376c085141ab53ull, 0x2748774cdf8eeb99ull,
    0x34b0bcb5e19b48a8ull, 0x391c0cb3c5c95a63ull, 0x4ed8aa4ae3418acbull,
    0x5b9cca4f7763e373ull, 0x682e6ff3d6b2b8a3ull, 0x748f82ee5defb2fcull,
    0x78a5636f43172f60ull, 0x84c87814a1f0ab72ull, 0x8cc702081a6439ecull,
    0x90befffa23631e28ull, 0xa4506cebde82bde9ull, 0xbef9a3f7b2c67915ull,
    0xc67178f2e372532bull, 0xca273eceea26619cull, 0xd186b8c721c0c207ull,
    0xeada7dd6cde0eb1eull, 0xf57d4f7fee6ed178ull, 0x06f067aa72176fbaull,
    0x0a637dc5a2c898a6ull, 0x113f9804bef90daeull, 0x1b710b35131c471bull,
    0x28db77f523047d84ull, 0x32caab7b40c72493ull, 0x3c9ebe0a15c9bebcull,
    0x431d67c49c100d4cull, 0x4cc5d4becb3e42b6ull, 0x597f299cfc657e2aull,
    0x5fcb6fab3ad6faecull, 0x6c44198c4a475817ull,
};

static inline uint64_t rotr64(uint64_t x, int n)
{
    return (x >> n) | (x << (64 - n));
}

static void sha512_block(sha512_t *ctx, const uint8_t *p)
{
    uint64_t w[80];
    for (int i = 0; i < 16; i++, p += 8) {
        w[i] = 0;
        for (int k = 0; k < 8; k++)
            w[i] = (w[i] << 8) | p[k];
    }
    for (int i = 16; i < 80; i++) {
        uint64_t s0 = rotr64(w[i - 15], 1) ^ rotr64(w[i - 15], 8) ^ (w[i - 15] >> 7);
        uint64_t s1 = rotr64(w[i - 2], 19) ^ rotr64(w[i - 2], 61) ^ (w[i - 2] >> 6);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint64_t a = ctx->state[0], b = ctx->state[1], c = ctx->state[2];
    uint64_t d = ctx->state[3], e = ctx->state[4], f = ctx->state[5];
    uint64_t g = ctx->state[6], h = ctx->state[7];
    for (int i = 0; i < 80; i++) {
        uint64_t s1 = rotr64(e, 14) ^ rotr64(e, 18) ^ rotr64(e, 41);
        uint64_t t1 = h + s1 + ((e & f) ^ (~e & g)) + k512[i] + w[i];
        uint64_t s0 = rotr64(a, 28) ^ rotr64(a, 34) ^ rotr64(a, 39);
        uint64_t t2 = s0 + ((a & b) ^ (a & c) ^ (b & c));
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

void sha512_init(sha512_t *ctx)
{
    static const uint64_t initial[8] = {
    0x6a09e667f3bcc908ull, 0xbb67ae8584caa73bull, 0x3c6ef372fe94f82bull,
    0xa54ff53a5f1d36f1ull, 0x510e527fade682d1ull, 0x9b05688c2b3e6c1full,
    0x1f83d9abfb41bd6bull, 0x5be0cd19137e2179ull,
    };
    memcpy(ctx->state, initial, sizeof(initial));
    ctx->length = 0;
    ctx->used = 0;
}

void sha512_update(sha512_t *ctx, const void *data, size_t len)
{
    const uint8_t *p = data;
    ctx->length += len;
    while (len > 0) {
        size_t n = 128 - ctx->used;
        if (n > len)
            n = len;
        memcpy(ctx->buffer + ctx->used, p, n);
        ctx->used += n;
        p += n;
        len -= n;
        if (ctx->used == 128) {
            sha512_block(ctx, ctx->buffer);
            ctx->used = 0;
        }
    }
}

void sha512_final(sha512_t *ctx, uint8_t out[64])
{
    uint64_t bits = ctx->length * 8;
    uint8_t pad[144] = { 0x80 };
    size_t pad_len = (ctx->used < 112 ? 112 : 240) - ctx->used;
    /* Die Länge ist 128 Bit breit; die oberen 64 bleiben hier immer null. */
    for (int i = 0; i < 8; i++)
        pad[pad_len + 8 + i] = (uint8_t)(bits >> (56 - 8 * i));
    sha512_update(ctx, pad, pad_len + 16);

    for (int i = 0; i < 8; i++) {
        for (int k = 0; k < 8; k++)
            out[8 * i + k] = (uint8_t)(ctx->state[i] >> (56 - 8 * k));
    }
}

/* --- HMAC und HKDF ---------------------------------------------------------------- */

void hmac_sha256(const uint8_t *key, size_t key_len, const void *data,
                 size_t len, uint8_t out[32])
{
    uint8_t block[64] = { 0 };
    uint8_t inner[32];
    sha256_t ctx;

    if (key_len > 64)
        sha256(key, key_len, block);
    else
        memcpy(block, key, key_len);

    for (int i = 0; i < 64; i++)
        block[i] ^= 0x36;
    sha256_init(&ctx);
    sha256_update(&ctx, block, 64);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, inner);

    for (int i = 0; i < 64; i++)
        block[i] ^= 0x36 ^ 0x5C;
    sha256_init(&ctx);
    sha256_update(&ctx, block, 64);
    sha256_update(&ctx, inner, 32);
    sha256_final(&ctx, out);
}

void hkdf_extract(const uint8_t *salt, size_t salt_len, const uint8_t *ikm,
                  size_t ikm_len, uint8_t out[32])
{
    hmac_sha256(salt, salt_len, ikm, ikm_len, out);
}

void hkdf_expand(const uint8_t prk[32], const uint8_t *info, size_t info_len,
                 uint8_t *out, size_t out_len)
{
    /* Ein einziger Block genügt für bis zu 32 Bytes: T(1) = HMAC(info || 1) */
    uint8_t message[160];
    uint8_t block[32];
    if (info_len > sizeof(message) - 1)
        info_len = sizeof(message) - 1;
    memcpy(message, info, info_len);
    message[info_len] = 1;
    hmac_sha256(prk, 32, message, info_len + 1, block);
    memcpy(out, block, out_len < 32 ? out_len : 32);
}

/* --- Zufallszahlen ------------------------------------------------------------------ */

/*
 * Ein Vorrat (Hashwert), in den alles Unvorhersehbare gemischt wird, das der
 * Kernel zu fassen bekommt: Zeitstempel, Eingaben, der Zufallsgenerator des
 * Prozessors (falls vorhanden). Ausgaben entstehen durch erneutes Hashen.
 */
static uint8_t pool[32];
static uint64_t counter;

static uint64_t timestamp(void)
{
#if defined(__x86_64__)
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
#else
    return 0;
#endif
}

/* RDRAND, sofern der Prozessor es anbietet (CPUID.1:ECX Bit 30). */
static bool hardware_random(uint64_t *out)
{
#if defined(__x86_64__)
    uint32_t eax = 1, ebx, ecx = 0, edx;
    __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "+c"(ecx), "=d"(edx));
    if (!(ecx & (1u << 30)))
        return false;
    for (int i = 0; i < 10; i++) {
        uint8_t ok;
        /* rdrand %rax; setc ok */
        __asm__ volatile(".byte 0x48, 0x0F, 0xC7, 0xF0; setc %1"
                         : "=a"(*out), "=qm"(ok) : : "cc");
        if (ok)
            return true;
    }
#else
    (void)out;
#endif
    return false;
}

void random_stir(const void *data, size_t len)
{
    sha256_t ctx;
    uint64_t now = timestamp();
    sha256_init(&ctx);
    sha256_update(&ctx, pool, sizeof(pool));
    sha256_update(&ctx, &now, sizeof(now));
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, pool);
}

void random_bytes(void *out, size_t len)
{
    uint8_t *p = out;
    while (len > 0) {
        uint64_t extra[3] = { timestamp(), counter++, 0 };
        hardware_random(&extra[2]);
        random_stir(extra, sizeof(extra));

        /* Ausgabe = Hash(Vorrat, "out"); der Vorrat selbst bleibt geheim. */
        uint8_t block[32];
        sha256_t ctx;
        sha256_init(&ctx);
        sha256_update(&ctx, pool, sizeof(pool));
        sha256_update(&ctx, "out", 3);
        sha256_final(&ctx, block);

        size_t n = len < 32 ? len : 32;
        memcpy(p, block, n);
        p += n;
        len -= n;
    }
}
