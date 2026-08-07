import {
  Priority,
  QueueMode,
  TeamMode,
  TeamRole,
  TicketStatus,
} from "@prisma/client";

import { db } from "@/server/db";

/**
 * Gemeinsame Vorbereitung für die Tests gegen eine echte Datenbank.
 *
 * Manche Dinge lassen sich nicht sinnvoll nachbauen: Die Nummernvergabe lebt
 * von einer Zeilensperre, die Zuordnung von Antwortmails von Unique-Indizes,
 * das Abspalten von einer Transaktion über acht Tabellen. Ein nachgebauter
 * Datenbankzugriff würde hier genau das prüfen, was man selbst hineingeschrieben
 * hat. Diese Tests laufen deshalb gegen PostgreSQL.
 *
 * Einrichten:
 *
 *   createdb ticketsystem_test
 *   DATABASE_URL="postgresql://…/ticketsystem_test" npx prisma migrate deploy
 */

/**
 * Sicherung gegen den teuersten Unfall.
 *
 * `leereDatenbank()` löscht alle Zeilen. Zeigt `DATABASE_URL` versehentlich auf
 * die Entwicklungs- oder gar die Produktivdatenbank, ist das nicht rückgängig
 * zu machen. Deshalb muss der Datenbankname auf `_test` enden – sonst laufen
 * diese Tests gar nicht erst an.
 */
export function pruefeTestdatenbank(): void {
  const url = process.env.DATABASE_URL ?? "";
  const name = url.split("/").pop()?.split("?")[0] ?? "";

  if (!name.endsWith("_test")) {
    throw new Error(
      `Die Tests unter tests/db löschen alle Zeilen und laufen deshalb nur ` +
        `gegen eine Datenbank, deren Name auf "_test" endet. ` +
        `Angegeben ist "${name || "(nichts)"}". ` +
        `Setzen Sie TEST_DATABASE_URL entsprechend.`,
    );
  }
}

/** Steht die Testdatenbank überhaupt zur Verfügung? */
export async function datenbankErreichbar(): Promise<boolean> {
  try {
    pruefeTestdatenbank();
    await db.$queryRaw`SELECT 1`;
    return true;
  } catch {
    return false;
  }
}

/**
 * Räumt alle Tabellen leer.
 *
 * `TRUNCATE … CASCADE` in einem Befehl, damit die Reihenfolge der
 * Fremdschlüssel keine Rolle spielt. Die Migrationstabelle bleibt stehen –
 * sonst hielte Prisma das Schema für nicht eingerichtet.
 */
export async function leereDatenbank(): Promise<void> {
  pruefeTestdatenbank();

  const tabellen = await db.$queryRaw<{ tablename: string }[]>`
    SELECT tablename FROM pg_tables
    WHERE schemaname = 'public' AND tablename <> '_prisma_migrations'
  `;

  if (tabellen.length === 0) return;

  const liste = tabellen.map((t) => `"public"."${t.tablename}"`).join(", ");
  await db.$executeRawUnsafe(`TRUNCATE TABLE ${liste} CASCADE`);
}

// ---------------------------------------------------------------------------
// Bausteine für die Testdaten
// ---------------------------------------------------------------------------

let zaehler = 0;
/** Eindeutiger Namensteil, damit Unique-Indizes zwischen Tests nicht kollidieren. */
function eindeutig(praefix: string): string {
  zaehler += 1;
  return `${praefix}-${zaehler}`;
}

export async function standortAnlegen(name?: string) {
  const bezeichnung = name ?? eindeutig("Standort");
  return db.site.create({
    data: { name: bezeichnung, shortName: bezeichnung, city: "Musterstadt" },
  });
}

export async function bereichAnlegen(opts: {
  key?: string;
  name?: string;
  mode?: TeamMode;
  queueMode?: QueueMode;
  isActive?: boolean;
  allowEmailCommands?: boolean;
}) {
  const key = opts.key ?? eindeutig("T").toUpperCase().replace("-", "");
  return db.team.create({
    data: {
      key,
      name: opts.name ?? key,
      mode: opts.mode ?? TeamMode.VOLL,
      queueMode: opts.queueMode ?? QueueMode.SINGLE,
      isActive: opts.isActive ?? true,
      allowEmailCommands: opts.allowEmailCommands ?? true,
    },
  });
}

export async function eingangAnlegen(opts: {
  teamId: string;
  siteId?: string | null;
  name?: string;
  isActive?: boolean;
  sortOrder?: number;
}) {
  return db.queue.create({
    data: {
      teamId: opts.teamId,
      siteId: opts.siteId ?? null,
      name: opts.name ?? eindeutig("Eingang"),
      isActive: opts.isActive ?? true,
      sortOrder: opts.sortOrder ?? 0,
    },
  });
}

export async function personAnlegen(opts: {
  name?: string;
  email?: string;
  siteId?: string | null;
  isActive?: boolean;
} = {}) {
  const kennung = eindeutig("person");
  return db.user.create({
    data: {
      name: opts.name ?? kennung,
      email: opts.email ?? `${kennung}@example.org`,
      siteId: opts.siteId ?? null,
      isActive: opts.isActive ?? true,
    },
  });
}

export async function mitgliedschaftAnlegen(
  userId: string,
  teamId: string,
  role: TeamRole = TeamRole.MEMBER,
) {
  return db.teamMembership.create({ data: { userId, teamId, role } });
}

export async function freigabeAnlegen(userId: string, queueId: string) {
  return db.queueAccess.create({ data: { userId, queueId } });
}

export async function ticketAnlegenRoh(opts: {
  number: string;
  subject?: string;
  queueId: string;
  teamId: string;
  siteId?: string | null;
  requesterId: string;
  assigneeId?: string | null;
  status?: TicketStatus;
  priority?: Priority;
  originEmailMessageId?: string | null;
}) {
  return db.ticket.create({
    data: {
      number: opts.number,
      subject: opts.subject ?? "Testvorgang",
      queueId: opts.queueId,
      teamId: opts.teamId,
      siteId: opts.siteId ?? null,
      requesterId: opts.requesterId,
      assigneeId: opts.assigneeId ?? null,
      status: opts.status ?? TicketStatus.NEU,
      priority: opts.priority ?? Priority.NORMAL,
      originEmailMessageId: opts.originEmailMessageId ?? null,
    },
  });
}

export { db };
