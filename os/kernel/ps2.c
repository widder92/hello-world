/*
 * PS/2-Controller (i8042): Tastatur und Maus.
 *
 * Statt Interrupts zu verwenden, fragt der Systemtakt den Controller tausendmal
 * pro Sekunde ab. Das ist unabhängig davon, wie ein Rechner seine Interrupts
 * verdrahtet hat. Rechner ganz ohne PS/2 werden erkannt und übersprungen.
 */
#include "input.h"
#include "console.h"
#include "cpu.h"
#include "gui.h"
#include "time.h"

#define DATA_PORT    0x60
#define STATUS_PORT  0x64
#define COMMAND_PORT 0x64

#define STATUS_OUTPUT_FULL 0x01
#define STATUS_INPUT_FULL  0x02
#define STATUS_FROM_MOUSE  0x20

static bool present;
static bool extended;                   /* Tastatur: Präfix 0xE0 gesehen */
static uint8_t packet[3];               /* Maus: Bytes des aktuellen Pakets */
static int packet_len;

static bool wait_input_clear(void)
{
    for (int i = 0; i < 20000; i++) {
        if (!(inb(STATUS_PORT) & STATUS_INPUT_FULL))
            return true;
        time_delay_us(10);
    }
    return false;
}

static int read_data(void)
{
    for (int i = 0; i < 20000; i++) {
        if (inb(STATUS_PORT) & STATUS_OUTPUT_FULL)
            return inb(DATA_PORT);
        time_delay_us(10);
    }
    return -1;
}

static void flush(void)
{
    for (int i = 0; i < 64 && (inb(STATUS_PORT) & STATUS_OUTPUT_FULL); i++)
        inb(DATA_PORT);
}

static void controller_command(uint8_t command)
{
    wait_input_clear();
    outb(COMMAND_PORT, command);
}

/* Schickt ein Kommando an die Maus und wartet auf deren Bestätigung. */
static bool mouse_command(uint8_t command)
{
    controller_command(0xD4);           /* nächstes Byte geht an die Maus */
    wait_input_clear();
    outb(DATA_PORT, command);
    for (int i = 0; i < 4; i++) {
        int reply = read_data();
        if (reply == 0xFA)
            return true;
        if (reply < 0)
            break;
    }
    return false;
}

void ps2_init(void)
{
    /* Ohne Controller liefert der Port 0xFF. */
    if (inb(STATUS_PORT) == 0xFF) {
        klog("PS/2: kein Controller vorhanden\n");
        return;
    }

    controller_command(0xAD);           /* Tastatur aus */
    controller_command(0xA7);           /* Maus aus */
    flush();

    /* Konfiguration: keine Interrupts, Scancodes in Set 1 übersetzen */
    controller_command(0x20);
    int config = read_data();
    if (config < 0) {
        klog("PS/2: Controller antwortet nicht\n");
        return;
    }
    config &= ~0x03;                    /* IRQ 1 und IRQ 12 aus */
    config &= ~0x30;                    /* Takt für beide Anschlüsse an */
    config |= 0x40;                     /* Übersetzung in Set 1 */
    controller_command(0x60);
    wait_input_clear();
    outb(DATA_PORT, (uint8_t)config);

    controller_command(0xAE);           /* Tastatur an */
    controller_command(0xA8);           /* Maus an */

    mouse_command(0xF6);                /* Standardwerte */
    bool mouse = mouse_command(0xF4);   /* Bewegungsdaten senden */
    flush();

    present = true;
    klog("PS/2: Tastatur bereit, Maus %s\n", mouse ? "bereit" : "nicht gefunden");
}

bool ps2_present(void)
{
    return present;
}

static void mouse_byte(uint8_t data)
{
    /* Im ersten Byte ist Bit 3 immer gesetzt - daran richten wir uns aus. */
    if (packet_len == 0 && !(data & 0x08))
        return;
    packet[packet_len++] = data;
    if (packet_len < 3)
        return;
    packet_len = 0;

    if (packet[0] & 0xC0)
        return;                         /* Überlauf: Paket verwerfen */
    int dx = packet[1] - ((packet[0] & 0x10) ? 256 : 0);
    int dy = packet[2] - ((packet[0] & 0x20) ? 256 : 0);
    gui_post_mouse(dx, -dy, packet[0] & 0x07);
}

static void keyboard_byte(uint8_t data)
{
    if (data == 0xE0) {
        extended = true;
        return;
    }
    if (data == 0xE1 || data == 0xFA || data == 0xAA)
        return;                         /* Pause-Präfix, Bestätigung, Selbsttest */
    keyboard_handle_scancode(data & 0x7F, extended, data & 0x80);
    extended = false;
}

void ps2_poll(void)
{
    if (!present)
        return;
    for (int i = 0; i < 16; i++) {
        uint8_t status = inb(STATUS_PORT);
        if (!(status & STATUS_OUTPUT_FULL))
            break;
        uint8_t data = inb(DATA_PORT);
        if (status & STATUS_FROM_MOUSE)
            mouse_byte(data);
        else
            keyboard_byte(data);
    }
}
