import { expect, test } from "@playwright/test";

import { KONTEN, anmelden, pruefeBarrierefreiheit } from "./hilfen";

/**
 * Das Abspalten eines Teilvorgangs – der ausdrücklich genannte Kernwunsch.
 *
 * Durchgespielt wird der Fall, für den es gedacht ist: Ein Vorgang kommt in
 * die IT, ein Teil davon geht den Hausmeister an, und die IT gibt genau diesen
 * Teil weiter, ohne ihren eigenen Anteil aus der Hand zu geben und ohne den
 * Melder zu bitten, ein zweites Ticket zu schreiben.
 */

test.describe("Teilvorgang abspalten", () => {
  test("gibt einen Teil an den Hausmeister und stellt den eigenen zurück", async ({
    page,
  }) => {
    // Vorbereitung: ein frischer Vorgang in der IT, damit der Test nicht auf
    // einen bestimmten Zustand der Demo-Daten angewiesen ist.
    const betreff = `Neuer Arbeitsplatz Abnahme ${Date.now()}`;

    await anmelden(page, KONTEN.melderin);
    await page.goto("/neu");
    await page.getByRole("button", { name: /^IT/ }).click();
    await page.getByLabel("Worum geht es?").fill(betreff);
    await page
      .getByLabel("Beschreiben Sie es bitte genauer")
      .fill(
        "Der Rechner ist eingerichtet, aber es fehlt eine Netzwerkdose am Platz.",
      );
    await page.getByRole("button", { name: "Meldung absenden" }).click();
    await expect(
      page.getByRole("heading", { name: "Ihre Meldung ist angekommen" }),
    ).toBeVisible({ timeout: 30_000 });

    const nummer = (await page.locator("span.font-mono").first().textContent())!.trim();
    expect(nummer).toMatch(/^IT-/);

    // Jetzt als IT: den baulichen Teil abspalten.
    await page.context().clearCookies();
    await anmelden(page, KONTEN.it);
    await page.goto(`/tickets/${nummer}`);

    await page.getByRole("button", { name: "Teil abspalten" }).click();
    await expect(
      page.getByRole("heading", { name: "Teilvorgang abspalten" }),
    ).toBeVisible();

    // 1. Zielbereich
    const dialog = page.getByRole("dialog");
    await dialog.getByRole("button", { name: /Hausmeister/ }).click();

    // Bei einem Bereich mit Eingang je Standort ist der Standort Pflicht.
    await expect(dialog.getByLabel("Standort")).toBeVisible();

    // 2. Auftrag
    await dialog.getByLabel("Betreff").fill("Netzwerkdose setzen");
    await dialog
      .getByLabel("Beschreibung")
      .fill("Am neuen Arbeitsplatz fehlt eine Dose neben dem Schreibtisch.");

    // 4. Der eigene Vorgang wartet auf den Teilvorgang.
    await dialog
      .getByText("Zurückstellen, bis der Teilvorgang erledigt ist")
      .click();

    await dialog.getByRole("button", { name: "Teilvorgang anlegen" }).click();

    // Der Ursprungsvorgang zeigt die Verknüpfung und ist zurückgestellt.
    await expect(page.getByText(/HM-\d{4}-\d{6}/).first()).toBeVisible({
      timeout: 20_000,
    });
    await expect(page.getByText("Zurückgestellt").first()).toBeVisible();
  });

  test("hält den Abspalten-Dialog barrierefrei", async ({ page }) => {
    await anmelden(page, KONTEN.it);
    await page.goto("/tickets");
    await page.locator("a[href^='/tickets/']").first().click();
    await page.waitForURL(/\/tickets\/[A-Z]/);

    await page.getByRole("button", { name: "Teil abspalten" }).click();
    await expect(
      page.getByRole("heading", { name: "Teilvorgang abspalten" }),
    ).toBeVisible();

    await pruefeBarrierefreiheit(page);
  });
});
