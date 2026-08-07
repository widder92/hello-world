import { MessageType, NotificationType, TeamRole } from "@prisma/client";

import { db } from "@/server/db";
import { antwortText, sendeMail } from "@/mail/outbound";
import { STATUS_ERLEDIGT } from "@/lib/domain";

/**
 * Benachrichtigungen.
 *
 * Zwei Wege: ein Eintrag in der Glocke im System und – bei allem, was den
 * Melder betrifft – zusätzlich eine E-Mail. Interne Notizen lösen nie eine
 * Mail an den Melder aus; das ist der ganze Sinn einer internen Notiz.
 *
 * Fehler beim Mailversand werden protokolliert, aber nicht weitergereicht:
 * Ein klemmender Mailserver darf nicht dazu führen, dass eine Antwort im
 * Ticket gar nicht erst gespeichert wird.
 */

function appUrl(): string {
  return (process.env.APP_URL || "http://localhost:3000").replace(/\/$/, "");
}

async function erstellen(opts: {
  userId: string;
  ticketId?: string;
  type: NotificationType;
  title: string;
  body?: string | null;
  /** Nicht benachrichtigen, wenn Empfänger und Auslöser dieselbe Person sind. */
  skipIfSelf?: string | null;
}) {
  if (opts.skipIfSelf && opts.skipIfSelf === opts.userId) return;

  await db.notification.create({
    data: {
      userId: opts.userId,
      ticketId: opts.ticketId,
      type: opts.type,
      title: opts.title,
      body: opts.body ?? null,
    },
  });
}

/**
 * Neuer Vorgang im Eingang: informiert die Personen, die dort arbeiten.
 *
 * Bewusst nur ein Eintrag in der Glocke und keine Mail – sonst bekäme die IT
 * bei jedem Ticket zwei Nachrichten, und die Mailflut war ja gerade der Grund
 * für ein Ticketsystem.
 */
async function neuerVorgangImEingang(ticketId: string) {
  const ticket = await db.ticket.findUnique({
    where: { id: ticketId },
    select: {
      id: true,
      number: true,
      subject: true,
      queueId: true,
      teamId: true,
      requesterId: true,
      requester: { select: { name: true } },
    },
  });
  if (!ticket) return;

  // Wer den Eingang freigeschaltet hat, plus die Bereichsleitungen.
  const [freigaben, leitungen] = await Promise.all([
    db.queueAccess.findMany({
      where: { queueId: ticket.queueId },
      select: { userId: true },
    }),
    db.teamMembership.findMany({
      where: { teamId: ticket.teamId, role: TeamRole.LEAD },
      select: { userId: true },
    }),
  ]);

  const empfaenger = new Set([
    ...freigaben.map((f) => f.userId),
    ...leitungen.map((l) => l.userId),
  ]);
  empfaenger.delete(ticket.requesterId);

  if (empfaenger.size === 0) return;

  await db.notification.createMany({
    data: [...empfaenger].map((userId) => ({
      userId,
      ticketId: ticket.id,
      type: NotificationType.TICKET_NEW_IN_QUEUE,
      title: `Neu: ${ticket.number}`,
      body: `${ticket.subject} – von ${ticket.requester.name}`,
    })),
  });
}

/**
 * Öffentliche Antwort: geht an den Melder (per Mail) sowie an Beobachter und
 * die zuständige Bearbeitung (in der Glocke).
 */
