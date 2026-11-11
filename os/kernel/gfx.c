#include "gfx.h"
#include "memory.h"
#include "string.h"

/* --- Rechtecke ---------------------------------------------------------------- */

rect_t rect_intersect(rect_t a, rect_t b)
{
    int x0 = a.x > b.x ? a.x : b.x;
    int y0 = a.y > b.y ? a.y : b.y;
    int x1 = a.x + a.w < b.x + b.w ? a.x + a.w : b.x + b.w;
    int y1 = a.y + a.h < b.y + b.h ? a.y + a.h : b.y + b.h;
    if (x1 <= x0 || y1 <= y0)
        return rect_make(0, 0, 0, 0);
    return rect_make(x0, y0, x1 - x0, y1 - y0);
}

rect_t rect_union(rect_t a, rect_t b)
{
    if (rect_empty(a))
        return b;
    if (rect_empty(b))
        return a;
    int x0 = a.x < b.x ? a.x : b.x;
    int y0 = a.y < b.y ? a.y : b.y;
    int x1 = a.x + a.w > b.x + b.w ? a.x + a.w : b.x + b.w;
    int y1 = a.y + a.h > b.y + b.h ? a.y + a.h : b.y + b.h;
    return rect_make(x0, y0, x1 - x0, y1 - y0);
}

/* --- Flächen ------------------------------------------------------------------ */

void surface_init(surface_t *s, uint32_t *pixels, int width, int height,
                  int stride)
{
    s->pixels = pixels;
    s->width = width;
    s->height = height;
    s->stride = stride;
    s->clip = rect_make(0, 0, width, height);
}

surface_t *surface_create(int width, int height)
{
    surface_t *s = kmalloc(sizeof(*s));
    uint32_t *pixels = kzalloc((size_t)width * height * sizeof(uint32_t));
    if (!s || !pixels) {
        kfree(s);
        kfree(pixels);
        return NULL;
    }
    surface_init(s, pixels, width, height, width);
    return s;
}

void surface_free(surface_t *s)
{
    if (!s)
        return;
    kfree(s->pixels);
    kfree(s);
}

void gfx_set_clip(surface_t *s, rect_t clip)
{
    s->clip = rect_intersect(clip, rect_make(0, 0, s->width, s->height));
}

void gfx_reset_clip(surface_t *s)
{
    s->clip = rect_make(0, 0, s->width, s->height);
}

static inline uint32_t *row(const surface_t *s, int y)
{
    return s->pixels + (size_t)y * s->stride;
}

/* Mischt zwei Farben: alpha 0 = nur bg, 255 = nur fg. */
static inline color_t blend(color_t bg, color_t fg, unsigned alpha)
{
    unsigned inv = 255 - alpha;
    uint32_t rb = ((fg & 0xFF00FF) * alpha + (bg & 0xFF00FF) * inv + 0x800080);
    uint32_t g = ((fg & 0x00FF00) * alpha + (bg & 0x00FF00) * inv + 0x008000);
    rb = ((rb + ((rb >> 8) & 0xFF00FF)) >> 8) & 0xFF00FF;
    g = ((g + ((g >> 8) & 0x00FF00)) >> 8) & 0x00FF00;
    return rb | g;
}

/* --- Grundformen -------------------------------------------------------------- */

void gfx_fill(surface_t *s, int x, int y, int w, int h, color_t color)
{
    rect_t r = rect_intersect(rect_make(x, y, w, h), s->clip);
    for (int yy = r.y; yy < r.y + r.h; yy++) {
        uint32_t *p = row(s, yy) + r.x;
        for (int i = 0; i < r.w; i++)
            p[i] = color;
    }
}

void gfx_frame(surface_t *s, int x, int y, int w, int h, color_t color)
{
    gfx_fill(s, x, y, w, 1, color);
    gfx_fill(s, x, y + h - 1, w, 1, color);
    gfx_fill(s, x, y, 1, h, color);
    gfx_fill(s, x + w - 1, y, 1, h, color);
}

