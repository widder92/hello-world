import {
  LinkType,
  MessageType,
  QueueMode,
  TicketStatus,
} from "@prisma/client";
import { beforeEach, describe, expect, it } from "vitest";

import { ticketAbspalten, ticketZusammenfuehren } from "@/server/services/split";
import { statusAendern } from "@/server/services/ticket";
import {
  bereichAnlegen,
  datenbankErreichbar,
  db,
  eingangAnlegen,
  leereDatenbank,
  personAnlegen,
  standortAnlegen,
  ticketAnlegenRoh,
} from "./datenbank";

/**
 * Das Abspalten eines Teilvorgangs war der ausdrücklich genannte Kernwunsch:
 * Ein Ticket kommt in die IT, ein Teil davon geht den Hausmeister an, und man
 * möchte weder den Melder ein zweites Ticket schreiben lassen noch den
 * eigenen Anteil aus der Hand geben.
 *
 * Geprüft wird hier vor allem, was danach mit dem Ursprungsticket geschieht –
 * denn genau daran entscheidet sich, ob ein zurückgestellter Vorgang irgendwann
 * wieder auftaucht oder für immer liegen bleibt.
 */

const verfuegbar = await datenbankErreichbar();

/**
 * Baut eine kleine, aber vollständige Welt: IT (zentral) und Hausmeister
 * (je Standort), eine Melderin, ein Bearbeiter und ein IT-Vorgang mit Verlauf.
 */
async function welt() {
  const nord = await standortAnlegen("Wohnheim Nord");
  const sued = await standortAnlegen("Wohnheim Süd");

  const it = await bereichAnlegen({ key: "IT", name: "IT" });
  const eingangIt = await eingangAnlegen({ teamId: it.id, name: "IT" });

  const hm = await bereichAnlegen({
    key: "HM",
    name: "Hausmeister",
    queueMode: QueueMode.PER_SITE,
  });
  const eingangNord = await eingangAnlegen({
    teamId: hm.id,
    siteId: nord.id,
    name: "Hausmeister Wohnheim Nord",
  });
  const eingangSued = await eingangAnlegen({
    teamId: hm.id,
    siteId: sued.id,
    name: "Hausmeister Wohnheim Süd",
  });

  const melderin = await personAnlegen({ name: "Anna Bergmann", siteId: nord.id });
  const bearbeiter = await personAnlegen({ name: "Frank Berger" });

  const ticket = await ticketAnlegenRoh({
    number: "IT-2026-000001",
    subject: "Neuer Arbeitsplatz eingerichtet",
    queueId: eingangIt.id,
    teamId: it.id,
    siteId: nord.id,
    requesterId: melderin.id,
    assigneeId: bearbeiter.id,
    status: TicketStatus.IN_BEARBEITUNG,
  });

  const oeffentlich = await db.ticketMessage.create({
    data: {
      ticketId: ticket.id,
      type: MessageType.PUBLIC_REPLY,
      authorId: melderin.id,
      authorName: melderin.name,
      body: "Der Rechner steht, aber es fehlt eine Netzwerkdose.",
      bodyText: "Der Rechner steht, aber es fehlt eine Netzwerkdose.",
    },
  });

  const intern = await db.ticketMessage.create({
    data: {
      ticketId: ticket.id,
      type: MessageType.INTERNAL_NOTE,
      authorId: bearbeiter.id,
      authorName: bearbeiter.name,
      body: "Lieferant hat den Rahmenvertrag gekündigt, intern klären.",
      bodyText: "Lieferant hat den Rahmenvertrag gekündigt, intern klären.",
    },
  });

  return {
    nord,
    sued,
    it,
    hm,
    eingangIt,
    eingangNord,
    eingangSued,
    melderin,
    bearbeiter,
    ticket,
    oeffentlich,
    intern,
    actor: { id: bearbeiter.id, name: bearbeiter.name },
  };
}

