/* Kleine Dialogfenster: "Über HelloOS" und "Ausschalten" */
#include "apps.h"
#include "bootinfo.h"
#include "console.h"
#include "cpu.h"
#include "gui.h"
#include "input.h"
#include "memory.h"
#include "net.h"
#include "shell.h"
#include "string.h"
#include "task.h"
#include "update.h"

#include <cpuid.h>

#define C_BACKGROUND RGB(240, 242, 246)
#define C_TEXT       RGB(34, 38, 48)
#define C_MUTED      RGB(110, 116, 130)
#define C_ACCENT     RGB(48, 104, 196)

/* --- Über HelloOS -------------------------------------------------------------- */

#define ABOUT_W 440
#define ABOUT_H 290

static void cpu_name(char *out, size_t size)
{
    char brand[49] = "";
    if (__get_cpuid_max(0x80000000, NULL) >= 0x80000004) {
        unsigned *regs = (unsigned *)brand;
        for (unsigned leaf = 0x80000002; leaf <= 0x80000004; leaf++, regs += 4)
            __get_cpuid(leaf, &regs[0], &regs[1], &regs[2], &regs[3]);
    }
    const char *name = brand;
    while (*name == ' ')
        name++;
    strlcpy(out, *name ? name : "unbekannt", size);
}

void about_app(void *arg)
{
    (void)arg;
    window_t *win = window_create("Über HelloOS", ABOUT_W, ABOUT_H);
    if (!win)
        return;
    surface_t *s = window_surface(win);

    char line[80], cpu[49];
    int w, h;
    memory_stats_t mem;
    cpu_name(cpu, 40);
    gui_screen_size(&w, &h);
    memory_stats(&mem);

    gfx_fill(s, 0, 0, ABOUT_W, ABOUT_H, C_BACKGROUND);
    gfx_text_scaled(s, 24, 20, OS_NAME, C_ACCENT, 3);
    ksnprintf(line, sizeof(line), "Version " OS_VERSION ", Build %u",
              update_current_build());
    gfx_text(s, 24 + gfx_text_width(OS_NAME) * 3 + 12, 46, line, C_MUTED, false);
    gfx_text(s, 24, 84, "Ein kleines, selbst geschriebenes Betriebssystem", C_TEXT,
             false);
    gfx_text(s, 24, 102, "für PCs mit 64-Bit-Prozessor und UEFI.", C_TEXT,
             false);
    gfx_fill(s, 24, 130, ABOUT_W - 48, 1, RGB(206, 210, 218));

    int y = 144;
    ksnprintf(line, sizeof(line), "Prozessor   %s", cpu);
    gfx_text(s, 24, y, line, C_TEXT, false);
    ksnprintf(line, sizeof(line), "Speicher    %llu MiB",
              (mem.total_bytes + 512 * 1024) / (1024 * 1024));
    gfx_text(s, 24, y += 20, line, C_TEXT, false);
    ksnprintf(line, sizeof(line), "Bildschirm  %d x %d", w, h);
    gfx_text(s, 24, y += 20, line, C_TEXT, false);
    ksnprintf(line, sizeof(line), "Firmware    %s", boot_info.firmware_vendor);
    gfx_text(s, 24, y += 20, line, C_TEXT, false);

    net_info_t net;
    char address[16] = "nicht verbunden";
    net_get_info(&net);
    if (net.configured)
        ip4_format(net.ip, address);
    ksnprintf(line, sizeof(line), "Netzwerk    %s",
              net.present ? address : "keine unterstützte Karte");
    gfx_text(s, 24, y += 20, line, C_TEXT, false);

    rect_t ok = rect_make(ABOUT_W - 24 - 88, ABOUT_H - 20 - 28, 88, 28);
    ui_button(s, ok, "OK", true);
    window_invalidate(win, 0, 0, ABOUT_W, ABOUT_H);

    for (;;) {
        event_t ev;
        window_get_event(win, &ev, 0);
        if (ev.type == EV_CLOSE ||
            (ev.type == EV_KEY && (ev.key == '\n' || ev.key == 27)) ||
            (ev.type == EV_MOUSE_UP && rect_contains(ok, ev.x, ev.y)))
            break;
    }
    window_destroy(win);
}

