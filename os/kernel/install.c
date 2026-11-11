/*
 * Start-Assistent: richtet HelloOS auf einer eigenen Partition der internen
 * Platte ein. Er läuft ganz am Anfang, solange die Firmware noch aktiv ist -
 * und lässt *sie* die Arbeit tun: Die Startdatei schreibt der Dateisystem-
 * treiber der Firmware, den Eintrag im Startmenü des PCs legt sie selbst an.
 * HelloOS fasst dabei keinen Sektor mit eigenem Code an.
 *
 * Voraussetzung ist eine FAT32-Partition namens HELLOOS, die der Benutzer
 * vorher (unter Windows) angelegt hat. Andere Partitionen werden nicht einmal
 * geöffnet - bis auf die Abfrage ihres Namens.
 */
#include "install.h"
#include "bootinfo.h"
#include "serial.h"
#include "storage.h"
#include "string.h"

#define BOOT_PATH  u"\\EFI\\BOOT\\BOOTX64.EFI"
#define ENTRY_NAME u"HelloOS"
#define IMAGE_MAX  (16 * 1024 * 1024)
#define VARIABLE_ATTRIBUTES 7           /* dauerhaft, beim Start und im Betrieb sichtbar */

static const efi_guid_t loaded_image_guid = EFI_LOADED_IMAGE_GUID;
static const efi_guid_t device_path_guid = EFI_DEVICE_PATH_GUID;
static const efi_guid_t file_system_guid = EFI_FILE_SYSTEM_GUID;
static const efi_guid_t file_info_guid = EFI_FILE_INFO_GUID;
static const efi_guid_t volume_label_guid = EFI_VOLUME_LABEL_GUID;
static const efi_guid_t global_guid = EFI_GLOBAL_VARIABLE_GUID;

static efi_system_table_t *system_table;
static efi_boot_services_t *bs;

/* Text auf den Bildschirm der Firmware und zur Kontrolle auf die serielle Leitung. */
static void say(const uint16_t *text)
{
    char ascii[2] = { 0, 0 };
    system_table->con_out->output_string(system_table->con_out, text);
    for (; *text; text++) {
        if (*text == '\r')
            continue;
        ascii[0] = *text < 0x7F ? (char)*text : '?';
        serial_write(ascii);
    }
}

static bool same16(const uint16_t *a, const uint16_t *b)
{
    for (;; a++, b++) {
        uint16_t x = *a, y = *b;
        if (x >= 'a' && x <= 'z')
            x -= 32;
        if (y >= 'a' && y <= 'z')
            y -= 32;
        if (x != y)
            return false;
        if (!x)
            return true;
    }
}

static size_t node_length(const efi_device_path_t *node)
{
    return (size_t)node->length[0] | ((size_t)node->length[1] << 8);
}

static bool end_node(const efi_device_path_t *node)
{
    return node->type == 0x7F || node_length(node) < 4;
}

/* --- Von welcher Partition wurde gestartet? ------------------------------------------- */

static void note_boot_partition(efi_handle_t device)
{
    efi_device_path_t *path = NULL;
    if (EFI_ERROR(bs->handle_protocol(device, &device_path_guid, (void **)&path)) || !path)
        return;
    boot_info.boot_known = true;
    boot_info.boot_partition_start = 0; /* Datenträger ohne Partitionstabelle */
    for (const efi_device_path_t *node = path; !end_node(node);
         node = (const efi_device_path_t *)((const uint8_t *)node + node_length(node))) {
        if (node->type == 4 && node->subtype == 1 && node_length(node) >= 24)
            memcpy(&boot_info.boot_partition_start, (const uint8_t *)node + 8, 8);
        /* Glieder für SATA (18) und NVMe (23) verraten eine eingebaute Platte. */
        if (node->type == 3 && (node->subtype == 18 || node->subtype == 23))
            boot_info.boot_internal = true;
    }
}

/* --- Dateien -------------------------------------------------------------------------------- */

/* Liest eine ganze Datei in neu besorgten Speicher; NULL, wenn es sie nicht gibt. */
static uint8_t *read_file(efi_file_t *root, const uint16_t *path, uint64_t *size)
{
    efi_file_t *file = NULL;
    uint8_t info_buffer[512];
    uint64_t info_size = sizeof(info_buffer);
    uint8_t *data = NULL;

    if (EFI_ERROR(root->open(root, &file, path, EFI_FILE_MODE_READ, 0)) || !file)
        return NULL;
    if (!EFI_ERROR(file->get_info(file, &file_info_guid, &info_size, info_buffer))) {
        const efi_file_info_t *info = (const efi_file_info_t *)info_buffer;
        *size = info->file_size;
        if (*size > 0 && *size <= IMAGE_MAX &&
            !EFI_ERROR(bs->allocate_pool(EFI_LOADER_DATA, *size, (void **)&data)) && data) {
            uint64_t got = *size;
            if (EFI_ERROR(file->read(file, &got, data)) || got != *size) {
                bs->free_pool(data);
                data = NULL;
            }
        }
    }
    file->close(file);
    return data;
}

