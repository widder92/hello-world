/* Malprogramm: mit gedrückter Maustaste zeichnen, oben Farbe und Stift wählen */
#include "apps.h"
#include "gui.h"
#include "input.h"

#define WIDTH     520
#define HEIGHT    380
#define TOOLBAR_H 40
#define SWATCH    24

#define C_TOOLBAR RGB(226, 229, 235)
#define C_CANVAS  RGB(255, 255, 255)
#define C_LINE    RGB(170, 176, 188)
#define C_SELECT  RGB(34, 38, 48)

static const color_t colors[] = {
    RGB(24, 26, 32), RGB(214, 64, 64), RGB(236, 150, 52), RGB(240, 206, 70),
    RGB(84, 176, 96), RGB(60, 130, 214), RGB(140, 92, 200), RGB(255, 255, 255),
};
#define COLOR_SLOTS ((int)(sizeof(colors) / sizeof(colors[0])))

static const int pens[] = { 2, 5, 10 };
#define PEN_SLOTS ((int)(sizeof(pens) / sizeof(pens[0])))

static rect_t color_rect(int i)
{
    return rect_make(10 + i * (SWATCH + 6), (TOOLBAR_H - SWATCH) / 2, SWATCH,
                     SWATCH);
}

static rect_t pen_rect(int i)
{
    rect_t last = color_rect(COLOR_SLOTS - 1);
    return rect_make(last.x + last.w + 20 + i * (SWATCH + 6), last.y, SWATCH,
                     SWATCH);
}

static rect_t clear_rect(void)
{
    return rect_make(WIDTH - 10 - 72, (TOOLBAR_H - SWATCH) / 2, 72, SWATCH);
}

static void draw_toolbar(surface_t *s, int color, int pen)
{
    gfx_reset_clip(s);
    gfx_fill(s, 0, 0, WIDTH, TOOLBAR_H, C_TOOLBAR);
    gfx_fill(s, 0, TOOLBAR_H - 1, WIDTH, 1, C_LINE);

    for (int i = 0; i < COLOR_SLOTS; i++) {
        rect_t r = color_rect(i);
        gfx_fill(s, r.x, r.y, r.w, r.h, colors[i]);
        gfx_frame(s, r.x, r.y, r.w, r.h, C_LINE);
        if (i == color)
            gfx_frame(s, r.x - 2, r.y - 2, r.w + 4, r.h + 4, C_SELECT);
    }
    for (int i = 0; i < PEN_SLOTS; i++) {
        rect_t r = pen_rect(i);
        gfx_fill(s, r.x, r.y, r.w, r.h, C_CANVAS);
        gfx_frame(s, r.x, r.y, r.w, r.h, C_LINE);
        int d = pens[i];
        gfx_fill(s, r.x + (r.w - d) / 2, r.y + (r.h - d) / 2, d, d, C_SELECT);
        if (i == pen)
            gfx_frame(s, r.x - 2, r.y - 2, r.w + 4, r.h + 4, C_SELECT);
    }
    ui_button(s, clear_rect(), "Leeren", false);
}

void paint_app(void *arg)
{
    (void)arg;
    window_t *win = window_create("Malen", WIDTH, HEIGHT);
    if (!win)
        return;
    surface_t *s = window_surface(win);

    const rect_t canvas = rect_make(0, TOOLBAR_H, WIDTH, HEIGHT - TOOLBAR_H);
    int color = 0, pen = 1;
    bool drawing = false;
    int last_x = 0, last_y = 0;

    gfx_fill(s, canvas.x, canvas.y, canvas.w, canvas.h, C_CANVAS);
    draw_toolbar(s, color, pen);
    window_invalidate(win, 0, 0, WIDTH, HEIGHT);

    for (;;) {
        event_t ev;
        window_get_event(win, &ev, 0);

        if (ev.type == EV_CLOSE)
            break;

        if (ev.type == EV_MOUSE_DOWN && ev.y < TOOLBAR_H) {
            for (int i = 0; i < COLOR_SLOTS; i++) {
                if (rect_contains(color_rect(i), ev.x, ev.y))
                    color = i;
            }
            for (int i = 0; i < PEN_SLOTS; i++) {
                if (rect_contains(pen_rect(i), ev.x, ev.y))
                    pen = i;
            }
            if (rect_contains(clear_rect(), ev.x, ev.y))
                gfx_fill(s, canvas.x, canvas.y, canvas.w, canvas.h, C_CANVAS);
            draw_toolbar(s, color, pen);
            window_invalidate(win, 0, 0, WIDTH, HEIGHT);
            continue;
        }

        if (ev.type == EV_MOUSE_DOWN) {
            drawing = true;
            last_x = ev.x;
            last_y = ev.y;
        }
        if (ev.type == EV_MOUSE_UP)
            drawing = false;

        if (drawing && (ev.type == EV_MOUSE_DOWN || ev.type == EV_MOUSE_MOVE)) {
            int d = pens[pen];
            gfx_set_clip(s, canvas);    /* nicht über die Werkzeugleiste malen */
            gfx_line(s, last_x, last_y, ev.x, ev.y, d, colors[color]);
            gfx_reset_clip(s);

            /* Nur das Rechteck um die neue Linie neu darstellen */
            int x0 = last_x < ev.x ? last_x : ev.x;
            int y0 = last_y < ev.y ? last_y : ev.y;
            int x1 = last_x > ev.x ? last_x : ev.x;
            int y1 = last_y > ev.y ? last_y : ev.y;
            window_invalidate(win, x0 - d, y0 - d, x1 - x0 + 2 * d + 1,
                              y1 - y0 + 2 * d + 1);
            last_x = ev.x;
            last_y = ev.y;
        }
    }
    window_destroy(win);
}
