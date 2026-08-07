"use server";

import { revalidatePath } from "next/cache";

import { db } from "@/server/db";
import { requireActor, requireTicketView } from "@/server/policy";
import {
  entferneAnhang,
  maxUploadBytes,
  speichereAnhang,
  UploadFehler,
  type GespeicherterAnhang,
} from "@/server/services/attachments";
import { logEvent } from "@/server/services/events";

/**
 * Dateien hochladen.
 *
 * Zwei Wege, die sich nur darin unterscheiden, wann der Vorgang existiert:
 *
 *   - Beim Melden gibt es noch kein Ticket. Die Datei wird ohne Zuordnung
 *     abgelegt und beim Absenden nachgetragen; nicht zugeordnete Anhänge räumt
 *     der Hintergrunddienst nach 24 Stunden ab.
 *   - Beim Antworten auf einen bestehenden Vorgang wird sofort zugeordnet.
 *
 * Die Prüfung von Größe und Art passiert in `attachments.ts` und damit auch
 * für Anhänge aus eingehenden Mails.
 */

export interface UploadErgebnis {
  ok: boolean;
  anhang?: GespeicherterAnhang;
  fehler?: string;
}

export async function hochladenAction(
  formData: FormData,
): Promise<UploadErgebnis> {
  const actor = await requireActor();

  const datei = formData.get("datei");
  if (!(datei instanceof File)) {
    return { ok: false, fehler: "Es wurde keine Datei übermittelt." };
  }

  // Vorabprüfung der Größe, bevor die Datei in den Speicher gelesen wird.
  if (datei.size > maxUploadBytes()) {
    return {
      ok: false,
      fehler: `Die Datei ist größer als ${Math.round(maxUploadBytes() / 1024 / 1024)} MB.`,
    };
  }

  const ticketId = formData.get("ticketId");

  // Bei einem bestehenden Vorgang zuerst prüfen, ob überhaupt Zugriff besteht –
  // sonst ließe sich über einen Anhang eine fremde Vorgangskennung befüllen.
  let zuordnung: string | null = null;
  if (typeof ticketId === "string" && ticketId) {
    const ticket = await db.ticket.findUnique({
      where: { id: ticketId },
      select: {
        id: true,
        number: true,
        queueId: true,
        teamId: true,
        requesterId: true,
        watchers: { select: { userId: true } },
      },
    });
    if (!ticket) return { ok: false, fehler: "Der Vorgang wurde nicht gefunden." };

    requireTicketView(actor, ticket);
    zuordnung = ticket.id;
  }

  try {
    const inhalt = Buffer.from(await datei.arrayBuffer());

    const anhang = await speichereAnhang({
      inhalt,
      filename: datei.name,
      mimeType: datei.type || "application/octet-stream",
      ticketId: zuordnung,
      uploadedById: actor.id,
    });

    if (zuordnung) {
      await logEvent(db, {
        ticketId: zuordnung,
        type: "ATTACHMENT_ADDED",
        actorId: actor.id,
        actorName: actor.name,
        toValue: anhang.filename,
      });
    }

    return { ok: true, anhang };
  } catch (error) {
    if (error instanceof UploadFehler) {
      return { ok: false, fehler: error.message };
    }
    console.error("[Upload] Speichern fehlgeschlagen:", error);
    return {
      ok: false,
      fehler: "Die Datei konnte nicht gespeichert werden. Bitte erneut versuchen.",
    };
  }
}

/**
 * Entfernt einen Anhang.
 *
 * Erlaubt für die hochladende Person selbst und für alle, die den Vorgang
 * bearbeiten dürfen.
 */
export async function anhangEntfernenAction(attachmentId: string) {
  const actor = await requireActor();

  const anhang = await db.attachment.findUnique({
    where: { id: attachmentId },
    select: {
      id: true,
      filename: true,
      uploadedById: true,
      ticket: {
        select: {
          id: true,
          number: true,
          queueId: true,
          teamId: true,
          requesterId: true,
          watchers: { select: { userId: true } },
        },
      },
    },
  });

  if (!anhang) return { ok: true as const };

  if (anhang.ticket) {
    requireTicketView(actor, anhang.ticket);
  } else if (anhang.uploadedById !== actor.id) {
    // Noch nicht zugeordnete Anhänge gehören nur der hochladenden Person.
    throw new Error("Diesen Anhang können Sie nicht entfernen.");
  }

  await entferneAnhang(attachmentId);

  if (anhang.ticket) {
    revalidatePath(`/tickets/${anhang.ticket.number}`);
  }

  return { ok: true as const };
}
