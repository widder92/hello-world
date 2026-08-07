import { describe, expect, it } from "vitest";

import { fromZonedTime } from "date-fns-tz";

import {
  addBusinessMinutes,
  businessMinutesBetween,
  istInDienstzeit,
  toBusinessHoursConfig,
  type BusinessHoursConfig,
} from "@/lib/business-hours";

/**
 * Die Dienstzeiten-Rechnung entscheidet, ob die Ampel am Montagmorgen rot
 * steht oder nicht. Rechnet sie falsch, ist entweder alles rot – dann schaut
 * niemand mehr hin – oder nichts, dann ist sie wertlos.
 */

/** Mo–Do 08:00–16:30, Fr 08:00–14:00, Wochenende frei. */
const REGEL: BusinessHoursConfig = {
  timezone: "Europe/Berlin",
  schedule: {
    "1": [["08:00", "16:30"]],
    "2": [["08:00", "16:30"]],
    "3": [["08:00", "16:30"]],
    "4": [["08:00", "16:30"]],
    "5": [["08:00", "14:00"]],
    "6": [],
    "7": [],
  },
  holidays: new Set<string>(),
};

/** Mit geteiltem Dienst: Vormittag und Nachmittag mit Pause dazwischen. */
const GETEILT: BusinessHoursConfig = {
  ...REGEL,
  schedule: {
    ...REGEL.schedule,
    "1": [
      ["08:00", "12:00"],
      ["13:00", "17:00"],
    ],
  },
};

/**
 * Ortszeit in Europe/Berlin als echter Zeitpunkt.
 *
 * Nutzt dieselbe Umrechnung wie die Anwendung selbst, statt den Versatz zu
 * raten – ein Testhelfer, der die Sommerzeit anders bestimmt als der Code,
 * prüft am Ende nur sich selbst.
 */
function berlin(text: string): Date {
  return fromZonedTime(text.replace("T", " "), "Europe/Berlin");
}

describe("Fristen innerhalb der Dienstzeit", () => {
  it("rechnet innerhalb eines Tages einfach weiter", () => {
    // Montag 09:00 + 2 Std. = Montag 11:00
    const ergebnis = addBusinessMinutes(berlin("2026-08-03T09:00:00"), 120, REGEL);
    expect(ergebnis.toISOString()).toBe(berlin("2026-08-03T11:00:00").toISOString());
  });

  it("überspringt den Feierabend und rechnet am nächsten Morgen weiter", () => {
    // Montag 16:00 + 2 Std.: 30 Min. bis 16:30, Rest ab Dienstag 08:00 → 09:30
    const ergebnis = addBusinessMinutes(berlin("2026-08-03T16:00:00"), 120, REGEL);
    expect(ergebnis.toISOString()).toBe(berlin("2026-08-04T09:30:00").toISOString());
  });

  it("überspringt das Wochenende – der wichtigste Fall", () => {
    // Freitag 13:50 + 4 Std.: 10 Min. bis 14:00, Rest ab Montag 08:00 → 11:50.
    // Ohne diese Rechnung wäre der Vorgang Freitagabend überfällig gewesen.
    const ergebnis = addBusinessMinutes(berlin("2026-08-07T13:50:00"), 240, REGEL);
    expect(ergebnis.toISOString()).toBe(berlin("2026-08-10T11:50:00").toISOString());
  });

  it("überspringt Feiertage", () => {
    const mitFeiertag: BusinessHoursConfig = {
      ...REGEL,
      // Montag, 3. August ist hier Feiertag.
      holidays: new Set(["2026-08-03"]),
    };

    // Freitag 13:50 + 4 Std.: 10 Min. bis 14:00, Montag fällt aus,
    // Rest ab Dienstag 08:00 → 11:50.
    const ergebnis = addBusinessMinutes(
      berlin("2026-07-31T13:50:00"),
      240,
      mitFeiertag,
    );
    expect(ergebnis.toISOString()).toBe(berlin("2026-08-04T11:50:00").toISOString());
  });

  it("startet erst mit der Dienstzeit, wenn außerhalb gemeldet wird", () => {
    // Sonntag 20:00 + 1 Std. → Montag 09:00
    const ergebnis = addBusinessMinutes(berlin("2026-08-02T20:00:00"), 60, REGEL);
    expect(ergebnis.toISOString()).toBe(berlin("2026-08-03T09:00:00").toISOString());
  });

  it("berücksichtigt geteilten Dienst mit Mittagspause", () => {
    // Montag 11:30 + 1 Std.: 30 Min. bis 12:00, Pause, Rest ab 13:00 → 13:30
    const ergebnis = addBusinessMinutes(
      berlin("2026-08-03T11:30:00"),
      60,
      GETEILT,
    );
    expect(ergebnis.toISOString()).toBe(berlin("2026-08-03T13:30:00").toISOString());
  });

  it("rechnet über die Sommerzeitumstellung hinweg korrekt", () => {
    // Ende Oktober 2026: Umstellung in der Nacht zum Sonntag, 25.10.
    // Freitag 23.10. 13:00 + 4 Std.: 1 Std. bis 14:00, Rest ab Montag 26.10.
    // 08:00 → 11:00 Ortszeit (dann bereits Winterzeit).
    const ergebnis = addBusinessMinutes(berlin("2026-10-23T13:00:00"), 240, REGEL);
    expect(ergebnis.toISOString()).toBe(
      berlin("2026-10-26T11:00:00").toISOString(),
    );
  });

  it("fällt ohne jede Dienstzeit auf Kalenderzeit zurück", () => {
    const ohne: BusinessHoursConfig = {
      timezone: "Europe/Berlin",
      schedule: { "1": [], "2": [], "3": [], "4": [], "5": [], "6": [], "7": [] },
      holidays: new Set(),
    };

    // Ohne diesen Rückfall entstünde eine Endlosschleife.
    const start = berlin("2026-08-03T09:00:00");
    const ergebnis = addBusinessMinutes(start, 120, ohne);
    expect(ergebnis.getTime() - start.getTime()).toBe(120 * 60 * 1000);
  });
});

