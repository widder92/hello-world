/*
 * Prüfprogramm für die Bild-Dekoder (kernel/image*.c, inflate.c) auf dem
 * Entwicklungsrechner. Gesteuert von tests/image_test.py:
 *
 *   image_test decode <datei> <ausgabe>   Bild entschlüsseln, als RGBA ablegen
 *   image_test scale <datei> <b> <h> <ausgabe>
 *   image_test fuzz <datei> <runden>      Datei beschädigen und entschlüsseln
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../kernel/image.h"

/* Was der Kernel sonst bereitstellt */
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
    uint8_t *data = malloc(length > 0 ? (size_t)length : 1);   /* genau passend: Überläufe fallen auf */
    if (fread(data, 1, (size_t)length, f) != (size_t)length) {
        fclose(f);
        free(data);
        return NULL;
    }
    fclose(f);
    *size = (size_t)length;
    return data;
}

static int write_rgba(const image_t *image, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return 1;
    for (size_t i = 0; i < (size_t)image->width * (size_t)image->height; i++) {
        uint32_t p = image->pixels[i];
        uint8_t rgba[4] = { (uint8_t)(p >> 16), (uint8_t)(p >> 8), (uint8_t)p, (uint8_t)(p >> 24) };
        fwrite(rgba, 1, 4, f);
    }
    fclose(f);
    printf("%d %d %d\n", image->width, image->height, image->alpha ? 1 : 0);
    return 0;
}

int main(int argc, char **argv)
{
    size_t size = 0;
    const char *error = NULL;
    if (argc < 3)
        return 2;
    uint8_t *data = read_file(argv[2], &size);
    if (!data) {
        printf("Datei nicht lesbar\n");
        return 2;
    }

    if (strcmp(argv[1], "decode") == 0 && argc == 4) {
        image_t *image = image_decode(data, size, &error);
        if (!image) {
            printf("FEHLER %s\n", error ? error : "?");
            return 1;
        }
        int result = write_rgba(image, argv[3]);
        image_free(image);
        free(data);
        return result;
    }
    if (strcmp(argv[1], "scale") == 0 && argc == 6) {
        image_t *image = image_decode(data, size, &error);
        if (!image) {
            printf("FEHLER %s\n", error ? error : "?");
            return 1;
        }
        image_t *scaled = image_scale(image, atoi(argv[3]), atoi(argv[4]), 0x00FFFFFF);
        if (!scaled)
            return 1;
        int result = write_rgba(scaled, argv[5]);
        image_free(scaled);
        image_free(image);
        free(data);
        return result;
    }
    if (strcmp(argv[1], "fuzz") == 0 && argc == 4) {
        int rounds = atoi(argv[3]), decoded = 0;
        srand(4711);
        for (int round = 0; round < rounds; round++) {
            size_t length = size;
            if (rand() % 5 == 0 && size > 1)
                length = 1 + (size_t)rand() % (size - 1);      /* abgeschnitten */
            uint8_t *copy = malloc(length);
            memcpy(copy, data, length);
            int changes = 1 + rand() % 6;
            for (int i = 0; i < changes; i++) {
                size_t at = (size_t)rand() % length;
                if (rand() % 3 == 0 && at > 20)
                    copy[at] = (uint8_t)rand();
                else
                    copy[at] ^= (uint8_t)(1 << (rand() % 8));
            }
            image_t *image = image_decode(copy, length, &error);
            if (image) {
                decoded++;
                image_t *scaled = image_scale(image, 1 + rand() % 40, 1 + rand() % 40, 0x00FFFFFF);
                image_free(scaled);
                image_free(image);
            }
            free(copy);
        }
        printf("%d\n", decoded);
        free(data);
        return 0;
    }
    return 2;
}
