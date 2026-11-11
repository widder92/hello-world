/*
 * Konsolen: Jeder Task schreibt auf "seine" Konsole - ein Terminalfenster
 * oder die serielle Schnittstelle. Neue Tasks erben die Konsole des Erzeugers.
 */
#pragma once

#include <stdarg.h>
#include <stddef.h>

typedef enum {
    COLOR_DEFAULT,
    COLOR_GREEN,
    COLOR_CYAN,
    COLOR_RED,
    COLOR_YELLOW,
    COLOR_WHITE,
    COLOR_COUNT,
} console_color_t;

typedef struct console console_t;

struct console {
    void (*putc)(console_t *self, char c);          /* Byte eines UTF-8-Stroms */
    void (*set_color)(console_t *self, console_color_t color);
    void (*clear)(console_t *self);
    int (*getchar)(console_t *self);    /* blockiert; -1: Konsole geschlossen */
    void (*release)(console_t *self);   /* letzte Referenz ist weg */
    int refs;
};

void console_ref(console_t *con);
void console_unref(console_t *con);

/* Die folgenden Funktionen arbeiten auf der Konsole des laufenden Tasks. */
void console_putc(char c);
void console_write(const char *data, size_t len);
void console_set_color(console_color_t color);
void console_clear(void);
int console_getchar(void);

/*
 * Formatierte Ausgabe: %s %c %d %u %x %X %p %% mit Breite, '0' und '-'.
 * Für 64-Bit-Werte: %llu, %llx, %zu.
 */
void kprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int ksnprintf(char *buf, size_t size, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
int kvsnprintf(char *buf, size_t size, const char *fmt, va_list ap);

/*
 * Kernel-Meldungen: gehen immer an die serielle Schnittstelle und - solange
 * die Oberfläche noch nicht läuft - als Startprotokoll auf den Bildschirm.
 */
void klog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void klog_set_screen(void (*write)(const char *text));
