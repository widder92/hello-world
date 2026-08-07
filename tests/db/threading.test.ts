import { MessageType } from "@prisma/client";
import { beforeEach, describe, expect, it } from "vitest";

import { buildReplyAddress, createReplyToken } from "@/lib/crypto";
import { bereitsVerarbeitet, findeVorgang } from "@/mail/threading";
import type { EingehendeNachricht } from "@/mail/types";
import {
  bereichAnlegen,
  datenbankErreichbar,
  db,
  eingangAnlegen,
  leereDatenbank,
  personAnlegen,
  ticketAnlegenRoh,
} from "./datenbank";

/**
 * Antwortet jemand auf eine Benachrichtigung, muss der Text am selben Vorgang
 * landen. Entsteht stattdessen ein neues Ticket, hat man nach drei Antworten
 * vier Vorgänge zu einer Sache – genau das Problem, das ein Ticketsystem
 * lösen soll.
 *
 * Es gibt vier Wege dorthin, weil jeder einzelne für sich unzuverlässig ist:
 * Mailprogramme schneiden Betreffzeilen ab, Verteiler schreiben Kopfzeilen um,
 * und wer von Hand eine neue Mail schreibt, hat gar keine Verkettung. Hier wird
 * jeder Weg einzeln geprüft – und zusätzlich, dass sie in der richtigen
 * Reihenfolge greifen.
 */

const verfuegbar = await datenbankErreichbar();

function mail(teil: Partial<EingehendeNachricht> = {}): EingehendeNachricht {
  return {
    quellId: "1",
    messageId: `<${Math.random().toString(36).slice(2)}@example.org>`,
    inReplyTo: null,
    references: [],
    betreff: "Heizung im Gruppenraum",
    vonAdresse: "anna.bergmann@example.org",
    vonName: "Anna Bergmann",
    anAdressen: ["hausmeister@example.org"],
    ccAdressen: [],
    text: "Es ist immer noch kalt.",
    html: null,
    empfangenAm: new Date(),
    autoSubmitted: null,
    precedence: null,
    anhaenge: [],
    ...teil,
  };
}

async function vorgang(nummer = "HM-2026-000087") {
  const team = await bereichAnlegen({ key: "HM", name: "Hausmeister" });
  const eingang = await eingangAnlegen({ teamId: team.id });
  const melderin = await personAnlegen({ name: "Anna Bergmann" });

  const ticket = await ticketAnlegenRoh({
    number: nummer,
    subject: "Heizung im Gruppenraum",
    queueId: eingang.id,
    teamId: team.id,
    requesterId: melderin.id,
  });

  return { team, eingang, melderin, ticket };
}

