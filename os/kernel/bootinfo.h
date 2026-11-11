/* Was der Startcode (boot.c) von der Firmware einsammelt und dem Kernel übergibt */
#pragma once

#include "efi.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    /* Bildschirm */
    uint64_t fb_base;                   /* 0: keine Grafik gefunden */
    uint32_t fb_width, fb_height;
    uint32_t fb_stride;                 /* Pixel pro Zeile im Speicher */
    bool fb_bgr;                        /* Bytefolge B, G, R (sonst R, G, B) */

    /* Speicherkarte der Firmware zum Zeitpunkt der Übergabe */
    efi_memory_descriptor_t *mmap;
    uint64_t mmap_size;
    uint64_t mmap_desc_size;

    /* Seitentabellen: bereits aktiv, bilden den RAM 1:1 ab */
    uint64_t *pml4;

    uint64_t tsc_hz;                    /* Frequenz des Zeitstempelzählers */
    efi_time_t boot_time;               /* Uhrzeit beim Start */
    uint64_t rsdp;                      /* ACPI-Wurzeltabelle (oder 0) */
    efi_runtime_services_t *runtime;
    char firmware_vendor[32];

    /* Von welcher Partition wurde gestartet? (Anfangssektor; nur gültig mit boot_known) */
    bool boot_known;
    uint64_t boot_partition_start;
    bool boot_internal;                 /* von einer fest eingebauten Platte (nicht USB) */
    /* Was der Start-Assistent getan hat - als Meldung für den Benutzer (oder leer) */
    char install_note[96];
} boot_info_t;

extern boot_info_t boot_info;

/* Einstieg in den eigentlichen Kernel (main.c). */
void kmain(void *unused);
