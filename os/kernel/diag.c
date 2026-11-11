/*
 * Diagnosefenster: zeigt das Startprotokoll - die Meldungen der Treiber.
 *
 * Es öffnet sich von selbst, wenn HelloOS nach dem Start weder Tastatur noch
 * Maus findet: Dann lässt sich nichts bedienen, aber ein Foto dieses Fensters
 * zeigt, was an den USB-Anschlüssen erkannt wurde und woran es hakt. Mit
 * funktionierender Tastatur zeigt der Befehl "log" dasselbe im Terminal.
 */
#include "apps.h"
#include "console.h"
#include "gui.h"
#include "memory.h"
#include "shell.h"
#include "string.h"
#include "usb.h"

#define C_BACKGROUND RGB(250, 250, 252)
#define C_TEXT       RGB(34, 38, 48)
#define C_MUTED      RGB(110, 116, 130)
#define C_ACCENT     RGB(176, 60, 40)
#define C_USB        RGB(20, 70, 150)

#define MARGIN 12
#define HEAD   (3 * FONT_H + 16)

static bool is_usb(const char *line)
{
    return line[0] == 'U' && line[1] == 'S' && line[2] == 'B';
}

static void draw(window_t *win, bool no_input)
{
    surface_t *s = window_surface(win);
    int width = s->width, height = s->height;
    gfx_fill(s, 0, 0, width, height, C_BACKGROUND);

    int y = MARGIN;
    if (no_input) {
        gfx_text(s, MARGIN, y, "Keine Tastatur und keine Maus gefunden.",
                 C_ACCENT, true);
        gfx_text(s, MARGIN, y + FONT_H + 2,
                 "Bitte fotografiere dieses Fenster - es zeigt, was HelloOS "
                 "an den USB-Anschlüssen sieht.", C_TEXT, false);
    } else {
        gfx_text(s, MARGIN, y, "Startprotokoll", C_TEXT, true);
        gfx_text(s, MARGIN, y + FONT_H + 2,
                 "Meldungen der Treiber seit dem Start; USB-Meldungen stehen oben.",
                 C_TEXT, false);
    }
    gfx_text(s, MARGIN, y + 2 * FONT_H + 4,
             OS_NAME " " OS_VERSION " - das Fenster aktualisiert sich von selbst.",
             C_MUTED, false);

    int rows = (height - HEAD - MARGIN) / FONT_H;
    int columns = (width - 2 * MARGIN) / FONT_W;
    if (columns > KLOG_WIDTH - 1)
        columns = KLOG_WIDTH - 1;
    int count = klog_count();
    char line[KLOG_WIDTH];

    /* Passt nicht alles, haben die USB-Meldungen Vorrang, dann die neuesten. */
    int usb_lines = 0;
    for (int i = 0; i < count; i++) {
        klog_line(i, line, sizeof(line));
        if (is_usb(line))
            usb_lines++;
    }
    int skip_usb = usb_lines > rows ? usb_lines - rows : 0;
    int other_rows = rows - (usb_lines - skip_usb);
    int skip_other = count - usb_lines > other_rows ? count - usb_lines - other_rows : 0;

    y = HEAD;
    for (int pass = 0; pass < 2; pass++) {
        int skip = pass == 0 ? skip_usb : skip_other;
        for (int i = 0; i < count; i++) {
            klog_line(i, line, sizeof(line));
            if (is_usb(line) != (pass == 0))
                continue;
            if (skip > 0) {
                skip--;
                continue;
            }
            line[columns] = '\0';
            gfx_text(s, MARGIN, y, line, pass == 0 ? C_USB : C_TEXT, false);
            y += FONT_H;
        }
    }
    if (usb_lines == 0 && rows > 0)
        gfx_text(s, MARGIN, y, "(noch keine Meldung eines USB-Controllers)",
                 C_USB, false);
    window_invalidate(win, 0, 0, width, height);
}

void diagnose_app(void *arg)
{
    bool no_input = arg != NULL;        /* von selbst geöffnet */
    kfree(arg);

    int screen_w, screen_h;
    gui_screen_size(&screen_w, &screen_h);
    int width = screen_w - 80, height = screen_h - 140;
    if (width > 1040)
        width = 1040;
    if (height > 900)
        height = 900;
    window_t *win = window_create(no_input ? "Diagnose: keine Eingabegeräte"
                                           : "Startprotokoll", width, height);
    if (!win)
        return;
    window_set_resizable(win, 480, 240);

    unsigned shown = 0;
    bool redraw = true;
    for (;;) {
        /* Sobald Geräte da sind, ändert sich die Überschrift. */
        if (no_input && usb_input_devices() > 0) {
            no_input = false;
            redraw = true;
        }
        if (redraw || klog_total() != shown) {
            shown = klog_total();
            redraw = false;
            draw(win, no_input);
        }

        event_t ev;
        if (!window_get_event(win, &ev, 1000))
            continue;
        if (ev.type == EV_CLOSE || (ev.type == EV_KEY && ev.key == 27))
            break;
        if (ev.type == EV_RESIZE && window_resize(win, ev.x, ev.y))
            redraw = true;
    }
    window_destroy(win);
}
