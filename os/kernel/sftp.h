/*
 * SFTP: Dateien auf einem Server ansehen, holen und ablegen - über eine
 * SSH-Verbindung (siehe ssh.h), also verschlüsselt. Das ist das Protokoll,
 * das auch Programme wie FileZilla für "SFTP" verwenden (Version 3).
 */
#pragma once

#include "ssh.h"

typedef struct sftp sftp_t;

typedef struct {
    char name[256];
    bool directory;
    bool link;                          /* Verknüpfung (zeigt auf Datei oder Ordner) */
    uint64_t size;
    uint32_t mtime;                     /* Sekunden seit 1970, 0 wenn unbekannt */
} sftp_entry_t;

/* Fortschritt einer Übertragung; total ist 0, wenn die Größe unbekannt ist. */
typedef void (*sftp_progress_t)(void *ctx, uint64_t done, uint64_t total);

/* Verbindet, meldet an und startet SFTP. Bei Fehlern NULL und ein Text in error. */
sftp_t *sftp_connect(const ssh_options_t *options, char *error, size_t error_size);
void sftp_close(sftp_t *s);

/* Erklärung, wenn eine der folgenden Funktionen false liefert. */
const char *sftp_error(const sftp_t *s);
/* false: Die Verbindung selbst ist verloren - dann hilft nur neu verbinden. */
bool sftp_alive(const sftp_t *s);

/* Macht aus einem Pfad ("." für das Startverzeichnis) einen vollständigen. */
bool sftp_realpath(sftp_t *s, const char *path, char *out, size_t size);

/* Listet ein Verzeichnis: Ordner zuerst, dann nach Namen. entries mit kfree freigeben. */
bool sftp_list(sftp_t *s, const char *path, sftp_entry_t **entries, int *count);

/* Lädt eine Datei in einen neuen Puffer (kfree); max begrenzt die Größe. */
bool sftp_download(sftp_t *s, const char *path, size_t max, char **data,
                   size_t *size, sftp_progress_t progress, void *ctx);
/* Legt die Datei an bzw. überschreibt sie. */
bool sftp_upload(sftp_t *s, const char *path, const void *data, size_t size,
                 sftp_progress_t progress, void *ctx);

bool sftp_remove(sftp_t *s, const char *path);
bool sftp_mkdir(sftp_t *s, const char *path);
bool sftp_rmdir(sftp_t *s, const char *path);
bool sftp_rename(sftp_t *s, const char *from, const char *to);

/* Hängt name an ein Verzeichnis an ("/var" + "www" -> "/var/www"; ".." geht hoch). */
void sftp_join(const char *directory, const char *name, char *out, size_t size);
