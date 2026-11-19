/*
 * Seitenaufbau mit Kastenmodell: setzt eine Webseite aus ihrem Abbild (dom.h)
 * so, wie ihre Gestaltung es verlangt - jedes Element ein Kasten mit Außen-
 * und Innenabstand, Rahmen und Hintergrund; Kästen untereinander, nebeneinander
 * (float, flex, grid, Tabellen) oder frei platziert (position); Text in Zeilen
 * mit eigener Höhe.
 *
 * Das Ergebnis ist eine Seite (html.h) mit Lagen in Pixeln. Der einfache
 * Seitenaufbau (html_layout) bleibt daneben bestehen: als Darstellung im
 * Zeilenraster, die nur Text, Links, Bilder und Formulare zeigt.
 *
 * Reine Rechenarbeit ohne Hardware; läuft auch im Test auf dem
 * Entwicklungsrechner (tests/layout_test.c).
 */
#pragma once

#include "css.h"
#include "dom.h"
#include "html.h"

typedef struct {
    int width, height;                  /* Größe der Anzeige in Pixeln */
    const char *base_url;               /* Adresse der Seite */
    const css_sheet_t *css;             /* ihre Stylesheets, oder NULL */
    const html_text_t *text;            /* Schriftauskunft und die üblichen Farben */
    const html_pictures_t *pictures;    /* Auskunft über Bilder, oder NULL: keine zeigen */
    /*
     * Ober- und Unterlänge einer Schrift (Pixel über und unter der
     * Grundlinie); ohne diese Auskunft wird geschätzt.
     */
    void (*metrics)(void *ctx, uint8_t size, uint8_t style, int *ascent, int *descent);
    void *metrics_ctx;
    bool scripts;                       /* die Skripte der Seite laufen */
    bool geometry;                      /* die Lage jedes Elements festhalten (page->rects) */
} layout_options_t;

/* Setzt die Seite; NULL bei Speichermangel. Das Abbild bleibt unverändert. */
page_t *layout_document(const dom_doc *doc, const layout_options_t *options);
