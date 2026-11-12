/*
 * HID-Berichte verstehen: Tastaturen und Mäuse beschreiben selbst, wie die
 * Daten aussehen, die sie schicken ("Report Descriptor"). Wer sich daran
 * hält, statt ein festes Format anzunehmen, kommt auch mit Funkempfängern
 * zurecht, die über einen Kanal Mausbewegungen, Sondertasten und anderes
 * mischen - jeweils mit einer Kennung vorweg.
 *
 * Dieser Teil ist reine Rechenarbeit ohne Hardware und läuft deshalb auch im
 * Test auf dem Entwicklungsrechner (tests/hid_test.c).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HID_MAX_REPORTS 12

/* Ein Feld im Bericht: wo es steht und wie groß es ist (bits 0: fehlt). */
typedef struct {
    uint16_t offset;                    /* in Bits, ohne die Kennung am Anfang */
    uint8_t bits;                       /* Größe eines Eintrags */
    uint8_t count;                      /* Anzahl der Einträge */
    int32_t minimum, maximum;           /* Wertebereich laut Beschreibung */
} hid_field_t;

typedef enum { HID_REPORT_OTHER, HID_REPORT_MOUSE, HID_REPORT_KEYBOARD } hid_report_kind_t;

typedef struct {
    uint8_t id;                         /* Kennung; 0, wenn das Gerät keine benutzt */
    uint8_t kind;
    uint16_t bits;                      /* Länge der Daten ohne Kennung */

    /* Maus */
    hid_field_t buttons;                /* je Taste ein Bit */
    hid_field_t x, y, wheel;
    bool absolute;                      /* x/y sind Positionen statt Bewegungen */

    /* Tastatur */
    hid_field_t modifiers;              /* 8 Bits: Strg, Umschalt, Alt, ... */
    hid_field_t keys;                   /* Liste gedrückter Tasten (Nummern) */
    hid_field_t bitmap;                 /* oder: je Taste ein Bit ... */
    uint16_t bitmap_first;              /* ... ab dieser Tastennummer */
} hid_report_t;

typedef struct {
    bool uses_ids;                      /* Berichte beginnen mit ihrer Kennung */
    int count;
    hid_report_t reports[HID_MAX_REPORTS];
} hid_layout_t;

/* Wertet eine Beschreibung aus. false: nicht lesbar (dann nicht benutzen). */
bool hid_parse(const uint8_t *descriptor, size_t length, hid_layout_t *out);

/* Enthält die Beschreibung Berichte der angegebenen Art? */
bool hid_has(const hid_layout_t *layout, hid_report_kind_t kind);

/*
 * Ordnet empfangene Daten einem Bericht zu. Liefert NULL, wenn die Kennung
 * unbekannt ist; sonst zeigen payload/payload_length auf die Daten dahinter.
 */
const hid_report_t *hid_match(const hid_layout_t *layout, const uint8_t *data,
                              size_t length, const uint8_t **payload,
                              size_t *payload_length);

typedef struct {
    int dx, dy, wheel;                  /* bei absolute: x, y in 0..65535 */
    int buttons;                        /* Bit 0 links, 1 rechts, 2 Mitte */
    bool absolute;
} hid_mouse_t;

/* false: Bericht zu kurz oder kein Mausbericht. */
bool hid_decode_mouse(const hid_report_t *report, const uint8_t *payload,
                      size_t length, hid_mouse_t *out);

/*
 * Bringt einen Tastaturbericht in die einfache Standardform: Byte 0 die
 * Umschalttasten, Byte 2 bis 7 die Nummern von bis zu sechs gedrückten Tasten.
 */
bool hid_decode_keyboard(const hid_report_t *report, const uint8_t *payload,
                         size_t length, uint8_t boot[8]);
