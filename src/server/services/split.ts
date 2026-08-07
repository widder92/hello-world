import {
  EventType,
  LinkType,
  MessageType,
  Priority,
  TicketSource,
  TicketStatus,
} from "@prisma/client";

import { db } from "@/server/db";
import { logEvent } from "@/server/services/events";
import { benachrichtige } from "@/server/services/notify";
import { nextTicketNumber } from "@/server/services/number";
import { resolveQueue } from "@/server/services/routing";
import { berechneFristen } from "@/server/services/sla";
import type { AkteurInfo } from "@/server/services/ticket";

/**
 * Abspalten eines Teilvorgangs.
 *
 * Der Fall aus dem Alltag: Ein Ticket kommt in die IT („neuer Arbeitsplatz
 * eingerichtet, kein Netzwerk, keine Steckdose"). Die Dose zu legen ist Sache
 * des Technischen Dienstes, den PC einzurichten Sache der IT. Ohne Abspalten
 * bliebe nur, den Melder zu bitten, ein zweites Ticket zu schreiben – oder das
 * Ticket komplett wegzugeben und den eigenen Anteil zu vergessen.
 *
 * Drei Dinge machen das hier brauchbar:
 *
 *  1. Der Zielbereich bekommt den nötigen Zusammenhang mit: Aus dem Verlauf
 *     lassen sich einzelne Nachrichten auswählen, die mitkopiert werden. Nicht
 *     alles – ein interner Vermerk über einen Lieferanten hilft dem Hausmeister
 *     nicht und gehört ihm auch nicht vorgelegt.
 *
 *  2. Der Melder muss nichts erneut melden. Er bleibt Melder beider Vorgänge
 *     und sieht die Verknüpfung im Portal.
 *
 *  3. Das Ursprungsticket hat drei mögliche Fortsetzungen, weil es drei
 *     Situationen gibt: Der eigene Anteil läuft weiter, der eigene Anteil
 *     hängt am Teilvorgang, oder man ist schlicht nicht mehr zuständig.
 */

export type NachbehandlungElternticket =
  /** Der eigene Anteil läuft weiter. */
  | "offen_lassen"
  /** Zurückstellen, bis der Teilvorgang erledigt ist – dann automatisch wecken. */
  | "warten_auf_teilvorgang"
  /** Geht uns nichts mehr an. */
  | "schliessen";

export interface AbspaltenInput {
  quellTicketId: string;
  zielTeamId: string;
  /** Bei Bereichen mit Eingang je Standort erforderlich. */
  zielSiteId?: string | null;
  zielCategoryId?: string | null;
  betreff: string;
  beschreibung: string;
  prioritaet?: Priority;
  /** Nachrichten aus dem Verlauf, die in den neuen Vorgang kopiert werden. */
  nachrichtenIds: string[];
  nachbehandlung: NachbehandlungElternticket;
  /** Abschlussnotiz, wenn das Ursprungsticket geschlossen wird. */
  abschlussnotiz?: string;
  actor: AkteurInfo;
}

export interface AbspaltenErgebnis {
  kindTicketId: string;
  kindNummer: string;
  zielBereich: string;
}

