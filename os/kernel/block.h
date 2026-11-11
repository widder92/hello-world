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
    void *driver_data;
} block_device_t;

#define BLOCK_DEVICES_MAX 4

/* Von Treibern aufgerufen, wenn ein Datenträger auftaucht oder verschwindet. */
block_device_t *block_register(const char *name, uint64_t sectors);
void block_unregister(block_device_t *dev);

/* Für Nutzer: Gerät Nummer index (0 .. BLOCK_DEVICES_MAX-1) oder NULL. */
block_device_t *block_get(int index);

bool block_read(block_device_t *dev, uint64_t lba, uint32_t count, void *buffer);
bool block_write(block_device_t *dev, uint64_t lba, uint32_t count,
                 const void *buffer);
