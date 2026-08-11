import type { Metadata } from "next";
import Link from "next/link";
import { redirect } from "next/navigation";
import { PenLine } from "lucide-react";

import { StatusBadge } from "@/components/ticket/badges";
import { Button } from "@/components/ui/button";
import { Card } from "@/components/ui/card";
import { db } from "@/server/db";
import { getActor } from "@/server/policy";
import { STATUS_ERLEDIGT, STATUS_OFFEN } from "@/lib/domain";
import { formatCompact } from "@/lib/format";
import { cn } from "@/lib/utils";

export const metadata: Metadata = { title: "Meine Meldungen" };

/**
 * Eigene Meldungen im Portal.
 *
 * Zeigt auch Vorgänge, bei denen man auf Kopie steht – und Vorgänge, die aus
 * einer eigenen Meldung abgespalten wurden. Wer eine Meldung geschrieben hat,
 * soll beide Teile verfolgen können, ohne zu verstehen, wie die Bereiche das
 * intern aufgeteilt haben.
 */
export default async function MeineMeldungenPage({
  searchParams,
}: {
  searchParams: Promise<{ ansicht?: string }>;
}) {
  const actor = await getActor();
  if (!actor) redirect("/anmelden");

  const sp = await searchParams;
  const ansicht = sp.ansicht === "erledigt" ? "erledigt" : "offen";
  const einfach = Boolean(actor.a11yPrefs.simpleMode);

  const basis = {
    OR: [
      { requesterId: actor.id },
      { watchers: { some: { userId: actor.id } } },
    ],
  };

  const [tickets, zaehlerOffen, zaehlerErledigt] = await Promise.all([
    db.ticket.findMany({
      where: {
        AND: [
          basis,
          {
            status: {
              in: ansicht === "offen" ? STATUS_OFFEN : STATUS_ERLEDIGT,
            },
          },
        ],
      },
      orderBy: { lastActivityAt: "desc" },
      take: 60,
      select: {
        id: true,
        number: true,
        subject: true,
        status: true,
        lastActivityAt: true,
        createdAt: true,
        requesterId: true,
        team: { select: { name: true, color: true } },
        site: { select: { name: true, shortName: true } },
        linksTo: {
          where: { type: "SPLIT_FROM" },
          select: { from: { select: { number: true } } },
        },
      },
    }),
    db.ticket.count({
      where: { AND: [basis, { status: { in: STATUS_OFFEN } }] },
    }),
    db.ticket.count({
      where: { AND: [basis, { status: { in: STATUS_ERLEDIGT } }] },
    }),
  ]);

  return (
    <div>
      <div className="mb-5 flex flex-wrap items-end justify-between gap-3">
        <div>
          <h1
            className={cn(
              "font-semibold tracking-tight text-fg",
              einfach ? "text-3xl" : "text-2xl",
            )}
          >
            Meine Meldungen
          </h1>
          <p className={cn("mt-1 text-fg-muted", einfach ? "text-lg" : "text-sm")}>
            Hier sehen Sie, was aus Ihren Meldungen geworden ist.
          </p>
        </div>

        <Button asChild size={einfach ? "xl" : "lg"}>
          <Link href="/neu">
            <PenLine aria-hidden="true" />
            Neue Meldung
          </Link>
        </Button>
      </div>

      <div
        role="tablist"
        aria-label="Ansicht"
        className="mb-4 inline-flex items-center gap-1 rounded-lg bg-surface-2 p-1"
      >
        {(
          [
            ["offen", "Läuft noch", zaehlerOffen],
            ["erledigt", "Erledigt", zaehlerErledigt],
          ] as const
        ).map(([wert, label, anzahl]) => {
          const aktiv = ansicht === wert;
          return (
            <Link
              key={wert}
              href={`?ansicht=${wert}`}
              role="tab"
              aria-selected={aktiv}
              className={cn(
                "inline-flex items-center gap-2 rounded-md px-4 py-2 font-medium transition-colors",
                einfach ? "text-base" : "text-sm",
                aktiv
                  ? "bg-surface text-fg shadow-card"
                  : "text-fg-muted hover:text-fg",
              )}
            >
              {label}
              <span className="text-xs tabular-nums text-fg-subtle">{anzahl}</span>
            </Link>
          );
        })}
      </div>

      {tickets.length === 0 ? (
        <Card>
          <div className="px-6 py-16 text-center">
            <p className={cn("font-medium text-fg", einfach && "text-lg")}>
              {ansicht === "offen"
                ? "Sie haben gerade keine offene Meldung."
                : "Noch nichts abgeschlossen."}
            </p>
            {ansicht === "offen" && (
              <p className="mt-1.5 text-sm text-fg-muted">
                Wenn etwas nicht funktioniert, schreiben Sie uns.
              </p>
            )}
          </div>
        </Card>
      ) : (
        <ul className="space-y-3">
          {tickets.map((t) => (
            <li key={t.id}>
              <Link
                href={`/tickets/${t.number}`}
                className="block rounded-lg border border-line bg-surface p-4 shadow-card transition-colors hover:bg-surface-2"
              >
                <div className="flex flex-wrap items-start justify-between gap-3">
                  <div className="min-w-0 flex-1">
                    <h2
                      className={cn(
                        "font-medium text-fg",
                        einfach ? "text-lg" : "text-base",
                      )}
                    >
                      {t.subject}
                    </h2>

                    <p className="mt-1 flex flex-wrap items-center gap-x-2 gap-y-0.5 text-sm text-fg-muted">
                      <span className="inline-flex items-center gap-1.5">
                        <span
                          className="size-2 rounded-full"
                          style={{ backgroundColor: t.team.color }}
                          aria-hidden="true"
                        />
                        {t.team.name}
                      </span>
                      {t.site && (
                        <>
                          <span aria-hidden="true">·</span>
                          <span>{t.site.shortName ?? t.site.name}</span>
                        </>
                      )}
                      <span aria-hidden="true">·</span>
                      <span className="font-mono text-xs">{t.number}</span>
                    </p>

                    {t.linksTo.length > 0 && (
                      <p className="mt-1.5 text-xs text-fg-subtle">
                        Dieser Vorgang gehört zu Ihrer Meldung{" "}
                        <span className="font-mono">
                          {t.linksTo[0].from.number}
                        </span>
                        .
                      </p>
                    )}

                    {t.requesterId !== actor.id && (
                      <p className="mt-1.5 text-xs text-fg-subtle">
                        Sie stehen bei dieser Meldung auf Kopie.
                      </p>
                    )}
                  </div>

                  <div className="flex shrink-0 flex-col items-end gap-1.5">
                    <StatusBadge
                      status={t.status}
                      einfach
                      size={einfach ? "lg" : "md"}
                    />
                    <time
                      dateTime={t.lastActivityAt.toISOString()}
                      className="text-xs text-fg-subtle"
                    >
                      {formatCompact(t.lastActivityAt)}
                    </time>
                  </div>
                </div>
              </Link>
            </li>
          ))}
        </ul>
      )}
    </div>
  );
}
