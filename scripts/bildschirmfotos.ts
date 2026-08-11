/**
 * Bildschirmfotos der Hauptansichten – für Vorstellungen und Absprachen.
 *
 * Gedacht für den Fall, dass man das System zeigen muss, ohne es vorführen zu
 * können: in einer Besprechung ohne Rechner, in einer Vorlage, per Mail an
 * jemanden, der es nicht selbst starten will.
 *
 * Aufruf (die Anwendung muss laufen und die Demo-Daten enthalten):
 *   npm run bilder
 *   npm run bilder -- --adresse=http://localhost:3000 --ziel=bilder
 */

import { mkdirSync } from "node:fs";
import path from "node:path";

import { chromium, type Page } from "@playwright/test";

const CHROMIUM =
  process.env.PLAYWRIGHT_CHROMIUM_PATH ?? "/opt/pw-browsers/chromium";
const PASSWORT = "Willkommen-2026-Demo";

function option(name: string, standard: string): string {
  const treffer = process.argv
    .slice(2)
    .find((a) => a.startsWith(`--${name}=`));
  return treffer ? treffer.slice(name.length + 3) : standard;
}

const ADRESSE = option("adresse", "http://localhost:3000");
const ZIEL = option("ziel", "bildschirmfotos");

/** Was aufgenommen wird – Reihenfolge wie in einer Vorstellung. */
const AUFNAHMEN: {
  datei: string;
  konto: string | null;
  pfad: string;
  titel: string;
  /** Vor der Aufnahme noch etwas anklicken. */
  vorher?: (page: Page) => Promise<void>;
}[] = [
  {
    datei: "01-anmeldung",
    konto: null,
    pfad: "/anmelden",
    titel: "Anmeldung mit Dienstkonto oder Microsoft-Konto",
  },
  {
    datei: "02-bereich-waehlen",
    konto: "anna.bergmann@example.org",
    pfad: "/neu",
    titel: "Melden, Schritt 1: An wen geht die Meldung?",
  },
  {
    datei: "03-meldeformular",
    konto: "anna.bergmann@example.org",
    pfad: "/neu",
    titel: "Melden, Schritt 2: Der Standort ist bereits vorbelegt",
    vorher: async (page) => {
      await page.getByRole("button", { name: /Hausmeister/ }).click();
      await page
        .getByRole("heading", { name: "Meldung an Hausmeister" })
        .waitFor();
    },
  },
  {
    datei: "04-einfache-ansicht",
    konto: "claudia.schmitt@example.org",
    pfad: "/neu",
    titel: "Dieselbe Seite in großer Schrift und einfacher Ansicht",
  },
  {
    datei: "05-meine-meldungen",
    konto: "anna.bergmann@example.org",
    pfad: "/meine-tickets",
    titel: "Was die Melderin von ihren Vorgängen sieht",
  },
  {
    datei: "06-uebersicht",
    konto: "klaus.hoffmann@example.org",
    pfad: "/dashboard",
    titel: "Übersicht im vollen Arbeitsplatz",
  },
  {
    datei: "07-vorgang",
    konto: "klaus.hoffmann@example.org",
    pfad: "/tickets/IT-2026-000001",
    titel: "Ein Vorgang mit Verlauf und Antwortfeld",
  },
  {
    datei: "08-abspalten",
    konto: "klaus.hoffmann@example.org",
    pfad: "/tickets/IT-2026-000001",
    titel: "Teilvorgang an einen anderen Bereich abspalten",
    vorher: async (page) => {
      await page.getByRole("button", { name: "Teil abspalten" }).click();
      await page
        .getByRole("heading", { name: "Teilvorgang abspalten" })
        .waitFor();
    },
  },
  {
    datei: "09-ticketliste",
    konto: "klaus.hoffmann@example.org",
    pfad: "/tickets",
    titel: "Alle Vorgänge mit Filtern",
  },
  {
    datei: "10-schlanke-stufe",
    konto: "marion.fischer@example.org",
    pfad: "/",
    titel: "Die schlanke Ausbaustufe: kein Sammeleingang, keine Ampel",
  },
  {
    datei: "11-verwaltung-bereiche",
    konto: "admin@example.org",
    pfad: "/verwaltung/bereiche",
    titel: "Hier wird die Ausbaustufe je Bereich eingestellt",
  },
  {
    datei: "12-verwaltung-benutzer",
    konto: "admin@example.org",
    pfad: "/verwaltung/benutzer",
    titel: "Wer in welchem Bereich und an welchen Standorten arbeitet",
  },
  {
    datei: "13-auswertung",
    konto: "admin@example.org",
    pfad: "/statistik",
    titel: "Auswertung",
  },

  // --- IT-Inventar ---------------------------------------------------------
  {
    datei: "14-inventar-liste",
    konto: "klaus.hoffmann@example.org",
    pfad: "/inventar",
    titel: "Das Inventar: Status, Kostenstelle, Besitz auf einen Blick",
  },
  {
    datei: "15-inventar-geraet",
    konto: "klaus.hoffmann@example.org",
    pfad: "/inventar?q=NB-00104",
    titel: "Ein Leihgerät, das seinen Kreis schon durchlaufen hat",
    vorher: async (page) => {
      await page.getByRole("link", { name: /Leihnotebook 1/ }).first().click();
      await page.getByRole("heading", { name: "Chronik" }).waitFor();
    },
  },
  {
    datei: "16-inventar-anlegen",
    konto: "klaus.hoffmann@example.org",
    pfad: "/inventar/neu",
    titel: "Gerät anlegen: Hersteller und Modell werden ausgewählt",
    vorher: async (page) => {
      await page.getByLabel("Hersteller").selectOption({ label: "Lenovo" });
      await page.getByLabel("Modell").selectOption({ label: "ThinkPad L14" });
      await page.getByLabel("Bezeichnung").fill("Notebook Verwaltung Buchhaltung");
      await page.getByLabel("Seriennummer").fill("PF7XK4402");
    },
  },
  {
    datei: "17-inventar-herausgeben",
    konto: "klaus.hoffmann@example.org",
    pfad: "/inventar?q=NB-00104",
    titel: "Herausgeben: Person suchen, Zubehör festhalten",
    vorher: async (page) => {
      await page.getByRole("link", { name: /Leihnotebook 1/ }).first().click();
      await page.getByRole("button", { name: "Herausgeben" }).click();
      await page.getByLabel("Person").fill("Bergmann");
      await page.getByRole("button", { name: /Anna Bergmann/ }).click();
      await page.getByLabel("Zustand").fill("Gebraucht, guter Zustand");
      await page.getByLabel("Zubehör").fill("Netzteil, Tasche");
    },
  },
  {
    datei: "18-uebergabeprotokoll",
    konto: "klaus.hoffmann@example.org",
    pfad: "/inventar?q=NB-00101",
    titel: "Übergabeprotokoll zum Ausdrucken und Unterschreiben",
    vorher: async (page) => {
      await page
        .getByRole("link", { name: /Notebook Wohnbereichsleitung Nord/ })
        .first()
        .click();
      await page.getByRole("link", { name: /UP-\d{4}-\d{6}/ }).first().click();
      await page.getByRole("heading", { name: "Übergabeprotokoll" }).waitFor();
    },
  },
  {
    datei: "19-inventar-auswertung",
    konto: "admin@example.org",
    pfad: "/inventar/auswertung",
    titel: "Anschaffungswerte je Kostenstelle",
  },
  {
    datei: "20-meine-geraete",
    konto: "anna.bergmann@example.org",
    pfad: "/meine-geraete",
    titel: "Was Mitarbeitende von ihren Geräten sehen – nur zum Ansehen",
  },
];

