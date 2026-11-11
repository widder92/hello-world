/*
 * Selbstaktualisierung.
 *
 * Auf dem Update-Server liegen zwei Dateien:
 *   helloos.version  - die neueste Build-Nummer als Text
 *   helloos.upd      - das Paket: Signatur, Kopf, "Was ist neu", BOOTX64.EFI
 *
 * Ablauf:
 *   1. Prüfen: Ist die Nummer auf dem Server größer als die eigene, wird das
 *      Paket geladen und seine Ed25519-Signatur gegen den eingebauten
 *      öffentlichen Schlüssel geprüft. Nur wer den geheimen Schlüssel hat,
 *      kann ein gültiges Paket bauen - egal, wer die Verbindung manipuliert.
 *      Die Build-Nummer ist mitsigniert, damit niemand ein altes Paket als
 *      neu ausgeben kann.
 *   2. Fragen: Installiert wird nichts von allein. Ein Fenster (oder der
 *      Befehl "update") zeigt die Neuerungen und wartet auf Zustimmung.
 *   3. Installieren: Auf dem USB-Stick, von dem HelloOS gestartet wurde, wird
 *      die Startdatei EFI/BOOT/BOOTX64.EFI ausgetauscht (siehe fat_replace()).
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
#include "storage.h"
#include "string.h"
#include "task.h"
#include "time.h"

/* Wird beim Bauen erzeugt: OS_BUILD, UPDATE_URL, UPDATE_KEY_HEX, RELEASE_NOTES */
#include "update-config.h"

#define BOOT_FILE       "EFI/BOOT/BOOTX64.EFI"
#define PACKAGE_MAX     (16u * 1024 * 1024)
#define HEADER_SIZE     96
#define NOTES_MAX       4096
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

/* Das geladene und geprüfte Update, das auf Zustimmung wartet */
static struct {
    uint8_t *package;                   /* ganzes Paket (kmalloc) */
    uint32_t build;
    const uint8_t *notes, *image;       /* zeigen in package */
    uint32_t notes_len, image_len;
    bool declined;
} pending;

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

/* Immer nur ein Vorgang zur Zeit (Hintergrund-Task, Fenster, Shell). */
static bool begin(bool verbose)
{
    uint64_t flags = irq_save();
    bool ok = !busy;
    busy = true;
    irq_restore(flags);
    if (!ok && verbose)
        kprintf("Es läuft bereits ein Update-Vorgang.\n");
    return ok;
}

static void forget_pending(void)
{
    uint64_t flags = irq_save();
    uint8_t *package = pending.package;
    memset(&pending, 0, sizeof(pending));
    irq_restore(flags);
    kfree(package);
}

/* --- Prüfen und laden ------------------------------------------------------------------ */

/*
 * Paketaufbau (Zahlen little-endian):
 *    0  64  Signatur über alles, was folgt
 *   64   8  "HELLOUPD"
 *   72   4  Formatversion (2)
 *   76   4  Build-Nummer
 *   80   4  Länge der Startdatei
 *   84   4  Länge des Textes "Was ist neu"
 *   88   8  reserviert
 *   96   …  Text, danach die Startdatei
 */
static bool accept_package(bool verbose, uint8_t *package, size_t len)
{
    if (len <= HEADER_SIZE || memcmp(package + 64, "HELLOUPD", 8) != 0 ||
        rd32(package + 72) != 2) {
        set_state(verbose, "Das Paket ist beschädigt oder kein HelloOS-Update");
        return false;
    }
    uint32_t build = rd32(package + 76);
    uint32_t image_len = rd32(package + 80);
    uint32_t notes_len = rd32(package + 84);
    if (notes_len > NOTES_MAX || image_len == 0 ||
        (uint64_t)HEADER_SIZE + notes_len + image_len != len) {
        set_state(verbose, "Das Paket ist beschädigt");
        return false;
    }
    /* Erst prüfen, dann vertrauen */
    if (!ed25519_verify(package, public_key, package + 64, len - 64)) {
        set_state(verbose, "Signatur ungültig - das Update wird verworfen");
        return false;
    }
    if (build <= OS_BUILD) {
        set_state(verbose, "Das Paket ist nicht neuer als die laufende Version");
        return false;
    }
    const uint8_t *image = package + HEADER_SIZE + notes_len;
    if (!contains(image, image_len, image_marker)) {
        set_state(verbose, "Das Paket enthält keine HelloOS-Startdatei");
        return false;
    }

    forget_pending();
    uint64_t flags = irq_save();
    pending.package = package;
    pending.build = build;
    pending.notes = package + HEADER_SIZE;
    pending.notes_len = notes_len;
    pending.image = image;
    pending.image_len = image_len;
    irq_restore(flags);
    return true;
}

