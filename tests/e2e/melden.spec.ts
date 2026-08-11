import { expect, test } from "@playwright/test";

import { KONTEN, anmelden, pruefeBarrierefreiheit } from "./hilfen";

/**
 * Die zentrale Zusage des Systems, hier von vorn bis hinten durchgespielt:
 * Eine Betreuerin aus dem Wohnbereich schreibt dem Hausmeister, und der
 * Vorgang landet direkt beim Hausmeister ihres Hauses – nicht in der IT und
 * nicht an einem anderen Standort.
 */

test.describe("Meldung aus dem Wohnbereich an den Hausmeister", () => {
  test("landet direkt im Eingang des richtigen Standorts", async ({ page }) => {
    await anmelden(page, KONTEN.melderin);

    // Wer in keinem Bereich arbeitet, landet im Melder-Portal.
    await page.goto("/neu");
    await expect(
      page.getByRole("heading", { name: "An wen geht Ihre Meldung?" }),
    ).toBeVisible();

    // Schritt 1: den Bereich selbst wählen – das ist der Punkt, an dem die
    // Meldung ihren Weg nimmt.
    await page.getByRole("button", { name: /Hausmeister/ }).click();
    await expect(
      page.getByRole("heading", { name: "Meldung an Hausmeister" }),
    ).toBeVisible();

    const betreff = `Wasserhahn tropft (Abnahme ${Date.now()})`;
    await page.getByLabel("Worum geht es?").fill(betreff);
    await page
      .getByLabel("Beschreiben Sie es bitte genauer")
      .fill("In Gruppenraum 2 tropft der Wasserhahn seit gestern durchgehend.");
    await page.getByLabel("Raum oder Bereich").fill("Gruppenraum 2");

    // Der eigene Standort ist bereits vorbelegt – niemand soll ihn suchen
    // müssen.
    await expect(page.getByLabel("An welchem Standort?")).toHaveValue(/.+/);
    const standort = await page
      .getByLabel("An welchem Standort?")
      .locator("option:checked")
      .textContent();
    expect(standort).toContain("Wohnheim Nord");

    await page.getByRole("button", { name: "Meldung absenden" }).click();

    await expect(
      page.getByRole("heading", { name: "Ihre Meldung ist angekommen" }),
    ).toBeVisible({ timeout: 30_000 });

    // Die Nummer trägt das Kürzel des Hausmeisterbereichs, nicht das der IT.
    const nummer = await page.locator("span.font-mono").first().textContent();
    expect(nummer?.trim()).toMatch(/^HM-\d{4}-\d{6}$/);

    // Und der Vorgang liegt tatsächlich beim Hausmeister in Wohnheim Nord.
    await page.goto(`/tickets/${nummer!.trim()}`);
    await expect(page.getByText("Hausmeister Wohnheim Nord")).toBeVisible();
  });

  test("führt die Meldende ohne Umweg zu ihren Vorgängen", async ({ page }) => {
    await anmelden(page, KONTEN.melderin);
    await page.goto("/meine-tickets");

    await expect(
      page.getByRole("heading", { name: /Meine Meldungen|Meine Tickets/ }),
    ).toBeVisible();
  });

  test("hält das Meldeformular barrierefrei", async ({ page }) => {
    await anmelden(page, KONTEN.melderin);
    await page.goto("/neu");
    await pruefeBarrierefreiheit(page);

    await page.getByRole("button", { name: /Hausmeister/ }).click();
    await expect(
      page.getByRole("heading", { name: "Meldung an Hausmeister" }),
    ).toBeVisible();
    await pruefeBarrierefreiheit(page);
  });
});
