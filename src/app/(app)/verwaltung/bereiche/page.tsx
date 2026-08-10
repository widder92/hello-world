import type { Metadata } from "next";
import { QueueMode, TeamMode } from "@prisma/client";
import { Boxes, EyeOff, Inbox, MapPin } from "lucide-react";

import { teamIcon } from "@/components/layout/icon";
import { PageHeader } from "@/components/layout/page-header";
import { BereichDialog } from "@/components/verwaltung/bereich-dialog";
import { Badge } from "@/components/ui/badge";
import { Card } from "@/components/ui/card";
import { db } from "@/server/db";
import { requireAdmin } from "@/server/policy";
import { STATUS_OFFEN } from "@/lib/domain";

export const metadata: Metadata = { title: "Bereiche" };

/**
 * Bereichsverwaltung.
 *
 * Die zentrale Stellschraube des Systems: Hier wird entschieden, welcher
 * Bereich den vollen Arbeitsplatz bekommt und welcher die schlanke Ansicht,
 * und ob die Eingänge nach Standorten getrennt sind.
 */
export default async function BereichePage() {
  await requireAdmin();

  const [bereiche, standorte] = await Promise.all([
    db.team.findMany({
      orderBy: [{ sortOrder: "asc" }, { name: "asc" }],
      include: {
        queues: {
          orderBy: { sortOrder: "asc" },
          select: {
            id: true,
            name: true,
            isActive: true,
            inboundEmail: true,
            site: { select: { name: true, shortName: true } },
          },
        },
        _count: { select: { memberships: true, tickets: true } },
      },
    }),
    db.site.count({ where: { isActive: true } }),
  ]);

  // Offene Vorgänge je Bereich – zeigt, wo tatsächlich gearbeitet wird.
  const offen = await db.ticket.groupBy({
    by: ["teamId"],
    where: { status: { in: STATUS_OFFEN } },
    _count: { _all: true },
  });
  const offenMap = new Map(offen.map((o) => [o.teamId, o._count._all]));

  return (
    <>
      <PageHeader
        title="Bereiche"
        description={
          <>
            Ein Bereich bündelt Zuständigkeit, Rechte und Auswertung. Seine{" "}
            <strong className="font-medium text-fg">Ausbaustufe</strong> bestimmt,
            wie viel Oberfläche die Mitglieder sehen, seine{" "}
            <strong className="font-medium text-fg">Eingangsstruktur</strong>,
            ob Meldungen nach Standorten getrennt ankommen.
          </>
        }
        actions={<BereichDialog standortAnzahl={standorte} />}
      />

      <div className="space-y-3 px-4 py-5 sm:px-6">
        {bereiche.map((b) => {
          const Icon = teamIcon(b.icon);
          const voll = b.mode === TeamMode.VOLL;
          const jeStandort = b.queueMode === QueueMode.PER_SITE;
          const aktiveEingaenge = b.queues.filter((q) => q.isActive);

          return (
            <Card key={b.id} className={b.isActive ? undefined : "opacity-60"}>
              <div className="flex flex-wrap items-start gap-4 px-5 py-4">
                <span
                  className="flex size-11 shrink-0 items-center justify-center rounded-lg"
                  style={{ backgroundColor: `${b.color}1a`, color: b.color }}
                >
                  <Icon className="size-5" aria-hidden="true" />
                </span>

                <div className="min-w-0 flex-1">
                  <div className="flex flex-wrap items-center gap-2">
                    <h2 className="text-base font-semibold text-fg">{b.name}</h2>
                    <span className="rounded bg-surface-2 px-1.5 py-0.5 font-mono text-xs text-fg-muted">
                      {b.key}
                    </span>

                    <Badge tone={voll ? "primary" : "neutral"}>
                      {voll ? "Voller Arbeitsplatz" : "Schlanke Ansicht"}
                    </Badge>

                    <Badge tone="outline">
                      {jeStandort ? (
                        <>
                          <MapPin aria-hidden="true" />
                          Eingang je Standort
                        </>
                      ) : (
                        <>
                          <Inbox aria-hidden="true" />
                          Ein Eingang
                        </>
                      )}
                    </Badge>

                    {!b.portalVisible && (
                      <Badge
                        tone="warning"
                        title="Nimmt keine Meldungen über das Portal entgegen – erreichbar nur per Weiterleitung."
                      >
                        <EyeOff aria-hidden="true" />
                        Nicht im Portal
                      </Badge>
                    )}

                    {b.hasInventory && (
                      <Badge
                        tone="outline"
                        title="Dieser Bereich verwaltet eigene Geräte unter /inventar."
                      >
                        <Boxes aria-hidden="true" />
                        Führt ein Inventar
                      </Badge>
                    )}

                    {!b.isActive && <Badge tone="danger">Stillgelegt</Badge>}
                  </div>

                  {b.description && (
                    <p className="mt-1 text-sm text-fg-muted">{b.description}</p>
                  )}

                  {b.portalVisible && b.portalLabel && (
                    <p className="mt-1 text-sm text-fg-subtle">
                      Im Portal beschriftet als: „{b.portalLabel}“
                    </p>
                  )}

                  <div className="mt-2.5 flex flex-wrap items-center gap-x-4 gap-y-1 text-xs text-fg-muted">
                    <span>
                      {b._count.memberships}{" "}
                      {b._count.memberships === 1 ? "Mitglied" : "Mitglieder"}
                    </span>
                    <span>
                      {aktiveEingaenge.length}{" "}
                      {aktiveEingaenge.length === 1 ? "Eingang" : "Eingänge"}
                    </span>
                    <span>{offenMap.get(b.id) ?? 0} offen</span>
                    <span>{b._count.tickets} insgesamt</span>
                  </div>

                  {aktiveEingaenge.length > 0 && (
                    <ul className="mt-2.5 flex flex-wrap gap-1.5">
                      {aktiveEingaenge.map((q) => (
                        <li
                          key={q.id}
                          className="rounded-md border border-line bg-surface-2 px-2 py-1 text-xs"
                        >
                          <span className="text-fg">
                            {q.site
                              ? (q.site.shortName ?? q.site.name)
                              : q.name}
                          </span>
                          {q.inboundEmail && (
                            <span className="ml-1.5 font-mono text-fg-subtle">
                              {q.inboundEmail}
                            </span>
                          )}
                        </li>
                      ))}
                    </ul>
                  )}
                </div>

                <BereichDialog
                  standortAnzahl={standorte}
                  bereich={{
                    id: b.id,
                    key: b.key,
                    name: b.name,
                    mode: b.mode,
                    queueMode: b.queueMode,
                    description: b.description,
                    color: b.color,
                    icon: b.icon,
                    portalVisible: b.portalVisible,
                    portalLabel: b.portalLabel,
                    portalDescription: b.portalDescription,
                    allowEmailCommands: b.allowEmailCommands,
                    hasInventory: b.hasInventory,
                    notifyRequesterOnTransfer: b.notifyRequesterOnTransfer,
                    isActive: b.isActive,
                  }}
                  offeneVorgaenge={offenMap.get(b.id) ?? 0}
                />
              </div>
            </Card>
          );
        })}
      </div>
    </>
  );
}
