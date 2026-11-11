#include "fs.h"
#include "cpu.h"
#include "fat.h"
#include "memory.h"
#include "storage.h"
#include "string.h"
#include "task.h"

static file_t files[FS_MAX_FILES];

/* --- Sperre: Änderungen laufen nacheinander ------------------------------------------- */

static bool locked;
static uint32_t lock_owner;

static void lock(void)
{
    for (;;) {
        uint64_t flags = irq_save();
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

static void unlock(void)
{
    locked = false;
}

/* --- Namen --------------------------------------------------------------------------- */

static file_t *find(const char *name)
{
    if (!*name)
        return NULL;
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (files[i].used && fat_same_name(files[i].name, name))
            return &files[i];
    }
    return NULL;
}

/* Erlaubt ist, was auch auf dem Stick (FAT32) und unter Windows geht. */
static bool valid_name(const char *name)
{
    size_t len = strlen(name);
    if (len == 0 || len >= FS_NAME_MAX || name[0] == '.' ||
        name[len - 1] == '.')
        return false;
    for (; *name; name++) {
        unsigned char c = (unsigned char)*name;
        if (c <= ' ' || c == 127 || c == '/' || c == '\\' || c == ':' ||
            c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
            c == '|' || c >= 0xF0)
            return false;
    }
    return true;
}

const file_t *fs_find(const char *name)
{
    return find(name);
}

const file_t *fs_entry(int index)
{
    if (index < 0 || index >= FS_MAX_FILES || !files[index].used)
        return NULL;
    return &files[index];
}

const char *fs_error_text(int error)
{
    switch (error) {
    case FS_OK:            return "in Ordnung";
    case FS_ERR_NAME:      return "ungültiger Dateiname";
    case FS_ERR_FULL:      return "zu viele Dateien";
    case FS_ERR_NOT_FOUND: return "Datei nicht gefunden";
    case FS_ERR_SIZE:      return "Datei zu groß";
    default:               return "nicht genug Speicher";
    }
}

/* --- Ändern --------------------------------------------------------------------------- */

static file_t *new_entry(const char *name)
{
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (!files[i].used) {
            memset(&files[i], 0, sizeof(files[i]));
            strlcpy(files[i].name, name, FS_NAME_MAX);
            return &files[i];
        }
    }
    return NULL;
}

int fs_write(const char *name, const char *data, size_t len, bool append)
{
    if (!valid_name(name))
        return FS_ERR_NAME;

    lock();
    int result = FS_OK;
    file_t *f = find(name);
    size_t old_size = (f && append) ? f->size : 0;
    size_t new_size = old_size + len;

    /* Immer in einen neuen Puffer - der Eintrag zeigt nie auf halbfertige Daten. */
    char *buf = new_size <= FS_FILE_MAX ? kmalloc(new_size + 1) : NULL;
    if (new_size > FS_FILE_MAX)
        result = FS_ERR_SIZE;
    else if (!buf)
        result = FS_ERR_MEMORY;
    else if (!f && !(f = new_entry(name)))
        result = FS_ERR_FULL;

    if (result == FS_OK) {
        if (old_size)
            memcpy(buf, f->data, old_size);
        memcpy(buf + old_size, data, len);
        buf[new_size] = '\0';

        uint64_t flags = irq_save();
        char *old = f->data;
        f->data = buf;
        f->size = new_size;
        f->capacity = new_size + 1;
        f->used = true;
        f->builtin = false;
        irq_restore(flags);
        kfree(old);
        f->saved = storage_save(STORAGE_FILES, f->name, f->data, f->size);
    } else {
        kfree(buf);
    }
    unlock();
    return result;
}

int fs_remove(const char *name)
{
    lock();
    file_t *f = find(name);
    int result = f ? FS_OK : FS_ERR_NOT_FOUND;
    if (f) {
        char stored[FS_NAME_MAX];
        bool on_stick = !f->builtin;
        strlcpy(stored, f->name, sizeof(stored));
        kfree(f->data);
        memset(f, 0, sizeof(*f));
        if (on_stick)
            storage_remove(STORAGE_FILES, stored);
    }
    unlock();
    return result;
}

int fs_read(const char *name, char **data, size_t *size)
{
    lock();
    const file_t *f = find(name);
    int result = f ? FS_OK : FS_ERR_NOT_FOUND;
    *data = NULL;
    *size = 0;
    if (f) {
        *data = kmalloc(f->size + 1);
        if (*data) {
            memcpy(*data, f->data, f->size);
            (*data)[f->size] = '\0';
            *size = f->size;
        } else {
            result = FS_ERR_MEMORY;
        }
    }
    unlock();
    return result;
}

/* --- Zusammenspiel mit dem Stick --------------------------------------------------------- */

int fs_adopt(const char *name, char *data, size_t size)
{
    if (!valid_name(name))
        return FS_ERR_NAME;

    lock();
    int result = FS_OK;
    file_t *f = find(name);
    if (f && !f->builtin) {
        result = FS_ERR_FULL;           /* inzwischen neu angelegt: die bleibt */
    } else if (!f && !(f = new_entry(name))) {
        result = FS_ERR_FULL;
    } else {
        data[size] = '\0';
        kfree(f->data);
        strlcpy(f->name, name, FS_NAME_MAX);
        f->data = data;
        f->size = size;
        f->capacity = size + 1;
        f->saved = true;
        f->builtin = false;
        f->used = true;
    }
    unlock();
    return result;
}

void fs_storage_ready(void)
{
    lock();
    for (int i = 0; i < FS_MAX_FILES; i++) {
        file_t *f = &files[i];
        if (f->used && !f->saved && !f->builtin)
            f->saved = storage_save(STORAGE_FILES, f->name, f->data, f->size);
    }
    unlock();
}

void fs_init(void)
{
    static const char welcome[] =
        "Willkommen bei HelloOS!\n"
        "\n"
        "Deine Dateien liegen im Arbeitsspeicher und werden auf dem Datenträger\n"
        "gesichert, von dem HelloOS gestartet wurde (Ordner DATEIEN). \"ls\"\n"
        "zeigt, ob eine Datei gesichert ist.\n"
        "\n"
        "Probier mal:\n"
        "  write notiz.txt Hallo Welt\n"
        "  edit notiz.txt\n"
        "  ls\n";
    file_t *f = new_entry("willkommen.txt");
    f->data = kmalloc(sizeof(welcome));
    if (!f->data)
        return;
    memcpy(f->data, welcome, sizeof(welcome));
    f->size = sizeof(welcome) - 1;
    f->capacity = sizeof(welcome);
    f->used = true;
    f->builtin = true;
}