export async function ticketAbspalten(
  input: AbspaltenInput,
): Promise<AbspaltenErgebnis> {
  const betreff = input.betreff.trim();
  const beschreibung = input.beschreibung.trim();

  if (betreff.length < 3) {
    throw new Error("Bitte geben Sie dem Teilvorgang einen Betreff.");
  }
  if (beschreibung.length < 3) {
    throw new Error(
      "Bitte beschreiben Sie kurz, was der andere Bereich tun soll.",
    );
  }

  const quelle = await db.ticket.findUniqueOrThrow({
    where: { id: input.quellTicketId },
    select: {
      id: true,
      number: true,
      subject: true,
      status: true,
      priority: true,
      siteId: true,
      roomOrArea: true,
      teamId: true,
      requesterId: true,
      createdAt: true,
      team: { select: { name: true } },
    },
  });

  const zielTeam = await db.team.findUniqueOrThrow({
    where: { id: input.zielTeamId },
    select: { id: true, key: true, name: true, mode: true, isActive: true },
  });

  if (!zielTeam.isActive) {
    throw new Error(`Der Bereich "${zielTeam.name}" nimmt keine Vorgänge entgegen.`);
  }

  // Nur Nachrichten aus diesem Vorgang übernehmen – sonst ließen sich mit
  // geratenen Kennungen fremde Nachrichten in ein eigenes Ticket kopieren.
  const zuKopieren = input.nachrichtenIds.length
    ? await db.ticketMessage.findMany({
        where: { id: { in: input.nachrichtenIds }, ticketId: quelle.id },
        orderBy: { createdAt: "asc" },
        select: {
          id: true,
          type: true,
          body: true,
          bodyText: true,
          authorId: true,
          authorName: true,
          authorEmail: true,
          createdAt: true,
          attachments: {
            select: {
              filename: true,
              mimeType: true,
              size: true,
              storageKey: true,
              checksum: true,
              uploadedById: true,
              fromEmail: true,
            },
          },
        },
      })
    : [];

  const ergebnis = await db.$transaction(async (tx) => {
    const ziel = await resolveQueue(
      zielTeam.id,
      input.zielSiteId ?? quelle.siteId,
      tx,
    );

    const nummer = await nextTicketNumber(tx, zielTeam.id, zielTeam.key);
    const prioritaet = input.prioritaet ?? quelle.priority;
    const jetzt = new Date();

    const fristen = await berechneFristen({
      tx,
      teamId: zielTeam.id,
      teamMode: zielTeam.mode,
      priority: prioritaet,
      categorySlaPolicyId: null,
      // Ein abgespaltener Teilvorgang ist neue Arbeit für den Zielbereich und
      // beginnt seine Frist deshalb jetzt – anders als eine Weiterleitung, bei
      // der derselbe Auftrag nur den Bereich wechselt.
      ab: jetzt,
    });

    const kind = await tx.ticket.create({
      data: {
        number: nummer,
        subject: betreff,
        queueId: ziel.queueId,
        teamId: zielTeam.id,
        siteId: ziel.siteId,
        roomOrArea: quelle.roomOrArea,
        categoryId: input.zielCategoryId ?? null,
        // Der Melder bleibt derselbe: Er hat den Vorgang gemeldet und soll
        // beide Teile verfolgen können, ohne etwas erneut zu tun.
        requesterId: quelle.requesterId,
        status: TicketStatus.NEU,
        priority: prioritaet,
        source: TicketSource.WEB,
        slaPolicyId: fristen.slaPolicyId,
        firstResponseDue: fristen.firstResponseDue,
        resolutionDue: fristen.resolutionDue,
        createdAt: jetzt,
        lastActivityAt: jetzt,
      },
      select: { id: true, number: true },
    });

    // Ein Hinweis am Anfang des neuen Verlaufs, damit im Zielbereich sofort
    // klar ist, woher der Vorgang kommt.
    await tx.ticketMessage.create({
      data: {
        ticketId: kind.id,
        type: MessageType.SYSTEM,
        authorId: input.actor.id,
        authorName: input.actor.name,
        body:
          `Dieser Teilvorgang wurde aus ${quelle.number} („${quelle.subject}") ` +
          `abgespalten.` +
          (zuKopieren.length
            ? ` ${zuKopieren.length === 1 ? "Eine Nachricht" : `${zuKopieren.length} Nachrichten`} ` +
              `aus dem bisherigen Verlauf ${zuKopieren.length === 1 ? "wurde" : "wurden"} übernommen.`
            : ""),
        bodyText: `Abgespalten aus ${quelle.number}.`,
        createdAt: jetzt,
      },
    });

    // Die eigentliche Aufgabenbeschreibung für den Zielbereich.
    await tx.ticketMessage.create({
      data: {
        ticketId: kind.id,
        type: MessageType.PUBLIC_REPLY,
        authorId: input.actor.id,
        authorName: input.actor.name,
        body: beschreibung,
        bodyText: beschreibung,
        createdAt: new Date(jetzt.getTime() + 1),
      },
    });

    // Ausgewählte Nachrichten übernehmen. Die ursprüngliche Reihenfolge und
    // Urheberschaft bleiben erhalten; die Zeitstempel werden leicht versetzt
    // hinter den Kopf gelegt, damit der Verlauf lesbar bleibt.
    for (const [i, m] of zuKopieren.entries()) {
      const kopie = await tx.ticketMessage.create({
        data: {
          ticketId: kind.id,
          // Interne Notizen bleiben intern, auch in der Kopie.
          type: m.type === MessageType.SYSTEM ? MessageType.SYSTEM : m.type,
          authorId: m.authorId,
          authorName: m.authorName,
          authorEmail: m.authorEmail,
          body: m.body,
          bodyText: m.bodyText,
          createdAt: new Date(jetzt.getTime() + 2 + i),
        },
        select: { id: true },
      });

      if (m.attachments.length) {
        await tx.attachment.createMany({
          data: m.attachments.map((a) => ({
            ticketId: kind.id,
            messageId: kopie.id,
            filename: a.filename,
            mimeType: a.mimeType,
            size: a.size,
            // Dieselbe Datei in der Ablage, nur ein zweiter Verweis darauf.
            // Kopieren wäre Verschwendung, und ein Foto vom Schaden ist in
            // beiden Vorgängen dasselbe Foto.
            storageKey: a.storageKey,
            checksum: a.checksum,
            uploadedById: a.uploadedById,
            fromEmail: a.fromEmail,
          })),
        });
      }
    }

    await tx.ticketLink.create({
      data: { fromId: quelle.id, toId: kind.id, type: LinkType.SPLIT_FROM },
    });

    // Protokoll auf beiden Seiten – wer eines der Tickets öffnet, sieht die
    // Abspaltung im Verlauf.
    await logEvent(tx, {
      ticketId: quelle.id,
      type: EventType.SPLIT,
      actorId: input.actor.id,
      actorName: input.actor.name,
      toValue: kind.number,
      note: `Teilvorgang an ${zielTeam.name} abgespalten: ${betreff}`,
    });
    await logEvent(tx, {
      ticketId: kind.id,
      type: EventType.SPLIT,
      actorId: input.actor.id,
      actorName: input.actor.name,
      fromValue: quelle.number,
      note: `Aus ${quelle.number} abgespalten.`,
    });

    // --- Das Ursprungsticket ------------------------------------------------
    if (input.nachbehandlung === "warten_auf_teilvorgang") {
      await tx.ticket.update({
        where: { id: quelle.id },
        data: {
          waitingForChild: true,
          status: TicketStatus.ZURUECKGESTELLT,
          lastActivityAt: jetzt,
        },
      });
      await logEvent(tx, {
        ticketId: quelle.id,
        type: EventType.STATUS_CHANGED,
        actorId: input.actor.id,
        actorName: input.actor.name,
        fromValue: quelle.status,
        toValue: TicketStatus.ZURUECKGESTELLT,
        note: `Zurückgestellt, bis ${kind.number} erledigt ist.`,
      });
    } else if (input.nachbehandlung === "schliessen") {
      await tx.ticket.update({
        where: { id: quelle.id },
        data: {
          status: TicketStatus.GELOEST,
          resolvedAt: jetzt,
          waitingForChild: false,
          lastActivityAt: jetzt,
        },
      });

      if (input.abschlussnotiz?.trim()) {
        await tx.ticketMessage.create({
          data: {
            ticketId: quelle.id,
            type: MessageType.PUBLIC_REPLY,
            authorId: input.actor.id,
            authorName: input.actor.name,
            body: input.abschlussnotiz.trim(),
            bodyText: input.abschlussnotiz.trim(),
            createdAt: jetzt,
          },
        });
      }

      await logEvent(tx, {
        ticketId: quelle.id,
        type: EventType.STATUS_CHANGED,
        actorId: input.actor.id,
        actorName: input.actor.name,
        fromValue: quelle.status,
        toValue: TicketStatus.GELOEST,
        note: `Abgeschlossen; der verbleibende Teil läuft unter ${kind.number}.`,
      });
    } else {
      await tx.ticket.update({
        where: { id: quelle.id },
        data: { lastActivityAt: jetzt },
      });
    }

    return { kindTicketId: kind.id, kindNummer: kind.number };
  });

  await benachrichtige.abgespalten({
    elternTicketId: quelle.id,
    kindTicketId: ergebnis.kindTicketId,
    zielBereich: zielTeam.name,
    actorId: input.actor.id,
  });

  return { ...ergebnis, zielBereich: zielTeam.name };
}

