import { EventType, TicketStatus } from "@prisma/client";

import { db } from "@/server/db";
import { logEvent } from "@/server/services/events";
import { benachrichtige } from "@/server/services/notify";
import { resolveQueue } from "@/server/services/routing";
import { berechneFristen } from "@/server/services/sla";
import type { AkteurInfo } from "@/server/services/ticket";

/**
 * Weiterleiten an einen anderen Bereich.
 *
 * Der häufigste Fall im Alltag: Ein Ticket landet beim falschen Bereich, weil
 * beim Melden nicht klar war, wer zuständig ist. Statt den Melder zurück ans
 * Portal zu schicken, gibt der empfangende Bereich den Vorgang mit einem Klick
 * weiter – Verlauf, Anhänge und Melder bleiben erhalten.
 *
 * Die Zuweisung wird dabei aufgehoben: Im neuen Bereich soll der Vorgang im
 * Sammeleingang liegen und nicht bei jemandem, der ihn nie gesehen hat.
 */
export async function ticketWeiterleiten(opts: {
  ticketId: string;
  zielTeamId: string;
  /** Bei Bereichen mit Eingang je Standort erforderlich. */
  zielSiteId?: string | null;
  zielCategoryId?: string | null;
  begruendung: string;
  actor: AkteurInfo;
}) {
  const begruendung = opts.begruendung.trim();
  if (begruendung.length < 3) {
    throw new Error(
      "Bitte geben Sie kurz an, warum der Vorgang weitergeleitet wird – der " +
        "neue Bereich sieht sonst nur ein Ticket ohne Zusammenhang.",
    );
  }

  const ticket = await db.ticket.findUniqueOrThrow({
    where: { id: opts.ticketId },
    select: {
      id: true,
      number: true,
      subject: true,
      status: true,
      priority: true,
      siteId: true,
      teamId: true,
      queueId: true,
      createdAt: true,
      team: { select: { id: true, name: true, notifyRequesterOnTransfer: true } },
    },
  });

  if (ticket.teamId === opts.zielTeamId) {
    throw new Error("Der Vorgang liegt bereits in diesem Bereich.");
  }

  const zielTeam = await db.team.findUniqueOrThrow({
    where: { id: opts.zielTeamId },
    select: { id: true, name: true, mode: true, isActive: true },
  });

  const ergebnis = await db.$transaction(async (tx) => {
    // Der Standort des Vorgangs ist die Vorbelegung; bei Bereichen mit Eingang
    // je Standort kann er im Dialog abweichend gewählt werden.
    const ziel = await resolveQueue(
      opts.zielTeamId,
      opts.zielSiteId ?? ticket.siteId,
      tx,
    );

    // Fristen gelten je Bereich – deshalb neu rechnen, aber weiterhin ab dem
    // ursprünglichen Eingang. Ein weitergeleiteter Vorgang bekommt so keine
    // frische Frist geschenkt.
    const fristen = await berechneFristen({
      tx,
      teamId: zielTeam.id,
      teamMode: zielTeam.mode,
      priority: ticket.priority,
      categorySlaPolicyId: null,
      ab: ticket.createdAt,
    });

    await tx.ticket.update({
      where: { id: ticket.id },
      data: {
        queueId: ziel.queueId,
        teamId: zielTeam.id,
        siteId: ziel.siteId,
        categoryId: opts.zielCategoryId ?? null,
        assigneeId: null,
        status:
          ticket.status === TicketStatus.NEU
            ? TicketStatus.NEU
            : TicketStatus.OFFEN,
        slaPolicyId: fristen.slaPolicyId,
        firstResponseDue: fristen.firstResponseDue,
        resolutionDue: fristen.resolutionDue,
        slaWarningNotified: false,
        slaBreached: false,
        lastActivityAt: new Date(),
      },
    });

    await logEvent(tx, {
      ticketId: ticket.id,
      type: EventType.TRANSFERRED,
      actorId: opts.actor.id,
      actorName: opts.actor.name,
      fromValue: ticket.team.name,
      toValue: zielTeam.name,
      note: begruendung,
    });

    return { queueId: ziel.queueId };
  });

  await benachrichtige.weitergeleitet({
    ticketId: ticket.id,
    vonBereich: ticket.team.name,
    nachBereich: zielTeam.name,
    neuerQueueId: ergebnis.queueId,
    begruendung,
    actorId: opts.actor.id,
    melderInformieren: ticket.team.notifyRequesterOnTransfer,
  });

  return { queueId: ergebnis.queueId, zielBereich: zielTeam.name };
}