static bool make_directory(efi_file_t *root, const uint16_t *path)
{
    efi_file_t *dir = NULL;
    if (EFI_ERROR(root->open(root, &dir, path,
                             EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
                             EFI_FILE_DIRECTORY)) || !dir)
        return false;
    dir->close(dir);
    return true;
}

static bool write_file(efi_file_t *root, const uint16_t *path, const uint8_t *data,
                       uint64_t size)
{
    efi_file_t *file = NULL;
    uint64_t mode = EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE;

    if (EFI_ERROR(root->open(root, &file, path, mode, 0)) || !file)
        return false;
    uint64_t written = size;
    bool ok = !EFI_ERROR(file->write(file, &written, data)) && written == size &&
              !EFI_ERROR(file->flush(file));
    file->close(file);
    return ok;
}

/* --- Eintrag im Startmenü des PCs --------------------------------------------------------- */

static void entry_variable(unsigned number, uint16_t name[9])
{
    static const char hex[] = "0123456789ABCDEF";
    memcpy(name, u"Boot", 8);
    for (int i = 0; i < 4; i++)
        name[4 + i] = (uint16_t)hex[(number >> (12 - 4 * i)) & 15];
    name[8] = 0;
}

/* Gibt es schon einen Eintrag "HelloOS"? Liefert seine Nummer oder -1. */
static int find_entry(void)
{
    uint8_t option[1024];
    for (unsigned number = 0; number < 0x40; number++) {
        uint16_t name[9];
        uint64_t size = sizeof(option);
        entry_variable(number, name);
        if (EFI_ERROR(system_table->runtime_services->get_variable(name, &global_guid, NULL,
                                                                   &size, option)))
            continue;
        if (size > 6 + sizeof(ENTRY_NAME) - 2 && same16((const uint16_t *)(option + 6), ENTRY_NAME))
            return (int)number;
    }
    return -1;
}

/* Legt den Eintrag an: Beschreibung plus Weg zur Startdatei auf der Zielpartition. */
static bool create_entry(efi_handle_t target)
{
    efi_runtime_services_t *rt = system_table->runtime_services;
    efi_device_path_t *path = NULL;
    uint8_t option[1024];
    size_t n = 0;

    if (EFI_ERROR(bs->handle_protocol(target, &device_path_guid, (void **)&path)) || !path)
        return false;
    /*
     * Kurzform: Der Weg beginnt beim Glied der Partition, die die Firmware an
     * ihrer eindeutigen Kennung wiederfindet - gleich, an welchem Steckplatz
     * die Platte hängt. So sind auch die Einträge von Windows aufgebaut.
     */
    for (const efi_device_path_t *node = path; !end_node(node);
         node = (const efi_device_path_t *)((const uint8_t *)node + node_length(node))) {
        if (node->type == 4 && node->subtype == 1) {
            path = (efi_device_path_t *)node;
            break;
        }
    }
    size_t path_bytes = 0;
    for (const efi_device_path_t *node = path; !end_node(node);
         node = (const efi_device_path_t *)((const uint8_t *)node + node_length(node)))
        path_bytes += node_length(node);
    size_t file_bytes = 4 + sizeof(BOOT_PATH);          /* Kopf + Pfad samt Endezeichen */
    size_t list_bytes = path_bytes + file_bytes + 4;
    if (6 + sizeof(ENTRY_NAME) + list_bytes > sizeof(option))
        return false;

    /* Aufbau: Merkmale (aktiv), Länge der Wegbeschreibung, Name, Wegbeschreibung */
    uint32_t attributes = 1;
    uint16_t list_length = (uint16_t)list_bytes;
    memcpy(option + n, &attributes, 4);
    n += 4;
    memcpy(option + n, &list_length, 2);
    n += 2;
    memcpy(option + n, ENTRY_NAME, sizeof(ENTRY_NAME));
    n += sizeof(ENTRY_NAME);
    memcpy(option + n, path, path_bytes);
    n += path_bytes;
    option[n++] = 4;                    /* Glied "Datei auf diesem Dateisystem" */
    option[n++] = 4;
    option[n++] = (uint8_t)file_bytes;
    option[n++] = (uint8_t)(file_bytes >> 8);
    memcpy(option + n, BOOT_PATH, sizeof(BOOT_PATH));
    n += sizeof(BOOT_PATH);
    option[n++] = 0x7F;                 /* Ende der Wegbeschreibung */
    option[n++] = 0xFF;
    option[n++] = 4;
    option[n++] = 0;

    /* Eine freie Nummer suchen (oder den alten HelloOS-Eintrag überschreiben) */
    int number = find_entry();
    for (unsigned candidate = 0; number < 0 && candidate < 0x100; candidate++) {
        uint16_t name[9];
        uint8_t probe[4];
        uint64_t size = sizeof(probe);
        entry_variable(candidate, name);
        if (rt->get_variable(name, &global_guid, NULL, &size, probe) == EFI_NOT_FOUND)
            number = (int)candidate;
    }
    if (number < 0)
        return false;
    uint16_t name[9];
    entry_variable((unsigned)number, name);
    if (EFI_ERROR(rt->set_variable(name, &global_guid, VARIABLE_ATTRIBUTES, n, option)))
        return false;

    /* Hinten an die Startreihenfolge hängen - Windows bleibt das, was von allein startet. */
    uint16_t order[130];
    uint64_t size = sizeof(order) - 2 * sizeof(uint16_t);
    uint32_t order_attributes = VARIABLE_ATTRIBUTES;
    efi_status_t status = rt->get_variable(u"BootOrder", &global_guid, &order_attributes,
                                           &size, order);
    if (status == EFI_NOT_FOUND)
        size = 0;
    else if (EFI_ERROR(status))
        return true;                    /* Eintrag steht; die Reihenfolge lassen wir, wie sie ist */
    size_t count = size / 2;
    for (size_t i = 0; i < count; i++) {
        if (order[i] == (uint16_t)number)
            return true;
    }
    order[count++] = (uint16_t)number;
    rt->set_variable(u"BootOrder", &global_guid, VARIABLE_ATTRIBUTES, count * 2, order);
    return true;
}

