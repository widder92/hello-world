"use server";

import { revalidatePath } from "next/cache";
import { Priority, QueueMode, TicketSource } from "@prisma/client";
import { z } from "zod";

import { db } from "@/server/db";
import { isAgent, requireActor, type Actor } from "@/server/policy";
import { benachrichtige } from "@/server/services/notify";
import { ticketAnlegen, ticketUebernehmen } from "@/server/services/ticket";

/**
 * Einen Vorgang für eine andere Person aufnehmen.
 *
 * Zwei Situationen aus dem Alltag, für die das Melder-Portal nicht taugt:
 *
 *  1. Jemand ruft an, statt zu schreiben. Nicht jede Kollegin kommt mit einem
 *     Formular zurecht, und manche Anliegen erklären sich am Telefon in zwei
 *     Sätzen. Bisher blieb nur, die Person zum Selbstschreiben zu drängen –
 *     oder den Vorgang gar nicht zu erfassen. Beides ist schlecht: Das eine
 *     hilft niemandem, das andere macht die Auswertung wertlos, weil die
 *     telefonische Arbeit nirgends auftaucht.
 *
 *  2. Man dachte, es sei am Telefon erledigt, und merkt später, dass mehr
 *     dahintersteckt. Dann braucht es den Vorgang nachträglich – mitsamt dem,
 *     was schon besprochen wurde.
 *
 * Der aufgenommene Vorgang gehört dem Melder, nicht dem Aufnehmenden: Er sieht
 * ihn unter seinen Meldungen, bekommt die Benachrichtigungen und kann per
 * Antwortmail mitreden. Wer ihn aufgenommen hat, steht im Verlauf – damit
 * später nachvollziehbar bleibt, dass der Text nicht vom Melder selbst stammt.
 */

const aufnahmeSchema = z
  .object({
    /** Bestehendes Konto als Melder. */
    melderId: z.string().optional().or(z.literal("")),
    /** Alternativ: Person ohne Konto, mit Namen und Adresse erfasst. */
    melderName: z.string().trim().max(120).optional().or(z.literal("")),
    melderEmail: z
      .string()
      .trim()
      .toLowerCase()
      .email("Bitte eine gültige E-Mail-Adresse angeben.")
      .optional()
      .or(z.literal("")),

    teamId: z.string().min(1, "Bitte wählen Sie den zuständigen Bereich."),
    siteId: z.string().optional().or(z.literal("")),
    categoryId: z.string().optional().or(z.literal("")),

    subject: z
      .string()
      .trim()
      .min(5, "Bitte geben Sie einen Betreff mit mindestens 5 Zeichen an.")
      .max(200),
    body: z
      .string()
      .trim()
      .min(10, "Bitte halten Sie fest, worum es geht."),
    roomOrArea: z.string().trim().max(120).optional().or(z.literal("")),
    priority: z.nativeEnum(Priority).optional(),

    /** Gleich selbst übernehmen – der Regelfall, wenn man schon dran ist. */
    mirZuweisen: z.boolean().default(true),
    /** Bestätigung an den Melder, damit er die Nummer hat. */
    melderInformieren: z.boolean().default(true),
  })
  .refine((d) => d.melderId || (d.melderName && d.melderEmail), {
    message:
      "Bitte wählen Sie den Melder aus oder erfassen Sie Namen und " +
      "E-Mail-Adresse.",
    path: ["melderId"],
  });

export type AufnahmeInput = z.input<typeof aufnahmeSchema>;

/**
 * Bereiche, in die aufgenommen werden darf.
 *
 * Die eigenen Bereiche immer – dort arbeitet man ja. Dazu die, die auch im
 * Portal offenstehen, damit sich ein Anruf direkt an den Hausmeister
 * weitergeben lässt. Bereiche, die bewusst geschlossen sind (etwa die
 * Personalabteilung), bleiben es auch hier: Sonst ließe sich die
 * Portal-Einstellung über diesen Weg aushebeln.
 */
function bereichsFilter(actor: Actor) {
  const eigene = actor.memberships.map((m) => m.teamId);
  return actor.isAdmin
    ? { isActive: true }
    : {
        isActive: true,
        OR: [{ portalVisible: true }, { id: { in: eigene } }],
      };
}

export async function bereicheFuerAufnahme() {
  const actor = await requireActor();
  if (!isAgent(actor)) return [];

  return db.team.findMany({
    where: bereichsFilter(actor),
    orderBy: [{ sortOrder: "asc" }, { name: "asc" }],
    select: {
      id: true,
      name: true,
      mode: true,
      queueMode: true,
      color: true,
      icon: true,
      queues: {
        where: { isActive: true },
        orderBy: { sortOrder: "asc" },
        select: { id: true, site: { select: { id: true, name: true } } },
      },
      categories: {
        where: { isActive: true, parentId: null },
        orderBy: [{ sortOrder: "asc" }, { name: "asc" }],
        select: { id: true, name: true },
      },
    },
  });
}

/** Alle aktiven Standorte – für Bereiche mit einem gemeinsamen Eingang. */
export async function standorteFuerAufnahme() {
  await requireActor();
  return db.site.findMany({
    where: { isActive: true },
    orderBy: [{ sortOrder: "asc" }, { name: "asc" }],
    select: { id: true, name: true, city: true },
  });
}

