import { dirname } from "node:path";
import { fileURLToPath } from "node:url";

import { FlatCompat } from "@eslint/eslintrc";

/**
 * ESLint-Konfiguration.
 *
 * `eslint-config-next` liegt noch im alten Format vor; `FlatCompat` übersetzt
 * es in die Flat-Config von ESLint 9. Ohne diese Datei fragt `next lint`
 * interaktiv nach einer Einrichtung und bleibt in jedem Lauf ohne Terminal
 * hängen.
 */
const compat = new FlatCompat({
  baseDirectory: dirname(fileURLToPath(import.meta.url)),
});

const config = [
  {
    ignores: [
      ".next/**",
      ".scratch/**",
      "next-env.d.ts",
      "node_modules/**",
      "playwright-report/**",
      "test-results/**",
      "uploads/**",
    ],
  },
  ...compat.extends("next/core-web-vitals", "next/typescript"),
  {
    rules: {
      // Ungenutzte Variablen sind ein Fehler, mit einer Ausnahme: ein
      // führender Unterstrich sagt ausdrücklich „wird hier nicht gebraucht",
      // etwa bei Platzhaltern in einer Signatur.
      "@typescript-eslint/no-unused-vars": [
        "error",
        {
          argsIgnorePattern: "^_",
          varsIgnorePattern: "^_",
          caughtErrorsIgnorePattern: "^_",
        },
      ],
    },
  },
];

export default config;
