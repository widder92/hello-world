import { TeamMode, TeamRole } from "@prisma/client";
import { cache } from "react";

import { auth } from "@/server/auth";
import { capabilitiesFor, type TeamCapabilities } from "@/server/capabilities";
import { db } from "@/server/db";

/**
 * Rechteprüfung – die einzige Stelle im System, an der entschieden wird, wer
 * was sehen und tun darf.
 *
 * Bewusst zentral: Verstreute Prüfungen in einzelnen Seiten und Komponenten
 * driften auseinander, und ein vergessener Check fällt niemandem auf, weil die
 * Oberfläche den Knopf ja gar nicht anzeigt. Hier gilt umgekehrt: Jede
 * Server-Aktion und jede Seite holt sich ihren Zugriff über die Funktionen in
 * dieser Datei, und das Ausblenden im UI ist nur Bequemlichkeit, nie der
 * Schutz.
 *
 * Die Stufen:
 *
 *   ohne Mitgliedschaft   eigene Tickets und die, bei denen man auf CC steht
 *   MEMBER               zusätzlich die freigegebenen Eingänge des Bereichs
 *   DISPATCHER           wie MEMBER, darf zusätzlich anderen zuweisen
 *   LEAD                 alle Eingänge des Bereichs, Einstellungen, Auswertung
 *   ADMIN                alles
 */

export class AccessDenied extends Error {
  constructor(message = "Für diesen Vorgang fehlt die Berechtigung.") {
    super(message);
    this.name = "AccessDenied";
  }
}

export class NotAuthenticated extends Error {
  constructor(message = "Bitte melden Sie sich an.") {
    super(message);
    this.name = "NotAuthenticated";
  }
}

export interface MembershipInfo {
  teamId: string;
  teamKey: string;
  teamName: string;
  teamMode: TeamMode;
  role: TeamRole;
  capabilities: TeamCapabilities;
  /** Ob dieser Bereich ein Inventar führt (Team.hasInventory). */
  teamHasInventory: boolean;
}

export interface QueueInfo {
  id: string;
  name: string;
  teamId: string;
  siteId: string | null;
  siteName: string | null;
  isDefault: boolean;
}

export interface Actor {
  id: string;
  name: string;
  email: string;
  isAdmin: boolean;
  siteId: string | null;
  a11yPrefs: Record<string, boolean>;
  theme: string;
  memberships: MembershipInfo[];
  /** Alle Eingänge, in denen diese Person arbeiten darf. */
  queues: QueueInfo[];
  /** Startansicht nach dem Anmelden. */
  defaultQueueId: string | null;
}

/**
 * Lädt die angemeldete Person samt Mitgliedschaften und Eingangs-Freigaben.
 *
 * `cache` bündelt die Abfrage innerhalb eines Seitenaufrufs: Layout, Seite und
 * mehrere Komponenten dürfen die Funktion beliebig oft aufrufen, es entsteht
 * trotzdem nur eine Datenbankabfrage.
 *
 * Die Rechte kommen absichtlich aus der Datenbank und nicht aus dem
 * Sitzungstoken – sonst würde eine entzogene Mitgliedschaft erst gelten, wenn
 * das Token abläuft.
 */