describe.runIf(verfuegbar)("Teilvorgang abspalten", () => {
  beforeEach(leereDatenbank);

  it("legt den Teilvorgang im richtigen Eingang des Zielbereichs an", async () => {
    const w = await welt();

    const ergebnis = await ticketAbspalten({
      quellTicketId: w.ticket.id,
      zielTeamId: w.hm.id,
      zielSiteId: w.sued.id,
      betreff: "Netzwerkdose setzen",
      beschreibung: "In Raum 12 fehlt eine Dose neben dem Schreibtisch.",
      nachrichtenIds: [],
      nachbehandlung: "offen_lassen",
      actor: w.actor,
    });

    const kind = await db.ticket.findUniqueOrThrow({
      where: { id: ergebnis.kindTicketId },
    });

    // Ein eigener Vorgang beim Hausmeister – am angegebenen Standort.
    expect(kind.queueId).toBe(w.eingangSued.id);
    expect(kind.teamId).toBe(w.hm.id);
    expect(kind.siteId).toBe(w.sued.id);
    expect(kind.number).toMatch(/^HM-\d{4}-\d{6}$/);
    expect(ergebnis.zielBereich).toBe("Hausmeister");
  });

  it("übernimmt den Standort des Ursprungstickets, wenn keiner angegeben ist", async () => {
    const w = await welt();

    const ergebnis = await ticketAbspalten({
      quellTicketId: w.ticket.id,
      zielTeamId: w.hm.id,
      betreff: "Netzwerkdose setzen",
      beschreibung: "In Raum 12 fehlt eine Dose.",
      nachrichtenIds: [],
      nachbehandlung: "offen_lassen",
      actor: w.actor,
    });

    const kind = await db.ticket.findUniqueOrThrow({
      where: { id: ergebnis.kindTicketId },
    });
    expect(kind.queueId).toBe(w.eingangNord.id);
  });

  it("behält den Melder, damit er beide Teile verfolgen kann", async () => {
    // Der Melder soll nichts erneut tun müssen – das ist der Sinn des
    // Abspaltens gegenüber „bitte schreiben Sie dem Hausmeister selbst".
    const w = await welt();

    const ergebnis = await ticketAbspalten({
      quellTicketId: w.ticket.id,
      zielTeamId: w.hm.id,
      betreff: "Netzwerkdose setzen",
      beschreibung: "In Raum 12 fehlt eine Dose.",
      nachrichtenIds: [],
      nachbehandlung: "offen_lassen",
      actor: w.actor,
    });

    const kind = await db.ticket.findUniqueOrThrow({
      where: { id: ergebnis.kindTicketId },
    });
    expect(kind.requesterId).toBe(w.melderin.id);
  });

  it("kopiert nur die ausgewählten Nachrichten mit", async () => {
    // Der interne Vermerk über den Lieferanten hilft dem Hausmeister nicht und
    // gehört ihm auch nicht vorgelegt.
    const w = await welt();

    const ergebnis = await ticketAbspalten({
      quellTicketId: w.ticket.id,
      zielTeamId: w.hm.id,
      betreff: "Netzwerkdose setzen",
      beschreibung: "In Raum 12 fehlt eine Dose.",
      nachrichtenIds: [w.oeffentlich.id],
      nachbehandlung: "offen_lassen",
      actor: w.actor,
    });

    const texte = (
      await db.ticketMessage.findMany({
        where: { ticketId: ergebnis.kindTicketId },
        orderBy: { createdAt: "asc" },
      })
    ).map((m) => m.bodyText);

    expect(texte.join("\n")).toContain("es fehlt eine Netzwerkdose");
    expect(texte.join("\n")).not.toContain("Rahmenvertrag");
  });

  it("kopiert keine Nachrichten aus fremden Vorgängen", async () => {
    // Die Kennungen kommen aus dem Formular und sind damit frei wählbar. Ohne
    // die Einschränkung auf den Quellvorgang ließen sich mit geratenen
    // Kennungen fremde Nachrichten in ein eigenes Ticket holen.
    const w = await welt();
    const fremd = await ticketAnlegenRoh({
      number: "IT-2026-000099",
      queueId: w.eingangIt.id,
      teamId: w.it.id,
      requesterId: w.melderin.id,
    });
    const fremdeNachricht = await db.ticketMessage.create({
      data: {
        ticketId: fremd.id,
        type: MessageType.PUBLIC_REPLY,
        body: "Gehaltsabrechnung fehlerhaft",
        bodyText: "Gehaltsabrechnung fehlerhaft",
      },
    });

    const ergebnis = await ticketAbspalten({
      quellTicketId: w.ticket.id,
      zielTeamId: w.hm.id,
      betreff: "Netzwerkdose setzen",
      beschreibung: "In Raum 12 fehlt eine Dose.",
      nachrichtenIds: [w.oeffentlich.id, fremdeNachricht.id],
      nachbehandlung: "offen_lassen",
      actor: w.actor,
    });

    const texte = (
      await db.ticketMessage.findMany({ where: { ticketId: ergebnis.kindTicketId } })
    ).map((m) => m.bodyText);

    expect(texte.join("\n")).not.toContain("Gehaltsabrechnung");
  });

  it("verknüpft beide Vorgänge sichtbar miteinander", async () => {
    const w = await welt();

    const ergebnis = await ticketAbspalten({
      quellTicketId: w.ticket.id,
      zielTeamId: w.hm.id,
      betreff: "Netzwerkdose setzen",
      beschreibung: "In Raum 12 fehlt eine Dose.",
      nachrichtenIds: [],
      nachbehandlung: "offen_lassen",
      actor: w.actor,
    });

    const verknuepfung = await db.ticketLink.findFirst({
      where: { fromId: w.ticket.id, toId: ergebnis.kindTicketId },
    });
    expect(verknuepfung?.type).toBe(LinkType.SPLIT_FROM);

    // Und im Protokoll beider Seiten.
    const ereignisse = await db.ticketEvent.findMany({
      where: { ticketId: { in: [w.ticket.id, ergebnis.kindTicketId] }, type: "SPLIT" },
    });
    expect(ereignisse).toHaveLength(2);
  });

  it("verlangt Betreff und Beschreibung", async () => {
    const w = await welt();
    const basis = {
      quellTicketId: w.ticket.id,
      zielTeamId: w.hm.id,
      nachrichtenIds: [],
      nachbehandlung: "offen_lassen" as const,
      actor: w.actor,
    };

    await expect(
      ticketAbspalten({ ...basis, betreff: "", beschreibung: "Eine Dose fehlt." }),
    ).rejects.toThrow(/Betreff/);
    await expect(
      ticketAbspalten({ ...basis, betreff: "Dose setzen", beschreibung: " " }),
    ).rejects.toThrow(/beschreiben/);
  });
});

