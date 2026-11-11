/*
 * Prüfprogramm für die Kryptografie des Kernels. Läuft nicht in HelloOS,
 * sondern auf dem Entwicklungsrechner: liest Aufgaben von der Standardeingabe
 * (eine pro Zeile, Daten als Hex, "-" für leer) und gibt je eine Ergebniszeile
 * aus. tests/crypto_test.py vergleicht die Ergebnisse mit einer Bibliothek.
 */
#include "../kernel/crypto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FIELDS 5
#define MAX_BYTES  8192

static uint8_t field[MAX_FIELDS][MAX_BYTES];
static size_t field_len[MAX_FIELDS];

static size_t parse_hex(const char *text, uint8_t *out)
{
    if (strcmp(text, "-") == 0)
        return 0;
    size_t n = strlen(text) / 2;
    for (size_t i = 0; i < n; i++) {
        unsigned value;
        sscanf(text + 2 * i, "%2x", &value);
        out[i] = (uint8_t)value;
    }
    return n;
}

static void print_hex(const uint8_t *data, size_t len)
{
    if (len == 0)
        printf("-");
    for (size_t i = 0; i < len; i++)
        printf("%02x", data[i]);
    printf("\n");
}

int main(void)
{
    static char line[5 * 2 * MAX_BYTES + 64];
    static uint8_t out[MAX_BYTES + 64];

    while (fgets(line, sizeof(line), stdin)) {
        char *command = strtok(line, " \n");
        if (!command)
            continue;
        int fields = 0;
        for (char *token; fields < MAX_FIELDS && (token = strtok(NULL, " \n")); fields++)
            field_len[fields] = parse_hex(token, field[fields]);

        if (strcmp(command, "sha256") == 0) {
            /* In zwei Teilen füttern, um auch das Puffern zu prüfen */
            sha256_t ctx;
            sha256_init(&ctx);
            sha256_update(&ctx, field[0], field_len[0] / 3);
            sha256_update(&ctx, field[0] + field_len[0] / 3,
                          field_len[0] - field_len[0] / 3);
            sha256_final(&ctx, out);
            print_hex(out, 32);
        } else if (strcmp(command, "sha512") == 0) {
            sha512_t ctx;
            sha512_init(&ctx);
            sha512_update(&ctx, field[0], field_len[0] / 3);
            sha512_update(&ctx, field[0] + field_len[0] / 3,
                          field_len[0] - field_len[0] / 3);
            sha512_final(&ctx, out);
            print_hex(out, 64);
        } else if (strcmp(command, "hmac") == 0) {
            hmac_sha256(field[0], field_len[0], field[1], field_len[1], out);
            print_hex(out, 32);
        } else if (strcmp(command, "hkdf") == 0) {
            uint8_t prk[32];
            hkdf_extract(field[0], field_len[0], field[1], field_len[1], prk);
            hkdf_expand(prk, field[2], field_len[2], out, field[3][0]);
            print_hex(out, field[3][0]);
        } else if (strcmp(command, "seal") == 0) {
            chacha20poly1305_encrypt(field[0], field[1], field[2], field_len[2],
                                     field[3], field_len[3], out,
                                     out + field_len[3]);
            print_hex(out, field_len[3] + 16);
        } else if (strcmp(command, "open") == 0) {
            size_t len = field_len[3] - 16;
            if (chacha20poly1305_decrypt(field[0], field[1], field[2],
                                         field_len[2], field[3], len,
                                         field[3] + len, out))
                print_hex(out, len);
            else
                printf("FAIL\n");
        } else if (strcmp(command, "x25519") == 0) {
            x25519(out, field[0], field[1]);
            print_hex(out, 32);
        } else if (strcmp(command, "ed25519") == 0) {
            printf("%d\n", ed25519_verify(field[1], field[0], field[2],
                                          field_len[2]));
        } else {
            printf("unbekannt: %s\n", command);
        }
    }
    return 0;
}
