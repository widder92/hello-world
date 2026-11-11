#include "pci.h"
#include "cpu.h"

/* Klassischer Zugriff über zwei Ports: Adresse wählen, dann Daten lesen. */
#define CONFIG_ADDRESS 0xCF8
#define CONFIG_DATA    0xCFC

static uint32_t address(const pci_device_t *dev, uint8_t offset)
{
    return 0x80000000u | ((uint32_t)dev->bus << 16) |
           ((uint32_t)dev->device << 11) | ((uint32_t)dev->function << 8) |
           (offset & 0xFC);
}

uint32_t pci_read32(const pci_device_t *dev, uint8_t offset)
{
    uint64_t flags = irq_save();
    outl(CONFIG_ADDRESS, address(dev, offset));
    uint32_t value = inl(CONFIG_DATA);
    irq_restore(flags);
    return value;
}

uint16_t pci_read16(const pci_device_t *dev, uint8_t offset)
{
    return (uint16_t)(pci_read32(dev, offset) >> ((offset & 2) * 8));
}

void pci_write32(const pci_device_t *dev, uint8_t offset, uint32_t value)
{
    uint64_t flags = irq_save();
    outl(CONFIG_ADDRESS, address(dev, offset));
    outl(CONFIG_DATA, value);
    irq_restore(flags);
}

void pci_write16(const pci_device_t *dev, uint8_t offset, uint16_t value)
{
    int shift = (offset & 2) * 8;
    uint32_t old = pci_read32(dev, offset);
    pci_write32(dev, offset,
                (old & ~(0xFFFFu << shift)) | ((uint32_t)value << shift));
}

int pci_find(uint8_t class_code, uint8_t subclass, uint8_t interface,
             pci_device_t *out, int max)
{
    int count = 0;
    uint32_t wanted = ((uint32_t)class_code << 16) | ((uint32_t)subclass << 8) |
                      interface;

    for (int bus = 0; bus < 256; bus++) {
        for (int device = 0; device < 32; device++) {
            pci_device_t dev = { (uint8_t)bus, (uint8_t)device, 0, 0, 0 };
            if (pci_read16(&dev, 0x00) == 0xFFFF)
                continue;               /* Steckplatz leer */

            /* Bit 7 des Kopftyps: Gerät hat mehrere Funktionen */
            int functions = (pci_read32(&dev, 0x0C) & 0x00800000) ? 8 : 1;
            for (int function = 0; function < functions; function++) {
                dev.function = (uint8_t)function;
                uint32_t id = pci_read32(&dev, 0x00);
                if ((id & 0xFFFF) == 0xFFFF)
                    continue;
                if (pci_read32(&dev, 0x08) >> 8 != wanted)
                    continue;
                dev.vendor_id = id & 0xFFFF;
                dev.device_id = id >> 16;
                if (count < max)
                    out[count++] = dev;
            }
        }
    }
    return count;
}
