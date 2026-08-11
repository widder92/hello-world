import type { Metadata } from "next";

import { PageHeader } from "@/components/layout/page-header";
import { BausteinDialog } from "@/components/verwaltung/baustein-dialog";
import { Badge } from "@/components/ui/badge";
import { Card } from "@/components/ui/card";
import { db } from "@/server/db";
import { requireAdmin } from "@/server/policy";

export const metadata: Metadata = { title: "Textbausteine" };

/**
 * Textbausteine.
 *
 * Wiederkehrende Antworten in gleichbleibender Qualität – und in einem Ton,
 * auf den sich das Haus geeinigt hat. Erscheinen im Antwortfeld unter dem
 * Text und lassen sich mit einem Klick einfügen.
 */
export default async function TextbausteinePage() {
  await requireAdmin();

  const [bausteine, bereiche] = await Promise.all([
    db.cannedResponse.findMany({
      orderBy: [{ teamId: "asc" }, { sortOrder: "asc" }],
      include: { team: { select: { id: true, name: true, color: true } } },
    }),
    db.team.findMany({
      where: { isActive: true },
      orderBy: [{ sortOrder: "asc" }, { name: "asc" }],
      select: { id: true, name: true },
    }),
  ]);

  return (
    <>
      <PageHeader
        title="Textbausteine"
        description="Erscheinen im Antwortfeld und lassen sich mit einem Klick einfügen. Bausteine ohne Bereich stehen allen zur Verfügung."
        actions={<BausteinDialog bereiche={bereiche} />}
      />

      <div className="px-4 py-5 sm:px-6">
        <Card className="divide-y divide-line">
          {bausteine.length === 0 && (
            <p className="px-6 py-12 text-center text-sm text-fg-muted">
              Noch keine Textbausteine.
            </p>
          )}

          {bausteine.map((b) => (
            <div
              key={b.id}
              className={`flex flex-wrap items-start gap-4 px-5 py-3.5 ${b.isActive ? "" : "opacity-55"}`}
            >
              <div className="min-w-0 flex-1">
                <div className="flex flex-wrap items-center gap-2">
                  <span className="font-medium text-fg">{b.title}</span>
                  {b.shortcut && (
                    <code className="rounded bg-surface-2 px-1.5 py-0.5 font-mono text-xs text-fg-muted">
                      {b.shortcut}
                    </code>
                  )}
                  {b.team ? (
                    <Badge tone="outline" size="sm">
                      <span
                        className="size-2 rounded-full"
                        style={{ backgroundColor: b.team.color }}
                        aria-hidden="true"
                      />
                      {b.team.name}
                    </Badge>
                  ) : (
                    <Badge tone="neutral" size="sm">
                      Alle Bereiche
                    </Badge>
                  )}
                  {!b.isActive && (
                    <Badge tone="neutral" size="sm">
                      Nicht aktiv
                    </Badge>
                  )}
                </div>

                <p className="mt-1 line-clamp-3 whitespace-pre-wrap text-sm text-fg-muted">
                  {b.body}
                </p>
              </div>

              <BausteinDialog
                bereiche={bereiche}
                baustein={{
                  id: b.id,
                  teamId: b.teamId,
                  title: b.title,
                  shortcut: b.shortcut,
                  body: b.body,
                  isActive: b.isActive,
                }}
              />
            </div>
          ))}
        </Card>
      </div>
    </>
  );
}
