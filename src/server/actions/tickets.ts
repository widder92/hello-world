"use server";

import { revalidatePath } from "next/cache";
import { MessageType, Priority, TicketStatus } from "@prisma/client";
import { z } from "zod";

import { db } from "@/server/db";
import { requireCapability, statusAuswahlFor } from "@/server/capabilities";
import {
  canAssignToOthers,
  requireActor,
  requireTicketView,
  requireTicketWork,
} from "@/server/policy";
import {
  beobachterEntfernen,
  beobachterHinzufuegen,
  kategorieAendern,
  nachrichtHinzufuegen,
  prioritaetAendern,
  statusAendern,
  ticketUebernehmen,
  ticketZuweisen,
  type AkteurInfo,
} from "@/server/services/ticket";
import { ticketWeiterleiten } from "@/server/services/transfer";
import {
  ticketAbspalten,
  ticketZusammenfuehren,
  type NachbehandlungElternticket,
} from "@/server/services/split";

/**
 * Server-Aktionen rund um einen Vorgang.
 *
 * Jede Aktion prüft in dieser Reihenfolge:
 *   1. Ist überhaupt jemand angemeldet?
 *   2. Darf diese Person den Vorgang bearbeiten (Bereich **und** Eingang)?
 *   3. Hat der Bereich diese Funktion in seiner Ausbaustufe?
 *
 * Erst danach wird der fachliche Ablauf aufgerufen. Die Oberfläche blendet
 * dieselben Funktionen aus – aber das ist Bequemlichkeit, nicht der Schutz.
 */

/** Lädt einen Vorgang und prüft, ob er bearbeitet werden darf. */
async function fuerBearbeitung(ticketId: string) {
  const actor = await requireActor();

  const ticket = await db.ticket.findUnique({
    where: { id: ticketId },
    select: {
      id: true,
      number: true,
      queueId: true,
      teamId: true,
      requesterId: true,
      status: true,
      team: { select: { id: true, name: true, mode: true } },
      watchers: { select: { userId: true } },
    },
  });

  if (!ticket) throw new Error("Der Vorgang wurde nicht gefunden.");

  requireTicketWork(actor, ticket);

  const akteur: AkteurInfo = { id: actor.id, name: actor.name };
  return { actor, ticket, akteur };
}

/** Wie oben, erlaubt aber auch dem Melder das Antworten auf seinen Vorgang. */
async function fuerAnsicht(ticketId: string) {
  const actor = await requireActor();

  const ticket = await db.ticket.findUnique({
    where: { id: ticketId },
    select: {
      id: true,
      number: true,
      queueId: true,
      teamId: true,
      requesterId: true,
      status: true,
      team: { select: { id: true, name: true, mode: true } },
      watchers: { select: { userId: true } },
    },
  });

  if (!ticket) throw new Error("Der Vorgang wurde nicht gefunden.");

  const level = requireTicketView(actor, ticket);
  const akteur: AkteurInfo = { id: actor.id, name: actor.name };
  return { actor, ticket, akteur, level };
}

function aktualisiere(nummer: string) {
  revalidatePath(`/tickets/${nummer}`);
  revalidatePath("/", "layout");
}

// ---------------------------------------------------------------------------
// Zuweisung
// ---------------------------------------------------------------------------

export async function uebernehmenAction(ticketId: string) {
  const { ticket, akteur } = await fuerBearbeitung(ticketId);
  requireCapability(ticket.team.mode, "zuweisung");

  await ticketUebernehmen(ticketId, akteur);
  aktualisiere(ticket.number);
  return { ok: true as const };
}

