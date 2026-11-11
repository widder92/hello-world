#include "serial.h"
#include "idt.h"
#include "input.h"
#include "io.h"

#include <stdbool.h>

#define COM1 0x3F8

static bool present;

/* Zustand für die Dekodierung eingehender Bytes */
static int utf_pending;
static uint32_t utf_codepoint;
static int esc_state;                   /* 0: normal, 1: ESC, 2: ESC [ */

void serial_init(void)
{
    outb(COM1 + 1, 0x00);               /* Interrupts aus */
    outb(COM1 + 3, 0x80);               /* DLAB: Baudrate setzen */
    outb(COM1 + 0, 0x01);               /* Teiler 1 -> 115200 Baud */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);               /* 8 Datenbits, keine Parität, 1 Stopp */
    outb(COM1 + 2, 0xC7);               /* FIFO an und leeren */

    /* Loopback-Test: Gibt es die Schnittstelle überhaupt? */
    outb(COM1 + 4, 0x1E);
    outb(COM1 + 0, 0xAE);
    present = inb(COM1 + 0) == 0xAE;

    outb(COM1 + 4, 0x0B);               /* DTR, RTS, OUT2 (für den IRQ nötig) */
}

void serial_putc(char c)
{
    if (!present)
        return;
    if (c == '\n')
        serial_putc('\r');
    for (int i = 0; i < 100000; i++) {
        if (inb(COM1 + 5) & 0x20)       /* Sendepuffer frei? */
            break;
    }
    outb(COM1, (uint8_t)c);
}

/* Übersetzt den Bytestrom eines Terminals (UTF-8, ANSI-Pfeiltasten). */
static void decode(uint8_t b)
{
    if (utf_pending > 0) {
        if ((b & 0xC0) == 0x80) {
            utf_codepoint = (utf_codepoint << 6) | (b & 0x3F);
            if (--utf_pending == 0)
                input_push((int)utf_codepoint);
            return;
        }
        utf_pending = 0;
    }

    if (esc_state == 1) {
        esc_state = (b == '[') ? 2 : 0;
        if (esc_state)
            return;
    } else if (esc_state == 2) {
        esc_state = 0;
        if (b == 'A')
            input_push(KEY_UP);
        else if (b == 'B')
            input_push(KEY_DOWN);
        return;
    }

    if (b == 0x1B) {
        esc_state = 1;
    } else if (b >= 0xC0 && b < 0xE0) {
        utf_pending = 1;
        utf_codepoint = b & 0x1F;
    } else if (b == '\r') {
        input_push('\n');
    } else if (b == 0x7F) {
        input_push('\b');
    } else if (b < 0x80) {
        input_push(b);
    }
}

static void serial_irq(regs_t *regs)
{
    (void)regs;
    while (inb(COM1 + 5) & 0x01)
        decode(inb(COM1));
}

void serial_enable_input(void)
{
    if (!present)
        return;
    irq_register(4, serial_irq);
    outb(COM1 + 1, 0x01);               /* Interrupt bei empfangenen Daten */
}
