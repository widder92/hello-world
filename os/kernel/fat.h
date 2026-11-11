/*
 * FAT32: das Dateisystem der EFI-Systempartition, von der HelloOS startet.
 * Für das Update: Dateien mit kurzen Namen (8.3) finden, lesen und ihren
 * Inhalt austauschen. Für eigene Dateien: Verzeichnisse auflisten, Dateien
 * mit langen Namen anlegen und löschen.
 */
#pragma once

#include "block.h"

#include <stddef.h>

typedef struct {
    block_device_t *dev;
    uint64_t start;                     /* erster Sektor der Partition */
    uint32_t sectors_per_cluster;
    uint32_t fat_start;                 /* relativ zur Partition */
    uint32_t fat_sectors;
    uint32_t fat_count;
    uint32_t data_start;
    uint32_t root_cluster;
    uint32_t cluster_count;
    uint32_t info_sector;

    /* Zwischenspeicher für einen Abschnitt der Zuordnungstabelle */
    uint8_t *cache;
    uint32_t cache_sector;              /* relativ zum Anfang der FAT */
    uint32_t cache_sectors;
    bool cache_dirty;
} fat_t;

typedef struct {
    uint32_t first_cluster;
    uint32_t size;
    bool directory;
    uint64_t entry_sector;              /* wo der Verzeichniseintrag steht */
    uint32_t entry_offset;
    /* Nur von fat_list()/fat_lookup()/fat_create() gefüllt: */
    uint32_t dir_cluster;               /* Verzeichnis, in dem der Eintrag steht */
    uint32_t dir_offset;                /* Position des Eintrags darin (Bytes) */
    uint32_t name_entries;              /* davor liegende Einträge des langen Namens */
} fat_file_t;

#define FAT_NAME_MAX 256                /* UTF-8, mit Nullbyte */

/* Sucht auf dem Datenträger eine FAT32-Partition (GPT, MBR oder ohne Tabelle). */
bool fat_mount(fat_t *fs, block_device_t *dev);
void fat_unmount(fat_t *fs);

/* Pfad mit '/' getrennt, Groß/Klein egal: "EFI/BOOT/BOOTX64.EFI". */
bool fat_find(fat_t *fs, const char *path, fat_file_t *out);

/* Liest die ganze Datei; buffer muss file->size Bytes fassen. */
bool fat_read(fat_t *fs, const fat_file_t *file, void *buffer);

/*
 * Ersetzt den Inhalt einer Datei. Der neue Inhalt wird zuerst vollständig in
 * freie Bereiche geschrieben; erst dann wird - mit einem einzigen
 * Sektor-Schreibzugriff - der Verzeichniseintrag umgehängt. Fällt mittendrin
 * der Strom aus, ist die Datei also entweder noch die alte oder schon die neue.
 */
bool fat_replace(fat_t *fs, fat_file_t *file, const void *data, uint32_t size);

/* --- Eigene Dateien: lange Namen (UTF-8), Groß/Klein egal ---------------------------- */

/* Wird für jeden Eintrag aufgerufen; false beendet die Auflistung. */
typedef bool (*fat_visit_t)(void *ctx, const char *name, const fat_file_t *file);

/* Listet ein Verzeichnis auf (dir_cluster: first_cluster des Verzeichnisses). */
bool fat_list(fat_t *fs, uint32_t dir_cluster, fat_visit_t visit, void *ctx);
bool fat_lookup(fat_t *fs, uint32_t dir_cluster, const char *name, fat_file_t *out);
/* Der Vergleich, der dabei gilt (UTF-8, ohne Rücksicht auf Groß/Klein). */
bool fat_same_name(const char *a, const char *b);

/* Legt eine leere Datei oder ein leeres Verzeichnis an; der Name darf noch
 * nicht vorkommen. Den Inhalt einer Datei schreibt danach fat_replace(). */
bool fat_create(fat_t *fs, uint32_t dir_cluster, const char *name, bool directory,
                fat_file_t *out);

/* Löscht eine Datei (kein Verzeichnis), die fat_list()/fat_lookup() geliefert hat. */
bool fat_delete(fat_t *fs, const fat_file_t *file);
