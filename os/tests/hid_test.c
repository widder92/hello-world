/*
 * Test für kernel/hid.c auf dem Entwicklungsrechner: Beschreibungen echter
 * Gerätearten auswerten und Berichte entschlüsseln, dazu zufällige Eingaben.
 *
 *   cc -fsanitize=address,undefined -o hid_test tests/hid_test.c kernel/hid.c
 */
#include "../kernel/hid.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures;

#define CHECK(condition) do { \
    checks++; \
    if (!(condition)) { \
        failures++; \
        printf("FEHLER Zeile %d: %s\n", __LINE__, #condition); \
    } \
} while (0)

/* Die einfachste Maus: 3 Tasten, X und Y mit je 8 Bit, keine Kennung */
static const uint8_t simple_mouse[] = {
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x01, 0xA1, 0x00,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01,
    0x95, 0x03, 0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x05,
    0x81, 0x01, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x15, 0x81,
    0x25, 0x7F, 0x75, 0x08, 0x95, 0x02, 0x81, 0x06, 0xC0, 0xC0,
};

/* Die einfachste Tastatur: 8 Umschaltbits, ein Füllbyte, 6 Tasten */
static const uint8_t simple_keyboard[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x05, 0x07, 0x19, 0xE0,
    0x29, 0xE7, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08,
    0x81, 0x02, 0x95, 0x01, 0x75, 0x08, 0x81, 0x01, 0x95, 0x05,
    0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02,
    0x95, 0x01, 0x75, 0x03, 0x91, 0x01, 0x95, 0x06, 0x75, 0x08,
    0x15, 0x00, 0x25, 0x65, 0x05, 0x07, 0x19, 0x00, 0x29, 0x65,
    0x81, 0x00, 0xC0,
};

/*
 * Wie die zweite Schnittstelle vieler Funkempfänger: unter Kennung 1 eine
 * Maus mit 5 Tasten, 12-Bit-Bewegung und Rad, unter Kennung 2 Sondertasten
 * (Lautstärke usw.), unter Kennung 3 Systemtasten (Ausschalten, Ruhe).
 */
static const uint8_t receiver[] = {
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x01, 0x09, 0x01, 0xA1, 0x00,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x05, 0x15, 0x00, 0x25, 0x01,
    0x95, 0x05, 0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x03, 0x81, 0x01,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x16, 0x01, 0xF8, 0x26, 0xFF, 0x07,
    0x75, 0x0C, 0x95, 0x02, 0x81, 0x06,
    0x09, 0x38, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x01, 0x81, 0x06,
    0xC0, 0xC0,
    0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x02, 0x15, 0x00, 0x26, 0xFF, 0x03,
    0x19, 0x00, 0x2A, 0xFF, 0x03, 0x75, 0x10, 0x95, 0x01, 0x81, 0x00, 0xC0,
    0x05, 0x01, 0x09, 0x80, 0xA1, 0x01, 0x85, 0x03, 0x19, 0x81, 0x29, 0x83,
    0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x03, 0x81, 0x02,
    0x95, 0x05, 0x81, 0x01, 0xC0,
};

/* Zeigegerät mit festen Positionen (so meldet sich QEMUs "usb-tablet") */
static const uint8_t tablet[] = {
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x01, 0xA1, 0x00,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01,
    0x95, 0x03, 0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x05, 0x81, 0x01,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x15, 0x00, 0x26, 0xFF, 0x7F,
    0x35, 0x00, 0x46, 0xFF, 0x7F, 0x75, 0x10, 0x95, 0x02, 0x81, 0x02,
    0x05, 0x01, 0x09, 0x38, 0x15, 0x81, 0x25, 0x7F, 0x35, 0x00, 0x45, 0x00,
    0x75, 0x08, 0x95, 0x01, 0x81, 0x06, 0xC0, 0xC0,
};

/* Tastatur, die jede Taste als eigenes Bit meldet, mit Kennung 4 */
static const uint8_t bitmap_keyboard[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x04, 0x05, 0x07,
    0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x19, 0x04, 0x29, 0x73, 0x75, 0x01, 0x95, 0x70, 0x81, 0x02, 0xC0,
};

