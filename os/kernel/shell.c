#include "shell.h"
#include "console.h"
#include "fs.h"
#include "input.h"
#include "io.h"
#include "memory.h"
#include "rtc.h"
#include "string.h"
#include "task.h"
#include "timer.h"
#include "vga.h"

#include <cpuid.h>

#define LINE_MAX     128
#define HISTORY_MAX  8

extern char __kernel_start[], __kernel_end[];

static char history[HISTORY_MAX][LINE_MAX];
static int history_count;

/* --- Zeileneingabe ---------------------------------------------------------- */

static void prompt(void)
{
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    kprintf("helloos");
    vga_reset_color();
    kprintf("> ");
}

/* Löscht die sichtbare Zeile; Umlaute belegen zwei Bytes, aber eine Spalte. */
static void erase_line(const char *buf, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        if (((uint8_t)buf[i] & 0xC0) != 0x80)
            console_putc('\b');
    }
}

static void history_add(const char *line)
{
    if (history_count > 0 && strcmp(history[history_count - 1], line) == 0)
        return;
    if (history_count == HISTORY_MAX) {
        memmove(history[0], history[1], (HISTORY_MAX - 1) * LINE_MAX);
        history_count--;
    }
    strlcpy(history[history_count++], line, LINE_MAX);
}

static void read_line(char *buf)
{
    size_t len = 0;
    int pos = history_count;            /* Position beim Blättern im Verlauf */

    for (;;) {
        int c = input_getchar();

        if (c == '\n') {
            console_putc('\n');
            break;
        }
        if (c == 3) {                   /* Strg+C: Eingabe verwerfen */
            kprintf("^C\n");
            len = 0;
            break;
        }
        if (c == 12) {                  /* Strg+L: Bildschirm löschen */
            vga_clear();
            prompt();
            console_write(buf, len);
            continue;
        }
        if (c == '\b') {
            if (len == 0)
                continue;
            while (len > 0 && ((uint8_t)buf[len - 1] & 0xC0) == 0x80)
                len--;                  /* UTF-8-Folgebytes */
            if (len > 0)
                len--;
            console_putc('\b');
            continue;
        }
        if (c == KEY_UP || c == KEY_DOWN) {
            if (c == KEY_UP && pos > 0)
                pos--;
            else if (c == KEY_DOWN && pos < history_count)
                pos++;
            else
                continue;
            erase_line(buf, len);
            len = 0;
            if (pos < history_count) {
                strlcpy(buf, history[pos], LINE_MAX);
                len = strlen(buf);
                console_write(buf, len);
            }
            continue;
        }
        if (c < 32 || c == 127 || c >= 0x800)
            continue;                   /* nicht druckbar */

        /* Zeichen als UTF-8 in den Puffer schreiben */
        if (c < 0x80) {
            if (len + 1 >= LINE_MAX)
                continue;
            buf[len++] = (char)c;
            console_putc((char)c);
        } else {
            if (len + 2 >= LINE_MAX)
                continue;
            buf[len++] = (char)(0xC0 | (c >> 6));
            buf[len++] = (char)(0x80 | (c & 0x3F));
            console_write(buf + len - 2, 2);
        }
    }
    buf[len] = '\0';
}

/* Trennt das nächste Wort ab und rückt *cursor dahinter. */
static char *next_token(char **cursor)
{
    char *s = *cursor;
    while (*s == ' ')
        s++;
    char *token = s;
    while (*s && *s != ' ')
        s++;
    if (*s)
        *s++ = '\0';
    while (*s == ' ')
        s++;
    *cursor = s;
    return token;
}

/* --- Befehle ---------------------------------------------------------------- */

struct command {
    const char *name;
    const char *usage;
    const char *help;
    void (*run)(char *args);
};

static const struct command commands[];

static void cmd_help(char *args)
{
    (void)args;
    kprintf("Verfügbare Befehle:\n");
    for (const struct command *c = commands; c->name; c++) {
        vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
        kprintf("  %-22s", c->usage);
        vga_reset_color();
        kprintf("%s\n", c->help);
    }
    kprintf("\nPfeil hoch/runter blättert im Verlauf, Strg+L löscht den "
            "Bildschirm.\n");
}

static void cmd_clear(char *args)
{
    (void)args;
    vga_clear();
}

static void cmd_echo(char *args)
{
    kprintf("%s\n", args);
}

