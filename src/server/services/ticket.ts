import {
  EventType,
  MessageType,
  NotificationType,
  Priority,
  TicketSource,
  TicketStatus,
  type Prisma,
} from "@prisma/client";

import { db } from "@/server/db";
import { ordneAnhaengeZu } from "@/server/services/attachments";
import { logEvent, touchTicket } from "@/server/services/events";
import { nextTicketNumber } from "@/server/services/number";
import { resolveQueue } from "@/server/services/routing";
import { berechneFristen } from "@/server/services/sla";
import { benachrichtige } from "@/server/services/notify";
import { STATUS_ERLEDIGT } from "@/lib/domain";

/**
 * Fachliche Abläufe rund um einen Vorgang.
 *
 * Alles, was den Zustand eines Tickets verändert, läuft hier durch – nicht in
 * den Server-Aktionen. Die Aktionen prüfen die Rechte und rufen dann diese
 * Funktionen auf. So bleibt eine Statusänderung dieselbe, egal ob sie aus der
 * Oberfläche, aus einer Antwortmail oder aus dem Hintergrunddienst kommt.
 */

export interface AkteurInfo {
  id: string | null;
  name: string;
}

// ---------------------------------------------------------------------------
// Anlegen
// ---------------------------------------------------------------------------

export interface TicketAnlegenInput {
  teamId: string;
  siteId?: string | null;
  subject: string;
  body: string;
  requesterId: string;
  categoryId?: string | null;
  roomOrArea?: string | null;
  priority?: Priority;
  impact?: Prisma.TicketCreateInput["impact"];
  urgency?: Prisma.TicketCreateInput["urgency"];
  source?: TicketSource;
  formData?: Prisma.InputJsonValue;
  /** Bei E-Mail-Herkunft die Message-ID, für die spätere Zuordnung. */
  originEmailMessageId?: string | null;
  quarantined?: boolean;
  watcherIds?: string[];
  /** Wer den Vorgang anlegt – bei Selbstmeldungen der Melder selbst. */
  actor?: AkteurInfo;
}

export async function ticketAnlegen(input: TicketAnlegenInput) {
  const actor = input.actor ?? { id: input.requesterId, name: "" };

  const ticket = await db.$transaction(async (tx) => {
    // Bereich + Standort ergeben den Eingang. Wirft, wenn es für den Standort
    // keinen Eingang gibt – dann darf kein Ticket entstehen, das nirgends
    // ankommt.
    const ziel = await resolveQueue(input.teamId, input.siteId, tx);

    const team = await tx.team.findUniqueOrThrow({
      where: { id: input.teamId },
      select: { id: true, key: true, mode: true },
    });

    const number = await nextTicketNumber(tx, team.id, team.key);

    const kategorie = input.categoryId
      ? await tx.category.findUnique({
          where: { id: input.categoryId },
          select: { defaultPriority: true, slaPolicyId: true },
        })
      : null;

    const priority =
      input.priority ?? kategorie?.defaultPriority ?? Priority.NORMAL;

    const erstellt = new Date();
    const fristen = await berechneFristen({
      tx,
      teamId: team.id,
      teamMode: team.mode,
      priority,
      categorySlaPolicyId: kategorie?.slaPolicyId ?? null,
      ab: erstellt,
    });

    const neu = await tx.ticket.create({
      data: {
        number,
        subject: input.subject.trim(),
        queueId: ziel.queueId,
        teamId: team.id,
        siteId: ziel.siteId,
        roomOrArea: input.roomOrArea?.trim() || null,
        categoryId: input.categoryId ?? null,
        requesterId: input.requesterId,
        status: TicketStatus.NEU,
        priority,
        impact: input.impact,
        urgency: input.urgency,
        source: input.source ?? TicketSource.WEB,
        formData: input.formData,
        originEmailMessageId: input.originEmailMessageId ?? null,
        quarantined: input.quarantined ?? false,
        slaPolicyId: fristen.slaPolicyId,
        firstResponseDue: fristen.firstResponseDue,
        resolutionDue: fristen.resolutionDue,
        createdAt: erstellt,
        lastActivityAt: erstellt,
      },
      select: { id: true, number: true, teamId: true, queueId: true },
    });

    await tx.ticketMessage.create({
      data: {
        ticketId: neu.id,
        type: MessageType.PUBLIC_REPLY,
        authorId: input.requesterId,
        body: input.body,
        bodyText: input.body,
        emailMessageId: input.originEmailMessageId ?? null,
      },
    });

    await logEvent(tx, {
      ticketId: neu.id,
      type: EventType.CREATED,
      actorId: actor.id,
      actorName: actor.name || null,
    });

    if (input.watcherIds?.length) {
      await tx.ticketWatcher.createMany({
        data: input.watcherIds
          .filter((id) => id !== input.requesterId)
          .map((userId) => ({ ticketId: neu.id, userId })),
        skipDuplicates: true,
      });
    }

    return neu;
  });

  // Benachrichtigungen außerhalb der Transaktion: Sie dürfen den Vorgang nicht
  // scheitern lassen, wenn der Mailversand klemmt.
  await benachrichtige.neuerVorgangImEingang(ticket.id);

  return ticket;
}

