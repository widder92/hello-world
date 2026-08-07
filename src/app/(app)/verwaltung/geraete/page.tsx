import type { Metadata } from "next";
import { Laptop, Search } from "lucide-react";

import { PageHeader } from "@/components/layout/page-header";
import { Avatar } from "@/components/ui/avatar";
import { Badge } from "@/components/ui/badge";
import { Card } from "@/components/ui/card";
import { Input, Select } from "@/components/ui/input";
import { db } from "@/server/db";
import { requireAdmin } from "@/server/policy";
import { ASSET_TYPE } from "@/lib/domain";
import { formatDate } from "@/lib/format";

export const metadata: Metadata = { title: "Geräte" };

/**
 * Geräteinventar.
 *
 * Bewusst schlank gehalten: Es geht nicht um eine vollständige
 * Anlagenbuchhaltung, sondern darum, bei einem Ticket zu wissen, um welches
 * Gerät es geht, wo es steht und wer es nutzt.
 */
export default async function GeraetePage({
  searchParams,
}: {
  searchParams: Promise<{ q?: string; suche?: string; standort?: string }>;
}) {
  await requireAdmin();

  const sp = await searchParams;
  // Aus der Ticket-Seitenleiste kommt der Parameter als "suche".
  const suche = (sp.q ?? sp.suche ?? "").trim();

  const [geraete, standorte] = await Promise.all([
    db.asset.findMany({
      where: {
        ...(suche
          ? {
              OR: [
                { assetTag: { contains: suche, mode: "insensitive" } },
                { name: { contains: suche, mode: "insensitive" } },
                { serial: { contains: suche, mode: "insensitive" } },
                { model: { contains: suche, mode: "insensitive" } },
              ],
            }
          : {}),
        ...(sp.standort ? { siteId: sp.standort } : {}),
      },
      orderBy: [{ isActive: "desc" }, { assetTag: "asc" }],
      take: 200,
      include: {
        site: { select: { name: true, shortName: true } },
        assignedTo: { select: { name: true } },
        _count: { select: { tickets: true } },
      },
    }),
    db.site.findMany({
      where: { isActive: true },
      orderBy: [{ sortOrder: "asc" }, { name: "asc" }],
      select: { id: true, name: true },
    }),
  ]);

  return (
    <>
      <PageHeader
        title="Geräte"
        description="Verknüpft mit Vorgängen, damit beim Ticket sofort klar ist, um welches Gerät es geht und wo es steht."
      >
        <form className="flex flex-wrap items-center gap-2">
          <div className="relative min-w-56 flex-1">
            <label htmlFor="geraet-suche" className="sr-only">
              Geräte suchen
            </label>
            <Search
              className="pointer-events-none absolute left-2.5 top-1/2 size-4 -translate-y-1/2 text-fg-subtle"
              aria-hidden="true"
            />
            <Input
              id="geraet-suche"
              name="q"
              defaultValue={suche}
              placeholder="Nummer, Name, Modell oder Seriennummer"
              className="h-9 pl-8.5"
            />
          </div>
          <Select
            name="standort"
            defaultValue={sp.standort ?? ""}
            className="h-9 w-auto min-w-44"
            aria-label="Standort"
          >
            <option value="">Alle Standorte</option>
            {standorte.map((s) => (
              <option key={s.id} value={s.id}>
                {s.name}
              </option>
            ))}
          </Select>
        </form>
      </PageHeader>

      <div className="px-4 py-5 sm:px-6">
        <Card className="divide-y divide-line">
          {geraete.length === 0 && (
            <div className="px-6 py-12 text-center">
              <Laptop className="mx-auto size-8 text-fg-subtle" aria-hidden="true" />
              <p className="mt-2 text-sm text-fg-muted">
                {suche ? "Keine Treffer." : "Noch keine Geräte erfasst."}
              </p>
            </div>
          )}

          {geraete.map((g) => (
            <div
              key={g.id}
              className={`flex flex-wrap items-center gap-4 px-5 py-3 ${g.isActive ? "" : "opacity-55"}`}
            >
              <span className="w-24 shrink-0 font-mono text-xs text-primary">
                {g.assetTag}
              </span>

              <div className="min-w-0 flex-1">
                <div className="flex flex-wrap items-center gap-2">
                  <span className="font-medium text-fg">{g.name}</span>
                  <Badge tone="outline" size="sm">
                    {ASSET_TYPE[g.type]}
                  </Badge>
                  {!g.isActive && (
                    <Badge tone="neutral" size="sm">
                      Ausgemustert
                    </Badge>
                  )}
                </div>
                <p className="mt-0.5 flex flex-wrap items-center gap-x-3 text-xs text-fg-muted">
                  {(g.manufacturer || g.model) && (
                    <span>{[g.manufacturer, g.model].filter(Boolean).join(" ")}</span>
                  )}
                  {g.serial && <span className="font-mono">{g.serial}</span>}
                  {g.site && (
                    <span>
                      {g.site.shortName ?? g.site.name}
                      {g.room ? `, ${g.room}` : ""}
                    </span>
                  )}
                  {g.warrantyUntil && (
                    <span>Garantie bis {formatDate(g.warrantyUntil)}</span>
                  )}
                  <span>{g._count.tickets} Vorgänge</span>
                </p>
              </div>

              {g.assignedTo && (
                <span className="flex shrink-0 items-center gap-1.5 text-xs text-fg-muted">
                  <Avatar name={g.assignedTo.name} size="xs" />
                  {g.assignedTo.name}
                </span>
              )}
            </div>
          ))}
        </Card>

        <p className="mt-3 text-xs text-fg-subtle">
          Geräte werden über die Datenübernahme eingelesen. Ein Formular zum
          einzelnen Anlegen ist noch nicht gebaut.
        </p>
      </div>
    </>
  );
}
