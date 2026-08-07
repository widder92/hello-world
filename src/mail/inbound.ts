import {
  EventType,
  MailTransport,
  MessageType,
  TicketSource,
  UnknownSenderPolicy,
} from "@prisma/client";

import { db } from "@/server/db";
import { decryptOptional } from "@/lib/crypto";
import { createGraphSource } from "@/mail/graph";
import { createImapSource } from "@/mail/imap";
import {
  beschreibeBefehl,
  erkenneBefehle,
  findeZielBereich,
  pruefeBefehlsrecht,
  statusFuerBefehl,
} from "@/mail/commands";
import {
  betreffBereinigen,
  istAutomatischeAntwort,
  nachrichtentext,
} from "@/mail/parse";
import { bereitsVerarbeitet, findeVorgang } from "@/mail/threading";
import type { EingehendeNachricht, MailSource } from "@/mail/types";
import {
  speichereAnhang,
  UploadFehler,
} from "@/server/services/attachments";
import { logEvent } from "@/server/services/events";
import {
  nachrichtHinzufuegen,
  statusAendern,
  ticketAnlegen,
  ticketUebernehmen,
} from "@/server/services/ticket";
import { ticketWeiterleiten } from "@/server/services/transfer";

/**
 * Abruf und Verarbeitung eingehender Mails.
 *
 * Je Eingang ein Postfach: Eine Mail an hausmeister-wohnheim-nord@… wird
 * unmittelbar zum Vorgang im Eingang „Hausmeister Wohnheim Nord". Damit
 * funktioniert der Mailweg genauso direkt wie die Bereichswahl im Portal –
 * nichts läuft als Umweg über die IT.
 */

export interface AbrufErgebnis {
  eingang: string;
  geholt: number;
  neueVorgaenge: number;
  antworten: number;
  uebersprungen: number;
  fehler: string | null;
}

/** Baut die passende Postfachanbindung zu einem Konto. */
function quelleFuer(konto: {
  transport: MailTransport;
  imapHost: string | null;
  imapPort: number | null;
  imapSecure: boolean;
  imapUser: string | null;
  imapPasswordEnc: string | null;
  imapFolder: string;
  graphUserId: string | null;
  lastUid: number | null;
}): MailSource {
  if (konto.transport === MailTransport.GRAPH) {
    if (!konto.graphUserId) {
      throw new Error("Für Microsoft Graph fehlt die Postfachkennung.");
    }
    return createGraphSource(konto.graphUserId);
  }

  if (!konto.imapHost || !konto.imapUser) {
    throw new Error("Für IMAP fehlen Server oder Benutzername.");
  }

  return createImapSource({
    host: konto.imapHost,
    port: konto.imapPort ?? 993,
    secure: konto.imapSecure,
    user: konto.imapUser,
    password: decryptOptional(konto.imapPasswordEnc) ?? "",
    folder: konto.imapFolder,
    lastUid: konto.lastUid,
  });
}

/** Ruft alle eingerichteten Postfächer ab. */
export async function alleEingaengeAbrufen(): Promise<AbrufErgebnis[]> {
  const konten = await db.mailAccount.findMany({
    where: { isActive: true, queue: { isActive: true } },
    include: {
      queue: {
        select: {
          id: true,
          name: true,
          teamId: true,
          siteId: true,
          team: { select: { id: true, key: true, name: true } },
        },
      },
    },
  });

  const ergebnisse: AbrufErgebnis[] = [];

  for (const konto of konten) {
    ergebnisse.push(await eingangAbrufen(konto.id));
  }

  return ergebnisse;
}

