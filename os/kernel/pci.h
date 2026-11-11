/* PCI: Geräte am Systembus finden und ihre Konfigurationsregister lesen */
#pragma once

#include <stdbool.h>
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

/*
 * Schaltet ein Gerät ein (Speicherzugriff und Busmaster) - und ebenso alle
 * Brücken zwischen ihm und dem Prozessor, ohne die es nicht an den Speicher
 * käme. Treiber rufen das auf, bevor sie ihr Gerät benutzen.
 */
void pci_enable(const pci_device_t *dev);

/* Liefert die Brücke direkt vor dem Gerät; false, wenn es an Bus 0 hängt. */
bool pci_parent_bridge(const pci_device_t *dev, pci_device_t *out);
