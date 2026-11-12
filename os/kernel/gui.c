#include "gui.h"
#include "apps.h"
#include "bootinfo.h"
#include "console.h"
#include "cpu.h"
#include "input.h"
#include "memory.h"
#include "serial.h"
#include "sound.h"
#include "shell.h"
#include "string.h"
#include "task.h"
#include "usb.h"
#include "time.h"
#include "update.h"

#define MAX_WINDOWS   16
#define MAX_DIRTY     16
#define RETIRED_MAX   4
#define GRIP          16                /* Griff zum Ziehen in der rechten unteren Ecke */
#define EVENT_QUEUE   64
#define RAW_QUEUE     256

#define TITLE_H       26
#define BORDER        1
#define SHADOW        5
#define TASKBAR_H     36
#define BUTTON_H      26

/* Farben der Oberfläche */
#define C_DESKTOP_TOP    RGB(22, 38, 70)
#define C_DESKTOP_BOTTOM RGB(48, 112, 140)
#define C_WATERMARK      RGB(66, 132, 158)
#define C_TASKBAR        RGB(22, 26, 34)
#define C_TASKBAR_LINE   RGB(60, 68, 84)
#define C_BUTTON         RGB(44, 52, 68)
#define C_BUTTON_ACTIVE  RGB(58, 106, 190)
#define C_BUTTON_BORDER  RGB(78, 88, 108)
#define C_TEXT_LIGHT     RGB(228, 232, 240)
#define C_BORDER         RGB(14, 16, 22)
#define C_TITLE_TOP      RGB(64, 118, 208)
#define C_TITLE_BOTTOM   RGB(40, 82, 160)
#define C_TITLE_OFF_TOP  RGB(98, 104, 116)
#define C_TITLE_OFF_BOT  RGB(76, 82, 94)
#define C_CLOSE          RGB(214, 72, 72)
#define C_CLOSE_OFF      RGB(120, 126, 138)

struct window {
    int id;
    char title[40];
    int x, y;                           /* Rahmen: linke obere Ecke */
    surface_t *surface;                 /* Inhalt */
    uint32_t owner;                     /* PID der Anwendung */
    int refs;
    bool close_requested;
    bool auto_release;                  /* Referenz des Erzeugers verfällt,
                                           wenn dessen Task endet */

    bool resizable;
    int min_width, min_height;
    surface_t *retired[RETIRED_MAX];    /* abgelöste Flächen; gibt der Compositor frei */
    bool maximized;
    rect_t restore;                     /* Lage und Inhaltsgröße vor dem Maximieren */
    bool move_pending;                  /* neue Lage, die mit der neuen Größe gilt */
    int pending_x, pending_y;

    event_t events[EVENT_QUEUE];
    unsigned ev_head, ev_tail;
    uint32_t waiter;                    /* Task, der auf Ereignisse wartet */
};

/* Rohdaten der Eingabegeräte, bevor die Fensterverwaltung sie verteilt */
typedef struct {
    enum { RAW_KEY, RAW_MOUSE, RAW_POINTER, RAW_WHEEL } type;
    int a, b, c;
} raw_event_t;

static surface_t screen;                /* der echte Bildspeicher */
static surface_t back;                  /* hier entsteht jedes Bild zuerst */
static bool screen_bgr;
static bool running;

static window_t *zorder[MAX_WINDOWS];   /* von hinten nach vorn */
static int window_count;
static int next_window_id = 1;

static rect_t dirty[MAX_DIRTY];
static int dirty_count;

static raw_event_t raw[RAW_QUEUE];
static unsigned raw_head, raw_tail;

static uint32_t gui_pid;
static int mouse_x, mouse_y, mouse_buttons;
static window_t *drag_win;              /* Fenster wird gerade verschoben */
static int drag_dx, drag_dy;
static window_t *capture_win;           /* Fenster, in dem die Maus gedrückt wurde */
static window_t *resize_win;            /* Fenster, dessen Größe gerade gezogen wird */
static int resize_w, resize_h;          /* gewünschte Inhaltsgröße */
static int resize_dx, resize_dy;
static char clock_text[32];

/* --- Mauszeiger ----------------------------------------------------------------- */

#define CURSOR_W 12
#define CURSOR_H 19

static const char *const cursor_shape[CURSOR_H] = {
    "#           ",
    "##          ",
    "#.#         ",
    "#..#        ",
    "#...#       ",
    "#....#      ",
    "#.....#     ",
    "#......#    ",
    "#.......#   ",
    "#........#  ",
    "#.........# ",
    "#..........#",
    "#......#####",
    "#...#..#    ",
    "#..# #..#   ",
    "#.#  #..#   ",
    "##    #..#  ",
    "#     #..#  ",
    "       ##   ",
};

static void draw_cursor(void)
{
    for (int y = 0; y < CURSOR_H; y++) {
        for (int x = 0; x < CURSOR_W; x++) {
            char c = cursor_shape[y][x];
            if (c == '#')
                gfx_fill(&back, mouse_x + x, mouse_y + y, 1, 1, RGB(0, 0, 0));
            else if (c == '.')
                gfx_fill(&back, mouse_x + x, mouse_y + y, 1, 1,
                         RGB(255, 255, 255));
        }
    }
}

static rect_t cursor_rect(void)
{
    return rect_make(mouse_x, mouse_y, CURSOR_W, CURSOR_H);
}

/* --- Geänderte Bereiche ---------------------------------------------------------- */

static void wake_gui(void)
{
    if (gui_pid)
        task_wake(gui_pid);
}

static void add_dirty(rect_t r)
{
    r = rect_intersect(r, rect_make(0, 0, screen.width, screen.height));
    if (rect_empty(r))
        return;

    uint64_t flags = irq_save();
    bool merged = false;
    for (int i = 0; i < dirty_count && !merged; i++) {
        /* Überlappende oder fast benachbarte Bereiche zusammenfassen */
        rect_t grown = rect_make(dirty[i].x - 8, dirty[i].y - 8,
                                 dirty[i].w + 16, dirty[i].h + 16);
        if (!rect_empty(rect_intersect(grown, r))) {
            dirty[i] = rect_union(dirty[i], r);
            merged = true;
        }
    }
    if (!merged) {
        if (dirty_count < MAX_DIRTY) {
            dirty[dirty_count++] = r;
        } else {
            for (int i = 1; i < dirty_count; i++)
                dirty[0] = rect_union(dirty[0], dirty[i]);
            dirty[0] = rect_union(dirty[0], r);
            dirty_count = 1;
        }
    }
    irq_restore(flags);
    wake_gui();
}

