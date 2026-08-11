import { ImapFlow } from "imapflow";
import { simpleParser } from "mailparser";

import type { EingehendeNachricht, MailSource } from "@/mail/types";

/**
 * Postfachanbindung über IMAP.
 *
 * Funktioniert mit Exchange, Microsoft 365 und jedem anderen Mailserver – die
 * Anbindung, mit der man in jedem Haus anfangen kann. Für Microsoft 365 ist
 * die Graph-Anbindung langfristig der bessere Weg, weil Microsoft die
 * Basisauthentifizierung zunehmend abschaltet.
 *
 * Verarbeitete Nachrichten werden als gelesen markiert und, falls
 * eingerichtet, in einen Ordner verschoben. Zusätzlich merkt sich das System
 * die höchste verarbeitete UID – so entsteht auch dann kein doppeltes Ticket,
 * wenn jemand eine Mail im Postfach versehentlich wieder auf ungelesen setzt.
 */

export interface ImapZugang {
  host: string;
  port: number;
  secure: boolean;
  user: string;
  password: string;
  folder: string;
  /** Höchste bereits verarbeitete UID. */
  lastUid?: number | null;
}

export function createImapSource(zugang: ImapZugang): MailSource {
  async function verbinden(): Promise<ImapFlow> {
    const client = new ImapFlow({
      host: zugang.host,
      port: zugang.port,
      secure: zugang.secure,
      auth: { user: zugang.user, pass: zugang.password },
      // Die Bibliothek protokolliert sonst jede IMAP-Zeile mit; das würde die
      // Protokolldatei mit Postfachinhalten fluten.
      logger: false,
      socketTimeout: 30_000,
    });
    await client.connect();
    return client;
  }

  return {
    async holeNeue(limit = 25): Promise<EingehendeNachricht[]> {
      const client = await verbinden();
      const ergebnis: EingehendeNachricht[] = [];

      try {
        const lock = await client.getMailboxLock(zugang.folder);
        try {
          // Ungelesene Nachrichten oberhalb der zuletzt verarbeiteten UID.
          const suche = zugang.lastUid
            ? { uid: `${zugang.lastUid + 1}:*`, seen: false }
            : { seen: false };

          let gezaehlt = 0;

          for await (const nachricht of client.fetch(suche, {
            uid: true,
            source: true,
            envelope: true,
          })) {
            if (gezaehlt >= limit) break;
            gezaehlt++;

            // Bei "uid: n:*" liefert IMAP mindestens eine Nachricht zurück,
            // auch wenn keine neuere existiert – die bereits verarbeitete
            // hier überspringen.
            if (zugang.lastUid && nachricht.uid <= zugang.lastUid) continue;

            // Ohne Quelltext lässt sich nichts auswerten; das kommt vor, wenn
            // die Nachricht zwischen Auflisten und Abrufen verschoben wurde.
            if (!nachricht.source) continue;

            const geparst = await simpleParser(nachricht.source);

            const empfaenger = (adressen: unknown): string[] => {
              const feld = adressen as
                | { value?: { address?: string }[] }
                | { value?: { address?: string }[] }[]
                | undefined;
              if (!feld) return [];
              const liste = Array.isArray(feld) ? feld : [feld];
              return liste
                .flatMap((f) => f.value ?? [])
                .map((v) => v.address?.toLowerCase() ?? "")
                .filter(Boolean);
            };

            ergebnis.push({
              quellId: String(nachricht.uid),
              messageId: geparst.messageId ?? null,
              inReplyTo: geparst.inReplyTo ?? null,
              references: Array.isArray(geparst.references)
                ? geparst.references
                : geparst.references
                  ? [geparst.references]
                  : [],
              betreff: geparst.subject ?? "(ohne Betreff)",
              vonAdresse: geparst.from?.value?.[0]?.address?.toLowerCase() ?? "",
              vonName: geparst.from?.value?.[0]?.name ?? null,
              anAdressen: empfaenger(geparst.to),
              ccAdressen: empfaenger(geparst.cc),
              text: geparst.text ?? null,
              html: typeof geparst.html === "string" ? geparst.html : null,
              empfangenAm: geparst.date ?? new Date(),
              autoSubmitted:
                (geparst.headers.get("auto-submitted") as string | undefined) ??
                null,
              precedence:
                (geparst.headers.get("precedence") as string | undefined) ?? null,
              anhaenge: (geparst.attachments ?? []).map((a) => ({
                filename: a.filename ?? "anhang",
                mimeType: a.contentType || "application/octet-stream",
                inhalt: a.content as Buffer,
                inline: a.contentDisposition === "inline",
              })),
            });
          }
        } finally {
          lock.release();
        }
      } finally {
        await client.logout().catch(() => {
          // Ein misslungenes Abmelden darf die bereits geholten Nachrichten
          // nicht verwerfen.
        });
      }

      return ergebnis;
    },

    async markiereVerarbeitet(quellId: string, zielOrdner?: string) {
      const client = await verbinden();
      try {
        const lock = await client.getMailboxLock(zugang.folder);
        try {
          await client.messageFlagsAdd({ uid: quellId }, ["\\Seen"], {
            uid: true,
          });
          if (zielOrdner) {
            await client.messageMove({ uid: quellId }, zielOrdner, { uid: true });
          }
        } finally {
          lock.release();
        }
      } finally {
        await client.logout().catch(() => {});
      }
    },

    async pruefeVerbindung() {
      const client = await verbinden();
      try {
        const lock = await client.getMailboxLock(zugang.folder);
        lock.release();
      } finally {
        await client.logout().catch(() => {});
      }
    },
  };
}
