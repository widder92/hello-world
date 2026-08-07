import type { Metadata } from "next";
import Link from "next/link";
import { redirect } from "next/navigation";
import { Priority } from "@prisma/client";

import { FilterLeiste } from "@/components/ticket/filter-leiste";
import { PageHeader } from "@/components/layout/page-header";
import { TicketList } from "@/components/ticket/ticket-list";
import { Card } from "@/components/ui/card";
import { db } from "@/server/db";
import { agentTicketFilter, getActor, isAgent } from "@/server/policy";
import {
  findTickets,
  type TicketSort,
} from "@/server/services/ticket-queries";
import { STATUS_ERLEDIGT, STATUS_OFFEN } from "@/lib/domain";
import { formatNumber } from "@/lib/format";

export const metadata: Metadata = { title: "Vorgänge" };

const SORTIERUNGEN: TicketSort[] = [
  "aktivitaet",
  "neueste",
  "aelteste",
  "prioritaet",
  "frist",
];

/**
 * Vollständige Ticketliste mit Filtern.
 *
 * Nur für Bereiche der vollen Ausbaustufe gedacht – im Einfach-Modus führt der
 * Weg auf "Unsere Tickets". Der Sichtbarkeitsfilter greift trotzdem immer:
 * Auch wer die Adresse von Hand eintippt, sieht nur seine Eingänge.
 */
export default async function TicketlistePage({
  searchParams,
}: {
  searchParams: Promise<Record<string, string | undefined>>;
}) {
  const actor = await getActor();
  if (!actor) redirect("/anmelden");
  if (!isAgent(actor)) redirect("/meine-tickets");

  const sp = await searchParams;
  const seite = Number(sp.seite) || 1;

  const gewaehlteEingaenge = (sp.eingang ?? "")
    .split(",")
    .map((s) => s.trim())
    .filter(Boolean);

  const statusGruppe =
    sp.status === "erledigt" ? "erledigt" : sp.status === "alle" ? "alle" : "offen";

  const sortierung = (
    SORTIERUNGEN.includes(sp.sortierung as TicketSort)
      ? sp.sortierung
      : "aktivitaet"
  ) as TicketSort;

  const [liste, bereiche, standorte] = await Promise.all([
    findTickets({
      actor,
      visibility: agentTicketFilter(
        actor,
        gewaehlteEingaenge.length ? gewaehlteEingaenge : undefined,
      ),
      filter: {
        teamIds: sp.bereich ? [sp.bereich] : undefined,
        statusGruppe,
        status:
          statusGruppe === "offen"
            ? STATUS_OFFEN
            : statusGruppe === "erledigt"
              ? STATUS_ERLEDIGT
              : undefined,
        priority: sp.prioritaet ? [sp.prioritaet as Priority] : undefined,
        assigneeId:
          sp.zuweisung === "meine"
            ? "me"
            : sp.zuweisung === "offen"
              ? "none"
              : undefined,
        siteId: sp.standort || undefined,
        nurUeberfaellig: sp.ueberfaellig === "1",
        suche: sp.q || undefined,
      },
      sort: sortierung,
      page: seite,
      perPage: 50,
    }),
    db.team.findMany({
      where: { id: { in: actor.memberships.map((m) => m.teamId) } },
      select: { id: true, name: true, color: true },
      orderBy: { sortOrder: "asc" },
    }),
    db.site.findMany({
      where: { isActive: true },
      select: { id: true, name: true },
      orderBy: [{ sortOrder: "asc" }, { name: "asc" }],
    }),
  ]);

  return (
    <>
      <PageHeader
        title="Vorgänge"
        description={`${formatNumber(liste.total)} ${liste.total === 1 ? "Vorgang" : "Vorgänge"} gefunden`}
      >
        <FilterLeiste
          bereiche={bereiche}
          standorte={standorte}
          eingaenge={actor.queues}
        />
      </PageHeader>

      <div className="px-4 py-5 sm:px-6">
        <Card className="overflow-hidden">
          <TicketList
            tickets={liste.items}
            showQueue
            emptyTitle="Keine Treffer"
            emptyHint="Passen Sie die Filter an oder suchen Sie mit einem anderen Begriff."
          />
        </Card>

        {liste.pageCount > 1 && (
          <Blaetterung
            seite={seite}
            seiten={liste.pageCount}
            params={sp}
          />
        )}
      </div>
    </>
  );
}

function Blaetterung({
  seite,
  seiten,
  params,
}: {
  seite: number;
  seiten: number;
  params: Record<string, string | undefined>;
}) {
  function href(ziel: number) {
    const p = new URLSearchParams();
    for (const [k, v] of Object.entries(params)) {
      if (v && k !== "seite") p.set(k, v);
    }
    p.set("seite", String(ziel));
    return `?${p.toString()}`;
  }

  return (
    <nav
      aria-label="Seiten"
      className="mt-4 flex items-center justify-center gap-3 text-sm"
    >
      {seite > 1 && (
        <Link href={href(seite - 1)} className="font-medium text-primary hover:underline">
          Zurück
        </Link>
      )}
      <span className="text-fg-muted">
        Seite {seite} von {seiten}
      </span>
      {seite < seiten && (
        <Link href={href(seite + 1)} className="font-medium text-primary hover:underline">
          Weiter
        </Link>
      )}
    </nav>
  );
}