// ---------------------------------------------------------------------------
// Zuweisung
// ---------------------------------------------------------------------------

/** Selbst übernehmen ("Ticket ziehen"). */
export async function ticketUebernehmen(ticketId: string, actor: AkteurInfo) {
  if (!actor.id) throw new Error("Für die Übernahme wird ein Konto benötigt.");

  const ticket = await db.ticket.findUniqueOrThrow({
    where: { id: ticketId },
    select: { id: true, assigneeId: true, status: true },
  });

  // Wer schon zugewiesen ist, wird nicht stillschweigend überschrieben.
  if (ticket.assigneeId && ticket.assigneeId !== actor.id) {
    throw new Error(
      "Dieser Vorgang ist bereits jemandem zugewiesen. Bitte erst die " +
        "Zuweisung aufheben.",
    );
  }

  await db.$transaction(async (tx) => {
    await tx.ticket.update({
      where: { id: ticketId },
      data: {
        assigneeId: actor.id,
        // Ein übernommener Vorgang ist nicht mehr "neu".
        status:
          ticket.status === TicketStatus.NEU
            ? TicketStatus.IN_BEARBEITUNG
            : ticket.status,
        lastActivityAt: new Date(),
      },
    });

    await logEvent(tx, {
      ticketId,
      type: EventType.CLAIMED,
      actorId: actor.id,
      actorName: actor.name,
      toValue: actor.name,
    });
  });
}

/** An eine andere Person übergeben. */
export async function ticketZuweisen(
  ticketId: string,
  assigneeId: string | null,
  actor: AkteurInfo,
) {
  const ticket = await db.ticket.findUniqueOrThrow({
    where: { id: ticketId },
    select: {
      id: true,
      number: true,
      subject: true,
      status: true,
      assignee: { select: { id: true, name: true } },
    },
  });

  const neuerBearbeiter = assigneeId
    ? await db.user.findUniqueOrThrow({
        where: { id: assigneeId },
        select: { id: true, name: true },
      })
    : null;

  await db.$transaction(async (tx) => {
    await tx.ticket.update({
      where: { id: ticketId },
      data: {
        assigneeId: neuerBearbeiter?.id ?? null,
        status:
          neuerBearbeiter && ticket.status === TicketStatus.NEU
            ? TicketStatus.IN_BEARBEITUNG
            : ticket.status,
        lastActivityAt: new Date(),
      },
    });

    await logEvent(tx, {
      ticketId,
      type: neuerBearbeiter ? EventType.ASSIGNED : EventType.UNASSIGNED,
      actorId: actor.id,
      actorName: actor.name,
      fromValue: ticket.assignee?.name ?? null,
      toValue: neuerBearbeiter?.name ?? null,
    });
  });

  if (neuerBearbeiter && neuerBearbeiter.id !== actor.id) {
    await benachrichtige.erstellen({
      userId: neuerBearbeiter.id,
      ticketId,
      type: NotificationType.TICKET_ASSIGNED,
      title: `${ticket.number} wurde Ihnen zugewiesen`,
      body: ticket.subject,
    });
  }
}

