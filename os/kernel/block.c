#include "block.h"
#include "cpu.h"
#include "string.h"

static block_device_t devices[BLOCK_DEVICES_MAX];

block_device_t *block_register(const char *name, uint64_t sectors)
{
    uint64_t flags = irq_save();
    block_device_t *dev = NULL;
    for (int i = 0; i < BLOCK_DEVICES_MAX && !dev; i++) {
        if (!devices[i].present)
            dev = &devices[i];
    }
    if (dev) {
        memset(dev, 0, sizeof(*dev));
        strlcpy(dev->name, name, sizeof(dev->name));
        dev->sectors = sectors;
        dev->present = true;
    }
    irq_restore(flags);
    return dev;
}

void block_unregister(block_device_t *dev)
{
    if (dev)
        dev->present = false;
}

block_device_t *block_get(int index)
{
    if (index < 0 || index >= BLOCK_DEVICES_MAX || !devices[index].present)
        return NULL;
    return &devices[index];
}

bool block_read(block_device_t *dev, uint64_t lba, uint32_t count, void *buffer)
{
    if (!dev || !dev->present || lba + count > dev->sectors)
        return false;
    return dev->read(dev, lba, count, buffer);
}

bool block_write(block_device_t *dev, uint64_t lba, uint32_t count,
                 const void *buffer)
{
    if (!dev || !dev->present || lba + count > dev->sectors)
        return false;
    return dev->write(dev, lba, count, buffer);
}
