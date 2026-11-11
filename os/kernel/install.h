/* Start-Assistent: HelloOS auf einer eigenen Partition der internen Platte einrichten */
#pragma once

#include "efi.h"

/*
 * Läuft vor der Übergabe an den Kernel, solange die Firmware noch arbeitet:
 * merkt sich, von welcher Partition gestartet wurde, und bietet - falls es
 * eine noch leere Partition namens HELLOOS gibt - das Einrichten dort an.
 */
void install_assistant(efi_handle_t image, efi_system_table_t *st);
