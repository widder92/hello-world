/* Die Kommandozeile von HelloOS */
#pragma once

#define OS_NAME    "HelloOS"
#define OS_VERSION "0.3"

/*
 * Führt eine Shell auf der Konsole des laufenden Tasks aus. Kehrt zurück,
 * wenn die Konsole geschlossen wird.
 */
void shell_run(void);