static bool check(bool verbose)
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
        return pending.package != NULL;
    }

    /* 1. Welche Version gibt es? */
    if (!http_get(UPDATE_URL "/helloos.version", 64, &response)) {
        set_state(verbose, "Update-Server nicht erreichbar (%s)", response.error);
        return pending.package != NULL;
    }
    uint32_t available = 0;
    bool listed = response.status == 200 && parse_number(response.body, &available);
    int status = response.status;
    kfree(response.body);
    if (!listed) {
        set_state(verbose, "Auf dem Update-Server liegt nichts (Status %d)", status);
        return pending.package != NULL;
    }

    if (pending.package && pending.build >= available) {
        set_state(verbose, "Build %u steht zur Installation bereit", pending.build);
        return true;
    }
    if (available <= OS_BUILD || available <= installed_build) {
        if (installed_build)
            set_state(verbose, "Build %u ist installiert und wird beim nächsten Start aktiv",
                      installed_build);
        else
            set_state(verbose, "HelloOS ist aktuell (Build %u)", (uint32_t)OS_BUILD);
        return false;
    }

    /* 2. Paket laden und prüfen */
    set_state(verbose, "Lade Build %u ...", available);
    if (!http_get(UPDATE_URL "/helloos.upd", PACKAGE_MAX, &response)) {
        set_state(verbose, "Download fehlgeschlagen (%s)", response.error);
        return pending.package != NULL;
    }
    if (response.status != 200) {
        set_state(verbose, "Download fehlgeschlagen (Status %d)", response.status);
        kfree(response.body);
        return pending.package != NULL;
    }
    if (!accept_package(verbose, (uint8_t *)response.body, response.length)) {
        kfree(response.body);
        return pending.package != NULL;
    }
    set_state(verbose, "Build %u ist geprüft und steht zur Installation bereit",
              pending.build);
    gui_refresh_taskbar();
    return true;
}

bool update_check(bool verbose)
{
    if (!begin(verbose))
        return false;
    bool available = check(verbose);
    busy = false;
    return available;
}

/* --- Installation ------------------------------------------------------------------------ */

/* Sucht den Datenträger mit der Startdatei von HelloOS und tauscht sie aus. */
static bool install_unlocked(bool verbose, const uint8_t *image, uint32_t size)
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
            uint8_t *check_buffer = kmalloc(size);
            ok = check_buffer && fat_find(&fs, BOOT_FILE, &file) &&
                 file.size == size && fat_read(&fs, &file, check_buffer) &&
                 memcmp(check_buffer, image, size) == 0;
            kfree(check_buffer);
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

static bool install(bool verbose, const uint8_t *image, uint32_t size)
{
    /* Währenddessen darf niemand sonst auf den Stick schreiben. */
    storage_lock();
    bool ok = install_unlocked(verbose, image, size);
    storage_unlock();
    return ok;
}

bool update_install(bool verbose)
{
    if (!begin(verbose))
        return false;

    bool ok = false;
    if (!pending.package) {
        set_state(verbose, "Es liegt kein Update bereit");
    } else if (install(verbose, pending.image, pending.image_len)) {
        installed_build = pending.build;
        forget_pending();
        set_state(verbose, "Build %u installiert - wird beim nächsten Start aktiv",
                  installed_build);
        ok = true;
    }
    busy = false;
    gui_refresh_taskbar();
    return ok;
}

void update_decline(void)
{
    pending.declined = true;
}

/* --- Hintergrund-Task -------------------------------------------------------------------- */

static void update_task(void *arg)
{
    (void)arg;
    uint32_t announced = 0;             /* für diesen Build wurde schon gefragt */

    for (;;) {
        /* Auf Netzwerk warten, dann kurz Luft lassen (USB-Stick erkennen) */
        while (!net_wait_configured(60000))
            ;
        task_sleep(4000);

        if (update_check(false) && pending.build != announced) {
            announced = pending.build;
            /* Nachfragen: mit Oberfläche per Fenster, sonst als Hinweis */
            if (!gui_launch("update"))
                klog("Update: Build %u verfügbar - mit \"update\" installieren\n",
                     pending.build);
        }
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

/* --- Auskunft -------------------------------------------------------------------------------- */

void update_get_info(update_info_t *out)
{
    uint64_t flags = irq_save();
    out->enabled = enabled;
    out->build = OS_BUILD;
    out->available_build = pending.package ? pending.build : 0;
    out->installed_build = installed_build;
    out->declined = pending.declined;
    out->url = UPDATE_URL;
    strlcpy(out->state, state, sizeof(out->state));
    irq_restore(flags);
}

uint32_t update_current_build(void)
{
    return OS_BUILD;
}

const char *update_release_notes(void)
{
    return RELEASE_NOTES;
}

size_t update_available_notes(char *out, size_t size)
{
    size_t n = 0;
    uint64_t flags = irq_save();
    if (pending.package && size > 0) {
        n = pending.notes_len < size - 1 ? pending.notes_len : size - 1;
        memcpy(out, pending.notes, n);
    }
    irq_restore(flags);
    if (size > 0)
        out[n] = '\0';
    return n;
}
