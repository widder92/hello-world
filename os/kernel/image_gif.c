/*
 * GIF-Bilder: Gezeigt wird das erste Einzelbild (bewegte GIFs stehen also
 * still), mit Transparenz und Zeilensprung.
 */
#include "image.h"
#include "memory.h"
#include "string.h"

#define MAX_CODES 4096

typedef struct {
    const uint8_t *data;
    size_t length, pos;
    /* Die Bilddaten liegen in Häppchen von höchstens 255 Bytes vor */
    size_t block_left;
    bool ended;
    uint32_t bit_buffer;
    int bit_count;
} reader_t;

static int next_byte(reader_t *r)
{
    if (r->ended)
        return -1;
    if (r->block_left == 0) {
        if (r->pos >= r->length) {
            r->ended = true;
            return -1;
        }
        r->block_left = r->data[r->pos++];
        if (r->block_left == 0 || r->block_left > r->length - r->pos) {
            r->ended = true;
            return -1;
        }
    }
    r->block_left--;
    return r->data[r->pos++];
}

static int next_code(reader_t *r, int size)
{
    while (r->bit_count < size) {
        int byte = next_byte(r);
        if (byte < 0)
            return -1;
        r->bit_buffer |= (uint32_t)byte << r->bit_count;
        r->bit_count += 8;
    }
    int code = (int)(r->bit_buffer & ((1u << size) - 1));
    r->bit_buffer >>= size;
    r->bit_count -= size;
    return code;
}

/* Überspringt eine Folge von Datenhäppchen bis zum leeren Abschluss. */
static bool skip_blocks(const uint8_t *data, size_t length, size_t *pos)
{
    while (*pos < length) {
        size_t size = data[(*pos)++];
        if (size == 0)
            return true;
        if (size > length - *pos)
            return false;
        *pos += size;
    }
    return false;
}

