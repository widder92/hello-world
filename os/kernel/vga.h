/* VGA-Textmodus (80x25). Zeile 0 ist die Statusleiste, darunter das Terminal. */
#pragma once

#include <stdint.h>

#define VGA_WIDTH  80
#define VGA_HEIGHT 25

enum vga_color {
    VGA_BLACK, VGA_BLUE, VGA_GREEN, VGA_CYAN, VGA_RED, VGA_MAGENTA, VGA_BROWN,
    VGA_LIGHT_GREY, VGA_DARK_GREY, VGA_LIGHT_BLUE, VGA_LIGHT_GREEN,
    VGA_LIGHT_CYAN, VGA_LIGHT_RED, VGA_LIGHT_MAGENTA, VGA_YELLOW, VGA_WHITE,
};

void vga_init(void);
void vga_clear(void);

/* Gibt ein Byte eines UTF-8-Stroms aus (Umlaute werden nach CP437 übersetzt). */
void vga_putc(char c);

void vga_set_color(enum vga_color fg, enum vga_color bg);
void vga_reset_color(void);

/* Schreibt die Statusleiste (nur ASCII), aufgefüllt auf die volle Breite. */
void vga_write_status(const char *text, enum vga_color fg, enum vga_color bg);
