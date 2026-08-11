import { QueueMode } from "@prisma/client";
import { beforeAll, beforeEach, describe, expect, it } from "vitest";

import { RoutingError, resolveQueue, syncQueuesForTeam } from "@/server/services/routing";
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
 * Die Wegfindung ist die zentrale Zusage des Systems: Wer beim Melden
 * „Hausmeister" wählt und „Wohnheim Nord" angibt, landet beim Hausmeister in
 * Wohnheim Nord – nicht in der IT und nicht in einem anderen Haus.
 *
 * Wenn irgendetwas an diesem System schweigend falsch laufen darf, dann nicht
 * das hier: Ein Vorgang im falschen Eingang wird tagelang von niemandem
 * gesehen, weil jeder annimmt, der andere kümmere sich.
 */

const verfuegbar = await datenbankErreichbar();

describe.runIf(verfuegbar)("Bereich und Standort zu Eingang auflösen", () => {
  beforeAll(leereDatenbank);
  beforeEach(leereDatenbank);

  it("führt bei Eingang je Standort in genau das gewählte Haus", async () => {
    const nord = await standortAnlegen("Wohnheim Nord");
    const sued = await standortAnlegen("Wohnheim Süd");
    const hausmeister = await bereichAnlegen({
      key: "HM",
      name: "Hausmeister",
      queueMode: QueueMode.PER_SITE,
    });

    const eingangNord = await eingangAnlegen({
      teamId: hausmeister.id,
      siteId: nord.id,
      name: "Hausmeister Wohnheim Nord",
    });
    await eingangAnlegen({
      teamId: hausmeister.id,
      siteId: sued.id,
      name: "Hausmeister Wohnheim Süd",
    });

    const ziel = await resolveQueue(hausmeister.id, nord.id);

    expect(ziel.queueId).toBe(eingangNord.id);
    expect(ziel.siteId).toBe(nord.id);
  });

  it("verlangt bei Eingang je Standort eine Standortangabe", async () => {
    const hausmeister = await bereichAnlegen({
      name: "Hausmeister",
      queueMode: QueueMode.PER_SITE,
    });
    const standort = await standortAnlegen();
    await eingangAnlegen({ teamId: hausmeister.id, siteId: standort.id });

    await expect(resolveQueue(hausmeister.id, null)).rejects.toThrow(RoutingError);
  });

  it("nennt beim Standort ohne Eingang einen brauchbaren Grund", async () => {
    // Der Fall tritt auf, wenn ein neuer Standort dazukommt, für den noch kein
    // Hausmeister eingeteilt ist. Die Meldung muss sagen, was zu tun ist –
    // sonst ruft der Melder in der IT an, und darum ging es ja gerade nicht.
    const hausmeister = await bereichAnlegen({
      name: "Hausmeister",
      queueMode: QueueMode.PER_SITE,
    });
    const mitEingang = await standortAnlegen("Mit Hausmeister");
    const ohneEingang = await standortAnlegen("Ohne Hausmeister");
    await eingangAnlegen({ teamId: hausmeister.id, siteId: mitEingang.id });

    await expect(
      resolveQueue(hausmeister.id, ohneEingang.id),
    ).rejects.toThrow(/keinen\s+Eingang/);
  });

  it("übergeht bei einem gemeinsamen Eingang den Standort", async () => {
    // Die IT arbeitet zentral: Der Standort steht am Ticket, teilt es aber
    // nicht auf.
    const it = await bereichAnlegen({ key: "IT", name: "IT" });
    const zentral = await eingangAnlegen({ teamId: it.id, name: "IT" });
    const standort = await standortAnlegen();

    const ziel = await resolveQueue(it.id, standort.id);

    expect(ziel.queueId).toBe(zentral.id);
    // Der Standort geht nicht verloren – er wird nur nicht zum Sortierkriterium.
    expect(ziel.siteId).toBe(standort.id);
  });

  it("nimmt bei einem gemeinsamen Eingang auch gar keinen Standort an", async () => {
    const it = await bereichAnlegen({ key: "IT2", name: "IT zentral" });
    const zentral = await eingangAnlegen({ teamId: it.id });

    const ziel = await resolveQueue(it.id, null);

    expect(ziel.queueId).toBe(zentral.id);
    expect(ziel.siteId).toBeNull();
  });

  it("weist einen stillgelegten Bereich ab", async () => {
    const alt = await bereichAnlegen({ name: "Aufgelöster Bereich", isActive: false });
    await eingangAnlegen({ teamId: alt.id });

    await expect(resolveQueue(alt.id, null)).rejects.toThrow(
      /nimmt derzeit keine Tickets/,
    );
  });

  it("übergeht einen stillgelegten Eingang", async () => {
    const bereich = await bereichAnlegen({});
    const standort = await standortAnlegen();
    await eingangAnlegen({
      teamId: bereich.id,
      name: "Alter Eingang",
      isActive: false,
      sortOrder: 0,
    });
    const aktiv = await eingangAnlegen({
      teamId: bereich.id,
      siteId: standort.id,
      name: "Neuer Eingang",
      sortOrder: 1,
    });

    expect((await resolveQueue(bereich.id, null)).queueId).toBe(aktiv.id);
  });

  it("weist einen unbekannten Bereich ab", async () => {
    await expect(resolveQueue("gibt-es-nicht", null)).rejects.toThrow(
      /nicht gefunden/,
    );
  });
});

