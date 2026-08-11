import { ClientSecretCredential } from "@azure/identity";
import { Client } from "@microsoft/microsoft-graph-client";

import type { EingehendeNachricht, MailSource } from "@/mail/types";

/**
 * Microsoft Graph als Postfach-Anbindung.
 *
 * Alternative zu IMAP für Häuser mit Microsoft 365. Vorteile: keine
 * SMTP-/IMAP-Freischaltung nötig (Microsoft schaltet Basisauthentifizierung
 * zunehmend ab), und der Zugriff lässt sich über eine App-Registrierung genau
 * auf die Bereichspostfächer begrenzen.
 *
 * Voraussetzung ist eine App-Registrierung mit den Anwendungsberechtigungen
 * `Mail.ReadWrite` und `Mail.Send`, erteilt durch den M365-Administrator. Wie
 * das eingerichtet wird, steht in BETRIEB.md.
 */

function credential() {
  const tenantId = process.env.GRAPH_TENANT_ID;
  const clientId = process.env.GRAPH_CLIENT_ID;
  const clientSecret = process.env.GRAPH_CLIENT_SECRET;

  if (!tenantId || !clientId || !clientSecret) {
    throw new Error(
      "Microsoft Graph ist nicht eingerichtet. Es fehlen GRAPH_TENANT_ID, " +
        "GRAPH_CLIENT_ID oder GRAPH_CLIENT_SECRET.",
    );
  }

  return new ClientSecretCredential(tenantId, clientId, clientSecret);
}

function graphClient(): Client {
  const cred = credential();

  return Client.initWithMiddleware({
    authProvider: {
      getAccessToken: async () => {
        const token = await cred.getToken(
          "https://graph.microsoft.com/.default",
        );
        if (!token) throw new Error("Kein Zugriffstoken von Entra ID erhalten.");
        return token.token;
      },
    },
  });
}

interface GraphAdresse {
  emailAddress?: { address?: string; name?: string };
}

interface GraphNachricht {
  id: string;
  internetMessageId?: string;
  subject?: string;
  receivedDateTime?: string;
  from?: GraphAdresse;
  toRecipients?: GraphAdresse[];
  ccRecipients?: GraphAdresse[];
  body?: { contentType?: string; content?: string };
  hasAttachments?: boolean;
  internetMessageHeaders?: { name: string; value: string }[];
}

interface GraphAnhang {
  "@odata.type": string;
  name: string;
  contentType: string;
  size: number;
  contentBytes?: string;
  isInline?: boolean;
}

function adresse(a?: GraphAdresse): string {
  return a?.emailAddress?.address?.toLowerCase() ?? "";
}

function kopfzeile(
  nachricht: GraphNachricht,
  name: string,
): string | undefined {
  return nachricht.internetMessageHeaders?.find(
    (h) => h.name.toLowerCase() === name.toLowerCase(),
  )?.value;
}