export const getActor = cache(async (): Promise<Actor | null> => {
  const session = await auth();
  if (!session?.user?.id) return null;

  const user = await db.user.findUnique({
    where: { id: session.user.id },
    select: {
      id: true,
      name: true,
      email: true,
      globalRole: true,
      siteId: true,
      isActive: true,
      a11yPrefs: true,
      theme: true,
      memberships: {
        select: {
          role: true,
          team: {
            select: {
              id: true,
              key: true,
              name: true,
              mode: true,
              isActive: true,
              hasInventory: true,
            },
          },
        },
      },
      queueAccess: {
        select: {
          isDefault: true,
          queue: {
            select: {
              id: true,
              name: true,
              teamId: true,
              siteId: true,
              isActive: true,
              site: { select: { name: true } },
            },
          },
        },
      },
    },
  });

  if (!user || !user.isActive) return null;

  const isAdmin = user.globalRole === "ADMIN";

  const memberships: MembershipInfo[] = user.memberships
    .filter((m) => m.team.isActive)
    .map((m) => ({
      teamId: m.team.id,
      teamKey: m.team.key,
      teamName: m.team.name,
      teamMode: m.team.mode,
      role: m.role,
      capabilities: capabilitiesFor(m.team.mode),
      teamHasInventory: m.team.hasInventory,
    }));

  // Bereichsleitungen sehen alle Eingänge ihres Bereichs, auch ohne
  // ausdrückliche Freigabe – sonst müsste bei jedem neuen Standort daran
  // gedacht werden, die Leitung freizuschalten.
  const leadTeamIds = memberships
    .filter((m) => m.role === TeamRole.LEAD)
    .map((m) => m.teamId);

  const explicitQueues: QueueInfo[] = user.queueAccess
    .filter((a) => a.queue.isActive)
    .map((a) => ({
      id: a.queue.id,
      name: a.queue.name,
      teamId: a.queue.teamId,
      siteId: a.queue.siteId,
      siteName: a.queue.site?.name ?? null,
      isDefault: a.isDefault,
    }));

  const queueMap = new Map(explicitQueues.map((q) => [q.id, q]));

  if (leadTeamIds.length > 0 || isAdmin) {
    const extra = await db.queue.findMany({
      where: isAdmin
        ? { isActive: true }
        : { teamId: { in: leadTeamIds }, isActive: true },
      select: {
        id: true,
        name: true,
        teamId: true,
        siteId: true,
        site: { select: { name: true } },
      },
      orderBy: { sortOrder: "asc" },
    });
    for (const q of extra) {
      if (!queueMap.has(q.id)) {
        queueMap.set(q.id, {
          id: q.id,
          name: q.name,
          teamId: q.teamId,
          siteId: q.siteId,
          siteName: q.site?.name ?? null,
          isDefault: false,
        });
      }
    }
  }

  const queues = [...queueMap.values()].sort((a, b) =>
    a.name.localeCompare(b.name, "de"),
  );

  const defaultQueue =
    queues.find((q) => q.isDefault) ??
    // Ohne ausdrückliche Startansicht den Eingang am eigenen Standort wählen.
    queues.find((q) => q.siteId && q.siteId === user.siteId) ??
    queues[0] ??
    null;

  return {
    id: user.id,
    name: user.name,
    email: user.email,
    isAdmin,
    siteId: user.siteId,
    a11yPrefs: (user.a11yPrefs as Record<string, boolean>) ?? {},
    theme: user.theme,
    memberships,
    queues,
    defaultQueueId: defaultQueue?.id ?? null,
  };
});

/** Wie `getActor`, wirft aber, wenn niemand angemeldet ist. */
export async function requireActor(): Promise<Actor> {
  const actor = await getActor();
  if (!actor) throw new NotAuthenticated();
  return actor;
}

export async function requireAdmin(): Promise<Actor> {
  const actor = await requireActor();
  if (!actor.isAdmin) {
    throw new AccessDenied("Dieser Bereich ist der Administration vorbehalten.");
  }
  return actor;
}

// ---------------------------------------------------------------------------
// Bereiche und Eingänge
// ---------------------------------------------------------------------------

/** Arbeitet diese Person in mindestens einem Bereich? */
export function isAgent(actor: Actor): boolean {
  return actor.memberships.length > 0 || actor.isAdmin;
}

export function membershipFor(
  actor: Actor,
  teamId: string,
): MembershipInfo | null {
  return actor.memberships.find((m) => m.teamId === teamId) ?? null;
}

/** Darf diese Person in diesem Eingang arbeiten? */
export function canAccessQueue(actor: Actor, queueId: string): boolean {
  if (actor.isAdmin) return true;
  return actor.queues.some((q) => q.id === queueId);
}

export function requireQueueAccess(actor: Actor, queueId: string): QueueInfo {
  const queue = actor.queues.find((q) => q.id === queueId);
  if (!queue && !actor.isAdmin) {
    throw new AccessDenied(
      "Dieser Eingang ist für Sie nicht freigegeben. Wenden Sie sich an Ihre " +
        "Bereichsleitung, wenn Sie hier arbeiten sollen.",
    );
  }
  if (queue) return queue;

  throw new AccessDenied("Eingang nicht gefunden.");
}

