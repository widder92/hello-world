#include "storage.h"
#include "block.h"
#include "console.h"
#include "cpu.h"
#include "fat.h"
#include "fs.h"
#include "memory.h"
#include "string.h"
#include "task.h"

#define BOOT_FILE "EFI/BOOT/BOOTX64.EFI"

static bool available;

/* --- Sperre ------------------------------------------------------------------------- */

static bool locked;
static uint32_t lock_owner;

void storage_lock(void)
{
    for (;;) {
        uint64_t flags = irq_save();
        /* Wurde der Besitzer mittendrin beendet, gilt die Sperre als frei. */
        if (!locked || !task_alive(lock_owner)) {
            locked = true;
            lock_owner = task_current_pid();
            irq_restore(flags);
            return;
        }
        irq_restore(flags);
        task_sleep(5);
    }
}

void storage_unlock(void)
{
    locked = false;
}

/* --- Zugriff auf den Stick ------------------------------------------------------------ */

/* Hängt den Datenträger ein, von dem HelloOS gestartet wurde. */
static bool mount_stick(fat_t *fs)
{
    for (int i = 0; i < BLOCK_DEVICES_MAX; i++) {
        block_device_t *dev = block_get(i);
        fat_file_t boot;
        if (!dev || !fat_mount(fs, dev))
            continue;
        if (fat_find(fs, BOOT_FILE, &boot) && !boot.directory)
            return true;
        fat_unmount(fs);
    }
    return false;
}

static bool open_folder(fat_t *fs, const char *folder, bool create, fat_file_t *out)
{
    if (fat_lookup(fs, fs->root_cluster, folder, out))
        return out->directory;
    return create && fat_create(fs, fs->root_cluster, folder, true, out);
}

bool storage_available(void)
{
    return available;
}

bool storage_save(const char *folder, const char *name, const void *data,
                  size_t size)
{
    fat_t fs;
    fat_file_t dir, file;
    bool ok = false;

    if (size > 0xFFFFFFFFu)
        return false;
    storage_lock();
    available = mount_stick(&fs);
    if (available) {
        ok = open_folder(&fs, folder, true, &dir) &&
             (fat_lookup(&fs, dir.first_cluster, name, &file)
                  ? !file.directory
                  : fat_create(&fs, dir.first_cluster, name, false, &file)) &&
             fat_replace(&fs, &file, data, (uint32_t)size);
        fat_unmount(&fs);
    }
    storage_unlock();
    return ok;
}

bool storage_remove(const char *folder, const char *name)
{
    fat_t fs;
    fat_file_t dir, file;
    bool ok = false;

    storage_lock();
    available = mount_stick(&fs);
    if (available) {
        if (!open_folder(&fs, folder, false, &dir) ||
            !fat_lookup(&fs, dir.first_cluster, name, &file))
            ok = true;                  /* gibt es dort gar nicht */
        else
            ok = fat_delete(&fs, &file);
        fat_unmount(&fs);
    }
    storage_unlock();
    return ok;
}

bool storage_load(const char *folder, const char *name, size_t max, char **data,
                  size_t *size)
{
    fat_t fs;
    fat_file_t dir, file;
    bool ok = false;

    *data = NULL;
    *size = 0;
    storage_lock();
    available = mount_stick(&fs);
    if (available) {
        if (open_folder(&fs, folder, false, &dir) &&
            fat_lookup(&fs, dir.first_cluster, name, &file) && !file.directory &&
            file.size <= max) {
            *data = kmalloc((size_t)file.size + 1);
            ok = *data && fat_read(&fs, &file, *data);
            if (ok) {
                (*data)[file.size] = '\0';
                *size = file.size;
            } else {
                kfree(*data);
                *data = NULL;
            }
        }
        fat_unmount(&fs);
    }
    storage_unlock();
    return ok;
}

/* --- Beim Start: eigene Dateien in den Arbeitsspeicher holen --------------------------- */

/*
 * Gelesen wird mit gehaltener Sperre in eine Liste; in die Dateiverwaltung
 * kommen die Dateien erst danach. Sonst könnten sich dieser Task und einer,
 * der gerade eine Datei schreibt, gegenseitig blockieren.
 */
typedef struct loaded {
    struct loaded *next;
    char *data;
    size_t size;
    char name[FAT_NAME_MAX];
} loaded_t;

typedef struct {
    fat_t *fs;
    loaded_t *first;
    int skipped;
} load_t;

static bool load_visit(void *ctx, const char *name, const fat_file_t *file)
{
    load_t *l = ctx;
    if (file->directory)
        return true;

    loaded_t *item = file->size <= FS_FILE_MAX ? kmalloc(sizeof(*item)) : NULL;
    char *data = item ? kmalloc((size_t)file->size + 1) : NULL;
    if (data && fat_read(l->fs, file, data)) {
        item->data = data;
        item->size = file->size;
        strlcpy(item->name, name, sizeof(item->name));
        item->next = l->first;
        l->first = item;
    } else {
        kfree(data);
        kfree(item);
        l->skipped++;
    }
    return true;
}

static void storage_task(void *arg)
{
    (void)arg;
    /* Der USB-Stick meldet sich erst einige Zeit nach dem Start. */
    for (;;) {
        fat_t fs;
        fat_file_t dir;
        load_t l = { &fs, NULL, 0 };

        storage_lock();
        bool found = mount_stick(&fs);
        if (found) {
            available = true;
            if (open_folder(&fs, STORAGE_FILES, false, &dir))
                fat_list(&fs, dir.first_cluster, load_visit, &l);
            fat_unmount(&fs);
        }
        storage_unlock();

        if (found) {
            int loaded = 0;
            for (loaded_t *item = l.first; item; ) {
                loaded_t *next = item->next;
                if (fs_adopt(item->name, item->data, item->size) == FS_OK) {
                    loaded++;
                } else {
                    kfree(item->data);
                    l.skipped++;
                }
                kfree(item);
                item = next;
            }
            klog("Dateien: %d vom USB-Stick geladen (Ordner %s)\n", loaded,
                 STORAGE_FILES);
            if (l.skipped)
                klog("Dateien: %d übersprungen (zu groß, zu viele oder Name "
                     "nicht darstellbar)\n", l.skipped);
            fs_storage_ready();
            return;
        }
        task_sleep(1000);
    }
}

void storage_init(void)
{
    task_create("dateien", storage_task, NULL, NULL);
}