image_t *gif_decode(const uint8_t *data, size_t length, const char **error)
{
    *error = "GIF-Datei ist beschädigt";
    if (length < 13 || memcmp(data, "GIF8", 4) != 0)
        return NULL;

    uint32_t global_colors[256], local_colors[256];
    const uint32_t *colors = global_colors;
    int transparent = -1;
    unsigned screen_w = data[6] | (data[7] << 8);
    unsigned screen_h = data[8] | (data[9] << 8);
    size_t pos = 13;

    memset(global_colors, 0, sizeof(global_colors));
    if (data[10] & 0x80) {
        size_t count = (size_t)1 << ((data[10] & 7) + 1);
        if (pos + 3 * count > length)
            return NULL;
        for (size_t i = 0; i < count; i++, pos += 3)
            global_colors[i] = 0xFF000000u | ((uint32_t)data[pos] << 16) |
                               ((uint32_t)data[pos + 1] << 8) | data[pos + 2];
    }

    /* Bis zum ersten Bild vorarbeiten */
    for (;;) {
        if (pos >= length)
            return NULL;
        uint8_t kind = data[pos++];
        if (kind == 0x21) {             /* Erweiterung */
            if (pos + 1 > length)
                return NULL;
            uint8_t label = data[pos++];
            if (label == 0xF9 && pos + 5 <= length && data[pos] >= 4) {
                if (data[pos + 1] & 1)  /* Steuerblock: eine Farbe ist durchsichtig */
                    transparent = data[pos + 4];
            }
            if (!skip_blocks(data, length, &pos))
                return NULL;
        } else if (kind == 0x2C) {
            break;                      /* Bild */
        } else {
            return NULL;                /* auch 0x3B: Datei ohne Bild */
        }
    }
    if (pos + 9 > length)
        return NULL;
    unsigned left = data[pos] | (data[pos + 1] << 8);
    unsigned top = data[pos + 2] | (data[pos + 3] << 8);
    unsigned width = data[pos + 4] | (data[pos + 5] << 8);
    unsigned height = data[pos + 6] | (data[pos + 7] << 8);
    uint8_t flags = data[pos + 8];
    pos += 9;
    if (flags & 0x80) {
        size_t count = (size_t)1 << ((flags & 7) + 1);
        if (pos + 3 * count > length)
            return NULL;
        memset(local_colors, 0, sizeof(local_colors));
        for (size_t i = 0; i < count; i++, pos += 3)
            local_colors[i] = 0xFF000000u | ((uint32_t)data[pos] << 16) |
                              ((uint32_t)data[pos + 1] << 8) | data[pos + 2];
        colors = local_colors;
    }

    /* Manche Dateien geben die Gesamtgröße nicht an: dann zählt das Bild selbst */
    if (screen_w == 0 || screen_h == 0) {
        screen_w = left + width;
        screen_h = top + height;
    }
    if (width == 0 || height == 0 || screen_w == 0 || screen_h == 0)
        return NULL;
    if (screen_w > IMAGE_MAX_SIDE || screen_h > IMAGE_MAX_SIDE ||
        (uint64_t)screen_w * screen_h > IMAGE_MAX_PIXELS) {
        *error = "Bild ist zu groß";
        return NULL;
    }
    if (pos >= length)
        return NULL;
    int min_size = data[pos++];
    if (min_size < 2 || min_size > 8)
        return NULL;

    image_t *image = image_create((int)screen_w, (int)screen_h);
    uint16_t *prefix = kmalloc(MAX_CODES * sizeof(uint16_t));
    uint8_t *suffix = kmalloc(MAX_CODES);
    uint8_t *stack = kmalloc(MAX_CODES + 1);
    if (!image || !prefix || !suffix || !stack) {
        *error = "zu wenig Speicher";
        image_free(image);
        image = NULL;
        goto done;
    }
    memset(image->pixels, 0, (size_t)screen_w * screen_h * sizeof(uint32_t));
    image->alpha = transparent >= 0 || width != screen_w || height != screen_h;

    /* LZW: Codes stehen für immer längere Folgen von Farbnummern */
    reader_t reader = { .data = data, .length = length, .pos = pos };
    int clear = 1 << min_size, end = clear + 1;
    int size = min_size + 1, available = clear + 2, previous = -1, first = 0;
    for (int i = 0; i < clear; i++) {
        prefix[i] = 0;
        suffix[i] = (uint8_t)i;
    }

    /* Reihenfolge der Zeilen beim Zeilensprung: vier Durchgänge */
    static const uint8_t pass_start[4] = { 0, 4, 2, 1 }, pass_step[4] = { 8, 8, 4, 2 };
    bool interlaced = flags & 0x40;
    unsigned x = 0, y = 0;
    int pass = 0;
    size_t written = 0, total = (size_t)width * height;

    while (written < total) {
        int code = next_code(&reader, size);
        if (code < 0 || code == end)
            break;
        if (code == clear) {
            size = min_size + 1;
            available = clear + 2;
            previous = -1;
            continue;
        }

        int top_of_stack = 0, current = code;
        if (previous < 0) {
            if (code >= clear)
                break;                  /* beschädigt */
            first = code;
        } else if (code >= available) {
            if (code != available)
                break;
            stack[top_of_stack++] = (uint8_t)first;     /* Sonderfall: Folge + ihr Anfang */
            current = previous;
        }
        while (current >= clear) {
            if (current >= MAX_CODES || top_of_stack >= MAX_CODES)
                goto finish;
            stack[top_of_stack++] = suffix[current];
            current = prefix[current];
        }
        first = suffix[current];
        stack[top_of_stack++] = (uint8_t)first;

        if (previous >= 0 && available < MAX_CODES) {
            prefix[available] = (uint16_t)previous;
            suffix[available] = (uint8_t)first;
            available++;
            if (available == (1 << size) && size < 12)
                size++;
        }
        previous = code;

        /* Die Folge steht verkehrt herum im Stapel */
        while (top_of_stack > 0 && written < total) {
            int index = stack[--top_of_stack];
            unsigned px = left + x, py = top + y;
            if (px < screen_w && py < screen_h && index != transparent)
                image->pixels[(size_t)py * screen_w + px] = colors[index];
            written++;
            if (++x == width) {
                x = 0;
                if (interlaced) {
                    y += pass_step[pass];
                    while (y >= height && pass < 3) {
                        pass++;
                        y = pass_start[pass];
                    }
                } else {
                    y++;
                }
            }
        }
    }
finish:
    /* Ein abgeschnittenes Bild zeigen wir so weit, wie es reicht. */
    if (written == 0) {
        image_free(image);
        image = NULL;
    } else {
        *error = NULL;
    }
done:
    kfree(prefix);
    kfree(suffix);
    kfree(stack);
    return image;
}
