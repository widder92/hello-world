import { db } from "@/server/db";
import { extractReplyToken, verifyReplyToken } from "@/lib/crypto";
import { findTicketNumber } from "@/server/services/number";
import type { EingehendeNachricht } from "@/mail/types";

/**
 * Zuordnung eingehender Mails zu bestehenden Vorgängen.
 *
 * Der wichtigste Punkt der ganzen Mail-Anbindung: Antwortet jemand auf eine
 * Benachrichtigung, muss der Text am selben Vorgang landen. Entsteht
 * stattdessen ein neues Ticket, hat man nach drei Antworten vier Vorgänge zu
 * derselben Sache – genau das Problem, das ein Ticketsystem lösen soll.
 *
 * Vier Wege, in dieser Reihenfolge:
 *
 *   1. Signierte Antwortadresse (ticket+<token>@…) – der zuverlässigste Weg,
 *      weil er unabhängig von Betreff und Kopfzeilen funktioniert und
 *      zusätzlich beweist, dass der Absender diese Mail bekommen hat.
 *   2. In-Reply-To / References gegen gespeicherte Message-IDs.
 *   3. Ticketkennung im Betreff ([HM-2026-000087]).
 *   4. Message-ID, aus der der Vorgang ursprünglich entstand.
 *
 * Greift nichts davon, entsteht ein neuer Vorgang.
 */

export interface Zuordnung {
  ticketId: string;
  ticketNumber: string;
  /** Wie die Zuordnung zustande kam – wird protokolliert. */
  weg: "token" | "kopfzeile" | "betreff" | "herkunft";
  /**
   * Bei der Zuordnung über ein signiertes Token: die Person, an die diese
   * Mail ursprünglich ging. Nur dann dürfen Steuerbefehle laufen.
   */
  bestaetigterEmpfaengerId: string | null;
}

export async function findeVorgang(
  mail: EingehendeNachricht,
): Promise<Zuordnung | null> {
  // --- 1. Signierte Antwortadresse ----------------------------------------
  for (const adresse of [...mail.anAdressen, ...mail.ccAdressen]) {
    const token = extractReplyToken(adresse);
    if (!token) continue;

    const inhalt = verifyReplyToken(token);
    if (!inhalt) {
      // Ein Token, dessen Signatur nicht passt, ist ein Warnzeichen: entweder
      // ein Tippfehler oder ein Fälschungsversuch. Die Mail wird deshalb wie
      // eine ohne Token behandelt, nicht dem Vorgang zugeordnet.
      continue;
    }

    const ticket = await db.ticket.findUnique({
      where: { id: inhalt.ticketId },
      select: { id: true, number: true },
    });
    if (ticket) {
      return {
        ticketId: ticket.id,
        ticketNumber: ticket.number,
        weg: "token",
        bestaetigterEmpfaengerId: inhalt.recipientId,
      };
    }
  }

  // --- 2. Kopfzeilen der Mailverkettung ------------------------------------
  const kandidaten = [mail.inReplyTo, ...mail.references].filter(
    (id): id is string => Boolean(id),
  );

  if (kandidaten.length > 0) {
    const nachricht = await db.ticketMessage.findFirst({
      where: { emailMessageId: { in: kandidaten } },
      select: { ticket: { select: { id: true, number: true } } },
    });
    if (nachricht) {
      return {
        ticketId: nachricht.ticket.id,
        ticketNumber: nachricht.ticket.number,
        weg: "kopfzeile",
        bestaetigterEmpfaengerId: null,
      };
    }
  }

  // --- 3. Ticketkennung im Betreff -----------------------------------------
  const nummer = findTicketNumber(mail.betreff);
  if (nummer) {
    const ticket = await db.ticket.findUnique({
      where: { number: nummer },
      select: { id: true, number: true },
    });
    if (ticket) {
      return {
        ticketId: ticket.id,
        ticketNumber: ticket.number,
        weg: "betreff",
        bestaetigterEmpfaengerId: null,
      };
    }
  }

  // --- 4. Ursprungsmail des Vorgangs ---------------------------------------
  if (mail.inReplyTo) {
    const ticket = await db.ticket.findFirst({
      where: { originEmailMessageId: mail.inReplyTo },
      select: { id: true, number: true },
    });
    if (ticket) {
      return {
        ticketId: ticket.id,
        ticketNumber: ticket.number,
        weg: "herkunft",
        bestaetigterEmpfaengerId: null,
      };
    }
  }

  return null;
}

/**
 * Wurde diese Mail schon einmal verarbeitet?
 *
 * Schutz gegen Doppelverarbeitung, etwa wenn der Abrufdienst neu startet,
 * bevor er die Nachricht als gelesen markieren konnte.
 */
export async function bereitsVerarbeitet(messageId: string | null): Promise<boolean> {
  if (!messageId) return false;

  const [nachricht, ticket] = await Promise.all([
    db.ticketMessage.findUnique({
      where: { emailMessageId: messageId },
      select: { id: true },
    }),
    db.ticket.findFirst({
      where: { originEmailMessageId: messageId },
      select: { id: true },
    }),
  ]);

  return Boolean(nachricht || ticket);
}
