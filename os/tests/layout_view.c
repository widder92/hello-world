/*
 * Werkzeug für den Entwicklungsrechner: setzt eine HTML-Datei mit dem
 * Seitenaufbau des Browsers (kernel/layout.c) und schreibt das Ergebnis als
 * Bild (PPM) - mit den Schriften, die auch HelloOS benutzt. So lässt sich
 * ansehen, wie eine Seite gesetzt wird, ohne das System zu starten.
 *
 *   layout_view seite.html [breite] [bild.ppm]
 *
 * Stylesheets werden aus der Seite gelesen (<style>) und, wenn verlinkt, als
 * Datei neben ihr gesucht. Bilder gibt es nicht: Jedes gilt als 220x150 groß
 * (Adresse mit "fehlt": nicht ladbar; mit "spaeter": noch nicht da).
 * Mit ZEILEN=1 in der Umgebung erscheinen Kästen und Zeilen als Text.
 */
#include "../kernel/layout.h"
#include "../kernel/gfx.h"
#include "../kernel/truetype.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#define strlcpy libc_strlcpy
#include <string.h>
#undef strlcpy

void *kmalloc(size_t n) { return malloc(n ? n : 1); }
void *kzalloc(size_t n) { return calloc(1, n ? n : 1); }
void kfree(void *p) { free(p); }
void strlcpy(char *d, const char *s, size_t n)
{
    if (!n) return;
    size_t i = 0;
    for (; i + 1 < n && s[i]; i++) d[i] = s[i];
    d[i] = 0;
}
int ksnprintf(char *b, size_t n, const char *f, ...)
{
    va_list ap;
    va_start(ap, f);
    int r = vsnprintf(b, n, f, ap);
    va_end(ap);
    return r;
}
void claude_plain_text(char *text) { (void)text; }
void call_on_stack(void (*fn)(void *), void *arg, void *top) { (void)top; fn(arg); }

static ttf_t faces[2];
static const char *directory = ".";

static char *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    char *data = malloc((size_t)n + 1);
    if (fread(data, 1, (size_t)n, f) != (size_t)n)
        n = 0;
    data[n] = 0;
    fclose(f);
    *len = (size_t)n;
    return data;
}

static uint32_t next_char(const char **p, const char *end)
{
    const uint8_t *s = (const uint8_t *)*p;
    uint32_t cp = *s;
    int extra = cp >= 0xF0 ? 3 : cp >= 0xE0 ? 2 : cp >= 0xC0 ? 1 : 0;
    cp = extra == 3 ? cp & 7 : extra == 2 ? cp & 15 : extra == 1 ? cp & 31 : cp;
    s++;
    for (; extra > 0 && (const char *)s < end && (*s & 0xC0) == 0x80; extra--)
        cp = (cp << 6) | (*s++ & 0x3F);
    *p = (const char *)s;
    return cp;
}

static int text_width(void *ctx, const char *text, size_t len, uint8_t style, uint8_t size)
{
    (void)ctx;
    const char *end = text + len;
    int w = 0;
    while (text < end) {
        uint32_t cp = next_char(&text, end);
        if (style & STYLE_MONO)
            w += FONT_W;
        else
            w += ttf_advance(&faces[style & STYLE_BOLD ? 1 : 0], ttf_glyph(&faces[style & STYLE_BOLD ? 1 : 0], cp),
                             size ? size : 15);
    }
    return w;
}

static void metrics(void *ctx, uint8_t size, uint8_t style, int *ascent, int *descent)
{
    (void)ctx;
    (void)style;
    int line;
    ttf_line_metrics(&faces[0], size, ascent, descent, &line);
}

static picture_state_t lookup(void *ctx, const char *url, int *w, int *h)
{
    (void)ctx;
    if (strstr(url, "fehlt"))
        return PICTURE_BROKEN;
    if (strstr(url, "spaeter"))
        return PICTURE_UNKNOWN;
    *w = 220;
    *h = 150;
    const char *size = strstr(url, "gross=");
    if (size)
        sscanf(size, "gross=%dx%d", w, h);
    return PICTURE_READY;
}

