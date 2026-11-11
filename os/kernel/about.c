/* Kleine Dialogfenster: "Über HelloOS" und "Ausschalten" */
#include "apps.h"
#include "disk.h"
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
    disk_shutdown();
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
    disk_shutdown();
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

/* --- Update: nachfragen, installieren, Ergebnis melden ---------------------------- */

#define UPDATE_W     540
#define UPDATE_H     330
#define WRAP_COLUMNS 62

/* Gibt Text mehrzeilig aus und bricht dabei an Leerzeichen um. */
static int draw_wrapped(surface_t *s, int x, int y, const char *text,
                        int max_lines, color_t color)
{
    char line[WRAP_COLUMNS * 2 + 2];
    while (*text && max_lines > 0) {
        /* Eine Zeile: bis zum Zeilenende oder so viele Zeichen, wie passen */
        const char *p = text, *last_space = NULL;
        int columns = 0;
        while (*p && *p != '\n' && columns < WRAP_COLUMNS) {
            if (*p == ' ')
                last_space = p;
            if (((uint8_t)*p & 0xC0) != 0x80)
                columns++;              /* UTF-8-Folgebytes zählen nicht */
            p++;
        }
        while (((uint8_t)*p & 0xC0) == 0x80)
            p++;                        /* kein Zeichen in der Mitte teilen */
        const char *end = p;
        if (*p && *p != '\n' && last_space)
            end = last_space;           /* mitten im Wort: beim Leerzeichen trennen */

        size_t n = (size_t)(end - text);
        if (n >= sizeof(line))
            n = sizeof(line) - 1;
        memcpy(line, text, n);
        line[n] = '\0';
        gfx_text(s, x, y, line, color, false);
        y += FONT_H + 2;
        max_lines--;

        text = end;
        while (*text == ' ')
            text++;
        if (*text == '\n')
            text++;
    }
    return y;
}

void update_app(void *arg)
{
    (void)arg;
    static bool open;                   /* nie zwei solche Fenster gleichzeitig */
    enum { ASK, DONE, FAILED, NOTHING } stage;

    uint64_t flags = irq_save();
    bool already = open;
    open = true;
    irq_restore(flags);
    if (already)
        return;

    window_t *win = window_create("Software-Update", UPDATE_W, UPDATE_H);
    if (!win) {
        open = false;
        return;
    }
    surface_t *s = window_surface(win);
    const rect_t primary = rect_make(20, UPDATE_H - 52, 172, 32);
    const rect_t secondary = rect_make(204, UPDATE_H - 52, 100, 32);

    update_info_t info;
    update_get_info(&info);
    stage = info.available_build ? ASK : info.installed_build ? DONE : NOTHING;

    for (;;) {
        static char notes[2048];
        char line[96];
        update_get_info(&info);

        /* --- Zeichnen --- */
        gfx_fill(s, 0, 0, UPDATE_W, UPDATE_H, C_BACKGROUND);
        bool two_buttons = true;
        const char *primary_label = "", *secondary_label = "Später";
        if (stage == ASK) {
            ksnprintf(line, sizeof(line), "Für HelloOS gibt es ein Update: Build %u",
                      info.available_build);
            gfx_text(s, 20, 20, line, C_TEXT, true);
            ksnprintf(line, sizeof(line), "Installiert ist Build %u. Das ist neu:",
                      info.build);
            gfx_text(s, 20, 42, line, C_MUTED, false);
            update_available_notes(notes, sizeof(notes));
            draw_wrapped(s, 20, 72,
                         notes[0] ? notes : "(keine Beschreibung mitgeliefert)",
                         10, C_TEXT);
            primary_label = "Jetzt installieren";
        } else if (stage == DONE) {
            ksnprintf(line, sizeof(line), "Build %u ist installiert.",
                      info.installed_build);
            gfx_text(s, 20, 20, line, C_TEXT, true);
            gfx_text(s, 20, 46, "Die neue Version wird beim nächsten Start aktiv.",
                     C_MUTED, false);
            primary_label = "Jetzt neu starten";
        } else {
            gfx_text(s, 20, 20,
                     stage == FAILED ? "Die Installation hat nicht geklappt."
                                     : "Im Moment liegt kein Update bereit.",
                     C_TEXT, true);
            draw_wrapped(s, 20, 46, info.state, 3, C_MUTED);
            primary_label = "Schließen";
            two_buttons = false;
        }
        ui_button(s, primary, primary_label, true);
        if (two_buttons)
            ui_button(s, secondary, secondary_label, false);
        window_invalidate(win, 0, 0, UPDATE_W, UPDATE_H);

        /* --- Auf die Entscheidung warten --- */
        bool yes = false, no = false;
        while (!yes && !no) {
            event_t ev;
            window_get_event(win, &ev, 0);
            /*
             * Bewusst keine Bestätigung per Tastatur: Das Fenster kann
             * aufgehen, während jemand gerade tippt.
             */
            if (ev.type == EV_CLOSE || (ev.type == EV_KEY && ev.key == 27))
                no = true;
            else if (ev.type == EV_MOUSE_UP && rect_contains(primary, ev.x, ev.y))
                yes = true;
            else if (ev.type == EV_MOUSE_UP && two_buttons &&
                     rect_contains(secondary, ev.x, ev.y))
                no = true;
        }

        if (stage == ASK && yes) {
            gfx_fill(s, 0, 0, UPDATE_W, UPDATE_H, C_BACKGROUND);
            gfx_text(s, 20, 20, "Das Update wird installiert ...", C_TEXT, true);
            gfx_text(s, 20, 46, "Bitte nicht ausschalten und nichts abziehen.", C_MUTED,
                     false);
            window_invalidate(win, 0, 0, UPDATE_W, UPDATE_H);
            stage = update_install(false) ? DONE : FAILED;
            continue;
        }
        if (stage == ASK)
            update_decline();           /* "Später": Knopf in der Taskleiste bleibt */
        if (stage == DONE && yes)
            power_reboot();
        break;
    }
    window_destroy(win);
    open = false;
}
