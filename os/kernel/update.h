/* Selbstaktualisierung: signierte Updates aus dem Netz holen und installieren */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool enabled;                       /* ein Update-Schlüssel ist eingebaut */
    uint32_t build;                     /* laufende Build-Nummer */
    uint32_t installed_build;           /* installiert, aktiv nach Neustart (oder 0) */
    char state[96];                     /* letzter Stand in Klartext */
    const char *url;
} update_info_t;

/* Startet den Task, der regelmäßig nach Updates sieht. */
void update_init(void);
void update_get_info(update_info_t *out);
uint32_t update_current_build(void);

/*
 * Sucht jetzt nach einem Update und installiert es. Mit verbose erscheinen
 * die einzelnen Schritte auf der Konsole des Aufrufers. Liefert true, wenn
 * ein Update installiert wurde.
 */
bool update_run(bool verbose);