/* --- Die Rückfrage ---------------------------------------------------------------------------- */

/* Wartet auf eine Taste; liefert 0, wenn in der Zeit keine kam. */
static uint16_t wait_key(unsigned seconds)
{
    efi_text_input_t *in = system_table->con_in;
    efi_input_key_t key;
    while (in && !EFI_ERROR(in->read_key(in, &key)))
        ;                               /* alte Tastendrücke verwerfen */
    for (unsigned i = 0; in && i < seconds * 10; i++) {
        if (!EFI_ERROR(in->read_key(in, &key)))
            return key.character ? key.character : 0xFFFF;
        bs->stall(100000);
    }
    return 0;
}

static void remember(const char *note)
{
    strlcpy(boot_info.install_note, note, sizeof(boot_info.install_note));
}

/*
 * Viele PCs starten "schnell" und sprechen dabei nur den Datenträger an, von
 * dem sie laden. Hier werden die Treiber der Firmware für alle Steckkarten und
 * Controller gestartet - erst danach sind die Partitionen der internen Platte
 * zu sehen.
 */
static void connect_disks(void)
{
    static const efi_guid_t pci_guid = EFI_PCI_IO_GUID;
    efi_handle_t *handles = NULL;
    uint64_t count = 0;
    if (EFI_ERROR(bs->locate_handle_buffer(2, &pci_guid, NULL, &count, &handles)))
        return;
    for (uint64_t i = 0; i < count; i++)
        bs->connect_controller(handles[i], NULL, NULL, 1);
    bs->free_pool(handles);
}