/* --- Ausschalten ----------------------------------------------------------------- */

#define POWER_W 420
#define POWER_H 132

void power_reboot(void)
{
    irq_disable();
    boot_info.runtime->reset_system(EFI_RESET_COLD, EFI_SUCCESS, 0, NULL);

    /* Falls die Firmware nicht reagiert: die klassischen Wege */
    outb(0xCF9, 0x06);
    outb(0x64, 0xFE);
    for (;;)
        cpu_halt();
}

void power_shutdown(void)
{
    irq_disable();
    boot_info.runtime->reset_system(EFI_RESET_SHUTDOWN, EFI_SUCCESS, 0, NULL);
    for (;;)
        cpu_halt();
}

void power_app(void *arg)
{
    (void)arg;
    window_t *win = window_create("Ausschalten", POWER_W, POWER_H);
    if (!win)
        return;
    surface_t *s = window_surface(win);

    const rect_t shutdown = rect_make(20, 76, 148, 32);
    const rect_t reboot = rect_make(180, 76, 116, 32);
    const rect_t cancel = rect_make(308, 76, 92, 32);

    gfx_fill(s, 0, 0, POWER_W, POWER_H, C_BACKGROUND);
    gfx_text(s, 20, 22, "Was möchtest du tun?", C_TEXT, true);
    gfx_text(s, 20, 44, "Nicht gespeicherte Dateien gehen verloren.", C_MUTED,
             false);
    ui_button(s, shutdown, "Herunterfahren", true);
    ui_button(s, reboot, "Neu starten", false);
    ui_button(s, cancel, "Abbrechen", false);
    window_invalidate(win, 0, 0, POWER_W, POWER_H);

    for (;;) {
        event_t ev;
        window_get_event(win, &ev, 0);
        if (ev.type == EV_CLOSE || (ev.type == EV_KEY && ev.key == 27))
            break;
        if (ev.type == EV_KEY && ev.key == '\n')
            power_shutdown();
        if (ev.type != EV_MOUSE_UP)
            continue;
        if (rect_contains(shutdown, ev.x, ev.y))
            power_shutdown();
        if (rect_contains(reboot, ev.x, ev.y))
            power_reboot();
        if (rect_contains(cancel, ev.x, ev.y))
            break;
    }
    window_destroy(win);
}

/* --- Hinweis nach einem Update ---------------------------------------------------- */

#define UPDATE_W 440
#define UPDATE_H 140

void update_app(void *arg)
{
    (void)arg;
    window_t *win = window_create("Update installiert", UPDATE_W, UPDATE_H);
    if (!win)
        return;
    surface_t *s = window_surface(win);

    const rect_t restart = rect_make(20, 86, 164, 32);
    const rect_t later = rect_make(196, 86, 92, 32);
    update_info_t info;
    char line[64];
    update_get_info(&info);

    gfx_fill(s, 0, 0, UPDATE_W, UPDATE_H, C_BACKGROUND);
    ksnprintf(line, sizeof(line), "HelloOS wurde auf Build %u aktualisiert.",
              info.installed_build);
    gfx_text(s, 20, 22, line, C_TEXT, true);
    gfx_text(s, 20, 46, "Die neue Version wird beim nächsten Start aktiv.", C_MUTED,
             false);
    ui_button(s, restart, "Jetzt neu starten", true);
    ui_button(s, later, "Später", false);
    window_invalidate(win, 0, 0, UPDATE_W, UPDATE_H);

    for (;;) {
        event_t ev;
        window_get_event(win, &ev, 0);
        if (ev.type == EV_CLOSE || (ev.type == EV_KEY && ev.key == 27))
            break;
        if (ev.type != EV_MOUSE_UP)
            continue;
        if (rect_contains(restart, ev.x, ev.y))
            power_reboot();
        if (rect_contains(later, ev.x, ev.y))
            break;
    }
    window_destroy(win);
}