/** Alle Eingänge eines Bereichs, die diese Person sehen darf. */
export function queuesOfTeam(actor: Actor, teamId: string): QueueInfo[] {
  return actor.queues.filter((q) => q.teamId === teamId);
}

/** Darf diese Person die Einstellungen des Bereichs ändern? */
export function canManageTeam(actor: Actor, teamId: string): boolean {
  if (actor.isAdmin) return true;
  return membershipFor(actor, teamId)?.role === TeamRole.LEAD;
}

/** Darf diese Person Tickets an andere Personen zuweisen? */
export function canAssignToOthers(actor: Actor, teamId: string): boolean {
  if (actor.isAdmin) return true;
  const membership = membershipFor(actor, teamId);
  if (!membership) return false;
  if (!membership.capabilities.zuweisung) return false;
  return (
    membership.role === TeamRole.DISPATCHER || membership.role === TeamRole.LEAD
  );
}

/** Darf diese Person die Auswertung des Bereichs sehen? */
export function canViewStatistics(actor: Actor, teamId: string): boolean {
  if (actor.isAdmin) return true;
  const membership = membershipFor(actor, teamId);
  if (!membership?.capabilities.statistik) return false;
  return membership.role === TeamRole.LEAD;
}

// ---------------------------------------------------------------------------
// Inventar
// ---------------------------------------------------------------------------

export type InventoryLevel =
  /** Darf Geräte anlegen, ändern, herausgeben – und die Stammdatenlisten pflegen. */
  | "manage"
  /** Darf das Inventar sehen und Geräte pflegen, aber keine Stammdatenlisten. */
  | "read"
  /** Kein Zugang zum Inventar. Sieht höchstens die eigenen Geräte. */
  | "none";

/**
 * Zugang zum Inventar.
 *
 * Anders als bei den Vorgängen hängt er nicht an einzelnen Eingängen, sondern
 * am Bereich: Wer in einem Bereich arbeitet, der ein Inventar führt, sieht
 * dessen Geräte vollständig. Eine standortweise Trennung wäre hier falsch –
 * ein Notebook wandert zwischen Häusern, und wer es sucht, muss es finden.
 *
 * Die eigenen Geräte sieht jede Person unabhängig davon; dafür ist diese
 * Funktion nicht zuständig (siehe /meine-geraete).
 */
export function inventoryAccess(actor: Actor): InventoryLevel {
  if (actor.isAdmin) return "manage";

  const inventarBereiche = actor.memberships.filter((m) => m.teamHasInventory);
  if (inventarBereiche.length === 0) return "none";

  // Gerätearten und Kostenstellen wirken auf alle Geräte gleichzeitig und
  // hängen an Buchhaltung und Nummernkreisen – ihr Pflegen bleibt bei der
  // Bereichsleitung.
  //
  // Hersteller und Modelle sind ausdrücklich **nicht** davon erfasst; sie
  // entstehen beim Erfassen eines Geräts (siehe `canCreateInventoryLookups`).
  return inventarBereiche.some((m) => m.role === TeamRole.LEAD)
    ? "manage"
    : "read";
}

/**
 * Darf diese Person Hersteller und Gerätemodelle anlegen?
 *
 * Jede, die Geräte erfasst. Das ist bewusst großzügiger als bei den übrigen
 * Stammdaten: Wer ein neu geliefertes Notebook einbuchen will, dessen
 * Hersteller noch niemand eingetragen hat, käme sonst nicht weiter und müsste
 * auf die Bereichsleitung warten – für einen Listeneintrag, der ohnehin nur
 * ein Name ist.
 *
 * Der Schaden wäre auch gering: Ein überflüssiger Hersteller lässt sich
 * stilllegen. Eine falsche Kostenstelle dagegen verfälscht Summen.
 */
export function canCreateInventoryLookups(actor: Actor): boolean {
  return inventoryAccess(actor) !== "none";
}

/** Bereiche, deren Inventar diese Person führen darf. */
export function inventoryTeamIds(actor: Actor): string[] {
  return actor.memberships.filter((m) => m.teamHasInventory).map((m) => m.teamId);
}

export function requireInventory(
  actor: Actor,
  mindestens: Exclude<InventoryLevel, "none"> = "read",
): InventoryLevel {
  const stufe = inventoryAccess(actor);
  if (stufe === "none") {
    throw new AccessDenied(
      "Das Inventar steht nur Bereichen offen, die eines führen.",
    );
  }
  if (mindestens === "manage" && stufe !== "manage") {
    throw new AccessDenied(
      "Stammdaten des Inventars darf nur die Bereichsleitung ändern.",
    );
  }
  return stufe;
}