/** Ruft ein einzelnes Postfach ab. */
export async function eingangAbrufen(mailAccountId: string): Promise<AbrufErgebnis> {
  const konto = await db.mailAccount.findUniqueOrThrow({
    where: { id: mailAccountId },
    include: {
      queue: {
        select: {
          id: true,
          name: true,
          teamId: true,
          siteId: true,
          team: { select: { id: true, key: true, name: true } },
        },
      },
    },
  });

  const ergebnis: AbrufErgebnis = {
    eingang: konto.queue.name,
    geholt: 0,
    neueVorgaenge: 0,
    antworten: 0,
    uebersprungen: 0,
    fehler: null,
  };

  let quelle: MailSource;
  try {
    quelle = quelleFuer(konto);
  } catch (e) {
    ergebnis.fehler = e instanceof Error ? e.message : "Anbindung nicht nutzbar.";
    await db.mailAccount.update({
      where: { id: konto.id },
      data: { lastSyncError: ergebnis.fehler, lastSyncAt: new Date() },
    });
    return ergebnis;
  }

  let hoechsteUid = konto.lastUid ?? 0;

  try {
    const mails = await quelle.holeNeue(25);
    ergebnis.geholt = mails.length;

    for (const mail of mails) {
      try {
        const art = await verarbeiteMail(mail, konto.queue, konto.unknownSender);

        if (art === "neu") ergebnis.neueVorgaenge++;
        else if (art === "antwort") ergebnis.antworten++;
        else ergebnis.uebersprungen++;

        await quelle.markiereVerarbeitet(
          mail.quellId,
          konto.imapProcessedFolder ?? undefined,
        );

        const uid = Number(mail.quellId);
        if (!Number.isNaN(uid) && uid > hoechsteUid) hoechsteUid = uid;
      } catch (e) {
        // Eine einzelne fehlerhafte Mail darf den restlichen Abruf nicht
        // stoppen – sonst blockiert eine kaputte Nachricht das ganze Postfach.
        console.error(
          `[Mail] Nachricht ${mail.quellId} in "${konto.queue.name}" konnte nicht verarbeitet werden:`,
          e,
        );
        ergebnis.uebersprungen++;
      }
    }

    await db.mailAccount.update({
      where: { id: konto.id },
      data: {
        lastSyncAt: new Date(),
        lastSyncError: null,
        lastUid: hoechsteUid || null,
      },
    });
  } catch (e) {
    ergebnis.fehler = e instanceof Error ? e.message : "Abruf fehlgeschlagen.";
    await db.mailAccount.update({
      where: { id: konto.id },
      data: { lastSyncAt: new Date(), lastSyncError: ergebnis.fehler },
    });
  }

  return ergebnis;
}

type Verarbeitungsart = "neu" | "antwort" | "uebersprungen";

async function verarbeiteMail(
  mail: EingehendeNachricht,
  queue: {
    id: string;
    name: string;
    teamId: string;
    siteId: string | null;
    team: { id: string; key: string; name: string };
  },
  unknownSender: UnknownSenderPolicy,
): Promise<Verarbeitungsart> {
  // Schon verarbeitet? Kommt vor, wenn der Dienst zwischen Verarbeitung und
  // Markieren neu startet.
  if (await bereitsVerarbeitet(mail.messageId)) return "uebersprungen";

  // Abwesenheitsnotizen und Unzustellbarkeitsberichte erzeugen keine Vorgänge.
  if (istAutomatischeAntwort(mail)) {
    const zuordnung = await findeVorgang(mail);
    if (zuordnung) {
      // Am bestehenden Vorgang als interne Notiz festhalten – der Hinweis,
      // dass der Melder im Urlaub ist, ist für die Bearbeitung nützlich.
      await nachrichtHinzufuegen({
        ticketId: zuordnung.ticketId,
        body: `Automatische Antwort von ${mail.vonAdresse}:\n\n${nachrichtentext(mail).slice(0, 500)}`,
        type: MessageType.INTERNAL_NOTE,
        authorName: "System",
        authorEmail: mail.vonAdresse,
        emailMessageId: mail.messageId,
      });
    }
    return "uebersprungen";
  }

  const text = nachrichtentext(mail);
  const zuordnung = await findeVorgang(mail);

  if (zuordnung) {
    await verarbeiteAntwort(mail, text, zuordnung);
    return "antwort";
  }

  await legeVorgangAn(mail, text, queue, unknownSender);
  return "neu";
}

