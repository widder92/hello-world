import type { Metadata } from "next";
import { TeamMode } from "@prisma/client";
import { Clock, Info } from "lucide-react";

import { teamIcon } from "@/components/layout/icon";
import { PageHeader } from "@/components/layout/page-header";
import { SlaDialog } from "@/components/verwaltung/sla-dialog";
import { PriorityBadge } from "@/components/ticket/badges";
import { Badge } from "@/components/ui/badge";
import { Card } from "@/components/ui/card";
import { db } from "@/server/db";
import { requireAdmin } from "@/server/policy";
import { formatDuration, formatPercent } from "@/lib/format";

export const metadata: Metadata = { title: "Fristen" };

/**
 * Fristen je Bereich und Priorität.
 *
 * Nur Bereiche im vollen Arbeitsplatz haben Fristen. Für die schlanken
 * Bereiche werden bewusst keine gesetzt: Eine Ampel, hinter der keine
 * Vereinbarung steht, erzeugt nur schlechtes Gewissen ohne Nutzen.
 */
export default async function SlaPage() {
  await requireAdmin();

  const [bereiche, dienstzeiten] = await Promise.all([
    db.team.findMany({
      where: { isActive: true },
      orderBy: [{ sortOrder: "asc" }, { name: "asc" }],
      select: {
        id: true,
        name: true,
        color: true,
        icon: true,
        mode: true,
        slaPolicies: {
          orderBy: { priority: "desc" },
          include: { businessHours: { select: { id: true, name: true } } },
        },
      },
    }),
    db.businessHours.findMany({
      orderBy: { name: "asc" },
      select: { id: true, name: true, isDefault: true },
    }),
  ]);

  const vollBereiche = bereiche.filter((b) => b.mode === TeamMode.VOLL);
  const einfachBereiche = bereiche.filter((b) => b.mode === TeamMode.EINFACH);

  return (
    <>
      <PageHeader
        title="Fristen"
        description="Fristen laufen nur während der Dienstzeit. Ein Vorgang, der freitags um 15:50 gemeldet wird, ist bei vier Stunden Frist nicht um 19:50 überfällig, sondern am Montagvormittag."
      />

      <div className="space-y-5 px-4 py-5 sm:px-6">
        {vollBereiche.map((b) => {
          const Icon = teamIcon(b.icon);
          return (
            <section key={b.id}>
              <div className="mb-2 flex flex-wrap items-center justify-between gap-2">
                <h2 className="flex items-center gap-2 text-sm font-semibold text-fg">
                  <span
                    className="flex size-6 items-center justify-center rounded"
                    style={{ backgroundColor: `${b.color}1a`, color: b.color }}
                  >
                    <Icon className="size-3.5" aria-hidden="true" />
                  </span>
                  {b.name}
                </h2>
                <SlaDialog
                  teamId={b.id}
                  teamName={b.name}
                  dienstzeiten={dienstzeiten}
                />
              </div>

              <Card className="divide-y divide-line">
                {b.slaPolicies.length === 0 && (
                  <p className="px-5 py-6 text-center text-sm text-fg-muted">
                    Keine Fristen hinterlegt. Vorgänge dieses Bereichs zeigen
                    dann keine Ampel.
                  </p>
                )}

                {b.slaPolicies.map((p) => (
                  <div
                    key={p.id}
                    className={`flex flex-wrap items-center gap-4 px-5 py-3 ${p.isActive ? "" : "opacity-55"}`}
                  >
                    <div className="w-28 shrink-0">
                      <PriorityBadge priority={p.priority} size="sm" />
                    </div>

                    <div className="min-w-0 flex-1">
                      <dl className="flex flex-wrap gap-x-6 gap-y-1 text-sm">
                        <div className="flex gap-1.5">
                          <dt className="text-fg-subtle">Erstreaktion:</dt>
                          <dd className="font-medium text-fg">
                            {formatDuration(p.firstResponseMinutes)}
                          </dd>
                        </div>
                        <div className="flex gap-1.5">
                          <dt className="text-fg-subtle">Lösung:</dt>
                          <dd className="font-medium text-fg">
                            {formatDuration(p.resolutionMinutes)}
                          </dd>
                        </div>
                        <div className="flex gap-1.5">
                          <dt className="text-fg-subtle">Warnung ab:</dt>
                          <dd className="text-fg-muted">
                            {formatPercent(p.warningThreshold)}
                          </dd>
                        </div>
                      </dl>
                      <p className="mt-0.5 flex items-center gap-1.5 text-xs text-fg-subtle">
                        <Clock className="size-3.5" aria-hidden="true" />
                        {p.businessHours.name}
                      </p>
                    </div>

                    <SlaDialog
                      teamId={b.id}
                      teamName={b.name}
                      dienstzeiten={dienstzeiten}
                      frist={{
                        id: p.id,
                        name: p.name,
                        priority: p.priority,
                        firstResponseMinutes: p.firstResponseMinutes,
                        resolutionMinutes: p.resolutionMinutes,
                        warningThreshold: p.warningThreshold,
                        businessHoursId: p.businessHoursId,
                        isActive: p.isActive,
                      }}
                    />
                  </div>
                ))}
              </Card>
            </section>
          );
        })}

        {einfachBereiche.length > 0 && (
          <Card>
            <div className="flex items-start gap-2.5 px-5 py-4">
              <Info
                className="mt-0.5 size-4 shrink-0 text-fg-subtle"
                aria-hidden="true"
              />
              <div>
                <p className="text-sm font-medium text-fg">
                  Ohne Fristen: {einfachBereiche.map((b) => b.name).join(", ")}
                </p>
                <p className="mt-0.5 text-sm text-fg-muted">
                  Diese Bereiche nutzen die schlanke Ansicht und haben deshalb
                  keine Fristen. Wenn Sie dort Fristen brauchen, stellen Sie den
                  Bereich unter{" "}
                  <Badge tone="outline" size="sm">
                    Bereiche
                  </Badge>{" "}
                  auf den vollen Arbeitsplatz um.
                </p>
              </div>
            </div>
          </Card>
        )}
      </div>
    </>
  );
}
