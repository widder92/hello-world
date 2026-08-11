import { expect, test } from "@playwright/test";

import { KONTEN, anmelden, pruefeBarrierefreiheit } from "./hilfen";

/**
 * Der Verwaltungsbereich.
 *
 * Hier wird eingestellt, was der Plan als einstellbar verspricht – vor allem
 * die Ausbaustufe je Bereich und die Struktur der Eingänge. Beides ist
 * eingreifend genug, dass es nicht nur „irgendwie geht", sondern auch
 * nachvollziehbar sein muss.
 */

/** Alle Seiten der Verwaltung, damit keine unbemerkt kaputtgeht. */
const SEITEN = [
  "/verwaltung",
  "/verwaltung/bereiche",
  "/verwaltung/eingaenge",
  "/verwaltung/benutzer",
  "/verwaltung/mailkonten",
  "/verwaltung/kategorien",
  "/verwaltung/sla",
  "/verwaltung/dienstzeiten",
  "/verwaltung/standorte",
  "/verwaltung/geraete",
  "/verwaltung/textbausteine",
  "/verwaltung/vorlagen",
  "/verwaltung/wissen",
  "/verwaltung/protokoll",
  "/verwaltung/import",
] as const;

test.describe("Verwaltung", () => {
  test("liefert jede Seite aus", async ({ page }) => {
    // Fünfzehn Seiten nacheinander; im Entwicklungsmodus wird jede beim ersten
    // Aufruf noch übersetzt.
    test.setTimeout(300_000);
    await anmelden(page, KONTEN.admin);

    for (const pfad of SEITEN) {
      const antwort = await page.goto(pfad);
      expect(antwort?.status(), `Seite ${pfad}`).toBeLessThan(400);
      await expect(
        page.getByRole("heading", { level: 1 }).first(),
        `Überschrift auf ${pfad}`,
      ).toBeVisible();
    }
  });

  test("hält die Verwaltung barrierefrei", async ({ page }) => {
    await anmelden(page, KONTEN.admin);

    // Stichprobe über die Seiten mit den meisten Bedienelementen – dort ist
    // ein Fehler am wahrscheinlichsten.
    for (const pfad of [
      "/verwaltung/bereiche",
      "/verwaltung/benutzer",
      "/verwaltung/mailkonten",
      "/verwaltung/dienstzeiten",
    ]) {
      await page.goto(pfad);
      await expect(page.getByRole("heading", { level: 1 }).first()).toBeVisible();
      await pruefeBarrierefreiheit(page);
    }
  });

  test("zeigt die Ausbaustufe jedes Bereichs an", async ({ page }) => {
    await anmelden(page, KONTEN.admin);
    await page.goto("/verwaltung/bereiche");

    // Beide Stufen kommen in den Demo-Daten vor und müssen auf einen Blick
    // unterscheidbar sein – daran hängt, was die Kolleginnen und Kollegen des
    // Bereichs überhaupt zu sehen bekommen.
    await expect(
      page.getByText("Voller Arbeitsplatz").first(),
    ).toBeVisible();
    await expect(page.getByText("Schlanke Ansicht").first()).toBeVisible();
  });
});

test.describe("Weitere Hauptseiten", () => {
  test("liefert Übersicht, Suche, Auswertung und Einstellungen aus", async ({
    page,
  }) => {
    await anmelden(page, KONTEN.it);

    for (const pfad of [
      "/dashboard",
      "/tickets",
      "/suche",
      "/wissen",
      "/einstellungen",
      "/meine-tickets",
      "/meine-tickets-bearbeitung",
    ]) {
      const antwort = await page.goto(pfad);
      expect(antwort?.status(), `Seite ${pfad}`).toBeLessThan(400);
    }
  });

  test("hält Übersicht, Suche und Einstellungen barrierefrei", async ({
    page,
  }) => {
    await anmelden(page, KONTEN.it);

    for (const pfad of ["/dashboard", "/suche", "/einstellungen", "/wissen"]) {
      await page.goto(pfad);
      await expect(page.getByRole("heading", { level: 1 }).first()).toBeVisible();
      await pruefeBarrierefreiheit(page);
    }
  });

  test("findet einen Vorgang über die Volltextsuche", async ({ page }) => {
    await anmelden(page, KONTEN.it);
    await page.goto("/suche?q=Drucker");

    // Entweder Treffer oder ein klarer Hinweis, dass es keine gibt – eine
    // leere Seite wäre beides nicht.
    await expect(page.getByRole("heading", { level: 1 }).first()).toBeVisible();
    const inhalt = await page.locator("main").innerText();
    expect(inhalt.length).toBeGreaterThan(20);
  });
});
