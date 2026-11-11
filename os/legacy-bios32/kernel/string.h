/* Minimale String- und Speicherfunktionen (es gibt keine libc im Kernel) */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

void *memset(void *dst, int value, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
int memcmp(const void *a, const void *b, size_t n);

size_t strlen(const char *s);
int strcmp(const char *a, const char *b);

/* Kopiert höchstens size-1 Zeichen und terminiert immer mit '\0'. */
void strlcpy(char *dst, const char *src, size_t size);

/* Liest eine nichtnegative Dezimalzahl; false bei ungültiger Eingabe. */
bool parse_uint(const char *s, uint32_t *out);