/* --- Fenster ---------------------------------------------------------------------- */

static rect_t frame_rect(const window_t *w)
{
    return rect_make(w->x, w->y, w->surface->width + 2 * BORDER,
                     w->surface->height + TITLE_H + 2 * BORDER);
}

/* Rahmen samt Schatten - der Bereich, der beim Verschieben neu entsteht. */
static rect_t damage_rect(const window_t *w)
{
    rect_t r = frame_rect(w);
    r.w += SHADOW;
    r.h += SHADOW;
    return r;
}

static rect_t client_rect(const window_t *w)
{
    return rect_make(w->x + BORDER, w->y + BORDER + TITLE_H, w->surface->width,
                     w->surface->height);
}

static rect_t close_rect(const window_t *w)
{
    rect_t f = frame_rect(w);
    return rect_make(f.x + f.w - BORDER - 23, f.y + BORDER + 4, 18, 18);
}

/* Knopf zum Maximieren - nur bei Fenstern mit veränderbarer Größe */
static rect_t maximize_rect(const window_t *w)
{
    rect_t c = close_rect(w);
    return rect_make(c.x - 22, c.y, 18, 18);
}

static rect_t grip_rect(const window_t *w)
{
    rect_t f = frame_rect(w);
    return rect_make(f.x + f.w - GRIP, f.y + f.h - GRIP, GRIP, GRIP);
}

/* Der Rahmen, der beim Ziehen die künftige Größe anzeigt */
static rect_t outline_rect(void)
{
    return rect_make(resize_win->x, resize_win->y, resize_w + 2 * BORDER,
                     resize_h + TITLE_H + 2 * BORDER);
}

/* Meldet nur die vier Kanten eines Rechtecks als geändert. */
static void add_dirty_outline(rect_t r)
{
    add_dirty(rect_make(r.x, r.y, r.w, 3));
    add_dirty(rect_make(r.x, r.y + r.h - 3, r.w, 3));
    add_dirty(rect_make(r.x, r.y, 3, r.h));
    add_dirty(rect_make(r.x + r.w - 3, r.y, 3, r.h));
}

void window_set_resizable(window_t *win, int min_width, int min_height)
{
    win->min_width = min_width;
    win->min_height = min_height;
    win->resizable = true;
    add_dirty(damage_rect(win));
}

surface_t *window_resize(window_t *win, int width, int height)
{
    if (width < 1 || height < 1 || width > screen.width || height > screen.height)
        return NULL;
    surface_t *fresh = surface_create(width, height);
    if (!fresh)
        return NULL;

    uint64_t flags = irq_save();
    int slot = 0;
    while (slot < RETIRED_MAX && win->retired[slot])
        slot++;
    if (slot == RETIRED_MAX || win->close_requested) {
        irq_restore(flags);
        surface_free(fresh);
        return NULL;
    }
    rect_t before = damage_rect(win);
    /* Die alte Fläche kann gerade auf den Bildschirm kopiert werden - freigeben
     * darf sie deshalb nur der Compositor selbst, wenn er damit fertig ist. */
    win->retired[slot] = win->surface;
    win->surface = fresh;
    if (win->move_pending) {
        win->x = win->pending_x;
        win->y = win->pending_y;
        win->move_pending = false;
    }
    rect_t after = damage_rect(win);
    irq_restore(flags);

    add_dirty(before);
    add_dirty(after);
    return fresh;
}

static void free_retired(void)
{
    for (int i = 0; i < window_count; i++) {
        window_t *w = zorder[i];
        for (int slot = 0; slot < RETIRED_MAX; slot++) {
            uint64_t flags = irq_save();
            surface_t *old = w->retired[slot];
            w->retired[slot] = NULL;
            irq_restore(flags);
            surface_free(old);
        }
    }
}

window_t *window_create(const char *title, int width, int height)
{
    if (!running)
        return NULL;

    window_t *w = kzalloc(sizeof(*w));
    surface_t *surface = surface_create(width, height);
    if (!w || !surface) {
        kfree(w);
        surface_free(surface);
        return NULL;
    }
    w->surface = surface;
    w->owner = task_current_pid();
    w->refs = 2;                        /* Erzeuger + Fensterliste */
    w->auto_release = true;
    strlcpy(w->title, title, sizeof(w->title));

    uint64_t flags = irq_save();
    if (window_count == MAX_WINDOWS) {
        irq_restore(flags);
        surface_free(surface);
        kfree(w);
        return NULL;
    }
    w->id = next_window_id++;

    /* Neue Fenster versetzt anordnen, damit sie sich nicht verdecken */
    rect_t f = frame_rect(w);
    int room_x = screen.width - f.w - 64;
    int room_y = screen.height - TASKBAR_H - f.h - 48;
    int step = w->id - 1;
    w->x = 32 + (room_x > 0 ? step * 190 % room_x : 0);
    w->y = 24 + (room_y > 0 ? step * 64 % room_y : 0);

    rect_t previous = window_count ? damage_rect(zorder[window_count - 1])
                                   : rect_make(0, 0, 0, 0);
    zorder[window_count++] = w;         /* ganz nach vorn */
    irq_restore(flags);

    add_dirty(previous);                /* dessen Titelleiste wird inaktiv */
    add_dirty(damage_rect(w));
    add_dirty(rect_make(0, screen.height - TASKBAR_H, screen.width, TASKBAR_H));
    return w;
}

surface_t *window_surface(window_t *win)
{
    return win->surface;
}

void window_close(window_t *win)
{
    win->close_requested = true;
    wake_gui();
}

void window_unref(window_t *win)
{
    uint64_t flags = irq_save();
    bool last = --win->refs == 0;
    irq_restore(flags);
    if (last) {
        for (int slot = 0; slot < RETIRED_MAX; slot++)
            surface_free(win->retired[slot]);
        surface_free(win->surface);
        kfree(win);
    }
}

void window_keep(window_t *win)
{
    win->auto_release = false;
}

void window_destroy(window_t *win)
{
    win->auto_release = false;
    window_close(win);
    window_unref(win);
}

void window_invalidate(window_t *win, int x, int y, int w, int h)
{
    uint64_t flags = irq_save();
    rect_t r = rect_make(0, 0, 0, 0);
    if (!win->close_requested) {
        rect_t c = client_rect(win);
        r = rect_intersect(rect_make(c.x + x, c.y + y, w, h), c);
    }
    irq_restore(flags);
    add_dirty(r);
}

