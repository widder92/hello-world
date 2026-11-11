/* Die mitgelieferten Fenster-Anwendungen (jede läuft als eigener Task) */
#pragma once

void terminal_app(void *arg);           /* Kommandozeile */
void monitor_app(void *arg);            /* Systemmonitor */
void paint_app(void *arg);              /* Malprogramm */
void about_app(void *arg);              /* Über HelloOS */
void power_app(void *arg);              /* Herunterfahren / Neu starten */
void update_app(void *arg);             /* Hinweis: Update installiert */

/* Rechner ausschalten bzw. neu starten - kehren nicht zurück. */
void power_shutdown(void) __attribute__((noreturn));
void power_reboot(void) __attribute__((noreturn));