async function neueAntwort(
  ticketId: string,
  messageId: string,
  autorId: string | null,
) {
  const [ticket, nachricht] = await Promise.all([
    db.ticket.findUnique({
      where: { id: ticketId },
      select: {
        id: true,
        number: true,
        subject: true,
        requesterId: true,
        assigneeId: true,
        requester: {
          select: { id: true, name: true, email: true, isActive: true },
        },
        watchers: {
          select: { user: { select: { id: true, email: true, name: true } } },
        },
        messages: {
          where: { type: MessageType.PUBLIC_REPLY },
          orderBy: { createdAt: "desc" },
          take: 5,
          select: { emailMessageId: true },
        },
      },
    }),
    db.ticketMessage.findUnique({
      where: { id: messageId },
      select: {
        id: true,
        type: true,
        bodyText: true,
        authorId: true,
        authorName: true,
        author: { select: { name: true } },
      },
    }),
  ]);

  if (!ticket || !nachricht) return;
  if (nachricht.type !== MessageType.PUBLIC_REPLY) return;

  const autorName = nachricht.author?.name ?? nachricht.authorName ?? "Ihr Team";
  const vomMelder = autorId === ticket.requesterId;

  // --- Mail an den Melder, wenn die Antwort nicht von ihm selbst kommt ------
  if (!vomMelder && ticket.requester.email && ticket.requester.isActive) {
    const referenzen = ticket.messages
      .map((m) => m.emailMessageId)
      .filter((id): id is string => Boolean(id));

    const ergebnis = await sendeMail(
      {
        an: ticket.requester.email,
        anName: ticket.requester.name,
        cc: ticket.watchers
          .map((w) => w.user.email)
          .filter((e): e is string => Boolean(e)),
        betreff: ticket.subject,
        text: antwortText({
          empfaengerName: ticket.requester.name,
          ticketNumber: ticket.number,
          betreff: ticket.subject,
          text: nachricht.bodyText,
          autorName,
          appUrl: appUrl(),
        }),
        ticketId: ticket.id,
        empfaengerId: ticket.requester.id,
        inReplyTo: referenzen[0] ?? null,
        references: referenzen,
      },
      ticket.number,
    );

    if (ergebnis.versendet) {
      await db.ticketMessage.update({
        where: { id: messageId },
        data: { emailSentAt: new Date(), emailMessageId: ergebnis.messageId },
      });
    }
  }

  // --- Einträge in der Glocke ----------------------------------------------
  const empfaenger = new Set<string>();
  if (!vomMelder) empfaenger.add(ticket.requesterId);
  if (ticket.assigneeId) empfaenger.add(ticket.assigneeId);
  for (const w of ticket.watchers) empfaenger.add(w.user.id);
  if (autorId) empfaenger.delete(autorId);

  for (const userId of empfaenger) {
    await erstellen({
      userId,
      ticketId: ticket.id,
      type: NotificationType.TICKET_REPLY,
      title: `Neue Antwort zu ${ticket.number}`,
      body: ticket.subject,
    });
  }
}

/**
 * Weiterleitung an einen anderen Bereich.
 *
 * Der Melder wird informiert, wenn der abgebende Bereich das so eingestellt
 * hat – aus seiner Sicht ändert sich schließlich, wer sich kümmert.
 */
async function weitergeleitet(opts: {
  ticketId: string;
  vonBereich: string;
  nachBereich: string;
  neuerQueueId: string;
  begruendung: string;
  actorId: string | null;
  melderInformieren: boolean;
}) {
  const ticket = await db.ticket.findUnique({
    where: { id: opts.ticketId },
    select: {
      id: true,
      number: true,
      subject: true,
      requesterId: true,
      requester: { select: { id: true, name: true, email: true } },
    },
  });
  if (!ticket) return;

  // Die Kolleg:innen im Zielbereich sollen es in der Glocke sehen.
  const freigaben = await db.queueAccess.findMany({
    where: { queueId: opts.neuerQueueId },
    select: { userId: true },
  });

  for (const f of freigaben) {
    await erstellen({
      userId: f.userId,
      ticketId: ticket.id,
      type: NotificationType.TICKET_TRANSFERRED,
      title: `${ticket.number} wurde an ${opts.nachBereich} weitergeleitet`,
      body: ticket.subject,
      skipIfSelf: opts.actorId,
    });
  }

  if (!opts.melderInformieren || !ticket.requester.email) return;

  await sendeMail(
    {
      an: ticket.requester.email,
      anName: ticket.requester.name,
      betreff: ticket.subject,
      text: [
        `Guten Tag ${ticket.requester.name},`,
        "",
        `Ihr Vorgang wurde von ${opts.vonBereich} an ${opts.nachBereich} weitergeleitet.`,
        `Ab sofort kümmert sich dieser Bereich darum. Sie müssen nichts weiter tun.`,
        "",
        `Begründung: ${opts.begruendung}`,
        "",
        `Vorgang ${ticket.number}: ${ticket.subject}`,
        `Stand ansehen: ${appUrl()}/tickets/${ticket.number}`,
      ].join("\n"),
      ticketId: ticket.id,
      empfaengerId: ticket.requester.id,
    },
    ticket.number,
  );
}

