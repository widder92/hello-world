/* Ton: Systemklänge über die Soundkarte (Intel HD Audio) */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    SOUND_STARTUP,                      /* HelloOS ist gestartet */
    SOUND_SHUTDOWN,                     /* wird heruntergefahren */
    SOUND_NOTIFY,                       /* Hinweis oder Rückfrage */
    SOUND_ERROR,                        /* etwas ist schiefgegangen */
    SOUND_COUNT,
} sound_id_t;

/* Sucht die Soundkarte, richtet die Ausgänge ein und spielt den Startklang. */
void sound_init(void);
bool sound_available(void);
/* Kurze Beschreibung der Soundkarte und der gefundenen Ausgänge. */
const char *sound_describe(void);

/* Spielt einen Klang (kehrt sofort zurück; mehrere mischen sich). */
void sound_play(sound_id_t id);
/* Wartet, bis alles verklungen ist - höchstens timeout_ms. */
void sound_wait(uint32_t timeout_ms);

/* Lautstärke in Prozent; 0 ist stumm. Wird auf dem Datenträger gemerkt. */
int sound_volume(void);
void sound_set_volume(int percent);
