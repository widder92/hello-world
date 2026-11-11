/* Blockgeräte: Datenträger, die sich in 512-Byte-Sektoren lesen und schreiben lassen */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define SECTOR_SIZE 512

typedef struct block_device {
    bool present;
    char name[24];
    uint64_t sectors;
    bool (*read)(struct block_device *self, uint64_t lba, uint32_t count,
                 void *buffer);
    bool (*write)(struct block_device *self, uint64_t lba, uint32_t count,
                  const void *buffer);
    /* Sorgt dafür, dass Geschriebenes wirklich auf dem Datenträger liegt (oder NULL). */
    bool (*flush)(struct block_device *self);
    void *driver_data;

    /*
     * Fest eingebaute Platten können ein anderes Betriebssystem enthalten.
     * Sie sind deshalb zunächst komplett schreibgeschützt; beschrieben werden
     * darf nur der Bereich, der mit block_allow_writes() freigegeben wurde.
     */
    bool internal;
    uint64_t write_first, write_count;
    uint32_t refused;                   /* abgewiesene Schreibversuche */
} block_device_t;

#define BLOCK_DEVICES_MAX 4

/* Von Treibern aufgerufen, wenn ein Datenträger auftaucht oder verschwindet. */
block_device_t *block_register(const char *name, uint64_t sectors);
/* Wie block_register(), aber für fest eingebaute Platten: ohne Schreiberlaubnis. */
block_device_t *block_register_internal(const char *name, uint64_t sectors);
/* Gibt genau diesen Bereich zum Schreiben frei (count 0: nichts). */
void block_allow_writes(block_device_t *dev, uint64_t first, uint64_t count);
void block_unregister(block_device_t *dev);

/* Für Nutzer: Gerät Nummer index (0 .. BLOCK_DEVICES_MAX-1) oder NULL. */
block_device_t *block_get(int index);

bool block_read(block_device_t *dev, uint64_t lba, uint32_t count, void *buffer);
bool block_write(block_device_t *dev, uint64_t lba, uint32_t count,
                 const void *buffer);

/*
 * Die Entscheidung, ob ein Schreibzugriff sein darf: ganz innerhalb des
 * Datenträgers und ganz innerhalb des freigegebenen Bereichs. So geschrieben,
 * dass keine der Rechnungen überlaufen kann.
 */
static inline bool block_write_allowed(const block_device_t *dev, uint64_t lba,
                                       uint32_t count)
{
    if (count == 0 || lba >= dev->sectors || count > dev->sectors - lba)
        return false;
    if (lba < dev->write_first)
        return false;
    uint64_t offset = lba - dev->write_first;
    return offset < dev->write_count && count <= dev->write_count - offset;
}
bool block_flush(block_device_t *dev);
