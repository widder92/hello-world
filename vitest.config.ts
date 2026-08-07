import { defineConfig } from "vitest/config";
import path from "node:path";

export default defineConfig({
  test: {
    environment: "node",
    include: ["tests/unit/**/*.test.ts", "tests/db/**/*.test.ts"],
    // Die Tests unter tests/db teilen sich eine Datenbank und räumen sie
    // zwischen den Dateien leer – parallel liefen sie sich gegenseitig die
    // Daten weg.
    fileParallelism: false,
    testTimeout: 30_000,
    setupFiles: ["tests/setup.ts"],
    server: {
      // next-auth durch Vite laufen lassen statt an Node durchzureichen –
      // sonst greift der Verweis auf "next/server" weiter unten nicht.
      deps: { inline: ["next-auth", "@auth/core"] },
    },
  },
  resolve: {
    alias: [
      { find: /^@\//, replacement: path.resolve(__dirname, "./src") + "/" },
      // next-auth importiert "next/server" ohne Dateiendung. Next.js löst das
      // über seine Exportkarte auf, die Node-Auflösung von Vitest nicht –
      // ohne diesen Verweis bricht schon der Import von `policy.ts` ab.
      {
        find: /^next\/server$/,
        replacement: path.resolve(__dirname, "./node_modules/next/server.js"),
      },
    ],
  },
});
