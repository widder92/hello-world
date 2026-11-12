/*
 * JPEG-Bilder: das übliche Verfahren ("Baseline") und das schrittweise
 * aufgebaute ("progressiv"), in Farbe oder Grau, mit allen gängigen
 * Unterabtastungen der Farbanteile.
 *
 * Ablauf: Kopfabschnitte lesen -> in jedem Durchgang ("Scan") die
 * Koeffizienten der 8x8-Blöcke sammeln -> am Ende jeden Block zurück in
 * Bildpunkte rechnen (IDCT) und die Farbanteile zu RGB zusammensetzen.
 * Es wird nur mit ganzen Zahlen gerechnet.
 */
#include "image.h"
#include "memory.h"
#include "string.h"

/* Reihenfolge, in der die 64 Koeffizienten eines Blocks gespeichert sind */
static const uint8_t zigzag[64] = {
    0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
};

#define LOOK_BITS 9

typedef struct {
    bool present;
    uint8_t values[256];
    int32_t maxcode[18];                /* größter Code je Länge, -1: keiner */
    int32_t offset[17];                 /* Index des ersten Werts minus erstem Code */
    uint8_t look_size[1 << LOOK_BITS];  /* Schnellzugriff für kurze Codes */
    uint8_t look_value[1 << LOOK_BITS];
} huffman_t;

typedef struct {
    int id, h, v, quant;
    int dc_table, ac_table;
    int blocks_w, blocks_h;             /* Blöcke, die das Bild wirklich bedecken */
    int stride, rows;                   /* gespeicherte Blöcke je Zeile / Zeilen */
    int16_t *coefs;
    int pred;                           /* Vorhersage für den Gleichanteil */
    uint8_t *plane;                     /* fertige Bildpunkte, stride*8 breit */
} component_t;

typedef struct {
    const uint8_t *data;
    size_t length, pos;
    uint32_t bit_buffer;
    int bit_count;
    bool marker_hit;                    /* im Datenstrom kam ein Abschnittsanfang */
    bool failed;

    int width, height, ncomp;
    bool progressive, have_frame;
    int hmax, vmax, mcus_w, mcus_h;
    uint16_t quant[4][64];
    huffman_t *dc, *ac;                 /* je 4 Tabellen */
    component_t comp[3];
    int restart_interval;
    int adobe_transform;                /* -1: keine Angabe */

    /* aktueller Durchgang */
    int scan_count;
    component_t *scan[3];
    int ss, se, ah, al;
    int eobrun;
} jpeg_t;

/* --- Bits lesen ------------------------------------------------------------------ */

static void fill(jpeg_t *j)
{
    while (j->bit_count <= 24) {
        uint32_t byte = 0;
        if (!j->marker_hit && j->pos < j->length) {
            byte = j->data[j->pos];
            if (byte == 0xFF) {
                uint8_t next = j->pos + 1 < j->length ? j->data[j->pos + 1] : 0xD9;
                if (next == 0x00) {
                    j->pos += 2;        /* FF 00 steht für ein echtes FF */
                } else {
                    j->marker_hit = true;   /* Abschnittsanfang: ab hier Nullen liefern */
                    byte = 0;
                }
            } else {
                j->pos++;
            }
        }
        j->bit_buffer |= byte << (24 - j->bit_count);
        j->bit_count += 8;
    }
}

static int get_bits(jpeg_t *j, int n)
{
    if (n == 0)
        return 0;
    if (j->bit_count < n)
        fill(j);
    int value = (int)(j->bit_buffer >> (32 - n));
    j->bit_buffer <<= n;
    j->bit_count -= n;
    return value;
}

/* Deutet t Bits als Zahl mit Vorzeichen, wie JPEG sie ablegt. */
static int extend(int value, int t)
{
    return value < (1 << (t - 1)) ? value - (1 << t) + 1 : value;
}

/* --- Huffman-Tabellen -------------------------------------------------------------- */

