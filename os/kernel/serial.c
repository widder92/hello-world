#include "serial.h"
#include "cpu.h"
#include "input.h"
#include "task.h"

#define COM1        0x3F8
#define BUFFER_SIZE 256

static bool present;

/* Eingabepuffer der seriellen Konsole */
static int buffer[BUFFER_SIZE];
static unsigned head, tail;             /* head: schreiben, tail: lesen */
static uint32_t waiting_pid;

/* Zustand für die Dekodierung eingehender Bytes */
static int utf_pending;
static uint32_t utf_codepoint;
static int esc_state;                   /* 0: normal, 1: ESC, 2: ESC [ */

void serial_init(void)
{
    outb(COM1 + 1, 0x00);               /* Interrupts aus - wir fragen ab */
    outb(COM1 + 3, 0x80);               /* DLAB: Baudrate setzen */
    outb(COM1 + 0, 0x01);               /* Teiler 1 -> 115200 Baud */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);               /* 8 Datenbits, keine Parität, 1 Stopp */
    outb(COM1 + 2, 0xC7);               /* FIFO an und leeren */

    /* Loopback-Test: Gibt es die Schnittstelle überhaupt? */
    outb(COM1 + 4, 0x1E);
    outb(COM1 + 0, 0xAE);
    present = inb(COM1 + 0) == 0xAE;

    outb(COM1 + 4, 0x03);               /* DTR, RTS */
}

bool serial_present(void)
{
    return present;
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

void serial_write(const char *text)
{
    while (*text)
        serial_putc(*text++);
}

static void push(int c)
{
    unsigned next = (head + 1) % BUFFER_SIZE;
    if (next != tail) {                 /* voller Puffer: Zeichen verwerfen */
        buffer[head] = c;
        head = next;
    }
    if (waiting_pid) {
        task_wake(waiting_pid);
        waiting_pid = 0;
    }
}

/* Übersetzt den Bytestrom eines Terminals (UTF-8, ANSI-Pfeiltasten). */
static void decode(uint8_t b)
{
    if (utf_pending > 0) {
        if ((b & 0xC0) == 0x80) {
            utf_codepoint = (utf_codepoint << 6) | (b & 0x3F);
            if (--utf_pending == 0)
                push((int)utf_codepoint);
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
            push(KEY_UP);
        else if (b == 'B')
            push(KEY_DOWN);
        return;
    }

    if (b == 0x1B) {
        esc_state = 1;
    } else if (b >= 0xC0 && b < 0xE0) {
        utf_pending = 1;
        utf_codepoint = b & 0x1F;
    } else if (b == '\r') {
        push('\n');
    } else if (b == 0x7F) {
        push('\b');
    } else if (b < 0x80) {
        push(b);
    }
}

void serial_poll(void)
{
    if (!present)
        return;
    for (int i = 0; i < 16 && (inb(COM1 + 5) & 0x01); i++)
        decode(inb(COM1));
}

/* --- Konsole ---------------------------------------------------------------- */

static void con_putc(console_t *self, char c)
{
    (void)self;
    uint64_t flags = irq_save();
    if (c == '\b') {
        serial_putc('\b');
        serial_putc(' ');
    }
    serial_putc(c);
    irq_restore(flags);
}

static int con_getchar(console_t *self)
{
    (void)self;
    for (;;) {
        uint64_t flags = irq_save();
        if (head != tail) {
            int c = buffer[tail];
            tail = (tail + 1) % BUFFER_SIZE;
            irq_restore(flags);
            return c;
        }
        /* Nichts da: schlafen, bis serial_poll() uns weckt. */
        waiting_pid = task_current_pid();
        task_block(0);
        irq_restore(flags);
    }
}

static console_t console = {
    .putc = con_putc,
    .getchar = con_getchar,
    .refs = 1,                          /* wird nie freigegeben */
};

console_t *serial_console(void)
{
    return &console;
}
