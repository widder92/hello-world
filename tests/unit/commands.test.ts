import { TicketStatus } from "@prisma/client";
import { describe, expect, it } from "vitest";

import {
  beschreibeBefehl,
  erkenneBefehle,
  statusFuerBefehl,
} from "@/mail/commands";

/**
 * Steuerbefehle in Antwortmails sind das, was Kolleginnen und Kollegen ohne
 * eigenen Zugang überhaupt erst arbeitsfähig macht: antworten, `#erledigt`
 * darüberschreiben, fertig.
 *
 * Zwei Fehlerarten wären hier teuer. Ein nicht erkannter Befehl bleibt als
 * kryptische Zeile im Verlauf stehen und der Vorgang bleibt offen. Ein zu
 * eifrig erkannter Befehl schließt einen Vorgang, den niemand schließen
 * wollte – etwa weil das Wort in einem Zitat weiter unten stand.
 */
describe("Befehle erkennen", () => {
  it("erkennt einen einzelnen Befehl und entfernt die Zeile", () => {
    const { text, befehle } = erkenneBefehle("#erledigt\nSchloss ist getauscht.");

    expect(befehle).toHaveLength(1);
    expect(befehle[0].art).toBe("erledigt");
    expect(text).toBe("Schloss ist getauscht.");
  });

  it("erkennt mehrere Befehle hintereinander", () => {
    const { text, befehle } = erkenneBefehle(
      "#übernehmen\n#intern\nErsatzteil ist bestellt.",
    );

    expect(befehle.map((b) => b.art)).toEqual(["uebernehmen", "intern"]);
    expect(text).toBe("Ersatzteil ist bestellt.");
  });

  it("liest beim Weiterleiten den genannten Bereich mit", () => {
    const { text, befehle } = erkenneBefehle(
      "#an Hausmeister\nDie Steckdose fehlt noch.",
    );

    expect(befehle[0].art).toBe("weiterleiten");
    expect(befehle[0].ziel).toBe("hausmeister");
    expect(text).toBe("Die Steckdose fehlt noch.");
  });

  it("versteht Schreibweisen ohne Umlaut", () => {
    // Wer vom Handy schreibt, tippt eher "uebernehmen" als "übernehmen".
    expect(erkenneBefehle("#uebernehmen").befehle[0].art).toBe("uebernehmen");
    expect(erkenneBefehle("#geloest").befehle[0].art).toBe("erledigt");
  });

  it("überliest führende Leerzeilen", () => {
    const { befehle, text } = erkenneBefehle("\n\n#erledigt\n\nAlles gut.");

    expect(befehle).toHaveLength(1);
    expect(text).toBe("Alles gut.");
  });

  it("führt keinen Befehl aus, der mitten im Text steht", () => {
    // Der wichtigste Fall: Unter einer Antwort steht das Zitat der eigenen
    // Benachrichtigung. Stünde dort irgendwo "#erledigt", dürfte das nichts
    // auslösen.
    const text = [
      "Ich schaue morgen vorbei.",
      "",
      "> Bitte melden Sie sich mit #erledigt zurück.",
    ].join("\n");

    const ergebnis = erkenneBefehle(text);
    expect(ergebnis.befehle).toHaveLength(0);
    expect(ergebnis.text).toBe(text);
  });

  it("ignoriert eine unbekannte Raute-Zeile und behält sie im Text", () => {
    // "#3" ist keine Anweisung, sondern gehört zur Nachricht.
    const ergebnis = erkenneBefehle("#3 Etagen betroffen");

    expect(ergebnis.befehle).toHaveLength(0);
    expect(ergebnis.text).toBe("#3 Etagen betroffen");
  });

  it("lässt eine gewöhnliche Nachricht unangetastet", () => {
    const ergebnis = erkenneBefehle("Der Wasserhahn tropft weiterhin.");

    expect(ergebnis.befehle).toHaveLength(0);
    expect(ergebnis.text).toBe("Der Wasserhahn tropft weiterhin.");
  });

  it("kommt mit einem Befehl ohne weiteren Text zurecht", () => {
    const ergebnis = erkenneBefehle("#erledigt");

    expect(ergebnis.befehle).toHaveLength(1);
    expect(ergebnis.text).toBe("");
  });
});

describe("Wirkung der Befehle", () => {
  it("setzt bei #erledigt den Status gelöst", () => {
    expect(statusFuerBefehl("erledigt")).toBe(TicketStatus.GELOEST);
  });

  it("setzt bei #warten auf Rückmeldung des Melders", () => {
    expect(statusFuerBefehl("warten")).toBe(TicketStatus.WARTET_AUF_MELDER);
  });

  it("ändert bei #intern und #übernehmen den Status nicht", () => {
    // Eine interne Notiz ist eine Notiz und kein Statuswechsel; wer übernimmt,
    // entscheidet damit noch nicht über den Fortgang.
    expect(statusFuerBefehl("intern")).toBeNull();
    expect(statusFuerBefehl("uebernehmen")).toBeNull();
  });

  it("beschreibt Befehle lesbar fürs Protokoll", () => {
    expect(beschreibeBefehl({ art: "erledigt", zeile: "#erledigt" })).toBe(
      "#erledigt",
    );
    expect(
      beschreibeBefehl({
        art: "weiterleiten",
        ziel: "Hausmeister",
        zeile: "#an Hausmeister",
      }),
    ).toBe("#an Hausmeister");
  });
});
