/*
 * SSH-Client (Protokoll 2): eine verschlüsselte, geprüfte Verbindung zu einem
 * Server, Anmeldung mit Benutzername und Passwort, darauf ein Kanal für einen
 * Dienst wie SFTP.
 *
 * Unterstützt wird, was aktuelle Server anbieten:
 *   Schlüsselaustausch  curve25519-sha256, ecdh-sha2-nistp256
 *   Serverschlüssel     ssh-ed25519, ecdsa-sha2-nistp256/384, rsa-sha2-256/512
 *   Verschlüsselung     chacha20-poly1305@openssh.com, aes128/192/256-ctr
 *                       (mit hmac-sha2-256)
 *
 * Anders als bei HTTPS gibt es keine Zertifizierungsstellen: Beim ersten
 * Verbinden zeigt HelloOS den Fingerabdruck des Serverschlüssels und merkt
 * ihn sich, wenn der Benutzer zustimmt. Ändert er sich später, wird gewarnt.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct ssh ssh_t;

/*
 * Fragt, ob dem Serverschlüssel vertraut werden soll. changed: Für diesen
 * Server war bisher ein anderer Schlüssel gespeichert.
 */
typedef bool (*ssh_trust_t)(void *ctx, const char *server, const char *key_type,
                            const char *fingerprint, bool changed);

typedef struct {
    const char *host;
    uint16_t port;                      /* 0: Standard (22) */
    const char *user;
    const char *password;
    ssh_trust_t trust;
    void *trust_ctx;
} ssh_options_t;

/* Verbindet und meldet an; bei einem Fehler NULL und eine Erklärung in error. */
ssh_t *ssh_connect(const ssh_options_t *options, char *error, size_t error_size);

/* Öffnet einen Kanal und startet darin einen Dienst des Servers ("sftp"). */
bool ssh_open_subsystem(ssh_t *ssh, const char *name);

/* Daten des Kanals: senden bzw. genau len Bytes empfangen. */
bool ssh_write(ssh_t *ssh, const void *data, size_t len);
bool ssh_read(ssh_t *ssh, void *buffer, size_t len);

/* Erklärung zum letzten Fehler. */
const char *ssh_error(const ssh_t *ssh);
void ssh_close(ssh_t *ssh);
