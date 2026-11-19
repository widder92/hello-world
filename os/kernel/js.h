/*
 * JavaScript: eine eigene, kleine Maschine für die Skripte von Webseiten.
 *
 * Sie liest den Quelltext in einen Baum (js_parse.c) und führt ihn direkt aus
 * (js_run.c). Alle Zwischenwerte liegen auf einem eigenen Stapel; der
 * Speicher wird von Zeit zu Zeit bereinigt (js_core.c). Unterstützt wird
 * ECMAScript 5 und fast alles, was seit 2015 dazukam: let/const,
 * Pfeilfunktionen, Klassen (auch mit privaten Mitgliedern),
 * Vorlagen-Zeichenketten, Zerlegung (destructuring), Spread, Symbole und
 * Iteratoren (js_sym.c), Map/Set (js_coll.c), Generatoren und async/await
 * (js_co.c), Proxy und Reflect (js_proxy.c), Module (js_module.c). Es fehlt
 * "with"; große Ganzzahlen (10n) sind gewöhnliche Zahlen.
 *
 * Weil die Skripte von fremden Seiten kommen, hat alles Grenzen: Speicher,
 * Rechenschritte, Zeit, Verschachtelung. Wer eine davon überschreitet, wird
 * abgebrochen - der Rest des Systems läuft weiter.
 *
 * Reine Rechenarbeit ohne Hardware; läuft auch im Test auf dem
 * Entwicklungsrechner (tests/js_test.c). Die Dateien js*.c rechnen mit
 * Kommazahlen und werden deshalb als einzige mit SSE übersetzt.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct js_state js_state;

typedef struct {
    size_t heap_max;                    /* Speicher für Werte in Bytes (0: 32 MiB) */
    size_t stack_bytes;                 /* so viel C-Stack darf die Ausführung brauchen (0: 256 KiB) */
    /* Uhrzeit in Millisekunden seit 1970 (für Date) und eine gleichmäßig laufende Uhr */
    int64_t (*clock_ms)(void *ctx);
    uint64_t (*ticks_ms)(void *ctx);
    /* Ausgabe von console.log und Fehlermeldungen; darf NULL sein */
    void (*print)(void *ctx, const char *text);
    /* true: Es soll sofort Schluss sein (etwa weil das Fenster geschlossen wird); darf NULL sein */
    bool (*cancelled)(void *ctx);
    void *ctx;
} js_host_t;

js_state *js_create(const js_host_t *host);
void js_destroy(js_state *J);

/*
 * Führt Quelltext aus. Liefert false, wenn er sich nicht lesen ließ, ein
 * Fehler unbehandelt blieb oder eine Grenze überschritten wurde; die Meldung
 * steht dann in js_last_error(). budget_ms: so lange darf es höchstens dauern.
 */
bool js_run(js_state *J, const char *source, size_t length, const char *name,
            uint32_t budget_ms);

const char *js_last_error(const js_state *J);

/* Wurde wegen einer Grenze (Speicher, Zeit) abgebrochen? Dann ist der Zustand unbrauchbar. */
bool js_dead(const js_state *J);