describe.runIf(verfuegbar)("Struktur eines Bereichs umstellen", () => {
  beforeEach(leereDatenbank);

  it("legt beim Wechsel auf Eingang je Standort für jeden Standort einen an", async () => {
    await standortAnlegen("Haus A");
    await standortAnlegen("Haus B");
    const bereich = await bereichAnlegen({ name: "Technischer Dienst" });
    await eingangAnlegen({ teamId: bereich.id, name: "Technischer Dienst" });

    await db.team.update({
      where: { id: bereich.id },
      data: { queueMode: QueueMode.PER_SITE },
    });
    await syncQueuesForTeam(bereich.id);

    const eingaenge = await db.queue.findMany({
      where: { teamId: bereich.id, isActive: true },
    });
    expect(eingaenge).toHaveLength(2);
    expect(eingaenge.every((e) => e.siteId !== null)).toBe(true);
  });

  it("legt einen Eingang beim Umstellen still, statt ihn zu löschen", async () => {
    // Das ist der Punkt, an dem eine Umstellung in der Verwaltung sonst
    // Vorgänge mitnähme. Ein Eingang mit Tickets darf nie verschwinden.
    const standort = await standortAnlegen("Haus A");
    const bereich = await bereichAnlegen({
      key: "TD",
      name: "Technischer Dienst",
      queueMode: QueueMode.PER_SITE,
    });
    const eingang = await eingangAnlegen({
      teamId: bereich.id,
      siteId: standort.id,
    });
    const melder = await personAnlegen();
    const ticket = await ticketAnlegenRoh({
      number: "TD-2026-000001",
      queueId: eingang.id,
      teamId: bereich.id,
      requesterId: melder.id,
    });

    await db.team.update({
      where: { id: bereich.id },
      data: { queueMode: QueueMode.SINGLE },
    });
    await syncQueuesForTeam(bereich.id);

    const danach = await db.queue.findUnique({ where: { id: eingang.id } });
    expect(danach).not.toBeNull();
    expect(danach!.isActive).toBe(false);

    // Der Vorgang liegt unverändert weiter dort.
    const weiterhin = await db.ticket.findUnique({ where: { id: ticket.id } });
    expect(weiterhin!.queueId).toBe(eingang.id);
  });

  it("weckt einen stillgelegten Eingang beim Zurückstellen wieder auf", async () => {
    // Wer die Struktur versehentlich umstellt und zurückstellt, soll seine
    // Eingänge samt Freigaben wiederbekommen und nicht neue anlegen müssen.
    const standort = await standortAnlegen("Haus A");
    const bereich = await bereichAnlegen({
      name: "Technischer Dienst",
      queueMode: QueueMode.PER_SITE,
    });
    const eingang = await eingangAnlegen({
      teamId: bereich.id,
      siteId: standort.id,
    });

    await db.team.update({
      where: { id: bereich.id },
      data: { queueMode: QueueMode.SINGLE },
    });
    await syncQueuesForTeam(bereich.id);

    await db.team.update({
      where: { id: bereich.id },
      data: { queueMode: QueueMode.PER_SITE },
    });
    await syncQueuesForTeam(bereich.id);

    const danach = await db.queue.findUnique({ where: { id: eingang.id } });
    expect(danach!.isActive).toBe(true);

    const anzahl = await db.queue.count({
      where: { teamId: bereich.id, siteId: standort.id },
    });
    expect(anzahl).toBe(1);
  });
});