static bool huffman_build(huffman_t *h, const uint8_t *counts, const uint8_t *values,
                          int total)
{
    memset(h, 0, sizeof(*h));
    memcpy(h->values, values, (size_t)total);

    int code = 0, index = 0;
    for (int len = 1; len <= 16; len++) {
        int n = counts[len - 1];
        h->offset[len] = index - code;
        if (n) {
            if (code + n > (1 << len))
                return false;           /* mehr Codes als diese Länge hergibt */
            /* kurze Codes zusätzlich in die Schnellzugriffstabelle */
            if (len <= LOOK_BITS) {
                for (int i = 0; i < n; i++) {
                    int first = (code + i) << (LOOK_BITS - len);
                    for (int k = 0; k < (1 << (LOOK_BITS - len)); k++) {
                        h->look_size[first + k] = (uint8_t)len;
                        h->look_value[first + k] = values[index + i];
                    }
                }
            }
            index += n;
            code += n;
            h->maxcode[len] = code - 1;
        } else {
            h->maxcode[len] = -1;
        }
        code <<= 1;
    }
    h->maxcode[17] = 0x7FFFFFFF;
    h->present = true;
    return true;
}

static int huffman_decode(jpeg_t *j, const huffman_t *h)
{
    if (j->bit_count < 16)
        fill(j);
    unsigned look = j->bit_buffer >> (32 - LOOK_BITS);
    int size = h->look_size[look];
    if (size) {
        j->bit_buffer <<= size;
        j->bit_count -= size;
        return h->look_value[look];
    }

    /* langer Code: Bit für Bit weitersuchen */
    int code = (int)(j->bit_buffer >> (32 - LOOK_BITS));
    int len = LOOK_BITS;
    j->bit_buffer <<= LOOK_BITS;
    j->bit_count -= LOOK_BITS;
    for (;;) {
        if (len > 16) {
            j->failed = true;
            return 0;
        }
        if (h->maxcode[len] >= 0 && code <= h->maxcode[len])
            break;
        code = (code << 1) | get_bits(j, 1);
        len++;
    }
    int index = code + h->offset[len];
    if (index < 0 || index > 255) {
        j->failed = true;
        return 0;
    }
    return h->values[index];
}

/* --- Blöcke entschlüsseln ----------------------------------------------------------- */

/* Ein ganzer Block im üblichen Verfahren */
static void block_baseline(jpeg_t *j, component_t *c, int16_t *block)
{
    int t = huffman_decode(j, &j->dc[c->dc_table]);
    if (t > 15) {
        j->failed = true;
        return;
    }
    int diff = t ? extend(get_bits(j, t), t) : 0;
    c->pred = (int16_t)(c->pred + diff);   /* bleibt auch bei kaputten Daten im Rahmen */
    block[0] = (int16_t)c->pred;

    const huffman_t *ac = &j->ac[c->ac_table];
    for (int k = 1; k < 64;) {
        int rs = huffman_decode(j, ac);
        int run = rs >> 4, size = rs & 15;
        if (size == 0) {
            if (run != 15)
                break;                  /* Rest des Blocks ist null */
            k += 16;
            continue;
        }
        k += run;
        if (k > 63) {
            j->failed = true;
            return;
        }
        block[zigzag[k++]] = (int16_t)extend(get_bits(j, size), size);
    }
}

/* Progressiv: erster Durchgang für den Gleichanteil */
static void block_dc_first(jpeg_t *j, component_t *c, int16_t *block)
{
    int t = huffman_decode(j, &j->dc[c->dc_table]);
    if (t > 15) {
        j->failed = true;
        return;
    }
    int diff = t ? extend(get_bits(j, t), t) : 0;
    c->pred = (int16_t)(c->pred + diff);   /* bleibt auch bei kaputten Daten im Rahmen */
    block[0] = (int16_t)(c->pred * (1 << j->al));
}

/* Progressiv: ein weiteres Bit für den Gleichanteil */
static void block_dc_refine(jpeg_t *j, component_t *c, int16_t *block)
{
    (void)c;
    if (get_bits(j, 1))
        block[0] = (int16_t)(block[0] | (1 << j->al));
}

