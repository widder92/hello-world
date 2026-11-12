/* Schriften für die Oberfläche - siehe font.h */
#include "font.h"
#include "console.h"
#include "cpu.h"
#include "memory.h"
#include "string.h"
#include "truetype.h"

/* Die eingebundenen Schriftdateien (kernel/fonts.asm) */
extern const uint8_t font_file_regular[], font_file_regular_end[];
extern const uint8_t font_file_bold[], font_file_bold_end[];

static ttf_t faces[2];                  /* 0: normal, 1: fett */
static bool ready;

/* --- Zwischenspeicher für gerasterte Zeichen ----------------------------------------- */

#define SLOTS        4096               /* Zweierpotenz */
#define ENTRIES_MAX  3000               /* darüber wird alles verworfen und neu gesammelt */
#define BYTES_MAX    (12u * 1024 * 1024)

typedef struct {
    uint32_t key;                       /* 0: frei */
    int16_t width, height, left, top, advance;
    uint8_t *coverage;                  /* NULL bei leeren Zeichen */
} cached_t;

static cached_t cache[SLOTS];
static int cache_entries;
static size_t cache_bytes;

/* Schrift, Neigung, Größe und Umriss in einer Zahl; nie 0 */
static uint32_t make_key(int face, bool oblique, int size, int glyph)
{
    return 0x80000000u | ((uint32_t)face << 30) | ((uint32_t)oblique << 29) |
           ((uint32_t)(size & 0x3FF) << 16) | (uint32_t)(glyph & 0xFFFF);
}

static unsigned slot_of(uint32_t key)
{
    return (key * 2654435761u) >> 20 & (SLOTS - 1);
}

/* Nur bei gesperrten Unterbrechungen aufrufen. */
static cached_t *cache_find(uint32_t key)
{
    for (unsigned i = slot_of(key), n = 0; n < SLOTS; i = (i + 1) & (SLOTS - 1), n++) {
        if (cache[i].key == key)
            return &cache[i];
        if (cache[i].key == 0)
            return NULL;
    }
    return NULL;
}

/* Rastert ein Zeichen und legt es ab (falls es nicht inzwischen ein anderer Task tat). */
static void cache_fill(uint32_t key, int face, bool oblique, int size, int glyph)
{
    ttf_bitmap_t bitmap;
    bool ok = ttf_render(&faces[face], glyph, size, oblique, &bitmap);
    int advance = ttf_advance(&faces[face], glyph, size);
    if (!ok)
        memset(&bitmap, 0, sizeof(bitmap));     /* als leeres Zeichen merken */

    uint8_t *doomed[SLOTS / 16];
    int doomed_count = 0;
    bool flush = false;

    uint64_t flags = irq_save();
    if (cache_find(key)) {
        irq_restore(flags);
        kfree(bitmap.coverage);
        return;
    }
    if (cache_entries >= ENTRIES_MAX || cache_bytes > BYTES_MAX)
        flush = true;
    irq_restore(flags);

    /*
     * Voll: alles verwerfen. Das Freigeben geschieht häppchenweise, damit die
     * Unterbrechungen nie lange gesperrt bleiben.
     */
    while (flush) {
        doomed_count = 0;
        flags = irq_save();
        for (unsigned i = 0; i < SLOTS && doomed_count < (int)(SLOTS / 16); i++) {
            if (cache[i].coverage) {
                doomed[doomed_count++] = cache[i].coverage;
                cache[i].coverage = NULL;
            }
        }
        if (doomed_count == 0) {
            memset(cache, 0, sizeof(cache));
            cache_entries = 0;
            cache_bytes = 0;
            flush = false;
        }
        irq_restore(flags);
        for (int i = 0; i < doomed_count; i++)
            kfree(doomed[i]);
    }

    flags = irq_save();
    if (!cache_find(key)) {
        unsigned i = slot_of(key);
        while (cache[i].key != 0)
            i = (i + 1) & (SLOTS - 1);
        cache[i].key = key;
        cache[i].width = (int16_t)bitmap.width;
        cache[i].height = (int16_t)bitmap.height;
        cache[i].left = (int16_t)bitmap.left;
        cache[i].top = (int16_t)bitmap.top;
        cache[i].advance = (int16_t)advance;
        cache[i].coverage = bitmap.coverage;
        cache_entries++;
        cache_bytes += (size_t)bitmap.width * (size_t)bitmap.height;
        bitmap.coverage = NULL;
    }
    irq_restore(flags);
    kfree(bitmap.coverage);
}

/* --- Zeichnen und Messen -------------------------------------------------------------- */