/** Abspaltung: der Melder erfährt, dass ein Teil eigene Wege geht. */
async function abgespalten(opts: {
  elternTicketId: string;
  kindTicketId: string;
  zielBereich: string;
  actorId: string | null;
}) {
  const [eltern, kind] = await Promise.all([
    db.ticket.findUnique({
      where: { id: opts.elternTicketId },
      select: {
        number: true,
        subject: true,
        requester: { select: { id: true, name: true, email: true } },
      },
    }),
    db.ticket.findUnique({
      where: { id: opts.kindTicketId },
      select: { id: true, number: true, subject: true, queueId: true },
    }),
  ]);
  if (!eltern || !kind) return;

  const freigaben = await db.queueAccess.findMany({
    where: { queueId: kind.queueId },
    select: { userId: true },
  });

  for (const f of freigaben) {
    await erstellen({
      userId: f.userId,
      ticketId: kind.id,
      type: NotificationType.TICKET_NEW_IN_QUEUE,
      title: `Teilvorgang ${kind.number} aus ${eltern.number}`,
      body: kind.subject,
      skipIfSelf: opts.actorId,
    });
  }

  if (!eltern.requester.email) return;

  await sendeMail(
    {
      an: eltern.requester.email,
      anName: eltern.requester.name,
      betreff: kind.subject,
      text: [
        `Guten Tag ${eltern.requester.name},`,
        "",
        `Für einen Teil Ihres Vorgangs ${eltern.number} ist ein anderer Bereich`,
        `zuständig. Wir haben diesen Teil deshalb an ${opts.zielBereich} abgegeben.`,
        "",
        `Sie müssen dafür nichts erneut melden – der neue Vorgang läuft unter`,
        `der Nummer ${kind.number} und ist mit Ihrem ursprünglichen Vorgang`,
        `verknüpft.`,
        "",
        `Teilvorgang ansehen: ${appUrl()}/tickets/${kind.number}`,
        `Ursprünglicher Vorgang: ${appUrl()}/tickets/${eltern.number}`,
      ].join("\n"),
      ticketId: kind.id,
      empfaengerId: eltern.requester.id,
    },
    kind.number,
  );
}

/** Bestätigung an den Melder, dass die Meldung angekommen ist. */
async function eingangsbestaetigung(ticketId: string) {
  const ticket = await db.ticket.findUnique({
    where: { id: ticketId },
    select: {
      id: true,
      number: true,
      subject: true,
      status: true,
      team: { select: { name: true } },
      queue: { select: { name: true } },
      requester: { select: { id: true, name: true, email: true } },
    },
  });
  if (!ticket?.requester.email) return;
  if (STATUS_ERLEDIGT.includes(ticket.status)) return;

  await sendeMail(
    {
      an: ticket.requester.email,
      anName: ticket.requester.name,
      betreff: ticket.subject,
      text: [
        `Guten Tag ${ticket.requester.name},`,
        "",
        `vielen Dank für Ihre Meldung. Sie ist bei ${ticket.team.name} eingegangen`,
        `und wird dort bearbeitet.`,
        "",
        `Ihre Vorgangsnummer: ${ticket.number}`,
        `Betreff: ${ticket.subject}`,
        "",
        `Stand ansehen: ${appUrl()}/tickets/${ticket.number}`,
        "",
        "Sie können direkt auf diese E-Mail antworten, wenn Ihnen noch etwas",
        "einfällt – Ihre Antwort wird dem Vorgang zugeordnet.",
      ].join("\n"),
      ticketId: ticket.id,
      empfaengerId: ticket.requester.id,
    },
    ticket.number,
  );
}

export const benachrichtige = {
  erstellen,
  neuerVorgangImEingang,
  neueAntwort,
  weitergeleitet,
  abgespalten,
  eingangsbestaetigung,
};
