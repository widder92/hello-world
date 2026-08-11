import nodemailer from "nodemailer";

import { buildReplyAddress } from "@/lib/crypto";

/**
 * E-Mail-Versand.
 *
 * Drei Wege, über `MAIL_TRANSPORT` umschaltbar:
 *   smtp   der Regelfall, funktioniert mit Exchange, Microsoft 365 und jedem
 *          anderen Mailserver
 *   graph  Versand über Microsoft 365 ohne SMTP-Freischaltung
 *   none   nichts geht hinaus, alles wird nur protokolliert – für Tests und
 *          für die erste Inbetriebnahme, damit ein Import nicht versehentlich
 *          1000 Benachrichtigungen auslöst
 *
 * Jede ausgehende Nachricht bekommt eine Antwortadresse mit signiertem Token.
 * Antwortet jemand darauf, landet der Text am richtigen Vorgang – und nur mit
 * gültigem Token werden Steuerbefehle ausgeführt.
 */

export interface AusgehendeMail {
  an: string;
  anName?: string | null;
  cc?: string[];
  betreff: string;
  text: string;
  /** Für die Antwortadresse mit signiertem Token. */
  ticketId: string;
  empfaengerId: string;
  /** Message-ID der Nachricht, auf die geantwortet wird. */
  inReplyTo?: string | null;
  references?: string[];
}

export interface VersandErgebnis {
  messageId: string | null;
  versendet: boolean;
  grund?: string;
}

let transporter: nodemailer.Transporter | null = null;

function smtpTransport(): nodemailer.Transporter {
  if (transporter) return transporter;

  transporter = nodemailer.createTransport({
    host: process.env.SMTP_HOST || "localhost",
    port: Number(process.env.SMTP_PORT || 25),
    secure: process.env.SMTP_SECURE === "true",
    auth: process.env.SMTP_USER
      ? {
          user: process.env.SMTP_USER,
          pass: process.env.SMTP_PASSWORD || "",
        }
      : undefined,
    // Ein Mailserver, der gerade nicht antwortet, darf die Oberfläche nicht
    // minutenlang blockieren.
    connectionTimeout: 10_000,
    greetingTimeout: 10_000,
    socketTimeout: 20_000,
  });

  return transporter;
}

function absender(): string {
  const name = process.env.MAIL_FROM_NAME || "Ticketsystem";
  const adresse = process.env.MAIL_FROM_ADDRESS || "ticketsystem@localhost";
  return `"${name}" <${adresse}>`;
}

/**
 * Setzt die Ticketkennung in den Betreff.
 *
 * Zweiter Anker neben den Mail-Kopfzeilen: Manche Mailprogramme – vor allem
 * Weiterleitungen über Verteiler – verlieren `In-Reply-To`. Die Kennung im
 * Betreff überlebt das fast immer.
 */
export function betreffMitKennung(betreff: string, ticketNumber: string): string {
  if (betreff.includes(`[${ticketNumber}]`)) return betreff;
  return `[${ticketNumber}] ${betreff}`;
}

export async function sendeMail(
  mail: AusgehendeMail,
  ticketNumber: string,
): Promise<VersandErgebnis> {
  const transport = process.env.MAIL_TRANSPORT || "smtp";

  if (transport === "none") {
    console.info(
      `[Mail] Versand abgeschaltet (MAIL_TRANSPORT=none). ` +
        `Hätte an ${mail.an} gesendet: ${mail.betreff}`,
    );
    return { messageId: null, versendet: false, grund: "Versand abgeschaltet" };
  }

  const replyTo = buildReplyAddress({
    ticketId: mail.ticketId,
    recipientId: mail.empfaengerId,
  });

  const kopfzeilen: Record<string, string> = {
    // Kennzeichnet die Nachricht als automatisch erzeugt. Ordentliche
    // Abwesenheitsassistenten antworten darauf nicht – das verhindert
    // Endlosschleifen zwischen zwei Automaten.
    "Auto-Submitted": "auto-generated",
    "X-Ticket-Number": ticketNumber,
  };

  const nachricht = {
    from: absender(),
    to: mail.anName ? `"${mail.anName}" <${mail.an}>` : mail.an,
    cc: mail.cc?.length ? mail.cc : undefined,
    replyTo: replyTo ?? undefined,
    subject: betreffMitKennung(mail.betreff, ticketNumber),
    text: mail.text,
    inReplyTo: mail.inReplyTo ?? undefined,
    references: mail.references?.length ? mail.references : undefined,
    headers: kopfzeilen,
  };

  try {
    if (transport === "graph") {
      const { sendeUeberGraph } = await import("@/mail/graph");
      const id = await sendeUeberGraph(nachricht);
      return { messageId: id, versendet: true };
    }

    const info = await smtpTransport().sendMail(nachricht);
    return { messageId: info.messageId ?? null, versendet: true };
  } catch (error) {
    // Ein fehlgeschlagener Versand darf den Vorgang im System nicht
    // zurückdrehen – die Nachricht steht im Ticket, der Melder sieht sie im
    // Portal. Der Fehler wird protokolliert, damit er auffällt.
    console.error(`[Mail] Versand an ${mail.an} fehlgeschlagen:`, error);
    return {
      messageId: null,
      versendet: false,
      grund: error instanceof Error ? error.message : "Unbekannter Fehler",
    };
  }
}

/** Baut den Nachrichtentext für eine Antwort an den Melder. */
export function antwortText(opts: {
  empfaengerName: string;
  ticketNumber: string;
  betreff: string;
  text: string;
  autorName: string;
  appUrl: string;
}): string {
  return [
    opts.text.trim(),
    "",
    "—",
    `${opts.autorName}`,
    "",
    `Vorgang ${opts.ticketNumber}: ${opts.betreff}`,
    `Stand ansehen: ${opts.appUrl}/tickets/${opts.ticketNumber}`,
    "",
    "Sie können direkt auf diese E-Mail antworten – Ihre Antwort wird dem",
    "Vorgang zugeordnet.",
  ].join("\n");
}