static void post_event(window_t *w, const event_t *ev)
{
    uint64_t flags = irq_save();
    unsigned next = (w->ev_head + 1) % EVENT_QUEUE;

    /* Aufeinanderfolgende Mausbewegungen zu einer zusammenfassen */
    unsigned last = (w->ev_head + EVENT_QUEUE - 1) % EVENT_QUEUE;
    if (ev->type == EV_MOUSE_MOVE && w->ev_head != w->ev_tail &&
        w->events[last].type == EV_MOUSE_MOVE) {
        w->events[last] = *ev;
    } else if (next != w->ev_tail) {
        w->events[w->ev_head] = *ev;
        w->ev_head = next;
    }
    if (w->waiter) {
        task_wake(w->waiter);
        w->waiter = 0;
    }
    irq_restore(flags);
}

bool window_get_event(window_t *win, event_t *ev, uint32_t timeout_ms)
{
    uint64_t deadline = time_ms() + timeout_ms;
    for (;;) {
        uint64_t flags = irq_save();
        if (win->ev_head != win->ev_tail) {
            *ev = win->events[win->ev_tail];
            win->ev_tail = (win->ev_tail + 1) % EVENT_QUEUE;
            irq_restore(flags);
            return true;
        }
        uint64_t now = time_ms();
        if (timeout_ms && now >= deadline) {
            irq_restore(flags);
            return false;
        }
        win->waiter = task_current_pid();
        task_block(timeout_ms ? (uint32_t)(deadline - now) : 0);
        irq_restore(flags);
    }
}

/* --- Bedienelemente ---------------------------------------------------------------- */

void ui_button(surface_t *s, rect_t r, const char *label, bool highlighted)
{
    gfx_fill(s, r.x, r.y, r.w, r.h, highlighted ? C_BUTTON_ACTIVE : C_BUTTON);
    gfx_frame(s, r.x, r.y, r.w, r.h, C_BUTTON_BORDER);
    int tx = r.x + (r.w - gfx_text_width(label)) / 2;
    int ty = r.y + (r.h - FONT_H) / 2;
    gfx_text(s, tx, ty, label, C_TEXT_LIGHT, false);
}

/* --- Taskleiste ---------------------------------------------------------------------- */

/* Die Programme; die Reihenfolge bestimmt die Funktionstasten F1, F2, ... */
static const struct {
    const char *label;                  /* im Startmenü */
    const char *app;
    color_t color;                      /* Farbe des kleinen Symbols */
    char letter;                        /* ... und der Buchstabe darin */
} launchers[] = {
    { "Terminal", "terminal", RGB(70, 76, 90), '>' },
    { "Systemmonitor", "monitor", RGB(46, 140, 96), 'S' },
    { "Malen", "malen", RGB(204, 108, 52), 'M' },
    { "Über HelloOS", "info", RGB(90, 110, 150), 'i' },
    { "Claude", "claude", RGB(190, 92, 58), 'C' },
    { "Texteditor", "editor", RGB(64, 118, 208), 'E' },
    { "Dateien übertragen", "transfer", RGB(140, 96, 180), 'D' },
    { "Browser", "browser", RGB(36, 132, 170), 'B' },
};
#define LAUNCHER_COUNT ((int)(sizeof(launchers) / sizeof(launchers[0])))

/* Reihenfolge im Startmenü: das Wichtigste oben */
static const int menu_order[] = { 7, 6, 5, 0, 4, 2, 1, 3 };

#define WINDOW_BUTTON_W 132

static rect_t taskbar_rect(void)
{
    return rect_make(0, screen.height - TASKBAR_H, screen.width, TASKBAR_H);
}

static rect_t start_rect(void)
{
    return rect_make(6, screen.height - TASKBAR_H + (TASKBAR_H - BUTTON_H) / 2,
                     gfx_text_width("Start") + 44, BUTTON_H);
}

static rect_t power_rect(void)
{
    return rect_make(screen.width - 8 - 44,
                     screen.height - TASKBAR_H + (TASKBAR_H - BUTTON_H) / 2, 44,
                     BUTTON_H);
}

static rect_t clock_rect(void)
{
    rect_t p = power_rect();
    int w = 20 * FONT_W + 16;
    return rect_make(p.x - 8 - w, p.y, w, BUTTON_H);
}

/* Solange ein geprüftes Update auf Zustimmung wartet, gibt es einen Knopf. */
#define UPDATE_LABEL "Update verfügbar"

static bool update_waiting(void)
{
    update_info_t info;
    update_get_info(&info);
    return info.available_build != 0;
}

/* Lautstärke: ein Knopf, der bei jedem Klick eine Stufe weiterschaltet (ohne Soundkarte: nichts) */
static rect_t volume_rect(void)
{
    rect_t clock = clock_rect();
    int w = sound_available() ? 9 * FONT_W + 16 : 0;
    return rect_make(clock.x - (w ? 8 + w : 0), clock.y, w, BUTTON_H);
}

static rect_t update_rect(void)
{
    rect_t volume = volume_rect();
    int w = gfx_text_width(UPDATE_LABEL) + 20;
    return rect_make(volume.x - 8 - w, volume.y, w, BUTTON_H);
}

/* Fenster in der Reihenfolge, in der sie geöffnet wurden. */
static int windows_by_age(window_t **out)
{
    uint64_t flags = irq_save();
    int n = window_count;
    for (int i = 0; i < n; i++) {
        window_t *w = zorder[i];
        int k = i;
        for (; k > 0 && out[k - 1]->id > w->id; k--)
            out[k] = out[k - 1];
        out[k] = w;
    }
    irq_restore(flags);
    return n;
}

static rect_t window_button_rect(int i)
{
    rect_t start = start_rect();
    int x = start.x + start.w + 20 + i * (WINDOW_BUTTON_W + 6);
    return rect_make(x, start.y, WINDOW_BUTTON_W, BUTTON_H);
}

/* --- Startmenü ----------------------------------------------------------------------- */

#define MENU_W       268
#define MENU_HEADER  40
#define MENU_ROW     30
#define MENU_GAP     9                  /* Trennlinie vor "Update" und "Ausschalten" */
#define C_MENU       RGB(30, 35, 46)
#define C_MENU_MUTED RGB(140, 148, 164)

static bool menu_open;
static int menu_selected = -1;          /* Zeile unter Maus oder Tastatur, -1: keine */

/* Zeilen: erst die Programme, dann (falls vorhanden) das Update, zuletzt Ausschalten. */
static int menu_rows(void)
{
    return LAUNCHER_COUNT + (update_waiting() ? 1 : 0) + 1;
}