static void test_simple_mouse(void)
{
    hid_layout_t layout;
    CHECK(hid_parse(simple_mouse, sizeof(simple_mouse), &layout));
    CHECK(layout.count == 1 && !layout.uses_ids);
    CHECK(hid_has(&layout, HID_REPORT_MOUSE) && !hid_has(&layout, HID_REPORT_KEYBOARD));
    CHECK(layout.reports[0].bits == 24);

    const uint8_t data[] = { 0x05, 0xFB, 0x10 };    /* links + Mitte, -5, +16 */
    const uint8_t *payload;
    size_t length;
    const hid_report_t *report = hid_match(&layout, data, sizeof(data), &payload, &length);
    hid_mouse_t mouse;
    CHECK(report && hid_decode_mouse(report, payload, length, &mouse));
    CHECK(mouse.dx == -5 && mouse.dy == 16 && mouse.buttons == 5 && !mouse.absolute);

    /* zu kurz: nicht verwenden */
    CHECK(!hid_decode_mouse(report, payload, 2, &mouse));
}

static void test_simple_keyboard(void)
{
    hid_layout_t layout;
    CHECK(hid_parse(simple_keyboard, sizeof(simple_keyboard), &layout));
    CHECK(layout.count == 1 && layout.reports[0].kind == HID_REPORT_KEYBOARD);
    CHECK(layout.reports[0].bits == 64);

    const uint8_t data[] = { 0x02, 0x00, 0x04, 0x1D, 0, 0, 0, 0 };  /* Umschalt + a + z */
    const uint8_t *payload;
    size_t length;
    const hid_report_t *report = hid_match(&layout, data, sizeof(data), &payload, &length);
    uint8_t boot[8];
    CHECK(report && hid_decode_keyboard(report, payload, length, boot));
    CHECK(memcmp(boot, data, 8) == 0);

    const uint8_t rollover[] = { 0, 0, 1, 1, 1, 1, 1, 1 };
    CHECK(hid_decode_keyboard(report, rollover, 8, boot) && boot[2] == 1);
}

static void test_receiver(void)
{
    hid_layout_t layout;
    CHECK(hid_parse(receiver, sizeof(receiver), &layout));
    CHECK(layout.count == 3 && layout.uses_ids);
    CHECK(hid_has(&layout, HID_REPORT_MOUSE) && !hid_has(&layout, HID_REPORT_KEYBOARD));

    const uint8_t *payload;
    size_t length;
    hid_mouse_t mouse;

    /* Maus: Kennung 1, rechte Taste, X = -3 (0xFFD), Y = +100 (0x064), Rad -1 */
    const uint8_t move[] = { 0x01, 0x02, 0xFD, 0x4F, 0x06, 0xFF };
    const hid_report_t *report = hid_match(&layout, move, sizeof(move), &payload, &length);
    CHECK(report && report->id == 1);
    CHECK(hid_decode_mouse(report, payload, length, &mouse));
    CHECK(mouse.dx == -3 && mouse.dy == 100 && mouse.wheel == -1 && mouse.buttons == 2);

    /* Sondertaste: Kennung 2 - das darf nie als Mausbewegung gelten */
    const uint8_t volume[] = { 0x02, 0xE9, 0x00 };
    report = hid_match(&layout, volume, sizeof(volume), &payload, &length);
    CHECK(report && report->kind == HID_REPORT_OTHER);
    CHECK(!hid_decode_mouse(report, payload, length, &mouse));

    /* Systemtaste: Kennung 3 */
    const uint8_t power[] = { 0x03, 0x01 };
    report = hid_match(&layout, power, sizeof(power), &payload, &length);
    CHECK(report && !hid_decode_mouse(report, payload, length, &mouse));

    /* unbekannte Kennung */
    const uint8_t unknown[] = { 0x09, 0x7F, 0x7F, 0x7F };
    CHECK(!hid_match(&layout, unknown, sizeof(unknown), &payload, &length));
}

