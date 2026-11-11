/* Eingabe: PS/2-Tastatur (und serielle Schnittstelle) -> Zeichenpuffer */
#pragma once

#include <stdbool.h>

/* Normale Zeichen sind Unicode-Codepoints; Sondertasten liegen darüber. */
#define KEY_UP    0x110001
#define KEY_DOWN  0x110002
#define KEY_LEFT  0x110003
#define KEY_RIGHT 0x110004

void keyboard_init(void);

/* Tastaturbelegung umschalten ("de" oder "us"); false bei unbekanntem Namen. */
bool keyboard_set_layout(const char *name);
const char *keyboard_layout_name(void);

/* Zeichen in den Eingabepuffer legen (aus einem Interrupt-Handler heraus). */
void input_push(int c);

/* Nächstes Zeichen holen; blockiert den aufrufenden Task, bis eines da ist. */
int input_getchar(void);
