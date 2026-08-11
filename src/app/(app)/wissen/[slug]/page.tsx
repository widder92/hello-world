import type { Metadata } from "next";
import Link from "next/link";
import { notFound, redirect } from "next/navigation";
import { ArticleVisibility } from "@prisma/client";
import { ArrowLeft, Lock, PenLine } from "lucide-react";

import { Markdown } from "@/components/ui/markdown";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import { db } from "@/server/db";
import { getActor, isAgent } from "@/server/policy";
import { formatDate } from "@/lib/format";

export async function generateMetadata({
  params,
}: {
  params: Promise<{ slug: string }>;
}): Promise<Metadata> {
  const { slug } = await params;
  const artikel = await db.kbArticle.findUnique({
    where: { slug },
    select: { title: true },
  });
  return { title: artikel?.title ?? "Artikel" };
}

export default async function ArtikelPage({
  params,
}: {
  params: Promise<{ slug: string }>;
}) {
  const actor = await getActor();
  if (!actor) redirect("/anmelden");

  const { slug } = await params;

  const artikel = await db.kbArticle.findUnique({
    where: { slug },
    include: {
      team: { select: { name: true, color: true } },
      author: { select: { name: true } },
    },
  });

  if (!artikel || !artikel.published) notFound();

  // Interne Artikel nur für Bereichsmitglieder. Ein 404 statt einer
  // Fehlermeldung – so verrät die Seite nicht einmal, dass es den Artikel gibt.
  if (artikel.visibility === ArticleVisibility.INTERN && !isAgent(actor)) {
    notFound();
  }

  // Aufrufzähler nebenbei hochsetzen; ein Fehler dabei darf die Seite nicht
  // scheitern lassen.
  void db.kbArticle
    .update({
      where: { id: artikel.id },
      data: { viewCount: { increment: 1 } },
    })
    .catch(() => {});

  return (
    <div className="mx-auto max-w-3xl px-4 py-6 sm:px-6">
      <Button asChild variant="ghost" size="sm" className="-ml-2 mb-4">
        <Link href="/wissen">
          <ArrowLeft aria-hidden="true" />
          Zur Wissensdatenbank
        </Link>
      </Button>

      <article>
        <header className="mb-6">
          <div className="mb-2 flex flex-wrap items-center gap-2">
            {artikel.team && (
              <span className="inline-flex items-center gap-1.5 text-sm text-fg-muted">
                <span
                  className="size-2 rounded-full"
                  style={{ backgroundColor: artikel.team.color }}
                  aria-hidden="true"
                />
                {artikel.team.name}
              </span>
            )}
            {artikel.visibility === ArticleVisibility.INTERN && (
              <Badge tone="warning">
                <Lock aria-hidden="true" />
                Nur intern
              </Badge>
            )}
          </div>

          <h1 className="text-3xl font-semibold tracking-tight text-fg text-balance">
            {artikel.title}
          </h1>

          {artikel.summary && (
            <p className="mt-2 text-lg text-fg-muted">{artikel.summary}</p>
          )}

          <p className="mt-3 text-xs text-fg-subtle">
            {artikel.author && <>Von {artikel.author.name} · </>}
            Zuletzt geändert am {formatDate(artikel.updatedAt)}
          </p>
        </header>

        <Markdown text={artikel.body} />
      </article>

      <footer className="mt-10 border-t border-line pt-5">
        <p className="text-sm text-fg-muted">
          Hat Ihnen das nicht weitergeholfen?
        </p>
        <Button asChild className="mt-2">
          <Link href="/neu">
            <PenLine aria-hidden="true" />
            Meldung schreiben
          </Link>
        </Button>
      </footer>
    </div>
  );
}
