/*
 * PNG-Bilder: alle Farbarten und Bittiefen, mit Palette, Transparenz und
 * dem Zeilensprungverfahren "Adam7".
 */
#include "image.h"
#include "memory.h"
#include "string.h"

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

typedef struct {
    uint32_t width, height;
    int depth, color_type, interlace;
    int channels;                       /* Werte je Pixel in den Rohdaten */
    uint32_t palette[256];              /* fertig als 0xAARRGGBB */
    int palette_size;
    bool has_key;                       /* eine Farbe gilt als durchsichtig */
    uint16_t key[3];
} png_t;

/* Bytes einer Zeile mit width Pixeln (ohne das Filterbyte) */
static size_t row_bytes(const png_t *png, uint32_t width)
{
    return ((size_t)width * (size_t)(png->channels * png->depth) + 7) / 8;
}

/* Macht die Filterung einer Zeile rückgängig. */
static bool unfilter(uint8_t *row, const uint8_t *previous, size_t bytes,
                     size_t bpp, int filter)
{
    for (size_t i = 0; i < bytes; i++) {
        int left = i >= bpp ? row[i - bpp] : 0;
        int up = previous ? previous[i] : 0;
        int corner = (previous && i >= bpp) ? previous[i - bpp] : 0;
        int add;
        switch (filter) {
        case 0: add = 0; break;
        case 1: add = left; break;
        case 2: add = up; break;
        case 3: add = (left + up) / 2; break;
        case 4: {                       /* Paeth: der nächstliegende der drei Nachbarn */
            int p = left + up - corner;
            int pa = p > left ? p - left : left - p;
            int pb = p > up ? p - up : up - p;
            int pc = p > corner ? p - corner : corner - p;
            add = (pa <= pb && pa <= pc) ? left : (pb <= pc ? up : corner);
            break;
        }
        default:
            return false;
        }
        row[i] = (uint8_t)(row[i] + add);
    }
    return true;
}

/* Liest den Wert Nummer index einer Zeile bei 1, 2 oder 4 Bit je Wert. */
static unsigned packed(const uint8_t *row, uint32_t index, int depth)
{
    unsigned shift = 8 - (unsigned)depth - (index * (unsigned)depth) % 8;
    return (row[(index * (unsigned)depth) / 8] >> shift) & ((1u << depth) - 1);
}

/* Wandelt ein Pixel der Rohzeile in 0xAARRGGBB. */
static uint32_t pixel_at(const png_t *png, const uint8_t *row, uint32_t x)
{
    unsigned v[4] = { 0, 0, 0, 0xFFFF };
    int depth = png->depth;

    if (depth < 8) {                    /* nur Grau oder Palette */
        v[0] = packed(row, x, depth);
    } else {
        size_t step = (size_t)depth / 8;
        const uint8_t *p = row + (size_t)x * (size_t)png->channels * step;
        for (int c = 0; c < png->channels; c++, p += step)
            v[c] = depth == 16 ? (unsigned)((p[0] << 8) | p[1]) : p[0];
    }

    if (png->color_type == 3)
        return v[0] < (unsigned)png->palette_size ? png->palette[v[0]] : 0xFF000000u;

    /* Auf 8 Bit bringen */
    unsigned max = (1u << depth) - 1;
    unsigned r, g, b, a = 255;
    bool gray = png->color_type == 0 || png->color_type == 4;
    if (gray) {
        r = g = b = v[0] * 255 / max;
        if (png->color_type == 4)
            a = v[1] * 255 / max;
        else if (png->has_key && v[0] == png->key[0])
            a = 0;
    } else {
        r = v[0] * 255 / max;
        g = v[1] * 255 / max;
        b = v[2] * 255 / max;
        if (png->color_type == 6)
            a = v[3] * 255 / max;
        else if (png->has_key && v[0] == png->key[0] && v[1] == png->key[1] &&
                 v[2] == png->key[2])
            a = 0;
    }
    return (a << 24) | (r << 16) | (g << 8) | b;
}