describe.runIf(verfuegbar)("Fortsetzung des Ursprungstickets", () => {
  beforeEach(leereDatenbank);

  it("lässt es bei der Wahl offen lassen unverändert weiterlaufen", async () => {
    const w = await welt();

    await ticketAbspalten({
      quellTicketId: w.ticket.id,
      zielTeamId: w.hm.id,
      betreff: "Netzwerkdose setzen",
      beschreibung: "In Raum 12 fehlt eine Dose.",
      nachrichtenIds: [],
      nachbehandlung: "offen_lassen",
      actor: w.actor,
    });

    const eltern = await db.ticket.findUniqueOrThrow({ where: { id: w.ticket.id } });
    expect(eltern.status).toBe(TicketStatus.IN_BEARBEITUNG);
    expect(eltern.waitingForChild).toBe(false);
  });

  it("stellt es bei der Wahl warten auf Teilvorgang zurück", async () => {
    const w = await welt();

    await ticketAbspalten({
      quellTicketId: w.ticket.id,
      zielTeamId: w.hm.id,
      betreff: "Netzwerkdose setzen",
      beschreibung: "In Raum 12 fehlt eine Dose.",
      nachrichtenIds: [],
      nachbehandlung: "warten_auf_teilvorgang",
      actor: w.actor,
    });

    const eltern = await db.ticket.findUniqueOrThrow({ where: { id: w.ticket.id } });
    expect(eltern.status).toBe(TicketStatus.ZURUECKGESTELLT);
    expect(eltern.waitingForChild).toBe(true);
  });

  it("schließt es bei der Wahl schließen samt Abschlussnotiz", async () => {
    const w = await welt();

    await ticketAbspalten({
      quellTicketId: w.ticket.id,
      zielTeamId: w.hm.id,
      betreff: "Netzwerkdose setzen",
      beschreibung: "In Raum 12 fehlt eine Dose.",
      nachrichtenIds: [],
      nachbehandlung: "schliessen",
      abschlussnotiz: "Der Rechner läuft; die Dose übernimmt der Hausmeister.",
      actor: w.actor,
    });

    const eltern = await db.ticket.findUniqueOrThrow({ where: { id: w.ticket.id } });
    expect(eltern.status).toBe(TicketStatus.GELOEST);
    expect(eltern.resolvedAt).not.toBeNull();

    const nachrichten = await db.ticketMessage.findMany({
      where: { ticketId: w.ticket.id },
    });
    expect(nachrichten.map((n) => n.bodyText).join("\n")).toContain(
      "übernimmt der Hausmeister",
    );
  });
});

