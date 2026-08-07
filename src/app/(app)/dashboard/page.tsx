import type { Metadata } from "next";
import Link from "next/link";
import { redirect } from "next/navigation";
import { TeamMode } from "@prisma/client";
import {
  AlertTriangle,
  ArrowRight,
  CheckCircle2,
  Inbox,
  ListChecks,
} from "lucide-react";

import { teamIcon } from "@/components/layout/icon";
import { PageHeader } from "@/components/layout/page-header";
import { TicketList } from "@/components/ticket/ticket-list";
import { Card, CardContent } from "@/components/ui/card";
import { db } from "@/server/db";
import { agentTicketFilter, getActor, isAgent } from "@/server/policy";
import { buildNavigation } from "@/server/navigation";
import { findTickets } from "@/server/services/ticket-queries";
import { STATUS_ERLEDIGT, STATUS_OFFEN } from "@/lib/domain";
import { cn } from "@/lib/utils";

export const metadata: Metadata = { title: "Übersicht" };

/**
 * Übersicht für Bearbeitende.
 *
 * Beantwortet die drei Fragen, mit denen man morgens an den Rechner geht:
 * Was liegt bei mir, was wartet in meinen Eingängen auf Übernahme, und ist
 * etwas überfällig.
 */
export default async function DashboardPage() {
  const actor = await getActor();
  if (!actor) redirect("/anmelden");
  if (!isAgent(actor)) redirect("/meine-tickets");

  const navigation = await buildNavigation(actor);
  const alleEingaenge = actor.queues.map((q) => q.id);
  const visibility = agentTicketFilter(actor);

  const [meine, ohneZuweisung, ueberfaellig, heuteErledigt] = await Promise.all([
    findTickets({
      actor,
      visibility: { assigneeId: actor.id },
      filter: { status: STATUS_OFFEN },
      sort: "frist",
      perPage: 10,
    }),
    findTickets({
      actor,
      visibility,
      filter: { assigneeId: "none", status: STATUS_OFFEN },
      sort: "aelteste",
      perPage: 10,
    }),
    findTickets({
      actor,
      visibility,
      filter: { nurUeberfaellig: true },
      sort: "frist",
      perPage: 5,
    }),
    alleEingaenge.length
      ? db.ticket.count({
          where: {
            queueId: { in: alleEingaenge },
            status: { in: STATUS_ERLEDIGT },
            resolvedAt: { gte: new Date(new Date().setHours(0, 0, 0, 0)) },
          },
        })
      : Promise.resolve(0),
  ]);

  const vorname = actor.name.split(" ")[0];
  const stunde = new Date().getHours();
  const gruss =
    stunde < 11 ? "Guten Morgen" : stunde < 18 ? "Guten Tag" : "Guten Abend";

  return (
    <>
      <PageHeader
        title={`${gruss}, ${vorname}`}
        description={
          meine.total === 0 && ohneZuweisung.total === 0
            ? "In Ihren Eingängen liegt gerade nichts an."
            : `${meine.total} Vorgänge bei Ihnen, ${ohneZuweisung.total} warten auf Übernahme.`
        }
      />

      <div className="space-y-6 px-4 py-5 sm:px-6">
        {/* --- Kennzahlen ---------------------------------------------- */}
        <div className="grid gap-3 sm:grid-cols-2 xl:grid-cols-4">
          <Kennzahl
            label="Bei mir offen"
            wert={meine.total}
            icon={ListChecks}
            href="/meine-tickets-bearbeitung"
            tone="primary"
          />
          <Kennzahl
            label="Warten auf Übernahme"
            wert={ohneZuweisung.total}
            icon={Inbox}
            href={
              actor.defaultQueueId ? `/eingang/${actor.defaultQueueId}` : undefined
            }
            tone={ohneZuweisung.total > 0 ? "warning" : "neutral"}
          />
          <Kennzahl
            label="Frist überschritten"
            wert={ueberfaellig.total}
            icon={AlertTriangle}
            href="/tickets?ueberfaellig=1"
            tone={ueberfaellig.total > 0 ? "danger" : "success"}
          />
          <Kennzahl
            label="Heute erledigt"
            wert={heuteErledigt}
            icon={CheckCircle2}
            tone="success"
          />
        </div>

        {/* --- Überfällige zuerst -------------------------------------- */}
        {ueberfaellig.items.length > 0 && (
          <Abschnitt
            titel="Frist überschritten"
            hinweis="Diese Vorgänge haben ihre vereinbarte Lösungsfrist überschritten."
            href="/tickets?ueberfaellig=1"
            dringend
          >
            <TicketList
              tickets={ueberfaellig.items}
              showQueue={actor.queues.length > 1}
            />
          </Abschnitt>
        )}

        <div className="grid gap-6 xl:grid-cols-2">
          <Abschnitt
            titel="Bei mir"
            hinweis="Nach Frist sortiert – das Dringlichste zuerst."
            href="/meine-tickets-bearbeitung"
          >
            <TicketList
              tickets={meine.items}
              variant="kompakt"
              showQueue={actor.queues.length > 1}
              emptyTitle="Nichts bei Ihnen"
              emptyHint="Ziehen Sie sich einen Vorgang aus einem Sammeleingang."
            />
          </Abschnitt>

          <Abschnitt
            titel="Warten auf Übernahme"
            hinweis="Am längsten Liegendes zuerst."
            href={
              actor.defaultQueueId ? `/eingang/${actor.defaultQueueId}` : undefined
            }
          >
            <TicketList
              tickets={ohneZuweisung.items}
              variant="kompakt"
              showQueue={actor.queues.length > 1}
              emptyTitle="Alles verteilt"
              emptyHint="In Ihren Eingängen liegt nichts ohne Zuweisung."
            />
          </Abschnitt>
        </div>

        {/* --- Eingänge im Überblick ----------------------------------- */}
        {navigation.teams.length > 0 && (
          <section>
            <h2 className="mb-3 text-sm font-semibold text-fg">Ihre Eingänge</h2>
            <div className="grid gap-3 sm:grid-cols-2 lg:grid-cols-3">
              {navigation.teams.flatMap((team) =>
                team.queues.map((queue) => {
                  const Icon = teamIcon(team.icon);
                  const einfach = team.mode === TeamMode.EINFACH;
                  return (
                    <Link
                      key={queue.id}
                      href={
                        einfach
                          ? `/unsere-tickets/${queue.id}`
                          : `/eingang/${queue.id}`
                      }
                      className="group flex items-start gap-3 rounded-lg border border-line bg-surface p-4 shadow-card transition-colors hover:bg-surface-2"
                    >
                      <span
                        className="flex size-9 shrink-0 items-center justify-center rounded-md"
                        style={{
                          backgroundColor: `${team.color}1a`,
                          color: team.color,
                        }}
                      >
                        <Icon className="size-4.5" aria-hidden="true" />
                      </span>

                      <span className="min-w-0 flex-1">
                        <span className="flex items-center gap-1.5">
                          <span className="truncate text-sm font-medium text-fg">
                            {queue.siteName ?? queue.name}
                          </span>
                          {einfach && (
                            <span className="shrink-0 rounded bg-surface-2 px-1.5 text-[0.625rem] text-fg-subtle">
                              einfach
                            </span>
                          )}
                        </span>
                        {/* Bei Bereichen mit einem einzigen Eingang heißen
                            Eingang und Bereich gleich – dann entfällt die
                            Wiederholung. */}
                        {(queue.siteName ?? queue.name) !== team.name && (
                          <span className="mt-0.5 block text-xs text-fg-muted">
                            {team.name}
                          </span>
                        )}
                        <span className="mt-1.5 flex flex-wrap items-center gap-x-2 text-xs">
                          <span className="text-fg-muted">
                            {queue.openCount} offen
                          </span>
                          {!einfach && queue.unassignedCount > 0 && (
                            <span className="font-medium text-primary">
                              {queue.unassignedCount} ohne Zuweisung
                            </span>
                          )}
                        </span>
                      </span>

                      <ArrowRight
                        className="mt-1 size-4 shrink-0 text-fg-subtle transition-transform group-hover:translate-x-0.5"
                        aria-hidden="true"
                      />
                    </Link>
                  );
                }),
              )}
            </div>
          </section>
        )}
      </div>
    </>
  );
}

