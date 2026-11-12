/*
 * Entpacken von "Deflate"-Daten (zlib-Verpackung), wie sie in PNG-Bildern
 * stecken. Geradlinig statt schnell: Die Codes werden Bit für Bit gelesen.
 */
#include "image.h"

#define MAX_BITS    15
#define MAX_LCODES  288
#define MAX_DCODES  32

typedef struct {
    const uint8_t *in;
    size_t in_size, in_pos;
    uint32_t bit_buffer;
    int bit_count;
    uint8_t *out;
    size_t out_size, out_pos;
    bool failed;
} stream_t;

/* Ein Huffman-Code: wie viele Codes es je Länge gibt, und die Zeichen dazu */
typedef struct {
    uint16_t count[MAX_BITS + 1];
    uint16_t symbol[MAX_LCODES];
} huffman_t;

static uint32_t bits(stream_t *s, int need)
{
    while (s->bit_count < need) {
        if (s->in_pos >= s->in_size) {
            s->failed = true;
            return 0;
        }
        s->bit_buffer |= (uint32_t)s->in[s->in_pos++] << s->bit_count;
        s->bit_count += 8;
    }
    uint32_t value = s->bit_buffer & ((1u << need) - 1);
    s->bit_buffer >>= need;
    s->bit_count -= need;
    return value;
}

/* Baut den Code aus den Längen; false bei einer unmöglichen Längenverteilung. */
static bool build(huffman_t *h, const uint8_t *lengths, int n)
{
    uint16_t offsets[MAX_BITS + 1];
    for (int len = 0; len <= MAX_BITS; len++)
        h->count[len] = 0;
    for (int i = 0; i < n; i++)
        h->count[lengths[i]]++;
    h->count[0] = 0;

    int left = 1;                       /* freie Codes der aktuellen Länge */
    for (int len = 1; len <= MAX_BITS; len++) {
        left = (left << 1) - h->count[len];
        if (left < 0)
            return false;               /* mehr Codes als möglich */
    }
    offsets[1] = 0;
    for (int len = 1; len < MAX_BITS; len++)
        offsets[len + 1] = (uint16_t)(offsets[len] + h->count[len]);
    for (int i = 0; i < n; i++) {
        if (lengths[i])
            h->symbol[offsets[lengths[i]]++] = (uint16_t)i;
    }
    return true;
}

static int decode(stream_t *s, const huffman_t *h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= MAX_BITS; len++) {
        code |= (int)bits(s, 1);
        if (s->failed)
            return -1;
        int count = h->count[len];
        if (code - count < first)
            return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    s->failed = true;                   /* kein gültiger Code */
    return -1;
}

static const uint16_t length_base[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258,
};
static const uint8_t length_extra[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0,
};
static const uint16_t distance_base[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
    8193, 12289, 16385, 24577,
};
static const uint8_t distance_extra[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13,
};

/* Ein Block aus Zeichen und Rückverweisen */
static void codes(stream_t *s, const huffman_t *lencode, const huffman_t *distcode)
{
    for (;;) {
        int symbol = decode(s, lencode);
        if (s->failed)
            return;
        if (symbol < 256) {
            if (s->out_pos >= s->out_size) {
                s->failed = true;
                return;
            }
            s->out[s->out_pos++] = (uint8_t)symbol;
        } else if (symbol == 256) {
            return;                     /* Ende des Blocks */
        } else {
            symbol -= 257;
            if (symbol >= 29) {
                s->failed = true;
                return;
            }
            size_t len = length_base[symbol] + bits(s, length_extra[symbol]);
            int dsym = decode(s, distcode);
            if (s->failed || dsym >= 30) {
                s->failed = true;
                return;
            }
            size_t dist = distance_base[dsym] + bits(s, distance_extra[dsym]);
            if (s->failed || dist > s->out_pos || len > s->out_size - s->out_pos) {
                s->failed = true;
                return;
            }
            /* Byte für Byte: Quelle und Ziel dürfen sich überlappen */
            for (size_t i = 0; i < len; i++, s->out_pos++)
                s->out[s->out_pos] = s->out[s->out_pos - dist];
        }
    }
}

