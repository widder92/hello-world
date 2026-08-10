import { AssetStatus, HandoverDirection } from "@prisma/client";
import { afterAll, beforeAll, beforeEach, describe, expect, it } from "vitest";

import {
  db,
  bereichAnlegen,
  datenbankErreichbar,
  geraetAnlegen,
  geraeteartAnlegen,
  leereDatenbank,
  personAnlegen,
} from "./datenbank";
import {
  InventarFehler,
  naechsteInventarnummer,
  statusWechseln,
} from "@/server/services/inventar";
import { entferneVerwaisteAnhaenge } from "@/server/services/attachments";

/**
 * Was sich nur gegen eine echte Datenbank prüfen lässt: die Nummernvergabe
 * unter Gleichzeitigkeit (sie lebt von einer Zeilensperre) und `statusWechseln`
 * (Gerät, Chronik und Protokoll entstehen in einer Transaktion – ein Nachbau
 * würde genau das prüfen, was man selbst hineingeschrieben hat).
 */

const erreichbar = await datenbankErreichbar();
const wenn = erreichbar ? describe : describe.skip;

if (!erreichbar) {
  console.warn(
    "[tests/db] Testdatenbank nicht erreichbar – Inventartests übersprungen.",
  );
}

wenn("Inventar gegen die Datenbank", () => {
  let bereichId: string;
  let artId: string;
  let artKey: string;
  let itler: { id: string; name: string };
  let anna: { id: string; name: string };
  let bernd: { id: string; name: string };

  beforeAll(async () => {
    await leereDatenbank();
  });

  afterAll(async () => {
    await db.$disconnect();
  });

  beforeEach(async () => {
    await leereDatenbank();

    const bereich = await bereichAnlegen({ hasInventory: true });
    bereichId = bereich.id;

    const art = await geraeteartAnlegen({ key: "NB", name: "Notebook" });
    artId = art.id;
    artKey = art.key;

    itler = await personAnlegen({ name: "Klaus Hoffmann" });
    anna = await personAnlegen({ name: "Anna Bergmann" });
    bernd = await personAnlegen({ name: "Bernd Kaiser" });
  });

  // --- Nummernvergabe ------------------------------------------------------

  describe("Inventarnummern", () => {
    it("zählt je Geräteart hoch und füllt auf fünf Stellen auf", async () => {
      const erste = await db.$transaction((tx) =>
        naechsteInventarnummer(tx, artId, artKey),
      );
      const zweite = await db.$transaction((tx) =>
        naechsteInventarnummer(tx, artId, artKey),
      );

      expect(erste).toBe("NB-00001");
      expect(zweite).toBe("NB-00002");
    });

    it("vergibt bei gleichzeitigem Anlegen keine Nummer doppelt", async () => {
      // Der eigentliche Grund für die Zeilensperre: Zwei Personen legen im
      // selben Moment ein Gerät an. Eine doppelte Nummer wäre nicht nur
      // unschön – zwei Aufkleber mit derselben Nummer machen die Geräte
      // ununterscheidbar.
      const nummern = await Promise.all(
        Array.from({ length: 12 }, () =>
          db.$transaction((tx) => naechsteInventarnummer(tx, artId, artKey)),
        ),
      );

      expect(new Set(nummern).size).toBe(12);
      expect([...nummern].sort()).toEqual(
        Array.from({ length: 12 }, (_, i) => `NB-${String(i + 1).padStart(5, "0")}`),
      );
    });

    it("führt je Geräteart einen eigenen Zähler", async () => {
      const andere = await geraeteartAnlegen({ key: "TAB", name: "Tablet" });

      await db.$transaction((tx) => naechsteInventarnummer(tx, artId, artKey));
      const tablet = await db.$transaction((tx) =>
        naechsteInventarnummer(tx, andere.id, andere.key),
      );

      expect(tablet).toBe("TAB-00001");
    });
  });

  // --- Statuswechsel -------------------------------------------------------

  describe("statusWechseln", () => {
    it("schreibt bei einer Herausgabe Gerät, Chronik und Protokoll", async () => {
      const geraet = await geraetAnlegen({ kindId: artId, teamId: bereichId });

      const ergebnis = await statusWechseln(
        geraet.id,
        AssetStatus.HERAUSGEGEBEN,
        {
          userId: anna.id,
          condition: "Neugerät",
          accessories: "Netzteil, Tasche",
        },
        itler,
      );

      const nachher = await db.asset.findUniqueOrThrow({
        where: { id: geraet.id },
        include: { events: true, handovers: true },
      });

      expect(nachher.status).toBe(AssetStatus.HERAUSGEGEBEN);
      expect(nachher.assignedToId).toBe(anna.id);
      expect(nachher.assignedAt).not.toBeNull();

      // Chronik und Protokoll müssen beide da sein – ein herausgegebenes
      // Gerät ohne Protokoll wäre im Streitfall wertlos.
      expect(nachher.handovers).toHaveLength(1);
      expect(nachher.handovers[0].direction).toBe(HandoverDirection.AUSGABE);
      expect(nachher.handovers[0].userName).toBe("Anna Bergmann");
      expect(nachher.handovers[0].accessories).toBe("Netzteil, Tasche");
      expect(nachher.events).toHaveLength(1);
      expect(nachher.events[0].toStatus).toBe(AssetStatus.HERAUSGEGEBEN);
      expect(nachher.events[0].actorName).toBe("Klaus Hoffmann");

      expect(ergebnis.protokoll?.protocolNo).toMatch(/^UP-\d{4}-\d{6}$/);
    });

    it("nennt im Rücknahmeprotokoll den bisherigen Besitzer", async () => {
      // Die Verwechslung liegt nahe: Bei einer Rücknahme gibt es keinen
      // Empfänger, sondern jemanden, der abgibt. Stünde dort niemand oder die
      // falsche Person, wäre das Protokoll nicht zu gebrauchen.
      const geraet = await geraetAnlegen({
        kindId: artId,
        teamId: bereichId,
        status: AssetStatus.HERAUSGEGEBEN,
        assignedToId: anna.id,
      });

      await statusWechseln(geraet.id, AssetStatus.LAGER, {}, itler);

      const protokoll = await db.assetHandover.findFirstOrThrow({
        where: { assetId: geraet.id, direction: HandoverDirection.RUECKNAHME },
      });

      expect(protokoll.userName).toBe("Anna Bergmann");
      expect(protokoll.actorName).toBe("Klaus Hoffmann");

      const nachher = await db.asset.findUniqueOrThrow({
        where: { id: geraet.id },
      });
      expect(nachher.assignedToId).toBeNull();
      expect(nachher.assignedAt).toBeNull();
    });

    it("gibt ein Gerät direkt an die nächste Person weiter", async () => {
      const geraet = await geraetAnlegen({
        kindId: artId,
        teamId: bereichId,
        status: AssetStatus.HERAUSGEGEBEN,
        assignedToId: anna.id,
      });

      await statusWechseln(
        geraet.id,
        AssetStatus.HERAUSGEGEBEN,
        { userId: bernd.id },
        itler,
      );

      const nachher = await db.asset.findUniqueOrThrow({
        where: { id: geraet.id },
        include: { handovers: true },
      });

      expect(nachher.assignedToId).toBe(bernd.id);
      expect(nachher.handovers[0].userName).toBe("Bernd Kaiser");
    });

    it("verlangt beim Aussondern einen Grund", async () => {
      const geraet = await geraetAnlegen({ kindId: artId, teamId: bereichId });

      await expect(
        statusWechseln(geraet.id, AssetStatus.AUSGESONDERT, {}, itler),
      ).rejects.toThrow(InventarFehler);

      // Und das Gerät steht danach unverändert da – die Transaktion hat
      // nichts halb geschrieben.
      const unveraendert = await db.asset.findUniqueOrThrow({
        where: { id: geraet.id },
        include: { events: true },
      });
      expect(unveraendert.status).toBe(AssetStatus.LAGER);
      expect(unveraendert.events).toHaveLength(0);
    });

    it("löst beim Aussondern die Zuweisung und hält den Grund fest", async () => {
      const geraet = await geraetAnlegen({
        kindId: artId,
        teamId: bereichId,
        status: AssetStatus.HERAUSGEGEBEN,
        assignedToId: anna.id,
      });

      await statusWechseln(
        geraet.id,
        AssetStatus.AUSGESONDERT,
        { note: "Displayschaden, Reparatur unwirtschaftlich." },
        itler,
      );

      const nachher = await db.asset.findUniqueOrThrow({
        where: { id: geraet.id },
      });

      // Bliebe die Zuweisung stehen, stünde ein entsorgtes Gerät weiter
      // unter „Meine Geräte".
      expect(nachher.assignedToId).toBeNull();
      expect(nachher.status).toBe(AssetStatus.AUSGESONDERT);
      expect(nachher.retiredAt).not.toBeNull();
      expect(nachher.retiredReason).toContain("unwirtschaftlich");
    });

    it("lässt die Rückholung aus der Aussonderung nur mit Leitungsrecht", async () => {
      const geraet = await geraetAnlegen({
        kindId: artId,
        teamId: bereichId,
        status: AssetStatus.AUSGESONDERT,
      });

      await expect(
        statusWechseln(
          geraet.id,
          AssetStatus.LAGER,
          { note: "War doch noch da.", istLeitung: false },
          itler,
        ),
      ).rejects.toThrow(/Bereichsleitung/);

      await expect(
        statusWechseln(
          geraet.id,
          AssetStatus.LAGER,
          { note: "War doch noch da.", istLeitung: true },
          itler,
        ),
      ).resolves.toBeTruthy();
    });

    it("weist einen unzulässigen Schritt ab", async () => {
      const geraet = await geraetAnlegen({
        kindId: artId,
        teamId: bereichId,
        status: AssetStatus.AUSGESONDERT,
      });

      await expect(
        statusWechseln(
          geraet.id,
          AssetStatus.HERAUSGEGEBEN,
          { userId: anna.id, istLeitung: true },
          itler,
        ),
      ).rejects.toThrow(InventarFehler);
    });

    it("nummeriert Übergabeprotokolle fortlaufend", async () => {
      const a = await geraetAnlegen({ kindId: artId, teamId: bereichId });
      const b = await geraetAnlegen({ kindId: artId, teamId: bereichId });

      const erste = await statusWechseln(
        a.id,
        AssetStatus.HERAUSGEGEBEN,
        { userId: anna.id },
        itler,
      );
      const zweite = await statusWechseln(
        b.id,
        AssetStatus.HERAUSGEGEBEN,
        { userId: bernd.id },
        itler,
      );

      const jahr = new Date().getFullYear();
      expect(erste.protokoll?.protocolNo).toBe(`UP-${jahr}-000001`);
      expect(zweite.protokoll?.protocolNo).toBe(`UP-${jahr}-000002`);
    });
  });

  // --- Anhänge -------------------------------------------------------------

  describe("Aufräumen verwaister Anhänge", () => {
    it("lässt ein Gerätefoto stehen", async () => {
      // Regression: Die Aufräumroutine löschte alles ohne Vorgang. Gerätefotos
      // haben keinen Vorgang – sie wären nach einem Tag verschwunden.
      const geraet = await geraetAnlegen({ kindId: artId, teamId: bereichId });
      const vorgestern = new Date(Date.now() - 48 * 60 * 60 * 1000);

      const foto = await db.attachment.create({
        data: {
          filename: "notebook.jpg",
          mimeType: "image/jpeg",
          size: 1024,
          storageKey: "2026-08/foto-test",
          createdAt: vorgestern,
        },
      });
      await db.asset.update({
        where: { id: geraet.id },
        data: { photoId: foto.id },
      });

      // Ein zweiter Anhang, der an nichts hängt – der soll weg.
      await db.attachment.create({
        data: {
          filename: "abgebrochen.pdf",
          mimeType: "application/pdf",
          size: 512,
          storageKey: "2026-08/verwaist-test",
          createdAt: vorgestern,
        },
      });

      const entfernt = await entferneVerwaisteAnhaenge(24);

      expect(entfernt).toBe(1);
      expect(
        await db.attachment.findUnique({ where: { id: foto.id } }),
      ).not.toBeNull();
    });

    it("lässt auch eine am Gerät hängende Rechnung stehen", async () => {
      const geraet = await geraetAnlegen({ kindId: artId, teamId: bereichId });
      const rechnung = await db.attachment.create({
        data: {
          assetId: geraet.id,
          filename: "rechnung.pdf",
          mimeType: "application/pdf",
          size: 2048,
          storageKey: "2026-08/rechnung-test",
          createdAt: new Date(Date.now() - 48 * 60 * 60 * 1000),
        },
      });

      await entferneVerwaisteAnhaenge(24);

      expect(
        await db.attachment.findUnique({ where: { id: rechnung.id } }),
      ).not.toBeNull();
    });
  });
});