static void test_tablet(void)
{
    hid_layout_t layout;
    CHECK(hid_parse(tablet, sizeof(tablet), &layout));
    CHECK(layout.count == 1 && layout.reports[0].kind == HID_REPORT_MOUSE);
    CHECK(layout.reports[0].absolute);

    const uint8_t data[] = { 0x01, 0xFF, 0x3F, 0xFF, 0x7F, 0x00 };  /* halb rechts, ganz unten */
    const uint8_t *payload;
    size_t length;
    hid_mouse_t mouse;
    const hid_report_t *report = hid_match(&layout, data, sizeof(data), &payload, &length);
    CHECK(report && hid_decode_mouse(report, payload, length, &mouse));
    CHECK(mouse.absolute && mouse.buttons == 1);
    CHECK(mouse.dx > 32700 && mouse.dx < 32800 && mouse.dy == 65535);
}

static void test_bitmap_keyboard(void)
{
    hid_layout_t layout;
    CHECK(hid_parse(bitmap_keyboard, sizeof(bitmap_keyboard), &layout));
    CHECK(layout.uses_ids && layout.reports[0].kind == HID_REPORT_KEYBOARD);

    /* Kennung 4, Strg links, Tasten 0x04 (a) und 0x2C (Leertaste) */
    uint8_t data[1 + 1 + 14] = { 0x04, 0x01 };
    data[2] |= 1 << 0;                              /* 0x04 */
    data[2 + (0x2C - 4) / 8] |= (uint8_t)(1 << ((0x2C - 4) % 8));
    const uint8_t *payload;
    size_t length;
    uint8_t boot[8];
    const hid_report_t *report = hid_match(&layout, data, sizeof(data), &payload, &length);
    CHECK(report && hid_decode_keyboard(report, payload, length, boot));
    CHECK(boot[0] == 0x01 && boot[2] == 0x04 && boot[3] == 0x2C && boot[4] == 0);
}

/* Unsinn darf nichts zum Absturz bringen (der Test läuft mit Speicherprüfung). */
static void test_fuzz(void)
{
    static const uint8_t *const samples[] = {
        simple_mouse, simple_keyboard, receiver, tablet, bitmap_keyboard,
    };
    static const size_t sizes[] = {
        sizeof(simple_mouse), sizeof(simple_keyboard), sizeof(receiver),
        sizeof(tablet), sizeof(bitmap_keyboard),
    };
    srand(12345);
    int parsed = 0;
    for (int round = 0; round < 20000; round++) {
        uint8_t descriptor[256], data[64];
        size_t length;
        if (round % 2) {                /* reiner Zufall */
            length = (size_t)(rand() % 256);
            for (size_t i = 0; i < length; i++)
                descriptor[i] = (uint8_t)rand();
        } else {                        /* echtes Beispiel, leicht beschädigt */
            int which = rand() % 5;
            length = sizes[which];
            memcpy(descriptor, samples[which], length);
            for (int i = 0; i < 1 + rand() % 4; i++)
                descriptor[(size_t)rand() % length] = (uint8_t)rand();
            if (rand() % 4 == 0)
                length = (size_t)rand() % length;
        }
        uint8_t *copy = malloc(length ? length : 1);    /* genau passend: Überläufe fallen auf */
        memcpy(copy, descriptor, length);
        hid_layout_t layout;
        if (hid_parse(copy, length, &layout)) {
            parsed++;
            for (int k = 0; k < 8; k++) {
                size_t data_length = (size_t)(rand() % 64);
                for (size_t i = 0; i < data_length; i++)
                    data[i] = (uint8_t)rand();
                uint8_t *packet = malloc(data_length ? data_length : 1);
                memcpy(packet, data, data_length);
                const uint8_t *payload;
                size_t payload_length;
                const hid_report_t *report =
                    hid_match(&layout, packet, data_length, &payload, &payload_length);
                if (report) {
                    hid_mouse_t mouse;
                    uint8_t boot[8];
                    hid_decode_mouse(report, payload, payload_length, &mouse);
                    hid_decode_keyboard(report, payload, payload_length, boot);
                }
                free(packet);
            }
        }
        free(copy);
    }
    CHECK(parsed > 1000);
}

int main(void)
{
    test_simple_mouse();
    test_simple_keyboard();
    test_receiver();
    test_tablet();
    test_bitmap_keyboard();
    test_fuzz();
    printf("%d Prüfungen an den HID-Berichten, %d Fehler\n", checks, failures);
    return failures ? 1 : 0;
}