/** Antwort auf einen bestehenden Vorgang, ggf. mit Steuerbefehlen. */
async function verarbeiteAntwort(
  mail: EingehendeNachricht,
  rohtext: string,
  zuordnung: NonNullable<Awaited<ReturnType<typeof findeVorgang>>>,
) {
  const { text, befehle } = erkenneBefehle(rohtext);

  const absender = await db.user.findFirst({
    where: { email: mail.vonAdresse.toLowerCase() },
    select: { id: true, name: true },
  });

  // Ohne Befehle ist es eine ganz normale Antwort.
  if (befehle.length === 0) {
    await nachrichtHinzufuegen({
      ticketId: zuordnung.ticketId,
      body: text || rohtext,
      type: MessageType.PUBLIC_REPLY,
      authorId: absender?.id ?? null,
      authorName: absender?.name ?? mail.vonName ?? mail.vonAdresse,
      authorEmail: mail.vonAdresse,
      emailMessageId: mail.messageId,
      emailInReplyTo: mail.inReplyTo,
    });
    await speichereAnhaenge(mail, zuordnung.ticketId);
    return;
  }

  // Befehle: nur mit gültigem Token und Bereichszugehörigkeit.
  const pruefung = await pruefeBefehlsrecht({
    ticketId: zuordnung.ticketId,
    absenderAdresse: mail.vonAdresse,
    bestaetigterEmpfaengerId: zuordnung.bestaetigterEmpfaengerId,
  });

  if (!pruefung.erlaubt) {
    // Text trotzdem festhalten – er enthält ja eine echte Rückmeldung –,
    // aber ohne die Befehle auszuführen, und mit sichtbarem Hinweis.
    await nachrichtHinzufuegen({
      ticketId: zuordnung.ticketId,
      body: text || rohtext,
      type: MessageType.PUBLIC_REPLY,
      authorId: absender?.id ?? null,
      authorName: absender?.name ?? mail.vonName ?? mail.vonAdresse,
      authorEmail: mail.vonAdresse,
      emailMessageId: mail.messageId,
      emailInReplyTo: mail.inReplyTo,
    });

    await logEvent(db, {
      ticketId: zuordnung.ticketId,
      type: EventType.EMAIL_COMMAND_REJECTED,
      actorName: "System",
      toValue: befehle.map(beschreibeBefehl).join(", "),
      note: pruefung.grund,
      meta: {
        absender: mail.vonAdresse,
        zuordnungsweg: zuordnung.weg,
      },
    });

    await speichereAnhaenge(mail, zuordnung.ticketId);
    return;
  }

  const akteur = { id: pruefung.userId!, name: pruefung.name! };
  const nurIntern = befehle.some((b) => b.art === "intern");

  if (text) {
    await nachrichtHinzufuegen({
      ticketId: zuordnung.ticketId,
      body: text,
      type: nurIntern ? MessageType.INTERNAL_NOTE : MessageType.PUBLIC_REPLY,
      authorId: akteur.id,
      authorName: akteur.name,
      authorEmail: mail.vonAdresse,
      emailMessageId: mail.messageId,
      emailInReplyTo: mail.inReplyTo,
      viaEmailCommand: true,
    });
  }

  await speichereAnhaenge(mail, zuordnung.ticketId);

  for (const befehl of befehle) {
    await logEvent(db, {
      ticketId: zuordnung.ticketId,
      type: EventType.EMAIL_COMMAND,
      actorId: akteur.id,
      actorName: akteur.name,
      toValue: beschreibeBefehl(befehl),
    });

    if (befehl.art === "uebernehmen") {
      await ticketUebernehmen(zuordnung.ticketId, akteur).catch((e) => {
        console.warn("[Mail] #übernehmen fehlgeschlagen:", e);
      });
      continue;
    }

    if (befehl.art === "weiterleiten") {
      const ziel = befehl.ziel ? await findeZielBereich(befehl.ziel) : null;
      if (!ziel) {
        await logEvent(db, {
          ticketId: zuordnung.ticketId,
          type: EventType.EMAIL_COMMAND_REJECTED,
          actorId: akteur.id,
          actorName: akteur.name,
          toValue: beschreibeBefehl(befehl),
          note: `Es gibt keinen Bereich mit der Bezeichnung „${befehl.ziel ?? ""}".`,
        });
        continue;
      }

      const ticket = await db.ticket.findUnique({
        where: { id: zuordnung.ticketId },
        select: { siteId: true },
      });

      await ticketWeiterleiten({
        ticketId: zuordnung.ticketId,
        zielTeamId: ziel.id,
        zielSiteId: ticket?.siteId ?? null,
        begruendung: `Per E-Mail weitergeleitet von ${akteur.name}.`,
        actor: akteur,
      }).catch(async (e) => {
        await logEvent(db, {
          ticketId: zuordnung.ticketId,
          type: EventType.EMAIL_COMMAND_REJECTED,
          actorId: akteur.id,
          actorName: akteur.name,
          toValue: beschreibeBefehl(befehl),
          note: e instanceof Error ? e.message : "Weiterleitung fehlgeschlagen.",
        });
      });
      continue;
    }

    const status = statusFuerBefehl(befehl.art);
    if (status) {
      await statusAendern(
        zuordnung.ticketId,
        status,
        akteur,
        `Per E-Mail gesetzt (${beschreibeBefehl(befehl)}).`,
      );
    }
  }
}

