import { expect, test } from "@playwright/test";

import { KONTEN, anmelden } from "./hilfen";

/**
 * Was die Oberfläche nicht anzeigt, ist damit noch nicht geschützt: Adressen
 * lassen sich von Hand eintippen, und ein ausgeblendeter Knopf ist eine
 * Höflichkeit, keine Sicherung.
 *
 * Hier wird deshalb genau das versucht, was ein neugieriger Mensch versuchen
 * würde – fremde Vorgänge über die Adresszeile aufrufen.
 */

test.describe("Zugriff über die Adresszeile", () => {
  test("weist eine Melderin bei einem fremden Vorgang ab", async ({ page }) => {
    await anmelden(page, KONTEN.melderin);

    // Ein Vorgang der IT, den sie nicht gemeldet hat.
    const antwort = await page.goto("/tickets/IT-2026-000001");

    // Entweder eine klare Abweisung oder gar nicht erst gefunden – auf keinen
    // Fall der Inhalt.
    expect(antwort?.status()).toBeGreaterThanOrEqual(400);
  });

  test("weist einen Hausmeister bei einem IT-Vorgang ab", async ({ page }) => {
    // Beide arbeiten im Haus, beide sind Mitarbeitende – trotzdem gehen die
    // Vorgänge der IT den Hausmeister nichts an.
    await anmelden(page, KONTEN.hausmeister);

    const antwort = await page.goto("/tickets/IT-2026-000001");
    expect(antwort?.status()).toBeGreaterThanOrEqual(400);
  });

  test("weist einen nicht freigegebenen Eingang desselben Bereichs ab", async ({
    page,
  }) => {
    // Der eigentliche Prüfstein der standortgenauen Trennung. Beide arbeiten
    // im Bereich Hausmeister; Frank Berger ist aber nicht für die Schule
    // freigegeben, die Bereichsleitung schon.
    await anmelden(page, KONTEN.hausmeisterLeitung);

    // Die Eingänge stehen mit dem Namen ihres Standorts in der Navigation.
    const schuleLink = page.locator("nav a[href^='/eingang/']", {
      hasText: "Förderschule",
    });
    await expect(
      schuleLink,
      "Die Bereichsleitung sieht alle Eingänge ihres Bereichs",
    ).toBeVisible();
    const schuleId = (await schuleLink.getAttribute("href"))!.split("/").pop()!;

    // Jetzt derselbe Eingang als Kollege ohne diese Freigabe.
    await page.context().clearCookies();
    await anmelden(page, KONTEN.hausmeister);

    await expect(page.locator("nav a[href^='/eingang/']").first()).toBeVisible();
    expect(await page.locator("nav").first().innerText()).not.toContain(
      "Förderschule",
    );

    const antwort = await page.goto(`/eingang/${schuleId}`);
    expect(antwort?.status()).toBeGreaterThanOrEqual(400);
  });

  test("lässt eine Melderin nicht in die Verwaltung", async ({ page }) => {
    await anmelden(page, KONTEN.melderin);

    const antwort = await page.goto("/verwaltung/bereiche");
    const abgewiesen =
      (antwort?.status() ?? 0) >= 400 || !page.url().includes("/verwaltung");

    expect(abgewiesen).toBe(true);
  });

  test("weist den Anhang eines fremden Vorgangs ab", async ({ page }) => {
    // Die Auslieferung von Anhängen prüft die Rechte am Vorgang, nicht nur die
    // Kenntnis der Kennung.
    await anmelden(page, KONTEN.melderin);

    const antwort = await page.goto(
      "/api/uploads/00000000-0000-0000-0000-000000000000",
    );
    expect(antwort?.status()).toBeGreaterThanOrEqual(400);
  });

  test("führt nach dem Abmelden zurück auf die Anmeldeseite", async ({ page }) => {
    // Im fertigen Bau lauscht der Server auf 0.0.0.0. Ohne eine ausdrücklich
    // gesetzte öffentliche Adresse baut Auth.js seine Weiterleitungen aus
    // genau dieser Bindeadresse – und `http://0.0.0.0:3000/anmelden` lässt
    // sich nicht aufrufen. Wer sich abmeldet, landete dann auf einer
    // Fehlerseite des Browsers statt bei der Anmeldung.
    await anmelden(page, KONTEN.melderin);

    await page.getByRole("button", { name: /Menü für/ }).click();
    await page.getByRole("menuitem", { name: "Abmelden" }).click();

    await page.waitForURL(/\/anmelden/, { timeout: 20_000 });
    expect(page.url()).not.toContain("0.0.0.0");

    // Und die Sitzung ist tatsächlich beendet.
    await page.goto("/dashboard");
    await expect(page).toHaveURL(/\/anmelden/);
  });

  test("führt ohne Anmeldung auf die Anmeldeseite", async ({ page }) => {
    await page.context().clearCookies();
    await page.goto("/dashboard");

    await expect(page).toHaveURL(/\/anmelden/);
  });
});