static void style_found(void *ctx, bool linked, const char *text, size_t len)
{
    css_sheet_t *css = ctx;
    if (!linked) {
        css_add(css, text, len);
        return;
    }
    /* verlinkt: die Datei mit dem letzten Stück der Adresse, neben der Seite */
    char path[1024];
    const char *name = text;
    for (const char *p = text; p < text + len; p++) {
        if (*p == '/')
            name = p + 1;
        if (*p == '?')
            break;
    }
    size_t n = 0;
    while (name + n < text + len && name[n] != '?')
        n++;
    snprintf(path, sizeof(path), "%s/%.*s", directory, (int)n, name);
    size_t size;
    char *file = slurp(path, &size);
    if (!file) {
        /* oder unter der ganzen Adresse als Dateiname (alles außer Buchstaben, Ziffern, Punkt wird "_") */
        char flat[200];
        size_t k = 0;
        const char *u = text;
        for (size_t i = 0; i + 2 < len; i++) {
            if (text[i] == ':' && text[i + 1] == '/' && text[i + 2] == '/') {
                u = text + i + 3;
                break;
            }
        }
        for (; u < text + len && k < 150; u++)
            flat[k++] = (*u >= 'a' && *u <= 'z') || (*u >= 'A' && *u <= 'Z') || (*u >= '0' && *u <= '9') || *u == '.' ? *u : '_';
        flat[k] = 0;
        snprintf(path, sizeof(path), "%s/%s", directory, flat);
        file = slurp(path, &size);
    }
    if (file)
        css_add(css, file, size);
    else
        fprintf(stderr, "Stylesheet fehlt: %s\n", path);
    free(file);
}

/* --- Zeichnen ---------------------------------------------------------------------------------- */

static uint32_t *pixels;
static int W, H;
static int clip_x0, clip_y0, clip_x1 = 1 << 30, clip_y1 = 1 << 30;      /* der geltende Ausschnitt */

static void fill(int x, int y, int w, int h, uint32_t color)
{
    for (int yy = y < 0 ? 0 : y; yy < y + h && yy < H; yy++) {
        if (yy < clip_y0 || yy >= clip_y1)
            continue;
        for (int xx = x < 0 ? 0 : x; xx < x + w && xx < W; xx++) {
            if (xx >= clip_x0 && xx < clip_x1)
                pixels[(size_t)yy * W + xx] = color;
        }
    }
}

static void blend(int x, int y, uint32_t color, uint32_t a)
{
    if (x < 0 || y < 0 || x >= W || y >= H || !a || x < clip_x0 || x >= clip_x1 || y < clip_y0 || y >= clip_y1)
        return;
    uint32_t d = pixels[(size_t)y * W + x], rest = 255 - a;
    uint32_t r = (((d >> 16) & 255) * rest + ((color >> 16) & 255) * a) / 255;
    uint32_t g = (((d >> 8) & 255) * rest + ((color >> 8) & 255) * a) / 255;
    uint32_t b = ((d & 255) * rest + (color & 255) * a) / 255;
    pixels[(size_t)y * W + x] = (r << 16) | (g << 8) | b;
}

