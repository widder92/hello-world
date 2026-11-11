/*
 * X.509-Zertifikate: Damit weist sich ein Webserver aus. Geprüft wird, ob
 * das Zertifikat zum Servernamen passt, gerade gültig ist und über eine
 * Kette von Signaturen auf ein eingebautes Wurzelzertifikat zurückführt.
 */
#pragma once

#include "crypto.h"

typedef enum {
    SIG_UNSUPPORTED,
    SIG_RSA_SHA256, SIG_RSA_SHA384, SIG_RSA_SHA512,
    SIG_ECDSA_SHA256, SIG_ECDSA_SHA384, SIG_ECDSA_SHA512,
} x509_sig_t;

/* Die Zeiger verweisen in die übergebenen Zertifikatsdaten. */
typedef struct {
    const uint8_t *tbs;                 /* der signierte Teil */
    size_t tbs_len;
    const uint8_t *issuer, *subject;    /* Aussteller und Inhaber (kodiert) */
    size_t issuer_len, subject_len;
    uint64_t not_before, not_after;     /* als Zahl JJJJMMTThhmmss */
    x509_sig_t sig_alg;
    const uint8_t *signature;
    size_t signature_len;
    public_key_t key;
    bool is_ca;                         /* darf selbst Zertifikate ausstellen */
    const uint8_t *names;               /* Liste der Namen, für die es gilt */
    size_t names_len;
} x509_cert_t;

bool x509_parse(const uint8_t *der, size_t len, x509_cert_t *out);

/* Prüft die Signatur eines Zertifikats mit dem Schlüssel des Ausstellers. */
bool x509_check_signature(const x509_cert_t *cert, const public_key_t *issuer_key);

/*
 * Meldet eine Sammlung vertrauenswürdiger Wurzelzertifikate an. Format:
 * je Zertifikat zwei Bytes Länge (höchstwertiges zuerst) und die Daten,
 * am Ende die Länge 0.
 */
void x509_add_roots(const uint8_t *blob);

/*
 * Prüft die Kette eines Servers (certs[0] ist sein eigenes Zertifikat, der
 * Rest sind Zwischenzertifikate). now ist die aktuelle Zeit als
 * JJJJMMTThhmmss. Liefert NULL, wenn alles stimmt, sonst den Grund; bei
 * Erfolg steht in *server_key der öffentliche Schlüssel des Servers.
 */
const char *x509_verify_chain(const uint8_t *const *certs, const size_t *lens,
                              int count, const char *host, uint64_t now,
                              public_key_t *server_key);