// ---------------------------------------------------------------------------
// Tickets
// ---------------------------------------------------------------------------

export interface TicketAccessSubject {
  id: string;
  queueId: string;
  teamId: string;
  requesterId: string;
  watchers?: { userId: string }[];
}

export type TicketAccessLevel =
  /** Darf den Vorgang nur als Melder oder Beobachter lesen und antworten. */
  | "requester"
  /** Darf den Vorgang bearbeiten. */
  | "agent"
  /** Kein Zugriff. */
  | "none";

/**
 * Welchen Zugriff hat diese Person auf diesen Vorgang?
 *
 * Der Bearbeitungszugriff verlangt beides: Mitgliedschaft im Bereich **und**
 * Freigabe für den Eingang. Ein Hausmeister aus Musterstadt kommt so nicht an
 * die Vorgänge der Schule, obwohl beide im selben Bereich arbeiten.
 */
export function ticketAccessLevel(
  actor: Actor,
  ticket: TicketAccessSubject,
): TicketAccessLevel {
  if (actor.isAdmin) return "agent";

  const isMember = actor.memberships.some((m) => m.teamId === ticket.teamId);
  if (isMember && canAccessQueue(actor, ticket.queueId)) return "agent";

  if (ticket.requesterId === actor.id) return "requester";
  if (ticket.watchers?.some((w) => w.userId === actor.id)) return "requester";

  return "none";
}

export function canViewTicket(
  actor: Actor,
  ticket: TicketAccessSubject,
): boolean {
  return ticketAccessLevel(actor, ticket) !== "none";
}

export function canWorkTicket(
  actor: Actor,
  ticket: TicketAccessSubject,
): boolean {
  return ticketAccessLevel(actor, ticket) === "agent";
}

export function requireTicketView(
  actor: Actor,
  ticket: TicketAccessSubject,
): TicketAccessLevel {
  const level = ticketAccessLevel(actor, ticket);
  if (level === "none") {
    throw new AccessDenied(
      "Dieser Vorgang gehört nicht zu Ihren Tickets und liegt in keinem " +
        "Eingang, für den Sie freigegeben sind.",
    );
  }
  return level;
}

export function requireTicketWork(
  actor: Actor,
  ticket: TicketAccessSubject,
): void {
  if (!canWorkTicket(actor, ticket)) {
    throw new AccessDenied(
      "Diesen Vorgang darf nur der zuständige Bereich bearbeiten.",
    );
  }
}

/**
 * Filter für Ticketlisten. Liefert eine Prisma-Bedingung, die genau die
 * Vorgänge einschließt, die diese Person sehen darf.
 *
 * Wird von jeder Liste und jeder Suche verwendet – so kann keine Abfrage
 * versehentlich mehr zurückgeben, als erlaubt ist.
 */
export function ticketVisibilityFilter(actor: Actor) {
  if (actor.isAdmin) return {};

  const queueIds = actor.queues.map((q) => q.id);
  const teamIds = actor.memberships.map((m) => m.teamId);

  const conditions: object[] = [
    { requesterId: actor.id },
    { watchers: { some: { userId: actor.id } } },
  ];

  if (queueIds.length > 0 && teamIds.length > 0) {
    conditions.push({
      AND: [{ queueId: { in: queueIds } }, { teamId: { in: teamIds } }],
    });
  }

  return { OR: conditions };
}

/** Filter für die Arbeitsansichten: nur Vorgänge, die bearbeitet werden dürfen. */
export function agentTicketFilter(actor: Actor, queueIds?: string[]) {
  const allowed = actor.queues.map((q) => q.id);
  const selected = queueIds?.filter((id) => allowed.includes(id)) ?? allowed;

  if (actor.isAdmin && !queueIds) return {};

  if (selected.length === 0) {
    // Keine Freigabe: bewusst eine Bedingung, die nichts trifft, statt eines
    // leeren Filters – ein leerer Filter würde alles zurückgeben.
    return { id: { in: [] } };
  }

  return { queueId: { in: selected } };
}