// ---------------------------------------------------------------------------
// Status
// ---------------------------------------------------------------------------

export async function statusAendern(
  ticketId: string,
  status: TicketStatus,
  actor: AkteurInfo,
  note?: string,
) {
  const ticket = await db.ticket.findUniqueOrThrow({
    where: { id: ticketId },
    select: {
      id: true,
      number: true,
      subject: true,
      status: true,
      requesterId: true,
      resolvedAt: true,
    },
  });

  if (ticket.status === status) return;

  const jetzt = new Date();
  const wirdErledigt = STATUS_ERLEDIGT.includes(status);
  const warErledigt = STATUS_ERLEDIGT.includes(ticket.status);

  await db.$transaction(async (tx) => {
    await tx.ticket.update({
      where: { id: ticketId },
      data: {
        status,
        resolvedAt: wirdErledigt ? (ticket.resolvedAt ?? jetzt) : null,
        closedAt: status === TicketStatus.GESCHLOSSEN ? jetzt : null,
        lastActivityAt: jetzt,
      },
    });

    await logEvent(tx, {
      ticketId,
      type: warErledigt && !wirdErledigt ? EventType.REOPENED : EventType.STATUS_CHANGED,
      actorId: actor.id,
      actorName: actor.name,
      fromValue: ticket.status,
      toValue: status,
      note: note ?? null,
    });
  });

  // Ein erledigter Vorgang kann ein Elternticket freigeben, das auf ihn wartet.
  if (wirdErledigt && !warErledigt) {
    await elternticketFreigeben(ticketId, actor);
    await benachrichtige.erstellen({
      userId: ticket.requesterId,
      ticketId,
      type: NotificationType.TICKET_RESOLVED,
      title: `${ticket.number} wurde erledigt`,
      body: ticket.subject,
      skipIfSelf: actor.id,
    });
  }
}

/**
 * Weckt ein Ursprungsticket, das beim Abspalten auf diesen Teilvorgang
 * gestellt wurde.
 *
 * Ohne diesen Schritt bliebe das Elternticket unbemerkt liegen, bis jemand
 * zufällig hineinschaut – genau der Fall, den das Abspalten vermeiden soll.
 */
async function elternticketFreigeben(kindId: string, actor: AkteurInfo) {
  const verknuepfung = await db.ticketLink.findFirst({
    where: { toId: kindId, type: "SPLIT_FROM" },
    select: {
      from: {
        select: {
          id: true,
          number: true,
          status: true,
          waitingForChild: true,
          assigneeId: true,
        },
      },
      to: { select: { number: true, subject: true } },
    },
  });

  const eltern = verknuepfung?.from;
  if (!eltern?.waitingForChild) return;

  // Weitere offene Teilvorgänge? Dann bleibt das Elternticket zurückgestellt.
  const weitereOffen = await db.ticketLink.count({
    where: {
      fromId: eltern.id,
      type: "SPLIT_FROM",
      to: { status: { notIn: STATUS_ERLEDIGT } },
    },
  });
  if (weitereOffen > 0) return;

  await db.$transaction(async (tx) => {
    await tx.ticket.update({
      where: { id: eltern.id },
      data: {
        waitingForChild: false,
        status:
          eltern.status === TicketStatus.ZURUECKGESTELLT
            ? TicketStatus.OFFEN
            : eltern.status,
        lastActivityAt: new Date(),
      },
    });

    await logEvent(tx, {
      ticketId: eltern.id,
      type: EventType.SPLIT_CHILD_RESOLVED,
      actorId: actor.id,
      actorName: actor.name,
      fromValue: verknuepfung!.to.number,
      note: `Der abgespaltene Teilvorgang ${verknuepfung!.to.number} ist erledigt. Dieser Vorgang wurde wieder geöffnet.`,
    });
  });

  if (eltern.assigneeId) {
    await benachrichtige.erstellen({
      userId: eltern.assigneeId,
      ticketId: eltern.id,
      type: NotificationType.TICKET_SPLIT_CHILD_RESOLVED,
      title: `${eltern.number} läuft weiter`,
      body: `Der Teilvorgang ${verknuepfung!.to.number} ist erledigt.`,
      skipIfSelf: actor.id,
    });
  }
}

