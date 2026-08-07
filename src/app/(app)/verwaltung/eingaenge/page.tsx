import type { Metadata } from "next";
import { QueueMode } from "@prisma/client";
import { Inbox, MapPin } from "lucide-react";

import { teamIcon } from "@/components/layout/icon";
import { PageHeader } from "@/components/layout/page-header";
import { EingangDialog } from "@/components/verwaltung/eingang-dialog";
import { Badge } from "@/components/ui/badge";
import { Card } from "@/components/ui/card";
import { db } from "@/server/db";
import { requireAdmin } from "@/server/policy";
import { STATUS_OFFEN } from "@/lib/domain";

export const metadata: Metadata = { title: "Eingänge" };

/**
 * Eingänge.
 *
 * Der Eingang ist der Ort, an dem ein Vorgang tatsächlich landet. Hier bekommt
 * er seine Empfangsadresse – und damit die direkte Zustellung per Mail.
 *
 * Eingänge werden nicht hier angelegt, sondern ergeben sich aus der
 * Eingangsstruktur des Bereichs. Das verhindert, dass ein Bereich mit
 * Standort-Eingängen versehentlich einen zweiten gemeinsamen Eingang bekommt,
 * an dem dann Vorgänge unbemerkt liegen bleiben.
 */
export default async function EingaengePage() {
  await requireAdmin();

  const bereiche = await db.team.findMany({
    orderBy: [{ sortOrder: "asc" }, { name: "asc" }],
    select: {
      id: true,
      name: true,
      color: true,
      icon: true,
      queueMode: true,
      queues: {
        orderBy: { sortOrder: "asc" },
        select: {
          id: true,
          name: true,
          inboundEmail: true,
          portalVisible: true,
          isActive: true,
          site: { select: { name: true, shortName: true } },
          mailAccount: { select: { id: true, isActive: true } },
          _count: { select: { access: true } },
        },
      },
    },
  });

  const offen = await db.ticket.groupBy({
    by: ["queueId"],
    where: { status: { in: STATUS_OFFEN } },
    _count: { _all: true },
  });
  const offenMap = new Map(offen.map((o) => [o.queueId, o._count._all]));

  return (
    <>
      <PageHeader
        title="Eingänge"
        description="Jeder Eingang kann eine eigene Empfangsadresse haben. Eine Mail dorthin wird unmittelbar zum Vorgang in diesem Eingang – ohne Umweg über einen anderen Bereich."
      />

      <div className="space-y-5 px-4 py-5 sm:px-6">
        {bereiche.map((b) => {
          const Icon = teamIcon(b.icon);
          const jeStandort = b.queueMode === QueueMode.PER_SITE;

          return (
            <section key={b.id}>
              <h2 className="mb-2 flex items-center gap-2 text-sm font-semibold text-fg">
                <span
                  className="flex size-6 items-center justify-center rounded"
                  style={{ backgroundColor: `${b.color}1a`, color: b.color }}
                >
                  <Icon className="size-3.5" aria-hidden="true" />
                </span>
                {b.name}
                <Badge tone="outline" size="sm">
                  {jeStandort ? (
                    <>
                      <MapPin aria-hidden="true" />
                      je Standort
                    </>
                  ) : (
                    <>
                      <Inbox aria-hidden="true" />
                      ein Eingang
                    </>
                  )}
                </Badge>
              </h2>

              <Card className="divide-y divide-line">
                {b.queues.map((q) => (
                  <div
                    key={q.id}
                    className={`flex flex-wrap items-center gap-3 px-5 py-3 ${q.isActive ? "" : "opacity-55"}`}
                  >
                    <div className="min-w-0 flex-1">
                      <div className="flex flex-wrap items-center gap-2">
                        <span className="font-medium text-fg">
                          {q.site ? (q.site.shortName ?? q.site.name) : q.name}
                        </span>
                        {!q.isActive && (
                          <Badge
                            tone="neutral"
                            size="sm"
                            title="Stillgelegt. Bestehende Vorgänge bleiben erreichbar, neue kommen hier nicht mehr an."
                          >
                            Stillgelegt
                          </Badge>
                        )}
                        {!q.portalVisible && (
                          <Badge tone="warning" size="sm">
                            Nicht im Portal
                          </Badge>
                        )}
                        {q.mailAccount && (
                          <Badge
                            tone={q.mailAccount.isActive ? "success" : "neutral"}
                            size="sm"
                          >
                            Postfach{q.mailAccount.isActive ? "" : " (aus)"}
                          </Badge>
                        )}
                      </div>

                      <p className="mt-0.5 flex flex-wrap items-center gap-x-3 text-xs text-fg-muted">
                        {q.inboundEmail ? (
                          <span className="font-mono">{q.inboundEmail}</span>
                        ) : (
                          <span className="italic text-fg-subtle">
                            keine Empfangsadresse
                          </span>
                        )}
                        <span>{offenMap.get(q.id) ?? 0} offen</span>
                        <span>
                          {q._count.access}{" "}
                          {q._count.access === 1 ? "Freigabe" : "Freigaben"}
                        </span>
                      </p>
                    </div>

                    <EingangDialog
                      eingang={{
                        id: q.id,
                        name: q.name,
                        inboundEmail: q.inboundEmail,
                        portalVisible: q.portalVisible,
                        isActive: q.isActive,
                      }}
                      bereichName={b.name}
                    />
                  </div>
                ))}

                {b.queues.length === 0 && (
                  <p className="px-5 py-6 text-center text-sm text-fg-muted">
                    Noch kein Eingang. Er entsteht, sobald die Eingangsstruktur
                    des Bereichs gespeichert wird.
                  </p>
                )}
              </Card>
            </section>
          );
        })}
      </div>
    </>
  );
}
