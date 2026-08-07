/**
 * Gemeinsame Vorbereitung für alle Tests.
 *
 * Läuft vor jeder Testdatei und vor allen Importen daraus – deshalb steht hier
 * auch die Umleitung der Datenbank: Der Prisma-Client liest `DATABASE_URL`
 * beim Erzeugen aus, und der entsteht erst, wenn eine Testdatei
 * `@/server/db` importiert.
 */

/** Feste Werte, damit Signaturen und Adressen reproduzierbar sind. */
process.env.APP_SECRET =
  process.env.APP_SECRET ?? "test-geheimnis-nur-fuer-die-testlaeufe-1234567890";
process.env.MAIL_REPLY_DOMAIN = process.env.MAIL_REPLY_DOMAIN ?? "example.org";
process.env.MAIL_REPLY_LOCALPART = process.env.MAIL_REPLY_LOCALPART ?? "ticket";

/**
 * Feste Zeitzone. Ohne sie liefert die Dienstzeiten-Rechnung auf einem Rechner
 * in einer anderen Zeitzone andere Ergebnisse als hier.
 */
process.env.TZ = "Europe/Berlin";

/** Kein Mailversand aus Tests heraus – nur protokollieren. */
process.env.MAIL_TRANSPORT = "none";

/**
 * Datenbank für die Tests unter `tests/db`.
 *
 * Der Name muss auf `_test` enden. Das ist keine Förmlichkeit: Die Tests
 * räumen zwischen den Läufen Tabellen leer, und ein versehentlich auf die
 * Entwicklungs- oder gar Produktivdatenbank gerichteter Lauf wäre nicht mehr
 * rückgängig zu machen. Die Prüfung steht in `tests/db/datenbank.ts`.
 */
process.env.DATABASE_URL =
  process.env.TEST_DATABASE_URL ??
  "postgresql://ticket:ticket@localhost:5432/ticketsystem_test?schema=public";