static void cmd_info(char *args)
{
    (void)args;
    unsigned eax, ebx, ecx, edx;
    char vendor[13] = "unbekannt";
    char brand[49] = "";

    if (__get_cpuid(0, &eax, &ebx, &ecx, &edx)) {
        memcpy(vendor, &ebx, 4);
        memcpy(vendor + 4, &edx, 4);
        memcpy(vendor + 8, &ecx, 4);
        vendor[12] = '\0';
    }
    if (__get_cpuid_max(0x80000000, NULL) >= 0x80000004) {
        unsigned *out = (unsigned *)brand;
        for (unsigned leaf = 0x80000002; leaf <= 0x80000004; leaf++, out += 4)
            __get_cpuid(leaf, &out[0], &out[1], &out[2], &out[3]);
        brand[48] = '\0';
    }
    const char *model = brand;
    while (*model == ' ')
        model++;

    memory_stats_t mem;
    memory_stats(&mem);

    kprintf("System:      %s %s (x86, 32 Bit, Protected Mode)\n", OS_NAME,
            OS_VERSION);
    kprintf("Prozessor:   %s", vendor);
    if (*model)
        kprintf(" - %s", model);
    kprintf("\n");
    kprintf("Speicher:    %u MiB nutzbar\n", (mem.total_kb + 512) / 1024);
    kprintf("Kernel:      %u KiB bei %p\n",
            (uint32_t)(__kernel_end - __kernel_start + 1023) / 1024,
            (void *)__kernel_start);
    kprintf("Systemtakt:  %u Hz\n", TIMER_HZ);
    kprintf("Tastatur:    %s\n", keyboard_layout_name());
}

static void cmd_mem(char *args)
{
    (void)args;
    static const char *const types[] = {
        "unbekannt", "nutzbar", "reserviert", "ACPI", "ACPI NVS", "defekt",
    };
    memory_stats_t mem;
    memory_stats(&mem);

    kprintf("Nutzbarer RAM: %u KiB\n", mem.total_kb);
    kprintf("Kernel-Heap:   %u KiB ab %p, davon %u KiB belegt (%u Blöcke)\n",
            mem.heap_size / 1024, (void *)mem.heap_start,
            (mem.heap_used + 1023) / 1024, mem.heap_blocks);

    kprintf("\nSpeicherkarte des BIOS:\n");
    for (int i = 0; i < memory_map_count(); i++) {
        const mmap_entry_t *e = memory_map_entry(i);
        uint64_t end = e->base + e->length - 1;
        kprintf("  %08x%08x - %08x%08x  %9u KiB  %s\n",
                (uint32_t)(e->base >> 32), (uint32_t)e->base,
                (uint32_t)(end >> 32), (uint32_t)end,
                (uint32_t)(e->length >> 10), types[e->type <= 5 ? e->type : 0]);
    }
}

static void cmd_uptime(char *args)
{
    (void)args;
    uint32_t ticks = timer_ticks();
    uint32_t s = ticks / TIMER_HZ;
    kprintf("Laufzeit: %02u:%02u:%02u (%u Ticks)\n", s / 3600, s / 60 % 60,
            s % 60, ticks);
}

static void cmd_date(char *args)
{
    (void)args;
    datetime_t now;
    rtc_read(&now);
    kprintf("%02u.%02u.%u %02u:%02u:%02u\n", now.day, now.month, now.year,
            now.hour, now.minute, now.second);
}

static void cmd_ps(char *args)
{
    (void)args;
    static const char *const states[] = {
        "-", "bereit", "läuft", "schläft", "blockiert", "beendet",
    };
    task_info_t info[MAX_TASKS];
    int count = task_snapshot(info, MAX_TASKS);

    kprintf("%4s  %-15s %10s  %s\n", "PID", "NAME", "CPU-ZEIT", "ZUSTAND");
    for (int i = 0; i < count; i++) {
        kprintf("%4u  %-15s %5u.%02u s  %s\n", info[i].pid, info[i].name,
                info[i].cpu_ticks / TIMER_HZ, info[i].cpu_ticks % TIMER_HZ,
                states[info[i].state]);
    }
}

/* Demo-Task: zählt im Sekundentakt und beendet sich dann. */
static void counter_task(void *arg)
{
    uint32_t n = (uint32_t)(uintptr_t)arg;
    for (uint32_t i = 1; i <= n; i++) {
        kprintf("[Zähler, PID %u] %u von %u\n", task_current_pid(), i, n);
        task_sleep(1000);
    }
}

