import { describe, expect, it, beforeEach } from "vitest";

import { nextTicketNumber } from "@/server/services/number";
import {
  bereichAnlegen,
  datenbankErreichbar,
  db,
  leereDatenbank,
} from "./datenbank";

/**
 * Die Vergabe der laufenden Nummer ist die einzige Stelle im System, an der
 * zwei gleichzeitige Vorgänge sich in die Quere kommen können – und sie
 * passiert bei jedem einzelnen Ticket.
 *
 * Ein Zähler nach dem Muster „höchste Nummer suchen, eins dazu" funktioniert
 * im Test immer und fällt erst im Betrieb um: Wenn morgens um acht ein halbes
 * Haus gleichzeitig meldet oder der Mailabruf mehrere Nachrichten auf einmal
 * verarbeitet, vergeben zwei Transaktionen dieselbe Nummer, und eine davon
 * bricht mit einem Unique-Fehler ab. Für den Melder sieht das aus, als sei
 * seine Meldung verlorengegangen.
 *
 * Deshalb wird hier tatsächlich parallel vergeben.
 */

const verfuegbar = await datenbankErreichbar();

describe.runIf(verfuegbar)("Vergabe der Ticketnummer", () => {
  beforeEach(leereDatenbank);

  it("zählt je Bereich und Jahr fortlaufend hoch", async () => {
    const team = await bereichAnlegen({ key: "HM", name: "Hausmeister" });

    const nummern: string[] = [];
    for (let i = 0; i < 3; i++) {
      nummern.push(
        await db.$transaction((tx) => nextTicketNumber(tx, team.id, team.key)),
      );
    }

    const jahr = new Date().getFullYear();
    expect(nummern).toEqual([
      `HM-${jahr}-000001`,
      `HM-${jahr}-000002`,
      `HM-${jahr}-000003`,
    ]);
  });

  it("führt für jeden Bereich einen eigenen Zähler", async () => {
    // Sonst hätte der erste Vorgang des Hausmeisters die Nummer 4711, nur weil
    // die IT schon so viele hatte – die Nummer wäre nicht mehr sprechend.
    const it = await bereichAnlegen({ key: "IT", name: "IT" });
    const hm = await bereichAnlegen({ key: "HM", name: "Hausmeister" });
    const jahr = new Date().getFullYear();

    await db.$transaction((tx) => nextTicketNumber(tx, it.id, it.key));
    await db.$transaction((tx) => nextTicketNumber(tx, it.id, it.key));
    const erste = await db.$transaction((tx) =>
      nextTicketNumber(tx, hm.id, hm.key),
    );

    expect(erste).toBe(`HM-${jahr}-000001`);
  });

  it("beginnt in einem neuen Jahr wieder bei eins", async () => {
    const team = await bereichAnlegen({ key: "IT", name: "IT" });

    const alt = await db.$transaction((tx) =>
      nextTicketNumber(tx, team.id, team.key, new Date("2025-12-31T12:00:00Z")),
    );
    const neu = await db.$transaction((tx) =>
      nextTicketNumber(tx, team.id, team.key, new Date("2026-01-02T09:00:00Z")),
    );

    expect(alt).toBe("IT-2025-000001");
    expect(neu).toBe("IT-2026-000001");
  });

  it("richtet sich beim Jahreswechsel nach der Ortszeit", async () => {
    // 31.12. um 23:00 UTC ist in Berlin bereits der 1. Januar. Ein Vorgang,
    // der in der Silvesternacht um halb eins gemeldet wird, gehört ins neue
    // Jahr – so steht es auch auf jeder Rechnung und in jeder Statistik.
    const team = await bereichAnlegen({ key: "IT", name: "IT" });

    const nummer = await db.$transaction((tx) =>
      nextTicketNumber(tx, team.id, team.key, new Date("2025-12-31T23:30:00Z")),
    );

    expect(nummer).toBe("IT-2026-000001");
  });

  it("vergibt bei zwanzig gleichzeitigen Vorgängen zwanzig verschiedene Nummern", async () => {
    const team = await bereichAnlegen({ key: "IT", name: "IT" });

    const nummern = await Promise.all(
      Array.from({ length: 20 }, () =>
        db.$transaction((tx) => nextTicketNumber(tx, team.id, team.key), {
          timeout: 20_000,
          maxWait: 20_000,
        }),
      ),
    );

    // Keine doppelt …
    expect(new Set(nummern).size).toBe(20);
    // … und keine ausgelassen: genau 1 bis 20, in irgendeiner Reihenfolge.
    const laufend = nummern
      .map((n) => Number(n.split("-")[2]))
      .sort((a, b) => a - b);
    expect(laufend).toEqual(Array.from({ length: 20 }, (_, i) => i + 1));
  });

  it("legt die Zählerzeile auch bei gleichzeitigem Erstzugriff nur einmal an", async () => {
    // Der Erstzugriff auf einen frisch angelegten Bereich: Zwei Transaktionen
    // wollen gleichzeitig die Zählerzeile für dieses Jahr anlegen. Ohne
    // ON CONFLICT DO NOTHING bräche eine davon mit einem Unique-Fehler ab.
    const team = await bereichAnlegen({ key: "NEU", name: "Neuer Bereich" });

    await Promise.all(
      Array.from({ length: 5 }, () =>
        db.$transaction((tx) => nextTicketNumber(tx, team.id, team.key), {
          timeout: 20_000,
          maxWait: 20_000,
        }),
      ),
    );

    const zeilen = await db.ticketCounter.findMany({
      where: { teamId: team.id },
    });
    expect(zeilen).toHaveLength(1);
    expect(zeilen[0].value).toBe(5);
  });
});
