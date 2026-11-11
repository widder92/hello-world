#include "block.h"
#include "console.h"
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
        dev->write_first = 0;
        dev->write_count = sectors;
        dev->present = true;
    }
    irq_restore(flags);
    return dev;
}

block_device_t *block_register_internal(const char *name, uint64_t sectors)
{
    block_device_t *dev = block_register(name, sectors);
    if (dev) {
        dev->internal = true;
        dev->write_count = 0;
    }
    return dev;
}

void block_allow_writes(block_device_t *dev, uint64_t first, uint64_t count)
{
    uint64_t flags = irq_save();
    if (first > dev->sectors || count > dev->sectors - first)
        count = 0;                      /* unsinnige Angabe: lieber gar nichts */
    dev->write_first = first;
    dev->write_count = count;
    irq_restore(flags);
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
    if (!dev || !dev->present || count == 0 || lba >= dev->sectors ||
        count > dev->sectors - lba)
        return false;
    return dev->read(dev, lba, count, buffer);
}

bool block_write(block_device_t *dev, uint64_t lba, uint32_t count,
                 const void *buffer)
{
    if (!dev || !dev->present)
        return false;
    /* Die letzte Schranke vor dem Datenträger: nur im freigegebenen Bereich. */
    if (!block_write_allowed(dev, lba, count)) {
        if (dev->refused++ == 0)
            klog("%s: Schreibzugriff außerhalb des erlaubten Bereichs abgewiesen "
                 "(Sektor %llu)\n", dev->name, lba);
        return false;
    }
    return dev->write(dev, lba, count, buffer);
}

bool block_flush(block_device_t *dev)
{
    if (!dev || !dev->present)
        return false;
    return dev->flush ? dev->flush(dev) : true;
}
