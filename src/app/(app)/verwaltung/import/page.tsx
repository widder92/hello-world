import type { Metadata } from "next";
import { AlertTriangle, Terminal } from "lucide-react";

import { PageHeader } from "@/components/layout/page-header";
import { Card } from "@/components/ui/card";
import { db } from "@/server/db";
import { requireAdmin } from "@/server/policy";
import { TicketSource } from "@prisma/client";

export const metadata: Metadata = { title: "Datenübernahme" };

const ARTEN = [
  {
    typ: "standorte",
    titel: "Standorte",
    spalten: "Name; Kurzname; Art; Strasse; PLZ; Ort",
    hinweis:
      "Zuerst übernehmen – Benutzer und Vorgänge verweisen über den Namen darauf. Art: Schule, Wohnheim, Betreutes Wohnen, Verwaltung, Werkstatt.",
  },
  {
    typ: "benutzer",
    titel: "Benutzer",
    spalten: "Email; Name; Funktion; Abteilung; Standort; Telefon",
    hinweis:
      "Ohne Passwort – die Anmeldung läuft über Entra ID. Lokale Konten legen Sie bei Bedarf einzeln unter Benutzer an.",
  },
  {
    typ: "geraete",
    titel: "Geräte",
    spalten:
      "Nummer; Name; Hersteller; Modell; Seriennummer; Standort; Raum; Zugewiesen",
    hinweis:
      "Zugewiesen erwartet die E-Mail-Adresse. Vorhandene Nummern werden aktualisiert statt doppelt angelegt.",
  },
  {
    typ: "tickets",
    titel: "Alte Vorgänge",
    spalten:
      "Nummer; Betreff; Beschreibung; Bereich; Standort; Melder; Status; Erstellt; Erledigt",
    hinweis:
      "Die alte Nummer bleibt im Protokoll erhalten. Vorgänge bekommen neue Nummern, damit sie sich nicht mit laufenden überschneiden. Melder, die es nicht mehr gibt, landen auf einem Sammelkonto.",
  },
];

/**
 * Datenübernahme.
 *
 * Bewusst über die Kommandozeile statt als Datei-Upload: Ein Import über
 * mehrere tausend Zeilen dauert länger, als eine Weboberfläche sinnvoll offen
 * halten kann, und wer ihn ausführt, sollte die Ausgabe Zeile für Zeile
 * mitlesen können.
 */
export default async function ImportPage() {
  await requireAdmin();

  const [uebernommen, standorte, benutzer, geraete] = await Promise.all([
    db.ticket.count({ where: { source: TicketSource.IMPORT } }),
    db.site.count(),
    db.user.count(),
    db.asset.count(),
  ]);

  return (
    <>
      <PageHeader
        title="Datenübernahme"
        description="Übernahme aus dem Altsystem über CSV. Damit kommen Sie aus praktisch jedem Vorsystem heraus – auch aus einer gewachsenen Excel-Tabelle."
      />

      <div className="space-y-5 px-4 py-5 sm:px-6">
        <div className="grid gap-3 sm:grid-cols-4">
          {[
            ["Standorte", standorte],
            ["Benutzer", benutzer],
            ["Geräte", geraete],
            ["Übernommene Vorgänge", uebernommen],
          ].map(([label, wert]) => (
            <Card key={label as string}>
              <div className="px-4 py-3">
                <p className="text-2xl font-semibold tabular-nums text-fg">
                  {wert}
                </p>
                <p className="text-xs text-fg-muted">{label}</p>
              </div>
            </Card>
          ))}
        </div>

        <Card className="border-warning/40 bg-warning-soft">
          <div className="flex items-start gap-2.5 px-5 py-4">
            <AlertTriangle
              className="mt-0.5 size-4 shrink-0 text-warning-soft-fg"
              aria-hidden="true"
            />
            <p className="text-sm text-warning-soft-fg">
              Führen Sie jeden Import <strong>zuerst mit <code className="font-mono">--dry-run</code></strong> aus.
              Der Trockenlauf schreibt nichts, prüft aber jede Zeile und meldet,
              was schiefginge – etwa fehlende Standorte oder unbekannte
              Bereiche. Legen Sie außerdem vorher eine Sicherung der Datenbank an.
            </p>
          </div>
        </Card>

        <Card>
          <div className="px-5 py-4">
            <h2 className="flex items-center gap-2 text-sm font-semibold text-fg">
              <Terminal className="size-4 text-fg-subtle" aria-hidden="true" />
              Aufruf
            </h2>
            <pre className="mt-2 overflow-x-auto rounded-lg bg-surface-2 p-3.5 text-xs">
              <code className="font-mono">
                {`# Trockenlauf – prüft nur\nnpm run import -- --typ=standorte --datei=standorte.csv --dry-run\n\n# Übernehmen\nnpm run import -- --typ=standorte --datei=standorte.csv\n\n# Bei komma-getrennten Dateien\nnpm run import -- --typ=benutzer --datei=benutzer.csv --trennzeichen=,`}
              </code>
            </pre>
            <p className="mt-2 text-xs text-fg-muted">
              Vorgabe für das Trennzeichen ist das Semikolon – so exportiert
              Excel im deutschen Sprachraum. Spaltennamen werden unabhängig von
              Groß- und Kleinschreibung und Umlautschreibung erkannt.
            </p>
          </div>
        </Card>

        <section>
          <h2 className="mb-2 text-sm font-semibold text-fg">
            Reihenfolge und Spalten
          </h2>
          <Card className="divide-y divide-line">
            {ARTEN.map((a, i) => (
              <div key={a.typ} className="flex gap-4 px-5 py-3.5">
                <span className="flex size-6 shrink-0 items-center justify-center rounded-full bg-surface-2 text-xs font-semibold text-fg-muted">
                  {i + 1}
                </span>
                <div className="min-w-0">
                  <p className="font-medium text-fg">
                    {a.titel}{" "}
                    <code className="ml-1 rounded bg-surface-2 px-1.5 py-0.5 font-mono text-xs text-fg-muted">
                      --typ={a.typ}
                    </code>
                  </p>
                  <p className="mt-1 font-mono text-xs text-fg-subtle">
                    {a.spalten}
                  </p>
                  <p className="mt-1 text-sm text-fg-muted">{a.hinweis}</p>
                </div>
              </div>
            ))}
          </Card>
        </section>
      </div>
    </>
  );
}
