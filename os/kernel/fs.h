/* RAM-Dateisystem: flache Liste von Dateien, die im Kernel-Heap liegen */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#define FS_MAX_FILES 32
#define FS_NAME_MAX  32

typedef struct {
    bool used;
    char name[FS_NAME_MAX];
    char *data;
    size_t size;
    size_t capacity;
} file_t;

enum {
    FS_OK = 0,
    FS_ERR_NAME = -1,                   /* ungültiger Dateiname */
    FS_ERR_FULL = -2,                   /* keine freien Einträge */
    FS_ERR_MEMORY = -3,
    FS_ERR_NOT_FOUND = -4,
};

void fs_init(void);

const file_t *fs_find(const char *name);

/* Schreibt (oder hängt an) und legt die Datei bei Bedarf an. */
int fs_write(const char *name, const char *data, size_t len, bool append);
int fs_remove(const char *name);

/* Für Auflistungen: Eintrag index oder NULL, wenn er unbenutzt ist. */
const file_t *fs_entry(int index);
