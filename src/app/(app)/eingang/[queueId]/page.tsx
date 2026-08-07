import type { Metadata } from "next";
import Link from "next/link";
import { notFound, redirect } from "next/navigation";
import { TeamMode } from "@prisma/client";
import { Inbox, ListChecks, MapPin } from "lucide-react";

import { PageHeader } from "@/components/layout/page-header";
import { StandortUmschalter } from "@/components/layout/standort-umschalter";
import { TicketList } from "@/components/ticket/ticket-list";
import { Button } from "@/components/ui/button";
import { Card } from "@/components/ui/card";
import { db } from "@/server/db";
import {
  agentTicketFilter,
  getActor,
  queuesOfTeam,
  requireQueueAccess,
} from "@/server/policy";
import { findTickets } from "@/server/services/ticket-queries";
import { STATUS_OFFEN } from "@/lib/domain";

export const metadata: Metadata = { title: "Sammeleingang" };

/**
 * Sammeleingang eines Voll-Bereichs.
 *
 * Zeigt, was noch niemand übernommen hat – das ist die eigentliche Frage beim
 * Verteilen. Wer sich etwas zieht, sieht es danach unter „Meine Vorgänge".
 *
 * Über den Standort-Umschalter lassen sich weitere freigegebene Eingänge
 * desselben Bereichs dazuschalten. Wer zwei Häuser betreut, arbeitet damit in
 * einer Liste statt in zweien.
 */
export default async function EingangPage({
  params,
  searchParams,
}: {
  params: Promise<{ queueId: string }>;
  searchParams: Promise<{ standorte?: string; seite?: string }>;
}) {
  const actor = await getActor();
  if (!actor) redirect("/anmelden");

  const { queueId } = await params;
  const sp = await searchParams;

  // Wirft, wenn der Eingang nicht freigegeben ist – ein Hausmeister aus
  // Musterstadt kommt so auch über die Adresszeile nicht an die Vorgänge der
  // Schule.
  requireQueueAccess(actor, queueId);

  const queue = await db.queue.findUnique({
    where: { id: queueId },
    select: {
      id: true,
      name: true,
      inboundEmail: true,
      site: { select: { id: true, name: true, shortName: true } },
      team: {
        select: { id: true, key: true, name: true, mode: true, color: true },
      },
    },
  });
  if (!queue) notFound();

  // Die schlanke Ausbaustufe hat keinen Sammeleingang; dort führt der Weg auf
  // die eine Liste, die es gibt.
  if (queue.team.mode === TeamMode.EINFACH) {
    redirect(`/unsere-tickets/${queueId}`);
  }

  // Zusätzlich gewählte Eingänge desselben Bereichs.
  const weitereIds = (sp.standorte ?? "")
    .split(",")
    .map((s) => s.trim())
    .filter(Boolean);

  const bereichsEingaenge = queuesOfTeam(actor, queue.team.id);
  const gewaehlt = [
    queueId,
    ...weitereIds.filter((id) => bereichsEingaenge.some((q) => q.id === id)),
  ];

  const seite = Number(sp.seite) || 1;

  const [ohneZuweisung, inArbeit] = await Promise.all([
    findTickets({
      actor,
      visibility: agentTicketFilter(actor, gewaehlt),
      filter: { assigneeId: "none", status: STATUS_OFFEN },
      sort: "frist",
      page: seite,
      perPage: 50,
    }),
    findTickets({
      actor,
      visibility: agentTicketFilter(actor, gewaehlt),
      filter: { status: STATUS_OFFEN },
      sort: "aktivitaet",
      perPage: 1,
    }).then((r) => r.total),
  ]);

  const mehrereEingaenge = bereichsEingaenge.length > 1;

  return (
    <>
      <PageHeader
        title={queue.site ? `${queue.team.name} – ${queue.site.name}` : queue.name}
        description={
          <span className="flex flex-wrap items-center gap-x-3 gap-y-1">
            <span>
              <strong className="font-semibold text-fg">
                {ohneZuweisung.total}
              </strong>{" "}
              {ohneZuweisung.total === 1 ? "Vorgang wartet" : "Vorgänge warten"} auf
              Übernahme
            </span>
            <span aria-hidden="true">·</span>
            <span>{inArbeit} offen insgesamt</span>
            {queue.inboundEmail && (
              <>
                <span aria-hidden="true">·</span>
                <span className="font-mono text-xs">{queue.inboundEmail}</span>
              </>
            )}
          </span>
        }
        actions={
          <>
            {mehrereEingaenge && (
              <StandortUmschalter
                aktueller={queueId}
                eingaenge={bereichsEingaenge}
                gewaehlt={gewaehlt}
              />
            )}
            <Button asChild variant="secondary" size="sm">
              <Link href={`/tickets?eingang=${gewaehlt.join(",")}`}>
                <ListChecks aria-hidden="true" />
                Alle Vorgänge
              </Link>
            </Button>
          </>
        }
      />

      <div className="px-4 py-5 sm:px-6">
        {gewaehlt.length > 1 && (
          <p className="mb-3 flex items-center gap-1.5 text-sm text-fg-muted">
            <MapPin className="size-4" aria-hidden="true" />
            Zusammengefasst über {gewaehlt.length} Standorte.
          </p>
        )}

        <Card className="overflow-hidden">
          <div className="flex items-center gap-2 border-b border-line bg-surface-2 px-4 py-2.5">
            <Inbox className="size-4 text-fg-subtle" aria-hidden="true" />
            <h2 className="text-sm font-semibold text-fg">
              Wartet auf Übernahme
            </h2>
          </div>

          <TicketList
            tickets={ohneZuweisung.items}
            showQueue={gewaehlt.length > 1}
            showAssignee={false}
            emptyTitle="Nichts liegen geblieben"
            emptyHint="Alle Vorgänge in diesem Eingang sind jemandem zugewiesen. Neue Meldungen erscheinen hier automatisch."
          />
        </Card>

        {ohneZuweisung.pageCount > 1 && (
          <nav
            aria-label="Seiten"
            className="mt-4 flex items-center justify-center gap-2"
          >
            {seite > 1 && (
              <Button asChild variant="secondary" size="sm">
                <Link href={`?seite=${seite - 1}`}>Zurück</Link>
              </Button>
            )}
            <span className="text-sm text-fg-muted">
              Seite {seite} von {ohneZuweisung.pageCount}
            </span>
            {seite < ohneZuweisung.pageCount && (
              <Button asChild variant="secondary" size="sm">
                <Link href={`?seite=${seite + 1}`}>Weiter</Link>
              </Button>
            )}
          </nav>
        )}
      </div>
    </>
  );
}