/** Neue Mail ohne Bezug zu einem Vorgang: Ticket anlegen. */
async function legeVorgangAn(
  mail: EingehendeNachricht,
  text: string,
  queue: {
    id: string;
    teamId: string;
    siteId: string | null;
    team: { id: string; key: string; name: string };
  },
  unknownSender: UnknownSenderPolicy,
) {
  let melder = await db.user.findFirst({
    where: { email: mail.vonAdresse.toLowerCase() },
    select: { id: true, name: true, isActive: true },
  });

  let quarantaene = false;

  if (!melder) {
    if (unknownSender === UnknownSenderPolicy.REJECT) {
      console.info(
        `[Mail] Nachricht von unbekanntem Absender ${mail.vonAdresse} verworfen.`,
      );
      return;
    }

    // Externen Melder anlegen. Er kann sich nicht anmelden (kein Passwort,
    // isExternal), bekommt aber Antworten per Mail und taucht als Melder auf.
    melder = await db.user.create({
      data: {
        email: mail.vonAdresse.toLowerCase(),
        name: mail.vonName?.trim() || mail.vonAdresse,
        isExternal: true,
        isActive: true,
      },
      select: { id: true, name: true, isActive: true },
    });

    quarantaene = unknownSender === UnknownSenderPolicy.QUARANTINE;
  }

  const ticket = await ticketAnlegen({
    teamId: queue.teamId,
    // Der Standort des Eingangs; bei zentralen Eingängen bleibt er offen und
    // wird beim Bearbeiten nachgetragen.
    siteId: queue.siteId,
    subject: betreffBereinigen(mail.betreff),
    body: text,
    requesterId: melder.id,
    source: TicketSource.EMAIL,
    originEmailMessageId: mail.messageId,
    quarantined: quarantaene,
    actor: { id: null, name: melder.name },
  });

  await logEvent(db, {
    ticketId: ticket.id,
    type: EventType.EMAIL_RECEIVED,
    actorName: melder.name,
    meta: { von: mail.vonAdresse, an: mail.anAdressen },
  });

  await speichereAnhaenge(mail, ticket.id);
}

/**
 * Legt Anhänge einer eingehenden Mail in der Dateiablage ab.
 *
 * Nutzt denselben Dienst wie der Upload aus dem Browser – damit gelten
 * Größenbegrenzung und Typprüfung für beide Wege gleichermaßen. Eine Mail
 * kann so keine ausführbare Datei einschleusen, die über die Weboberfläche
 * abgelehnt worden wäre.
 *
 * Eingebettete Bilder aus Signaturen werden übersprungen, sofern sie klein
 * sind – sonst hängt an jedem Vorgang das Firmenlogo des Absenders.
 */
async function speichereAnhaenge(mail: EingehendeNachricht, ticketId: string) {
  const echte = mail.anhaenge.filter(
    (a) => !a.inline || a.inhalt.length > 64 * 1024,
  );

  for (const anhang of echte) {
    try {
      await speichereAnhang({
        inhalt: anhang.inhalt,
        filename: anhang.filename,
        mimeType: anhang.mimeType,
        ticketId,
        fromEmail: true,
      });
    } catch (error) {
      // Ein abgelehnter Anhang darf die Mail nicht verwerfen – der Text ist
      // wichtiger als die Datei. Der Hinweis landet im Protokoll.
      const grund =
        error instanceof UploadFehler ? error.message : "unbekannter Fehler";
      console.warn(
        `[Mail] Anhang "${anhang.filename}" wurde nicht übernommen: ${grund}`,
      );
      await logEvent(db, {
        ticketId,
        type: EventType.ATTACHMENT_ADDED,
        actorName: "System",
        toValue: anhang.filename,
        note: `Nicht übernommen: ${grund}`,
      });
    }
  }
}
