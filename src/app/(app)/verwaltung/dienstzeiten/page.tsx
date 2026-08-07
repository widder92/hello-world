import type { Metadata } from "next";
import { CalendarOff, Star } from "lucide-react";

import { PageHeader } from "@/components/layout/page-header";
import { DienstzeitDialog } from "@/components/verwaltung/dienstzeit-dialog";
import { Badge } from "@/components/ui/badge";
import { Card } from "@/components/ui/card";
import { db } from "@/server/db";
import { requireAdmin } from "@/server/policy";
import { formatDate } from "@/lib/format";

export const metadata: Metadata = { title: "Dienstzeiten" };

const WOCHENTAGE = [
  ["1", "Montag"],
  ["2", "Dienstag"],
  ["3", "Mittwoch"],
  ["4", "Donnerstag"],
  ["5", "Freitag"],
  ["6", "Samstag"],
  ["7", "Sonntag"],
] as const;

/**
 * Dienstzeiten und Feiertage.
 *
 * Sie bestimmen, wann die Uhr für Fristen läuft. In einem Wohnheim gelten
 * andere Zeiten als in der Verwaltung – deshalb lassen sich mehrere Pläne
 * anlegen und je Frist zuordnen.
 */
export default async function DienstzeitenPage() {
  await requireAdmin();

  const dienstzeiten = await db.businessHours.findMany({
    orderBy: [{ isDefault: "desc" }, { name: "asc" }],
    include: {
      holidays: { orderBy: { date: "asc" } },
      _count: { select: { policies: true } },
    },
  });

  return (
    <>
      <PageHeader
        title="Dienstzeiten"
        description="Fristen laufen nur innerhalb dieser Zeiten. Außerhalb – abends, am Wochenende und an Feiertagen – steht die Uhr still."
        actions={<DienstzeitDialog />}
      />

      <div className="space-y-4 px-4 py-5 sm:px-6">
        {dienstzeiten.map((d) => {
          const plan = (d.schedule ?? {}) as Record<string, [string, string][]>;
          const kommendeFeiertage = d.holidays.filter(
            (h) => h.date >= new Date(new Date().setHours(0, 0, 0, 0)),
          );

          return (
            <Card key={d.id}>
              <div className="flex flex-wrap items-start gap-4 px-5 py-4">
                <div className="min-w-0 flex-1">
                  <div className="flex flex-wrap items-center gap-2">
                    <h2 className="font-semibold text-fg">{d.name}</h2>
                    {d.isDefault && (
                      <Badge tone="primary" size="sm">
                        <Star aria-hidden="true" />
                        Vorgabe
                      </Badge>
                    )}
                    <span className="text-xs text-fg-subtle">{d.timezone}</span>
                    <span className="text-xs text-fg-subtle">
                      · {d._count.policies}{" "}
                      {d._count.policies === 1 ? "Frist" : "Fristen"} nutzen sie
                    </span>
                  </div>

                  <dl className="mt-3 grid gap-x-6 gap-y-1 text-sm sm:grid-cols-2 lg:grid-cols-3">
                    {WOCHENTAGE.map(([nummer, name]) => {
                      const fenster = plan[nummer] ?? [];
                      return (
                        <div key={nummer} className="flex gap-2">
                          <dt className="w-24 shrink-0 text-fg-subtle">{name}</dt>
                          <dd
                            className={
                              fenster.length ? "text-fg" : "text-fg-subtle"
                            }
                          >
                            {fenster.length
                              ? fenster.map((f) => `${f[0]}–${f[1]}`).join(", ")
                              : "frei"}
                          </dd>
                        </div>
                      );
                    })}
                  </dl>

                  {kommendeFeiertage.length > 0 && (
                    <div className="mt-3 border-t border-line pt-3">
                      <p className="mb-1.5 flex items-center gap-1.5 text-xs font-medium text-fg">
                        <CalendarOff className="size-3.5" aria-hidden="true" />
                        Kommende Feiertage
                      </p>
                      <ul className="flex flex-wrap gap-1.5">
                        {kommendeFeiertage.slice(0, 12).map((h) => (
                          <li
                            key={h.id}
                            className="rounded border border-line bg-surface-2 px-1.5 py-0.5 text-xs text-fg-muted"
                          >
                            {formatDate(h.date)} · {h.name}
                          </li>
                        ))}
                      </ul>
                    </div>
                  )}
                </div>

                <DienstzeitDialog
                  dienstzeit={{
                    id: d.id,
                    name: d.name,
                    timezone: d.timezone,
                    schedule: plan,
                    isDefault: d.isDefault,
                  }}
                  feiertage={d.holidays.map((h) => ({
                    id: h.id,
                    date: h.date.toISOString().slice(0, 10),
                    name: h.name,
                  }))}
                />
              </div>
            </Card>
          );
        })}
      </div>
    </>
  );
}
