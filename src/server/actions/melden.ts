"use server";

import { revalidatePath } from "next/cache";
import { Impact, TicketSource, Urgency } from "@prisma/client";
import { z } from "zod";

import { db } from "@/server/db";
import { requireActor } from "@/server/policy";
import { ordneAnhaengeZu } from "@/server/services/attachments";
import { benachrichtige } from "@/server/services/notify";
import { ticketAnlegen } from "@/server/services/ticket";
import { suggestPriority } from "@/lib/domain";

/**
 * Eine Meldung aus dem Portal aufgeben.
 *
 * Der Weg, den die allermeisten Mitarbeitenden nehmen. Bewusst kurz: Bereich,
 * Betreff, Anliegen, Standort. Alles Weitere – Priorität, Kategorie, Frist –
 * ergibt sich daraus oder setzt der Bereich beim Bearbeiten.
 */

const meldenSchema = z.object({
  teamId: z.string().min(1, "Bitte wählen Sie, an wen die Meldung gehen soll."),
  siteId: z.string().min(1, "Bitte wählen Sie den Standort."),
  categoryId: z.string().optional().or(z.literal("")),
  subject: z
    .string()
    .trim()
    .min(5, "Bitte geben Sie einen Betreff mit mindestens 5 Zeichen an.")
    .max(200),
  body: z
    .string()
    .trim()
    .min(10, "Bitte beschreiben Sie Ihr Anliegen etwas ausführlicher."),
  roomOrArea: z.string().trim().max(120).optional().or(z.literal("")),
  urgency: z.nativeEnum(Urgency).optional(),
  impact: z.nativeEnum(Impact).optional(),
  /** Antworten auf die Zusatzfelder der Kategorie. */
  formData: z.record(z.string(), z.union([z.string(), z.boolean()])).optional(),
  /** Zuvor hochgeladene Anhänge – etwa das Foto vom Schaden. */
  anhangIds: z.array(z.string()).default([]),
});

export type MeldenInput = z.infer<typeof meldenSchema>;

export async function meldungAufgebenAction(input: MeldenInput) {
  const actor = await requireActor();
  const daten = meldenSchema.parse(input);

  // Nur Bereiche, die Selbstmeldungen entgegennehmen. Sonst ließe sich durch
  // Untermogeln einer Kennung in einen Bereich schreiben, der bewusst
  // geschlossen ist – etwa die Personalabteilung.
  const team = await db.team.findFirst({
    where: { id: daten.teamId, isActive: true, portalVisible: true },
    select: { id: true, name: true },
  });
  if (!team) {
    throw new Error(
      "Dieser Bereich nimmt keine Meldungen über das Portal entgegen.",
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

  const ticket = await ticketAnlegen({
    teamId: team.id,
    siteId: daten.siteId,
    subject: daten.subject,
    body: daten.body,
    requesterId: actor.id,
    categoryId: daten.categoryId || null,
    roomOrArea: daten.roomOrArea || null,
    impact: daten.impact,
    urgency: daten.urgency,
    // Aus Auswirkung und Dringlichkeit ergibt sich ein Vorschlag; der Bereich
    // kann ihn beim Bearbeiten jederzeit überschreiben.
    priority: suggestPriority(daten.impact, daten.urgency),
    source: TicketSource.WEB,
    formData: daten.formData,
    actor: { id: actor.id, name: actor.name },
  });

  // Die Anhänge wurden beim Ausfüllen hochgeladen und hingen bis hierhin an
  // keinem Vorgang – jetzt bekommen sie ihre Zuordnung.
  if (daten.anhangIds.length) {
    const ersteNachricht = await db.ticketMessage.findFirst({
      where: { ticketId: ticket.id },
      orderBy: { createdAt: "asc" },
      select: { id: true },
    });

    await ordneAnhaengeZu({
      attachmentIds: daten.anhangIds,
      ticketId: ticket.id,
      messageId: ersteNachricht?.id ?? null,
      uploadedById: actor.id,
    });
  }

  await benachrichtige.eingangsbestaetigung(ticket.id);

  revalidatePath("/meine-tickets");
  revalidatePath("/", "layout");

  return { ok: true as const, nummer: ticket.number };
}

/**
 * Vorschläge aus der Wissensdatenbank zum eingegebenen Text.
 *
 * Wird beim Tippen im Meldeformular aufgerufen. Manche Anliegen lassen sich
 * damit lösen, bevor überhaupt ein Ticket entsteht – das entlastet beide
 * Seiten.
 */
export async function wissensVorschlaege(begriff: string, teamId?: string) {
  await requireActor();

  const text = begriff.trim();
  if (text.length < 4) return [];

  // Volltextsuche mit deutscher Wortstammerkennung: "Drucker druckt nicht"
  // findet auch "Der Drucker druckte gestern noch".
  //
  // Bei gewähltem Bereich werden dessen Artikel bevorzugt, allgemeine Artikel
  // ohne Bereichsbezug aber weiterhin gezeigt – „So schreiben Sie ein Ticket"
  // hilft unabhängig davon, an wen die Meldung geht.
  const treffer = await db.$queryRaw<
    { id: string; slug: string; title: string; summary: string | null }[]
  >`
    SELECT "id", "slug", "title", "summary"
    FROM "KbArticle"
    WHERE "published" = true
      AND "visibility" = 'OEFFENTLICH'
      AND ("teamId" IS NULL OR ${teamId ?? null}::text IS NULL OR "teamId" = ${teamId ?? null}::text)
      AND "searchVector" @@ websearch_to_tsquery('german', ${text})
    ORDER BY
      ("teamId" = ${teamId ?? null}::text) DESC NULLS LAST,
      ts_rank("searchVector", websearch_to_tsquery('german', ${text})) DESC
    LIMIT 3
  `;

  return treffer;
}