describe.runIf(verfuegbar)("Zurückgestelltes Ursprungsticket wieder wecken", () => {
  beforeEach(leereDatenbank);

  it("öffnet es, sobald der Teilvorgang erledigt ist", async () => {
    // Ohne dieses Wecken bliebe ein zurückgestellter Vorgang unbemerkt liegen,
    // bis jemand zufällig hineinschaut – genau der Fall, den das Abspalten
    // vermeiden soll.
    const w = await welt();

    const ergebnis = await ticketAbspalten({
      quellTicketId: w.ticket.id,
      zielTeamId: w.hm.id,
      betreff: "Netzwerkdose setzen",
      beschreibung: "In Raum 12 fehlt eine Dose.",
      nachrichtenIds: [],
      nachbehandlung: "warten_auf_teilvorgang",
      actor: w.actor,
    });

    const hausmeister = await personAnlegen({ name: "Petra Klein" });
    await statusAendern(ergebnis.kindTicketId, TicketStatus.GELOEST, {
      id: hausmeister.id,
      name: hausmeister.name,
    });

    const eltern = await db.ticket.findUniqueOrThrow({ where: { id: w.ticket.id } });
    expect(eltern.status).toBe(TicketStatus.OFFEN);
    expect(eltern.waitingForChild).toBe(false);

    // Im Protokoll steht, warum der Vorgang wieder auftaucht.
    const ereignis = await db.ticketEvent.findFirst({
      where: { ticketId: w.ticket.id, type: "SPLIT_CHILD_RESOLVED" },
    });
    expect(ereignis).not.toBeNull();

    // Und der Bearbeiter erfährt davon.
    const nachricht = await db.notification.findFirst({
      where: { userId: w.bearbeiter.id, ticketId: w.ticket.id },
    });
    expect(nachricht).not.toBeNull();
  });

  it("wartet, solange noch ein weiterer Teilvorgang offen ist", async () => {
    // Zwei Abspaltungen aus demselben Vorgang: Erst wenn beide erledigt sind,
    // wird das Ursprungsticket wieder aufgemacht.
    const w = await welt();

    const ersteAbspaltung = await ticketAbspalten({
      quellTicketId: w.ticket.id,
      zielTeamId: w.hm.id,
      zielSiteId: w.nord.id,
      betreff: "Netzwerkdose setzen",
      beschreibung: "In Raum 12 fehlt eine Dose.",
      nachrichtenIds: [],
      nachbehandlung: "warten_auf_teilvorgang",
      actor: w.actor,
    });

    const zweiteAbspaltung = await ticketAbspalten({
      quellTicketId: w.ticket.id,
      zielTeamId: w.hm.id,
      zielSiteId: w.sued.id,
      betreff: "Schreibtisch aufbauen",
      beschreibung: "Der Tisch steht noch zerlegt im Flur.",
      nachrichtenIds: [],
      nachbehandlung: "warten_auf_teilvorgang",
      actor: w.actor,
    });

    await statusAendern(ersteAbspaltung.kindTicketId, TicketStatus.GELOEST, w.actor);

    let eltern = await db.ticket.findUniqueOrThrow({ where: { id: w.ticket.id } });
    expect(eltern.status).toBe(TicketStatus.ZURUECKGESTELLT);
    expect(eltern.waitingForChild).toBe(true);

    await statusAendern(zweiteAbspaltung.kindTicketId, TicketStatus.GELOEST, w.actor);

    eltern = await db.ticket.findUniqueOrThrow({ where: { id: w.ticket.id } });
    expect(eltern.status).toBe(TicketStatus.OFFEN);
    expect(eltern.waitingForChild).toBe(false);
  });

  it("rührt ein Ursprungsticket nicht an, das nicht wartet", async () => {
    const w = await welt();

    const ergebnis = await ticketAbspalten({
      quellTicketId: w.ticket.id,
      zielTeamId: w.hm.id,
      betreff: "Netzwerkdose setzen",
      beschreibung: "In Raum 12 fehlt eine Dose.",
      nachrichtenIds: [],
      nachbehandlung: "offen_lassen",
      actor: w.actor,
    });

    await statusAendern(ergebnis.kindTicketId, TicketStatus.GELOEST, w.actor);

    const eltern = await db.ticket.findUniqueOrThrow({ where: { id: w.ticket.id } });
    expect(eltern.status).toBe(TicketStatus.IN_BEARBEITUNG);
  });
});