/* Progressiv: erster Durchgang für einen Bereich der übrigen Koeffizienten */
static void block_ac_first(jpeg_t *j, component_t *c, int16_t *block)
{
    if (j->eobrun > 0) {
        j->eobrun--;                    /* dieser Block bleibt in dem Bereich leer */
        return;
    }
    const huffman_t *ac = &j->ac[c->ac_table];
    for (int k = j->ss; k <= j->se;) {
        int rs = huffman_decode(j, ac);
        int run = rs >> 4, size = rs & 15;
        if (size == 0) {
            if (run < 15) {             /* so viele Blöcke enden hier */
                j->eobrun = (1 << run) - 1;
                if (run)
                    j->eobrun += get_bits(j, run);
                break;
            }
            k += 16;
        } else {
            k += run;
            if (k > 63) {
                j->failed = true;
                return;
            }
            block[zigzag[k++]] = (int16_t)(extend(get_bits(j, size), size) * (1 << j->al));
        }
    }
}

/* Ein schon vorhandener Koeffizient bekommt ein weiteres Bit. */
static void refine_bit(jpeg_t *j, int16_t *coef, int plus, int minus)
{
    if (get_bits(j, 1) && (*coef & plus) == 0)
        *coef = (int16_t)(*coef + (*coef >= 0 ? plus : minus));
}

/* Progressiv: Verfeinerung der übrigen Koeffizienten */
static void block_ac_refine(jpeg_t *j, component_t *c, int16_t *block)
{
    int plus = 1 << j->al, minus = -(1 << j->al);
    const huffman_t *ac = &j->ac[c->ac_table];
    int k = j->ss;

    if (j->eobrun == 0) {
        for (; k <= j->se; k++) {
            int rs = huffman_decode(j, ac);
            int run = rs >> 4, size = rs & 15, value = 0;
            if (size) {                 /* ein neuer Koeffizient: +1 oder -1 an dieser Stelle */
                value = get_bits(j, 1) ? plus : minus;
            } else if (run != 15) {
                j->eobrun = 1 << run;
                if (run)
                    j->eobrun += get_bits(j, run);
                break;
            }
            /* run leere Plätze überspringen; vorhandene unterwegs verfeinern */
            while (k <= j->se) {
                int16_t *coef = &block[zigzag[k]];
                if (*coef != 0)
                    refine_bit(j, coef, plus, minus);
                else if (--run < 0)
                    break;
                k++;
            }
            if (value && k <= j->se)
                block[zigzag[k]] = (int16_t)value;
            if (j->failed)
                return;
        }
    }
    if (j->eobrun > 0) {
        for (; k <= j->se; k++) {
            int16_t *coef = &block[zigzag[k]];
            if (*coef != 0)
                refine_bit(j, coef, plus, minus);
        }
        j->eobrun--;
    }
}

/* --- Ein Durchgang -------------------------------------------------------------------- */

/* An einer Neustartmarke beginnt die Vorhersage von vorn. Liefert false am Ende. */
static bool restart(jpeg_t *j)
{
    j->bit_buffer = 0;
    j->bit_count = 0;
    j->marker_hit = false;
    while (j->pos + 1 < j->length) {
        if (j->data[j->pos] != 0xFF) {
            j->pos++;
            continue;
        }
        uint8_t marker = j->data[j->pos + 1];
        if (marker == 0xFF || marker == 0x00) {
            j->pos++;
            continue;
        }
        if (marker < 0xD0 || marker > 0xD7)
            return false;               /* etwas anderes: der Durchgang ist zu Ende */
        j->pos += 2;
        for (int i = 0; i < j->scan_count; i++)
            j->scan[i]->pred = 0;
        j->eobrun = 0;
        return true;
    }
    return false;
}

