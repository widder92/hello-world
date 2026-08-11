/**
 * Zeigt, welche Konten in der Datenbank stehen, die diese Anwendung benutzt.
 *
 * Gedacht für den Fall „die Anmeldung nimmt meine Zugangsdaten nicht an".
 * Dahinter steckt fast immer eine von zwei Ursachen, und beide sind von außen
 * nicht zu unterscheiden – die Anmeldeseite sagt aus gutem Grund nur
 * „E-Mail-Adresse oder Passwort stimmt nicht" und nicht, welches von beidem:
 *
 *   1. Die Anwendung schaut in eine andere Datenbank als die, in der die
 *      Demo-Daten liegen. Etwa, weil die .env später geändert oder von der
 *      Beispieldatei überschrieben wurde.
 *   2. Das Konto gibt es, aber es hat kein Kennwort (reines SSO-Konto) oder es
 *      ist stillgelegt.
 *
 * Dieses Skript beantwortet beides in einem Aufruf. Kennwörter gibt es nicht
 * aus – sie liegen nur als Hashwert vor und lassen sich nicht zurückrechnen.
 */

import { PrismaClient } from "@prisma/client";

const db = new PrismaClient();

/** Verbindungsangabe ohne das Kennwort, damit sie gefahrlos anzeigbar ist. */
function datenbankOhneKennwort(): string {
  const roh = process.env.DATABASE_URL ?? "(nicht gesetzt)";
  return roh.replace(/\/\/([^:]+):[^@]*@/, "//$1:***@");
}

try {
  console.info(`\nDatenbank:  ${datenbankOhneKennwort()}\n`);

  const konten = await db.user.findMany({
    orderBy: [{ globalRole: "desc" }, { email: "asc" }],
    select: {
      email: true,
      name: true,
      globalRole: true,
      isActive: true,
      passwordHash: true,
      memberships: { select: { role: true, team: { select: { name: true } } } },
    },
  });

  if (konten.length === 0) {
    console.info(
      "Es steht KEIN Konto in dieser Datenbank.\n\n" +
        "Das erklärt die abgewiesene Anmeldung. Entweder zeigt DATABASE_URL in\n" +
        "der .env auf eine andere Datenbank als beim Einrichten, oder die\n" +
        "Demo-Daten wurden nie angelegt. Anlegen lassen sie sich mit:\n\n" +
        "  npm run db:seed\n",
    );
  } else {
    console.info(`${konten.length} Konten:\n`);
    for (const k of konten) {
      const bereiche =
        k.memberships.map((m) => `${m.team.name} (${m.role})`).join(", ") ||
        "kein Bereich";
      const hinweise = [
        k.passwordHash ? null : "KEIN KENNWORT – nur über SSO anmeldbar",
        k.isActive ? null : "STILLGELEGT",
        k.globalRole === "ADMIN" ? "Administration" : null,
      ].filter(Boolean);

      console.info(`  ${k.email.padEnd(30)} ${k.name}`);
      console.info(`  ${"".padEnd(30)} ${bereiche}`);
      if (hinweise.length) {
        console.info(`  ${"".padEnd(30)} ⚠ ${hinweise.join(" · ")}`);
      }
    }
    console.info(
      `\nPasswort aller Demo-Konten: Willkommen-2026-Demo\n` +
        `Genau so, mit großem W und den beiden Bindestrichen.\n`,
    );
  }
} catch (fehler) {
  console.error(
    "\nDie Datenbank ist nicht erreichbar. Damit kann sich auch niemand\n" +
      "anmelden – die Anmeldung braucht sie, um das Konto nachzuschlagen.\n",
  );
  console.error(fehler instanceof Error ? fehler.message : fehler);
  process.exitCode = 1;
} finally {
  await db.$disconnect();
}