/* Die Fläche des Menüs - mit Platz für die Update-Zeile, ob sie da ist oder nicht. */
static rect_t menu_rect(void)
{
    int h = MENU_HEADER + (LAUNCHER_COUNT + 2) * MENU_ROW + MENU_GAP + 6;
    return rect_make(4, screen.height - TASKBAR_H - h - 2, MENU_W, h);
}

static rect_t menu_row_rect(int row)
{
    rect_t m = menu_rect();
    int y = m.y + m.h - 6 - (menu_rows() - row) * MENU_ROW;
    if (row < LAUNCHER_COUNT)
        y -= MENU_GAP;
    return rect_make(m.x + 4, y, m.w - 8, MENU_ROW);
}

static int menu_row_at(int x, int y)
{
    for (int row = 0; row < menu_rows(); row++) {
        if (rect_contains(menu_row_rect(row), x, y))
            return row;
    }
    return -1;
}

static void menu_set_open(bool open)
{
    if (menu_open == open)
        return;
    menu_open = open;
    menu_selected = -1;
    add_dirty(menu_rect());
    add_dirty(taskbar_rect());
}

static void menu_select(int row)
{
    if (row != menu_selected) {
        menu_selected = row;
        add_dirty(menu_rect());
    }
}

static void menu_activate(int row)
{
    menu_set_open(false);
    if (row < 0)
        return;
    if (row < LAUNCHER_COUNT)
        gui_launch(launchers[menu_order[row]].app);
    else if (row == menu_rows() - 1)
        gui_launch("aus");
    else
        gui_launch("update");
}

static void draw_menu_row(int row, const char *label, const char *hint, color_t icon,
                          char letter)
{
    rect_t r = menu_row_rect(row);
    char text[2] = { letter, 0 };
    if (row == menu_selected)
        gfx_fill(&back, r.x, r.y, r.w, r.h, C_BUTTON_ACTIVE);
    gfx_fill(&back, r.x + 8, r.y + 5, 20, 20, icon);
    gfx_text(&back, r.x + 14, r.y + 7, text, RGB(255, 255, 255), true);
    gfx_text(&back, r.x + 38, r.y + 7, label, C_TEXT_LIGHT, false);
    gfx_text(&back, r.x + r.w - 8 - gfx_text_width(hint), r.y + 7, hint,
             row == menu_selected ? C_TEXT_LIGHT : C_MENU_MUTED, false);
}

static void draw_start_menu(void)
{
    rect_t m = menu_rect();
    if (!menu_open || rect_empty(rect_intersect(m, back.clip)))
        return;

    /* Ohne die Update-Zeile ist das Menü eine Zeile niedriger. */
    int rows = menu_rows();
    int top = menu_row_rect(0).y - MENU_HEADER;
    gfx_fill(&back, m.x, top, m.w, m.y + m.h - top, C_MENU);
    gfx_frame(&back, m.x, top, m.w, m.y + m.h - top, C_BUTTON_BORDER);
    gfx_gradient(&back, m.x + 1, top + 1, m.w - 2, MENU_HEADER - 6, C_TITLE_TOP,
                 C_TITLE_BOTTOM);
    gfx_text(&back, m.x + 14, top + 10, OS_NAME " " OS_VERSION, RGB(255, 255, 255),
             true);

    for (int row = 0; row < LAUNCHER_COUNT; row++) {
        int index = menu_order[row];
        char hint[8];
        ksnprintf(hint, sizeof(hint), "F%d", index + 1);
        draw_menu_row(row, launchers[index].label, hint, launchers[index].color,
                      launchers[index].letter);
    }
    rect_t line = menu_row_rect(LAUNCHER_COUNT);
    gfx_fill(&back, m.x + 10, line.y - MENU_GAP / 2 - 1, m.w - 20, 1, C_TASKBAR_LINE);
    if (rows == LAUNCHER_COUNT + 2)
        draw_menu_row(LAUNCHER_COUNT, "Update installieren", "", RGB(46, 140, 96), 'U');
    draw_menu_row(rows - 1, "Ausschalten ...", "", RGB(200, 70, 70), 'A');
}

static void draw_taskbar(void)
{
    rect_t bar = taskbar_rect();
    if (rect_empty(rect_intersect(bar, back.clip)))
        return;

    gfx_fill(&back, bar.x, bar.y, bar.w, bar.h, C_TASKBAR);
    gfx_fill(&back, bar.x, bar.y, bar.w, 1, C_TASKBAR_LINE);

    /* Der Start-Knopf mit einem kleinen Fenster-Symbol aus vier Kacheln */
    rect_t start = start_rect();
    gfx_fill(&back, start.x, start.y, start.w, start.h,
             menu_open ? C_BUTTON_ACTIVE : C_BUTTON);
    gfx_frame(&back, start.x, start.y, start.w, start.h, C_BUTTON_BORDER);
    for (int i = 0; i < 4; i++)
        gfx_fill(&back, start.x + 10 + (i % 2) * 8, start.y + 6 + (i / 2) * 8, 6, 6,
                 RGB(120, 190, 255));
    gfx_text(&back, start.x + 32, start.y + (BUTTON_H - FONT_H) / 2, "Start",
             C_TEXT_LIGHT, true);
    gfx_fill(&back, start.x + start.w + 9, bar.y + 8, 1, bar.h - 16, C_TASKBAR_LINE);

    window_t *list[MAX_WINDOWS];
    int n = windows_by_age(list);
    window_t *focused = window_count ? zorder[window_count - 1] : NULL;
    rect_t clock = clock_rect();
    bool update = update_waiting();
    int limit = update ? update_rect().x : sound_available() ? volume_rect().x : clock.x;
    for (int i = 0; i < n; i++) {
        rect_t r = window_button_rect(i);
        if (r.x + r.w > limit - 8)
            break;                      /* kein Platz mehr */
        char label[16];
        strlcpy(label, list[i]->title, sizeof(label));
        ui_button(&back, r, label, list[i] == focused);
    }

    if (update)
        ui_button(&back, update_rect(), UPDATE_LABEL, true);
    if (sound_available()) {
        char label[16];
        if (sound_volume())
            ksnprintf(label, sizeof(label), "Ton %d%%", sound_volume());
        else
            strlcpy(label, "Ton aus", sizeof(label));
        ui_button(&back, volume_rect(), label, false);
    }
    gfx_text(&back, clock.x + 8, clock.y + (BUTTON_H - FONT_H) / 2, clock_text,
             C_TEXT_LIGHT, false);
    ui_button(&back, power_rect(), "Aus", false);
}