// ---------------------------------------------------------------------------
// Weitere Eigenschaften
// ---------------------------------------------------------------------------

export async function prioritaetAendern(
  ticketId: string,
  priority: Priority,
  actor: AkteurInfo,
) {
  const ticket = await db.ticket.findUniqueOrThrow({
    where: { id: ticketId },
    select: {
      id: true,
      priority: true,
      createdAt: true,
      teamId: true,
      categoryId: true,
      team: { select: { mode: true } },
    },
  });

  if (ticket.priority === priority) return;

  // Die Frist hängt an der Priorität und wird deshalb neu gerechnet – ab dem
  // Eingang des Vorgangs, nicht ab jetzt, sonst ließen sich Fristen durch
  // Hoch- und Zurückstufen beliebig verlängern.
  const fristen = await berechneFristen({
    tx: db,
    teamId: ticket.teamId,
    teamMode: ticket.team.mode,
    priority,
    categorySlaPolicyId: null,
    ab: ticket.createdAt,
  });

  await db.$transaction(async (tx) => {
    await tx.ticket.update({
      where: { id: ticketId },
      data: {
        priority,
        slaPolicyId: fristen.slaPolicyId,
        firstResponseDue: fristen.firstResponseDue,
        resolutionDue: fristen.resolutionDue,
        slaWarningNotified: false,
        lastActivityAt: new Date(),
      },
    });

    await logEvent(tx, {
      ticketId,
      type: EventType.PRIORITY_CHANGED,
      actorId: actor.id,
      actorName: actor.name,
      fromValue: ticket.priority,
      toValue: priority,
    });
  });
}

export async function kategorieAendern(
  ticketId: string,
  categoryId: string | null,
  actor: AkteurInfo,
) {
  const ticket = await db.ticket.findUniqueOrThrow({
    where: { id: ticketId },
    select: { category: { select: { id: true, name: true } } },
  });

  const neue = categoryId
    ? await db.category.findUniqueOrThrow({
        where: { id: categoryId },
        select: { id: true, name: true },
      })
    : null;

  await db.$transaction(async (tx) => {
    await tx.ticket.update({
      where: { id: ticketId },
      data: { categoryId: neue?.id ?? null, lastActivityAt: new Date() },
    });
    await logEvent(tx, {
      ticketId,
      type: EventType.CATEGORY_CHANGED,
      actorId: actor.id,
      actorName: actor.name,
      fromValue: ticket.category?.name ?? null,
      toValue: neue?.name ?? null,
    });
  });
}

// ---------------------------------------------------------------------------
// Nachrichten
// ---------------------------------------------------------------------------

export interface NachrichtInput {
  ticketId: string;
  body: string;
  type: MessageType;
  authorId?: string | null;
  authorName?: string | null;
  authorEmail?: string | null;
  emailMessageId?: string | null;
  emailInReplyTo?: string | null;
  viaEmailCommand?: boolean;
  /** Status, auf den der Vorgang nach der Nachricht gesetzt wird. */
  neuerStatus?: TicketStatus;
  /** Zuvor hochgeladene Anhänge, die an diese Nachricht gehängt werden. */
  anhangIds?: string[];
}

