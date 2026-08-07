import { defineConfig, devices } from "@playwright/test";

/**
 * Abnahme im Browser.
 *
 * Die Tests laufen gegen die Anwendung mit den Demo-Daten – also gegen dieselbe
 * Datenbank wie `npm run dev`, nicht gegen die Testdatenbank der Vitest-Läufe.
 * Sie legen dabei Vorgänge an und ändern deren Zustand. Nach einem Lauf sind
 * die Demo-Daten also nicht mehr ganz unberührt; `npm run db:seed` stellt sie
 * wieder her.
 */

const BASIS = process.env.E2E_BASE_URL ?? "http://localhost:3000";

/**
 * Chromium liegt in dieser Umgebung bereits unter /opt/pw-browsers. Ohne den
 * ausdrücklichen Pfad sucht Playwright in seinem eigenen Zwischenspeicher und
 * verlangt ein `playwright install`, das hier weder nötig ist noch
 * durchläuft.
 */
const CHROMIUM = process.env.PLAYWRIGHT_CHROMIUM_PATH ?? "/opt/pw-browsers/chromium";

export default defineConfig({
  testDir: "./tests/e2e",
  // Server-Aktionen schreiben in dieselbe Datenbank; parallele Läufe kämen
  // sich bei Statuswechseln gegenseitig in die Quere.
  workers: 1,
  fullyParallel: false,
  forbidOnly: Boolean(process.env.CI),
  retries: process.env.CI ? 1 : 0,
  reporter: process.env.CI ? "github" : "list",

  timeout: 60_000,
  expect: { timeout: 10_000 },

  use: {
    baseURL: BASIS,
    locale: "de-DE",
    timezoneId: "Europe/Berlin",
    trace: "retain-on-failure",
    screenshot: "only-on-failure",
  },

  projects: [
    {
      name: "chromium",
      use: {
        ...devices["Desktop Chrome"],
        launchOptions: { executablePath: CHROMIUM },
      },
    },
  ],

  webServer: process.env.E2E_BASE_URL
    ? undefined
    : {
        command: "npm run dev",
        url: BASIS,
        reuseExistingServer: true,
        // Der erste Aufruf im Entwicklungsmodus übersetzt die Seite noch.
        timeout: 180_000,
      },
});