export async function zuweisenAction(ticketId: string, assigneeId: string | null) {
  const { actor, ticket, akteur } = await fuerBearbeitung(ticketId);
  requireCapability(ticket.team.mode, "zuweisung");

  // Sich selbst zuweisen darf jedes Mitglied; anderen zuteilen nur
  // Verteilende und Leitungen.
  const anAndere = assigneeId !== null && assigneeId !== actor.id;
  if (anAndere && !canAssignToOthers(actor, ticket.teamId)) {
    throw new Error(
      "Vorgänge an Kolleginnen und Kollegen zuzuteilen ist der Verteilung " +
        "und der Bereichsleitung vorbehalten. Übernehmen können Sie ihn selbst.",
    );
  }

  if (assigneeId) {
    // Nur an Personen zuweisen, die im Bereich arbeiten und den Eingang sehen.
    const darfArbeiten = await db.user.findFirst({
      where: {
        id: assigneeId,
        isActive: true,
        memberships: { some: { teamId: ticket.teamId } },
        OR: [
          { queueAccess: { some: { queueId: ticket.queueId } } },
          { memberships: { some: { teamId: ticket.teamId, role: "LEAD" } } },
        ],
      },
      select: { id: true },
    });

    if (!darfArbeiten) {
      throw new Error(
        "Diese Person arbeitet nicht in diesem Bereich oder ist für diesen " +
          "Eingang nicht freigegeben.",
      );
    }
  }

  await ticketZuweisen(ticketId, assigneeId, akteur);
  aktualisiere(ticket.number);
  return { ok: true as const };
}

// ---------------------------------------------------------------------------
// Status und Eigenschaften
// ---------------------------------------------------------------------------

export async function statusAendernAction(ticketId: string, status: TicketStatus) {
  const { ticket, akteur } = await fuerBearbeitung(ticketId);

  const erlaubt = statusAuswahlFor(ticket.team.mode);
  if (!erlaubt.includes(status)) {
    throw new Error(
      `Der Status „${status}" steht in diesem Bereich nicht zur Verfügung.`,
    );
  }

  await statusAendern(ticketId, status, akteur);
  aktualisiere(ticket.number);
  return { ok: true as const };
}

export async function prioritaetAendernAction(
  ticketId: string,
  priority: Priority,
) {
  const { ticket, akteur } = await fuerBearbeitung(ticketId);
  requireCapability(ticket.team.mode, "prioritaet");

  await prioritaetAendern(ticketId, priority, akteur);
  aktualisiere(ticket.number);
  return { ok: true as const };
}

export async function kategorieAendernAction(
  ticketId: string,
  categoryId: string | null,
) {
  const { ticket, akteur } = await fuerBearbeitung(ticketId);
  requireCapability(ticket.team.mode, "kategorien");

  if (categoryId) {
    const gehoertZumBereich = await db.category.findFirst({
      where: { id: categoryId, teamId: ticket.teamId },
      select: { id: true },
    });
    if (!gehoertZumBereich) {
      throw new Error("Diese Kategorie gehört nicht zu diesem Bereich.");
    }
  }

  await kategorieAendern(ticketId, categoryId, akteur);
  aktualisiere(ticket.number);
  return { ok: true as const };
}

// ---------------------------------------------------------------------------
// Nachrichten
// ---------------------------------------------------------------------------

const antwortSchema = z.object({
  ticketId: z.string().min(1),
  body: z.string().trim().min(1, "Bitte schreiben Sie einen Text."),
  intern: z.boolean().default(false),
  neuerStatus: z.nativeEnum(TicketStatus).optional(),
  /** Zuvor hochgeladene Anhänge, die an diese Nachricht gehängt werden. */
  anhangIds: z.array(z.string()).default([]),
});

export async function antwortenAction(input: z.infer<typeof antwortSchema>) {
  const daten = antwortSchema.parse(input);
  const { actor, ticket, akteur, level } = await fuerAnsicht(daten.ticketId);

  // Der Melder darf antworten, aber keine internen Notizen schreiben und den
  // Status nicht setzen – sonst könnte er seinen eigenen Vorgang schließen,
  // während die Bearbeitung noch läuft.
  if (level === "requester") {
    if (daten.intern) {
      throw new Error("Interne Notizen sind der Bearbeitung vorbehalten.");
    }
    if (daten.neuerStatus && daten.neuerStatus !== TicketStatus.GELOEST) {
      throw new Error("Diesen Status können Sie nicht setzen.");
    }
  }

  if (daten.intern) {
    requireCapability(ticket.team.mode, "interneNotizen");
  }

  if (daten.neuerStatus && level === "agent") {
    const erlaubt = statusAuswahlFor(ticket.team.mode);
    if (!erlaubt.includes(daten.neuerStatus)) {
      throw new Error("Dieser Status steht in diesem Bereich nicht zur Verfügung.");
    }
  }

  const nachricht = await nachrichtHinzufuegen({
    ticketId: daten.ticketId,
    body: daten.body,
    type: daten.intern ? MessageType.INTERNAL_NOTE : MessageType.PUBLIC_REPLY,
    authorId: actor.id,
    authorName: actor.name,
    authorEmail: actor.email,
    neuerStatus: daten.neuerStatus,
    anhangIds: daten.anhangIds,
  });

  aktualisiere(ticket.number);
  return { ok: true as const, nachrichtId: nachricht.id };
}

