import type { Metadata } from "next";
import Link from "next/link";
import { redirect } from "next/navigation";
import { ArticleVisibility } from "@prisma/client";
import { BookOpen, Search, Ticket } from "lucide-react";

import { PageHeader } from "@/components/layout/page-header";
import { StatusBadge } from "@/components/ticket/badges";
import { Card } from "@/components/ui/card";
import { Input } from "@/components/ui/input";
import { db } from "@/server/db";
import { getActor, isAgent, ticketVisibilityFilter } from "@/server/policy";
import { formatCompact } from "@/lib/format";

export const metadata: Metadata = { title: "Suche" };

interface TicketTreffer {
  id: string;
  number: string;
  subject: string;
  status: string;
  lastActivityAt: Date;
  teamName: string;
  teamColor: string;
  siteName: string | null;
  /** Ausschnitt aus der gefundenen Nachricht. */
  fundstelle: string | null;
}

/**
 * Volltextsuche über Vorgänge und Wissensartikel.
 *
 * Sucht nicht nur im Betreff, sondern auch im Verlauf: „heizung kaputt"
 * findet einen Vorgang, in dessen dritter Nachricht „Heizung ist defekt"
 * steht. Möglich macht das die deutsche Wortstammerkennung von PostgreSQL.
 *
 * Der Sichtbarkeitsfilter greift immer – die Suche zeigt niemandem Vorgänge,
 * die er über die Liste nicht auch sähe.
 */
export default async function SuchePage({
  searchParams,
}: {
  searchParams: Promise<{ q?: string }>;
}) {
  const actor = await getActor();
  if (!actor) redirect("/anmelden");

  const { q } = await searchParams;
  const begriff = q?.trim() ?? "";

  const darfIntern = isAgent(actor);

  const [tickets, artikel] = begriff
    ? await Promise.all([
        sucheTickets(begriff, actor),
        sucheArtikel(begriff, darfIntern),
      ])
    : [[], []];

  return (
    <>
      <PageHeader
        title="Suche"
        description={
          "Durchsucht Betreff, Verlauf und Wissensdatenbank. Verwandte " +
          "Wortformen werden mitgefunden: Die Suche nach Drucker findet auch " +
          "Druckern und Druckers."
        }
      >
        <form className="relative max-w-2xl">
          <label htmlFor="volltextsuche" className="sr-only">
            Suchbegriff
          </label>
          <Search
            className="pointer-events-none absolute left-3 top-1/2 size-4.5 -translate-y-1/2 text-fg-subtle"
            aria-hidden="true"
          />
          <Input
            id="volltextsuche"
            name="q"
            defaultValue={begriff}
            autoFocus
            placeholder="Wonach suchen Sie? Zum Beispiel: Heizung Wohnheim Nord"
            className="h-11 pl-10 text-base"
          />
        </form>
      </PageHeader>

      <div className="space-y-6 px-4 py-5 sm:px-6">
        {!begriff && (
          <p className="py-10 text-center text-sm text-fg-muted">
            Geben Sie oben einen Begriff ein.
          </p>
        )}

        {begriff && tickets.length === 0 && artikel.length === 0 && (
          <Card>
            <div className="px-6 py-12 text-center">
              <p className="text-sm font-medium text-fg">
                Keine Treffer für „{begriff}“
              </p>
              <p className="mx-auto mt-1 max-w-md text-sm text-fg-muted">
                Versuchen Sie einen anderen Begriff oder suchen Sie nach der
                Vorgangsnummer.
              </p>
            </div>
          </Card>
        )}

        {tickets.length > 0 && (
          <section>
            <h2 className="mb-2 flex items-center gap-2 text-sm font-semibold text-fg">
              <Ticket className="size-4 text-fg-subtle" aria-hidden="true" />
              Vorgänge ({tickets.length})
            </h2>
            <Card className="divide-y divide-line">
              {tickets.map((t) => (
                <Link
                  key={t.id}
                  href={`/tickets/${t.number}`}
                  className="block px-5 py-3 transition-colors hover:bg-surface-2"
                >
                  <div className="flex flex-wrap items-start justify-between gap-3">
                    <div className="min-w-0 flex-1">
                      <p className="font-medium text-fg">{t.subject}</p>
                      <p className="mt-0.5 flex flex-wrap items-center gap-x-2 text-xs text-fg-muted">
                        <span className="font-mono">{t.number}</span>
                        <span aria-hidden="true">·</span>
                        <span className="inline-flex items-center gap-1.5">
                          <span
                            className="size-2 rounded-full"
                            style={{ backgroundColor: t.teamColor }}
                            aria-hidden="true"
                          />
                          {t.teamName}
                        </span>
                        {t.siteName && (
                          <>
                            <span aria-hidden="true">·</span>
                            <span>{t.siteName}</span>
                          </>
                        )}
                        <span aria-hidden="true">·</span>
                        <time dateTime={t.lastActivityAt.toISOString()}>
                          {formatCompact(t.lastActivityAt)}
                        </time>
                      </p>
                      {t.fundstelle && (
                        <p className="mt-1 line-clamp-2 text-sm text-fg-subtle">
                          … {t.fundstelle} …
                        </p>
                      )}
                    </div>
                    <StatusBadge
                      status={t.status as never}
                      einfach={!darfIntern}
                      size="sm"
                    />
                  </div>
                </Link>
              ))}
            </Card>
          </section>
        )}

        {artikel.length > 0 && (
          <section>
            <h2 className="mb-2 flex items-center gap-2 text-sm font-semibold text-fg">
              <BookOpen className="size-4 text-fg-subtle" aria-hidden="true" />
              Wissensdatenbank ({artikel.length})
            </h2>
            <Card className="divide-y divide-line">
              {artikel.map((a) => (
                <Link
                  key={a.id}
                  href={`/wissen/${a.slug}`}
                  className="block px-5 py-3 transition-colors hover:bg-surface-2"
                >
                  <p className="font-medium text-fg">{a.title}</p>
                  {a.summary && (
                    <p className="mt-0.5 text-sm text-fg-muted">{a.summary}</p>
                  )}
                </Link>
              ))}
            </Card>
          </section>
        )}
      </div>
    </>
  );
}

