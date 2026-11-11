/*
 * Terminalfenster: eine Konsole (siehe console.h), die Text in ein Fenster
 * zeichnet und Tastendrücke aus dessen Ereignissen liest. Darin läuft die Shell.
 */
#include "apps.h"
#include "console.h"
#include "cpu.h"
#include "gui.h"
#include "input.h"
#include "memory.h"
#include "shell.h"
#include "string.h"
#include "task.h"

#define COLS    80
#define ROWS    25
#define PADDING 6

#define C_BACKGROUND RGB(16, 18, 24)
#define C_CURSOR     RGB(170, 176, 188)

static const color_t palette[COLOR_COUNT] = {
    [COLOR_DEFAULT] = RGB(204, 208, 216),
    [COLOR_GREEN] = RGB(126, 214, 126),
    [COLOR_CYAN] = RGB(110, 200, 232),
    [COLOR_RED] = RGB(240, 104, 104),
    [COLOR_YELLOW] = RGB(238, 208, 110),
    [COLOR_WHITE] = RGB(255, 255, 255),
};

typedef struct {
    console_t console;                  /* muss das erste Feld sein */
    window_t *window;
    surface_t *surface;
    int col, row;                       /* Position der Schreibmarke */
    console_color_t color;
    bool closed;
    int utf_pending;                    /* noch erwartete UTF-8-Folgebytes */
    uint32_t utf_codepoint;
    struct {
        uint8_t ch;
        uint8_t color;
    } cells[ROWS][COLS];
} terminal_t;

static void draw_cell(terminal_t *t, int col, int row, bool cursor)
{
    int x = PADDING + col * FONT_W;
    int y = PADDING + row * FONT_H;
    uint8_t ch = t->cells[row][col].ch;
    color_t fg = palette[t->cells[row][col].color];

    gfx_fill(t->surface, x, y, FONT_W, FONT_H, cursor ? C_CURSOR : C_BACKGROUND);
    if (ch > ' ')
        gfx_glyph(t->surface, x, y, ch, cursor ? C_BACKGROUND : fg, false);
    window_invalidate(t->window, x, y, FONT_W, FONT_H);
}

static void scroll(terminal_t *t)
{
    memmove(t->cells[0], t->cells[1], sizeof(t->cells[0]) * (ROWS - 1));
    memset(t->cells[ROWS - 1], 0, sizeof(t->cells[0]));

    /* Die Pixel ebenfalls verschieben - schneller, als alles neu zu zeichnen */
    surface_t *s = t->surface;
    uint32_t *top = s->pixels + (size_t)PADDING * s->stride;
    memmove(top, top + (size_t)FONT_H * s->stride,
            (size_t)(ROWS - 1) * FONT_H * s->stride * sizeof(uint32_t));
    gfx_fill(s, 0, PADDING + (ROWS - 1) * FONT_H, s->width, FONT_H,
             C_BACKGROUND);
    window_invalidate(t->window, 0, 0, s->width, s->height);
}

static void newline(terminal_t *t)
{
    t->col = 0;
    if (++t->row == ROWS) {
        t->row = ROWS - 1;
        scroll(t);
    }
}

static void put_codepoint(terminal_t *t, uint32_t cp)
{
    draw_cell(t, t->col, t->row, false);        /* Schreibmarke entfernen */

    switch (cp) {
    case '\n':
        newline(t);
        break;
    case '\r':
        t->col = 0;
        break;
    case '\t':
        do {
            put_codepoint(t, ' ');
        } while (t->col % 8 != 0);
        return;
    case '\b':
        if (t->col > 0) {
            t->col--;
        } else if (t->row > 0) {
            t->row--;
            t->col = COLS - 1;
        }
        t->cells[t->row][t->col].ch = 0;
        break;
    default:
        t->cells[t->row][t->col].ch = cp <= 0xFF ? (uint8_t)cp : '?';
        t->cells[t->row][t->col].color = (uint8_t)t->color;
        draw_cell(t, t->col, t->row, false);
        if (++t->col == COLS)
            newline(t);
        break;
    }
    draw_cell(t, t->col, t->row, true);
}

/* --- Konsolen-Schnittstelle ---------------------------------------------------- */

static void term_putc(console_t *self, char c)
{
    terminal_t *t = (terminal_t *)self;
    uint8_t b = (uint8_t)c;

    /* Mehrere Tasks können auf dasselbe Terminal schreiben: kurz sperren. */
    uint64_t flags = irq_save();
    if (t->utf_pending > 0 && (b & 0xC0) == 0x80) {
        t->utf_codepoint = (t->utf_codepoint << 6) | (b & 0x3F);
        if (--t->utf_pending == 0)
            put_codepoint(t, t->utf_codepoint);
    } else if (b >= 0xF0) {
        t->utf_pending = 3;
        t->utf_codepoint = b & 0x07;
    } else if (b >= 0xE0) {
        t->utf_pending = 2;
        t->utf_codepoint = b & 0x0F;
    } else if (b >= 0xC0) {
        t->utf_pending = 1;
        t->utf_codepoint = b & 0x1F;
    } else {
        t->utf_pending = 0;
        put_codepoint(t, b < 0x80 ? b : '?');
    }
    irq_restore(flags);
}

static void term_set_color(console_t *self, console_color_t color)
{
    ((terminal_t *)self)->color = color;
}

static void term_clear(console_t *self)
{
    terminal_t *t = (terminal_t *)self;
    uint64_t flags = irq_save();
    memset(t->cells, 0, sizeof(t->cells));
    t->col = 0;
    t->row = 0;
    gfx_fill(t->surface, 0, 0, t->surface->width, t->surface->height,
             C_BACKGROUND);
    draw_cell(t, 0, 0, true);
    window_invalidate(t->window, 0, 0, t->surface->width, t->surface->height);
    irq_restore(flags);
}

static int term_getchar(console_t *self)
{
    terminal_t *t = (terminal_t *)self;
    while (!t->closed) {
        event_t ev;
        window_get_event(t->window, &ev, 0);
        if (ev.type == EV_CLOSE)
            t->closed = true;
        else if (ev.type == EV_KEY && !(ev.mods & MOD_ALT))
            return ev.key;
    }
    return -1;
}

/* Wird aufgerufen, wenn kein Task mehr auf dieses Terminal schreibt. */
static void term_release(console_t *self)
{
    terminal_t *t = (terminal_t *)self;
    window_unref(t->window);
    kfree(t);
}

void terminal_app(void *arg)
{
    (void)arg;
    terminal_t *t = kzalloc(sizeof(*t));
    if (!t)
        return;
    t->window = window_create("Terminal", COLS * FONT_W + 2 * PADDING,
                              ROWS * FONT_H + 2 * PADDING);
    if (!t->window) {
        kfree(t);
        return;
    }
    /*
     * Das Fenster gehört ab jetzt der Konsole: Es wird erst freigegeben,
     * wenn auch der letzte Task fertig ist, der hierhin schreibt.
     */
    window_keep(t->window);
    t->surface = window_surface(t->window);
    t->console.putc = term_putc;
    t->console.set_color = term_set_color;
    t->console.clear = term_clear;
    t->console.getchar = term_getchar;
    t->console.release = term_release;
    term_clear(&t->console);

    task_set_console(&t->console);
    shell_run();
    window_close(t->window);
}