/**
 * Melder suchen.
 *
 * Über Namen und Adresse, damit man am Telefon mit dem findet, was die Person
 * gerade sagt – „Bergmann" genügt.
 */
export async function sucheMelder(begriff: string) {
  const actor = await requireActor();
  if (!isAgent(actor)) return [];

  const suche = begriff.trim();
  if (suche.length < 2) return [];

  return db.user.findMany({
    where: {
      isActive: true,
      OR: [
        { name: { contains: suche, mode: "insensitive" } },
        { email: { contains: suche, mode: "insensitive" } },
      ],
    },
    orderBy: { name: "asc" },
    take: 8,
    select: {
      id: true,
      name: true,
      email: true,
      isExternal: true,
      site: { select: { id: true, name: true } },
    },
  });
}

export async function vorgangAufnehmenAction(input: AufnahmeInput) {
  const actor = await requireActor();
  if (!isAgent(actor)) {
    throw new Error(
      "Vorgänge für andere aufnehmen darf nur, wer in einem Bereich arbeitet.",
    );
  }

  const daten = aufnahmeSchema.parse(input);

  const team = await db.team.findFirst({
    where: { ...bereichsFilter(actor), id: daten.teamId },
    select: { id: true, name: true, mode: true, queueMode: true },
  });
  if (!team) {
    throw new Error("In diesen Bereich können Sie keinen Vorgang aufnehmen.");
  }

  if (team.queueMode === QueueMode.PER_SITE && !daten.siteId) {
    throw new Error(
      `Der Bereich ${team.name} hat je Standort einen eigenen Eingang – ` +
        `bitte den betroffenen Standort wählen.`,
    );
  }

  if (daten.categoryId) {
    const kategorie = await db.category.findFirst({
      where: { id: daten.categoryId, teamId: team.id, isActive: true },
      select: { id: true },
    });
    if (!kategorie) {
      throw new Error("Diese Kategorie gehört nicht zum gewählten Bereich.");
    }
  }

  // --- Melder bestimmen ----------------------------------------------------
  let melder: { id: string; name: string; siteId: string | null };

  if (daten.melderId) {
    const vorhanden = await db.user.findFirst({
      where: { id: daten.melderId, isActive: true },
      select: { id: true, name: true, siteId: true },
    });
    if (!vorhanden) {
      throw new Error("Der gewählte Melder wurde nicht gefunden.");
    }
    melder = vorhanden;
  } else {
    // Person ohne Konto. Gibt es die Adresse schon, wird sie wiederverwendet –
    // sonst entstünde bei jedem Anruf ein neuer Melder mit derselben Adresse,
    // und niemand fände später seine Vorgänge beisammen.
    const email = daten.melderEmail!;
    const bestehend = await db.user.findUnique({
      where: { email },
      select: { id: true, name: true, siteId: true, isActive: true },
    });

    if (bestehend?.isActive) {
      melder = bestehend;
    } else if (bestehend) {
      throw new Error(
        `Zu ${email} gibt es ein stillgelegtes Konto. Bitte erst in der ` +
          `Verwaltung wieder aktivieren.`,
      );
    } else {
      melder = await db.user.create({
        data: {
          email,
          name: daten.melderName!,
          // Kein Kennwort und als extern markiert: Diese Person kann sich
          // nicht anmelden, bekommt aber Antworten per Mail.
          isExternal: true,
          isActive: true,
        },
        select: { id: true, name: true, siteId: true },
      });
    }
  }

  // Ohne ausdrücklichen Standort den des Melders nehmen – bei einem Anruf aus
  // dem Wohnheim ist das fast immer der richtige.
  const siteId = daten.siteId || melder.siteId || null;

  const ticket = await ticketAnlegen({
    teamId: team.id,
    siteId,
    subject: daten.subject,
    body: daten.body,
    requesterId: melder.id,
    categoryId: daten.categoryId || null,
    roomOrArea: daten.roomOrArea || null,
    priority: team.mode === "VOLL" ? (daten.priority ?? Priority.NORMAL) : undefined,
    source: TicketSource.TELEFON,
    actor: { id: actor.id, name: actor.name },
  });

  // Im Verlauf festhalten, dass der Text nicht vom Melder stammt. Ohne diesen
  // Vermerk läse sich der Vorgang, als hätte er ihn selbst geschrieben – und
  // bei einer Rückfrage stünde die falsche Person im Raum.
  await db.ticketMessage.create({
    data: {
      ticketId: ticket.id,
      type: "SYSTEM",
      authorId: actor.id,
      authorName: actor.name,
      body:
        `Telefonisch aufgenommen von ${actor.name} für ${melder.name}. ` +
        `Der Text oben ist die Mitschrift des Gesprächs.`,
      bodyText: `Telefonisch aufgenommen von ${actor.name}.`,
    },
  });

  if (daten.mirZuweisen) {
    await ticketUebernehmen(ticket.id, { id: actor.id, name: actor.name });
  }

  if (daten.melderInformieren) {
    await benachrichtige.eingangsbestaetigung(ticket.id);
  }

  revalidatePath("/tickets");
  revalidatePath("/dashboard");

  return { nummer: ticket.number };
}
