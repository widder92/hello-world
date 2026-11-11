#include "vga.h"
#include "io.h"
#include "string.h"

#define VRAM     ((uint16_t *)0xB8000)
#define TERM_TOP 1                      /* erste Zeile des Terminals */

static int cur_x;
static int cur_y = TERM_TOP;
static uint8_t attr = 0x07;             /* hellgrau auf schwarz */

/* Zustand des UTF-8-Decoders */
static int utf_pending;                 /* noch erwartete Folgebytes */
static uint32_t utf_codepoint;

static inline uint16_t cell(uint8_t glyph)
{
    return (uint16_t)(glyph | (attr << 8));
}

static void update_cursor(void)
{
    uint16_t pos = (uint16_t)(cur_y * VGA_WIDTH + cur_x);
    outb(0x3D4, 0x0F);
    outb(0x3D5, pos & 0xFF);
    outb(0x3D4, 0x0E);
    outb(0x3D5, pos >> 8);
}

static void clear_row(int y)
{
    for (int x = 0; x < VGA_WIDTH; x++)
        VRAM[y * VGA_WIDTH + x] = cell(' ');
}

static void newline(void)
{
    cur_x = 0;
    if (++cur_y < VGA_HEIGHT)
        return;
    /* Terminalbereich um eine Zeile nach oben schieben */
    memmove(VRAM + TERM_TOP * VGA_WIDTH, VRAM + (TERM_TOP + 1) * VGA_WIDTH,
            (VGA_HEIGHT - TERM_TOP - 1) * VGA_WIDTH * sizeof(uint16_t));
    cur_y = VGA_HEIGHT - 1;
    clear_row(cur_y);
}

static void put_glyph(uint8_t glyph)
{
    VRAM[cur_y * VGA_WIDTH + cur_x] = cell(glyph);
    if (++cur_x >= VGA_WIDTH)
        newline();
}

/* Der VGA-Zeichensatz ist Codepage 437 - die wichtigsten Zeichen übersetzen. */
static uint8_t unicode_to_cp437(uint32_t cp)
{
    switch (cp) {
    case 0xE4: return 0x84;             /* ä */
    case 0xF6: return 0x94;             /* ö */
    case 0xFC: return 0x81;             /* ü */
    case 0xC4: return 0x8E;             /* Ä */
    case 0xD6: return 0x99;             /* Ö */
    case 0xDC: return 0x9A;             /* Ü */
    case 0xDF: return 0xE1;             /* ß */
    case 0xA7: return 0x15;             /* § */
    case 0xB0: return 0xF8;             /* ° */
    case 0xB2: return 0xFD;             /* ² */
    case 0xB5: return 0xE6;             /* µ */
    case 0xB7: return 0xFA;             /* · */
    case 0xE9: return 0x82;             /* é */
    default:   return '?';
    }
}

void vga_putc(char c)
{
    uint8_t b = (uint8_t)c;

    if (utf_pending > 0) {
        if ((b & 0xC0) == 0x80) {
            utf_codepoint = (utf_codepoint << 6) | (b & 0x3F);
            if (--utf_pending == 0)
                put_glyph(unicode_to_cp437(utf_codepoint));
            update_cursor();
            return;
        }
        utf_pending = 0;                /* kaputte Sequenz: neu ansetzen */
    }

    if (b >= 0xF0) {
        utf_pending = 3;
        utf_codepoint = b & 0x07;
    } else if (b >= 0xE0) {
        utf_pending = 2;
        utf_codepoint = b & 0x0F;
    } else if (b >= 0xC0) {
        utf_pending = 1;
        utf_codepoint = b & 0x1F;
    } else if (b >= 0x80) {
        put_glyph('?');
    } else {
        switch (b) {
        case '\n':
            newline();
            break;
        case '\r':
            cur_x = 0;
            break;
        case '\t':
            do {
                put_glyph(' ');
            } while (cur_x % 8 != 0);
            break;
        case '\b':
            if (cur_x > 0) {
                cur_x--;
            } else if (cur_y > TERM_TOP) {
                cur_y--;
                cur_x = VGA_WIDTH - 1;
            }
            VRAM[cur_y * VGA_WIDTH + cur_x] = cell(' ');
            break;
        default:
            put_glyph(b);
            break;
        }
    }
    update_cursor();
}

void vga_set_color(enum vga_color fg, enum vga_color bg)
{
    attr = (uint8_t)(fg | (bg << 4));
}

void vga_reset_color(void)
{
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

void vga_clear(void)
{
    for (int y = TERM_TOP; y < VGA_HEIGHT; y++)
        clear_row(y);
    cur_x = 0;
    cur_y = TERM_TOP;
    update_cursor();
}

void vga_write_status(const char *text, enum vga_color fg, enum vga_color bg)
{
    uint16_t color = (uint16_t)((fg | (bg << 4)) << 8);
    for (int x = 0; x < VGA_WIDTH; x++) {
        uint8_t ch = ' ';
        if (*text)
            ch = (uint8_t)*text++;
        VRAM[x] = color | ch;
    }
}

void vga_init(void)
{
    /* Cursor als Unterstrich einschalten */
    outb(0x3D4, 0x0A);
    outb(0x3D5, 13);
    outb(0x3D4, 0x0B);
    outb(0x3D5, 14);

    vga_reset_color();
    vga_write_status("", VGA_WHITE, VGA_BLUE);
    vga_clear();
}
