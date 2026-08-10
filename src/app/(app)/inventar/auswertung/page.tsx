import type { Metadata } from "next";
import Link from "next/link";
import { redirect } from "next/navigation";
import { AssetStatus } from "@prisma/client";
import { ArrowLeft, Download } from "lucide-react";

import { PageHeader } from "@/components/layout/page-header";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import { Card, CardContent } from "@/components/ui/card";
import { db } from "@/server/db";
import { getActor, inventoryAccess, inventoryTeamIds } from "@/server/policy";
import { centAlsEuro } from "@/server/services/inventar";
import { ASSET_STATUS, ASSET_STATUS_TON } from "@/lib/domain";
import { formatDate } from "@/lib/format";

export const metadata: Metadata = { title: "Auswertung des Inventars" };

/**
 * Auswertung.
 *
 * Beantwortet die drei Fragen, die tatsächlich gestellt werden: Was steht auf
 * welcher Kostenstelle? Wovon haben wir wie viel? Und was läuft demnächst aus
 * der Garantie oder ist nirgends zugeordnet?
 *
 * Die letzte Zeile – Geräte ohne Kostenstelle – ist die wichtigste. Sie zeigt,
 * wie belastbar alles darüber ist.
 */
export default async function AuswertungPage() {
  const actor = await getActor();
  if (!actor) redirect("/anmelden");
  if (inventoryAccess(actor) === "none") redirect("/meine-geraete");

  const bereiche = actor.isAdmin
    ? (
        await db.team.findMany({
          where: { hasInventory: true },
          select: { id: true },
        })
      ).map((t) => t.id)
    : inventoryTeamIds(actor);

  const jetzt = new Date();
  const inSechsMonaten = new Date(jetzt.getTime() + 183 * 24 * 60 * 60 * 1000);

  // Ein einziger Durchlauf über die Geräte statt vieler Gruppierungsabfragen:
  // Bei einigen tausend Geräten ist das schneller als ein halbes Dutzend
  // Abfragen, und die Zahlen können nicht auseinanderlaufen.
  const geraete = await db.asset.findMany({
    where: { teamId: { in: bereiche } },
    select: {
      id: true,
      assetTag: true,
      name: true,
      status: true,
      purchasePriceCents: true,
      warrantyUntil: true,
      kind: { select: { id: true, name: true } },
      costCenter: { select: { id: true, code: true, name: true } },
    },
    orderBy: { assetTag: "asc" },
  });

  interface Gruppe {
    schluessel: string;
    bezeichnung: string;
    anzahl: number;
    wert: number;
    imEinsatz: number;
    imLager: number;
    ausgesondert: number;
  }

  function leer(schluessel: string, bezeichnung: string): Gruppe {
    return {
      schluessel,
      bezeichnung,
      anzahl: 0,
      wert: 0,
      imEinsatz: 0,
      imLager: 0,
      ausgesondert: 0,
    };
  }

  const nachKostenstelle = new Map<string, Gruppe>();
  const nachArt = new Map<string, Gruppe>();
  let gesamtWert = 0;

  for (const g of geraete) {
    const kstSchluessel = g.costCenter?.id ?? "ohne";
    const kstName = g.costCenter
      ? `${g.costCenter.code} – ${g.costCenter.name}`
      : "Ohne Kostenstelle";

    for (const [karte, schluessel, bezeichnung] of [
      [nachKostenstelle, kstSchluessel, kstName],
      [nachArt, g.kind.id, g.kind.name],
    ] as const) {
      const gruppe = karte.get(schluessel) ?? leer(schluessel, bezeichnung);
      gruppe.anzahl++;
      // Ausgesonderte Geräte zählen nicht in den Anschaffungswert: Sonst
      // wüchse die Summe Jahr für Jahr, obwohl die Geräte längst entsorgt sind.
      if (g.status !== AssetStatus.AUSGESONDERT) {
        gruppe.wert += g.purchasePriceCents ?? 0;
      }
      if (g.status === AssetStatus.HERAUSGEGEBEN) gruppe.imEinsatz++;
      else if (g.status === AssetStatus.LAGER) gruppe.imLager++;
      else if (g.status === AssetStatus.AUSGESONDERT) gruppe.ausgesondert++;
      karte.set(schluessel, gruppe);
    }

    if (g.status !== AssetStatus.AUSGESONDERT) {
      gesamtWert += g.purchasePriceCents ?? 0;
    }
  }

  const kostenstellen = [...nachKostenstelle.values()].sort((a, b) =>
    a.schluessel === "ohne" ? 1 : b.schluessel === "ohne" ? -1 : b.wert - a.wert,
  );
  const arten = [...nachArt.values()].sort((a, b) => b.anzahl - a.anzahl);

  const ohnePreis = geraete.filter(
    (g) => g.purchasePriceCents === null && g.status !== AssetStatus.AUSGESONDERT,
  ).length;
  const ohneKostenstelle = nachKostenstelle.get("ohne")?.anzahl ?? 0;

  const garantieLaeuftAus = geraete
    .filter(
      (g) =>
        g.warrantyUntil !== null &&
        g.warrantyUntil > jetzt &&
        g.warrantyUntil <= inSechsMonaten &&
        g.status !== AssetStatus.AUSGESONDERT,
    )
    .sort((a, b) => a.warrantyUntil!.getTime() - b.warrantyUntil!.getTime());

  const aktiv = geraete.filter(
    (g) => g.status !== AssetStatus.AUSGESONDERT,
  ).length;

  return (
    <>
      <PageHeader
        title="Auswertung des Inventars"
        description="Anschaffungswerte ohne ausgesonderte Geräte – sonst wüchse die Summe Jahr für Jahr weiter."
        actions={
          <div className="flex flex-wrap items-center gap-2">
            <Link
              href="/inventar"
              className="inline-flex items-center gap-1.5 rounded-md px-2.5 py-1.5 text-sm text-fg-muted transition-colors hover:bg-surface-2 hover:text-fg"
            >
              <ArrowLeft className="size-4" aria-hidden="true" />
              Inventar
            </Link>
            <Button asChild variant="secondary" size="sm">
              <a href="/inventar/auswertung/export" download>
                <Download aria-hidden="true" />
                Als CSV
              </a>
            </Button>
          </div>
        }
      />

      <div className="space-y-6 px-4 py-5 sm:px-6">
        {/* --- Kennzahlen --------------------------------------------- */}
        <div className="grid gap-3 sm:grid-cols-2 lg:grid-cols-4">
          <Kennzahl titel="Geräte im Bestand" wert={String(aktiv)} />
          <Kennzahl titel="Anschaffungswert" wert={centAlsEuro(gesamtWert)} />
          <Kennzahl
            titel="Ohne Kostenstelle"
            wert={String(ohneKostenstelle)}
            hinweis={
              ohneKostenstelle > 0
                ? "Diese Geräte fehlen in jeder Aufteilung unten."
                : "Alles zugeordnet."
            }
            warnen={ohneKostenstelle > 0}
          />
          <Kennzahl
            titel="Ohne Preis"
            wert={String(ohnePreis)}
            hinweis={
              ohnePreis > 0
                ? "Der Anschaffungswert ist entsprechend zu niedrig."
                : "Alle Preise erfasst."
            }
            warnen={ohnePreis > 0}
          />
        </div>

        {/* --- Kostenstellen ------------------------------------------ */}
        <section aria-labelledby="kst-auswertung">
          <h2 id="kst-auswertung" className="mb-2 text-base font-semibold text-fg">
            Nach Kostenstelle
          </h2>
          <Card>
            <div className="overflow-x-auto">
              <table className="w-full text-sm">
                <thead>
                  <tr className="border-b border-line text-left text-xs uppercase tracking-wide text-fg-subtle">
                    <th scope="col" className="px-5 py-2.5 font-medium">
                      Kostenstelle
                    </th>
                    <th scope="col" className="px-3 py-2.5 text-right font-medium">
                      Geräte
                    </th>
                    <th scope="col" className="px-3 py-2.5 text-right font-medium">
                      Im Einsatz
                    </th>
                    <th scope="col" className="px-3 py-2.5 text-right font-medium">
                      Im Lager
                    </th>
                    <th scope="col" className="px-5 py-2.5 text-right font-medium">
                      Anschaffungswert
                    </th>
                  </tr>
                </thead>
                <tbody className="divide-y divide-line">
                  {kostenstellen.map((k) => (
                    <tr key={k.schluessel}>
                      <th
                        scope="row"
                        className={`px-5 py-2.5 text-left font-medium ${
                          k.schluessel === "ohne" ? "text-warning" : "text-fg"
                        }`}
                      >
                        {k.bezeichnung}
                      </th>
                      <td className="px-3 py-2.5 text-right tabular-nums">
                        {k.anzahl}
                      </td>
                      <td className="px-3 py-2.5 text-right tabular-nums text-fg-muted">
                        {k.imEinsatz}
                      </td>
                      <td className="px-3 py-2.5 text-right tabular-nums text-fg-muted">
                        {k.imLager}
                      </td>
                      <td className="px-5 py-2.5 text-right tabular-nums">
                        {centAlsEuro(k.wert)}
                      </td>
                    </tr>
                  ))}
                </tbody>
                <tfoot>
                  <tr className="border-t-2 border-line font-medium">
                    <th scope="row" className="px-5 py-2.5 text-left">
                      Gesamt
                    </th>
                    <td className="px-3 py-2.5 text-right tabular-nums">{aktiv}</td>
                    <td className="px-3 py-2.5" />
                    <td className="px-3 py-2.5" />
                    <td className="px-5 py-2.5 text-right tabular-nums">
                      {centAlsEuro(gesamtWert)}
                    </td>
                  </tr>
                </tfoot>
              </table>
            </div>
          </Card>
        </section>

        {/* --- Gerätearten -------------------------------------------- */}
        <section aria-labelledby="art-auswertung">
          <h2 id="art-auswertung" className="mb-2 text-base font-semibold text-fg">
            Nach Geräteart
          </h2>
          <Card className="divide-y divide-line">
            {arten.map((a) => (
              <div
                key={a.schluessel}
                className="flex flex-wrap items-center gap-3 px-5 py-2.5"
              >
                <span className="min-w-40 flex-1 font-medium text-fg">
                  {a.bezeichnung}
                </span>
                <span className="text-sm tabular-nums text-fg-muted">
                  {a.imEinsatz} im Einsatz · {a.imLager} im Lager
                  {a.ausgesondert > 0 && ` · ${a.ausgesondert} ausgesondert`}
                </span>
                <span className="w-20 text-right text-sm tabular-nums text-fg">
                  {a.anzahl}
                </span>
                <span className="w-28 text-right text-sm tabular-nums text-fg-muted">
                  {centAlsEuro(a.wert)}
                </span>
              </div>
            ))}
          </Card>
        </section>

        {/* --- Garantie ----------------------------------------------- */}
        <section aria-labelledby="garantie">
          <h2 id="garantie" className="mb-2 text-base font-semibold text-fg">
            Garantie läuft in den nächsten sechs Monaten aus
          </h2>
          <Card className="divide-y divide-line">
            {garantieLaeuftAus.length === 0 && (
              <p className="px-5 py-8 text-center text-sm text-fg-muted">
                Kein Gerät, dessen Garantie demnächst endet.
              </p>
            )}
            {garantieLaeuftAus.map((g) => (
              <Link
                key={g.id}
                href={`/inventar/${g.id}`}
                className="flex flex-wrap items-center gap-3 px-5 py-2.5 transition-colors hover:bg-surface-2"
              >
                <span className="w-24 shrink-0 font-mono text-xs text-primary">
                  {g.assetTag}
                </span>
                <span className="min-w-40 flex-1 text-sm text-fg">{g.name}</span>
                <Badge tone={ASSET_STATUS_TON[g.status]} size="sm">
                  {ASSET_STATUS[g.status]}
                </Badge>
                <span className="text-sm tabular-nums text-fg-muted">
                  bis {formatDate(g.warrantyUntil!)}
                </span>
              </Link>
            ))}
          </Card>
        </section>
      </div>
    </>
  );
}

function Kennzahl({
  titel,
  wert,
  hinweis,
  warnen,
}: {
  titel: string;
  wert: string;
  hinweis?: string;
  warnen?: boolean;
}) {
  return (
    <Card>
      <CardContent className="py-4">
        <p className="text-xs uppercase tracking-wide text-fg-subtle">{titel}</p>
        <p
          className={`mt-1 text-2xl font-semibold tabular-nums ${
            warnen ? "text-warning" : "text-fg"
          }`}
        >
          {wert}
        </p>
        {hinweis && <p className="mt-0.5 text-xs text-fg-muted">{hinweis}</p>}
      </CardContent>
    </Card>
  );
}
