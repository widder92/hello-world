#include "fs.h"
#include "memory.h"
#include "string.h"

static file_t files[FS_MAX_FILES];

static file_t *find(const char *name)
{
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (files[i].used && strcmp(files[i].name, name) == 0)
            return &files[i];
    }
    return NULL;
}

static bool valid_name(const char *name)
{
    size_t len = strlen(name);
    if (len == 0 || len >= FS_NAME_MAX)
        return false;
    for (; *name; name++) {
        if ((unsigned char)*name <= ' ' || *name == '/')
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

int fs_write(const char *name, const char *data, size_t len, bool append)
{
    if (!valid_name(name))
        return FS_ERR_NAME;

    file_t *f = find(name);
    size_t old_size = (f && append) ? f->size : 0;
    size_t new_size = old_size + len;

    /* Erst den Speicher besorgen, dann den Eintrag anfassen */
    char *buf = NULL;
    size_t capacity = f ? f->capacity : 0;
    if (!f || new_size > f->capacity) {
        capacity = new_size < 64 ? 64 : new_size * 2;
        buf = kmalloc(capacity);
        if (!buf)
            return FS_ERR_MEMORY;
    }

    if (!f) {
        for (int i = 0; i < FS_MAX_FILES && !f; i++) {
            if (!files[i].used)
                f = &files[i];
        }
        if (!f) {
            kfree(buf);
            return FS_ERR_FULL;
        }
        memset(f, 0, sizeof(*f));
        f->used = true;
        strlcpy(f->name, name, FS_NAME_MAX);
    }

    if (buf) {
        if (old_size)
            memcpy(buf, f->data, old_size);
        kfree(f->data);
        f->data = buf;
        f->capacity = capacity;
    }
    memcpy(f->data + old_size, data, len);
    f->size = new_size;
    return FS_OK;
}

int fs_remove(const char *name)
{
    file_t *f = find(name);
    if (!f)
        return FS_ERR_NOT_FOUND;
    kfree(f->data);
    memset(f, 0, sizeof(*f));
    return FS_OK;
}

void fs_init(void)
{
    static const char welcome[] =
        "Willkommen bei HelloOS!\n"
        "\n"
        "Dieses Dateisystem lebt komplett im Arbeitsspeicher. Alles, was du\n"
        "hier ablegst, ist nach einem Neustart wieder weg.\n"
        "\n"
        "Probier mal:\n"
        "  write notiz.txt Hallo Welt\n"
        "  cat notiz.txt\n"
        "  spawn counter 5\n"
        "  ps\n";
    fs_write("willkommen.txt", welcome, sizeof(welcome) - 1, false);
}
