#include "pci.h"
#include "console.h"
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

/* Sucht die Brücke, hinter der der Bus mit dieser Nummer liegt. */
static bool find_bridge(uint8_t secondary, pci_device_t *out)
{
    for (int bus = 0; bus < 256; bus++) {
        for (int device = 0; device < 32; device++) {
            pci_device_t dev = { (uint8_t)bus, (uint8_t)device, 0, 0, 0 };
            if (pci_read16(&dev, 0x00) == 0xFFFF)
                continue;
            int functions = (pci_read32(&dev, 0x0C) & 0x00800000) ? 8 : 1;
            for (int function = 0; function < functions; function++) {
                dev.function = (uint8_t)function;
                uint32_t id = pci_read32(&dev, 0x00);
                if ((id & 0xFFFF) == 0xFFFF)
                    continue;
                if (((pci_read32(&dev, 0x0C) >> 16) & 0x7F) != 1)
                    continue;           /* keine Brücke */
                if (((pci_read32(&dev, 0x18) >> 8) & 0xFF) != secondary)
                    continue;
                dev.vendor_id = id & 0xFFFF;
                dev.device_id = id >> 16;
                *out = dev;
                return true;
            }
        }
    }
    return false;
}

bool pci_parent_bridge(const pci_device_t *dev, pci_device_t *out)
{
    return dev->bus != 0 && find_bridge(dev->bus, out);
}

void pci_enable(const pci_device_t *dev)
{
    /* Speicherzugriff + Busmaster, kein Interrupt-Pin */
    pci_write16(dev, 0x04, (uint16_t)(pci_read16(dev, 0x04) | 0x0006 | 0x0400));

    /*
     * Dasselbe für jede Brücke auf dem Weg zum Prozessor: Ist dort der
     * Busmaster aus, erreichen uns die Register des Geräts, aber es selbst
     * kommt nicht an den Speicher - es kann dann weder senden noch empfangen.
     * Manche Firmware schaltet Brücken nur für Geräte ein, die sie selbst
     * zum Starten braucht.
     */
    pci_device_t at = *dev, bridge;
    for (int guard = 0; guard < 8 && pci_parent_bridge(&at, &bridge); guard++) {
        uint16_t command = pci_read16(&bridge, 0x04);
        if ((command & 0x0006) != 0x0006) {
            pci_write16(&bridge, 0x04, (uint16_t)(command | 0x0006));
            klog("PCI: Brücke %02x:%02x.%u vor Gerät %02x:%02x.%u eingeschaltet "
                 "(Befehlsregister war %04x)\n", bridge.bus, bridge.device,
                 bridge.function, dev->bus, dev->device, dev->function, command);
        }
        at = bridge;
    }
}
