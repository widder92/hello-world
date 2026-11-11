/* Serielle Schnittstelle COM1: Kernel-Protokoll und Konsole ohne Bildschirm */
#pragma once

#include "console.h"

#include <stdbool.h>

void serial_init(void);
void serial_putc(char c);
void serial_write(const char *text);

/* Empfangene Zeichen abholen - wird vom Systemtakt aufgerufen. */
void serial_poll(void);

/* Die serielle Schnittstelle als Konsole (für eine Shell ohne Bildschirm). */
console_t *serial_console(void);
bool serial_present(void);
