import type { Metadata } from "next";
import Link from "next/link";
import { redirect } from "next/navigation";
import { AssetStatus, Prisma } from "@prisma/client";
import { BarChart3, Laptop, Package, Plus, Search, Settings2 } from "lucide-react";

import { PageHeader } from "@/components/layout/page-header";
import { Avatar } from "@/components/ui/avatar";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import { Card } from "@/components/ui/card";
import { Input, Select } from "@/components/ui/input";
import { db } from "@/server/db";
import { getActor, inventoryAccess, inventoryTeamIds } from "@/server/policy";
import { centAlsEuro, macNormalisierenWeich } from "@/server/services/inventar";
import { ASSET_STATUS, ASSET_STATUS_TON } from "@/lib/domain";
import { formatDate } from "@/lib/format";

export const metadata: Metadata = { title: "Inventar" };

const SEITENGROESSE = 50;

/**
 * Das Inventar.
 *
 * Die Suche geht bewusst auch über Seriennummer, MAC-Adresse und IMEI: Am
 * Telefon nennt niemand die Inventarnummer, sondern liest ab, was auf dem
 * Aufkleber steht – oder das Netzwerk meldet nur eine MAC-Adresse.
 */
export default async function InventarPage({
  searchParams,
}: {
  searchParams: Promise<{
    q?: string;
    status?: string;
    art?: string;
    standort?: string;
    kostenstelle?: string;
    person?: string;
    seite?: string;
  }>;
}) {
  const actor = await getActor();
  if (!actor) redirect("/anmelden");

  const stufe = inventoryAccess(actor);
  if (stufe === "none") redirect("/meine-geraete");

  const sp = await searchParams;
  const suche = (sp.q ?? "").trim();
  const seite = Math.max(1, Number(sp.seite) || 1);

  // Nur Geräte der eigenen Inventarbereiche.
  const bereiche = actor.isAdmin
    ? (
        await db.team.findMany({
          where: { hasInventory: true },
          select: { id: true },
        })
      ).map((t) => t.id)
    : inventoryTeamIds(actor);

  // Eine getippte MAC-Adresse wird vor der Suche vereinheitlicht – sonst
  // fände "3C-52-82-11-0A-01" den Datensatz "3c:52:82:11:0a:01" nie.
  const alsMac = suche ? macNormalisierenWeich(suche) : null;

  const suchFilter: Prisma.AssetWhereInput = suche
    ? {
        OR: [
          { assetTag: { contains: suche, mode: "insensitive" } },
          { name: { contains: suche, mode: "insensitive" } },
          { serial: { contains: suche, mode: "insensitive" } },
          { model: { contains: suche, mode: "insensitive" } },
          { imei: { contains: suche, mode: "insensitive" } },
          { assignedTo: { name: { contains: suche, mode: "insensitive" } } },
          {
            macs: {
              some: { address: { contains: alsMac ?? suche, mode: "insensitive" } },
            },
          },
        ],
      }
    : {};

  const where: Prisma.AssetWhereInput = {
    teamId: { in: bereiche },
    ...suchFilter,
    ...(sp.status && sp.status in AssetStatus
      ? { status: sp.status as AssetStatus }
      : {}),
    ...(sp.art ? { kindId: sp.art } : {}),
    ...(sp.standort ? { siteId: sp.standort } : {}),
    ...(sp.kostenstelle ? { costCenterId: sp.kostenstelle } : {}),
    ...(sp.person ? { assignedToId: sp.person } : {}),
  };

  const [geraete, anzahl, arten, standorte, kostenstellen, nachStatus] =
    await Promise.all([
      db.asset.findMany({
        where,
        orderBy: [{ status: "asc" }, { assetTag: "asc" }],
        skip: (seite - 1) * SEITENGROESSE,
        take: SEITENGROESSE,
        include: {
          kind: { select: { name: true } },
          manufacturer: { select: { name: true } },
          site: { select: { name: true, shortName: true } },
          costCenter: { select: { code: true } },
          assignedTo: { select: { id: true, name: true } },
          photo: { select: { id: true } },
          _count: { select: { tickets: true } },
        },
      }),
      db.asset.count({ where }),
      db.assetKind.findMany({
        where: { isActive: true },
        orderBy: [{ sortOrder: "asc" }, { name: "asc" }],
        select: { id: true, name: true },
      }),
      db.site.findMany({
        where: { isActive: true },
        orderBy: [{ sortOrder: "asc" }, { name: "asc" }],
        select: { id: true, name: true },
      }),
      db.costCenter.findMany({
        where: { isActive: true },
        orderBy: [{ sortOrder: "asc" }, { code: "asc" }],
        select: { id: true, code: true, name: true },
      }),
      db.asset.groupBy({
        by: ["status"],
        where: { teamId: { in: bereiche } },
        _count: { _all: true },
      }),
    ]);

  const statusZahl = new Map(nachStatus.map((r) => [r.status, r._count._all]));
  const seiten = Math.max(1, Math.ceil(anzahl / SEITENGROESSE));

  function seitenLink(nr: number) {
    const p = new URLSearchParams();
    for (const [k, v] of Object.entries(sp)) if (v && k !== "seite") p.set(k, v);
    if (nr > 1) p.set("seite", String(nr));
    const q = p.toString();
    return q ? `/inventar?${q}` : "/inventar";
  }

  return (
    <>
      <PageHeader
        title="Inventar"
        description="Alle Geräte des Bereichs – wo sie stehen, wer sie hat und was sie gekostet haben."
        actions={
          <div className="flex flex-wrap items-center gap-2">
            <Button asChild variant="secondary" size="sm">
              <Link href="/inventar/auswertung">
                <BarChart3 aria-hidden="true" />
                Auswertung
              </Link>
            </Button>
            {stufe === "manage" && (
              <Button asChild variant="secondary" size="sm">
                <Link href="/inventar/stammdaten">
                  <Settings2 aria-hidden="true" />
                  Stammdaten
                </Link>
              </Button>
            )}
            <Button asChild size="sm">
              <Link href="/inventar/neu">
                <Plus aria-hidden="true" />
                Gerät anlegen
              </Link>
            </Button>
          </div>
        }
      >
        <form className="flex flex-wrap items-end gap-2">
          <div className="relative min-w-64 flex-1">
            <label htmlFor="inv-suche" className="sr-only">
              Geräte suchen
            </label>
            <Search
              className="pointer-events-none absolute left-2.5 top-1/2 size-4 -translate-y-1/2 text-fg-subtle"
              aria-hidden="true"
            />
            <Input
              id="inv-suche"
              name="q"
              defaultValue={suche}
              placeholder="Nummer, Name, Seriennummer, MAC, IMEI oder Person"
              className="h-9 pl-8.5"
            />
          </div>

          <Select
            name="status"
            defaultValue={sp.status ?? ""}
            className="h-9 w-auto min-w-40"
            aria-label="Status"
          >
            <option value="">Alle Status</option>
            {Object.entries(ASSET_STATUS).map(([wert, label]) => (
              <option key={wert} value={wert}>
                {label} ({statusZahl.get(wert as AssetStatus) ?? 0})
              </option>
            ))}
          </Select>

          <Select
            name="art"
            defaultValue={sp.art ?? ""}
            className="h-9 w-auto min-w-36"
            aria-label="Geräteart"
          >
            <option value="">Alle Arten</option>
            {arten.map((a) => (
              <option key={a.id} value={a.id}>
                {a.name}
              </option>
            ))}
          </Select>

          <Select
            name="standort"
            defaultValue={sp.standort ?? ""}
            className="h-9 w-auto min-w-40"
            aria-label="Standort"
          >
            <option value="">Alle Standorte</option>
            {standorte.map((s) => (
              <option key={s.id} value={s.id}>
                {s.name}
              </option>
            ))}
          </Select>

          <Select
            name="kostenstelle"
            defaultValue={sp.kostenstelle ?? ""}
            className="h-9 w-auto min-w-40"
            aria-label="Kostenstelle"
          >
            <option value="">Alle Kostenstellen</option>
            {kostenstellen.map((k) => (
              <option key={k.id} value={k.id}>
                {k.code} – {k.name}
              </option>
            ))}
          </Select>

          <Button type="submit" variant="secondary" size="sm" className="h-9">
            Filtern
          </Button>
        </form>
      </PageHeader>

      <div className="px-4 py-5 sm:px-6">
        <p className="mb-3 text-sm text-fg-muted">
          {anzahl === 0
            ? "Keine Geräte gefunden."
            : `${anzahl} ${anzahl === 1 ? "Gerät" : "Geräte"}`}
          {seiten > 1 && ` – Seite ${seite} von ${seiten}`}
        </p>

        <Card className="divide-y divide-line">
          {geraete.length === 0 && (
            <div className="px-6 py-12 text-center">
              <Package
                className="mx-auto size-8 text-fg-subtle"
                aria-hidden="true"
              />
              <p className="mt-2 text-sm text-fg-muted">
                {suche
                  ? "Keine Treffer. Vielleicht hilft ein Teil der Seriennummer."
                  : "Noch keine Geräte erfasst."}
              </p>
            </div>
          )}

          {geraete.map((g) => (
            <Link
              key={g.id}
              href={`/inventar/${g.id}`}
              className="flex flex-wrap items-center gap-4 px-5 py-3 transition-colors hover:bg-surface-2"
            >
              {/* Miniaturbild – macht die Liste beim Suchen im Lager schneller
                  lesbar als jede Textzeile. */}
              <span className="flex size-11 shrink-0 items-center justify-center overflow-hidden rounded-lg border border-line bg-surface-2">
                {g.photo ? (
                  // eslint-disable-next-line @next/next/no-img-element
                  <img
                    src={`/api/uploads/${g.photo.id}`}
                    alt=""
                    className="size-full object-cover"
                  />
                ) : (
                  <Laptop className="size-5 text-fg-subtle" aria-hidden="true" />
                )}
              </span>

              <span className="w-24 shrink-0 font-mono text-xs text-primary">
                {g.assetTag}
              </span>

              <div className="min-w-0 flex-1">
                <div className="flex flex-wrap items-center gap-2">
                  <span className="font-medium text-fg">{g.name}</span>
                  <Badge tone={ASSET_STATUS_TON[g.status]} size="sm">
                    {ASSET_STATUS[g.status]}
                  </Badge>
                  <Badge tone="outline" size="sm">
                    {g.kind.name}
                  </Badge>
                </div>
                <p className="mt-0.5 flex flex-wrap items-center gap-x-3 text-xs text-fg-muted">
                  {(g.manufacturer || g.model) && (
                    <span>
                      {[g.manufacturer?.name, g.model].filter(Boolean).join(" ")}
                    </span>
                  )}
                  {g.serial && <span className="font-mono">{g.serial}</span>}
                  {g.site && (
                    <span>
                      {g.site.shortName ?? g.site.name}
                      {g.room ? `, ${g.room}` : ""}
                    </span>
                  )}
                  {g.status === "LAGER" && g.storageLocation && (
                    <span>{g.storageLocation}</span>
                  )}
                  {g.costCenter && <span>KSt. {g.costCenter.code}</span>}
                  {g.purchasePriceCents !== null && (
                    <span>{centAlsEuro(g.purchasePriceCents)}</span>
                  )}
                  {g._count.tickets > 0 && <span>{g._count.tickets} Vorgänge</span>}
                </p>
              </div>

              {g.assignedTo && (
                <span className="flex shrink-0 items-center gap-1.5 text-xs text-fg-muted">
                  <Avatar name={g.assignedTo.name} size="xs" />
                  <span>
                    {g.assignedTo.name}
                    {g.assignedAt && (
                      <span className="block text-fg-subtle">
                        seit {formatDate(g.assignedAt)}
                      </span>
                    )}
                  </span>
                </span>
              )}
            </Link>
          ))}
        </Card>

        {seiten > 1 && (
          <nav
            aria-label="Blättern"
            className="mt-4 flex items-center justify-center gap-2"
          >
            {seite > 1 && (
              <Button asChild variant="secondary" size="sm">
                <Link href={seitenLink(seite - 1)}>Zurück</Link>
              </Button>
            )}
            {seite < seiten && (
              <Button asChild variant="secondary" size="sm">
                <Link href={seitenLink(seite + 1)}>Weiter</Link>
              </Button>
            )}
          </nav>
        )}
      </div>
    </>
  );
}