/* --- Bild zusammensetzen ---------------------------------------------------------- */

static void draw_desktop(void)
{
    gfx_gradient(&back, 0, 0, screen.width, screen.height - TASKBAR_H,
                 C_DESKTOP_TOP, C_DESKTOP_BOTTOM);

    const char *mark = OS_NAME;
    int scale = 4;
    int w = gfx_text_width(mark) * scale;
    rect_t r = rect_make(screen.width - w - 28,
                         screen.height - TASKBAR_H - FONT_H * scale - 16, w,
                         FONT_H * scale);
    if (!rect_empty(rect_intersect(r, back.clip)))
        gfx_text_scaled(&back, r.x, r.y, mark, C_WATERMARK, scale);
}

static void draw_window(const window_t *w, bool active)
{
    rect_t f = frame_rect(w);
    if (rect_empty(rect_intersect(damage_rect(w), back.clip)))
        return;

    /* Schatten rechts und unten */
    gfx_darken(&back, f.x + f.w, f.y + SHADOW, SHADOW, f.h, 35);
    gfx_darken(&back, f.x + SHADOW, f.y + f.h, f.w - SHADOW, SHADOW, 35);

    gfx_frame(&back, f.x, f.y, f.w, f.h, C_BORDER);
    gfx_gradient(&back, f.x + BORDER, f.y + BORDER, f.w - 2 * BORDER, TITLE_H,
                 active ? C_TITLE_TOP : C_TITLE_OFF_TOP,
                 active ? C_TITLE_BOTTOM : C_TITLE_OFF_BOT);
    gfx_text(&back, f.x + 10, f.y + BORDER + (TITLE_H - FONT_H) / 2, w->title,
             RGB(255, 255, 255), true);

    rect_t c = close_rect(w);
    gfx_fill(&back, c.x, c.y, c.w, c.h, active ? C_CLOSE : C_CLOSE_OFF);
    gfx_line(&back, c.x + 5, c.y + 5, c.x + c.w - 6, c.y + c.h - 6, 2,
             RGB(255, 255, 255));
    gfx_line(&back, c.x + c.w - 6, c.y + 5, c.x + 5, c.y + c.h - 6, 2,
             RGB(255, 255, 255));

    rect_t content = client_rect(w);
    gfx_blit(&back, content.x, content.y, w->surface);

    if (w->resizable) {
        rect_t m = maximize_rect(w);
        gfx_fill(&back, m.x, m.y, m.w, m.h, active ? C_TITLE_BOTTOM : C_CLOSE_OFF);
        gfx_frame(&back, m.x + 4, m.y + 4, m.w - 8, m.h - 8, RGB(255, 255, 255));
        gfx_fill(&back, m.x + 4, m.y + 4, m.w - 8, 2, RGB(255, 255, 255));
        if (w->maximized)               /* zweites Rechteck: "wiederherstellen" */
            gfx_frame(&back, m.x + 7, m.y + 2, m.w - 9, m.h - 9, RGB(255, 255, 255));

        /* Griff: drei schräge Striche in der Ecke */
        rect_t g = grip_rect(w);
        for (int i = 0; i < 3; i++)
            gfx_line(&back, g.x + g.w - 3, g.y + 4 + i * 4, g.x + 4 + i * 4,
                     g.y + g.h - 3, 1, RGB(120, 126, 138));
    }
}

/* Kopiert einen fertigen Bereich in den Bildspeicher. */
static void present(rect_t r)
{
    for (int y = r.y; y < r.y + r.h; y++) {
        const uint32_t *src = back.pixels + (size_t)y * back.stride + r.x;
        uint32_t *dst = screen.pixels + (size_t)y * screen.stride + r.x;
        if (screen_bgr) {
            memcpy(dst, src, (size_t)r.w * sizeof(uint32_t));
        } else {
            for (int i = 0; i < r.w; i++) {     /* Rot und Blau tauschen */
                uint32_t p = src[i];
                dst[i] = ((p & 0xFF) << 16) | (p & 0xFF00) | ((p >> 16) & 0xFF);
            }
        }
    }
}

static void compose(rect_t area)
{
    window_t *list[MAX_WINDOWS];
    uint64_t flags = irq_save();
    int n = window_count;
    memcpy(list, zorder, sizeof(list));
    irq_restore(flags);

    gfx_set_clip(&back, area);
    draw_desktop();
    for (int i = 0; i < n; i++)
        draw_window(list[i], i == n - 1);
    draw_taskbar();
    draw_start_menu();
    if (resize_win) {                   /* künftige Größe als Rahmen */
        rect_t o = outline_rect();
        gfx_frame(&back, o.x, o.y, o.w, o.h, RGB(255, 255, 255));
        gfx_frame(&back, o.x + 1, o.y + 1, o.w - 2, o.h - 2, RGB(40, 82, 160));
        gfx_frame(&back, o.x + 2, o.y + 2, o.w - 4, o.h - 4, RGB(255, 255, 255));
    }
    draw_cursor();
    present(back.clip);
}

/* --- Fensterverwaltung --------------------------------------------------------------- */

static void raise_window(window_t *w)
{
    uint64_t flags = irq_save();
    int i = 0;
    while (i < window_count && zorder[i] != w)
        i++;
    if (i >= window_count - 1) {        /* nicht gefunden oder schon vorn */
        irq_restore(flags);
        return;
    }
    window_t *previous = zorder[window_count - 1];
    for (; i < window_count - 1; i++)
        zorder[i] = zorder[i + 1];
    zorder[window_count - 1] = w;
    irq_restore(flags);

    add_dirty(damage_rect(previous));
    add_dirty(damage_rect(w));
    add_dirty(taskbar_rect());
}

static void remove_window(window_t *w)
{
    rect_t area = damage_rect(w);

    uint64_t flags = irq_save();
    int i = 0;
    while (i < window_count && zorder[i] != w)
        i++;
    for (; i < window_count - 1; i++)
        zorder[i] = zorder[i + 1];
    window_count--;
    bool release_owner = w->auto_release && !task_alive(w->owner);
    w->auto_release = false;
    irq_restore(flags);

    if (drag_win == w)
        drag_win = NULL;
    if (capture_win == w)
        capture_win = NULL;
    if (resize_win == w) {
        add_dirty_outline(outline_rect());
        resize_win = NULL;
    }

    add_dirty(area);
    add_dirty(taskbar_rect());
    if (window_count)
        add_dirty(damage_rect(zorder[window_count - 1]));

    if (release_owner)
        window_unref(w);                /* den Erzeuger gibt es nicht mehr */
    window_unref(w);                    /* Referenz der Fensterliste */
}

