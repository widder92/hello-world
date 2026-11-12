/*
 * Prüfprogramm für den Schriftrenderer (kernel/truetype.c) auf dem
 * Entwicklungsrechner. Gesteuert von tests/font_test.py:
 *
 *   font_test render <schrift> <größe> <kursiv 0|1> <zeichen...>
 *       je Zeichen eine Zeile "U+XXXX umriss vorschub breite höhe links oben"
 *       und danach die Deckkraft-Werte als Hexzeichen
 *   font_test metrics <schrift> <größe>
 *   font_test fuzz <schrift> <runden>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../kernel/truetype.h"

void *kmalloc(size_t n) { return malloc(n ? n : 1); }
void *kzalloc(size_t n) { return calloc(1, n ? n : 1); }
void kfree(void *p) { free(p); }

static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long length = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc(length > 0 ? (size_t)length : 1);
    if (fread(data, 1, (size_t)length, f) != (size_t)length) {
        fclose(f);
        return NULL;
    }
    fclose(f);
    *size = (size_t)length;
    return data;
}

int main(int argc, char **argv)
{
    size_t size = 0;
    if (argc < 4)
        return 2;
    uint8_t *data = read_file(argv[2], &size);
    ttf_t font;
    if (!data)
        return 2;

    if (strcmp(argv[1], "fuzz") == 0) {
        int rounds = atoi(argv[3]), opened = 0;
        srand(99);
        for (int round = 0; round < rounds; round++) {
            size_t length = rand() % 4 == 0 ? 1 + (size_t)rand() % size : size;
            uint8_t *copy = malloc(length);
            memcpy(copy, data, length);
            for (int i = 0; i < 1 + rand() % 40; i++) {
                /* vor allem das Tabellenverzeichnis und zufällige Stellen treffen */
                size_t at = rand() % 3 == 0 ? (size_t)rand() % (length < 400 ? length : 400)
                                            : (size_t)rand() % length;
                copy[at] = (uint8_t)rand();
            }
            if (ttf_open(&font, copy, length)) {
                opened++;
                for (int i = 0; i < 40; i++) {
                    uint32_t cp = rand() % 2 ? (uint32_t)(32 + rand() % 400) : (uint32_t)rand() % 70000;
                    int glyph = rand() % 5 == 0 ? rand() % 70000 : ttf_glyph(&font, cp);
                    ttf_bitmap_t bitmap;
                    ttf_advance(&font, glyph, 8 + rand() % 60);
                    if (ttf_render(&font, glyph, 6 + rand() % 50, rand() % 2, &bitmap))
                        free(bitmap.coverage);
                }
            }
            free(copy);
        }
        printf("%d\n", opened);
        free(data);
        return 0;
    }

    if (!ttf_open(&font, data, size)) {
        printf("FEHLER: Schrift nicht lesbar\n");
        free(data);
        return 1;
    }
    int pixels = atoi(argv[3]);
    if (strcmp(argv[1], "metrics") == 0) {
        int ascent, descent, line;
        ttf_line_metrics(&font, pixels, &ascent, &descent, &line);
        printf("%d %d %d %d\n", font.units_per_em, ascent, descent, line);
        free(data);
        return 0;
    }
    if (strcmp(argv[1], "render") == 0 && argc >= 6) {
        bool oblique = atoi(argv[4]) != 0;
        for (int i = 5; i < argc; i++) {
            uint32_t cp = (uint32_t)strtoul(argv[i], NULL, 16);
            int glyph = ttf_glyph(&font, cp);
            ttf_bitmap_t bitmap;
            if (!ttf_render(&font, glyph, pixels, oblique, &bitmap)) {
                printf("U+%04X FEHLER\n", cp);
                continue;
            }
            printf("U+%04X %d %d %d %d %d %d\n", cp, glyph, ttf_advance(&font, glyph, pixels),
                   bitmap.width, bitmap.height, bitmap.left, bitmap.top);
            for (int k = 0; k < bitmap.width * bitmap.height; k++)
                printf("%02x", bitmap.coverage[k]);
            printf("\n");
            free(bitmap.coverage);
        }
        free(data);
        return 0;
    }
    free(data);
    return 2;
}