void gfx_gradient(surface_t *s, int x, int y, int w, int h, color_t top,
                  color_t bottom)
{
    rect_t r = rect_intersect(rect_make(x, y, w, h), s->clip);
    for (int yy = r.y; yy < r.y + r.h; yy++) {
        unsigned alpha = h > 1 ? (unsigned)((yy - y) * 255 / (h - 1)) : 0;
        color_t color = blend(top, bottom, alpha);
        uint32_t *p = row(s, yy) + r.x;
        for (int i = 0; i < r.w; i++)
            p[i] = color;
    }
}

void gfx_darken(surface_t *s, int x, int y, int w, int h, int percent)
{
    rect_t r = rect_intersect(rect_make(x, y, w, h), s->clip);
    unsigned alpha = (unsigned)(percent * 255 / 100);
    for (int yy = r.y; yy < r.y + r.h; yy++) {
        uint32_t *p = row(s, yy) + r.x;
        for (int i = 0; i < r.w; i++)
            p[i] = blend(p[i], 0, alpha);
    }
}

void gfx_line(surface_t *s, int x0, int y0, int x1, int y1, int thickness,
              color_t color)
{
    /* Bresenham; für jede Position ein kleines Quadrat */
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    int half = thickness / 2;

    for (;;) {
        gfx_fill(s, x0 - half, y0 - half, thickness, thickness, color);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void gfx_blit(surface_t *dst, int dx, int dy, const surface_t *src)
{
    rect_t r = rect_intersect(rect_make(dx, dy, src->width, src->height),
                              dst->clip);
    for (int y = r.y; y < r.y + r.h; y++) {
        memcpy(row(dst, y) + r.x, row(src, y - dy) + (r.x - dx),
               (size_t)r.w * sizeof(uint32_t));
    }
}

/* --- Text ---------------------------------------------------------------------- */

uint32_t utf8_next(const char **text)
{
    const uint8_t *p = (const uint8_t *)*text;
    uint32_t cp = *p;
    int extra = 0;

    if (cp == 0)
        return 0;
    p++;
    if (cp >= 0xF0) {
        cp &= 0x07;
        extra = 3;
    } else if (cp >= 0xE0) {
        cp &= 0x0F;
        extra = 2;
    } else if (cp >= 0xC0) {
        cp &= 0x1F;
        extra = 1;
    } else if (cp >= 0x80) {
        cp = '?';                       /* verirrtes Folgebyte */
    }
    while (extra-- > 0 && (*p & 0xC0) == 0x80)
        cp = (cp << 6) | (*p++ & 0x3F);

    *text = (const char *)p;
    return cp;
}

void gfx_glyph(surface_t *s, int x, int y, uint32_t codepoint, color_t color,
               bool bold)
{
    if (codepoint > 0xFF)
        codepoint = '?';
    const uint8_t *glyph = bold ? font_bold[codepoint] : font_regular[codepoint];

    rect_t r = rect_intersect(rect_make(x, y, FONT_W, FONT_H), s->clip);
    for (int yy = r.y; yy < r.y + r.h; yy++) {
        const uint8_t *alpha = glyph + (yy - y) * FONT_W - x;
        uint32_t *p = row(s, yy);
        for (int xx = r.x; xx < r.x + r.w; xx++) {
            unsigned a = alpha[xx];
            if (a == 255)
                p[xx] = color;
            else if (a)
                p[xx] = blend(p[xx], color, a);
        }
    }
}

int gfx_text(surface_t *s, int x, int y, const char *text, color_t color,
             bool bold)
{
    uint32_t cp;
    while ((cp = utf8_next(&text)) != 0) {
        gfx_glyph(s, x, y, cp, color, bold);
        x += FONT_W;
    }
    return x;
}

void gfx_text_scaled(surface_t *s, int x, int y, const char *text,
                     color_t color, int scale)
{
    uint32_t cp;
    while ((cp = utf8_next(&text)) != 0) {
        if (cp > 0xFF)
            cp = '?';
        const uint8_t *glyph = font_bold[cp];
        for (int gy = 0; gy < FONT_H; gy++) {
            for (int gx = 0; gx < FONT_W; gx++) {
                unsigned a = glyph[gy * FONT_W + gx];
                if (a < 96)
                    continue;
                gfx_fill(s, x + gx * scale, y + gy * scale, scale, scale,
                         color);
            }
        }
        x += FONT_W * scale;
    }
}

int gfx_text_width(const char *text)
{
    int count = 0;
    while (utf8_next(&text))
        count++;
    return count * FONT_W;
}