/**
 * Sucht Vorgänge über Betreff, Nummer und Nachrichtentext.
 *
 * Die Sichtbarkeit wird als Prisma-Bedingung vorab in eine Kennungsliste
 * aufgelöst und in die SQL-Abfrage hineingereicht. So bleibt die
 * Rechteprüfung an ihrer einen Stelle, obwohl die Suche selbst rohes SQL
 * braucht – die Volltextsuche lässt sich mit Prisma nicht ausdrücken.
 */
async function sucheTickets(
  begriff: string,
  actor: NonNullable<Awaited<ReturnType<typeof getActor>>>,
): Promise<TicketTreffer[]> {
  const sichtbare = await db.ticket.findMany({
    where: ticketVisibilityFilter(actor),
    select: { id: true },
    take: 5000,
  });

  if (sichtbare.length === 0) return [];
  const ids = sichtbare.map((t) => t.id);

  const treffer = await db.$queryRaw<
    {
      id: string;
      number: string;
      subject: string;
      status: string;
      lastActivityAt: Date;
      teamName: string;
      teamColor: string;
      siteName: string | null;
      fundstelle: string | null;
    }[]
  >`
    SELECT
      t."id", t."number", t."subject", t."status"::text, t."lastActivityAt",
      te."name"  AS "teamName",
      te."color" AS "teamColor",
      s."name"   AS "siteName",
      (
        SELECT ts_headline(
                 'german', m."bodyText",
                 websearch_to_tsquery('german', ${begriff}),
                 'StartSel=,StopSel=,MaxWords=25,MinWords=10,MaxFragments=1'
               )
        FROM "TicketMessage" m
        WHERE m."ticketId" = t."id"
          AND m."searchVector" @@ websearch_to_tsquery('german', ${begriff})
        ORDER BY m."createdAt" ASC
        LIMIT 1
      ) AS "fundstelle"
    FROM "Ticket" t
    JOIN "Team" te ON te."id" = t."teamId"
    LEFT JOIN "Site" s ON s."id" = t."siteId"
    WHERE t."id" = ANY(${ids})
      AND (
        t."searchVector" @@ websearch_to_tsquery('german', ${begriff})
        OR EXISTS (
          SELECT 1 FROM "TicketMessage" m
          WHERE m."ticketId" = t."id"
            AND m."searchVector" @@ websearch_to_tsquery('german', ${begriff})
        )
      )
    ORDER BY
      ts_rank(t."searchVector", websearch_to_tsquery('german', ${begriff})) DESC,
      t."lastActivityAt" DESC
    LIMIT 50
  `;

  return treffer;
}

async function sucheArtikel(begriff: string, darfIntern: boolean) {
  return db.$queryRaw<
    { id: string; slug: string; title: string; summary: string | null }[]
  >`
    SELECT "id", "slug", "title", "summary"
    FROM "KbArticle"
    WHERE "published" = true
      AND (${darfIntern} OR "visibility" = ${ArticleVisibility.OEFFENTLICH}::"ArticleVisibility")
      AND "searchVector" @@ websearch_to_tsquery('german', ${begriff})
    ORDER BY ts_rank("searchVector", websearch_to_tsquery('german', ${begriff})) DESC
    LIMIT 10
  `;
}