// ---------------------------------------------------------------------------
// Weiterleiten
// ---------------------------------------------------------------------------

const weiterleitenSchema = z.object({
  ticketId: z.string().min(1),
  zielTeamId: z.string().min(1, "Bitte wählen Sie einen Bereich."),
  zielSiteId: z.string().optional().nullable(),
  begruendung: z
    .string()
    .trim()
    .min(3, "Bitte geben Sie kurz an, warum Sie weiterleiten."),
});

export async function weiterleitenAction(
  input: z.infer<typeof weiterleitenSchema>,
) {
  const daten = weiterleitenSchema.parse(input);
  const { ticket, akteur } = await fuerBearbeitung(daten.ticketId);
  requireCapability(ticket.team.mode, "weiterleiten");

  const ergebnis = await ticketWeiterleiten({
    ticketId: daten.ticketId,
    zielTeamId: daten.zielTeamId,
    zielSiteId: daten.zielSiteId || null,
    begruendung: daten.begruendung,
    actor: akteur,
  });

  aktualisiere(ticket.number);
  return { ok: true as const, ...ergebnis };
}

// ---------------------------------------------------------------------------
// Abspalten
// ---------------------------------------------------------------------------

const abspaltenSchema = z.object({
  ticketId: z.string().min(1),
  zielTeamId: z.string().min(1, "Bitte wählen Sie einen Bereich."),
  zielSiteId: z.string().optional().nullable(),
  betreff: z.string().trim().min(3, "Bitte geben Sie einen Betreff an."),
  beschreibung: z
    .string()
    .trim()
    .min(3, "Bitte beschreiben Sie, was der andere Bereich tun soll."),
  prioritaet: z.nativeEnum(Priority).optional(),
  nachrichtenIds: z.array(z.string()).default([]),
  nachbehandlung: z.enum([
    "offen_lassen",
    "warten_auf_teilvorgang",
    "schliessen",
  ]),
  abschlussnotiz: z.string().optional(),
});

export async function abspaltenAction(input: z.infer<typeof abspaltenSchema>) {
  const daten = abspaltenSchema.parse(input);
  const { ticket, akteur } = await fuerBearbeitung(daten.ticketId);
  requireCapability(ticket.team.mode, "abspalten");

  if (daten.zielTeamId === ticket.teamId) {
    throw new Error(
      "Ein Teilvorgang wird an einen anderen Bereich abgespalten. Für den " +
        "eigenen Bereich genügt eine Aufgabe im Vorgang.",
    );
  }

  const ergebnis = await ticketAbspalten({
    quellTicketId: daten.ticketId,
    zielTeamId: daten.zielTeamId,
    zielSiteId: daten.zielSiteId || null,
    betreff: daten.betreff,
    beschreibung: daten.beschreibung,
    prioritaet: daten.prioritaet,
    nachrichtenIds: daten.nachrichtenIds,
    nachbehandlung: daten.nachbehandlung as NachbehandlungElternticket,
    abschlussnotiz: daten.abschlussnotiz,
    actor: akteur,
  });

  aktualisiere(ticket.number);
  revalidatePath(`/tickets/${ergebnis.kindNummer}`);
  return { ok: true as const, ...ergebnis };
}

export async function zusammenfuehrenAction(
  ticketId: string,
  zielNummer: string,
) {
  const { actor, ticket, akteur } = await fuerBearbeitung(ticketId);
  requireCapability(ticket.team.mode, "zusammenfuehren");

  const ziel = await db.ticket.findUnique({
    where: { number: zielNummer.trim().toUpperCase() },
    select: {
      id: true,
      number: true,
      queueId: true,
      teamId: true,
      requesterId: true,
      watchers: { select: { userId: true } },
    },
  });

  if (!ziel) {
    throw new Error(`Es gibt keinen Vorgang mit der Nummer ${zielNummer}.`);
  }

  // Auch der Zielvorgang muss bearbeitet werden dürfen – sonst ließen sich
  // Nachrichten in einen fremden Bereich schieben.
  requireTicketWork(actor, ziel);

  const ergebnis = await ticketZusammenfuehren({
    quellTicketId: ticketId,
    zielTicketId: ziel.id,
    actor: akteur,
  });

  aktualisiere(ticket.number);
  revalidatePath(`/tickets/${ziel.number}`);
  return { ok: true as const, ...ergebnis };
}

