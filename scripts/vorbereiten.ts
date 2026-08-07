/**
 * Datenbank startklar machen.
 *
 * Wird beim Hochfahren mit Docker einmal ausgeführt, bevor Anwendung und
 * Hintergrunddienst starten. Zwei Schritte:
 *
 *  1. Migrationen anwenden – bringt eine leere Datenbank auf den aktuellen
 *     Stand und eine bestehende auf den neuesten.
 *  2. Demo-Daten anlegen, **aber nur, wenn die Datenbank leer ist**.
 *
 * Der zweite Punkt ist der Grund für dieses Skript. Der Seed räumt vorher auf;
 * liefe er bei jedem Neustart mit, wären alle Vorgänge weg, die man beim
 * Ausprobieren angelegt hat – und ein Testsystem, das über Nacht vergisst, was
 * man ihm gezeigt hat, taugt nicht zum Vorführen.
 */

import { execFileSync } from "node:child_process";

import { PrismaClient } from "@prisma/client";

const db = new PrismaClient();

function lauf(befehl: string, argumente: string[]): void {
  execFileSync(befehl, argumente, { stdio: "inherit" });
}

async function main() {
  console.info("[Vorbereiten] Migrationen anwenden …");
  lauf("npx", ["prisma", "migrate", "deploy"]);

  const vorhandene = await db.user.count();

  if (vorhandene > 0) {
    console.info(
      `[Vorbereiten] Datenbank enthält bereits ${vorhandene} Konten – ` +
        `die Demo-Daten werden nicht erneut angelegt.`,
    );
    return;
  }

  console.info("[Vorbereiten] Leere Datenbank, lege Demo-Daten an …");
  lauf("npx", ["tsx", "prisma/seed.ts"]);
}

main()
  .catch((fehler) => {
    console.error("[Vorbereiten] Fehlgeschlagen:", fehler);
    process.exit(1);
  })
  .finally(async () => {
    await db.$disconnect();
  });