image_t *png_decode(const uint8_t *data, size_t length, const char **error)
{
    static const uint8_t signature[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    png_t png;
    memset(&png, 0, sizeof(png));
    uint8_t *packed_data = NULL, *raw = NULL;
    size_t packed_size = 0;
    image_t *image = NULL;
    bool have_header = false;

    *error = "PNG-Datei ist beschädigt";
    if (length < 8 || memcmp(data, signature, 8) != 0)
        return NULL;

    /* Alle IDAT-Stücke zusammen ergeben die gepackten Bilddaten */
    packed_data = kmalloc(length);
    if (!packed_data) {
        *error = "zu wenig Speicher";
        return NULL;
    }

    size_t pos = 8;
    while (pos + 12 <= length) {
        uint32_t size = be32(data + pos);
        const uint8_t *type = data + pos + 4;
        const uint8_t *body = data + pos + 8;
        if (size > length - pos - 12)
            goto done;
        if (memcmp(type, "IHDR", 4) == 0 && size >= 13) {
            png.width = be32(body);
            png.height = be32(body + 4);
            png.depth = body[8];
            png.color_type = body[9];
            png.interlace = body[12];
            static const int channels[7] = { 1, 0, 3, 1, 2, 0, 4 };
            if (png.color_type > 6 || !channels[png.color_type] || body[10] || body[11] ||
                png.interlace > 1)
                goto done;
            png.channels = channels[png.color_type];
            bool depth_ok = png.depth == 8 ||
                (png.depth == 16 && png.color_type != 3) ||
                ((png.depth == 1 || png.depth == 2 || png.depth == 4) &&
                 (png.color_type == 0 || png.color_type == 3));
            if (!depth_ok || png.width == 0 || png.height == 0)
                goto done;
            if (png.width > IMAGE_MAX_SIDE || png.height > IMAGE_MAX_SIDE ||
                (uint64_t)png.width * png.height > IMAGE_MAX_PIXELS) {
                *error = "Bild ist zu groß";
                goto done;
            }
            have_header = true;
        } else if (memcmp(type, "PLTE", 4) == 0) {
            png.palette_size = (int)(size / 3 > 256 ? 256 : size / 3);
            for (int i = 0; i < png.palette_size; i++)
                png.palette[i] = 0xFF000000u | ((uint32_t)body[3 * i] << 16) |
                                 ((uint32_t)body[3 * i + 1] << 8) | body[3 * i + 2];
        } else if (memcmp(type, "tRNS", 4) == 0 && have_header) {
            if (png.color_type == 3) {
                for (uint32_t i = 0; i < size && i < (uint32_t)png.palette_size; i++)
                    png.palette[i] = (png.palette[i] & 0x00FFFFFFu) | ((uint32_t)body[i] << 24);
            } else if (png.color_type == 0 && size >= 2) {
                png.has_key = true;
                png.key[0] = (uint16_t)((body[0] << 8) | body[1]);
            } else if (png.color_type == 2 && size >= 6) {
                png.has_key = true;
                for (int i = 0; i < 3; i++)
                    png.key[i] = (uint16_t)((body[2 * i] << 8) | body[2 * i + 1]);
            }
        } else if (memcmp(type, "IDAT", 4) == 0) {
            memcpy(packed_data + packed_size, body, size);
            packed_size += size;
        } else if (memcmp(type, "IEND", 4) == 0) {
            break;
        }
        pos += 12 + (size_t)size;
    }
    if (!have_header || packed_size == 0)
        goto done;

    /* Bei Adam7 besteht das Bild aus sieben ineinander verschachtelten Teilbildern. */
    static const uint8_t x_start[7] = { 0, 4, 0, 2, 0, 1, 0 };
    static const uint8_t y_start[7] = { 0, 0, 4, 0, 2, 0, 1 };
    static const uint8_t x_step[7] = { 8, 8, 4, 4, 2, 2, 1 };
    static const uint8_t y_step[7] = { 8, 8, 8, 4, 4, 2, 2 };
    int passes = png.interlace ? 7 : 1;
    uint32_t pass_w[7], pass_h[7];
    size_t raw_size = 0;
    for (int p = 0; p < passes; p++) {
        if (png.interlace) {
            pass_w[p] = (png.width + x_step[p] - 1 - x_start[p]) / x_step[p];
            pass_h[p] = (png.height + y_step[p] - 1 - y_start[p]) / y_step[p];
        } else {
            pass_w[p] = png.width;
            pass_h[p] = png.height;
        }
        if (pass_w[p] && pass_h[p])
            raw_size += (row_bytes(&png, pass_w[p]) + 1) * pass_h[p];
    }

    raw = kmalloc(raw_size);
    image = image_create((int)png.width, (int)png.height);
    if (!raw || !image) {
        *error = "zu wenig Speicher";
        image_free(image);
        image = NULL;
        goto done;
    }
    if (!inflate_zlib(packed_data, packed_size, raw, raw_size)) {
        image_free(image);
        image = NULL;
        goto done;
    }

    size_t bpp = (size_t)(png.channels * png.depth + 7) / 8;
    uint8_t *row = raw;
    for (int p = 0; p < passes; p++) {
        if (!pass_w[p] || !pass_h[p])
            continue;
        size_t bytes = row_bytes(&png, pass_w[p]);
        const uint8_t *previous = NULL;
        for (uint32_t y = 0; y < pass_h[p]; y++) {
            if (!unfilter(row + 1, previous, bytes, bpp, row[0])) {
                image_free(image);
                image = NULL;
                goto done;
            }
            for (uint32_t x = 0; x < pass_w[p]; x++) {
                uint32_t color = pixel_at(&png, row + 1, x);
                uint32_t tx = png.interlace ? x_start[p] + x * x_step[p] : x;
                uint32_t ty = png.interlace ? y_start[p] + y * y_step[p] : y;
                image->pixels[(size_t)ty * png.width + tx] = color;
                if ((color >> 24) != 255)
                    image->alpha = true;
            }
            previous = row + 1;
            row += bytes + 1;
        }
    }
    *error = NULL;

done:
    kfree(packed_data);
    kfree(raw);
    return image;
}