// ---------------------------------------------------------------------------
// Beobachter und Aufgaben
// ---------------------------------------------------------------------------

export async function beobachterAction(
  ticketId: string,
  userId: string,
  hinzufuegen: boolean,
) {
  const { ticket, akteur } = await fuerBearbeitung(ticketId);

  if (hinzufuegen) {
    await beobachterHinzufuegen(ticketId, userId, akteur);
  } else {
    await beobachterEntfernen(ticketId, userId, akteur);
  }

  aktualisiere(ticket.number);
  return { ok: true as const };
}

export async function aufgabeUmschaltenAction(taskId: string, done: boolean) {
  const actor = await requireActor();

  const aufgabe = await db.ticketTask.findUnique({
    where: { id: taskId },
    select: {
      id: true,
      ticket: {
        select: {
          id: true,
          number: true,
          queueId: true,
          teamId: true,
          requesterId: true,
          team: { select: { mode: true } },
          watchers: { select: { userId: true } },
        },
      },
    },
  });

  if (!aufgabe) throw new Error("Die Aufgabe wurde nicht gefunden.");

  requireTicketWork(actor, aufgabe.ticket);
  requireCapability(aufgabe.ticket.team.mode, "checklisten");

  await db.ticketTask.update({
    where: { id: taskId },
    data: { done, doneAt: done ? new Date() : null },
  });

  aktualisiere(aufgabe.ticket.number);
  return { ok: true as const };
}

export async function aufgabeAnlegenAction(ticketId: string, titel: string) {
  const { ticket } = await fuerBearbeitung(ticketId);
  requireCapability(ticket.team.mode, "checklisten");

  const text = titel.trim();
  if (!text) throw new Error("Bitte geben Sie der Aufgabe einen Namen.");

  const letzte = await db.ticketTask.findFirst({
    where: { ticketId },
    orderBy: { sortOrder: "desc" },
    select: { sortOrder: true },
  });

  await db.ticketTask.create({
    data: { ticketId, title: text, sortOrder: (letzte?.sortOrder ?? -1) + 1 },
  });

  aktualisiere(ticket.number);
  return { ok: true as const };
}

/** Personen, an die dieser Vorgang zugewiesen werden kann. */
export async function ladeZuweisbare(ticketId: string) {
  const { ticket } = await fuerBearbeitung(ticketId);

  const personen = await db.user.findMany({
    where: {
      isActive: true,
      isExternal: false,
      memberships: { some: { teamId: ticket.teamId } },
      OR: [
        { queueAccess: { some: { queueId: ticket.queueId } } },
        { memberships: { some: { teamId: ticket.teamId, role: "LEAD" } } },
      ],
    },
    select: { id: true, name: true, email: true },
    orderBy: { name: "asc" },
    take: 100,
  });

  return personen;
}

/** Bereiche, an die weitergeleitet oder abgespalten werden kann. */
export async function ladeZielBereiche(ticketId: string) {
  const { ticket } = await fuerBearbeitung(ticketId);

  const bereiche = await db.team.findMany({
    where: { isActive: true, id: { not: ticket.teamId } },
    select: {
      id: true,
      name: true,
      key: true,
      color: true,
      icon: true,
      mode: true,
      queueMode: true,
      description: true,
      queues: {
        where: { isActive: true },
        select: {
          id: true,
          siteId: true,
          site: { select: { id: true, name: true, shortName: true } },
        },
        orderBy: { sortOrder: "asc" },
      },
    },
    orderBy: [{ sortOrder: "asc" }, { name: "asc" }],
  });

  // Bereiche ohne nutzbaren Eingang gar nicht erst anbieten – sonst schlägt
  // die Weiterleitung erst beim Absenden fehl.
  return bereiche.filter((b) => b.queues.length > 0);
}
