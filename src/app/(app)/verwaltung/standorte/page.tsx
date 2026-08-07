import type { Metadata } from "next";
import { Building2 } from "lucide-react";

import { PageHeader } from "@/components/layout/page-header";
import { StandortDialog } from "@/components/verwaltung/standort-dialog";
import { Badge } from "@/components/ui/badge";
import { Card } from "@/components/ui/card";
import { db } from "@/server/db";
import { requireAdmin } from "@/server/policy";
import { SITE_TYPE } from "@/lib/domain";

export const metadata: Metadata = { title: "Standorte" };

/**
 * Standorte.
 *
 * Bei einem Träger mit Schule, mehreren Wohnheimen, betreutem Wohnen und
 * Werkstatt ist der Standort die wichtigste Filter- und Auswertungsachse –
 * und bei Bereichen mit Eingang je Standort bestimmt er, wo eine Meldung
 * ankommt.
 */
export default async function StandortePage() {
  await requireAdmin();

  const standorte = await db.site.findMany({
    orderBy: [{ sortOrder: "asc" }, { name: "asc" }],
    include: {
      _count: { select: { users: true, tickets: true, queues: true, assets: true } },
    },
  });

  return (
    <>
      <PageHeader
        title="Standorte"
        description="Ein neuer Standort bekommt in allen Bereichen mit Eingang je Standort automatisch einen eigenen Eingang – Sie müssen dort nur noch die Empfangsadresse nachtragen."
        actions={<StandortDialog />}
      />

      <div className="px-4 py-5 sm:px-6">
        <Card className="divide-y divide-line">
          {standorte.map((s) => (
            <div
              key={s.id}
              className={`flex flex-wrap items-center gap-4 px-5 py-3.5 ${s.isActive ? "" : "opacity-55"}`}
            >
              <span className="flex size-10 shrink-0 items-center justify-center rounded-lg bg-surface-2 text-fg-muted">
                <Building2 className="size-5" aria-hidden="true" />
              </span>

              <div className="min-w-0 flex-1">
                <div className="flex flex-wrap items-center gap-2">
                  <span className="font-medium text-fg">{s.name}</span>
                  <Badge tone="outline" size="sm">
                    {SITE_TYPE[s.type]}
                  </Badge>
                  {!s.isActive && (
                    <Badge tone="neutral" size="sm">
                      Nicht aktiv
                    </Badge>
                  )}
                </div>

                <p className="mt-0.5 flex flex-wrap items-center gap-x-3 text-xs text-fg-muted">
                  {(s.street || s.city) && (
                    <span>
                      {[s.street, [s.zip, s.city].filter(Boolean).join(" ")]
                        .filter(Boolean)
                        .join(", ")}
                    </span>
                  )}
                  <span>{s._count.users} Mitarbeitende</span>
                  <span>{s._count.queues} Eingänge</span>
                  <span>{s._count.tickets} Vorgänge</span>
                  <span>{s._count.assets} Geräte</span>
                </p>
              </div>

              <StandortDialog
                standort={{
                  id: s.id,
                  name: s.name,
                  shortName: s.shortName,
                  type: s.type,
                  street: s.street,
                  zip: s.zip,
                  city: s.city,
                  isActive: s.isActive,
                }}
              />
            </div>
          ))}
        </Card>
      </div>
    </>
  );
}
