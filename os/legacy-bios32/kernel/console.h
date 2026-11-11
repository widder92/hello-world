/* Konsole: Ausgabe auf Bildschirm und serieller Schnittstelle zugleich */
#pragma once

#include <stdarg.h>
#include <stddef.h>

void console_putc(char c);
void console_write(const char *data, size_t len);

/* Unterstützt %s %c %d %u %x %X %p %% sowie Breite, '0' und '-'. */
void kprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int ksnprintf(char *buf, size_t size, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
