/* Bilder: Format erkennen, anlegen, freigeben und auf Anzeigegröße bringen */
#include "image.h"
#include "memory.h"
#include "string.h"

image_t *image_create(int width, int height)
{
    if (width <= 0 || height <= 0 || width > IMAGE_MAX_SIDE || height > IMAGE_MAX_SIDE ||
        (uint64_t)width * (uint64_t)height > IMAGE_MAX_PIXELS)
        return NULL;
    image_t *image = kzalloc(sizeof(*image));
    if (!image)
        return NULL;
    image->pixels = kmalloc((size_t)width * (size_t)height * sizeof(uint32_t));
    if (!image->pixels) {
        kfree(image);
        return NULL;
    }
    image->width = width;
    image->height = height;
    return image;
}

void image_free(image_t *image)
{
    if (!image)
        return;
    kfree(image->pixels);
    kfree(image);
}

image_t *image_decode(const uint8_t *data, size_t length, const char **error)
{
    static const char *unknown = "unbekanntes Bildformat";
    const char *ignored;
    if (!error)
        error = &ignored;
    *error = unknown;
    if (length >= 8 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G')
        return png_decode(data, length, error);
    if (length >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF)
        return jpeg_decode(data, length, error);
    if (length >= 6 && memcmp(data, "GIF8", 4) == 0)
        return gif_decode(data, length, error);
    if (length >= 12 && memcmp(data, "RIFF", 4) == 0 && memcmp(data + 8, "WEBP", 4) == 0)
        *error = "WebP-Bilder werden noch nicht unterstützt";
    else if (length >= 5 && (memcmp(data, "<svg", 4) == 0 || memcmp(data, "<?xml", 5) == 0))
        *error = "SVG-Grafiken werden noch nicht unterstützt";
    return NULL;
}

/* Mischt einen Bildpunkt (mit Deckkraft alpha) über die Hintergrundfarbe. */
static uint32_t over(uint32_t pixel, uint32_t alpha, uint32_t background)
{
    if (alpha == 255)
        return pixel & 0x00FFFFFFu;
    uint32_t result = 0;
    for (int shift = 0; shift <= 16; shift += 8) {
        uint32_t front = (pixel >> shift) & 0xFF, back = (background >> shift) & 0xFF;
        result |= ((front * alpha + back * (255 - alpha) + 127) / 255) << shift;
    }
    return result;
}

image_t *image_scale(const image_t *source, int width, int height, uint32_t background)
{
    image_t *result = image_create(width, height);
    if (!result)
        return NULL;

    /* Gleiche Größe: nur den Hintergrund einrechnen */
    if (width == source->width && height == source->height) {
        size_t count = (size_t)width * (size_t)height;
        for (size_t i = 0; i < count; i++)
            result->pixels[i] = over(source->pixels[i],
                                     source->alpha ? source->pixels[i] >> 24 : 255, background);
        return result;
    }

    /*
     * Jeder Zielpunkt bekommt den Mittelwert aller Quellpunkte, die er
     * überdeckt (beim Vergrößern ist das genau einer).
     */
    for (int y = 0; y < height; y++) {
        int y0 = (int)((int64_t)y * source->height / height);
        int y1 = (int)((int64_t)(y + 1) * source->height / height);
        if (y1 <= y0)
            y1 = y0 + 1;
        if (y1 > source->height)
            y1 = source->height;
        for (int x = 0; x < width; x++) {
            int x0 = (int)((int64_t)x * source->width / width);
            int x1 = (int)((int64_t)(x + 1) * source->width / width);
            if (x1 <= x0)
                x1 = x0 + 1;
            if (x1 > source->width)
                x1 = source->width;

            uint64_t red = 0, green = 0, blue = 0, alpha = 0;
            for (int sy = y0; sy < y1; sy++) {
                const uint32_t *row = source->pixels + (size_t)sy * source->width;
                for (int sx = x0; sx < x1; sx++) {
                    /* Bilder ohne Transparenz gelten ganz als deckend */
                    uint32_t pixel = row[sx], a = source->alpha ? pixel >> 24 : 255;
                    /* durchsichtige Punkte tragen keine Farbe bei */
                    red += ((pixel >> 16) & 0xFF) * a;
                    green += ((pixel >> 8) & 0xFF) * a;
                    blue += (pixel & 0xFF) * a;
                    alpha += a;
                }
            }
            uint64_t count = (uint64_t)(x1 - x0) * (uint64_t)(y1 - y0);
            uint32_t pixel = 0, a = 0;
            if (alpha) {
                a = (uint32_t)(alpha / count);
                pixel = ((uint32_t)(red / alpha) << 16) | ((uint32_t)(green / alpha) << 8) |
                        (uint32_t)(blue / alpha);
            }
            result->pixels[(size_t)y * width + x] = over(pixel, a, background);
        }
    }
    return result;
}
