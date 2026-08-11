import { QueueMode, type Prisma } from "@prisma/client";
import { db } from "@/server/db";

/**
 * Wegfindung: aus Bereich und Standort wird ein Eingang.
 *
 * Das ist der Kern der Zustellung. Wer beim Melden "Hausmeister" wählt und den
 * Standort "Wohnheim Nord" angibt, landet im Eingang "Hausmeister Wohnheim
 * Nord" – nicht bei der IT und nicht bei einem anderen Haus. Bereiche mit
 * einem gemeinsamen Eingang (etwa die IT) nehmen den Standort dagegen nur als
 * Angabe am Ticket entgegen.
 */

export class RoutingError extends Error {}

/**
 * Ermittelt den Ziel-Eingang für einen Bereich und einen Standort.
 *
 * @param teamId  Der gewählte Bereich.
 * @param siteId  Der betroffene Standort. Bei Bereichen mit Eingang je
 *                Standort zwingend erforderlich.
 */
export async function resolveQueue(
  teamId: string,
  siteId: string | null | undefined,
  tx: Prisma.TransactionClient | typeof db = db,
): Promise<{ queueId: string; teamId: string; siteId: string | null }> {
  const team = await tx.team.findUnique({
    where: { id: teamId },
    select: { id: true, name: true, queueMode: true, isActive: true },
  });

  if (!team) {
    throw new RoutingError("Der gewählte Bereich wurde nicht gefunden.");
  }
  if (!team.isActive) {
    throw new RoutingError(
      `Der Bereich "${team.name}" nimmt derzeit keine Tickets entgegen.`,
    );
  }

  if (team.queueMode === QueueMode.PER_SITE) {
    if (!siteId) {
      throw new RoutingError(
        `Für den Bereich "${team.name}" muss ein Standort gewählt werden.`,
      );
    }
    const queue = await tx.queue.findFirst({
      where: { teamId, siteId, isActive: true },
      select: { id: true, siteId: true },
    });
    if (!queue) {
      throw new RoutingError(
        `Für den Bereich "${team.name}" gibt es an diesem Standort keinen ` +
          `Eingang. Bitte einen anderen Standort wählen oder die Verwaltung ` +
          `einen Eingang anlegen lassen.`,
      );
    }
    return { queueId: queue.id, teamId, siteId: queue.siteId };
  }

  // Ein gemeinsamer Eingang für alle Standorte.
  const queue = await tx.queue.findFirst({
    where: { teamId, isActive: true },
    orderBy: { sortOrder: "asc" },
    select: { id: true },
  });
  if (!queue) {
    throw new RoutingError(
      `Der Bereich "${team.name}" hat keinen aktiven Eingang.`,
    );
  }
  // Der Standort wird am Ticket festgehalten, bestimmt hier aber nicht das Ziel.
  return { queueId: queue.id, teamId, siteId: siteId ?? null };
}

/**
 * Bereiche, denen Mitarbeitende im Portal direkt schreiben dürfen – samt der
 * Standorte, die dabei zur Auswahl stehen.
 *
 * Bereiche mit `queueMode = PER_SITE` liefern nur die Standorte, für die
 * tatsächlich ein Eingang besteht. So kann niemand einen Standort wählen, an
 * dem es gar keinen Hausmeister gibt.
 */
export async function portalTeams() {
  const teams = await db.team.findMany({
    where: { isActive: true, portalVisible: true },
    orderBy: [{ sortOrder: "asc" }, { name: "asc" }],
    select: {
      id: true,
      key: true,
      name: true,
      mode: true,
      queueMode: true,
      color: true,
      icon: true,
      portalLabel: true,
      portalDescription: true,
      queues: {
        where: { isActive: true, portalVisible: true },
        orderBy: { sortOrder: "asc" },
        select: {
          id: true,
          name: true,
          site: {
            select: { id: true, name: true, type: true, city: true },
          },
        },
      },
      categories: {
        where: { isActive: true, parentId: null },
        orderBy: [{ sortOrder: "asc" }, { name: "asc" }],
        select: {
          id: true,
          name: true,
          description: true,
          formSchema: true,
          children: {
            where: { isActive: true },
            orderBy: [{ sortOrder: "asc" }, { name: "asc" }],
            select: {
              id: true,
              name: true,
              description: true,
              formSchema: true,
            },
          },
        },
      },
    },
  });

  // Bereiche ohne nutzbaren Eingang gar nicht erst anbieten.
  return teams.filter((team) => team.queues.length > 0);
}

