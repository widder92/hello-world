import type { Metadata } from "next";
import Link from "next/link";
import { redirect } from "next/navigation";
import { ArrowLeft, Factory, Package, Wallet } from "lucide-react";

import { PageHeader } from "@/components/layout/page-header";
import {
  GeraeteartDialog,
  HerstellerDialog,
  KostenstelleDialog,
} from "@/components/verwaltung/stammdaten-dialoge";
import { Badge } from "@/components/ui/badge";
import { Card } from "@/components/ui/card";
import { db } from "@/server/db";
import { getActor, inventoryAccess } from "@/server/policy";
import { centAlsEuro } from "@/server/services/inventar";

export const metadata: Metadata = { title: "Inventar-Stammdaten" };

/**
 * Stammdaten des Inventars: Gerätearten, Hersteller, Kostenstellen.
 *
 * Bewusst eine Seite mit drei Abschnitten statt drei Seiten – die Listen sind
 * kurz, werden selten geändert und hängen zusammen: Wer eine neue Geräteart
 * anlegt, will ihr meist gleich eine Kostenstelle mitgeben.
 *
 * Nur für die Bereichsleitung. Diese drei Listen wirken auf alle Geräte
 * gleichzeitig, und eine gelöschte Kostenstelle verfälscht jede Auswertung.
 */
