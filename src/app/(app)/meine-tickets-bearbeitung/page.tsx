import type { Metadata } from "next";
import { redirect } from "next/navigation";

import { PageHeader } from "@/components/layout/page-header";
import { TicketList } from "@/components/ticket/ticket-list";
import { Card } from "@/components/ui/card";
import { getActor, isAgent } from "@/server/policy";
import { findTickets } from "@/server/services/ticket-queries";
import { STATUS_ERLEDIGT, STATUS_OFFEN } from "@/lib/domain";

export const metadata: Metadata = { title: "Meine Vorgänge" };

/**
 * Vorgänge, die dieser Person zugewiesen sind – über alle Bereiche und
 * Eingänge hinweg. Wer in zwei Bereichen arbeitet, hat damit eine Liste statt
 * zweier.
 */
export default async function MeineVorgaengePage({
  searchParams,
}: {
  searchParams: Promise<{ ansicht?: string }>;
}) {
  const actor = await getActor();
  if (!actor) redirect("/anmelden");
  if (!isAgent(actor)) redirect("/meine-tickets");

  const sp = await searchParams;
  const erledigt = sp.ansicht === "erledigt";

  const liste = await findTickets({
    actor,
    visibility: { assigneeId: actor.id },
    filter: { status: erledigt ? STATUS_ERLEDIGT : STATUS_OFFEN },
    sort: erledigt ? "neueste" : "frist",
    perPage: 100,
  });

  return (
    <>
      <PageHeader
        title="Meine Vorgänge"
        description={
          erledigt
            ? "Von Ihnen abgeschlossene Vorgänge."
            : "Alles, was Ihnen zugewiesen ist – nach Frist sortiert."
        }
      />

      <div className="px-4 py-5 sm:px-6">
        <Card className="overflow-hidden">
          <TicketList
            tickets={liste.items}
            showQueue={actor.queues.length > 1}
            showAssignee={false}
            emptyTitle={erledigt ? "Noch nichts erledigt" : "Nichts bei Ihnen"}
            emptyHint={
              erledigt
                ? undefined
                : "Ziehen Sie sich einen Vorgang aus einem Sammeleingang oder warten Sie auf eine Zuweisung."
            }
          />
        </Card>
      </div>
    </>
  );
}
