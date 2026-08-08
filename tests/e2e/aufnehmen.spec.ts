import { expect, test } from "@playwright/test";

import { KONTEN, anmelden, pruefeBarrierefreiheit } from "./hilfen";

/**
 * Vorgang für eine andere Person aufnehmen – der Telefonfall.
 *
 * Zwei Dinge entscheiden darüber, ob das im Alltag taugt: Der Vorgang muss
 * dem *Melder* gehören und nicht dem, der ihn aufgenommen hat – sonst findet
 * die Person ihn nie wieder. Und im Verlauf muss stehen, dass der Text eine
 * Mitschrift ist, damit später niemand die falsche Person anspricht.
 */

test.describe("Vorgang telefonisch aufnehmen", () => {
  test("legt den Vorgang für den Melder an und weist ihn mir zu", async ({
    page,
  }) => {
    const betreff = `Drucker klemmt Abnahme ${Date.now()}`;

    await anmelden(page, KONTEN.it);
    await page.goto("/aufnehmen");

    // 1. Melder über die Suche finden.
    await page.getByLabel("Nach Person suchen").fill("Bergmann");
    await page.getByRole("button", { name: /Anna Bergmann/ }).click();
    await expect(page.getByText("anna.bergmann@example.org")).toBeVisible();

    // 2. Anliegen mitschreiben.
    await page.getByLabel("Betreff").fill(betreff);
    await page
      .getByLabel("Mitschrift des Gesprächs")
      .fill("Ruft an: Der Drucker zieht kein Papier ein. Neustart half nicht.");

    // 3. Zuständigkeit.
    await page.getByRole("button", { name: /^IT/ }).click();

    await page.getByRole("button", { name: "Vorgang aufnehmen" }).click();

    // Landet direkt im neuen Vorgang.
    await page.waitForURL(/\/tickets\/[A-Z]/, { timeout: 30_000 });
    await expect(page.getByText(betreff).first()).toBeVisible();

    // Der Melder ist Anna Bergmann – nicht der aufnehmende IT-Kollege.
    await expect(page.getByText("Anna Bergmann").first()).toBeVisible();

    // Und im Verlauf steht, dass es eine Mitschrift ist.
    await expect(
      page.getByText(/Telefonisch aufgenommen von/).first(),
    ).toBeVisible();
  });

  test("erfasst auch eine Person ohne Konto", async ({ page }) => {
    // Externe Handwerksfirmen und Angehörige rufen an, haben aber kein Konto.
    const kennung = Date.now();
    await anmelden(page, KONTEN.it);
    await page.goto("/aufnehmen");

    await page.getByRole("button", { name: "Person hat kein Konto" }).click();
    await page.getByLabel("Name").fill("Firma Sanitär Schmitt");
    await page.getByLabel("E-Mail-Adresse").fill(`schmitt-${kennung}@example.net`);

    await page.getByLabel("Betreff").fill(`Rückruf Heizung ${kennung}`);
    await page
      .getByLabel("Mitschrift des Gesprächs")
      .fill("Firma meldet, dass das Ersatzteil erst nächste Woche kommt.");
    await page.getByRole("button", { name: /^IT/ }).click();

    await page.getByRole("button", { name: "Vorgang aufnehmen" }).click();
    await page.waitForURL(/\/tickets\/[A-Z]/, { timeout: 30_000 });
    await expect(page.getByText("Firma Sanitär Schmitt").first()).toBeVisible();
  });

  test("bleibt Meldern ohne Bereich verschlossen", async ({ page }) => {
    // Wer selbst meldet, nimmt das Portal – dort steht der eigene Name fest.
    await anmelden(page, KONTEN.melderin);
    await page.goto("/aufnehmen");

    await expect(page).toHaveURL(/\/neu/);
  });

  test("hält die Aufnahme barrierefrei", async ({ page }) => {
    await anmelden(page, KONTEN.it);
    await page.goto("/aufnehmen");
    await pruefeBarrierefreiheit(page);
  });
});
