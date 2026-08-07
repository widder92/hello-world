import { EventType, type Prisma } from "@prisma/client";

import { db } from "@/server/db";

/**
 * Protokolleinträge.
 *
 * Jede Zustandsänderung eines Vorgangs hinterlässt einen Eintrag. Das ist
 * nicht nur Bequemlichkeit für die Bearbeitung, sondern auch die Grundlage
 * dafür, im Zweifel nachweisen zu können, wer wann was veranlasst hat – bei
 * Vorgängen, die die Sicherheit von Bewohnerinnen und Bewohnern berühren
 * (Handlauf, Automatiktür, Brandschutz) ist das kein Beiwerk.
 *
 * Der Name der handelnden Person wird mitgeschrieben und nicht nur verknüpft:
 * Wird ein Konto später gelöscht, bleibt das Protokoll trotzdem lesbar.
 */
export async function logEvent(
  tx: Prisma.TransactionClient | typeof db,
  input: {
    ticketId: string;
    type: EventType;
    actorId?: string | null;
    actorName?: string | null;
    fromValue?: string | null;
    toValue?: string | null;
    note?: string | null;
    meta?: Prisma.InputJsonValue;
  },
) {
  return tx.ticketEvent.create({
    data: {
      ticketId: input.ticketId,
      type: input.type,
      actorId: input.actorId ?? null,
      actorName: input.actorName ?? null,
      fromValue: input.fromValue ?? null,
      toValue: input.toValue ?? null,
      note: input.note ?? null,
      meta: input.meta,
    },
  });
}

/** Protokoll für Eingriffe außerhalb einzelner Vorgänge (Verwaltung). */
export async function logAudit(input: {
  actorId?: string | null;
  action: string;
  entityType: string;
  entityId?: string | null;
  summary?: string | null;
  meta?: Prisma.InputJsonValue;
}) {
  return db.auditLog.create({
    data: {
      actorId: input.actorId ?? null,
      action: input.action,
      entityType: input.entityType,
      entityId: input.entityId ?? null,
      summary: input.summary ?? null,
      meta: input.meta,
    },
  });
}

/** Setzt die Aktivitätsmarke, damit Listen nach „zuletzt passiert" sortieren. */
export async function touchTicket(
  tx: Prisma.TransactionClient | typeof db,
  ticketId: string,
  when: Date = new Date(),
) {
  await tx.ticket.update({
    where: { id: ticketId },
    data: { lastActivityAt: when },
  });
}
