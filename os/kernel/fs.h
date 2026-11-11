/*
 * Eigene Dateien: eine flache Liste im Arbeitsspeicher. Steckt der USB-Stick,
 * von dem HelloOS gestartet wurde, wird jede Änderung dort im Ordner DATEIEN
 * gesichert und beim nächsten Start wieder geladen (siehe storage.h).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#define FS_MAX_FILES 256
#define FS_NAME_MAX  64
#define FS_FILE_MAX  (32u * 1024 * 1024)

typedef struct {
    bool used;
    bool saved;                         /* liegt in dieser Fassung auf dem Stick */
    bool builtin;                       /* mitgelieferter Text, wird nicht gesichert */
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
    FS_ERR_SIZE = -5,                   /* Datei zu groß */
};

void fs_init(void);

/* Groß- und Kleinschreibung spielt bei Namen keine Rolle (wie auf dem Stick). */
const file_t *fs_find(const char *name);

/* Schreibt (oder hängt an) und legt die Datei bei Bedarf an. */
int fs_write(const char *name, const char *data, size_t len, bool append);
int fs_remove(const char *name);

/* Liefert eine eigene Kopie des Inhalts (kfree), mit Nullbyte dahinter. */
int fs_read(const char *name, char **data, size_t *size);

/* Für Auflistungen: Eintrag index oder NULL, wenn er unbenutzt ist. */
const file_t *fs_entry(int index);

/* Text zu einem Fehlercode - für Meldungen an den Benutzer. */
const char *fs_error_text(int error);

/* --- Für storage.c ---------------------------------------------------------------- */

/* Übernimmt eine vom Stick gelesene Datei samt Puffer (size + 1 Bytes). */
int fs_adopt(const char *name, char *data, size_t size);
/* Der Stick ist da: bisher nur im Arbeitsspeicher liegende Dateien sichern. */
void fs_storage_ready(void);
