/*
 * AES im Zählermodus (CTR), wie ihn SSH verwendet: Die Blockchiffre
 * verschlüsselt einen hochzählenden Zähler, das Ergebnis wird als
 * Schlüsselstrom mit den Daten verknüpft. Gebraucht wird deshalb nur die
 * Verschlüsselungsrichtung von AES.
 *
 * Die S-Box wird über eine Tabelle nachgeschlagen - einfach, aber nicht
 * gegen Angriffe über Laufzeit oder Cache gehärtet.
 */
#include "crypto.h"
#include "string.h"

static uint8_t sbox[256];
static bool sbox_ready;

/* Multiplikation mit x im Körper GF(2^8) */
static uint8_t xtime(uint8_t a)
{
    return (uint8_t)((a << 1) ^ ((a & 0x80) ? 0x1B : 0));
}

/* Die S-Box ist "Kehrwert in GF(2^8), dann eine feste Bitmischung". */
static void sbox_init(void)
{
    uint8_t p = 1, q = 1;
    do {
        p = (uint8_t)(p ^ xtime(p));            /* p mal 3 */
        q ^= (uint8_t)(q << 1);                 /* q durch 3 */
        q ^= (uint8_t)(q << 2);
        q ^= (uint8_t)(q << 4);
        if (q & 0x80)
            q ^= 0x09;
        uint8_t x = (uint8_t)(q ^ ((q << 1) | (q >> 7)) ^ ((q << 2) | (q >> 6)) ^
                              ((q << 3) | (q >> 5)) ^ ((q << 4) | (q >> 4)));
        sbox[p] = (uint8_t)(x ^ 0x63);
    } while (p != 1);
    sbox[0] = 0x63;
    sbox_ready = true;
}

static void expand_key(aes_ctr_t *ctx, const uint8_t *key, size_t key_len)
{
    int words = (int)key_len / 4;
    uint8_t *w = &ctx->round_keys[0][0];
    uint8_t rcon = 1;

    ctx->rounds = words + 6;
    memcpy(w, key, key_len);
    for (int i = words; i < 4 * (ctx->rounds + 1); i++) {
        uint8_t t[4];
        memcpy(t, w + 4 * (i - 1), 4);
        if (i % words == 0) {
            uint8_t first = t[0];
            t[0] = (uint8_t)(sbox[t[1]] ^ rcon);
            t[1] = sbox[t[2]];
            t[2] = sbox[t[3]];
            t[3] = sbox[first];
            rcon = xtime(rcon);
        } else if (words > 6 && i % words == 4) {
            for (int k = 0; k < 4; k++)
                t[k] = sbox[t[k]];
        }
        for (int k = 0; k < 4; k++)
            w[4 * i + k] = (uint8_t)(w[4 * (i - words) + k] ^ t[k]);
    }
}

static void encrypt_block(const aes_ctr_t *ctx, const uint8_t in[16], uint8_t out[16])
{
    uint8_t s[16], t[16];

    for (int i = 0; i < 16; i++)
        s[i] = in[i] ^ ctx->round_keys[0][i];
    for (int round = 1; round <= ctx->rounds; round++) {
        /* SubBytes und ShiftRows in einem: Zeile r wandert um r Spalten */
        for (int c = 0; c < 4; c++) {
            for (int r = 0; r < 4; r++)
                t[4 * c + r] = sbox[s[4 * ((c + r) % 4) + r]];
        }
        if (round < ctx->rounds) {      /* MixColumns */
            for (int c = 0; c < 4; c++) {
                uint8_t *col = t + 4 * c;
                uint8_t all = (uint8_t)(col[0] ^ col[1] ^ col[2] ^ col[3]);
                uint8_t first = col[0];
                for (int r = 0; r < 4; r++) {
                    uint8_t next = r < 3 ? col[r + 1] : first;
                    col[r] ^= (uint8_t)(all ^ xtime((uint8_t)(col[r] ^ next)));
                }
            }
        }
        for (int i = 0; i < 16; i++)
            s[i] = t[i] ^ ctx->round_keys[round][i];
    }
    memcpy(out, s, 16);
}

bool aes_ctr_init(aes_ctr_t *ctx, const uint8_t *key, size_t key_len,
                  const uint8_t iv[16])
{
    if (key_len != 16 && key_len != 24 && key_len != 32)
        return false;
    if (!sbox_ready)
        sbox_init();
    expand_key(ctx, key, key_len);
    memcpy(ctx->counter, iv, 16);
    ctx->used = 16;
    return true;
}

void aes_ctr_crypt(aes_ctr_t *ctx, const uint8_t *in, uint8_t *out, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        if (ctx->used == 16) {
            encrypt_block(ctx, ctx->counter, ctx->stream);
            ctx->used = 0;
            /* Der Zähler ist eine 128-Bit-Zahl, höchstwertiges Byte zuerst. */
            for (int k = 15; k >= 0 && ++ctx->counter[k] == 0; k--)
                ;
        }
        out[i] = in[i] ^ ctx->stream[ctx->used++];
    }
}