/* Demo-Task: rechnet ohne Pause - zeigt, dass der Kernel Tasks verdrängt. */
static void burn_task(void *arg)
{
    (void)arg;
    volatile uint32_t x = 0;
    for (;;)
        x++;
}

static void cmd_spawn(char *args)
{
    char *kind = next_token(&args);
    int pid;

    if (strcmp(kind, "counter") == 0) {
        uint32_t n = 5;
        if (*args && (!parse_uint(args, &n) || n == 0 || n > 1000)) {
            kprintf("Anzahl muss zwischen 1 und 1000 liegen.\n");
            return;
        }
        pid = task_create("zaehler", counter_task, (void *)(uintptr_t)n);
    } else if (strcmp(kind, "burn") == 0) {
        pid = task_create("heizung", burn_task, NULL);
    } else {
        kprintf("Aufruf: spawn counter [n] | spawn burn\n");
        return;
    }

    if (pid < 0)
        kprintf("Task konnte nicht gestartet werden.\n");
    else
        kprintf("Task gestartet (PID %d).\n", pid);
}

static void cmd_kill(char *args)
{
    uint32_t pid;
    if (!parse_uint(next_token(&args), &pid)) {
        kprintf("Aufruf: kill <pid>\n");
        return;
    }
    if (pid == 0 || pid == task_current_pid()) {
        kprintf("Leerlauf-Task und Shell lassen sich nicht beenden.\n");
        return;
    }
    if (task_kill(pid))
        kprintf("Task %u beendet.\n", pid);
    else
        kprintf("Kein laufender Task mit PID %u.\n", pid);
}

static void cmd_ls(char *args)
{
    (void)args;
    int count = 0;
    for (int i = 0; i < FS_MAX_FILES; i++) {
        const file_t *f = fs_entry(i);
        if (!f)
            continue;
        kprintf("%8u Bytes  %s\n", (uint32_t)f->size, f->name);
        count++;
    }
    kprintf("%d Datei(en)\n", count);
}

static void cmd_cat(char *args)
{
    char *name = next_token(&args);
    if (!*name) {
        kprintf("Aufruf: cat <datei>\n");
        return;
    }
    const file_t *f = fs_find(name);
    if (!f) {
        kprintf("Datei nicht gefunden: %s\n", name);
        return;
    }
    console_write(f->data, f->size);
    if (f->size > 0 && f->data[f->size - 1] != '\n')
        console_putc('\n');
}

static void write_file(char *args, bool append)
{
    char *name = next_token(&args);
    if (!*name || !*args) {
        kprintf("Aufruf: %s <datei> <text>\n", append ? "append" : "write");
        return;
    }
    int err = fs_write(name, args, strlen(args), append);
    if (err == FS_OK)
        err = fs_write(name, "\n", 1, true);

    switch (err) {
    case FS_OK:
        break;
    case FS_ERR_NAME:
        kprintf("Ungültiger Dateiname (höchstens %d Zeichen).\n",
                FS_NAME_MAX - 1);
        break;
    case FS_ERR_FULL:
        kprintf("Das Dateisystem ist voll (%d Dateien).\n", FS_MAX_FILES);
        break;
    default:
        kprintf("Nicht genug Speicher.\n");
        break;
    }
}

static void cmd_write(char *args)
{
    write_file(args, false);
}

static void cmd_append(char *args)
{
    write_file(args, true);
}

static void cmd_rm(char *args)
{
    char *name = next_token(&args);
    if (!*name) {
        kprintf("Aufruf: rm <datei>\n");
        return;
    }
    if (fs_remove(name) != FS_OK)
        kprintf("Datei nicht gefunden: %s\n", name);
}

static void cmd_keymap(char *args)
{
    char *name = next_token(&args);
    if (!*name)
        kprintf("Aktuelle Tastaturbelegung: %s\n", keyboard_layout_name());
    else if (keyboard_set_layout(name))
        kprintf("Tastaturbelegung: %s\n", name);
    else
        kprintf("Unbekannte Belegung. Verfügbar: de, us\n");
}

static void cmd_history(char *args)
{
    (void)args;
    for (int i = 0; i < history_count; i++)
        kprintf("%3d  %s\n", i + 1, history[i]);
}

