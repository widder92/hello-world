import { Prisma, TicketStatus } from "@prisma/client";

import { db } from "@/server/db";
import type { Actor } from "@/server/policy";
import { STATUS_ERLEDIGT, STATUS_OFFEN } from "@/lib/domain";

/**
 * Gemeinsame Abfragen für Ticketlisten.
 *
 * Eine einzige Auswahl von Feldern für alle Listen, damit die Anzeige überall
 * dieselben Angaben zur Verfügung hat und nicht jede Seite ihre eigene Abfrage
 * mit leicht abweichendem Umfang mitbringt.
 */

export const ticketListSelect = {
  id: true,
  number: true,
  subject: true,
  status: true,
  priority: true,
  createdAt: true,
  lastActivityAt: true,
  resolutionDue: true,
  firstResponseDue: true,
  firstResponseAt: true,
  resolvedAt: true,
  slaBreached: true,
  roomOrArea: true,
  source: true,
  waitingForChild: true,
  quarantined: true,
  queue: { select: { id: true, name: true, siteId: true } },
  team: { select: { id: true, key: true, name: true, color: true, mode: true } },
  category: { select: { id: true, name: true } },
  site: { select: { id: true, name: true, shortName: true } },
  requester: { select: { id: true, name: true, email: true } },
  assignee: { select: { id: true, name: true } },
  slaPolicy: { select: { warningThreshold: true } },
  tags: { select: { tag: { select: { id: true, name: true, color: true } } } },
  _count: { select: { messages: true, attachments: true, linksFrom: true } },
} satisfies Prisma.TicketSelect;

export type TicketListItem = Prisma.TicketGetPayload<{
  select: typeof ticketListSelect;
}>;

export type StatusFilter = "offen" | "erledigt" | "alle";

export interface TicketFilter {
  queueIds?: string[];
  teamIds?: string[];
  status?: TicketStatus[];
  statusGruppe?: StatusFilter;
  priority?: string[];
  assigneeId?: string | "none" | "me";
  requesterId?: string;
  siteId?: string;
  categoryId?: string;
  tagIds?: string[];
  /** Nur Vorgänge, deren Frist überschritten ist. */
  nurUeberfaellig?: boolean;
  suche?: string;
}

export type TicketSort =
  | "neueste"
  | "aelteste"
  | "aktivitaet"
  | "prioritaet"
  | "frist";

/** Übersetzt die Filter in eine Prisma-Bedingung. */
export function buildTicketWhere(
  actor: Actor,
  filter: TicketFilter,
): Prisma.TicketWhereInput {
  const where: Prisma.TicketWhereInput = {};
  const and: Prisma.TicketWhereInput[] = [];

  if (filter.queueIds?.length) where.queueId = { in: filter.queueIds };
  if (filter.teamIds?.length) where.teamId = { in: filter.teamIds };

  if (filter.status?.length) {
    where.status = { in: filter.status };
  } else if (filter.statusGruppe === "offen") {
    where.status = { in: STATUS_OFFEN };
  } else if (filter.statusGruppe === "erledigt") {
    where.status = { in: STATUS_ERLEDIGT };
  }

  if (filter.priority?.length) {
    where.priority = { in: filter.priority as Prisma.EnumPriorityFilter["in"] };
  }

  if (filter.assigneeId === "none") {
    where.assigneeId = null;
  } else if (filter.assigneeId === "me") {
    where.assigneeId = actor.id;
  } else if (filter.assigneeId) {
    where.assigneeId = filter.assigneeId;
  }

  if (filter.requesterId) where.requesterId = filter.requesterId;
  if (filter.siteId) where.siteId = filter.siteId;
  if (filter.categoryId) where.categoryId = filter.categoryId;

  if (filter.tagIds?.length) {
    and.push({ tags: { some: { tagId: { in: filter.tagIds } } } });
  }

  if (filter.nurUeberfaellig) {
    and.push({
      status: { notIn: STATUS_ERLEDIGT },
      resolutionDue: { lt: new Date() },
    });
  }

  if (filter.suche?.trim()) {
    const begriff = filter.suche.trim();
    and.push({
      OR: [
        { number: { contains: begriff, mode: "insensitive" } },
        { subject: { contains: begriff, mode: "insensitive" } },
        { roomOrArea: { contains: begriff, mode: "insensitive" } },
        { requester: { name: { contains: begriff, mode: "insensitive" } } },
      ],
    });
  }

  if (and.length) where.AND = and;
  return where;
}

export function buildTicketOrderBy(
  sort: TicketSort = "aktivitaet",
): Prisma.TicketOrderByWithRelationInput[] {
  switch (sort) {
    case "neueste":
      return [{ createdAt: "desc" }];
    case "aelteste":
      return [{ createdAt: "asc" }];
    case "prioritaet":
      // Prisma sortiert Aufzählungen in Definitionsreihenfolge; DRINGEND steht
      // hinten, deshalb absteigend.
      return [{ priority: "desc" }, { createdAt: "asc" }];
    case "frist":
      // Vorgänge ohne Frist ans Ende, nicht an den Anfang.
      return [{ resolutionDue: { sort: "asc", nulls: "last" } }];
    case "aktivitaet":
    default:
      return [{ lastActivityAt: "desc" }];
  }
}

/**
 * Lädt eine Seite Tickets samt Gesamtzahl.
 *
 * Der Sichtbarkeitsfilter wird immer zusätzlich angewendet – auch wenn der
 * Aufrufer schon nach Eingängen filtert. So kann eine vergessene Prüfung im
 * Aufrufer nicht dazu führen, dass jemand fremde Vorgänge sieht.
 */
export async function findTickets(opts: {
  actor: Actor;
  visibility: Prisma.TicketWhereInput;
  filter: TicketFilter;
  sort?: TicketSort;
  page?: number;
  perPage?: number;
}) {
  const { actor, visibility, filter } = opts;
  const perPage = Math.min(opts.perPage ?? 50, 200);
  const page = Math.max(opts.page ?? 1, 1);

  const where: Prisma.TicketWhereInput = {
    AND: [visibility, buildTicketWhere(actor, filter)],
  };

  const [items, total] = await Promise.all([
    db.ticket.findMany({
      where,
      select: ticketListSelect,
      orderBy: buildTicketOrderBy(opts.sort),
      skip: (page - 1) * perPage,
      take: perPage,
    }),
    db.ticket.count({ where }),
  ]);

  return {
    items,
    total,
    page,
    perPage,
    pageCount: Math.max(1, Math.ceil(total / perPage)),
  };
}

/** Zählt Vorgänge je Status – für die Reiter über einer Liste. */
export async function countByStatusGroup(
  actor: Actor,
  visibility: Prisma.TicketWhereInput,
  filter: TicketFilter,
) {
  const basis: Prisma.TicketWhereInput = {
    AND: [visibility, buildTicketWhere(actor, { ...filter, statusGruppe: undefined, status: undefined })],
  };

  const [offen, erledigt] = await Promise.all([
    db.ticket.count({ where: { AND: [basis, { status: { in: STATUS_OFFEN } }] } }),
    db.ticket.count({
      where: { AND: [basis, { status: { in: STATUS_ERLEDIGT } }] },
    }),
  ]);

  return { offen, erledigt, alle: offen + erledigt };
}
