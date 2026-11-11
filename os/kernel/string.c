#include "string.h"

/*
 * Die Kopierfunktionen nutzen die String-Befehle des Prozessors (rep movsb /
 * rep stosb). Auf aktuellen CPUs sind die sehr schnell - wichtig, weil die
 * Oberfläche ständig große Pixelblöcke bewegt.
 */

void *memset(void *dst, int value, size_t n)
{
    void *d = dst;
    __asm__ volatile("rep stosb"
                     : "+D"(d), "+c"(n) : "a"(value) : "memory");
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    void *d = dst;
    __asm__ volatile("rep movsb"
                     : "+D"(d), "+S"(src), "+c"(n) : : "memory");
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    if ((uintptr_t)dst <= (uintptr_t)src ||
        (uintptr_t)dst >= (uintptr_t)src + n)
        return memcpy(dst, src, n);

    /* Überlappend und Ziel hinter der Quelle: von hinten nach vorn kopieren */
    void *d = (uint8_t *)dst + n - 1;
    const void *s = (const uint8_t *)src + n - 1;
    __asm__ volatile("std; rep movsb; cld"
                     : "+D"(d), "+S"(s), "+c"(n) : : "memory");
    return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *x = a, *y = b;
    for (; n--; x++, y++) {
        if (*x != *y)
            return *x - *y;
    }
    return 0;
}

size_t strlen(const char *s)
{
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (uint8_t)*a - (uint8_t)*b;
}

void strlcpy(char *dst, const char *src, size_t size)
{
    if (size == 0)
        return;
    while (--size && *src)
        *dst++ = *src++;
    *dst = '\0';
}

bool parse_uint(const char *s, uint32_t *out)
{
    uint32_t value = 0;
    if (!*s)
        return false;
    for (; *s; s++) {
        if (*s < '0' || *s > '9')
            return false;
        uint32_t digit = (uint32_t)(*s - '0');
        if (value > (UINT32_MAX - digit) / 10)
            return false;
        value = value * 10 + digit;
    }
    *out = value;
    return true;
}