/* Fenster schließen, die es wünschen oder deren Anwendung beendet wurde. */
static void collect_closed_windows(void)
{
    for (int i = window_count - 1; i >= 0; i--) {
        window_t *w = zorder[i];
        if (w->close_requested || !task_alive(w->owner)) {
            w->close_requested = true;
            remove_window(w);
        }
    }
}

static window_t *window_at(int x, int y)
{
    for (int i = window_count - 1; i >= 0; i--) {
        if (rect_contains(frame_rect(zorder[i]), x, y))
            return zorder[i];
    }
    return NULL;
}

static void send_mouse(window_t *w, event_type_t type)
{
    rect_t c = client_rect(w);
    event_t ev = {
        .type = type,
        .x = mouse_x - c.x,
        .y = mouse_y - c.y,
        .buttons = mouse_buttons,
    };
    post_event(w, &ev);
}

static void taskbar_click(void)
{
    if (rect_contains(start_rect(), mouse_x, mouse_y)) {
        menu_set_open(true);
        return;
    }
    if (rect_contains(power_rect(), mouse_x, mouse_y)) {
        gui_launch("aus");
        return;
    }
    if (update_waiting() && rect_contains(update_rect(), mouse_x, mouse_y)) {
        gui_launch("update");
        return;
    }
    if (sound_available() && rect_contains(volume_rect(), mouse_x, mouse_y)) {
        /* 100 -> 75 -> 50 -> 25 -> aus -> 100; der Hinweiston zeigt die neue Lautstärke */
        int now = sound_volume();
        sound_set_volume(now > 75 ? 75 : now > 50 ? 50 : now > 25 ? 25 : now > 0 ? 0 : 100);
        sound_play(SOUND_NOTIFY);
        add_dirty(taskbar_rect());
        return;
    }
    window_t *list[MAX_WINDOWS];
    int n = windows_by_age(list);
    for (int i = 0; i < n; i++) {
        if (rect_contains(window_button_rect(i), mouse_x, mouse_y)) {
            raise_window(list[i]);
            return;
        }
    }
}

/* Bittet die Anwendung um eine neue Größe (und gegebenenfalls Lage). */
static void request_size(window_t *w, int x, int y, int width, int height)
{
    event_t ev = { .type = EV_RESIZE, .x = width, .y = height };
    w->pending_x = x;
    w->pending_y = y;
    w->move_pending = true;
    post_event(w, &ev);
}

static void toggle_maximize(window_t *w)
{
    if (!w->maximized) {
        w->restore = rect_make(w->x, w->y, w->surface->width, w->surface->height);
        request_size(w, 0, 0, screen.width - 2 * BORDER,
                     screen.height - TASKBAR_H - TITLE_H - 2 * BORDER);
    } else {
        request_size(w, w->restore.x, w->restore.y, w->restore.w, w->restore.h);
    }
    w->maximized = !w->maximized;
}

static void mouse_pressed(void)
{
    if (menu_open) {
        /* Ein Klick wählt aus dem Menü - oder schließt es, wenn er daneben geht. */
        bool on_start = rect_contains(start_rect(), mouse_x, mouse_y);
        bool inside = rect_contains(menu_rect(), mouse_x, mouse_y) &&
                      mouse_y >= menu_row_rect(0).y - MENU_HEADER;
        int row = menu_row_at(mouse_x, mouse_y);
        if (inside && row < 0)
            return;                     /* Kopf oder Trennlinie */
        menu_activate(row);
        if (inside || on_start)
            return;
    }
    if (rect_contains(taskbar_rect(), mouse_x, mouse_y)) {
        taskbar_click();
        return;
    }
    window_t *w = window_at(mouse_x, mouse_y);
    if (!w)
        return;
    raise_window(w);

    if (rect_contains(close_rect(w), mouse_x, mouse_y)) {
        event_t ev = { .type = EV_CLOSE };
        post_event(w, &ev);
    } else if (w->resizable && rect_contains(maximize_rect(w), mouse_x, mouse_y)) {
        toggle_maximize(w);
    } else if (w->resizable && rect_contains(grip_rect(w), mouse_x, mouse_y)) {
        resize_win = w;                 /* Ecke: Größe ziehen */
        resize_w = w->surface->width;
        resize_h = w->surface->height;
        resize_dx = w->x + resize_w - mouse_x;
        resize_dy = w->y + resize_h - mouse_y;
        add_dirty_outline(outline_rect());
    } else if (rect_contains(client_rect(w), mouse_x, mouse_y)) {
        capture_win = w;
        send_mouse(w, EV_MOUSE_DOWN);
    } else {
        drag_win = w;                   /* Titelleiste: verschieben */
        drag_dx = mouse_x - w->x;
        drag_dy = mouse_y - w->y;
    }
}

static void handle_mouse(int dx, int dy, int buttons)
{
    int old_buttons = mouse_buttons;
    rect_t old_cursor = cursor_rect();

    mouse_x += dx;
    mouse_y += dy;
    if (mouse_x < 0)
        mouse_x = 0;
    if (mouse_y < 0)
        mouse_y = 0;
    if (mouse_x > screen.width - 1)
        mouse_x = screen.width - 1;
    if (mouse_y > screen.height - 1)
        mouse_y = screen.height - 1;
    mouse_buttons = buttons;

    if (dx || dy) {
        add_dirty(old_cursor);
        add_dirty(cursor_rect());
        if (menu_open && rect_contains(menu_rect(), mouse_x, mouse_y))
            menu_select(menu_row_at(mouse_x, mouse_y));
    }

    if (resize_win && (dx || dy)) {
        window_t *w = resize_win;
        int max_w = screen.width - w->x - 2 * BORDER;
        int max_h = screen.height - TASKBAR_H - w->y - TITLE_H - 2 * BORDER;
        add_dirty_outline(outline_rect());
        resize_w = mouse_x + resize_dx - w->x;
        resize_h = mouse_y + resize_dy - w->y;
        if (resize_w > max_w)
            resize_w = max_w;
        if (resize_h > max_h)
            resize_h = max_h;
        if (resize_w < w->min_width)
            resize_w = w->min_width;
        if (resize_h < w->min_height)
            resize_h = w->min_height;
        add_dirty_outline(outline_rect());
    } else if (drag_win && (dx || dy)) {
        window_t *w = drag_win;
        rect_t before = damage_rect(w);
        int max_y = screen.height - TASKBAR_H - TITLE_H;
        w->x = mouse_x - drag_dx;
        w->y = mouse_y - drag_dy;
        if (w->y < 0)
            w->y = 0;
        if (w->y > max_y)
            w->y = max_y;
        add_dirty(before);
        add_dirty(damage_rect(w));
    } else if (capture_win && (dx || dy)) {
        send_mouse(capture_win, EV_MOUSE_MOVE);
    }

    bool was_down = old_buttons & MOUSE_LEFT;
    bool is_down = buttons & MOUSE_LEFT;
    if (is_down && !was_down) {
        mouse_pressed();
    } else if (!is_down && was_down) {
        if (capture_win)
            send_mouse(capture_win, EV_MOUSE_UP);
        capture_win = NULL;
        drag_win = NULL;
        if (resize_win) {               /* losgelassen: die Anwendung um die Größe bitten */
            window_t *w = resize_win;
            add_dirty_outline(outline_rect());
            resize_win = NULL;
            if (resize_w != w->surface->width || resize_h != w->surface->height) {
                w->maximized = false;
                request_size(w, w->x, w->y, resize_w, resize_h);
            }
        }
    }
}