/**
 * Zusammenführen zweier Vorgänge.
 *
 * Kommt vor allem vor, wenn dieselbe Störung von mehreren Personen gemeldet
 * wird – bei einem Netzwerkausfall meldet sich ein ganzes Haus.
 */
export async function ticketZusammenfuehren(opts: {
  quellTicketId: string;
  zielTicketId: string;
  actor: AkteurInfo;
}) {
  if (opts.quellTicketId === opts.zielTicketId) {
    throw new Error("Ein Vorgang lässt sich nicht mit sich selbst zusammenführen.");
  }

  const [quelle, ziel] = await Promise.all([
    db.ticket.findUniqueOrThrow({
      where: { id: opts.quellTicketId },
      select: { id: true, number: true, subject: true, status: true, requesterId: true },
    }),
    db.ticket.findUniqueOrThrow({
      where: { id: opts.zielTicketId },
      select: { id: true, number: true, subject: true },
    }),
  ]);

  await db.$transaction(async (tx) => {
    const jetzt = new Date();

    // Nachrichten und Anhänge wandern in den führenden Vorgang.
    await tx.ticketMessage.updateMany({
      where: { ticketId: quelle.id },
      data: { ticketId: ziel.id },
    });
    await tx.attachment.updateMany({
      where: { ticketId: quelle.id },
      data: { ticketId: ziel.id },
    });

    // Der Melder des zusammengeführten Vorgangs wird Beobachter beim
    // führenden – sonst verlöre er den Vorgang aus den Augen.
    await tx.ticketWatcher.upsert({
      where: {
        ticketId_userId: { ticketId: ziel.id, userId: quelle.requesterId },
      },
      create: { ticketId: ziel.id, userId: quelle.requesterId },
      update: {},
    });

    await tx.ticket.update({
      where: { id: quelle.id },
      data: {
        status: TicketStatus.GESCHLOSSEN,
        resolvedAt: jetzt,
        closedAt: jetzt,
        lastActivityAt: jetzt,
      },
    });

    await tx.ticketLink.create({
      data: { fromId: quelle.id, toId: ziel.id, type: LinkType.MERGED_INTO },
    });

    await tx.ticket.update({
      where: { id: ziel.id },
      data: { lastActivityAt: jetzt },
    });

    await logEvent(tx, {
      ticketId: quelle.id,
      type: EventType.MERGED,
      actorId: opts.actor.id,
      actorName: opts.actor.name,
      toValue: ziel.number,
      note: `Zusammengeführt mit ${ziel.number}.`,
    });
    await logEvent(tx, {
      ticketId: ziel.id,
      type: EventType.MERGED,
      actorId: opts.actor.id,
      actorName: opts.actor.name,
      fromValue: quelle.number,
      note: `${quelle.number} („${quelle.subject}") wurde hier eingegliedert.`,
    });
  });

  return { zielNummer: ziel.number };
}
