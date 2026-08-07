import type { Metadata } from "next";
import Link from "next/link";
import { redirect } from "next/navigation";
import { ArticleVisibility } from "@prisma/client";
import { BookOpen, Lock, Search, ThumbsUp } from "lucide-react";

import { PageHeader } from "@/components/layout/page-header";
import { Badge } from "@/components/ui/badge";
import { Card } from "@/components/ui/card";
import { Input } from "@/components/ui/input";
import { db } from "@/server/db";
import { getActor, isAgent } from "@/server/policy";

export const metadata: Metadata = { title: "Wissensdatenbank" };

/**
 * Wissensdatenbank.
 *
 * Öffentliche Artikel sehen alle, interne nur die Bereichsmitglieder. Die
 * Trennung ist keine Förmlichkeit: Ein Artikel über den Zugang zum Serverraum
 * gehört nicht in ein Portal, das 1000 Menschen offensteht.
 */
export default async function WissenPage({
  searchParams,
}: {
  searchParams: Promise<{ q?: string }>;
}) {
  const actor = await getActor();
  if (!actor) redirect("/anmelden");

  const { q } = await searchParams;
  const suche = q?.trim() ?? "";
  const darfIntern = isAgent(actor);

  const sichtbarkeit = darfIntern
    ? undefined
    : { visibility: ArticleVisibility.OEFFENTLICH };

  const artikel = suche
    ? await sucheArtikel(suche, darfIntern)
    : await db.kbArticle.findMany({
        where: { published: true, ...sichtbarkeit },
        orderBy: [{ viewCount: "desc" }, { title: "asc" }],
        take: 50,
        select: {
          id: true,
          slug: true,
          title: true,
          summary: true,
          visibility: true,
          viewCount: true,
          helpfulYes: true,
          team: { select: { name: true, color: true } },
        },
      });

  return (
    <>
      <PageHeader
        title="Wissensdatenbank"
        description="Anleitungen und Antworten auf häufige Fragen. Vielleicht steht Ihre Lösung schon hier."
      >
        <form className="relative max-w-md">
          <label htmlFor="wissen-suche" className="sr-only">
            Wissensdatenbank durchsuchen
          </label>
          <Search
            className="pointer-events-none absolute left-2.5 top-1/2 size-4 -translate-y-1/2 text-fg-subtle"
            aria-hidden="true"
          />
          <Input
            id="wissen-suche"
            name="q"
            defaultValue={suche}
            placeholder="Wonach suchen Sie?"
            className="pl-8.5"
          />
        </form>
      </PageHeader>

      <div className="px-4 py-5 sm:px-6">
        {suche && (
          <p className="mb-3 text-sm text-fg-muted">
            {artikel.length === 0
              ? `Keine Treffer für „${suche}".`
              : `${artikel.length} ${artikel.length === 1 ? "Treffer" : "Treffer"} für „${suche}".`}
          </p>
        )}

        {artikel.length === 0 && !suche ? (
          <Card>
            <div className="px-6 py-16 text-center">
              <BookOpen
                className="mx-auto size-8 text-fg-subtle"
                aria-hidden="true"
              />
              <p className="mt-2 text-sm font-medium text-fg">
                Noch keine Artikel
              </p>
            </div>
          </Card>
        ) : (
          <ul className="grid gap-3 sm:grid-cols-2 lg:grid-cols-3">
            {artikel.map((a) => (
              <li key={a.id}>
                <Link
                  href={`/wissen/${a.slug}`}
                  className="flex h-full flex-col rounded-lg border border-line bg-surface p-4 shadow-card transition-colors hover:bg-surface-2"
                >
                  <div className="mb-1.5 flex flex-wrap items-center gap-1.5">
                    {a.team && (
                      <span className="inline-flex items-center gap-1.5 text-xs text-fg-muted">
                        <span
                          className="size-2 rounded-full"
                          style={{ backgroundColor: a.team.color }}
                          aria-hidden="true"
                        />
                        {a.team.name}
                      </span>
                    )}
                    {a.visibility === ArticleVisibility.INTERN && (
                      <Badge tone="warning" size="sm">
                        <Lock aria-hidden="true" />
                        Intern
                      </Badge>
                    )}
                  </div>

                  <h2 className="font-medium text-fg">{a.title}</h2>
                  {a.summary && (
                    <p className="mt-1 flex-1 text-sm text-fg-muted">
                      {a.summary}
                    </p>
                  )}

                  {a.helpfulYes > 0 && (
                    <p className="mt-2.5 flex items-center gap-1 text-xs text-fg-subtle">
                      <ThumbsUp className="size-3.5" aria-hidden="true" />
                      {a.helpfulYes} fanden das hilfreich
                    </p>
                  )}
                </Link>
              </li>
            ))}
          </ul>
        )}
      </div>
    </>
  );
}

/**
 * Volltextsuche mit deutscher Wortstammerkennung – „Drucker druckt nicht"
 * findet auch „Der Drucker druckte gestern noch".
 */
async function sucheArtikel(begriff: string, darfIntern: boolean) {
  return db.$queryRaw<
    {
      id: string;
      slug: string;
      title: string;
      summary: string | null;
      visibility: ArticleVisibility;
      viewCount: number;
      helpfulYes: number;
      team: { name: string; color: string } | null;
    }[]
  >`
    SELECT
      a."id", a."slug", a."title", a."summary", a."visibility",
      a."viewCount", a."helpfulYes",
      CASE WHEN t."id" IS NULL THEN NULL
           ELSE json_build_object('name', t."name", 'color', t."color")
      END AS team
    FROM "KbArticle" a
    LEFT JOIN "Team" t ON t."id" = a."teamId"
    WHERE a."published" = true
      AND (${darfIntern} OR a."visibility" = 'OEFFENTLICH')
      AND a."searchVector" @@ websearch_to_tsquery('german', ${begriff})
    ORDER BY ts_rank(a."searchVector", websearch_to_tsquery('german', ${begriff})) DESC
    LIMIT 50
  `;
}
