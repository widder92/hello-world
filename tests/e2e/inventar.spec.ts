import { expect, test } from "@playwright/test";

import { KONTEN, anmelden, pruefeBarrierefreiheit } from "./hilfen";

/**
 * Das IT-Inventar.
 *
 * Der Weg, der im Alltag zählt, ist der ganze: Gerät anlegen, herausgeben,
 * Protokoll drucken, zurücknehmen, aussondern. Jeder Schritt für sich sagt
 * wenig – erst zusammen zeigt sich, ob der Zustand am Ende noch stimmt.
 *
 * Dazu die beiden Grenzen: Wer kein Inventar führt, kommt nicht hinein, und
 * wer ein Gerät hat, sieht es ohne jeden Bedienknopf.
 */

test.describe("Inventar", () => {
  test("führt ein Gerät von der Anlage bis zur Aussonderung", async ({
    page,
  }) => {
    const kennung = Date.now();
    const bezeichnung = `Prüfnotebook ${kennung}`;

    // MAC-Adressen sind systemweit eindeutig – bei einer festen Adresse
    // scheiterte der zweite Testlauf an einem Gerät aus dem ersten. Sie wird
    // deshalb aus der Zeit gebildet: „02" als lokal verwaltetes Präfix, dahinter
    // fünf Bytes aus dem Zeitstempel.
    const bytes = (kennung % 0xff_ffff_ffff).toString(16).padStart(10, "0");
    const macGetippt = `02-${bytes.match(/.{2}/g)!.join("-")}`.toUpperCase();
    const macErwartet = `02:${bytes.match(/.{2}/g)!.join(":")}`.toLowerCase();

    await anmelden(page, KONTEN.it);

    // --- Anlegen -----------------------------------------------------------
    await page.goto("/inventar/neu");

    await page.getByLabel("Geräteart").selectOption({ label: "Notebook" });
    await page.getByLabel("Bezeichnung").fill(bezeichnung);
    await page.getByLabel("Seriennummer").fill(`SN-${kennung}`);

    // MAC in einer Schreibweise, die nicht die abgelegte ist – am Ende muss
    // trotzdem die einheitliche Form dastehen.
    await page.getByRole("button", { name: "Adresse hinzufügen" }).click();
    await page.getByLabel("MAC-Adresse 1").fill(macGetippt);

    await page.getByLabel("Anschaffungspreis").fill("1.299,00");

    await page.getByRole("button", { name: "Gerät anlegen" }).click();

    // Landet auf der Geräteseite, die Nummer hat das System vergeben.
    await page.waitForURL(
      (url) =>
        /^\/inventar\/[a-z0-9]+$/.test(url.pathname) &&
        !url.pathname.endsWith("/neu"),
      { timeout: 30_000 },
    );
    await expect(page.getByText(bezeichnung).first()).toBeVisible();
    await expect(page.getByText(/^NB-\d{5}$/).first()).toBeVisible();
    await expect(page.getByText("Im Lager").first()).toBeVisible();

    // MAC vereinheitlicht und Preis deutsch formatiert.
    await expect(page.getByText(macErwartet)).toBeVisible();
    await expect(page.getByText(/1\.299,00/)).toBeVisible();

    const geraeteSeite = page.url();

    // --- Herausgeben -------------------------------------------------------
    await page.getByRole("button", { name: "Herausgeben" }).click();
    await page.getByLabel("Person").fill("Bergmann");
    await page.getByRole("button", { name: /Anna Bergmann/ }).click();
    await page.getByLabel("Zustand").fill("Neugerät");
    await page.getByLabel("Zubehör").fill("Netzteil, Tasche");
    await page.getByRole("button", { name: "Speichern" }).click();

    // Direkt im Übergabeprotokoll – es soll ja unterschrieben werden.
    await page.waitForURL(/\/protokoll\/[a-z0-9]+$/, { timeout: 30_000 });
    await expect(
      page.getByRole("heading", { name: "Übergabeprotokoll" }),
    ).toBeVisible();
    await expect(page.getByText(/^UP-\d{4}-\d{6}$/)).toBeVisible();
    await expect(page.getByText("Anna Bergmann").first()).toBeVisible();
    await expect(page.getByText("Netzteil, Tasche")).toBeVisible();
    await expect(page.getByText(/Eigentum des Trägers/)).toBeVisible();

    // --- Zurücknehmen ------------------------------------------------------
    await page.goto(geraeteSeite);
    await expect(page.getByText("Herausgegeben").first()).toBeVisible();

    await page.getByRole("button", { name: "Zurücknehmen" }).click();
    await page.getByRole("button", { name: "Speichern" }).click();
    await page.waitForURL(/\/protokoll\/[a-z0-9]+$/, { timeout: 30_000 });

    // Auf dem Rücknahmeprotokoll steht, wer abgegeben hat.
    await expect(page.getByText("Rücknahme").first()).toBeVisible();
    await expect(page.getByText("Anna Bergmann").first()).toBeVisible();

    // --- Aussondern --------------------------------------------------------
    await page.goto(geraeteSeite);
    await page.getByRole("button", { name: "Aussondern" }).click();

    // Ohne Grund geht es nicht weiter.
    await page.getByRole("button", { name: "Speichern" }).click();
    await expect(page.getByRole("alert")).toContainText(/Begründung|festhalten/);

    await page
      .getByLabel("Grund")
      .fill("Displayschaden, Reparatur unwirtschaftlich.");
    await page.getByRole("button", { name: "Speichern" }).click();

    await expect(page.getByText("Ausgesondert").first()).toBeVisible({
      timeout: 15_000,
    });
    await expect(page.getByText(/unwirtschaftlich/).first()).toBeVisible();
  });

  test("findet ein Gerät über die Seriennummer", async ({ page }) => {
    // Am Telefon wird abgelesen, was auf dem Aufkleber steht – nicht die
    // Inventarnummer.
    await anmelden(page, KONTEN.it);
    await page.goto("/inventar");

    await page.getByLabel("Geräte suchen").fill("PF3KL201");
    await page.getByRole("button", { name: "Filtern" }).click();

    await expect(page.getByText("Notebook Wohnbereichsleitung Nord")).toBeVisible();
    await expect(page.getByText("1 Gerät")).toBeVisible();
  });

  test("findet ein Gerät über die MAC-Adresse in anderer Schreibweise", async ({
    page,
  }) => {
    await anmelden(page, KONTEN.it);
    await page.goto("/inventar");

    // Im Seed steht 3c:52:82:11:0a:01 – hier mit Bindestrichen und gross.
    await page.getByLabel("Geräte suchen").fill("3C-52-82-11-0A-01");
    await page.getByRole("button", { name: "Filtern" }).click();

    await expect(page.getByText("Notebook Wohnbereichsleitung Nord")).toBeVisible();
  });

  test("zeigt der Melderin ihre Geräte ohne Bedienknöpfe", async ({ page }) => {
    await anmelden(page, KONTEN.melderin);
    await page.goto("/meine-geraete");

    await expect(page.getByText("Notebook Wohnbereichsleitung Nord")).toBeVisible();
    await expect(page.getByText("NB-00101")).toBeVisible();

    // Kein Knopf, der den Zustand ändern würde – das macht die IT.
    for (const beschriftung of [
      "Herausgeben",
      "Zurücknehmen",
      "Aussondern",
      "Bearbeiten",
    ]) {
      await expect(
        page.getByRole("button", { name: beschriftung }),
      ).toHaveCount(0);
    }

    // Der Weg zur Meldung steht offen und nimmt das Gerät mit.
    await page
      .getByRole("link", { name: /Problem mit diesem Gerät melden/ })
      .first()
      .click();
    await page.waitForURL(/\/neu\?geraet=/, { timeout: 30_000 });
    await expect(page.getByText(/Betrifft Ihr Gerät/)).toBeVisible();
  });

  test("hält Melderinnen aus dem Inventar heraus", async ({ page }) => {
    await anmelden(page, KONTEN.melderin);
    await page.goto("/inventar");

    await expect(page).toHaveURL(/\/meine-geraete/);
  });

  test("hält Bereiche ohne Inventar heraus", async ({ page }) => {
    // Der Hausmeister arbeitet im System, sein Bereich führt aber kein
    // Inventar – der Schalter sitzt je Bereich.
    await anmelden(page, KONTEN.hausmeister);
    await page.goto("/inventar");

    await expect(page).toHaveURL(/\/meine-geraete/);
  });

  test("lässt Stammdaten nur die Leitung pflegen", async ({ page }) => {
    // Klaus Hoffmann ist Dispatcher in der IT, nicht Leitung.
    await anmelden(page, KONTEN.it);
    await page.goto("/inventar/stammdaten");
    await expect(page).toHaveURL(/\/inventar$/);

    // Die Administration kommt hinein.
    await page.context().clearCookies();
    await anmelden(page, KONTEN.admin);
    await page.goto("/inventar/stammdaten");
    await expect(
      page.getByRole("heading", { name: "Inventar-Stammdaten" }),
    ).toBeVisible();
    await expect(page.getByRole("button", { name: "Geräteart anlegen" })).toBeVisible();
  });

  test("rechnet die Auswertung je Kostenstelle zusammen", async ({ page }) => {
    await anmelden(page, KONTEN.it);
    await page.goto("/inventar/auswertung");

    await expect(
      page.getByRole("heading", { name: "Auswertung des Inventars" }),
    ).toBeVisible();
    await expect(page.getByText("Nach Kostenstelle")).toBeVisible();
    // Die Zeile steht im Seed als Kostenstelle 4310.
    await expect(page.getByText(/4310 – Wohnheim Nord/)).toBeVisible();
  });

  test("leitet die alte Geräteseite auf das Inventar um", async ({ page }) => {
    // Der Pfad steht in Lesezeichen und in älteren Vorgängen.
    await anmelden(page, KONTEN.it);
    await page.goto("/verwaltung/geraete");

    await expect(page).toHaveURL(/\/inventar/);
  });

  test("hält das Inventar barrierefrei", async ({ page }) => {
    await anmelden(page, KONTEN.admin);

    for (const pfad of [
      "/inventar",
      "/inventar/neu",
      "/inventar/stammdaten",
      "/inventar/auswertung",
    ]) {
      await page.goto(pfad);
      await pruefeBarrierefreiheit(page);
    }
  });

  test("hält Meine Geräte barrierefrei", async ({ page }) => {
    await anmelden(page, KONTEN.melderin);
    await page.goto("/meine-geraete");
    await pruefeBarrierefreiheit(page);
  });
});
