#include "shell.h"
#include "apps.h"
#include "bootinfo.h"
#include "console.h"
#include "cpu.h"
#include "crypto.h"
#include "fs.h"
#include "gui.h"
#include "http.h"
#include "input.h"
#include "memory.h"
#include "net.h"
#include "string.h"
#include "task.h"
#include "time.h"

#include <cpuid.h>

#define LINE_MAX     128
#define HISTORY_MAX  8

/* Zustand einer Shell - es können mehrere gleichzeitig laufen. */
typedef struct {
    char line[LINE_MAX];
    char history[HISTORY_MAX][LINE_MAX];
    int history_count;
} shell_t;

/* --- Zeileneingabe ---------------------------------------------------------- */

static void prompt(void)
{
    console_set_color(COLOR_GREEN);
    kprintf("helloos");
    console_set_color(COLOR_DEFAULT);
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

static void history_add(shell_t *sh, const char *line)
{
    if (sh->history_count > 0 &&
        strcmp(sh->history[sh->history_count - 1], line) == 0)
        return;
    if (sh->history_count == HISTORY_MAX) {
        memmove(sh->history[0], sh->history[1], (HISTORY_MAX - 1) * LINE_MAX);
        sh->history_count--;
    }
    strlcpy(sh->history[sh->history_count++], line, LINE_MAX);
}

/* Liest eine Zeile; false, wenn die Konsole geschlossen wurde. */
static bool read_line(shell_t *sh)
{
    char *buf = sh->line;
    size_t len = 0;
    int pos = sh->history_count;        /* Position beim Blättern im Verlauf */

    for (;;) {
        int c = console_getchar();

        if (c < 0)
            return false;
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
            console_clear();
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
            else if (c == KEY_DOWN && pos < sh->history_count)
                pos++;
            else
                continue;
            erase_line(buf, len);
            len = 0;
            if (pos < sh->history_count) {
                strlcpy(buf, sh->history[pos], LINE_MAX);
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
    return true;
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
    void (*run)(shell_t *sh, char *args);
};

static const struct command commands[];

static void cmd_help(shell_t *sh, char *args)
{
    (void)sh;
    (void)args;
    kprintf("Verfügbare Befehle:\n");
    for (const struct command *c = commands; c->name; c++) {
        console_set_color(COLOR_CYAN);
        kprintf("  %-22s", c->usage);
        console_set_color(COLOR_DEFAULT);
        kprintf("%s\n", c->help);
    }
}

static void cmd_clear(shell_t *sh, char *args)
{
    (void)sh;
    (void)args;
    console_clear();
}

static void cmd_echo(shell_t *sh, char *args)
{
    (void)sh;
    kprintf("%s\n", args);
}

static void cmd_info(shell_t *sh, char *args)
{
    (void)sh;
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

    kprintf("System:      %s %s (x86-64, UEFI)\n", OS_NAME, OS_VERSION);
    kprintf("Prozessor:   %s", vendor);
    if (*model)
        kprintf(" - %s", model);
    kprintf("\n");
    kprintf("Takt (TSC):  %llu MHz\n", boot_info.tsc_hz / 1000000);
    kprintf("Speicher:    %llu MiB nutzbar\n",
            (mem.total_bytes + 512 * 1024) / (1024 * 1024));
    if (gui_available()) {
        int w, h;
        gui_screen_size(&w, &h);
        kprintf("Bildschirm:  %d x %d Pixel\n", w, h);
    } else {
        kprintf("Bildschirm:  keiner (nur serielle Konsole)\n");
    }
    kprintf("Firmware:    %s\n", boot_info.firmware_vendor);
    kprintf("Tastatur:    %s\n", keyboard_layout_name());
}

static void cmd_mem(shell_t *sh, char *args)
{
    (void)sh;
    (void)args;
    static const char *const types[EFI_MEMORY_TYPE_COUNT] = {
        "reserviert", "Startcode (Code)", "Startcode (Daten)",
        "Firmware beim Start (Code)", "Firmware beim Start (Daten)",
        "Firmware-Laufzeit (Code)", "Firmware-Laufzeit (Daten)", "frei",
        "defekt", "ACPI-Tabellen", "ACPI-Zustand", "Geräte (Speicher)",
        "Geräte (Ports)", "PAL-Code", "nichtflüchtig",
    };
    memory_stats_t mem;
    memory_stats(&mem);

    kprintf("Nutzbarer RAM: %llu MiB\n", mem.total_bytes / (1024 * 1024));
    kprintf("Kernel-Heap:   %llu MiB ab %p\n", mem.heap_size / (1024 * 1024),
            (void *)mem.heap_start);
    kprintf("               davon %llu KiB belegt (%llu Blöcke)\n",
            (mem.heap_used + 1023) / 1024, mem.heap_blocks);

    /* Speicherkarte der Firmware nach Art zusammenfassen */
    uint64_t pages[EFI_MEMORY_TYPE_COUNT] = { 0 };
    uint32_t regions[EFI_MEMORY_TYPE_COUNT] = { 0 };
    uint8_t *p = (uint8_t *)boot_info.mmap;
    uint8_t *end = p + boot_info.mmap_size;
    for (; p < end; p += boot_info.mmap_desc_size) {
        const efi_memory_descriptor_t *d = (const efi_memory_descriptor_t *)p;
        if (d->type < EFI_MEMORY_TYPE_COUNT) {
            pages[d->type] += d->number_of_pages;
            regions[d->type]++;
        }
    }
    kprintf("\nSpeicherkarte der Firmware:\n");
    for (int i = 0; i < EFI_MEMORY_TYPE_COUNT; i++) {
        if (regions[i])
            kprintf("  %10llu KiB in %3u Bereichen  %s\n", pages[i] * 4,
                    regions[i], types[i]);
    }
}

static void cmd_uptime(shell_t *sh, char *args)
{
    (void)sh;
    (void)args;
    uint64_t ms = time_ms();
    uint32_t s = (uint32_t)(ms / 1000);
    kprintf("Laufzeit: %02u:%02u:%02u (%llu ms)\n", s / 3600, s / 60 % 60,
            s % 60, ms);
}

static void cmd_date(shell_t *sh, char *args)
{
    (void)sh;
    (void)args;
    datetime_t now;
    time_now(&now);
    kprintf("%02u.%02u.%u %02u:%02u:%02u\n", now.day, now.month, now.year,
            now.hour, now.minute, now.second);
}

static void cmd_ps(shell_t *sh, char *args)
{
    (void)sh;
    (void)args;
    static const char *const states[] = {
        "-", "bereit", "läuft", "schläft", "wartet", "beendet",
    };
    task_info_t info[MAX_TASKS];
    int count = task_snapshot(info, MAX_TASKS);

    kprintf("%4s  %-15s %10s  %s\n", "PID", "NAME", "CPU-ZEIT", "ZUSTAND");
    for (int i = 0; i < count; i++) {
        kprintf("%4u  %-15s %5llu.%02u s  %s\n", info[i].pid, info[i].name,
                info[i].cpu_ms / 1000, (uint32_t)(info[i].cpu_ms % 1000 / 10),
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
    volatile uint64_t x = 0;
    for (;;)
        x++;
}

static void cmd_spawn(shell_t *sh, char *args)
{
    (void)sh;
    char *kind = next_token(&args);
    int pid;

    if (strcmp(kind, "counter") == 0) {
        uint32_t n = 5;
        if (*args && (!parse_uint(args, &n) || n == 0 || n > 1000)) {
            kprintf("Anzahl muss zwischen 1 und 1000 liegen.\n");
            return;
        }
        pid = task_create("zaehler", counter_task, (void *)(uintptr_t)n, NULL);
    } else if (strcmp(kind, "burn") == 0) {
        pid = task_create("heizung", burn_task, NULL, NULL);
    } else {
        kprintf("Aufruf: spawn counter [n] | spawn burn\n");
        return;
    }

    if (pid < 0)
        kprintf("Task konnte nicht gestartet werden.\n");
    else
        kprintf("Task gestartet (PID %d).\n", pid);
}

static void cmd_kill(shell_t *sh, char *args)
{
    (void)sh;
    uint32_t pid;
    if (!parse_uint(next_token(&args), &pid)) {
        kprintf("Aufruf: kill <pid>\n");
        return;
    }
    if (pid == 0 || pid == task_current_pid()) {
        kprintf("Leerlauf-Task und die eigene Shell lassen sich nicht beenden.\n");
        return;
    }
    if (task_kill(pid))
        kprintf("Task %u beendet.\n", pid);
    else
        kprintf("Kein laufender Task mit PID %u.\n", pid);
}

static void cmd_start(shell_t *sh, char *args)
{
    (void)sh;
    char *app = next_token(&args);
    if (!gui_available())
        kprintf("Es gibt keinen Bildschirm.\n");
    else if (!gui_launch(app))
        kprintf("Aufruf: start terminal | monitor | malen | info | aus\n");
}

/* --- Netzwerk -------------------------------------------------------------------- */

static void cmd_net(shell_t *sh, char *args)
{
    (void)sh;
    char *sub = next_token(&args);
    if (strcmp(sub, "dns") == 0) {
        ip4_t server;
        if (!ip4_parse(next_token(&args), &server)) {
            kprintf("Aufruf: net dns <adresse>\n");
            return;
        }
        net_set_dns(server);
    }

    net_info_t info;
    char text[16];
    net_get_info(&info);
    if (!info.present) {
        kprintf("Keine unterstützte Netzwerkkarte gefunden.\n");
        return;
    }
    kprintf("Karte:       %s\n", info.driver);
    kprintf("MAC-Adresse: %02x:%02x:%02x:%02x:%02x:%02x\n", info.mac[0],
            info.mac[1], info.mac[2], info.mac[3], info.mac[4], info.mac[5]);
    kprintf("Verbindung:  %s\n", info.link ? "aktiv" : "kein Kabel");
    if (info.configured) {
        ip4_format(info.ip, text);
        kprintf("IP-Adresse:  %s\n", text);
        ip4_format(info.netmask, text);
        kprintf("Netzmaske:   %s\n", text);
        ip4_format(info.gateway, text);
        kprintf("Router:      %s\n", text);
    } else {
        kprintf("IP-Adresse:  noch keine (warte auf DHCP)\n");
    }
    if (info.dns) {
        ip4_format(info.dns, text);
        kprintf("DNS-Server:  %s\n", text);
    }
    kprintf("Pakete:      %llu empfangen, %llu gesendet\n", info.rx_packets,
            info.tx_packets);
}

/* Prüft, ob das Netz bereit ist, und meldet es sonst dem Benutzer. */
static bool need_network(void)
{
    net_info_t info;
    net_get_info(&info);
    if (!info.present)
        kprintf("Keine unterstützte Netzwerkkarte gefunden.\n");
    else if (!info.configured)
        kprintf("Das Netzwerk ist noch nicht bereit (siehe \"net\").\n");
    return info.configured;
}

static void cmd_ping(shell_t *sh, char *args)
{
    (void)sh;
    char *host = next_token(&args);
    ip4_t target;
    char text[16];

    if (!*host) {
        kprintf("Aufruf: ping <name oder adresse>\n");
        return;
    }
    if (!need_network())
        return;
    if (!net_resolve(host, &target)) {
        kprintf("Name nicht auflösbar: %s\n", host);
        return;
    }
    ip4_format(target, text);
    kprintf("Ping an %s (%s):\n", host, text);

    int answered = 0;
    for (int i = 0; i < 4; i++) {
        uint32_t ms;
        if (net_ping(target, 2000, &ms)) {
            kprintf("  Antwort von %s: Zeit=%u ms\n", text, ms);
            answered++;
        } else {
            kprintf("  keine Antwort\n");
        }
        if (i < 3)
            task_sleep(500);
    }
    kprintf("%d von 4 Paketen beantwortet.\n", answered);
}

static void cmd_dns(shell_t *sh, char *args)
{
    (void)sh;
    char *name = next_token(&args);
    ip4_t ip;
    char text[16];

    if (!*name) {
        kprintf("Aufruf: dns <name>\n");
        return;
    }
    if (!need_network())
        return;
    if (!net_resolve(name, &ip)) {
        kprintf("Name nicht auflösbar: %s\n", name);
        return;
    }
    ip4_format(ip, text);
    kprintf("%s hat die Adresse %s\n", name, text);
}

/* Gibt HTML als lesbaren Text aus: ohne Tags, Skripte und Leerraum-Wüsten. */
static void print_as_text(const char *html, size_t length, int max_lines)
{
    static const char *const hidden[] = { "script", "style", "head", NULL };
    static const char *const breaks[] = {
        "p", "br", "div", "li", "tr", "h1", "h2", "h3", "h4", "h5", "h6",
        "title", "section", "article", "ul", "ol", "table", NULL,
    };
    const char *p = html, *end = html + length;
    const char *skip_until = NULL;      /* Name des Tags, das den Bereich beendet */
    int lines = 0, column = 0;
    bool space = false, newline = false;

    while (p < end && lines < max_lines) {
        if (*p == '<') {
            /* Tagnamen herauslösen */
            const char *q = p + 1;
            bool closing = *q == '/';
            q += closing;
            char name[12];
            size_t n = 0;
            while (q < end && n + 1 < sizeof(name) &&
                   (((*q | 0x20) >= 'a' && (*q | 0x20) <= 'z') ||
                    (*q >= '0' && *q <= '9')))
                name[n++] = (char)(*q++ | 0x20);
            name[n] = '\0';
            while (q < end && *q != '>')
                q++;
            p = q < end ? q + 1 : end;

            if (skip_until) {
                if (closing && strcmp(name, skip_until) == 0)
                    skip_until = NULL;
                continue;
            }
            for (int i = 0; hidden[i] && !closing; i++) {
                if (strcmp(name, hidden[i]) == 0)
                    skip_until = hidden[i];
            }
            for (int i = 0; breaks[i]; i++) {
                if (strcmp(name, breaks[i]) == 0)
                    newline = true;
            }
            continue;
        }
        if (skip_until) {
            p++;
            continue;
        }

        char c = *p++;
        const char *replacement = NULL;
        if (c == '&') {                 /* die häufigsten HTML-Kürzel */
            static const struct {
                const char *code;
                const char *text;
            } entities[] = {
                { "amp;", "&" }, { "lt;", "<" }, { "gt;", ">" },
                { "quot;", "\"" }, { "nbsp;", " " }, { "#39;", "'" },
                { "auml;", "ä" }, { "ouml;", "ö" }, { "uuml;", "ü" },
                { "Auml;", "Ä" }, { "Ouml;", "Ö" }, { "Uuml;", "Ü" },
                { "szlig;", "ß" },
            };
            for (unsigned i = 0; i < sizeof(entities) / sizeof(entities[0]); i++) {
                size_t n = strlen(entities[i].code);
                if ((size_t)(end - p) >= n && memcmp(p, entities[i].code, n) == 0) {
                    replacement = entities[i].text;
                    c = replacement[0];
                    p += n;
                    break;
                }
            }
        }
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            space = true;
            continue;
        }

        /* Zeilen an Wortgrenzen umbrechen, sehr lange Wörter notfalls hart */
        bool wrap = (space && column >= 68) ||
                    (column >= 78 && ((uint8_t)c & 0xC0) != 0x80);
        if ((newline || wrap) && column > 0) {
            console_putc('\n');
            lines++;
            column = 0;
        } else if (space && column > 0) {
            console_putc(' ');
            column++;
        }
        newline = space = false;
        if (replacement) {
            kprintf("%s", replacement);
            column++;
            continue;
        }
        console_putc(c);
        if (((uint8_t)c & 0xC0) != 0x80)
            column++;                   /* UTF-8-Folgebytes zählen nicht */
    }
    if (column > 0)
        console_putc('\n');
    if (p < end)
        kprintf("[... gekürzt, insgesamt %zu Bytes]\n", length);
}

static void cmd_web(shell_t *sh, char *args)
{
    (void)sh;
    char *url = next_token(&args);
    if (!*url) {
        kprintf("Aufruf: web <adresse>   z. B. web http://example.com\n");
        return;
    }
    if (!need_network())
        return;

    http_response_t response;
    if (!http_get(url, 512 * 1024, &response)) {
        kprintf("Fehler: %s\n", response.error);
        return;
    }
    console_set_color(COLOR_CYAN);
    kprintf("Status %d, %zu Bytes, %s\n", response.status, response.length,
            response.content_type[0] ? response.content_type : "Typ unbekannt");
    console_set_color(COLOR_DEFAULT);
    print_as_text(response.body, response.length, 40);
    kfree(response.body);
}

static void cmd_get(shell_t *sh, char *args)
{
    (void)sh;
    char *url = next_token(&args);
    char *name = next_token(&args);
    if (!*url || !*name) {
        kprintf("Aufruf: get <adresse> <datei>\n");
        return;
    }
    if (!need_network())
        return;

    http_response_t response;
    if (!http_get(url, 4 * 1024 * 1024, &response)) {
        kprintf("Fehler: %s\n", response.error);
        return;
    }
    if (response.status != 200)
        kprintf("Der Server antwortet mit Status %d.\n", response.status);
    else if (fs_write(name, response.body, response.length, false) != FS_OK)
        kprintf("Datei konnte nicht gespeichert werden.\n");
    else
        kprintf("%zu Bytes in %s gespeichert.\n", response.length, name);
    kfree(response.body);
}

/* --- Dateien ---------------------------------------------------------------------- */

static void cmd_ls(shell_t *sh, char *args)
{
    (void)sh;
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

static void cmd_cat(shell_t *sh, char *args)
{
    (void)sh;
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

static void cmd_sha256(shell_t *sh, char *args)
{
    (void)sh;
    char *name = next_token(&args);
    const file_t *f = *name ? fs_find(name) : NULL;
    if (!f) {
        kprintf(*name ? "Datei nicht gefunden: %s\n" : "Aufruf: sha256 <datei>\n",
                name);
        return;
    }
    uint8_t hash[32];
    sha256(f->data, f->size, hash);
    for (int i = 0; i < 32; i++)
        kprintf("%02x", hash[i]);
    kprintf("  %s\n", name);
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

static void cmd_write(shell_t *sh, char *args)
{
    (void)sh;
    write_file(args, false);
}

static void cmd_append(shell_t *sh, char *args)
{
    (void)sh;
    write_file(args, true);
}

static void cmd_rm(shell_t *sh, char *args)
{
    (void)sh;
    char *name = next_token(&args);
    if (!*name) {
        kprintf("Aufruf: rm <datei>\n");
        return;
    }
    if (fs_remove(name) != FS_OK)
        kprintf("Datei nicht gefunden: %s\n", name);
}

static void cmd_keymap(shell_t *sh, char *args)
{
    (void)sh;
    char *name = next_token(&args);
    if (!*name)
        kprintf("Aktuelle Tastaturbelegung: %s\n", keyboard_layout_name());
    else if (keyboard_set_layout(name))
        kprintf("Tastaturbelegung: %s\n", name);
    else
        kprintf("Unbekannte Belegung. Verfügbar: de, us\n");
}

static void cmd_history(shell_t *sh, char *args)
{
    (void)args;
    for (int i = 0; i < sh->history_count; i++)
        kprintf("%3d  %s\n", i + 1, sh->history[i]);
}

static void cmd_panic(shell_t *sh, char *args)
{
    (void)sh;
    (void)args;
    kprintf("Löse absichtlich eine Division durch Null aus ...\n");
    __asm__ volatile("xor %%edx, %%edx; xor %%ecx, %%ecx; div %%ecx"
                     : : : "eax", "ecx", "edx", "cc");
}

static void cmd_reboot(shell_t *sh, char *args)
{
    (void)sh;
    (void)args;
    kprintf("Neustart ...\n");
    power_reboot();
}

static void cmd_shutdown(shell_t *sh, char *args)
{
    (void)sh;
    (void)args;
    kprintf("System wird heruntergefahren. Tschüss!\n");
    power_shutdown();
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
    { "start",    "start <programm>",    "Fenster-Programm starten", cmd_start },
    { "net",      "net",                 "Zustand des Netzwerks", cmd_net },
    { "ping",     "ping <ziel>",         "Erreichbarkeit eines Rechners prüfen", cmd_ping },
    { "dns",      "dns <name>",          "Namen in eine Adresse auflösen", cmd_dns },
    { "web",      "web <adresse>",       "Webseite als Text anzeigen", cmd_web },
    { "get",      "get <adresse> <datei>", "Datei aus dem Netz laden", cmd_get },
    { "ls",       "ls",                  "Dateien auflisten", cmd_ls },
    { "cat",      "cat <datei>",         "Datei anzeigen", cmd_cat },
    { "write",    "write <datei> <text>", "Datei (über)schreiben", cmd_write },
    { "append",   "append <datei> <text>", "Zeile an Datei anhängen", cmd_append },
    { "rm",       "rm <datei>",          "Datei löschen", cmd_rm },
    { "sha256",   "sha256 <datei>",      "Prüfsumme einer Datei", cmd_sha256 },
    { "echo",     "echo <text>",         "Text ausgeben", cmd_echo },
    { "keymap",   "keymap [de|us]",      "Tastaturbelegung anzeigen/ändern", cmd_keymap },
    { "history",  "history",             "Befehlsverlauf anzeigen", cmd_history },
    { "clear",    "clear",               "Bildschirm löschen", cmd_clear },
    { "panic",    "panic",               "Kernel-Panic auslösen (Test)", cmd_panic },
    { "reboot",   "reboot",              "Neu starten", cmd_reboot },
    { "shutdown", "shutdown",            "Herunterfahren", cmd_shutdown },
    { NULL, NULL, NULL, NULL },
};

static void execute(shell_t *sh, char *line)
{
    char *name = next_token(&line);
    if (!*name)
        return;
    if (strcmp(name, "hilfe") == 0 || strcmp(name, "?") == 0)
        name = "help";

    for (const struct command *c = commands; c->name; c++) {
        if (strcmp(c->name, name) == 0) {
            c->run(sh, line);
            return;
        }
    }
    kprintf("Unbekannter Befehl: %s (\"help\" zeigt alle Befehle)\n", name);
}

void shell_run(void)
{
    /* Auf dem Stack, damit nichts verloren geht, wenn der Task beendet wird */
    shell_t state;
    shell_t *sh = &state;
    memset(sh, 0, sizeof(*sh));

    console_set_color(COLOR_CYAN);
    kprintf("%s %s", OS_NAME, OS_VERSION);
    console_set_color(COLOR_DEFAULT);
    kprintf(" - tippe \"help\" für eine Befehlsübersicht.\n\n");

    for (;;) {
        char copy[LINE_MAX];

        prompt();
        if (!read_line(sh))
            break;

        /* Leerzeichen am Ende entfernen */
        size_t len = strlen(sh->line);
        while (len > 0 && sh->line[len - 1] == ' ')
            sh->line[--len] = '\0';
        if (len == 0)
            continue;

        history_add(sh, sh->line);
        strlcpy(copy, sh->line, LINE_MAX);  /* execute() zerlegt den Puffer */
        execute(sh, copy);
    }
}
