import { describe, expect, it } from "vitest";

import { findTicketNumber, formatTicketNumber } from "@/server/services/number";

/**
 * Die Ticketnummer ist mehr als eine laufende Zahl: Sie steht im Betreff jeder
 * Benachrichtigung und ist der Anker, über den eine Antwortmail wieder zum
 * richtigen Vorgang findet, wenn die Mail-Kopfzeilen unterwegs verloren gehen.
 */
describe("Aufbau der Ticketnummer", () => {
  it("setzt Kürzel, Jahr und laufende Nummer zusammen", () => {
    expect(formatTicketNumber("HM", 2026, 87)).toBe("HM-2026-000087");
  });

  it("schreibt das Kürzel groß", () => {
    expect(formatTicketNumber("it", 2026, 1)).toBe("IT-2026-000001");
  });

  it("füllt bis sechs Stellen auf und wächst darüber hinaus", () => {
    expect(formatTicketNumber("IT", 2026, 1)).toBe("IT-2026-000001");
    expect(formatTicketNumber("IT", 2026, 1_234_567)).toBe("IT-2026-1234567");
  });
});

describe("Ticketnummer in einem Text finden", () => {
  it("findet die Nummer im Betreff einer Antwort", () => {
    expect(findTicketNumber("AW: [HM-2026-000087] Heizung defekt")).toBe(
      "HM-2026-000087",
    );
  });

  it("findet die Nummer auch ohne Klammern", () => {
    expect(findTicketNumber("Zu Vorgang IT-2026-000123 noch eine Frage")).toBe(
      "IT-2026-000123",
    );
  });

  it("ist bei der Schreibweise großzügig", () => {
    // Wer die Nummer von Hand abtippt, achtet nicht auf Großschreibung.
    expect(findTicketNumber("betrifft hm-2026-000087")).toBe("HM-2026-000087");
  });

  it("liefert null, wenn keine Nummer enthalten ist", () => {
    expect(findTicketNumber("Der Beamer im Raum 12 geht nicht an")).toBeNull();
  });

  it("nimmt die erste Nummer, wenn mehrere genannt sind", () => {
    expect(
      findTicketNumber("Siehe IT-2026-000001 und HM-2026-000002"),
    ).toBe("IT-2026-000001");
  });
});