void font_init(void)
{
    ready = ttf_open(&faces[0], font_file_regular,
                     (size_t)(font_file_regular_end - font_file_regular)) &&
            ttf_open(&faces[1], font_file_bold,
                     (size_t)(font_file_bold_end - font_file_bold));
    if (!ready)
        klog("Schrift: eingebaute Schriftdateien nicht lesbar - es bleibt bei der Rasterschrift\n");
}

bool font_available(void)
{
    return ready;
}

static int clamp_size(int size)
{
    return size < 6 ? 6 : size > 200 ? 200 : size;
}

void font_metrics(int size, int *ascent, int *descent, int *line_height)
{
    if (!ready) {
        if (ascent) *ascent = 12;
        if (descent) *descent = 4;
        if (line_height) *line_height = FONT_H;
        return;
    }
    ttf_line_metrics(&faces[0], clamp_size(size), ascent, descent, line_height);
}

/*
 * Geht den Text Zeichen für Zeichen durch. Mit Fläche wird gezeichnet, ohne
 * nur gemessen. Liefert die Breite.
 */
static int run(surface_t *s, int x, int baseline, const char *text, size_t len,
               int size, int style, color_t color)
{
    if (!ready || !text)
        return 0;
    size = clamp_size(size);
    int face = (style & FONT_BOLD) ? 1 : 0;
    bool oblique = style & FONT_ITALIC;
    const char *end = text + len;
    int pen = x;
    uint32_t red = (color >> 16) & 0xFF, green = (color >> 8) & 0xFF, blue = color & 0xFF;

    while (text < end && *text) {
        const char *before = text;
        uint32_t cp = utf8_next(&text);
        if (text > end || text == before)
            break;
        if (cp == 0x00AD)
            continue;                   /* weicher Trennstrich: unsichtbar */
        if (cp == 0x00A0 || cp == 0x202F || cp == 0x2009)
            cp = ' ';                   /* geschützte und schmale Leerzeichen */
        int glyph = ttf_glyph(&faces[face], cp);
        uint32_t key = make_key(face, oblique, size, glyph);

        for (int attempt = 0; attempt < 2; attempt++) {
            uint64_t flags = irq_save();
            const cached_t *c = cache_find(key);
            if (c) {
                /* Zeichnen unter der Sperre: Der Eintrag kann sonst verworfen werden. */
                if (s && c->coverage) {
                    int x0 = pen + c->left, y0 = baseline + c->top;
                    int left = s->clip.x, top = s->clip.y;
                    int right = s->clip.x + s->clip.w, bottom = s->clip.y + s->clip.h;
                    for (int row = 0; row < c->height; row++) {
                        int y = y0 + row;
                        if (y < top || y >= bottom)
                            continue;
                        const uint8_t *source = c->coverage + (size_t)row * c->width;
                        uint32_t *line = s->pixels + (size_t)y * s->stride;
                        for (int col = 0; col < c->width; col++) {
                            uint32_t a = source[col];
                            int px = x0 + col;
                            if (!a || px < left || px >= right)
                                continue;
                            uint32_t d = line[px], rest = 255 - a;
                            uint32_t dr = (((d >> 16) & 0xFF) * rest + red * a + 127) / 255;
                            uint32_t dg = (((d >> 8) & 0xFF) * rest + green * a + 127) / 255;
                            uint32_t db = ((d & 0xFF) * rest + blue * a + 127) / 255;
                            line[px] = (dr << 16) | (dg << 8) | db;
                        }
                    }
                }
                pen += c->advance;
                irq_restore(flags);
                break;
            }
            irq_restore(flags);
            if (attempt == 0)
                cache_fill(key, face, oblique, size, glyph);
            else
                pen += ttf_advance(&faces[face], glyph, size);     /* kein Speicher: nur vorrücken */
        }
    }
    return pen - x;
}

int font_width(const char *text, size_t len, int size, int style)
{
    return run(NULL, 0, 0, text, len, size, style, 0);
}

int font_draw(surface_t *s, int x, int baseline, const char *text, size_t len,
              int size, int style, color_t color)
{
    return run(s, x, baseline, text, len, size, style, color);
}

int ui_text(surface_t *s, int x, int y, const char *text, color_t color, bool bold)
{
    if (!ready)
        return gfx_text(s, x, y, text, color, bold);
    /* wie gfx_text(): liefert die Stelle hinter dem Text */
    return x + font_draw(s, x, y + 12, text, strlen(text), FONT_UI_SIZE,
                         bold ? FONT_BOLD : 0, color);
}

int ui_text_width(const char *text, bool bold)
{
    if (!ready)
        return gfx_text_width(text);
    return font_width(text, strlen(text), FONT_UI_SIZE, bold ? FONT_BOLD : 0);
}
