/*
 * Grafische Oberfläche: Fenster, Ereignisse und der Compositor, der alles
 * zu einem Bild zusammensetzt.
 *
 * Jede Anwendung ist ein Task, der in die Fläche seines Fensters zeichnet,
 * geänderte Bereiche mit window_invalidate() meldet und mit
 * window_get_event() auf Eingaben wartet.
 */
#pragma once

#include "gfx.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    EV_NONE,
    EV_KEY,                             /* key = Zeichen oder KEY_*, mods */
    EV_MOUSE_DOWN,                      /* x, y relativ zum Fensterinhalt */
    EV_MOUSE_UP,
    EV_MOUSE_MOVE,                      /* nur bei gedrückter Maustaste */
    EV_CLOSE,                           /* Schließen-Knopf angeklickt */
} event_type_t;

typedef struct {
    event_type_t type;
    int key, mods;
    int x, y, buttons;
} event_t;

typedef struct window window_t;

/* --- Für den Kernel ----------------------------------------------------------- */

/* Bildschirm übernehmen; ab jetzt erscheinen klog()-Meldungen dort. */
void gui_init(void);
bool gui_available(void);

/* Den Oberflächen-Task starten (Desktop, Taskleiste, Fensterverwaltung). */
void gui_start(void);

void gui_screen_size(int *width, int *height);

/* Fehlermeldung direkt auf den Bildschirm - funktioniert auch ohne Tasks. */
void gui_panic_screen(const char *text);

/* --- Für Gerätetreiber --------------------------------------------------------- */

void gui_post_key(int key, int mods);
void gui_post_mouse(int dx, int dy, int buttons);

/* --- Für Anwendungen ----------------------------------------------------------- */

/* Startet eine Anwendung: "terminal", "monitor", "malen", "info", "aus". */
bool gui_launch(const char *app);

/* Neues Fenster mit einer Inhaltsfläche der angegebenen Größe (oder NULL). */
window_t *window_create(const char *title, int width, int height);
surface_t *window_surface(window_t *win);

/* Fenster vom Bildschirm nehmen. Die Fläche bleibt gültig, bis die letzte
 * Referenz mit window_unref() abgegeben ist. */
void window_close(window_t *win);
void window_unref(window_t *win);

/* Kurzform für den Normalfall: schließen und eigene Referenz abgeben. */
void window_destroy(window_t *win);

/*
 * Normalerweise verfällt die Referenz des Erzeugers, wenn dessen Task endet.
 * Nach window_keep() bleibt sie bestehen, bis jemand window_unref() aufruft.
 */
void window_keep(window_t *win);

/* Meldet einen geänderten Bereich des Inhalts (Koordinaten im Fenster). */
void window_invalidate(window_t *win, int x, int y, int w, int h);

/*
 * Wartet auf das nächste Ereignis. Liefert false, wenn bis zum Ablauf der
 * Frist nichts passiert ist (timeout_ms 0 = unbegrenzt warten).
 */
bool window_get_event(window_t *win, event_t *ev, uint32_t timeout_ms);

/* Zeichnet einen Knopf mit zentrierter Beschriftung. */
void ui_button(surface_t *s, rect_t r, const char *label, bool highlighted);