static void scan_decode(jpeg_t *j)
{
    void (*decode)(jpeg_t *, component_t *, int16_t *);
    if (!j->progressive)
        decode = block_baseline;
    else if (j->ss == 0)
        decode = j->ah == 0 ? block_dc_first : block_dc_refine;
    else
        decode = j->ah == 0 ? block_ac_first : block_ac_refine;

    j->bit_buffer = 0;
    j->bit_count = 0;
    j->marker_hit = false;
    j->eobrun = 0;
    for (int i = 0; i < j->scan_count; i++)
        j->scan[i]->pred = 0;

    int units_w, units_h;
    component_t *single = j->scan_count == 1 ? j->scan[0] : NULL;
    if (single) {                       /* nur ein Farbanteil: Block für Block */
        units_w = single->blocks_w;
        units_h = single->blocks_h;
    } else {                            /* mehrere: in Einheiten aus mehreren Blöcken */
        units_w = j->mcus_w;
        units_h = j->mcus_h;
    }

    int until_restart = j->restart_interval;
    for (int uy = 0; uy < units_h; uy++) {
        for (int ux = 0; ux < units_w; ux++) {
            if (single) {
                decode(j, single, single->coefs + ((size_t)uy * single->stride + ux) * 64);
            } else {
                for (int i = 0; i < j->scan_count; i++) {
                    component_t *c = j->scan[i];
                    for (int by = 0; by < c->v; by++) {
                        for (int bx = 0; bx < c->h; bx++) {
                            size_t row = (size_t)uy * c->v + by, col = (size_t)ux * c->h + bx;
                            decode(j, c, c->coefs + (row * c->stride + col) * 64);
                        }
                    }
                }
            }
            if (j->failed)
                return;
            bool last = uy == units_h - 1 && ux == units_w - 1;
            if (j->restart_interval && --until_restart == 0 && !last) {
                if (!restart(j))
                    return;             /* Daten enden vorzeitig: bis hierher verwenden */
                until_restart = j->restart_interval;
            }
        }
    }
}

/* --- Zurück zu Bildpunkten -------------------------------------------------------------- */

#define CONST_BITS 13
#define PASS1_BITS 2
#define DESCALE(x, n) (((x) + (1 << ((n) - 1))) >> (n))

static uint8_t clamp(int value)
{
    return (uint8_t)(value < 0 ? 0 : value > 255 ? 255 : value);
}

static uint8_t clamp64(int64_t value)
{
    return (uint8_t)(value < 0 ? 0 : value > 255 ? 255 : value);
}

/* Inverse Kosinustransformation eines Blocks; schreibt 8x8 Bildpunkte. */
static void idct(const int16_t *coefs, const uint16_t *quant, uint8_t *out, int stride)
{
    int64_t work[64];

    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < 8; i++) {
            int64_t in[8];
            if (pass == 0) {            /* Spalten, dabei mit der Quantisierung malnehmen */
                for (int k = 0; k < 8; k++)
                    in[k] = (int64_t)coefs[k * 8 + i] * quant[k * 8 + i];
            } else {                    /* Zeilen */
                for (int k = 0; k < 8; k++)
                    in[k] = work[i * 8 + k];
            }

            /* gerader Teil */
            int64_t z2 = in[2], z3 = in[6];
            int64_t z1 = (z2 + z3) * 4433;
            int64_t tmp2 = z1 - z3 * 15137;
            int64_t tmp3 = z1 + z2 * 6270;
            int64_t tmp0 = (in[0] + in[4]) * (1 << CONST_BITS);
            int64_t tmp1 = (in[0] - in[4]) * (1 << CONST_BITS);
            int64_t tmp10 = tmp0 + tmp3, tmp13 = tmp0 - tmp3;
            int64_t tmp11 = tmp1 + tmp2, tmp12 = tmp1 - tmp2;

            /* ungerader Teil */
            tmp0 = in[7];
            tmp1 = in[5];
            tmp2 = in[3];
            tmp3 = in[1];
            z1 = tmp0 + tmp3;
            z2 = tmp1 + tmp2;
            z3 = tmp0 + tmp2;
            int64_t z4 = tmp1 + tmp3;
            int64_t z5 = (z3 + z4) * 9633;
            tmp0 *= 2446;
            tmp1 *= 16819;
            tmp2 *= 25172;
            tmp3 *= 12299;
            z1 *= -7373;
            z2 *= -20995;
            z3 = z3 * -16069 + z5;
            z4 = z4 * -3196 + z5;
            tmp0 += z1 + z3;
            tmp1 += z2 + z4;
            tmp2 += z2 + z3;
            tmp3 += z1 + z4;

            int64_t result[8] = {
                tmp10 + tmp3, tmp11 + tmp2, tmp12 + tmp1, tmp13 + tmp0,
                tmp13 - tmp0, tmp12 - tmp1, tmp11 - tmp2, tmp10 - tmp3,
            };
            if (pass == 0) {
                for (int k = 0; k < 8; k++)
                    work[k * 8 + i] = DESCALE(result[k], CONST_BITS - PASS1_BITS);
            } else {
                for (int k = 0; k < 8; k++)
                    out[i * stride + k] =
                        clamp64(DESCALE(result[k], CONST_BITS + PASS1_BITS + 3) + 128);
            }
        }
    }
}

