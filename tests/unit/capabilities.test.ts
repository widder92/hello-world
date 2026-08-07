import { TeamMode } from "@prisma/client";
import { describe, expect, it } from "vitest";

import {
  capabilitiesFor,
  requireCapability,
  statusAuswahlFor,
} from "@/server/capabilities";

/**
 * Die Ausbaustufen sind eine ausdrückliche Anforderung: Nicht jeder Bereich
 * braucht das volle System, aber jeder Bereich muss Tickets annehmen,
 * beantworten, weitergeben und abschließen können.
 *
 * Diese Tests halten genau das fest. Wenn jemand später eine Funktion in die
 * schlanke Stufe hebt oder aus ihr entfernt, soll das eine bewusste
 * Entscheidung sein und keine Nebenwirkung.
 */
describe("Ausbaustufen der Bereiche", () => {
  it("gibt der vollen Stufe alle Funktionen", () => {
    const voll = capabilitiesFor(TeamMode.VOLL);
    expect(Object.values(voll).every(Boolean)).toBe(true);
  });

  it("lässt die schlanke Stufe alles Wesentliche am Ticket tun", () => {
    // Der Kern der Anforderung: Ein Bereich in der schlanken Stufe kann
    // Vorgänge vollständig bearbeiten – nur ohne den Apparat drumherum.
    const einfach = capabilitiesFor(TeamMode.EINFACH);

    expect(einfach.bearbeiten).toBe(true);
    expect(einfach.interneNotizen).toBe(true);
    expect(einfach.weiterleiten).toBe(true);
    expect(einfach.abspalten).toBe(true);
    expect(einfach.emailBearbeitung).toBe(true);
  });

  it("hält den Apparat aus der schlanken Stufe heraus", () => {
    const einfach = capabilitiesFor(TeamMode.EINFACH);

    expect(einfach.sammeleingang).toBe(false);
    expect(einfach.sla).toBe(false);
    expect(einfach.prioritaet).toBe(false);
    expect(einfach.zuweisung).toBe(false);
    expect(einfach.statistik).toBe(false);
    expect(einfach.autoVerteilung).toBe(false);
  });

  it("beschreibt beide Stufen mit denselben Schlüsseln", () => {
    // Sonst wäre eine Funktion in einer Stufe schlicht `undefined` – und
    // `undefined` ist im if-Zweig genauso falsch wie `false`, nur unauffälliger.
    expect(Object.keys(capabilitiesFor(TeamMode.VOLL)).sort()).toEqual(
      Object.keys(capabilitiesFor(TeamMode.EINFACH)).sort(),
    );
  });
});

describe("Absicherung der Server-Aktionen", () => {
  it("lässt eine vorhandene Funktion durch", () => {
    expect(() =>
      requireCapability(TeamMode.EINFACH, "weiterleiten"),
    ).not.toThrow();
  });

  it("wirft bei einer Funktion, die die Stufe nicht hat", () => {
    // Das Ausblenden des Knopfes im UI ist Bequemlichkeit; hier liegt der
    // eigentliche Schutz gegen einen von Hand abgesetzten Aufruf.
    expect(() => requireCapability(TeamMode.EINFACH, "sla")).toThrow(/sla/);
  });
});

describe("Statusauswahl je Stufe", () => {
  it("bietet in der schlanken Stufe nur drei Zustände an", () => {
    const auswahl = statusAuswahlFor(TeamMode.EINFACH);
    expect(auswahl).toEqual(["OFFEN", "WARTET_AUF_MELDER", "GELOEST"]);
  });

  it("bietet in der vollen Stufe den ganzen Satz an", () => {
    const auswahl = statusAuswahlFor(TeamMode.VOLL);
    expect(auswahl).toContain("IN_BEARBEITUNG");
    expect(auswahl).toContain("ZURUECKGESTELLT");
    expect(auswahl.length).toBeGreaterThan(
      statusAuswahlFor(TeamMode.EINFACH).length,
    );
  });
});
