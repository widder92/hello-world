/* Systemmonitor: Auslastung, Speicher und Taskliste - jede Sekunde aktualisiert */
#include "apps.h"
#include "console.h"
#include "gui.h"
#include "memory.h"
#include "string.h"
#include "task.h"
#include "time.h"

#define WIDTH   400
#define HEIGHT  420
#define MARGIN  14
#define BAR_H   16

#define C_BACKGROUND RGB(240, 242, 246)
#define C_TEXT       RGB(34, 38, 48)
#define C_MUTED      RGB(110, 116, 130)
#define C_TRACK      RGB(212, 216, 224)
#define C_CPU        RGB(66, 132, 214)
#define C_MEMORY     RGB(92, 176, 112)
#define C_ROW        RGB(228, 231, 237)

static void draw_bar(surface_t *s, int y, const char *label, const char *value,
                     int percent, color_t color)
{
    gfx_text(s, MARGIN, y, label, C_TEXT, true);
    gfx_text(s, WIDTH - MARGIN - gfx_text_width(value), y, value, C_MUTED,
             false);
    int width = WIDTH - 2 * MARGIN;
    gfx_fill(s, MARGIN, y + 20, width, BAR_H, C_TRACK);
    gfx_fill(s, MARGIN, y + 20, width * percent / 100, BAR_H, color);
}

void monitor_app(void *arg)
{
    (void)arg;
    window_t *win = window_create("Systemmonitor", WIDTH, HEIGHT);
    if (!win)
        return;
    surface_t *s = window_surface(win);

    task_info_t before[MAX_TASKS], now[MAX_TASKS];
    int before_count = 0;
    uint64_t before_ms = time_ms();

    for (;;) {
        char text[64];
        int count = task_snapshot(now, MAX_TASKS);
        uint64_t now_ms = time_ms();
        uint64_t elapsed = now_ms - before_ms;
        if (elapsed == 0)
            elapsed = 1;

        /* Rechenzeit seit der letzten Messung je Task, in Zehntelprozent */
        int usage[MAX_TASKS];
        int idle = 1000;
        for (int i = 0; i < count; i++) {
            uint64_t earlier = 0;
            bool known = false;
            for (int k = 0; k < before_count; k++) {
                if (before[k].pid == now[i].pid) {
                    earlier = before[k].cpu_ms;
                    known = true;
                }
            }
            uint64_t used = known ? now[i].cpu_ms - earlier : 0;
            usage[i] = (int)(used * 1000 / elapsed);
            if (usage[i] > 1000)
                usage[i] = 1000;
            if (now[i].pid == 0 && before_count > 0)
                idle = usage[i];
        }

        gfx_fill(s, 0, 0, WIDTH, HEIGHT, C_BACKGROUND);

        int busy = 1000 - idle;
        ksnprintf(text, sizeof(text), "%d,%d %%", busy / 10, busy % 10);
        draw_bar(s, MARGIN, "Prozessor", text, busy / 10, C_CPU);

        memory_stats_t mem;
        memory_stats(&mem);
        ksnprintf(text, sizeof(text), "%llu von %llu MiB",
                  (mem.heap_used + 512 * 1024) / (1024 * 1024),
                  mem.heap_size / (1024 * 1024));
        draw_bar(s, MARGIN + 52, "Kernel-Speicher", text,
                 (int)(mem.heap_used * 100 / mem.heap_size), C_MEMORY);

        uint32_t up = (uint32_t)(now_ms / 1000);
        ksnprintf(text, sizeof(text), "Laufzeit %02u:%02u:%02u   Tasks: %d",
                  up / 3600, up / 60 % 60, up % 60, count);
        gfx_text(s, MARGIN, MARGIN + 104, text, C_MUTED, false);

        int y = MARGIN + 132;
        gfx_text(s, MARGIN, y, " PID  Name             CPU     Zeit", C_TEXT,
                 true);
        y += FONT_H + 4;
        for (int i = 0; i < count && y + FONT_H < HEIGHT - 6; i++) {
            if (i % 2 == 0)
                gfx_fill(s, MARGIN - 4, y - 1, WIDTH - 2 * MARGIN + 8,
                         FONT_H + 2, C_ROW);
            ksnprintf(text, sizeof(text), "%4u  %-15s %3d,%d %% %5llu s",
                      now[i].pid, now[i].name, usage[i] / 10, usage[i] % 10,
                      now[i].cpu_ms / 1000);
            gfx_text(s, MARGIN, y, text, C_TEXT, false);
            y += FONT_H + 2;
        }
        window_invalidate(win, 0, 0, WIDTH, HEIGHT);

        memcpy(before, now, sizeof(before));
        before_count = count;
        before_ms = now_ms;

        /* Eine Sekunde warten - oder bis das Fenster geschlossen wird */
        uint64_t next = now_ms + 1000;
        while (time_ms() < next) {
            event_t ev;
            if (window_get_event(win, &ev, (uint32_t)(next - time_ms()) + 1) &&
                ev.type == EV_CLOSE) {
                window_destroy(win);
                return;
            }
        }
    }
}