static void draw_text(int x, int baseline, const char *text, size_t len, uint8_t style, int size, uint32_t color)
{
    const char *end = text + len;
    while (text < end) {
        uint32_t cp = next_char(&text, end);
        if (style & STYLE_MONO) {
            if (cp < 256) {
                const uint8_t *glyph = style & STYLE_BOLD ? font_bold[cp] : font_regular[cp];
                for (int row = 0; row < FONT_H; row++) {
                    for (int col = 0; col < FONT_W; col++) {
                        if (glyph[row * FONT_W + col])
                            blend(x + col, baseline - 12 + row, color, 255);
                    }
                }
            }
            x += FONT_W;
            continue;
        }
        const ttf_t *face = &faces[style & STYLE_BOLD ? 1 : 0];
        int glyph = ttf_glyph(face, cp);
        ttf_bitmap_t bitmap;
        if (ttf_render(face, glyph, size, style & STYLE_ITALIC, &bitmap)) {
            for (int row = 0; row < bitmap.height; row++) {
                for (int col = 0; col < bitmap.width; col++)
                    blend(x + bitmap.left + col, baseline + bitmap.top + row, color,
                          bitmap.coverage[row * bitmap.width + col]);
            }
            free(bitmap.coverage);
        }
        x += ttf_advance(face, glyph, size);
    }
}

static void draw_page(const page_t *page)
{
    for (int i = 0; i < page->paint_count; i++) {
        const paint_t *item = &page->paint[i];
        clip_x0 = clip_y0 = 0;
        clip_x1 = clip_y1 = 1 << 30;
        if (item->clip > 0 && item->clip <= page->clip_count) {
            const clip_t *c = &page->clips[item->clip - 1];
            clip_x0 = c->x;
            clip_y0 = c->y;
            clip_x1 = c->x + c->w;
            clip_y1 = c->y + c->h;
        }
        if (item->kind == PAINT_BOX) {
            const box_t *b = &page->boxes[item->index];
            if (b->background && b->gradient) {
                uint32_t from = b->background & 0xFFFFFF, to = b->gradient & 0xFFFFFF;
                int steps = b->gradient_side ? b->w : b->h;
                for (int k = 0; k < steps; k++) {
                    int a = steps > 1 ? k * 255 / (steps - 1) : 0;
                    uint32_t color = ((((from >> 16) & 255) * (255 - a) + ((to >> 16) & 255) * a) / 255) << 16 |
                                     ((((from >> 8) & 255) * (255 - a) + ((to >> 8) & 255) * a) / 255) << 8 |
                                     (((from & 255) * (255 - a) + (to & 255) * a) / 255);
                    if (b->gradient_side)
                        fill(b->x + k, b->y, 1, b->h, color);
                    else
                        fill(b->x, b->y + k, b->w, 1, color);
                }
            } else if (b->background) {
                int r = b->radius > b->w / 2 ? b->w / 2 : b->radius;
                if (r > b->h / 2)
                    r = b->h / 2;
                for (int row = 0; row < b->h; row++) {
                    int inset = 0, dy = row < r ? r - row : row >= b->h - r ? row - (b->h - r) + 1 : 0;
                    while (dy && inset < r && (r - inset) * (r - inset) + dy * dy > r * r)
                        inset++;
                    fill(b->x + inset, b->y + row, b->w - 2 * inset, 1, b->background & 0xFFFFFF);
                }
            }
            if (b->picture) {
                /* ein Hintergrundbild: hier nur als getönte Fläche mit Streifen angedeutet */
                for (int row = 0; row < b->h; row++) {
                    for (int col = (row % 12) ; col < b->w; col += 12)
                        blend(b->x + col, b->y + row, 0x3060A0, 90);
                }
            }
            if (b->border[0])
                fill(b->x, b->y, b->w, b->border[0], b->border_color[0]);
            if (b->border[2])
                fill(b->x, b->y + b->h - b->border[2], b->w, b->border[2], b->border_color[2]);
            if (b->border[3])
                fill(b->x, b->y, b->border[3], b->h, b->border_color[3]);
            if (b->border[1])
                fill(b->x + b->w - b->border[1], b->y, b->border[1], b->h, b->border_color[1]);
        } else if (item->kind == PAINT_IMAGE) {
            const page_image_t *im = &page->images[item->index];
            fill(im->x, im->y, im->width, im->height, 0xB8C4D0);
            fill(im->x + 2, im->y + 2, im->width - 4, im->height - 4, 0xDDE4EA);
        } else if (item->kind == PAINT_WIDGET) {
            const widget_t *w = &page->widgets[item->index];
            bool button = w->kind == W_SUBMIT || w->kind == W_BUTTON;
            const char *text = button ? w->label : w->kind == W_SELECT && w->option_count
                                   ? page->options[w->first_option + w->selected].label : w->value;
            if (w->boxed)
                continue;               /* <button>: Die Seite hat ihn samt Inhalt gezeichnet */
            if (w->kind == W_CHECKBOX || w->kind == W_RADIO) {
                fill(w->x + 4, w->y + 3, 13, 13, 0xA0A4AC);
                fill(w->x + 5, w->y + 4, 11, 11, 0xFFFFFF);
                if (w->checked)
                    fill(w->x + 7, w->y + 6, 7, 7, 0x222630);
                continue;
            }
            if (button && w->background) {
                fill(w->x, w->y, w->w, w->h, w->background & 0xFFFFFF);
                int wide = text_width(NULL, text ? text : "", strlen(text ? text : ""), w->bold ? STYLE_BOLD : 0, w->size);
                draw_text(w->x + (w->w - wide) / 2, w->y + (w->h + w->size * 7 / 10) / 2, text ? text : "",
                          strlen(text ? text : ""), w->bold ? STYLE_BOLD : 0, w->size, w->color & 0xFFFFFF);
                continue;
            }
            fill(w->x, w->y, w->w, w->h, button ? 0x3C465C : 0xA0A4AC);
            if (!button)
                fill(w->x + 1, w->y + 1, w->w - 2, w->h - 2, 0xFFFFFF);
            if (!text || !text[0])
                text = button ? "" : w->placeholder ? w->placeholder : "";
            size_t n = strlen(text), room = (size_t)(w->w / FONT_W) - 2;
            draw_text(w->x + (button ? 16 : 8), w->y + (w->h - FONT_H) / 2 + 12, text, n < room ? n : room,
                      STYLE_MONO, 16, button ? 0xF0F2F6 : 0x222630);
        } else if (item->kind == PAINT_LINE) {
            const line_t *line = &page->lines[item->index];
            for (uint32_t k = 0; k < line->count; k++) {
                const span_t *s = &page->spans[line->first + k];
                int baseline = line->y + line->baseline + s->shift;
                uint32_t color = s->color ? s->color & 0xFFFFFF : s->style & STYLE_LINK ? 0x1E5ABE
                               : s->style & STYLE_MUTED ? 0x80848E : 0x222630;
                if (s->background)
                    fill(s->column, baseline - s->size, s->width, s->size + 4, s->background & 0xFFFFFF);
                draw_text(s->column, baseline, page->pool + s->text, s->bytes, s->style, s->size ? s->size : 15, color);
                if (s->style & STYLE_UNDERLINE)
                    fill(s->column, baseline + 2, s->width, 1, color);
            }
        }
    }
}