describe.runIf(verfuegbar)("Antwortmail dem Vorgang zuordnen", () => {
  beforeEach(leereDatenbank);

  it("erkennt den Vorgang an der signierten Antwortadresse", async () => {
    const { ticket, melderin } = await vorgang();
    const adresse = buildReplyAddress({
      ticketId: ticket.id,
      recipientId: melderin.id,
    });

    const treffer = await findeVorgang(mail({ anAdressen: [adresse!] }));

    expect(treffer?.ticketId).toBe(ticket.id);
    expect(treffer?.weg).toBe("token");
    // Nur dieser Weg beweist, dass der Absender die Mail bekommen hat.
    expect(treffer?.bestaetigterEmpfaengerId).toBe(melderin.id);
  });

  it("findet die Antwortadresse auch im Kopieempfänger", async () => {
    // Manche Mailprogramme setzen beim Antworten an alle die Adresse ins CC.
    const { ticket, melderin } = await vorgang();
    const adresse = buildReplyAddress({
      ticketId: ticket.id,
      recipientId: melderin.id,
    });

    const treffer = await findeVorgang(
      mail({ anAdressen: ["hausmeister@example.org"], ccAdressen: [adresse!] }),
    );

    expect(treffer?.ticketId).toBe(ticket.id);
  });

  it("erkennt den Vorgang an In-Reply-To", async () => {
    const { ticket } = await vorgang();
    await db.ticketMessage.create({
      data: {
        ticketId: ticket.id,
        type: MessageType.PUBLIC_REPLY,
        body: "Wir schauen morgen vorbei.",
        bodyText: "Wir schauen morgen vorbei.",
        emailMessageId: "<benachrichtigung-1@example.org>",
      },
    });

    const treffer = await findeVorgang(
      mail({ inReplyTo: "<benachrichtigung-1@example.org>" }),
    );

    expect(treffer?.ticketId).toBe(ticket.id);
    expect(treffer?.weg).toBe("kopfzeile");
    // Ohne Token kein Nachweis über den Absender – also keine Befehle.
    expect(treffer?.bestaetigterEmpfaengerId).toBeNull();
  });

  it("erkennt den Vorgang an der References-Kette", async () => {
    // Wenn ein Verteiler dazwischenhängt, geht In-Reply-To gern verloren,
    // References bleibt aber meist stehen.
    const { ticket } = await vorgang();
    await db.ticketMessage.create({
      data: {
        ticketId: ticket.id,
        type: MessageType.PUBLIC_REPLY,
        body: "Wir schauen morgen vorbei.",
        bodyText: "Wir schauen morgen vorbei.",
        emailMessageId: "<benachrichtigung-2@example.org>",
      },
    });

    const treffer = await findeVorgang(
      mail({
        inReplyTo: null,
        references: ["<fremd@example.net>", "<benachrichtigung-2@example.org>"],
      }),
    );

    expect(treffer?.ticketId).toBe(ticket.id);
    expect(treffer?.weg).toBe("kopfzeile");
  });

  it("erkennt den Vorgang an der Ticketnummer im Betreff", async () => {
    // Der Rettungsanker: Wer eine neue Mail schreibt und die Nummer aus einer
    // alten Benachrichtigung hineinkopiert, landet trotzdem richtig.
    const { ticket } = await vorgang("HM-2026-000087");

    const treffer = await findeVorgang(
      mail({ betreff: "AW: [HM-2026-000087] Heizung im Gruppenraum" }),
    );

    expect(treffer?.ticketId).toBe(ticket.id);
    expect(treffer?.weg).toBe("betreff");
  });

  it("erkennt den Vorgang an der Mail, aus der er entstanden ist", async () => {
    // Der Vorgang kam selbst per Mail herein, und jemand antwortet auf seine
    // eigene ursprüngliche Nachricht.
    const team = await bereichAnlegen({ key: "HM", name: "Hausmeister" });
    const eingang = await eingangAnlegen({ teamId: team.id });
    const melderin = await personAnlegen();
    const ticket = await ticketAnlegenRoh({
      number: "HM-2026-000090",
      queueId: eingang.id,
      teamId: team.id,
      requesterId: melderin.id,
      originEmailMessageId: "<ursprung@example.org>",
    });

    const treffer = await findeVorgang(
      mail({ betreff: "Noch etwas", inReplyTo: "<ursprung@example.org>" }),
    );

    expect(treffer?.ticketId).toBe(ticket.id);
    expect(treffer?.weg).toBe("herkunft");
  });

  it("bevorzugt das Token vor Betreff und Kopfzeile", async () => {
    // Wenn eine Mail widersprüchliche Angaben trägt, gilt die einzige, die
    // sich nicht fälschen lässt.
    const richtig = await vorgang("HM-2026-000001");
    const team = richtig.team;
    const eingang = richtig.eingang;
    const falsch = await ticketAnlegenRoh({
      number: "HM-2026-000002",
      queueId: eingang.id,
      teamId: team.id,
      requesterId: richtig.melderin.id,
    });

    const adresse = buildReplyAddress({
      ticketId: richtig.ticket.id,
      recipientId: richtig.melderin.id,
    });

    const treffer = await findeVorgang(
      mail({
        anAdressen: [adresse!],
        betreff: `AW: [${falsch.number}] Heizung`,
      }),
    );

    expect(treffer?.ticketId).toBe(richtig.ticket.id);
    expect(treffer?.weg).toBe("token");
  });

  it("ordnet bei gefälschter Signatur nicht zu", async () => {
    // Ein Token mit passender Kennung, aber falscher Unterschrift. Es darf
    // weder zugeordnet werden noch als Nachweis über den Absender gelten.
    const { ticket, melderin } = await vorgang();
    const echt = createReplyToken({
      ticketId: ticket.id,
      recipientId: melderin.id,
    });
    const [nutzlast] = echt.split(".");
    const gefaelscht = `ticket+${nutzlast}.AAAAAAAAAAAAAAAAAAAAAAAAAAA@example.org`;

    const treffer = await findeVorgang(
      mail({ anAdressen: [gefaelscht], betreff: "Ganz neue Sache" }),
    );

    expect(treffer).toBeNull();
  });

  it("liefert nichts, wenn keiner der vier Wege greift", async () => {
    // Dann entsteht ein neuer Vorgang – richtig so, denn eine Zuordnung auf
    // Verdacht wäre schlimmer als ein Ticket zu viel.
    await vorgang();

    const treffer = await findeVorgang(
      mail({ betreff: "Neue Meldung: Licht im Flur defekt" }),
    );

    expect(treffer).toBeNull();
  });

  it("ordnet eine Nummer aus einem fremden System nicht zu", async () => {
    await vorgang("HM-2026-000087");

    const treffer = await findeVorgang(
      mail({ betreff: "AW: [HM-2026-999999] Heizung" }),
    );

    expect(treffer).toBeNull();
  });
});

describe.runIf(verfuegbar)("Doppelte Verarbeitung verhindern", () => {
  beforeEach(leereDatenbank);

  it("erkennt eine bereits abgelegte Nachricht", async () => {
    // Der Abrufdienst kann neu starten, bevor er die Mail als gelesen
    // markieren konnte. Ohne diese Prüfung stünde sie zweimal im Verlauf.
    const { ticket } = await vorgang();
    await db.ticketMessage.create({
      data: {
        ticketId: ticket.id,
        type: MessageType.PUBLIC_REPLY,
        body: "Text",
        bodyText: "Text",
        emailMessageId: "<schon-da@example.org>",
      },
    });

    expect(await bereitsVerarbeitet("<schon-da@example.org>")).toBe(true);
    expect(await bereitsVerarbeitet("<neu@example.org>")).toBe(false);
  });

  it("erkennt die Mail, aus der ein Vorgang entstanden ist", async () => {
    const team = await bereichAnlegen({ key: "HM", name: "Hausmeister" });
    const eingang = await eingangAnlegen({ teamId: team.id });
    const melderin = await personAnlegen();
    await ticketAnlegenRoh({
      number: "HM-2026-000091",
      queueId: eingang.id,
      teamId: team.id,
      requesterId: melderin.id,
      originEmailMessageId: "<ursprung@example.org>",
    });

    expect(await bereitsVerarbeitet("<ursprung@example.org>")).toBe(true);
  });

  it("lässt eine Mail ohne Message-ID durch", async () => {
    expect(await bereitsVerarbeitet(null)).toBe(false);
  });
});