async function anmelden(page: Page, konto: string) {
  await page.goto(`${ADRESSE}/anmelden`);
  await page.getByLabel("E-Mail-Adresse").fill(konto);
  await page.getByLabel("Passwort").fill(PASSWORT);
  await page.getByRole("button", { name: "Anmelden" }).click();
  await page.waitForURL((url) => !url.pathname.startsWith("/anmelden"), {
    timeout: 30_000,
  });
}

async function main() {
  const ziel = path.resolve(ZIEL);
  mkdirSync(ziel, { recursive: true });

  const browser = await chromium.launch({ executablePath: CHROMIUM });

  let angemeldetAls: string | null = null;
  let context = await browser.newContext({
    viewport: { width: 1440, height: 900 },
    deviceScaleFactor: 2,
    locale: "de-DE",
    timezoneId: "Europe/Berlin",
  });
  let page = await context.newPage();

  for (const aufnahme of AUFNAHMEN) {
    // Kontowechsel: frischer Kontext, damit keine Sitzung hängen bleibt.
    if (aufnahme.konto !== angemeldetAls) {
      await context.close();
      context = await browser.newContext({
        viewport: { width: 1440, height: 900 },
        deviceScaleFactor: 2,
        locale: "de-DE",
        timezoneId: "Europe/Berlin",
      });
      page = await context.newPage();
      if (aufnahme.konto) await anmelden(page, aufnahme.konto);
      angemeldetAls = aufnahme.konto;
    }

    await page.goto(`${ADRESSE}${aufnahme.pfad}`);
    await page.waitForLoadState("networkidle").catch(() => {});
    if (aufnahme.vorher) await aufnahme.vorher(page);

    const datei = path.join(ziel, `${aufnahme.datei}.png`);
    await page.screenshot({ path: datei, fullPage: false });
    console.info(`✓ ${aufnahme.datei}.png — ${aufnahme.titel}`);
  }

  await context.close();
  await browser.close();
  console.info(`\nFertig. ${AUFNAHMEN.length} Bilder liegen in ${ziel}`);
}

main().catch((fehler) => {
  console.error(fehler);
  process.exit(1);
});