function Kennzahl({
  label,
  wert,
  icon: Icon,
  href,
  tone,
}: {
  label: string;
  wert: number;
  icon: React.ComponentType<{ className?: string; "aria-hidden"?: boolean }>;
  href?: string;
  tone: "primary" | "warning" | "danger" | "success" | "neutral";
}) {
  const toene = {
    primary: "bg-primary-soft text-primary-soft-fg",
    warning: "bg-warning-soft text-warning-soft-fg",
    danger: "bg-danger-soft text-danger-soft-fg",
    success: "bg-success-soft text-success-soft-fg",
    neutral: "bg-surface-2 text-fg-muted",
  };

  const inhalt = (
    <CardContent className="flex items-center gap-3 py-4">
      <span
        className={cn(
          "flex size-10 shrink-0 items-center justify-center rounded-lg",
          toene[tone],
        )}
      >
        <Icon className="size-5" aria-hidden={true} />
      </span>
      <span className="min-w-0">
        <span className="block text-2xl font-semibold tabular-nums text-fg">
          {wert}
        </span>
        <span className="block text-xs text-fg-muted">{label}</span>
      </span>
    </CardContent>
  );

  if (!href) return <Card>{inhalt}</Card>;

  return (
    <Card className="transition-colors hover:bg-surface-2">
      <Link href={href} className="block">
        {inhalt}
      </Link>
    </Card>
  );
}

function Abschnitt({
  titel,
  hinweis,
  href,
  dringend,
  children,
}: {
  titel: string;
  hinweis?: string;
  href?: string;
  dringend?: boolean;
  children: React.ReactNode;
}) {
  return (
    <section>
      <div className="mb-3 flex flex-wrap items-baseline justify-between gap-2">
        <div>
          <h2
            className={cn(
              "text-sm font-semibold",
              dringend ? "text-danger" : "text-fg",
            )}
          >
            {titel}
          </h2>
          {hinweis && <p className="text-xs text-fg-muted">{hinweis}</p>}
        </div>
        {href && (
          <Link
            href={href}
            className="text-xs font-medium text-primary hover:underline"
          >
            Alle ansehen
          </Link>
        )}
      </div>
      <Card
        className={cn("overflow-hidden", dringend && "border-danger/30")}
      >
        {children}
      </Card>
    </section>
  );
}