static bool render(jpeg_t *j)
{
    for (int n = 0; n < j->ncomp; n++) {
        component_t *c = &j->comp[n];
        size_t stride = (size_t)c->stride * 8;
        c->plane = kmalloc(stride * (size_t)c->rows * 8);
        if (!c->plane)
            return false;
        for (int by = 0; by < c->rows; by++) {
            for (int bx = 0; bx < c->stride; bx++)
                idct(c->coefs + ((size_t)by * c->stride + bx) * 64, j->quant[c->quant],
                     c->plane + (size_t)by * 8 * stride + (size_t)bx * 8, (int)stride);
        }
        kfree(c->coefs);                /* gleich freigeben: große Bilder brauchen den Platz */
        c->coefs = NULL;
    }
    return true;
}

/*
 * Liefert Zeile y eines Farbanteils in voller Bildbreite. Anteile, die nur
 * mit halber Auflösung gespeichert sind, werden dabei geglättet vergrößert
 * (Gewichte 3:1 zwischen den Nachbarn) - sonst zeigten Farbkanten Treppen.
 */
static void component_row(const jpeg_t *j, const component_t *c, int y, uint8_t *out,
                          int *sums)
{
    size_t stride = (size_t)c->stride * 8;
    int comp_w = (j->width * c->h + j->hmax - 1) / j->hmax;
    int comp_h = (j->height * c->v + j->vmax - 1) / j->vmax;
    bool exact = j->hmax % c->h == 0 && j->vmax % c->v == 0;
    int hs = j->hmax / c->h, vs = j->vmax / c->v;

    if (!exact || hs > 2 || vs > 2) {   /* seltene Verhältnisse: nächster Nachbar */
        const uint8_t *in = c->plane + (size_t)(y * c->v / j->vmax) * stride;
        for (int x = 0; x < j->width; x++)
            out[x] = in[x * c->h / j->hmax];
        return;
    }

    /* senkrecht: die nähere Quellzeile dreifach, die fernere einfach */
    int row = y / vs;
    const uint8_t *near = c->plane + (size_t)row * stride;
    if (vs == 2) {
        int other = (y & 1) ? row + 1 : row - 1;
        if (other < 0)
            other = 0;
        if (other > comp_h - 1)
            other = comp_h - 1;
        const uint8_t *far = c->plane + (size_t)other * stride;
        for (int i = 0; i < comp_w; i++)
            sums[i] = 3 * near[i] + far[i];
    } else {
        for (int i = 0; i < comp_w; i++)
            sums[i] = near[i];
    }

    if (hs == 1) {
        if (vs == 1) {
            memcpy(out, near, (size_t)j->width);
        } else {
            int bias = (y & 1) ? 2 : 1;
            for (int x = 0; x < j->width; x++)
                out[x] = (uint8_t)((sums[x] + bias) >> 2);
        }
        return;
    }

    /* waagerecht ebenso; die Rundung wechselt, damit nichts driftet */
    int shift = vs == 2 ? 4 : 2;
    int bias_even = vs == 2 ? 8 : 1, bias_odd = vs == 2 ? 7 : 2;
    for (int i = 0; i < comp_w; i++) {
        int left = i > 0 ? sums[i - 1] : sums[i];
        int right = i < comp_w - 1 ? sums[i + 1] : sums[i];
        int even, odd;
        if (vs == 1 && i == 0)
            even = sums[i];
        else
            even = (3 * sums[i] + left + bias_even) >> shift;
        if (vs == 1 && i == comp_w - 1)
            odd = sums[i];
        else
            odd = (3 * sums[i] + right + bias_odd) >> shift;
        if (2 * i < j->width)
            out[2 * i] = (uint8_t)even;
        if (2 * i + 1 < j->width)
            out[2 * i + 1] = (uint8_t)odd;
    }
}

