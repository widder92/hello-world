import type { Metadata } from "next";
import Link from "next/link";
import { Search, ShieldAlert } from "lucide-react";

import { PageHeader } from "@/components/layout/page-header";
import { Avatar } from "@/components/ui/avatar";
import { Badge } from "@/components/ui/badge";
import { Card } from "@/components/ui/card";
import { Input, Select } from "@/components/ui/input";
import { db } from "@/server/db";
import { requireAdmin } from "@/server/policy";
import { describeEvent } from "@/lib/domain";
import { formatDateTime } from "@/lib/format";

export const metadata: Metadata = { title: "Protokoll" };

/**
 * Protokoll.
 *
 * Zwei Quellen nebeneinander: Eingriffe in die Verwaltung (`AuditLog`) und
 * sicherheitsrelevante Ereignisse an Vorgängen – vor allem abgewiesene
 * Steuerbefehle aus E-Mails. Letztere sind der Ort, an dem ein
 * Fälschungsversuch auffallen würde.
 */
export default async function ProtokollPage({
  searchParams,
}: {
  searchParams: Promise<{ q?: string; art?: string }>;
}) {
  await requireAdmin();

  const sp = await searchParams;
  const suche = sp.q?.trim() ?? "";
  const art = sp.art ?? "alle";

  const [verwaltung, ereignisse] = await Promise.all([
    art === "tickets"
      ? Promise.resolve([])
      : db.auditLog.findMany({
          where: suche
            ? {
                OR: [
                  { action: { contains: suche, mode: "insensitive" } },
                  { summary: { contains: suche, mode: "insensitive" } },
                  { entityType: { contains: suche, mode: "insensitive" } },
                ],
              }
            : {},
          orderBy: { createdAt: "desc" },
          take: 100,
          include: { actor: { select: { name: true } } },
        }),
    art === "verwaltung"
      ? Promise.resolve([])
      : db.ticketEvent.findMany({
          where: {
            type: {
              in: [
                "EMAIL_COMMAND",
                "EMAIL_COMMAND_REJECTED",
                "TRANSFERRED",
                "SPLIT",
                "MERGED",
                "SLA_BREACHED",
              ],
            },
            ...(suche
              ? {
                  OR: [
                    { note: { contains: suche, mode: "insensitive" } },
                    { toValue: { contains: suche, mode: "insensitive" } },
                    { ticket: { number: { contains: suche, mode: "insensitive" } } },
                  ],
                }
              : {}),
          },
          orderBy: { createdAt: "desc" },
          take: 100,
          include: {
            actor: { select: { name: true } },
            ticket: { select: { number: true, subject: true } },
          },
        }),
  ]);

  // Beide Quellen in einen Strang, nach Zeit sortiert – so liest sich der
  // Verlauf wie eine Chronik statt wie zwei Listen.
  const eintraege = [
    ...verwaltung.map((v) => ({
      art: "verwaltung" as const,
      id: v.id,
      createdAt: v.createdAt,
      actorName: v.actor?.name ?? "System",
      text: v.summary ?? v.action,
      aktion: v.action,
      ticketNummer: null as string | null,
      warnung: false,
    })),
    ...ereignisse.map((e) => ({
      art: "ticket" as const,
      id: e.id,
      createdAt: e.createdAt,
      actorName: e.actor?.name ?? e.actorName ?? "System",
      text: describeEvent(e) + (e.note ? ` – ${e.note}` : ""),
      aktion: e.type,
      ticketNummer: e.ticket.number,
      warnung: e.type === "EMAIL_COMMAND_REJECTED",
    })),
  ].sort((a, b) => b.createdAt.getTime() - a.createdAt.getTime());

  return (
    <>
      <PageHeader
        title="Protokoll"
        description="Eingriffe in die Verwaltung und sicherheitsrelevante Ereignisse an Vorgängen. Abgewiesene E-Mail-Befehle sind besonders hervorgehoben – dort würde ein Fälschungsversuch auffallen."
      >
        <form className="flex flex-wrap items-center gap-2">
          <div className="relative min-w-56 flex-1">
            <label htmlFor="protokoll-suche" className="sr-only">
              Protokoll durchsuchen
            </label>
            <Search
              className="pointer-events-none absolute left-2.5 top-1/2 size-4 -translate-y-1/2 text-fg-subtle"
              aria-hidden="true"
            />
            <Input
              id="protokoll-suche"
              name="q"
              defaultValue={suche}
              placeholder="Aktion, Vorgangsnummer oder Text"
              className="h-9 pl-8.5"
            />
          </div>
          <Select
            name="art"
            defaultValue={art}
            className="h-9 w-auto min-w-40"
            aria-label="Art der Einträge"
          >
            <option value="alle">Alle Einträge</option>
            <option value="verwaltung">Nur Verwaltung</option>
            <option value="tickets">Nur Vorgänge</option>
          </Select>
        </form>
      </PageHeader>

      <div className="px-4 py-5 sm:px-6">
        <Card className="divide-y divide-line">
          {eintraege.length === 0 && (
            <p className="px-6 py-12 text-center text-sm text-fg-muted">
              Keine Einträge gefunden.
            </p>
          )}

          {eintraege.map((e) => (
            <div
              key={`${e.art}-${e.id}`}
              className={`flex items-start gap-3 px-5 py-3 ${e.warnung ? "bg-danger-soft/40" : ""}`}
            >
              {e.warnung ? (
                <span className="mt-0.5 flex size-7 shrink-0 items-center justify-center rounded-full bg-danger-soft">
                  <ShieldAlert
                    className="size-4 text-danger-soft-fg"
                    aria-hidden="true"
                  />
                </span>
              ) : (
                <Avatar name={e.actorName} size="sm" />
              )}

              <div className="min-w-0 flex-1">
                <p className="text-sm text-fg">
                  <span className="font-medium">{e.actorName}</span>{" "}
                  <span className="text-fg-muted">{e.text}</span>
                </p>
                <p className="mt-0.5 flex flex-wrap items-center gap-2 text-xs text-fg-subtle">
                  <time dateTime={e.createdAt.toISOString()}>
                    {formatDateTime(e.createdAt)}
                  </time>
                  <span aria-hidden="true">·</span>
                  <code className="font-mono">{e.aktion}</code>
                  {e.ticketNummer && (
                    <>
                      <span aria-hidden="true">·</span>
                      <Link
                        href={`/tickets/${e.ticketNummer}`}
                        className="font-mono text-primary hover:underline"
                      >
                        {e.ticketNummer}
                      </Link>
                    </>
                  )}
                </p>
              </div>

              {e.warnung && (
                <Badge tone="danger" size="sm">
                  Abgewiesen
                </Badge>
              )}
            </div>
          ))}
        </Card>
      </div>
    </>
  );
}
