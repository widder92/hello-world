/* PCI: Geräte am Systembus finden und ihre Konfigurationsregister lesen */
#pragma once

#include <stdint.h>

typedef struct {
    uint8_t bus, device, function;
    uint16_t vendor_id, device_id;
} pci_device_t;

uint32_t pci_read32(const pci_device_t *dev, uint8_t offset);
uint16_t pci_read16(const pci_device_t *dev, uint8_t offset);
void pci_write32(const pci_device_t *dev, uint8_t offset, uint32_t value);
void pci_write16(const pci_device_t *dev, uint8_t offset, uint16_t value);

/*
 * Sucht Geräte einer bestimmten Art (Klasse, Unterklasse, Schnittstelle).
 * Liefert die Anzahl der gefundenen Geräte (höchstens max).
 */
int pci_find(uint8_t class_code, uint8_t subclass, uint8_t interface,
             pci_device_t *out, int max);