static void dump(const page_t *page)
{
    printf("Seite: Höhe %d, %d Kästen, %d Zeilen, %d Bilder, %d Felder, %d Links\n", page->height,
           page->box_count, page->line_count, page->image_count, page->widget_count, page->link_count);
    for (int i = 0; i < page->paint_count; i++) {
        const paint_t *item = &page->paint[i];
        if (item->kind == PAINT_BOX) {
            const box_t *b = &page->boxes[item->index];
            printf("Kasten %4d,%4d %4dx%-4d grund %06X rahmen %d/%d/%d/%d rund %d\n", b->x, b->y, b->w, b->h,
                   b->background & 0xFFFFFF, b->border[0], b->border[1], b->border[2], b->border[3], b->radius);
        } else if (item->kind == PAINT_LINE) {
            const line_t *line = &page->lines[item->index];
            printf("Zeile  y %4d h %2d:", line->y, line->height);
            for (uint32_t k = 0; k < line->count; k++) {
                const span_t *s = &page->spans[line->first + k];
                printf(" [%d+%d %s%.*s]", s->column, s->width, s->link >= 0 ? "L:" : "", s->bytes, page->pool + s->text);
            }
            printf("\n");
        } else if (item->kind == PAINT_IMAGE) {
            const page_image_t *im = &page->images[item->index];
            printf("Bild   %4d,%4d %4dx%-4d\n", im->x, im->y, im->width, im->height);
        } else {
            const widget_t *w = &page->widgets[item->index];
            printf("Feld   %4d,%4d %4dx%-4d Art %d\n", w->x, w->y, w->w, w->h, w->kind);
        }
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Aufruf: layout_view seite.html [breite] [bild.ppm]\n");
        return 2;
    }
    int width = argc > 2 ? atoi(argv[2]) : 1262;
    size_t len, n1, n2;
    char *html = slurp(argv[1], &len);
    char *regular = slurp("fonts/DejaVuSans.ttf", &n1), *bold = slurp("fonts/DejaVuSans-Bold.ttf", &n2);
    if (!html || !regular || !bold || !ttf_open(&faces[0], (uint8_t *)regular, n1) ||
        !ttf_open(&faces[1], (uint8_t *)bold, n2)) {
        fprintf(stderr, "Seite oder Schriften (fonts/) nicht lesbar\n");
        return 2;
    }
    static char dir[1024];
    snprintf(dir, sizeof(dir), "%s", argv[1]);
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = 0;
        directory = dir;
    }

    const char *base = getenv("BASE") ? getenv("BASE") : "http://test.local/seite.html";
    css_sheet_t *css = css_create(width);
    css_set_viewport_height(css, 700);
    html_find_styles(html, len, base, style_found, css);
    dom_doc *doc = dom_parse(html, len);
    html_text_t text = { .cell_width = FONT_W, .width = text_width, .base_size = 15,
                         .heading_size = { 26, 21, 17 }, .text_color = 0x222630, .link_color = 0x1E5ABE,
                         .page_color = 0xFCFCFA, .css = css, .scripts = getenv("SKRIPTE") != NULL };
    html_pictures_t pictures = { .cell_width = 1, .line_height = 20, .max_rows = 34, .lookup = lookup };
    layout_options_t options = { .width = width, .height = 700, .base_url = base, .css = css, .text = &text,
                                 .pictures = &pictures, .metrics = metrics, .scripts = text.scripts };
    page_t *page = doc ? layout_document(doc, &options) : NULL;
    if (!page) {
        fprintf(stderr, "Die Seite ließ sich nicht setzen\n");
        return 1;
    }
    if (getenv("ZEILEN"))
        dump(page);
    else
        printf("Höhe %d, %d Kästen, %d Zeilen%s\n", page->height, page->box_count, page->line_count,
               page->failed ? " - UNVOLLSTÄNDIG" : "");
    if (argc > 3) {
        W = width;
        H = page->height < 200 ? 200 : page->height > 12000 ? 12000 : page->height;
        pixels = malloc((size_t)W * H * sizeof(uint32_t));
        uint32_t ground = page->background ? page->background & 0xFFFFFF : 0xFCFCFA;
        for (size_t i = 0; i < (size_t)W * H; i++)
            pixels[i] = ground;
        draw_page(page);
        FILE *out = fopen(argv[3], "wb");
        if (!out)
            return 1;
        fprintf(out, "P6\n%d %d\n255\n", W, H);
        for (size_t i = 0; i < (size_t)W * H; i++) {
            uint8_t rgb[3] = { (uint8_t)(pixels[i] >> 16), (uint8_t)(pixels[i] >> 8), (uint8_t)pixels[i] };
            fwrite(rgb, 1, 3, out);
        }
        fclose(out);
        free(pixels);
    }
    page_free(page);
    dom_free(doc);
    css_free(css);
    free(html);
    free(regular);
    free(bold);
    return 0;
}
