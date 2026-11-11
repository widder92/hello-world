/*
 * Selbstaktualisierung.
 *
 * Auf dem Update-Server liegen zwei Dateien:
 *   helloos.version  - die neueste Build-Nummer als Text
 *   helloos.upd      - das Paket: Signatur, Kopf, neue BOOTX64.EFI
 *
 * Ist die Nummer dort größer als die eigene, wird das Paket geladen. Bevor
 * irgendetwas installiert wird, muss die Ed25519-Signatur zum eingebauten
 * öffentlichen Schlüssel passen. Das schützt auch dann, wenn jemand die
 * Verbindung manipuliert oder den Server übernimmt - nur wer den geheimen
 * Schlüssel hat, kann ein gültiges Paket bauen. Die Build-Nummer steht mit
 * im signierten Teil, damit niemand ein altes Paket als neu ausgeben kann.
 *
 * Installiert wird auf den USB-Stick, von dem HelloOS gestartet wurde: Die
 * Startdatei EFI/BOOT/BOOTX64.EFI wird ausgetauscht (siehe fat_replace()).
 */
#include "update.h"
#include "block.h"
#include "console.h"
#include "cpu.h"
#include "crypto.h"
#include "fat.h"
#include "gui.h"
#include "http.h"
#include "memory.h"
#include "net.h"
#include "string.h"
#include "task.h"
#include "time.h"

/* Wird beim Bauen erzeugt: OS_BUILD, UPDATE_URL, UPDATE_KEY_HEX */
#include "update-config.h"

#define BOOT_FILE       "EFI/BOOT/BOOTX64.EFI"
#define PACKAGE_MAX     (16u * 1024 * 1024)
#define HEADER_SIZE     96
#define CHECK_INTERVAL  (6u * 60 * 60 * 1000)   /* alle sechs Stunden */

/*
 * Erkennungszeichen: Diese Zeichenfolge steht in jeder Startdatei von
 * HelloOS (einfach weil sie hier im Code steht). Daran erkennt der Updater
 * den richtigen Datenträger und ob ein Paket wirklich HelloOS enthält.
 */
static const char image_marker[] = "HelloOS-Startdatei-Kennung-7f3a";

static uint8_t public_key[32];
static bool enabled;
static bool busy;
static uint32_t installed_build;
static char state[96] = "noch nicht geprüft";

static void set_state(bool verbose, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

static void set_state(bool verbose, const char *fmt, ...)
{
    char text[sizeof(state)];
    va_list ap;
    va_start(ap, fmt);
    kvsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);

    uint64_t flags = irq_save();
    strlcpy(state, text, sizeof(state));
    irq_restore(flags);
    if (verbose)
        kprintf("%s\n", text);
    else
        klog("Update: %s\n", text);
}

static bool parse_key(const char *hex, uint8_t out[32])
{
    for (int i = 0; i < 64; i++) {
        char c = hex[i];
        int digit;
        if (c >= '0' && c <= '9')
            digit = c - '0';
        else if (c >= 'a' && c <= 'f')
            digit = c - 'a' + 10;
        else
            return false;
        if (i % 2 == 0)
            out[i / 2] = (uint8_t)(digit << 4);
        else
            out[i / 2] |= (uint8_t)digit;
    }
    return hex[64] == '\0';
}

static bool contains(const uint8_t *data, size_t len, const char *needle)
{
    size_t n = strlen(needle);
    for (size_t i = 0; i + n <= len; i++) {
        if (data[i] == (uint8_t)needle[0] && memcmp(data + i, needle, n) == 0)
            return true;
    }
    return false;
}

/* --- Installation ------------------------------------------------------------------- */

/* Sucht den Datenträger mit der Startdatei von HelloOS und tauscht sie aus. */
static bool install(bool verbose, const uint8_t *image, uint32_t size)
{
    bool any_device = false;

    for (int i = 0; i < BLOCK_DEVICES_MAX; i++) {
        block_device_t *dev = block_get(i);
        fat_t fs;
        fat_file_t file;
        if (!dev)
            continue;
        any_device = true;
        if (!fat_mount(&fs, dev))
            continue;

        bool ours = false;
        if (fat_find(&fs, BOOT_FILE, &file) && !file.directory &&
            file.size > 0 && file.size <= PACKAGE_MAX) {
            uint8_t *current = kmalloc(file.size);
            if (current && fat_read(&fs, &file, current))
                ours = contains(current, file.size, image_marker);
            kfree(current);
        }
        if (!ours) {
            fat_unmount(&fs);
            continue;
        }

        set_state(verbose, "Schreibe neue Startdatei auf %s ...", dev->name);
        bool ok = fat_replace(&fs, &file, image, size);

        /* Zur Kontrolle zurücklesen und vergleichen */
        if (ok) {
            uint8_t *check = kmalloc(size);
            ok = check && fat_find(&fs, BOOT_FILE, &file) && file.size == size &&
                 fat_read(&fs, &file, check) && memcmp(check, image, size) == 0;
            kfree(check);
        }
        fat_unmount(&fs);
        if (!ok)
            set_state(verbose, "Schreiben auf %s fehlgeschlagen", dev->name);
        return ok;
    }

    set_state(verbose, any_device
                  ? "Kein Datenträger mit der HelloOS-Startdatei gefunden"
                  : "Kein USB-Stick gefunden - Updates brauchen HelloOS auf einem Stick");
    return false;
}

