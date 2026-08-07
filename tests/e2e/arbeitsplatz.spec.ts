import { expect, test, type Page } from "@playwright/test";

import { KONTEN, anmelden, pruefeBarrierefreiheit } from "./hilfen";

/**
 * Der Arbeitsplatz in beiden Ausbaustufen.
 *
 * Die schlanke Stufe ist bewusst keine ausgegraute Fassung der vollen: Wer nur
 * Tickets entgegennimmt und beantwortet, soll auch nur das sehen. Ein
 * Sammeleingang, den niemand benutzt, und eine Ampel ohne Vereinbarung
 * dahinter sind kein Angebot, sondern Ballast.
 *
 * Die Eingänge heißen in der Navigation nach ihrem Standort („Wohnheim Nord"),
 * nicht nach ihrer Art. Angesteuert werden sie deshalb über den Pfad und nicht
 * über einen festen Namen – sonst hinge der Test an den Demo-Daten.
 */

/** Erster Link auf einen Sammeleingang bzw. eine schlanke Ticketliste. */
function eingangsLink(page: Page, basis: "/eingang/" | "/unsere-tickets/") {
  return page.locator(`nav a[href^='${basis}']`).first();
}

test.describe("Voller Arbeitsplatz", () => {
  test("führt nach dem Anmelden direkt in den Arbeitsplatz", async ({ page }) => {
    await anmelden(page, KONTEN.hausmeister);

    // Wer in einem Bereich arbeitet, landet nicht im Melder-Portal.
    await expect(page).toHaveURL(/\/dashboard/);
    await expect(page.getByRole("heading", { level: 1 }).first()).toBeVisible();
  });

  test("lässt ein Ticket aus dem Sammeleingang ziehen", async ({ page }) => {
    // Der ausdrücklich gewünschte Ablauf, vollständig: Die Betreuerin meldet,
    // der Vorgang sammelt sich im Eingang ihres Hauses, und der Hausmeister
    // zieht ihn sich selbst heraus.
    //
    // Der Vorgang wird hier eigens angelegt, statt einen vorhandenen zu
    // nehmen: Ein Sammeleingang ist genau dann leer, wenn die Kolleginnen und
    // Kollegen ihre Arbeit machen – und ein Test, der von einem vollen Eingang
    // ausgeht, schlägt irgendwann ohne eigenes Zutun fehl.
    // Ohne Sonderzeichen, damit der Betreff später unverändert als Suchtext
    // taugt.
    const betreff = `Türklinke lose Abnahme ${Date.now()}`;

    await anmelden(page, KONTEN.melderin);
    await page.goto("/neu");
    await page.getByRole("button", { name: /Hausmeister/ }).click();
    await page.getByLabel("Worum geht es?").fill(betreff);
    await page
      .getByLabel("Beschreiben Sie es bitte genauer")
      .fill("Die Klinke am Gruppenraum 2 wackelt und fällt gleich ab.");
    await page.getByRole("button", { name: "Meldung absenden" }).click();
    await expect(
      page.getByRole("heading", { name: "Ihre Meldung ist angekommen" }),
    ).toBeVisible({ timeout: 30_000 });

    await page.context().clearCookies();
    await anmelden(page, KONTEN.hausmeister);

    // Ausdrücklich der Eingang ihres Hauses – dort und nirgends sonst muss
    // der Vorgang liegen.
    await page
      .locator("nav a[href^='/eingang/']", { hasText: "Wohnheim Nord" })
      .click();
    await page.waitForURL(/\/eingang\//);

    // Der eben gemeldete Vorgang liegt hier, nicht zugewiesen.
    const vorgang = page.getByRole("link", { name: betreff });
    await expect(vorgang).toBeVisible();
    await vorgang.click();
    await page.waitForURL(/\/tickets\/[A-Z]/);

    const ziehen = page.getByRole("button", { name: "Ticket ziehen" });
    await expect(ziehen).toBeVisible();
    await ziehen.click();

    // Danach ist der Vorgang zugewiesen, und der Knopf verschwindet.
    await expect(ziehen).toBeHidden({ timeout: 15_000 });
  });

  test("zeigt dem Hausmeister nur die für ihn freigegebenen Standorte", async ({
    page,
  }) => {
    // Der Kern der standortgenauen Trennung: Frank Berger ist für Wohnheim
    // Nord und die Verwaltung freigegeben, für die Schule ausdrücklich nicht.
    await anmelden(page, KONTEN.hausmeister);

    const navigation = page.locator("nav").first();
    await expect(navigation.getByText("Wohnheim Nord")).toBeVisible();
    await expect(navigation.getByText("Hausmeister Schule")).toHaveCount(0);
  });

  test("hält Sammeleingang und Ticketansicht barrierefrei", async ({ page }) => {
    await anmelden(page, KONTEN.hausmeister);

    await eingangsLink(page, "/eingang/").click();
    await page.waitForURL(/\/eingang\//);
    await pruefeBarrierefreiheit(page);

    // Der Weg über die Ticketliste statt über den Sammeleingang: Der Eingang
    // kann leer sein – gerade dann, wenn zuvor jemand den letzten Vorgang
    // gezogen hat. Die Liste zeigt auch die bereits zugewiesenen.
    await page.goto("/tickets");
    await pruefeBarrierefreiheit(page);

    await page.locator("a[href^='/tickets/']").first().click();
    await page.waitForURL(/\/tickets\/[A-Z]/);
    await pruefeBarrierefreiheit(page);
  });
});

test.describe("Schlanke Ausbaustufe", () => {
  test("zeigt der Küche keinen Sammeleingang", async ({ page }) => {
    await anmelden(page, KONTEN.kueche);

    // Kein Sammeleingang – der Bereichseintrag führt direkt auf die eine
    // Liste, die es in dieser Stufe gibt.
    await expect(page.locator("nav a[href^='/eingang/']")).toHaveCount(0);
    await expect(eingangsLink(page, "/unsere-tickets/")).toBeVisible();
  });

  test("kommt ohne Frist-Ampel und ohne Priorität aus", async ({ page }) => {
    await anmelden(page, KONTEN.kueche);
    await eingangsLink(page, "/unsere-tickets/").click();
    await page.waitForURL(/\/unsere-tickets\//);

    await expect(page.getByRole("heading", { level: 1 }).first()).toBeVisible();
    // Eine Ampel, hinter der keine Vereinbarung steht, erzeugt nur schlechtes
    // Gewissen ohne Nutzen – deshalb gibt es sie hier nicht.
    await expect(page.getByText(/überfällig|Frist läuft/i)).toHaveCount(0);
  });

  test("hält die schlanke Ansicht barrierefrei", async ({ page }) => {
    await anmelden(page, KONTEN.kueche);
    await eingangsLink(page, "/unsere-tickets/").click();
    await page.waitForURL(/\/unsere-tickets\//);
    await pruefeBarrierefreiheit(page);
  });
});
