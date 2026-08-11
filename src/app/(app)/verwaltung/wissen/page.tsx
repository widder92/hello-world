import type { Metadata } from "next";
import Link from "next/link";
import { ArticleVisibility } from "@prisma/client";
import { ExternalLink, Lock, ThumbsDown, ThumbsUp } from "lucide-react";

import { PageHeader } from "@/components/layout/page-header";
import { ArtikelDialog } from "@/components/verwaltung/artikel-dialog";
import { Badge } from "@/components/ui/badge";
import { Card } from "@/components/ui/card";
import { db } from "@/server/db";
import { requireAdmin } from "@/server/policy";
import { formatDate } from "@/lib/format";

export const metadata: Metadata = { title: "Wissensartikel" };

/**
 * Wissensartikel.
 *
 * Öffentliche Artikel werden im Meldeformular beim Tippen vorgeschlagen –
 * manche Anliegen lösen sich damit, bevor überhaupt ein Ticket entsteht. Wie
 * gut das klappt, zeigt die Rückmeldung unter jedem Artikel.
 */
export default async function WissenVerwaltungPage() {
  await requireAdmin();

  const [artikel, bereiche] = await Promise.all([
    db.kbArticle.findMany({
      orderBy: [{ published: "desc" }, { viewCount: "desc" }],
      include: {
        team: { select: { name: true, color: true } },
        author: { select: { name: true } },
      },
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
        title="Wissensartikel"
        description="Öffentliche Artikel werden Meldenden beim Schreiben vorgeschlagen. Interne Artikel sehen nur Bereichsmitglieder."
        actions={<ArtikelDialog bereiche={bereiche} />}
      />

      <div className="px-4 py-5 sm:px-6">
        <Card className="divide-y divide-line">
          {artikel.length === 0 && (
            <p className="px-6 py-12 text-center text-sm text-fg-muted">
              Noch keine Artikel.
            </p>
          )}

          {artikel.map((a) => (
            <div
              key={a.id}
              className={`flex flex-wrap items-start gap-4 px-5 py-3.5 ${a.published ? "" : "opacity-60"}`}
            >
              <div className="min-w-0 flex-1">
                <div className="flex flex-wrap items-center gap-2">
                  <span className="font-medium text-fg">{a.title}</span>

                  {a.visibility === ArticleVisibility.INTERN ? (
                    <Badge tone="warning" size="sm">
                      <Lock aria-hidden="true" />
                      Intern
                    </Badge>
                  ) : (
                    <Badge tone="success" size="sm">
                      Öffentlich
                    </Badge>
                  )}

                  {!a.published && (
                    <Badge tone="neutral" size="sm">
                      Entwurf
                    </Badge>
                  )}

                  {a.team && (
                    <Badge tone="outline" size="sm">
                      <span
                        className="size-2 rounded-full"
                        style={{ backgroundColor: a.team.color }}
                        aria-hidden="true"
                      />
                      {a.team.name}
                    </Badge>
                  )}
                </div>

                {a.summary && (
                  <p className="mt-0.5 text-sm text-fg-muted">{a.summary}</p>
                )}

                <p className="mt-1 flex flex-wrap items-center gap-x-3 text-xs text-fg-subtle">
                  <span>{a.viewCount} Aufrufe</span>
                  {(a.helpfulYes > 0 || a.helpfulNo > 0) && (
                    <>
                      <span className="inline-flex items-center gap-1">
                        <ThumbsUp className="size-3" aria-hidden="true" />
                        {a.helpfulYes}
                      </span>
                      <span className="inline-flex items-center gap-1">
                        <ThumbsDown className="size-3" aria-hidden="true" />
                        {a.helpfulNo}
                      </span>
                    </>
                  )}
                  {a.author && <span>von {a.author.name}</span>}
                  <span>geändert {formatDate(a.updatedAt)}</span>
                </p>
              </div>

              <div className="flex shrink-0 gap-2">
                {a.published && (
                  <Link
                    href={`/wissen/${a.slug}`}
                    className="inline-flex items-center gap-1.5 rounded-md border border-line px-2.5 py-1.5 text-xs text-fg-muted transition-colors hover:bg-surface-2 hover:text-fg"
                  >
                    <ExternalLink className="size-3.5" aria-hidden="true" />
                    Ansehen
                  </Link>
                )}
                <ArtikelDialog
                  bereiche={bereiche}
                  artikel={{
                    id: a.id,
                    title: a.title,
                    summary: a.summary,
                    body: a.body,
                    teamId: a.teamId,
                    visibility: a.visibility,
                    published: a.published,
                  }}
                />
              </div>
            </div>
          ))}
        </Card>
      </div>
    </>
  );
}