/**
 * Standorte, die für einen Bereich zur Auswahl stehen.
 *
 * Bei Eingang je Standort sind das genau die Standorte mit Eingang; bei einem
 * gemeinsamen Eingang alle aktiven Standorte, weil der Standort dort frei
 * angebbar ist.
 */
export async function sitesForTeam(teamId: string) {
  const team = await db.team.findUnique({
    where: { id: teamId },
    select: { queueMode: true },
  });
  if (!team) return [];

  if (team.queueMode === QueueMode.PER_SITE) {
    const queues = await db.queue.findMany({
      where: { teamId, isActive: true, siteId: { not: null } },
      orderBy: { sortOrder: "asc" },
      select: {
        site: {
          select: { id: true, name: true, type: true, city: true, sortOrder: true },
        },
      },
    });
    return queues
      .map((q) => q.site)
      .filter((s): s is NonNullable<typeof s> => s !== null);
  }

  return db.site.findMany({
    where: { isActive: true },
    orderBy: [{ sortOrder: "asc" }, { name: "asc" }],
    select: { id: true, name: true, type: true, city: true, sortOrder: true },
  });
}

/**
 * Legt die Eingänge eines Bereichs so an, dass sie zur gewählten Struktur
 * passen. Wird beim Anlegen eines Bereichs und beim Umstellen der Struktur in
 * der Verwaltung aufgerufen.
 *
 * Bestehende Eingänge bleiben erhalten – ein Eingang, in dem Tickets liegen,
 * wird nie gelöscht, sondern nur deaktiviert. Sonst gingen beim Umstellen der
 * Struktur Vorgänge verloren.
 */
export async function syncQueuesForTeam(teamId: string): Promise<void> {
  const team = await db.team.findUnique({
    where: { id: teamId },
    select: { id: true, key: true, name: true, queueMode: true },
  });
  if (!team) throw new RoutingError("Bereich nicht gefunden.");

  const existing = await db.queue.findMany({ where: { teamId } });

  if (team.queueMode === QueueMode.SINGLE) {
    const central = existing.find((q) => q.siteId === null);
    if (central) {
      await db.queue.update({
        where: { id: central.id },
        data: { isActive: true },
      });
    } else {
      await db.queue.create({
        data: { teamId, siteId: null, name: team.name, sortOrder: 0 },
      });
    }
    // Standort-Eingänge stilllegen, ohne ihre Tickets anzutasten.
    await db.queue.updateMany({
      where: { teamId, siteId: { not: null } },
      data: { isActive: false },
    });
    return;
  }

  // Je Standort ein Eingang.
  const sites = await db.site.findMany({
    where: { isActive: true },
    orderBy: [{ sortOrder: "asc" }, { name: "asc" }],
  });

  for (const [index, site] of sites.entries()) {
    const match = existing.find((q) => q.siteId === site.id);
    if (match) {
      await db.queue.update({
        where: { id: match.id },
        data: { isActive: true, sortOrder: index },
      });
    } else {
      await db.queue.create({
        data: {
          teamId,
          siteId: site.id,
          name: `${team.name} ${site.shortName ?? site.name}`,
          sortOrder: index,
        },
      });
    }
  }

  await db.queue.updateMany({
    where: { teamId, siteId: null },
    data: { isActive: false },
  });
}
