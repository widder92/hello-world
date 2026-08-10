import { TeamMode, TeamRole } from "@prisma/client";

import { db } from "@/server/db";
import { inventoryAccess, type Actor } from "@/server/policy";
import { STATUS_OFFEN } from "@/lib/domain";

/**
 * Baut die Navigation für den Arbeitsplatz.
 *
 * Die Struktur folgt der Ausbaustufe des Bereichs, und zwar sichtbar:
 *
 *   VOLL     Sammeleingang (was noch niemand übernommen hat) und daneben die
 *            vollständige Ticketliste des Eingangs.
 *   EINFACH  nur "Unsere Tickets" – kein Verteilerboard, keine Ticketliste mit
 *            Filterleiste, weil es dort nichts zu verteilen gibt.
 *
 * Die Zähler werden hier gleich mitgeladen: Ohne sie müsste man jeden Eingang
 * einzeln anklicken, um zu sehen, wo etwas liegt.
 */

export interface NavQueue {
  id: string;
  name: string;
  siteName: string | null;
  /** Offene Vorgänge ohne Zuweisung – das, was auf Übernahme wartet. */
  unassignedCount: number;
  /** Alle offenen Vorgänge im Eingang. */
  openCount: number;
}

export interface NavTeam {
  id: string;
  key: string;
  name: string;
  mode: TeamMode;
  color: string;
  icon: string;
  role: TeamRole;
  isLead: boolean;
  queues: NavQueue[];
  /** Summe über alle sichtbaren Eingänge des Bereichs. */
  openCount: number;
  unassignedCount: number;
}

export interface Navigation {
  teams: NavTeam[];
  /** Mir zugewiesene offene Vorgänge. */
  myOpenCount: number;
  /** Ungelesene Benachrichtigungen. */
  unreadNotifications: number;
  /** Darf mindestens eine Auswertung sehen. */
  hasStatistics: boolean;
  /** Darf das Inventar sehen. */
  hasInventory: boolean;
  /** Wie viele Geräte mir selbst zugewiesen sind. */
  meineGeraeteCount: number;
}

export async function buildNavigation(actor: Actor): Promise<Navigation> {
  const queueIds = actor.queues.map((q) => q.id);

  // Zähler in zwei Gruppierungen statt in einer Schleife pro Eingang – bei
  // einem Träger mit vielen Standorten wären das sonst schnell 20 Abfragen
  // bei jedem Seitenaufruf.
  const [offenProEingang, ohneZuweisungProEingang, meineOffenen, ungelesen] =
    await Promise.all([
      queueIds.length
        ? db.ticket.groupBy({
            by: ["queueId"],
            where: { queueId: { in: queueIds }, status: { in: STATUS_OFFEN } },
            _count: { _all: true },
          })
        : Promise.resolve([]),
      queueIds.length
        ? db.ticket.groupBy({
            by: ["queueId"],
            where: {
              queueId: { in: queueIds },
              status: { in: STATUS_OFFEN },
              assigneeId: null,
            },
            _count: { _all: true },
          })
        : Promise.resolve([]),
      db.ticket.count({
        where: { assigneeId: actor.id, status: { in: STATUS_OFFEN } },
      }),
      db.notification.count({ where: { userId: actor.id, readAt: null } }),
    ]);

  // Der Menüpunkt "Meine Geräte" erscheint nur, wenn dort auch etwas steht –
  // ein leerer Punkt beantwortet keine Frage und kostet nur einen Klick.
  const meineGeraeteCount = await db.asset.count({
    where: { assignedToId: actor.id },
  });

  const offenMap = new Map(offenProEingang.map((r) => [r.queueId, r._count._all]));
  const ohneMap = new Map(
    ohneZuweisungProEingang.map((r) => [r.queueId, r._count._all]),
  );

  // Bereichsdaten für alle Bereiche, in denen sichtbare Eingänge liegen.
  const teamIds = [...new Set(actor.queues.map((q) => q.teamId))];
  const teams = teamIds.length
    ? await db.team.findMany({
        where: { id: { in: teamIds } },
        select: {
          id: true,
          key: true,
          name: true,
          mode: true,
          color: true,
          icon: true,
          sortOrder: true,
        },
        orderBy: [{ sortOrder: "asc" }, { name: "asc" }],
      })
    : [];

  const navTeams: NavTeam[] = teams.map((team) => {
    const membership = actor.memberships.find((m) => m.teamId === team.id);
    const queues: NavQueue[] = actor.queues
      .filter((q) => q.teamId === team.id)
      .map((q) => ({
        id: q.id,
        name: q.name,
        siteName: q.siteName,
        openCount: offenMap.get(q.id) ?? 0,
        unassignedCount: ohneMap.get(q.id) ?? 0,
      }))
      .sort((a, b) => a.name.localeCompare(b.name, "de"));

    return {
      id: team.id,
      key: team.key,
      name: team.name,
      mode: team.mode,
      color: team.color,
      icon: team.icon,
      role: membership?.role ?? TeamRole.MEMBER,
      isLead: membership?.role === TeamRole.LEAD || actor.isAdmin,
      queues,
      openCount: queues.reduce((sum, q) => sum + q.openCount, 0),
      unassignedCount: queues.reduce((sum, q) => sum + q.unassignedCount, 0),
    };
  });

  return {
    teams: navTeams,
    myOpenCount: meineOffenen,
    unreadNotifications: ungelesen,
    hasStatistics:
      actor.isAdmin ||
      navTeams.some((t) => t.mode === TeamMode.VOLL && t.isLead),
    hasInventory: inventoryAccess(actor) !== "none",
    meineGeraeteCount,
  };
}
