/* Die mitgelieferten Fenster-Anwendungen (jede läuft als eigener Task) */
#pragma once

void terminal_app(void *arg);           /* Kommandozeile */
void monitor_app(void *arg);            /* Systemmonitor */
void paint_app(void *arg);              /* Malprogramm */
void about_app(void *arg);              /* Über HelloOS */
void power_app(void *arg);              /* Herunterfahren / Neu starten */
void update_app(void *arg);             /* Update: nachfragen, installieren */
void claude_app(void *arg);             /* Gespräch mit Claude */
void browser_app(void *arg);            /* einfacher Browser; arg: Adresse (kmalloc) oder NULL */
void transfer_app(void *arg);           /* Dateien per SFTP übertragen */
void editor_app(void *arg);             /* Texteditor; arg: Dateiname (kmalloc) oder NULL */

/* Rechner ausschalten bzw. neu starten - kehren nicht zurück. */
void power_shutdown(void) __attribute__((noreturn));
void power_reboot(void) __attribute__((noreturn));