describe.runIf(verfuegbar)("Vorgänge zusammenführen", () => {
  beforeEach(leereDatenbank);

  it("holt Nachrichten in den führenden Vorgang und schließt den anderen", async () => {
    // Der Fall aus dem Alltag: Bei einem Netzausfall meldet sich ein ganzes
    // Haus, und es entstehen fünf Vorgänge zu einer Störung.
    const w = await welt();
    const zweiteMelderin = await personAnlegen({ name: "Bernd Wagner" });

    const doppelt = await ticketAnlegenRoh({
      number: "IT-2026-000002",
      subject: "Kein Netz im Erdgeschoss",
      queueId: w.eingangIt.id,
      teamId: w.it.id,
      requesterId: zweiteMelderin.id,
    });
    await db.ticketMessage.create({
      data: {
        ticketId: doppelt.id,
        type: MessageType.PUBLIC_REPLY,
        body: "Bei uns geht auch nichts mehr.",
        bodyText: "Bei uns geht auch nichts mehr.",
      },
    });

    await ticketZusammenfuehren({
      quellTicketId: doppelt.id,
      zielTicketId: w.ticket.id,
      actor: w.actor,
    });

    const geschlossen = await db.ticket.findUniqueOrThrow({
      where: { id: doppelt.id },
    });
    expect(geschlossen.status).toBe(TicketStatus.GESCHLOSSEN);

    const texte = (
      await db.ticketMessage.findMany({ where: { ticketId: w.ticket.id } })
    ).map((m) => m.bodyText);
    expect(texte.join("\n")).toContain("Bei uns geht auch nichts mehr.");

    // Die zweite Melderin verliert den Vorgang nicht aus den Augen.
    const beobachter = await db.ticketWatcher.findFirst({
      where: { ticketId: w.ticket.id, userId: zweiteMelderin.id },
    });
    expect(beobachter).not.toBeNull();
  });

  it("weist das Zusammenführen eines Vorgangs mit sich selbst ab", async () => {
    const w = await welt();

    await expect(
      ticketZusammenfuehren({
        quellTicketId: w.ticket.id,
        zielTicketId: w.ticket.id,
        actor: w.actor,
      }),
    ).rejects.toThrow(/sich selbst/);
  });
});