static void stored(stream_t *s)
{
    s->bit_buffer = 0;                  /* bis zur Bytegrenze verwerfen */
    s->bit_count = 0;
    if (s->in_size - s->in_pos < 4) {
        s->failed = true;
        return;
    }
    size_t len = s->in[s->in_pos] | ((size_t)s->in[s->in_pos + 1] << 8);
    size_t check = s->in[s->in_pos + 2] | ((size_t)s->in[s->in_pos + 3] << 8);
    s->in_pos += 4;
    if (len != (~check & 0xFFFF) || len > s->in_size - s->in_pos ||
        len > s->out_size - s->out_pos) {
        s->failed = true;
        return;
    }
    for (size_t i = 0; i < len; i++)
        s->out[s->out_pos++] = s->in[s->in_pos++];
}

static void fixed(stream_t *s)
{
    /* Jedes Mal neu gebaut: klein genug, und so stört kein zweiter Task. */
    huffman_t lencode, distcode;
    uint8_t lengths[MAX_LCODES];
    int i = 0;
    for (; i < 144; i++) lengths[i] = 8;
    for (; i < 256; i++) lengths[i] = 9;
    for (; i < 280; i++) lengths[i] = 7;
    for (; i < 288; i++) lengths[i] = 8;
    build(&lencode, lengths, 288);
    for (i = 0; i < 30; i++) lengths[i] = 5;
    build(&distcode, lengths, 30);
    codes(s, &lencode, &distcode);
}

static void dynamic(stream_t *s)
{
    static const uint8_t order[19] = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15,
    };
    uint8_t lengths[MAX_LCODES + MAX_DCODES];
    huffman_t lencode, distcode;

    int nlen = (int)bits(s, 5) + 257;
    int ndist = (int)bits(s, 5) + 1;
    int ncode = (int)bits(s, 4) + 4;
    if (s->failed || nlen > 286 || ndist > 30) {
        s->failed = true;
        return;
    }
    for (int i = 0; i < 19; i++)
        lengths[i] = 0;
    for (int i = 0; i < ncode; i++)
        lengths[order[i]] = (uint8_t)bits(s, 3);
    if (s->failed || !build(&lencode, lengths, 19)) {
        s->failed = true;
        return;
    }

    /* Die Längen der eigentlichen Codes sind selbst wieder codiert */
    int index = 0;
    while (index < nlen + ndist) {
        int symbol = decode(s, &lencode);
        if (s->failed)
            return;
        if (symbol < 16) {
            lengths[index++] = (uint8_t)symbol;
            continue;
        }
        int previous = 0, repeat;
        if (symbol == 16) {
            if (index == 0) {
                s->failed = true;
                return;
            }
            previous = lengths[index - 1];
            repeat = 3 + (int)bits(s, 2);
        } else if (symbol == 17) {
            repeat = 3 + (int)bits(s, 3);
        } else {
            repeat = 11 + (int)bits(s, 7);
        }
        if (s->failed || index + repeat > nlen + ndist) {
            s->failed = true;
            return;
        }
        while (repeat--)
            lengths[index++] = (uint8_t)previous;
    }
    if (lengths[256] == 0 || !build(&lencode, lengths, nlen) ||
        !build(&distcode, lengths + nlen, ndist)) {
        s->failed = true;
        return;
    }
    codes(s, &lencode, &distcode);
}

bool inflate_zlib(const uint8_t *in, size_t in_size, uint8_t *out, size_t out_size)
{
    /* zlib-Kopf: Verfahren 8 (Deflate), kein vorgegebenes Wörterbuch */
    if (in_size < 2 || (in[0] & 0x0F) != 8 || (in[1] & 0x20) ||
        ((in[0] << 8) | in[1]) % 31 != 0)
        return false;

    stream_t s = { .in = in + 2, .in_size = in_size - 2, .out = out, .out_size = out_size };
    for (;;) {
        bool last = bits(&s, 1);
        uint32_t type = bits(&s, 2);
        if (s.failed)
            return false;
        if (type == 0)
            stored(&s);
        else if (type == 1)
            fixed(&s);
        else if (type == 2)
            dynamic(&s);
        else
            return false;
        if (s.failed)
            return false;
        if (last)
            break;
    }
    return s.out_pos == out_size;
}
