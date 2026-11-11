/* Zeichenfunktionen: Flächen (Surfaces), Rechtecke, Linien, Text */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define FONT_W 8
#define FONT_H 16

/* Farben sind 0x00RRGGBB. */
typedef uint32_t color_t;
#define RGB(r, g, b) ((color_t)(((r) << 16) | ((g) << 8) | (b)))

typedef struct {
    int x, y, w, h;
} rect_t;

/* Eine Zeichenfläche im Speicher. Gezeichnet wird nur innerhalb von clip. */
typedef struct {
    uint32_t *pixels;
    int width, height;
    int stride;                         /* Pixel pro Zeile im Speicher */
    rect_t clip;
} surface_t;

extern const uint8_t font_regular[256][FONT_W * FONT_H];
extern const uint8_t font_bold[256][FONT_W * FONT_H];

static inline rect_t rect_make(int x, int y, int w, int h)
{
    return (rect_t){ x, y, w, h };
}

static inline bool rect_empty(rect_t r)
{
    return r.w <= 0 || r.h <= 0;
}

static inline bool rect_contains(rect_t r, int x, int y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

rect_t rect_intersect(rect_t a, rect_t b);
rect_t rect_union(rect_t a, rect_t b);

surface_t *surface_create(int width, int height);
void surface_free(surface_t *s);
void surface_init(surface_t *s, uint32_t *pixels, int width, int height,
                  int stride);
void gfx_set_clip(surface_t *s, rect_t clip);
void gfx_reset_clip(surface_t *s);

void gfx_fill(surface_t *s, int x, int y, int w, int h, color_t color);
void gfx_frame(surface_t *s, int x, int y, int w, int h, color_t color);
void gfx_gradient(surface_t *s, int x, int y, int w, int h, color_t top,
                  color_t bottom);
void gfx_darken(surface_t *s, int x, int y, int w, int h, int percent);
void gfx_line(surface_t *s, int x0, int y0, int x1, int y1, int thickness,
              color_t color);
void gfx_blit(surface_t *dst, int dx, int dy, const surface_t *src);

/* Text ist UTF-8; dargestellt werden die Zeichen bis U+00FF. */
void gfx_glyph(surface_t *s, int x, int y, uint32_t codepoint, color_t color,
               bool bold);
int gfx_text(surface_t *s, int x, int y, const char *text, color_t color,
             bool bold);
void gfx_text_scaled(surface_t *s, int x, int y, const char *text,
                     color_t color, int scale);
int gfx_text_width(const char *text);

/* Liest ein UTF-8-Zeichen und rückt *text weiter; 0 am Ende. */
uint32_t utf8_next(const char **text);