describe("Verstrichene Dienstzeit", () => {
  it("zählt nur die Zeit innerhalb der Fenster", () => {
    // Montag 15:30 bis Dienstag 09:30: 60 Min. + 90 Min. = 150 Min.
    const minuten = businessMinutesBetween(
      berlin("2026-08-03T15:30:00"),
      berlin("2026-08-04T09:30:00"),
      REGEL,
    );
    expect(minuten).toBe(150);
  });

  it("zählt über ein Wochenende hinweg nichts dazu", () => {
    // Freitag 14:00 (Dienstschluss) bis Montag 08:00 (Dienstbeginn) = 0
    const minuten = businessMinutesBetween(
      berlin("2026-08-07T14:00:00"),
      berlin("2026-08-10T08:00:00"),
      REGEL,
    );
    expect(minuten).toBe(0);
  });

  it("liefert 0, wenn das Ende vor dem Beginn liegt", () => {
    expect(
      businessMinutesBetween(
        berlin("2026-08-04T09:00:00"),
        berlin("2026-08-03T09:00:00"),
        REGEL,
      ),
    ).toBe(0);
  });
});

describe("Prüfung auf Dienstzeit", () => {
  it("erkennt einen Zeitpunkt innerhalb der Dienstzeit", () => {
    expect(istInDienstzeit(berlin("2026-08-03T10:00:00"), REGEL)).toBe(true);
  });

  it("erkennt Feierabend", () => {
    expect(istInDienstzeit(berlin("2026-08-03T17:00:00"), REGEL)).toBe(false);
  });

  it("erkennt das Wochenende", () => {
    expect(istInDienstzeit(berlin("2026-08-08T10:00:00"), REGEL)).toBe(false);
  });
});

describe("Aufbau der Konfiguration", () => {
  it("bildet Feiertage unabhängig von der Zeitzone auf den richtigen Tag ab", () => {
    // Feiertage werden als UTC-Mitternacht gespeichert. Ohne die
    // UTC-Auswertung rutschte der 1. Mai in Berlin auf den 30. April.
    const config = toBusinessHoursConfig({
      timezone: "Europe/Berlin",
      schedule: REGEL.schedule,
      holidays: [{ date: new Date("2026-05-01T00:00:00Z") }],
    });

    expect(config.holidays.has("2026-05-01")).toBe(true);
    expect(config.holidays.has("2026-04-30")).toBe(false);
  });
});