/** Postfachanbindung über Graph. */
export function createGraphSource(graphUserId: string): MailSource {
  return {
    async holeNeue(limit = 25): Promise<EingehendeNachricht[]> {
      const client = graphClient();

      const antwort = (await client
        .api(`/users/${graphUserId}/mailFolders/inbox/messages`)
        .filter("isRead eq false")
        .top(limit)
        .select(
          "id,internetMessageId,subject,receivedDateTime,from,toRecipients,ccRecipients,body,hasAttachments,internetMessageHeaders",
        )
        .orderby("receivedDateTime asc")
        .get()) as { value: GraphNachricht[] };

      const ergebnis: EingehendeNachricht[] = [];

      for (const m of antwort.value) {
        const anhaenge: EingehendeNachricht["anhaenge"] = [];

        if (m.hasAttachments) {
          const dateien = (await client
            .api(`/users/${graphUserId}/messages/${m.id}/attachments`)
            .get()) as { value: GraphAnhang[] };

          for (const d of dateien.value) {
            // Nur echte Dateianhänge; verknüpfte Elemente aus OneDrive haben
            // keinen Inhalt und würden als leere Datei landen.
            if (
              d["@odata.type"] !== "#microsoft.graph.fileAttachment" ||
              !d.contentBytes
            ) {
              continue;
            }
            anhaenge.push({
              filename: d.name,
              mimeType: d.contentType || "application/octet-stream",
              inhalt: Buffer.from(d.contentBytes, "base64"),
              inline: Boolean(d.isInline),
            });
          }
        }

        const istHtml = m.body?.contentType?.toLowerCase() === "html";

        ergebnis.push({
          quellId: m.id,
          messageId: m.internetMessageId ?? null,
          inReplyTo: kopfzeile(m, "In-Reply-To") ?? null,
          references:
            kopfzeile(m, "References")?.split(/\s+/).filter(Boolean) ?? [],
          betreff: m.subject ?? "(ohne Betreff)",
          vonAdresse: adresse(m.from),
          vonName: m.from?.emailAddress?.name ?? null,
          anAdressen: (m.toRecipients ?? []).map(adresse).filter(Boolean),
          ccAdressen: (m.ccRecipients ?? []).map(adresse).filter(Boolean),
          text: istHtml ? null : (m.body?.content ?? ""),
          html: istHtml ? (m.body?.content ?? null) : null,
          empfangenAm: m.receivedDateTime
            ? new Date(m.receivedDateTime)
            : new Date(),
          autoSubmitted: kopfzeile(m, "Auto-Submitted") ?? null,
          precedence: kopfzeile(m, "Precedence") ?? null,
          anhaenge,
        });
      }

      return ergebnis;
    },

    async markiereVerarbeitet(quellId: string, zielOrdner?: string) {
      const client = graphClient();

      await client
        .api(`/users/${graphUserId}/messages/${quellId}`)
        .patch({ isRead: true });

      if (zielOrdner) {
        await client
          .api(`/users/${graphUserId}/messages/${quellId}/move`)
          .post({ destinationId: zielOrdner });
      }
    },

    async pruefeVerbindung() {
      const client = graphClient();
      await client.api(`/users/${graphUserId}/mailFolders/inbox`).get();
    },
  };
}

/** Versand über Graph statt SMTP. */
export async function sendeUeberGraph(nachricht: {
  from: string;
  to: string;
  cc?: string[];
  replyTo?: string;
  subject: string;
  text: string;
  inReplyTo?: string;
  references?: string[];
  headers?: Record<string, string>;
}): Promise<string | null> {
  const client = graphClient();

  // Die Absenderadresse aus MAIL_FROM_ADDRESS bestimmt das sendende Postfach.
  const absenderAdresse =
    nachricht.from.match(/<([^>]+)>/)?.[1] ?? nachricht.from;

  const zusatzKopfzeilen = Object.entries(nachricht.headers ?? {})
    // Graph erlaubt nur eigene Kopfzeilen mit "x-"-Präfix.
    .filter(([name]) => name.toLowerCase().startsWith("x-"))
    .map(([name, value]) => ({ name, value }));

  await client.api(`/users/${absenderAdresse}/sendMail`).post({
    message: {
      subject: nachricht.subject,
      body: { contentType: "Text", content: nachricht.text },
      toRecipients: [{ emailAddress: { address: nachricht.to } }],
      ccRecipients: (nachricht.cc ?? []).map((a) => ({
        emailAddress: { address: a },
      })),
      replyTo: nachricht.replyTo
        ? [{ emailAddress: { address: nachricht.replyTo } }]
        : undefined,
      internetMessageHeaders: zusatzKopfzeilen.length
        ? zusatzKopfzeilen
        : undefined,
    },
    saveToSentItems: true,
  });

  // Graph gibt beim Senden keine Message-ID zurück. Die Zuordnung späterer
  // Antworten läuft deshalb über die signierte Antwortadresse und die
  // Ticketkennung im Betreff – beide sind hier ohnehin gesetzt.
  return null;
}
