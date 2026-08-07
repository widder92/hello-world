import type { Metadata } from "next";
import Link from "next/link";
import { notFound, redirect } from "next/navigation";
import { TeamMode } from "@prisma/client";

import { PageHeader } from "@/components/layout/page-header";
import { TicketList } from "@/components/ticket/ticket-list";
import { Card } from "@/components/ui/card";
import { db } from "@/server/db";
import { agentTicketFilter, getActor, requireQueueAccess } from "@/server/policy";
import { countByStatusGroup, findTickets } from "@/server/services/ticket-queries";
import { cn } from "@/lib/utils";

export const metadata: Metadata = { title: "Unsere Tickets" };

/**
 * Die Ansicht für Bereiche in der schlanken Ausbaustufe.
 *
 * Bewusst nur zwei Reiter und eine Liste. Kein Verteilerboard, keine
 * Filterleiste, keine Fristenspalte – wer hier arbeitet, hat drei Fragen:
 * Was ist neu, was ist offen, was ist erledigt. Alles andere wäre Ballast.
 *
 * Wird ein Bereich später in der Verwaltung auf die volle Ausbaustufe
 * umgestellt, führt derselbe Weg auf den Sammeleingang – die Vorgänge bleiben
 * unverändert, es kommen nur Ansichten dazu.
 */
export default async function UnsereTicketsPage({
  params,
  searchParams,
}: {
  params: Promise<{ queueId: string }>;
  searchParams: Promise<{ ansicht?: string; seite?: string }>;
}) {
  const actor = await getActor();
  if (!actor) redirect("/anmelden");

  const { queueId } = await params;
  const sp = await searchParams;

  requireQueueAccess(actor, queueId);

  const queue = await db.queue.findUnique({
    where: { id: queueId },
    select: {
      id: true,
      name: true,
      inboundEmail: true,
      site: { select: { name: true } },
      team: { select: { id: true, name: true, mode: true, description: true } },
    },
  });
  if (!queue) notFound();

  // Ein Voll-Bereich gehört in den Sammeleingang.
  if (queue.team.mode === TeamMode.VOLL) {
    redirect(`/eingang/${queueId}`);
  }

  const ansicht = sp.ansicht === "erledigt" ? "erledigt" : "offen";
  const seite = Number(sp.seite) || 1;

  const visibility = agentTicketFilter(actor, [queueId]);

  const [liste, zaehler] = await Promise.all([
    findTickets({
      actor,
      visibility,
      filter: { statusGruppe: ansicht },
      sort: ansicht === "offen" ? "aktivitaet" : "neueste",
      page: seite,
      perPage: 40,
    }),
    countByStatusGroup(actor, visibility, {}),
  ]);

  const reiter = [
    { key: "offen", label: "Offen", anzahl: zaehler.offen },
    { key: "erledigt", label: "Erledigt", anzahl: zaehler.erledigt },
  ] as const;

  return (
    <>
      <PageHeader
        title={queue.team.name}
        description={
          <>
            {queue.team.description}
            {queue.inboundEmail && (
              <span className="mt-1 block font-mono text-xs">
                {queue.inboundEmail}
              </span>
            )}
          </>
        }
      />

      <div className="px-4 py-5 sm:px-6">
        {/* Reiter als Links statt als Schaltflächen: so lässt sich die Ansicht
            als Lesezeichen ablegen und der Zurück-Knopf tut das Erwartete. */}
        <div
          role="tablist"
          aria-label="Ansicht"
          className="mb-4 inline-flex items-center gap-1 rounded-lg bg-surface-2 p-1"
        >
          {reiter.map((r) => {
            const aktiv = ansicht === r.key;
            return (
              <Link
                key={r.key}
                href={`?ansicht=${r.key}`}
                role="tab"
                aria-selected={aktiv}
                className={cn(
                  "inline-flex items-center gap-2 rounded-md px-4 py-2 text-sm font-medium transition-colors",
                  aktiv
                    ? "bg-surface text-fg shadow-card"
                    : "text-fg-muted hover:text-fg",
                )}
              >
                {r.label}
                <span
                  className={cn(
                    "rounded-full px-1.5 text-xs tabular-nums",
                    aktiv ? "bg-surface-3 text-fg-muted" : "text-fg-subtle",
                  )}
                >
                  {r.anzahl}
                </span>
              </Link>
            );
          })}
        </div>

        <Card className="overflow-hidden">
          <TicketList
            tickets={liste.items}
            variant="einfach"
            emptyTitle={
              ansicht === "offen"
                ? "Alles erledigt"
                : "Noch nichts abgeschlossen"
            }
            emptyHint={
              ansicht === "offen"
                ? "Neue Meldungen an Ihren Bereich erscheinen hier automatisch – aus dem Portal und aus eingehenden E-Mails."
                : "Abgeschlossene Vorgänge sammeln sich hier."
            }
          />
        </Card>

        {liste.pageCount > 1 && (
          <nav
            aria-label="Seiten"
            className="mt-4 flex items-center justify-center gap-3 text-sm"
          >
            {seite > 1 && (
              <Link
                href={`?ansicht=${ansicht}&seite=${seite - 1}`}
                className="font-medium text-primary hover:underline"
              >
                Zurück
              </Link>
            )}
            <span className="text-fg-muted">
              Seite {seite} von {liste.pageCount}
            </span>
            {seite < liste.pageCount && (
              <Link
                href={`?ansicht=${ansicht}&seite=${seite + 1}`}
                className="font-medium text-primary hover:underline"
              >
                Weiter
              </Link>
            )}
          </nav>
        )}
      </div>
    </>
  );
}