/* Sucht die Partition HELLOOS (nicht die, von der wir kommen) und bietet das Einrichten an. */
static void offer_install(efi_handle_t boot_device)
{
    efi_file_system_t *source_fs = NULL;
    efi_file_t *source_root = NULL;
    efi_handle_t *handles = NULL;
    uint64_t count = 0;

    if (EFI_ERROR(bs->handle_protocol(boot_device, &file_system_guid, (void **)&source_fs)) ||
        !source_fs || EFI_ERROR(source_fs->open_volume(source_fs, &source_root)) ||
        !source_root)
        return;
    connect_disks();
    if (EFI_ERROR(bs->locate_handle_buffer(2, &file_system_guid, NULL, &count, &handles)))
        count = 0;

    for (uint64_t i = 0; i < count; i++) {
        efi_file_system_t *fs = NULL;
        efi_file_t *root = NULL;
        uint16_t label[64];
        uint64_t label_size = sizeof(label) - 2;

        if (handles[i] == boot_device ||
            EFI_ERROR(bs->handle_protocol(handles[i], &file_system_guid, (void **)&fs)) ||
            !fs || EFI_ERROR(fs->open_volume(fs, &root)) || !root)
            continue;
        memset(label, 0, sizeof(label));
        bool ours = !EFI_ERROR(root->get_info(root, &volume_label_guid, &label_size, label)) &&
                    same16(label, u"" HELLOOS_VOLUME_LABEL);
        if (!ours) {
            root->close(root);
            continue;
        }

        /* Was fehlt dort: die Startdatei, der Eintrag im Startmenü - oder nichts? */
        uint64_t have_size = 0;
        uint8_t *have = read_file(root, BOOT_PATH, &have_size);
        bool need_file = have == NULL;
        bool need_entry = find_entry() < 0;
        if (have)
            bs->free_pool(have);
        if (!need_file && !need_entry) {
            root->close(root);
            break;                      /* alles schon eingerichtet */
        }

        say(u"\r\n=== HelloOS einrichten ===\r\n\r\n");
        if (need_file) {
            say(u"Auf der internen Platte gibt es eine Partition namens HELLOOS,\r\n"
                u"auf der HelloOS noch nicht eingerichtet ist.\r\n\r\n"
                u"Soll HelloOS dort jetzt eingerichtet werden?\r\n"
                u"  - Die Startdatei wird auf diese Partition kopiert.\r\n"
                u"  - Im Startmenue des PCs entsteht der Eintrag \"HelloOS\".\r\n"
                u"  - Windows und alle anderen Partitionen bleiben unberuehrt.\r\n\r\n");
        } else {
            say(u"HelloOS liegt schon auf der Partition HELLOOS der internen Platte,\r\n"
                u"aber im Startmenue des PCs fehlt der Eintrag \"HelloOS\".\r\n\r\n"
                u"Soll er jetzt angelegt werden?\r\n\r\n");
        }
        say(u"J = ja     jede andere Taste = nicht jetzt     (ohne Eingabe geht es in\r\n"
            u"30 Sekunden normal weiter)\r\n");
        uint16_t key = wait_key(30);
        if (key != 'j' && key != 'J') {
            say(u"\r\nNicht eingerichtet - HelloOS startet jetzt vom Stick.\r\n");
            root->close(root);
            break;
        }

        bool ok = true;
        if (need_file) {
            uint64_t size = 0;
            uint8_t *image = read_file(source_root, BOOT_PATH, &size);
            say(u"\r\nKopiere die Startdatei ...\r\n");
            ok = image && make_directory(root, u"\\EFI") &&
                 make_directory(root, u"\\EFI\\BOOT") &&
                 write_file(root, BOOT_PATH, image, size);
            if (ok) {                   /* zur Kontrolle zurücklesen und vergleichen */
                uint64_t check_size = 0;
                uint8_t *check = read_file(root, BOOT_PATH, &check_size);
                ok = check && check_size == size && memcmp(check, image, size) == 0;
                if (check)
                    bs->free_pool(check);
            }
            if (image)
                bs->free_pool(image);
            if (!ok)
                say(u"Das Kopieren ist fehlgeschlagen. Es wurde nichts weiter veraendert.\r\n");
        }
        if (ok) {
            say(u"Lege den Eintrag im Startmenue des PCs an ...\r\n");
            ok = create_entry(handles[i]);
            if (!ok)
                say(u"Der Eintrag liess sich nicht anlegen. Die Startdatei liegt bereit -\r\n"
                    u"der Eintrag kann im UEFI-Setup des PCs von Hand angelegt werden.\r\n");
        }
        if (ok) {
            say(u"\r\nFertig. So geht es weiter:\r\n"
                u"  1. HelloOS herunterfahren und den USB-Stick abziehen.\r\n"
                u"  2. PC einschalten und das Startmenue aufrufen (meist F12, F11, F8 oder Esc).\r\n"
                u"  3. \"HelloOS\" waehlen. Ohne Auswahl startet weiterhin Windows.\r\n\r\n"
                u"Weiter mit einer Taste ...\r\n");
            remember("HelloOS wurde auf der internen Platte eingerichtet - siehe \"disk\".");
        } else {
            say(u"\r\nWeiter mit einer Taste ...\r\n");
            remember("Das Einrichten auf der internen Platte ist fehlgeschlagen.");
        }
        wait_key(60);
        root->close(root);
        break;
    }

    if (handles)
        bs->free_pool(handles);
    source_root->close(source_root);
}

void install_assistant(efi_handle_t image, efi_system_table_t *st)
{
    efi_loaded_image_t *loaded = NULL;

    system_table = st;
    bs = st->boot_services;
    if (EFI_ERROR(bs->handle_protocol(image, &loaded_image_guid, (void **)&loaded)) ||
        !loaded || !loaded->device)
        return;
    note_boot_partition(loaded->device);
    offer_install(loaded->device);
}