/* Setzt die Farbanteile zu Bildpunkten zusammen. */
static image_t *compose(jpeg_t *j)
{
    image_t *image = image_create(j->width, j->height);
    uint8_t *lines = kmalloc(3 * ((size_t)j->width + 2));
    int *sums = kmalloc(((size_t)j->width + 2) * sizeof(int));
    if (!image || !lines || !sums) {
        image_free(image);
        kfree(lines);
        kfree(sums);
        return NULL;
    }

    bool rgb = j->ncomp == 3 &&
               (j->adobe_transform == 0 ||
                (j->adobe_transform < 0 && j->comp[0].id == 'R' && j->comp[1].id == 'G' &&
                 j->comp[2].id == 'B'));

    for (int y = 0; y < j->height; y++) {
        uint8_t *rows[3];
        for (int n = 0; n < j->ncomp; n++) {
            rows[n] = lines + (size_t)n * ((size_t)j->width + 2);
            component_row(j, &j->comp[n], y, rows[n], sums);
        }
        uint32_t *out = image->pixels + (size_t)y * j->width;
        if (j->ncomp == 1) {
            for (int x = 0; x < j->width; x++) {
                uint32_t gray = rows[0][x];
                out[x] = 0xFF000000u | (gray << 16) | (gray << 8) | gray;
            }
            continue;
        }
        for (int x = 0; x < j->width; x++) {
            int a = rows[0][x], b = rows[1][x], c = rows[2][x];
            int red, green, blue;
            if (rgb) {
                red = a;
                green = b;
                blue = c;
            } else {                    /* Helligkeit + zwei Farbdifferenzen */
                int cb = b - 128, cr = c - 128;
                red = a + ((91881 * cr + 32768) >> 16);
                green = a - ((22554 * cb + 46802 * cr + 32768) >> 16);
                blue = a + ((116130 * cb + 32768) >> 16);
            }
            out[x] = 0xFF000000u | ((uint32_t)clamp(red) << 16) |
                     ((uint32_t)clamp(green) << 8) | clamp(blue);
        }
    }
    kfree(lines);
    kfree(sums);
    return image;
}

/* --- Kopfabschnitte --------------------------------------------------------------------- */

