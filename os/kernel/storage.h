/*
 * Dauerhafter Speicher: der USB-Stick, von dem HelloOS gestartet wurde.
 *
 * Eigene Dateien liegen dort im Ordner DATEIEN (auch von Windows aus zu
 * sehen), Einstellungen von HelloOS im Ordner HELLOOS. Jeder Zugriff hängt
 * das Dateisystem frisch ein und wieder aus - der Stick darf also jederzeit
 * abgezogen werden, ohne dass etwas Halbgeschriebenes zurückbleibt.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#define STORAGE_FILES    "DATEIEN"
#define STORAGE_SETTINGS "HELLOOS"

/* Wartet im Hintergrund auf den Stick und lädt dann die eigenen Dateien. */
void storage_init(void);

/* Wurde beim letzten Zugriff ein Stick gefunden? */
bool storage_available(void);

bool storage_save(const char *folder, const char *name, const void *data,
                  size_t size);
bool storage_remove(const char *folder, const char *name);
/* Liest eine Datei in einen neuen Puffer (kfree); max begrenzt die Größe. */
bool storage_load(const char *folder, const char *name, size_t max, char **data,
                  size_t *size);

/*
 * Alles, was direkt auf Datenträger zugreift (auch Update und Schlüsseldatei),
 * hält währenddessen diese Sperre.
 */
void storage_lock(void);
void storage_unlock(void);