static void cmd_panic(char *args)
{
    (void)args;
    kprintf("Löse absichtlich eine Division durch Null aus ...\n");
    __asm__ volatile("xor %%edx, %%edx; xor %%ecx, %%ecx; div %%ecx"
                     : : : "eax", "ecx", "edx", "cc");
}

static void cmd_reboot(char *args)
{
    (void)args;
    kprintf("Neustart ...\n");
    irq_disable();
    for (int i = 0; i < 100000 && (inb(0x64) & 0x02); i++)
        ;
    outb(0x64, 0xFE);                   /* Reset über den Tastatur-Controller */

    /* Falls das nicht wirkt: Triple Fault erzwingen */
    struct {
        uint16_t limit;
        uint32_t base;
    } __attribute__((packed)) null_idt = { 0, 0 };
    __asm__ volatile("lidt %0; int $3" : : "m"(null_idt));
    for (;;)
        cpu_halt();
}

static void cmd_shutdown(char *args)
{
    (void)args;
    kprintf("System wird heruntergefahren. Tschüss!\n");
    outw(0x604, 0x2000);                /* QEMU */
    outw(0xB004, 0x2000);               /* Bochs, ältere QEMU-Versionen */
    outw(0x4004, 0x3400);               /* VirtualBox */

    /* Auf echter Hardware (ohne ACPI-Treiber) bleibt nur: anhalten. */
    task_sleep(1000);
    irq_disable();
    kprintf("Du kannst den Rechner jetzt ausschalten.\n");
    for (;;)
        cpu_halt();
}

static const struct command commands[] = {
    { "help",     "help",                "Diese Übersicht anzeigen", cmd_help },
    { "info",     "info",                "Systeminformationen", cmd_info },
    { "mem",      "mem",                 "Speicherbelegung und Speicherkarte", cmd_mem },
    { "uptime",   "uptime",              "Laufzeit seit dem Start", cmd_uptime },
    { "date",     "date",                "Datum und Uhrzeit", cmd_date },
    { "ps",       "ps",                  "Laufende Tasks auflisten", cmd_ps },
    { "spawn",    "spawn counter|burn",  "Demo-Task im Hintergrund starten", cmd_spawn },
    { "kill",     "kill <pid>",          "Task beenden", cmd_kill },
    { "ls",       "ls",                  "Dateien auflisten", cmd_ls },
    { "cat",      "cat <datei>",         "Datei anzeigen", cmd_cat },
    { "write",    "write <datei> <text>", "Datei (über)schreiben", cmd_write },
    { "append",   "append <datei> <text>", "Zeile an Datei anhängen", cmd_append },
    { "rm",       "rm <datei>",          "Datei löschen", cmd_rm },
    { "echo",     "echo <text>",         "Text ausgeben", cmd_echo },
    { "keymap",   "keymap [de|us]",      "Tastaturbelegung anzeigen/ändern", cmd_keymap },
    { "history",  "history",             "Befehlsverlauf anzeigen", cmd_history },
    { "clear",    "clear",               "Bildschirm löschen", cmd_clear },
    { "panic",    "panic",               "Kernel-Panic auslösen (Test)", cmd_panic },
    { "reboot",   "reboot",              "Neu starten", cmd_reboot },
    { "shutdown", "shutdown",            "Herunterfahren", cmd_shutdown },
    { NULL, NULL, NULL, NULL },
};

static void execute(char *line)
{
    char *name = next_token(&line);
    if (!*name)
        return;
    if (strcmp(name, "hilfe") == 0 || strcmp(name, "?") == 0)
        name = "help";

    for (const struct command *c = commands; c->name; c++) {
        if (strcmp(c->name, name) == 0) {
            c->run(line);
            return;
        }
    }
    kprintf("Unbekannter Befehl: %s (\"help\" zeigt alle Befehle)\n", name);
}

void shell_task(void *arg)
{
    (void)arg;
    static char line[LINE_MAX];
    static char copy[LINE_MAX];

    kprintf("\nWillkommen! Tippe \"help\" für eine Befehlsübersicht.\n\n");
    for (;;) {
        prompt();
        read_line(line);

        /* Leerzeichen am Ende entfernen */
        size_t len = strlen(line);
        while (len > 0 && line[len - 1] == ' ')
            line[--len] = '\0';
        if (len == 0)
            continue;

        history_add(line);
        strlcpy(copy, line, LINE_MAX);  /* execute() zerlegt den Puffer */
        execute(copy);
    }
}
