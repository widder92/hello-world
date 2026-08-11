import AxeBuilder from "@axe-core/playwright";
import { expect, type Page } from "@playwright/test";

/**
 * Gemeinsame Hilfen für die Abnahme im Browser.
 */

/** Passwort aller Demo-Konten aus `prisma/seed.ts`. */
export const DEMO_PASSWORT = "Willkommen-2026-Demo";

export const KONTEN = {
  /** Betreuerin im Wohnheim Nord, in keinem Bereich – sieht das Melder-Portal. */
  melderin: "anna.bergmann@example.org",
  /**
   * Hausmeister, voller Arbeitsplatz. Freigegeben für Wohnheim Nord
   * (Startansicht) und Verwaltung – die Schule ausdrücklich nicht.
   */
  hausmeister: "frank.berger@example.org",
  /** Bereichsleitung Hausmeister – sieht alle Eingänge des Bereichs. */
  hausmeisterLeitung: "dieter.wolf@example.org",
  /** IT, voller Arbeitsplatz, zentraler Eingang. */
  it: "klaus.hoffmann@example.org",
  /** Küche, schlanke Ausbaustufe. */
  kueche: "marion.fischer@example.org",
  admin: "admin@example.org",
} as const;

export async function anmelden(page: Page, email: string): Promise<void> {
  await page.goto("/anmelden");
  await page.getByLabel("E-Mail-Adresse").fill(email);
  await page.getByLabel("Passwort").fill(DEMO_PASSWORT);
  await page.getByRole("button", { name: "Anmelden" }).click();

  // Nach dem Anmelden leitet die Startseite dorthin, wo die Person arbeitet.
  await page.waitForURL((url) => !url.pathname.startsWith("/anmelden"), {
    timeout: 30_000,
  });
}

export async function abmelden(page: Page): Promise<void> {
  await page.context().clearCookies();
}

/**
 * Prüft eine Seite mit axe auf Verstöße gegen WCAG 2.1 AA.
 *
 * Die Anwendung wird von Menschen mit sehr unterschiedlichen Voraussetzungen
 * bedient – das ist bei einem Träger der Behindertenhilfe keine Kür. Geprüft
 * wird deshalb nicht nur, dass eine Seite funktioniert, sondern auch, dass sie
 * mit Tastatur und Vorleseprogramm benutzbar ist.
 */
export async function pruefeBarrierefreiheit(page: Page): Promise<void> {
  const ergebnis = await new AxeBuilder({ page })
    .withTags(["wcag2a", "wcag2aa", "wcag21a", "wcag21aa"])
    .analyze();

  const befunde = ergebnis.violations.map(
    (v) =>
      `${v.id} (${v.impact}): ${v.help}\n    ` +
      v.nodes
        .slice(0, 3)
        .map((n) => n.target.join(" "))
        .join("\n    "),
  );

  expect(befunde, `Barrierefreiheit auf ${page.url()}`).toEqual([]);
}
