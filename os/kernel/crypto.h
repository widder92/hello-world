/*
 * Kryptografie-Bausteine für TLS 1.3 und signierte Updates.
 *
 * Alles hier ist für HelloOS geschrieben und gegen eine etablierte Bibliothek
 * geprüft (siehe tests/crypto_test.c). Es ist aber keine gehärtete
 * Implementierung: Laufzeiten können von geheimen Werten abhängen.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* --- Hashfunktionen ------------------------------------------------------------ */

typedef struct {
    uint32_t state[8];
    uint64_t length;                    /* bisher verarbeitete Bytes */
    uint8_t buffer[64];
    size_t used;
} sha256_t;

void sha256_init(sha256_t *ctx);
void sha256_update(sha256_t *ctx, const void *data, size_t len);
void sha256_final(sha256_t *ctx, uint8_t out[32]);
void sha256(const void *data, size_t len, uint8_t out[32]);

typedef struct {
    uint64_t state[8];
    uint64_t length;
    uint8_t buffer[128];
    size_t used;
} sha512_t;

void sha512_init(sha512_t *ctx);
void sha512_update(sha512_t *ctx, const void *data, size_t len);
void sha512_final(sha512_t *ctx, uint8_t out[64]);

/* Schlüsselabhängiger Hash und Schlüsselableitung (HMAC, HKDF) mit SHA-256 */
void hmac_sha256(const uint8_t *key, size_t key_len, const void *data,
                 size_t len, uint8_t out[32]);
void hkdf_extract(const uint8_t *salt, size_t salt_len, const uint8_t *ikm,
                  size_t ikm_len, uint8_t out[32]);
/* Liefert höchstens 32 Bytes - mehr braucht TLS 1.3 mit SHA-256 nicht. */
void hkdf_expand(const uint8_t prk[32], const uint8_t *info, size_t info_len,
                 uint8_t *out, size_t out_len);

/* --- Verschlüsselung mit Echtheitsprüfung: ChaCha20-Poly1305 -------------------- */

void chacha20poly1305_encrypt(const uint8_t key[32], const uint8_t nonce[12],
                              const uint8_t *aad, size_t aad_len,
                              const uint8_t *plain, size_t len, uint8_t *cipher,
                              uint8_t tag[16]);
/* Liefert false, wenn die Daten verändert wurden; plain ist dann ungültig. */
bool chacha20poly1305_decrypt(const uint8_t key[32], const uint8_t nonce[12],
                              const uint8_t *aad, size_t aad_len,
                              const uint8_t *cipher, size_t len,
                              const uint8_t tag[16], uint8_t *plain);

/* --- Elliptische Kurve 25519 ------------------------------------------------------ */

/* Schlüsselaustausch: out = scalar * point (X25519, RFC 7748). */
void x25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t point[32]);
void x25519_public(uint8_t out[32], const uint8_t scalar[32]);

/* Prüft eine Ed25519-Signatur (RFC 8032). */
bool ed25519_verify(const uint8_t signature[64], const uint8_t public_key[32],
                    const void *message, size_t len);

/* --- Zufall ------------------------------------------------------------------------ */

/* Mischt beliebige Daten (Zeitpunkte von Eingaben, Paketen ...) in den Vorrat. */
void random_stir(const void *data, size_t len);
void random_bytes(void *out, size_t len);