static bool read_frame(jpeg_t *j, const uint8_t *p, size_t size, const char **error)
{
    if (size < 6 || j->have_frame)
        return false;
    if (p[0] != 8) {
        *error = "JPEG mit mehr als 8 Bit je Farbe wird nicht unterstützt";
        return false;
    }
    j->height = (p[1] << 8) | p[2];
    j->width = (p[3] << 8) | p[4];
    j->ncomp = p[5];
    if (j->ncomp != 1 && j->ncomp != 3) {
        *error = "JPEG in Druckfarben (CMYK) wird nicht unterstützt";
        return false;
    }
    if (size < 6 + 3 * (size_t)j->ncomp || j->width == 0 || j->height == 0)
        return false;
    if (j->width > IMAGE_MAX_SIDE || j->height > IMAGE_MAX_SIDE ||
        (uint64_t)j->width * (uint64_t)j->height > IMAGE_MAX_PIXELS) {
        *error = "Bild ist zu groß";
        return false;
    }

    j->hmax = j->vmax = 1;
    for (int n = 0; n < j->ncomp; n++) {
        component_t *c = &j->comp[n];
        c->id = p[6 + 3 * n];
        c->h = p[7 + 3 * n] >> 4;
        c->v = p[7 + 3 * n] & 15;
        c->quant = p[8 + 3 * n];
        if (c->h < 1 || c->h > 4 || c->v < 1 || c->v > 4 || c->quant > 3)
            return false;
        if (c->h > j->hmax)
            j->hmax = c->h;
        if (c->v > j->vmax)
            j->vmax = c->v;
    }
    if (j->ncomp == 1)                  /* ein einzelner Anteil zählt immer einfach */
        j->comp[0].h = j->comp[0].v = j->hmax = j->vmax = 1;

    j->mcus_w = (j->width + 8 * j->hmax - 1) / (8 * j->hmax);
    j->mcus_h = (j->height + 8 * j->vmax - 1) / (8 * j->vmax);
    for (int n = 0; n < j->ncomp; n++) {
        component_t *c = &j->comp[n];
        int w = (j->width * c->h + j->hmax - 1) / j->hmax;
        int h = (j->height * c->v + j->vmax - 1) / j->vmax;
        c->blocks_w = (w + 7) / 8;
        c->blocks_h = (h + 7) / 8;
        c->stride = j->mcus_w * c->h;
        c->rows = j->mcus_h * c->v;
        c->coefs = kzalloc((size_t)c->stride * (size_t)c->rows * 64 * sizeof(int16_t));
        if (!c->coefs) {
            *error = "zu wenig Speicher";
            return false;
        }
    }
    j->have_frame = true;
    return true;
}

static bool read_tables(jpeg_t *j, uint8_t marker, const uint8_t *p, size_t size)
{
    if (marker == 0xDB) {               /* Quantisierung */
        while (size >= 1) {
            int precision = p[0] >> 4, id = p[0] & 15;
            size_t need = 1 + (precision ? 128 : 64);
            if (id > 3 || precision > 1 || size < need)
                return false;
            for (int i = 0; i < 64; i++) {
                uint16_t value = precision ? (uint16_t)((p[1 + 2 * i] << 8) | p[2 + 2 * i])
                                           : p[1 + i];
                j->quant[id][zigzag[i]] = value;
            }
            p += need;
            size -= need;
        }
    } else if (marker == 0xC4) {        /* Huffman-Tabellen */
        while (size >= 17) {
            int kind = p[0] >> 4, id = p[0] & 15;
            int total = 0;
            for (int i = 0; i < 16; i++)
                total += p[1 + i];
            if (kind > 1 || id > 3 || total > 256 || size < 17 + (size_t)total)
                return false;
            if (!huffman_build(kind ? &j->ac[id] : &j->dc[id], p + 1, p + 17, total))
                return false;
            p += 17 + total;
            size -= 17 + (size_t)total;
        }
    } else if (marker == 0xDD && size >= 2) {
        j->restart_interval = (p[0] << 8) | p[1];
    } else if (marker == 0xEE && size >= 12 && memcmp(p, "Adobe", 5) == 0) {
        j->adobe_transform = p[11];
    }
    return true;
}