static void handle_key(int key, int mods)
{
    window_t *focused = window_count ? zorder[window_count - 1] : NULL;

    /* Startmenü: Windows-Taste oder Strg+Esc; darin Pfeile, Enter, Esc */
    if (key == KEY_SUPER || ((mods & MOD_CTRL) && key == 27)) {
        menu_set_open(!menu_open);
        if (menu_open)
            menu_select(0);
        return;
    }
    if (menu_open) {
        int rows = menu_rows();
        if (key == KEY_DOWN)
            menu_select(menu_selected < 0 ? 0 : (menu_selected + 1) % rows);
        else if (key == KEY_UP)
            menu_select(menu_selected <= 0 ? rows - 1 : menu_selected - 1);
        else if (key == '\n' && menu_selected >= 0)
            menu_activate(menu_selected);
        else if (key == 27 || key < KEY_UP)
            menu_set_open(false);
        if (key < KEY_F1 || key > KEY_F12)
            return;
        menu_set_open(false);           /* Funktionstasten starten wie immer */
    }

    /* Tastenkürzel der Oberfläche */
    if ((mods & MOD_ALT) && key == '\t') {
        if (window_count > 1)
            raise_window(zorder[0]);    /* hinterstes Fenster nach vorn */
        return;
    }
    if ((mods & MOD_ALT) && key == KEY_F1 + 3) {
        if (focused) {
            event_t ev = { .type = EV_CLOSE };
            post_event(focused, &ev);
        }
        return;
    }
    if ((mods & MOD_ALT) && key == KEY_F1 + 9) {        /* Alt+F10: maximieren und zurück */
        if (focused && focused->resizable)
            toggle_maximize(focused);
        return;
    }
    if (!mods && key >= KEY_F1 && key < KEY_F1 + LAUNCHER_COUNT) {
        gui_launch(launchers[key - KEY_F1].app);
        return;
    }

    if (focused) {
        event_t ev = { .type = EV_KEY, .key = key, .mods = mods };
        post_event(focused, &ev);
    }
}

static void update_clock(void)
{
    char text[sizeof(clock_text)];
    datetime_t now;
    time_now(&now);
    ksnprintf(text, sizeof(text), "%02u.%02u.%u  %02u:%02u:%02u", now.day,
              now.month, now.year, now.hour, now.minute, now.second);
    if (strcmp(text, clock_text) != 0) {
        strlcpy(clock_text, text, sizeof(clock_text));
        add_dirty(clock_rect());
    }
}

static bool pop_raw(raw_event_t *out)
{
    uint64_t flags = irq_save();
    bool have = raw_head != raw_tail;
    if (have) {
        *out = raw[raw_tail];
        raw_tail = (raw_tail + 1) % RAW_QUEUE;
    }
    irq_restore(flags);
    return have;
}

/* Das Mausrad gilt dem Fenster unter dem Zeiger. */
static void handle_wheel(int steps)
{
    window_t *w = window_at(mouse_x, mouse_y);
    if (!w || steps == 0)
        return;
    event_t ev = { .type = EV_SCROLL, .x = mouse_x, .y = steps, .buttons = mouse_buttons };
    post_event(w, &ev);
}

/* Kam seit dem Start irgendeine Eingabe an? (für die Diagnose) */
static volatile bool input_seen;

static void gui_task(void *arg)
{
    (void)arg;
    add_dirty(rect_make(0, 0, screen.width, screen.height));
    gui_launch("terminal");

    /*
     * Ohne Tastatur und Maus lässt sich nichts bedienen. Ist nach einer
     * Weile kein Eingabegerät da, zeigt ein Fenster von selbst, was die
     * Treiber gemeldet haben.
     */
    uint64_t diagnose_at = time_ms() + 15000;

    for (;;) {
        if (diagnose_at && time_ms() >= diagnose_at) {
            diagnose_at = 0;
            if (!input_seen && usb_input_devices() == 0)
                gui_launch_with("diagnose", "auto");
        }

        raw_event_t ev;
        while (pop_raw(&ev)) {
            if (ev.type == RAW_KEY) {
                handle_key(ev.a, ev.b);
            } else if (ev.type == RAW_MOUSE) {
                handle_mouse(ev.a, ev.b, ev.c);
            } else if (ev.type == RAW_POINTER) {
                /* feste Position: als Bewegung vom jetzigen Ort dorthin */
                int x = (int)((int64_t)ev.a * (screen.width - 1) / 65535);
                int y = (int)((int64_t)ev.b * (screen.height - 1) / 65535);
                handle_mouse(x - mouse_x, y - mouse_y, ev.c);
            } else {
                handle_wheel(ev.a);
            }
        }
        collect_closed_windows();
        free_retired();
        update_clock();

        /* Geänderte Bereiche übernehmen und neu zeichnen */
        rect_t todo[MAX_DIRTY];
        uint64_t flags = irq_save();
        int n = dirty_count;
        memcpy(todo, dirty, sizeof(todo));
        dirty_count = 0;
        irq_restore(flags);
        for (int i = 0; i < n; i++)
            compose(todo[i]);

        /* Schlafen, bis wieder etwas passiert (spätestens für die Uhr) */
        flags = irq_save();
        if (raw_head == raw_tail && dirty_count == 0)
            task_block(200);
        irq_restore(flags);
    }
}

/* --- Schnittstelle für Treiber ------------------------------------------------------ */