export async function nachrichtHinzufuegen(input: NachrichtInput) {
  const ticket = await db.ticket.findUniqueOrThrow({
    where: { id: input.ticketId },
    select: {
      id: true,
      number: true,
      subject: true,
      status: true,
      requesterId: true,
      assigneeId: true,
      firstResponseAt: true,
    },
  });

  const jetzt = new Date();
  const istOeffentlich = input.type === MessageType.PUBLIC_REPLY;
  const vonBearbeiter = input.authorId !== ticket.requesterId;

  // Die Erstreaktion zählt nur, wenn sie öffentlich ist und nicht vom Melder
  // selbst kommt – eine interne Notiz hat den Melder nicht erreicht.
  const istErstreaktion =
    istOeffentlich && vonBearbeiter && !ticket.firstResponseAt;

  const message = await db.$transaction(async (tx) => {
    const m = await tx.ticketMessage.create({
      data: {
        ticketId: input.ticketId,
        type: input.type,
        authorId: input.authorId ?? null,
        authorName: input.authorName ?? null,
        authorEmail: input.authorEmail ?? null,
        body: input.body,
        bodyText: input.body,
        emailMessageId: input.emailMessageId ?? null,
        emailInReplyTo: input.emailInReplyTo ?? null,
        viaEmailCommand: input.viaEmailCommand ?? false,
      },
      select: { id: true },
    });

    const daten: Prisma.TicketUpdateInput = { lastActivityAt: jetzt };

    if (istErstreaktion) daten.firstResponseAt = jetzt;

    if (input.neuerStatus) {
      daten.status = input.neuerStatus;
      if (STATUS_ERLEDIGT.includes(input.neuerStatus)) {
        daten.resolvedAt = jetzt;
      }
    } else if (
      // Antwortet der Melder auf eine Rückfrage, ist die Wartezeit vorbei.
      !vonBearbeiter &&
      ticket.status === TicketStatus.WARTET_AUF_MELDER
    ) {
      daten.status = TicketStatus.OFFEN;
    }

    await tx.ticket.update({ where: { id: input.ticketId }, data: daten });

    if (istErstreaktion) {
      await logEvent(tx, {
        ticketId: input.ticketId,
        type: EventType.FIRST_RESPONSE,
        actorId: input.authorId,
        actorName: input.authorName,
      });
    }

    return m;
  });

  // Anhänge erst jetzt zuordnen: Sie wurden beim Schreiben hochgeladen und
  // hingen bis hierhin an keinem Vorgang.
  if (input.anhangIds?.length && input.authorId) {
    await ordneAnhaengeZu({
      attachmentIds: input.anhangIds,
      ticketId: input.ticketId,
      messageId: message.id,
      uploadedById: input.authorId,
    });
  }

  if (istOeffentlich) {
    await benachrichtige.neueAntwort(input.ticketId, message.id, input.authorId ?? null);
  }

  return message;
}

// ---------------------------------------------------------------------------
// Beobachter
// ---------------------------------------------------------------------------

export async function beobachterHinzufuegen(
  ticketId: string,
  userId: string,
  actor: AkteurInfo,
) {
  const person = await db.user.findUniqueOrThrow({
    where: { id: userId },
    select: { id: true, name: true },
  });

  await db.ticketWatcher.upsert({
    where: { ticketId_userId: { ticketId, userId } },
    create: { ticketId, userId },
    update: {},
  });

  await logEvent(db, {
    ticketId,
    type: EventType.WATCHER_ADDED,
    actorId: actor.id,
    actorName: actor.name,
    toValue: person.name,
  });
  await touchTicket(db, ticketId);
}

export async function beobachterEntfernen(
  ticketId: string,
  userId: string,
  actor: AkteurInfo,
) {
  const person = await db.user.findUnique({
    where: { id: userId },
    select: { name: true },
  });

  await db.ticketWatcher.deleteMany({ where: { ticketId, userId } });

  await logEvent(db, {
    ticketId,
    type: EventType.WATCHER_REMOVED,
    actorId: actor.id,
    actorName: actor.name,
    toValue: person?.name ?? null,
  });
}