export default async function StammdatenPage() {
  const actor = await getActor();
  if (!actor) redirect("/anmelden");
  if (inventoryAccess(actor) !== "manage") redirect("/inventar");

  const [arten, hersteller, kostenstellen] = await Promise.all([
    db.assetKind.findMany({
      orderBy: [{ sortOrder: "asc" }, { name: "asc" }],
      include: {
        defaultCostCenter: { select: { code: true, name: true } },
        _count: { select: { assets: true } },
      },
    }),
    db.manufacturer.findMany({
      orderBy: { name: "asc" },
      include: { _count: { select: { assets: true } } },
    }),
    db.costCenter.findMany({
      orderBy: [{ sortOrder: "asc" }, { code: "asc" }],
      include: {
        _count: { select: { assets: true } },
        assets: { select: { purchasePriceCents: true } },
      },
    }),
  ]);

  const auswahl = kostenstellen.map((k) => ({
    id: k.id,
    code: k.code,
    name: k.name,
  }));

  return (
    <>
      <PageHeader
        title="Inventar-Stammdaten"
        description="Gerätearten, Hersteller und Kostenstellen. Änderungen hier wirken auf alle Geräte."
        actions={
          <Link
            href="/inventar"
            className="inline-flex items-center gap-1.5 rounded-md px-2.5 py-1.5 text-sm text-fg-muted transition-colors hover:bg-surface-2 hover:text-fg"
          >
            <ArrowLeft className="size-4" aria-hidden="true" />
            Zurück zum Inventar
          </Link>
        }
      />

      <div className="space-y-8 px-4 py-5 sm:px-6">
        {/* --- Gerätearten --------------------------------------------- */}
        <section aria-labelledby="arten-titel">
          <div className="mb-3 flex flex-wrap items-center justify-between gap-3">
            <div>
              <h2
                id="arten-titel"
                className="flex items-center gap-2 text-base font-semibold text-fg"
              >
                <Package className="size-4 text-fg-muted" aria-hidden="true" />
                Gerätearten
              </h2>
              <p className="mt-0.5 text-sm text-fg-muted">
                Bestimmen, welche Angaben verlangt werden und womit die
                Inventarnummer beginnt.
              </p>
            </div>
            <GeraeteartDialog kostenstellen={auswahl} />
          </div>

          <Card className="divide-y divide-line">
            {arten.map((a) => (
              <div
                key={a.id}
                className={`flex flex-wrap items-center gap-3 px-5 py-3 ${a.isActive ? "" : "opacity-55"}`}
              >
                <span className="w-14 shrink-0 font-mono text-xs text-primary">
                  {a.key}
                </span>
                <div className="min-w-0 flex-1">
                  <div className="flex flex-wrap items-center gap-2">
                    <span className="font-medium text-fg">{a.name}</span>
                    {!a.isActive && (
                      <Badge tone="neutral" size="sm">
                        Nicht aktiv
                      </Badge>
                    )}
                  </div>
                  <p className="mt-0.5 flex flex-wrap gap-x-3 text-xs text-fg-muted">
                    <span>{a._count.assets} Geräte</span>
                    {a.requiresSerial && <span>Seriennummer verlangt</span>}
                    {a.requiresMac && <span>fragt nach MAC</span>}
                    {a.defaultCostCenter && (
                      <span>
                        Kostenstelle {a.defaultCostCenter.code} vorbelegt
                      </span>
                    )}
                  </p>
                </div>
                <GeraeteartDialog
                  art={{
                    id: a.id,
                    key: a.key,
                    name: a.name,
                    icon: a.icon,
                    requiresSerial: a.requiresSerial,
                    requiresMac: a.requiresMac,
                    defaultCostCenterId: a.defaultCostCenterId,
                    isActive: a.isActive,
                  }}
                  kostenstellen={auswahl}
                />
              </div>
            ))}
          </Card>
        </section>

        {/* --- Hersteller ---------------------------------------------- */}
        <section aria-labelledby="hersteller-titel">
          <div className="mb-3 flex flex-wrap items-center justify-between gap-3">
            <div>
              <h2
                id="hersteller-titel"
                className="flex items-center gap-2 text-base font-semibold text-fg"
              >
                <Factory className="size-4 text-fg-muted" aria-hidden="true" />
                Hersteller
              </h2>
              <p className="mt-0.5 text-sm text-fg-muted">
                Mit Support-Telefonnummer, damit sie im Störungsfall niemand
                suchen muss.
              </p>
            </div>
            <HerstellerDialog />
          </div>

          <Card className="divide-y divide-line">
            {hersteller.length === 0 && (
              <p className="px-5 py-8 text-center text-sm text-fg-muted">
                Noch keine Hersteller angelegt.
              </p>
            )}
            {hersteller.map((h) => (
              <div
                key={h.id}
                className={`flex flex-wrap items-center gap-3 px-5 py-3 ${h.isActive ? "" : "opacity-55"}`}
              >
                <div className="min-w-0 flex-1">
                  <div className="flex flex-wrap items-center gap-2">
                    <span className="font-medium text-fg">{h.name}</span>
                    {!h.isActive && (
                      <Badge tone="neutral" size="sm">
                        Nicht aktiv
                      </Badge>
                    )}
                  </div>
                  <p className="mt-0.5 flex flex-wrap gap-x-3 text-xs text-fg-muted">
                    <span>{h._count.assets} Geräte</span>
                    {h.supportPhone && <span>Support {h.supportPhone}</span>}
                    {h.website && <span>{h.website.replace(/^https?:\/\//, "")}</span>}
                  </p>
                </div>
                <HerstellerDialog
                  hersteller={{
                    id: h.id,
                    name: h.name,
                    website: h.website,
                    supportPhone: h.supportPhone,
                    notes: h.notes,
                    isActive: h.isActive,
                  }}
                />
              </div>
            ))}
          </Card>
        </section>

        {/* --- Kostenstellen ------------------------------------------- */}
        <section aria-labelledby="kst-titel">
          <div className="mb-3 flex flex-wrap items-center justify-between gap-3">
            <div>
              <h2
                id="kst-titel"
                className="flex items-center gap-2 text-base font-semibold text-fg"
              >
                <Wallet className="size-4 text-fg-muted" aria-hidden="true" />
                Kostenstellen
              </h2>
              <p className="mt-0.5 text-sm text-fg-muted">
                Die Achse der Auswertung. Bitte genau so benennen wie in der
                Buchhaltung.
              </p>
            </div>
            <KostenstelleDialog />
          </div>

          <Card className="divide-y divide-line">
            {kostenstellen.length === 0 && (
              <p className="px-5 py-8 text-center text-sm text-fg-muted">
                Noch keine Kostenstellen angelegt. Ohne sie bleibt die
                Auswertung leer.
              </p>
            )}
            {kostenstellen.map((k) => {
              const summe = k.assets.reduce(
                (s, a) => s + (a.purchasePriceCents ?? 0),
                0,
              );
              return (
                <div
                  key={k.id}
                  className={`flex flex-wrap items-center gap-3 px-5 py-3 ${k.isActive ? "" : "opacity-55"}`}
                >
                  <span className="w-16 shrink-0 font-mono text-xs text-primary">
                    {k.code}
                  </span>
                  <div className="min-w-0 flex-1">
                    <div className="flex flex-wrap items-center gap-2">
                      <span className="font-medium text-fg">{k.name}</span>
                      {!k.isActive && (
                        <Badge tone="neutral" size="sm">
                          Nicht aktiv
                        </Badge>
                      )}
                    </div>
                    <p className="mt-0.5 flex flex-wrap gap-x-3 text-xs text-fg-muted">
                      <span>{k._count.assets} Geräte</span>
                      <span>{centAlsEuro(summe)} Anschaffungswert</span>
                      {k.description && <span>{k.description}</span>}
                    </p>
                  </div>
                  <KostenstelleDialog
                    kostenstelle={{
                      id: k.id,
                      code: k.code,
                      name: k.name,
                      description: k.description,
                      isActive: k.isActive,
                    }}
                  />
                </div>
              );
            })}
          </Card>
        </section>
      </div>
    </>
  );
}
