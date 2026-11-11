/* Selbstaktualisierung: signierte Updates aus dem Netz holen und installieren */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    bool enabled;                       /* ein Update-Schlüssel ist eingebaut */
    uint32_t build;                     /* laufende Build-Nummer */
    uint32_t available_build;           /* geladen und geprüft, wartet auf Zustimmung */
    uint32_t installed_build;           /* installiert, aktiv nach Neustart */
    bool declined;                      /* der Benutzer hat "Später" gewählt */
    char state[96];                     /* letzter Stand in Klartext */
    const char *url;
} update_info_t;

/* Startet den Task, der regelmäßig nach Updates sieht. */
void update_init(void);
void update_get_info(update_info_t *out);
uint32_t update_current_build(void);

/* "Was ist neu" - für die laufende Version bzw. für das wartende Update. */
const char *update_release_notes(void);
size_t update_available_notes(char *out, size_t size);

/*
 * Sieht nach, ob es ein Update gibt, lädt es und prüft die Signatur.
 * Installiert wird dabei nichts. Liefert true, wenn ein geprüftes Update
 * bereitliegt. Mit verbose erscheinen die Schritte auf der Konsole.
 */
bool update_check(bool verbose);

/* Installiert das bereitliegende Update - erst nach Zustimmung aufrufen. */
bool update_install(bool verbose);

/* Der Benutzer möchte jetzt nicht: nicht von selbst noch einmal fragen. */
void update_decline(void);
