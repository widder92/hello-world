import type { Metadata } from "next";
import { ClipboardList } from "lucide-react";

import { teamIcon } from "@/components/layout/icon";
import { PageHeader } from "@/components/layout/page-header";
import { PriorityBadge } from "@/components/ticket/badges";
import { Badge } from "@/components/ui/badge";
import { Card } from "@/components/ui/card";
import { db } from "@/server/db";
import { requireAdmin } from "@/server/policy";

export const metadata: Metadata = { title: "Vorlagen" };

/**
 * Ticketvorlagen.
 *
 * Für alles, was aus mehreren Schritten besteht und regelmäßig vorkommt –
 * eine neue Kollegin einrichten, jemanden ausscheiden lassen. Ohne Liste
 * bleibt regelmäßig ein Schritt liegen, und das fällt erst am ersten
 * Arbeitstag auf.
 */
export default async function VorlagenPage() {
  await requireAdmin();

  const vorlagen = await db.ticketTemplate.findMany({
    orderBy: [{ teamId: "asc" }, { name: "asc" }],
    include: { team: { select: { name: true, color: true, icon: true } } },
  });

  return (
    <>
      <PageHeader
        title="Vorlagen"
        description="Vorbereitete Vorgänge mit Betreff, Text und Checkliste. Beim Anlegen eines Vorgangs auswählbar."
      />

      <div className="space-y-3 px-4 py-5 sm:px-6">
        {vorlagen.length === 0 && (
          <Card>
            <div className="px-6 py-12 text-center">
              <ClipboardList
                className="mx-auto size-8 text-fg-subtle"
                aria-hidden="true"
              />
              <p className="mt-2 text-sm text-fg-muted">Noch keine Vorlagen.</p>
            </div>
          </Card>
        )}

        {vorlagen.map((v) => {
          const Icon = teamIcon(v.team.icon);
          const checkliste = Array.isArray(v.checklist)
            ? (v.checklist as string[])
            : [];

          return (
            <Card key={v.id} className={v.isActive ? undefined : "opacity-55"}>
              <div className="px-5 py-4">
                <div className="flex flex-wrap items-center gap-2">
                  <span
                    className="flex size-7 items-center justify-center rounded"
                    style={{
                      backgroundColor: `${v.team.color}1a`,
                      color: v.team.color,
                    }}
                  >
                    <Icon className="size-4" aria-hidden="true" />
                  </span>
                  <h2 className="font-semibold text-fg">{v.name}</h2>
                  <Badge tone="outline" size="sm">
                    {v.team.name}
                  </Badge>
                  <PriorityBadge priority={v.defaultPriority} size="sm" />
                  {!v.isActive && (
                    <Badge tone="neutral" size="sm">
                      Nicht aktiv
                    </Badge>
                  )}
                </div>

                {v.description && (
                  <p className="mt-1.5 text-sm text-fg-muted">{v.description}</p>
                )}

                <p className="mt-2 font-mono text-xs text-fg-subtle">
                  {v.subjectTemplate}
                </p>

                {checkliste.length > 0 && (
                  <div className="mt-3 border-t border-line pt-3">
                    <p className="mb-1.5 text-xs font-medium text-fg">
                      Checkliste ({checkliste.length} Schritte)
                    </p>
                    <ol className="space-y-0.5">
                      {checkliste.map((schritt, i) => (
                        <li
                          key={i}
                          className="flex gap-2 text-sm text-fg-muted"
                        >
                          <span className="text-fg-subtle tabular-nums">
                            {i + 1}.
                          </span>
                          {schritt}
                        </li>
                      ))}
                    </ol>
                  </div>
                )}
              </div>
            </Card>
          );
        })}

        <p className="text-xs text-fg-subtle">
          Vorlagen werden derzeit über die Seed-Daten gepflegt. Ein Formular zum
          Bearbeiten ist noch nicht gebaut.
        </p>
      </div>
    </>
  );
}