static void push_raw(raw_event_t ev)
{
    if (!running)
        return;
    uint64_t flags = irq_save();

    /* Mausbewegungen ohne Tastenwechsel aufsummieren */
    unsigned last = (raw_head + RAW_QUEUE - 1) % RAW_QUEUE;
    if (ev.type == RAW_MOUSE && raw_head != raw_tail &&
        raw[last].type == RAW_MOUSE && raw[last].c == ev.c) {
        raw[last].a += ev.a;
        raw[last].b += ev.b;
    } else {
        unsigned next = (raw_head + 1) % RAW_QUEUE;
        if (next != raw_tail) {
            raw[raw_head] = ev;
            raw_head = next;
        }
    }
    irq_restore(flags);
    wake_gui();
}

void gui_post_key(int key, int mods)
{
    input_seen = true;
    push_raw((raw_event_t){ RAW_KEY, key, mods, 0 });
}

void gui_post_mouse(int dx, int dy, int buttons)
{
    input_seen = true;
    push_raw((raw_event_t){ RAW_MOUSE, dx, dy, buttons });
}

void gui_post_pointer(int x, int y, int buttons)
{
    input_seen = true;
    push_raw((raw_event_t){ RAW_POINTER, x, y, buttons });
}

void gui_post_wheel(int steps)
{
    input_seen = true;
    push_raw((raw_event_t){ RAW_WHEEL, steps, 0, 0 });
}

/* --- Anwendungen starten ------------------------------------------------------------ */

bool gui_launch(const char *app)
{
    return gui_launch_with(app, NULL);
}

bool gui_launch_with(const char *app, const char *argument)
{
    static const struct {
        const char *name;
        const char *task_name;
        void (*entry)(void *);
    } apps[] = {
        { "terminal", "terminal", terminal_app },
        { "monitor", "monitor", monitor_app },
        { "malen", "malen", paint_app },
        { "info", "info", about_app },
        { "aus", "ausschalten", power_app },
        { "update", "update", update_app },
        { "claude", "claude", claude_app },
        { "editor", "editor", editor_app },
        { "transfer", "transfer", transfer_app },
        { "browser", "browser", browser_app },
        { "diagnose", "diagnose", diagnose_app },
    };

    if (!running)
        return false;
    for (unsigned i = 0; i < sizeof(apps) / sizeof(apps[0]); i++) {
        if (strcmp(apps[i].name, app) != 0)
            continue;
        /* Die Angabe wandert als eigene Kopie zur Anwendung, die sie freigibt. */
        char *copy = NULL;
        if (argument) {
            size_t size = strlen(argument) + 1;
            copy = kmalloc(size);
            if (!copy)
                return false;
            memcpy(copy, argument, size);
        }
        if (task_create(apps[i].task_name, apps[i].entry, copy,
                        serial_console()) >= 0)
            return true;
        kfree(copy);
        return false;
    }
    return false;
}

/* --- Start, Startprotokoll und Panic ------------------------------------------------- */

/* Farbe in die Bytefolge des Bildspeichers bringen (für direktes Zeichnen). */
static color_t native(color_t c)
{
    if (screen_bgr)
        return c;
    return ((c & 0xFF) << 16) | (c & 0xFF00) | ((c >> 16) & 0xFF);
}

static int log_x, log_y;

static void boot_screen_write(const char *text)
{
    uint32_t cp;
    while ((cp = utf8_next(&text)) != 0) {
        if (cp == '\n') {
            log_x = 0;
            log_y++;
            continue;
        }
        if (24 + log_x * FONT_W + FONT_W > screen.width ||
            72 + log_y * FONT_H + FONT_H > screen.height)
            continue;
        gfx_glyph(&screen, 24 + log_x * FONT_W, 72 + log_y * FONT_H, cp,
                  native(RGB(200, 206, 216)), false);
        log_x++;
    }
}

void gui_init(void)
{
    if (!boot_info.fb_base)
        return;

    uint64_t bytes = (uint64_t)boot_info.fb_stride * boot_info.fb_height * 4;
    vmm_map_framebuffer(boot_info.fb_base, bytes);
    surface_init(&screen, (uint32_t *)boot_info.fb_base, (int)boot_info.fb_width,
                 (int)boot_info.fb_height, (int)boot_info.fb_stride);
    screen_bgr = boot_info.fb_bgr;

    gfx_fill(&screen, 0, 0, screen.width, screen.height, native(RGB(14, 18, 28)));
    gfx_text_scaled(&screen, 24, 20, OS_NAME " " OS_VERSION,
                    native(RGB(110, 190, 230)), 2);
    klog_set_screen(boot_screen_write);
}

void gui_refresh_taskbar(void)
{
    if (running)
        add_dirty(taskbar_rect());
}

bool gui_available(void)
{
    return screen.pixels != NULL;
}

void gui_screen_size(int *width, int *height)
{
    *width = screen.width;
    *height = screen.height;
}

void gui_start(void)
{
    if (!gui_available())
        return;

    uint32_t *pixels = kmalloc((size_t)screen.width * screen.height * 4);
    if (!pixels) {
        klog("Oberfläche: nicht genug Speicher für das Bild.\n");
        return;
    }
    surface_init(&back, pixels, screen.width, screen.height, screen.width);
    mouse_x = screen.width / 2;
    mouse_y = screen.height / 2;

    klog_set_screen(NULL);
    running = true;
    int pid = task_create("oberflaeche", gui_task, NULL, NULL);
    if (pid < 0) {
        running = false;
        return;
    }
    gui_pid = (uint32_t)pid;
}

void gui_panic_screen(const char *text)
{
    if (!gui_available())
        return;

    int lines = 1;
    for (const char *p = text; *p; p++)
        lines += *p == '\n';
    int w = screen.width < 720 ? screen.width : 720;
    int h = (lines + 5) * FONT_H + 24;

    gfx_reset_clip(&screen);
    gfx_fill(&screen, 0, 0, w, h, native(RGB(150, 24, 24)));
    gfx_frame(&screen, 0, 0, w, h, native(RGB(255, 255, 255)));
    gfx_text(&screen, 16, 12, "*** KERNEL PANIC ***", native(RGB(255, 255, 255)),
             true);

    int x = 16, y = 12 + 2 * FONT_H;
    uint32_t cp;
    while ((cp = utf8_next(&text)) != 0) {
        if (cp == '\n') {
            x = 16;
            y += FONT_H;
            continue;
        }
        gfx_glyph(&screen, x, y, cp, native(RGB(255, 255, 255)), false);
        x += FONT_W;
    }
    gfx_text(&screen, 16, y + 2 * FONT_H, "System angehalten.",
             native(RGB(255, 255, 255)), true);
}
