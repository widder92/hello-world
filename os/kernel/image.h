/*
 * Bilder für den Browser: PNG, JPEG und GIF entschlüsseln und auf die
 * Anzeigegröße bringen. Reine Rechenarbeit ohne Hardware; läuft deshalb auch
 * im Test auf dem Entwicklungsrechner (tests/image_test.c).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Größer darf ein Bild nicht sein (Schutz vor Speichermangel) */
#define IMAGE_MAX_SIDE   16384
#define IMAGE_MAX_PIXELS (40u * 1000 * 1000)

typedef struct {
    int width, height;
    uint32_t *pixels;                   /* je Pixel 0xAARRGGBB; AA 255 = deckend */
    bool alpha;                         /* enthält durchscheinende Pixel */
} image_t;

/*
 * Entschlüsselt ein Bild; das Format wird am Anfang der Daten erkannt.
 * Bei einem Fehler NULL und in *error eine kurze Erklärung.
 */
image_t *image_decode(const uint8_t *data, size_t length, const char **error);
image_t *image_create(int width, int height);
void image_free(image_t *image);

/*
 * Liefert das Bild in der gewünschten Größe, durchscheinende Stellen mit
 * background unterlegt (Ergebnis: 0x00RRGGBB). Beim Verkleinern wird
 * gemittelt, damit nichts flimmert.
 */
image_t *image_scale(const image_t *source, int width, int height,
                     uint32_t background);

/* Die einzelnen Formate (von image_decode benutzt) */
image_t *png_decode(const uint8_t *data, size_t length, const char **error);
image_t *jpeg_decode(const uint8_t *data, size_t length, const char **error);
image_t *gif_decode(const uint8_t *data, size_t length, const char **error);

/*
 * Entpackt Daten im zlib-Format (wie in PNG). out_size ist die erwartete
 * Größe; liefert false, wenn die Daten beschädigt sind oder nicht passen.
 */
bool inflate_zlib(const uint8_t *in, size_t in_size, uint8_t *out, size_t out_size);
