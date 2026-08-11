/**
 * Demo-Daten anlegen – aber nur in eine leere Datenbank.
 *
 * Läuft als letzter Schritt von `npm run vorbereiten`, nachdem npm bereits
 * `prisma generate` und `prisma migrate deploy` ausgeführt hat.
 *
 * Warum die beiden Schritte in der package.json stehen und nicht hier: Ein
 * Skript, das `npx` als Programm startet, läuft unter Windows nicht. Dort ist
 * `npx` eine Batchdatei, und die lässt sich nicht wie ein Programm aufrufen –
 * der Lauf bricht mit `spawnSync npx ENOENT` ab. npm selbst kennt diesen
 * Unterschied und löst ihn auf jedem Betriebssystem richtig auf. Also überlässt
 * man ihm das und startet hier gar nichts mehr.
 *
 * Bleibt die eigentliche Entscheidung: Der Seed räumt vorher auf. Liefe er bei
 * jedem Neustart mit, wären alle Vorgänge weg, die man beim Ausprobieren
 * angelegt hat – und ein Testsystem, das über Nacht vergisst, was man ihm
 * gezeigt hat, taugt nicht zum Vorführen.
 */

import { PrismaClient } from "@prisma/client";

const db = new PrismaClient();

try {
  const vorhandene = await db.user.count();

  if (vorhandene > 0) {
    console.info(
      `[Vorbereiten] Datenbank enthält bereits ${vorhandene} Konten – ` +
        `die Demo-Daten werden nicht erneut angelegt.`,
    );
  } else {
    console.info("[Vorbereiten] Leere Datenbank, lege Demo-Daten an …");
    // Der Seed führt sich beim Laden selbst aus. Der Pfad steht bewusst in
    // einer Variablen: Ein wörtliches "../prisma/seed.ts" lehnt TypeScript ab,
    // weil die Endung .ts im Import normalerweise nicht erlaubt ist – tsx
    // löst sie zur Laufzeit aber genau so auf.
    const seedPfad = "../prisma/seed.ts";
    await import(seedPfad);
  }
} catch (fehler) {
  console.error("[Vorbereiten] Fehlgeschlagen:", fehler);
  process.exitCode = 1;
} finally {
  await db.$disconnect();
}
