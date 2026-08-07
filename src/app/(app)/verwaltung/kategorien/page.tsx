import type { Metadata } from "next";
import { TeamMode } from "@prisma/client";
import { Info } from "lucide-react";

import { teamIcon } from "@/components/layout/icon";
import { PageHeader } from "@/components/layout/page-header";
import { KategorieDialog } from "@/components/verwaltung/kategorie-dialog";
import { Badge } from "@/components/ui/badge";
import { Card } from "@/components/ui/card";
import { db } from "@/server/db";
import { requireAdmin } from "@/server/policy";

export const metadata: Metadata = { title: "Kategorien" };

/**
 * Kategorien je Bereich.
 *
 * Sie ordnen Vorgänge ein und können zusätzliche Formularfelder mitbringen –
 * bei „Drucker und Kopierer" etwa die Frage, wo das Gerät steht. Genau die
 * Rückfrage, die sonst einen Tag kostet.
 *
 * Nur Bereiche im vollen Arbeitsplatz nutzen Kategorien.
 */
export default async function KategorienPage() {
  await requireAdmin();

  const bereiche = await db.team.findMany({
    where: { isActive: true },
    orderBy: [{ sortOrder: "asc" }, { name: "asc" }],
    select: {
      id: true,
      name: true,
      color: true,
      icon: true,
      mode: true,
      categories: {
        where: { parentId: null },
        orderBy: [{ sortOrder: "asc" }, { name: "asc" }],
        select: {
          id: true,
          name: true,
          description: true,
          formSchema: true,
          isActive: true,
          _count: { select: { tickets: true } },
          children: {
            orderBy: { name: "asc" },
            select: {
              id: true,
              name: true,
              description: true,
              isActive: true,
              _count: { select: { tickets: true } },
            },
          },
        },
      },
    },
  });

  const vollBereiche = bereiche.filter((b) => b.mode === TeamMode.VOLL);
  const einfachBereiche = bereiche.filter((b) => b.mode === TeamMode.EINFACH);

  return (
    <>
      <PageHeader
        title="Kategorien"
        description="Kategorien ordnen Vorgänge ein und können zusätzliche Felder im Meldeformular mitbringen – etwa die Frage nach dem Standort eines Druckers."
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
                <KategorieDialog
                  teamId={b.id}
                  teamName={b.name}
                  oberkategorien={b.categories.map((k) => ({
                    id: k.id,
                    name: k.name,
                  }))}
                />
              </div>

              <Card className="divide-y divide-line">
                {b.categories.length === 0 && (
                  <p className="px-5 py-6 text-center text-sm text-fg-muted">
                    Keine Kategorien. Melder wählen dann nur den Bereich.
                  </p>
                )}

                {b.categories.map((k) => {
                  const felder = Array.isArray(k.formSchema)
                    ? (k.formSchema as unknown[]).length
                    : 0;

                  return (
                    <div key={k.id} className="px-5 py-3">
                      <div
                        className={`flex flex-wrap items-center gap-3 ${k.isActive ? "" : "opacity-55"}`}
                      >
                        <div className="min-w-0 flex-1">
                          <div className="flex flex-wrap items-center gap-2">
                            <span className="font-medium text-fg">{k.name}</span>
                            {felder > 0 && (
                              <Badge tone="outline" size="sm">
                                {felder} Zusatzfeld{felder === 1 ? "" : "er"}
                              </Badge>
                            )}
                            {!k.isActive && (
                              <Badge tone="neutral" size="sm">
                                Nicht aktiv
                              </Badge>
                            )}
                          </div>
                          {k.description && (
                            <p className="mt-0.5 text-xs text-fg-muted">
                              {k.description}
                            </p>
                          )}
                          <p className="mt-0.5 text-xs text-fg-subtle">
                            {k._count.tickets} Vorgänge
                          </p>
                        </div>

                        <KategorieDialog
                          teamId={b.id}
                          teamName={b.name}
                          oberkategorien={b.categories
                            .filter((o) => o.id !== k.id)
                            .map((o) => ({ id: o.id, name: o.name }))}
                          kategorie={{
                            id: k.id,
                            name: k.name,
                            description: k.description,
                            parentId: null,
                            isActive: k.isActive,
                          }}
                        />
                      </div>

                      {k.children.length > 0 && (
                        <ul className="mt-2 space-y-1 border-l-2 border-line pl-4">
                          {k.children.map((u) => (
                            <li
                              key={u.id}
                              className={`flex items-center justify-between gap-3 ${u.isActive ? "" : "opacity-55"}`}
                            >
                              <span className="min-w-0">
                                <span className="text-sm text-fg">{u.name}</span>
                                <span className="ml-2 text-xs text-fg-subtle">
                                  {u._count.tickets} Vorgänge
                                </span>
                              </span>
                              <KategorieDialog
                                teamId={b.id}
                                teamName={b.name}
                                oberkategorien={b.categories.map((o) => ({
                                  id: o.id,
                                  name: o.name,
                                }))}
                                kategorie={{
                                  id: u.id,
                                  name: u.name,
                                  description: u.description,
                                  parentId: k.id,
                                  isActive: u.isActive,
                                }}
                              />
                            </li>
                          ))}
                        </ul>
                      )}
                    </div>
                  );
                })}
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
              <p className="text-sm text-fg-muted">
                <span className="font-medium text-fg">
                  Ohne Kategorien: {einfachBereiche.map((b) => b.name).join(", ")}
                </span>{" "}
                – diese Bereiche nutzen die schlanke Ansicht. Melder geben dort
                nur Betreff, Beschreibung und Standort an.
              </p>
            </div>
          </Card>
        )}
      </div>
    </>
  );
}
