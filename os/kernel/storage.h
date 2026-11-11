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

/* Liest eine Datei aus dem Hauptverzeichnis des HelloOS-Datenträgers (kfree). */
bool storage_load_root(const char *name, size_t max, char **data, size_t *size);

/*
 * Alles, was direkt auf Datenträger zugreift (auch das Update), hält
 * währenddessen diese Sperre.
 */
void storage_lock(void);
void storage_unlock(void);

/*
 * Hängt das Dateisystem ein, auf dem HelloOS zu Hause ist (nur mit gehaltener
 * Sperre aufrufen; danach fat_unmount()). Das ist der USB-Stick oder die
 * eigene Partition auf der internen Platte - erkannt an der HelloOS-Startdatei,
 * auf internen Platten zusätzlich am Namen der Partition. Nur dieser Bereich
 * wird je beschrieben.
 */
struct fat;
bool storage_mount_home(struct fat *fs);

/* Kennung, die in jeder HelloOS-Startdatei steht. */
#define HELLOOS_IMAGE_MARKER "HelloOS-Startdatei-Kennung-7f3a"
/* So muss die Partition auf einer internen Platte heißen. */
#define HELLOOS_VOLUME_LABEL "HELLOOS"

/* Für "disk": Wo HelloOS zu Hause ist (Name des Datenträgers) oder NULL. */
const char *storage_home_name(bool *internal);
