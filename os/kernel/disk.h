/*
 * Fest eingebaute Datenträger: NVMe-SSDs und SATA-Platten (AHCI).
 *
 * Beide Treiber melden ihre Platten als schreibgeschützte Blockgeräte an
 * (siehe block.h); beschrieben wird später ausschließlich die eigene
 * Partition von HelloOS. Gearbeitet wird ohne Interrupts: Ein Auftrag wird
 * abgeschickt und auf sein Ende gewartet.
 */
#pragma once

void nvme_init(void);
void ahci_init(void);

/* Vor dem Ausschalten: Zwischenspeicher der Platten leeren, Laufwerke abmelden. */
void disk_shutdown(void);
