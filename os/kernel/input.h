/* Eingabegeräte: Tastencodes und die gemeinsame Tastaturlogik */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Normale Zeichen sind Unicode-Codepoints; Sondertasten liegen darüber. */
enum {
    KEY_UP = 0x110001,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_HOME,
    KEY_END,
    KEY_PAGE_UP,
    KEY_PAGE_DOWN,
    KEY_DELETE,
    KEY_F1,                             /* F1 bis F12 folgen direkt aufeinander */
    KEY_F12 = KEY_F1 + 11,
};

/* Zustand der Umschalttasten, wird mit jedem Tastendruck mitgeliefert. */
enum {
    MOD_SHIFT = 1,
    MOD_CTRL = 2,
    MOD_ALT = 4,
};

/* Maustasten */
enum {
    MOUSE_LEFT = 1,
    MOUSE_RIGHT = 2,
    MOUSE_MIDDLE = 4,
};

void keyboard_init(void);

/* Tastaturbelegung umschalten ("de" oder "us"); false bei unbekanntem Namen. */
bool keyboard_set_layout(const char *name);
const char *keyboard_layout_name(void);

/*
 * Eine Taste wurde gedrückt oder losgelassen. Treiber (PS/2, USB) melden
 * Tasten als Scancode aus "Set 1"; extended steht für das Präfix 0xE0.
 */
void keyboard_handle_scancode(uint8_t scancode, bool extended, bool released);

/* PS/2-Controller: Tastatur und Maus. */
void ps2_init(void);
void ps2_poll(void);                    /* vom Systemtakt aufgerufen */
bool ps2_present(void);