/* --- Prüfen und laden ------------------------------------------------------------------ */

/* Liest die Dezimalzahl am Anfang eines Textes. */
static bool parse_number(const char *text, uint32_t *out)
{
    uint32_t value = 0;
    int digits = 0;
    for (; *text >= '0' && *text <= '9' && digits < 9; text++, digits++)
        value = value * 10 + (uint32_t)(*text - '0');
    *out = value;
    return digits > 0;
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static bool check_and_install(bool verbose)
{
    http_response_t response;
    net_info_t net;

    if (!enabled) {
        set_state(verbose, "Updates sind nicht eingerichtet (kein Schlüssel eingebaut)");
        return false;
    }
    net_get_info(&net);
    if (!net.configured) {
        set_state(verbose, "Kein Netzwerk");
        return false;
    }

    /* 1. Welche Version gibt es? */
    if (!http_get(UPDATE_URL "/helloos.version", 64, &response)) {
        set_state(verbose, "Update-Server nicht erreichbar (%s)", response.error);
        return false;
    }
    uint32_t available = 0;
    bool listed = response.status == 200 && parse_number(response.body, &available);
    int status = response.status;
    kfree(response.body);
    if (!listed) {
        set_state(verbose, "Auf dem Update-Server liegt nichts (Status %d)", status);
        return false;
    }
    if (available <= OS_BUILD || available <= installed_build) {
        if (installed_build)
            set_state(verbose, "Build %u ist installiert und wird beim nächsten Start aktiv",
                      installed_build);
        else
            set_state(verbose, "HelloOS ist aktuell (Build %u)", (uint32_t)OS_BUILD);
        return false;
    }

    /* 2. Paket laden */
    set_state(verbose, "Lade Build %u ...", available);
    if (!http_get(UPDATE_URL "/helloos.upd", PACKAGE_MAX, &response)) {
        set_state(verbose, "Download fehlgeschlagen (%s)", response.error);
        return false;
    }
    const uint8_t *package = (const uint8_t *)response.body;
    size_t len = response.length;
    bool installed = false;

    /* 3. Erst prüfen, dann vertrauen */
    if (response.status != 200 || len <= HEADER_SIZE ||
        memcmp(package + 64, "HELLOUPD", 8) != 0 || rd32(package + 72) != 1 ||
        rd32(package + 80) != len - HEADER_SIZE) {
        set_state(verbose, "Das Paket ist beschädigt oder kein HelloOS-Update");
    } else if (!ed25519_verify(package, public_key, package + 64, len - 64)) {
        set_state(verbose, "Signatur ungültig - Update wird NICHT installiert");
    } else if (rd32(package + 76) <= OS_BUILD) {
        set_state(verbose, "Das Paket ist nicht neuer als die laufende Version");
    } else if (!contains(package + HEADER_SIZE, len - HEADER_SIZE, image_marker)) {
        set_state(verbose, "Das Paket enthält keine HelloOS-Startdatei");
    } else {
        /* 4. Installieren */
        uint32_t build = rd32(package + 76);
        set_state(verbose, "Signatur von Build %u ist gültig", build);
        if (install(verbose, package + HEADER_SIZE, (uint32_t)(len - HEADER_SIZE))) {
            installed_build = build;
            installed = true;
            set_state(verbose, "Build %u installiert - wird beim nächsten Start aktiv",
                      build);
        }
    }
    kfree(response.body);
    return installed;
}

bool update_run(bool verbose)
{
    uint64_t flags = irq_save();
    if (busy) {
        irq_restore(flags);
        if (verbose)
            kprintf("Es läuft bereits eine Prüfung.\n");
        return false;
    }
    busy = true;
    irq_restore(flags);

    bool installed = check_and_install(verbose);
    busy = false;
    return installed;
}

/* --- Hintergrund-Task -------------------------------------------------------------------- */

static void update_task(void *arg)
{
    (void)arg;
    for (;;) {
        /* Auf Netzwerk warten, dann kurz Luft lassen (USB-Stick erkennen) */
        while (!net_wait_configured(60000))
            ;
        task_sleep(4000);
        if (update_run(false))
            gui_launch("update");
        task_sleep(CHECK_INTERVAL);
    }
}

void update_init(void)
{
    enabled = parse_key(UPDATE_KEY_HEX, public_key);
    if (!enabled) {
        strlcpy(state, "nicht eingerichtet (kein Schlüssel eingebaut)",
                sizeof(state));
        return;
    }
    task_create("update", update_task, NULL, NULL);
}

void update_get_info(update_info_t *out)
{
    uint64_t flags = irq_save();
    out->enabled = enabled;
    out->build = OS_BUILD;
    out->installed_build = installed_build;
    out->url = UPDATE_URL;
    strlcpy(out->state, state, sizeof(out->state));
    irq_restore(flags);
}

uint32_t update_current_build(void)
{
    return OS_BUILD;
}
