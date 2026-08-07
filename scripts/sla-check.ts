/**
 * Fristenprüfung von Hand auslösen.
 *
 * Im laufenden Betrieb macht das der Hintergrunddienst zyklisch. Dieses
 * Skript ist für die Abnahme und für die Fehlersuche: Man sieht sofort, wie
 * viele Vorgänge gewarnt und wie viele als überfällig markiert wurden.
 *
 * Aufruf:  npm run sla:check
 */

import { PrismaClient } from "@prisma/client";

import { pruefeFristen } from "../src/server/services/sla";

const db = new PrismaClient();

async function main() {
  const start = Date.now();
  const ergebnis = await pruefeFristen();

  console.log("Fristenprüfung abgeschlossen:");
  console.log(`  geprüft:        ${ergebnis.geprueft}`);
  console.log(`  neu gewarnt:    ${ergebnis.gewarnt}`);
  console.log(`  überschritten:  ${ergebnis.ueberschritten}`);
  console.log(`  Dauer:          ${Date.now() - start} ms`);
}

main()
  .catch((error) => {
    console.error(error);
    process.exit(1);
  })
  .finally(async () => {
    await db.$disconnect();
  });
