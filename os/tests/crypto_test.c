/*
 * Prüfprogramm für die Kryptografie des Kernels. Läuft nicht in HelloOS,
 * sondern auf dem Entwicklungsrechner: liest Aufgaben von der Standardeingabe
 * (eine pro Zeile, Daten als Hex, "-" für leer) und gibt je eine Ergebniszeile
 * aus. tests/crypto_test.py vergleicht die Ergebnisse mit einer Bibliothek.
 */
#include "../kernel/crypto.h"
#include "../kernel/x509.h"

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
        } else if (strcmp(command, "hash") == 0) {
            /* Felder: Verfahren (0 = SHA-256, 1 = SHA-384, 2 = SHA-512), Daten */
            hash_compute((hash_alg_t)field[0][0], field[1], field_len[1], out);
            print_hex(out, hash_size((hash_alg_t)field[0][0]));
        } else if (strcmp(command, "rsa_pkcs1") == 0 ||
                   strcmp(command, "rsa_pss") == 0) {
            /* Felder: Verfahren, Modul, Exponent, Hashwert, Signatur */
            public_key_t key = {
                .type = KEY_RSA,
                .modulus = field[1], .modulus_len = field_len[1],
                .exponent = field[2], .exponent_len = field_len[2],
            };
            hash_alg_t alg = (hash_alg_t)field[0][0];
            bool ok = command[5] == 'k'
                ? rsa_pkcs1_verify(&key, alg, field[3], field[4], field_len[4])
                : rsa_pss_verify(&key, alg, field[3], field[4], field_len[4]);
            printf("%d\n", ok);
        } else if (strcmp(command, "ecdsa") == 0) {
            /* Felder: Kurve (0 = P-256, 1 = P-384), Punkt, Hashwert, Signatur */
            public_key_t key = {
                .type = field[0][0] ? KEY_EC_P384 : KEY_EC_P256,
                .point = field[1],
            };
            printf("%d\n", ecdsa_verify(&key, field[2], field_len[2], field[3],
                                        field_len[3]));
        } else if (strcmp(command, "roots") == 0) {
            /* Vertrauenswürdige Wurzeln für alle folgenden "chain"-Aufgaben */
            uint8_t *copy = malloc(field_len[0]);
            memcpy(copy, field[0], field_len[0]);
            x509_add_roots(copy);
            printf("ok\n");
        } else if (strcmp(command, "selfsig") == 0) {
            /* Zertifikat lesen und seine Signatur mit dem eigenen Schlüssel prüfen */
            x509_cert_t cert;
            if (!x509_parse(field[0], field_len[0], &cert))
                printf("unlesbar\n");
            else if (cert.sig_alg == SIG_UNSUPPORTED || cert.key.type == KEY_NONE)
                printf("nicht unterstuetzt\n");
            else
                printf("%d\n", x509_check_signature(&cert, &cert.key));
        } else if (strcmp(command, "chain") == 0) {
            /* Felder: Servername, Zeit (8 Bytes), Kette (je 2 Bytes Länge + Daten) */
            const uint8_t *certs[16];
            size_t lens[16];
            int count = 0;
            for (size_t pos = 0; pos + 2 <= field_len[2] && count < 16; ) {
                size_t len = ((size_t)field[2][pos] << 8) | field[2][pos + 1];
                certs[count] = field[2] + pos + 2;
                lens[count++] = len;
                pos += 2 + len;
            }
            uint64_t now = 0;
            for (int i = 0; i < 8; i++)
                now = (now << 8) | field[1][i];
            field[0][field_len[0]] = 0;
            public_key_t key;
            const char *error = x509_verify_chain(certs, lens, count,
                                                  (const char *)field[0], now, &key);
            printf("%s\n", error ? error : "OK");
        } else {
            printf("unbekannt: %s\n", command);
        }
    }
    return 0;
}