static bool read_scan(jpeg_t *j, const uint8_t *p, size_t size)
{
    if (!j->have_frame || size < 1)
        return false;
    j->scan_count = p[0];
    if (j->scan_count < 1 || j->scan_count > j->ncomp || size < 4 + 2 * (size_t)j->scan_count)
        return false;
    for (int i = 0; i < j->scan_count; i++) {
        component_t *found = NULL;
        for (int n = 0; n < j->ncomp; n++) {
            if (j->comp[n].id == p[1 + 2 * i])
                found = &j->comp[n];
        }
        if (!found)
            return false;
        for (int k = 0; k < i; k++) {
            if (j->scan[k] == found)
                return false;
        }
        found->dc_table = p[2 + 2 * i] >> 4;
        found->ac_table = p[2 + 2 * i] & 15;
        if (found->dc_table > 3 || found->ac_table > 3)
            return false;
        j->scan[i] = found;
    }
    const uint8_t *tail = p + 1 + 2 * j->scan_count;
    j->ss = tail[0];
    j->se = tail[1];
    j->ah = tail[2] >> 4;
    j->al = tail[2] & 15;
    if (!j->progressive) {
        j->ss = 0;
        j->se = 63;
        j->ah = j->al = 0;
    } else if (j->ss > j->se || j->se > 63 || j->al > 13 ||
               (j->ss == 0 && j->se != 0) || (j->ss > 0 && j->scan_count != 1)) {
        return false;
    }

    /* Die benötigten Tabellen müssen da sein */
    for (int i = 0; i < j->scan_count; i++) {
        bool need_dc = !j->progressive || (j->ss == 0 && j->ah == 0);
        bool need_ac = !j->progressive || j->ss > 0;
        if ((need_dc && !j->dc[j->scan[i]->dc_table].present) ||
            (need_ac && !j->ac[j->scan[i]->ac_table].present))
            return false;
    }
    return true;
}

image_t *jpeg_decode(const uint8_t *data, size_t length, const char **error)
{
    *error = "JPEG-Datei ist beschädigt";
    if (length < 4 || data[0] != 0xFF || data[1] != 0xD8)
        return NULL;

    jpeg_t *j = kzalloc(sizeof(*j));
    huffman_t *tables = kzalloc(8 * sizeof(huffman_t));
    image_t *image = NULL;
    int scans = 0;
    if (!j || !tables) {
        *error = "zu wenig Speicher";
        goto done;
    }
    j->data = data;
    j->length = length;
    j->pos = 2;
    j->dc = tables;
    j->ac = tables + 4;
    j->adobe_transform = -1;

    for (;;) {
        /* zum nächsten Abschnitt */
        while (j->pos < length && data[j->pos] != 0xFF)
            j->pos++;
        while (j->pos < length && data[j->pos] == 0xFF)
            j->pos++;
        if (j->pos >= length)
            break;                      /* abgeschnitten: zeigen, was da ist */
        uint8_t marker = data[j->pos++];
        if (marker == 0xD9)
            break;                      /* Ende des Bildes */
        if (marker == 0x00 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD8))
            continue;                   /* ohne Inhalt */
        if (j->pos + 2 > length)
            break;
        size_t size = ((size_t)data[j->pos] << 8) | data[j->pos + 1];
        if (size < 2 || size > length - j->pos)
            break;
        const uint8_t *body = data + j->pos + 2;
        size_t body_size = size - 2;
        j->pos += size;

        if (marker == 0xC0 || marker == 0xC1 || marker == 0xC2) {
            j->progressive = marker == 0xC2;
            if (!read_frame(j, body, body_size, error))
                goto done;
        } else if (marker == 0xC3 || (marker >= 0xC5 && marker <= 0xCF && marker != 0xC8 &&
                                      marker != 0xCC)) {
            *error = "diese JPEG-Variante wird nicht unterstützt";
            goto done;
        } else if (marker == 0xDA) {
            if (!read_scan(j, body, body_size))
                goto done;
            scan_decode(j);
            scans++;
            if (j->failed)
                break;                  /* beschädigt: zeigen, was bis dahin entstand */
        } else if (!read_tables(j, marker, body, body_size)) {
            goto done;
        }
    }

    if (!j->have_frame || scans == 0)
        goto done;
    if (!render(j)) {
        *error = "zu wenig Speicher";
        goto done;
    }
    image = compose(j);
    *error = image ? NULL : "zu wenig Speicher";

done:
    if (j) {
        for (int n = 0; n < 3; n++) {
            kfree(j->comp[n].coefs);
            kfree(j->comp[n].plane);
        }
    }
    kfree(tables);
    kfree(j);
    return image;
}
