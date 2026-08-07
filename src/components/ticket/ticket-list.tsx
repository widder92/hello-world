import Link from "next/link";
import {
  ArrowUpRight,
  Mail,
  MessageSquare,
  Paperclip,
  PauseCircle,
  ShieldAlert,
} from "lucide-react";

import { Avatar } from "@/components/ui/avatar";
import { PriorityBadge, SlaBadge, StatusBadge } from "@/components/ticket/badges";
import type { TicketListItem } from "@/server/services/ticket-queries";
import { formatCompact } from "@/lib/format";
import { isErledigt } from "@/lib/domain";
import { cn } from "@/lib/utils";

/**
 * Ticketliste.
 *
 * Zwei Erscheinungsformen für dieselben Daten:
 *
 *   "voll"     dichte Tabelle mit Priorität, Frist und Zuweisung – gedacht für
 *              Menschen, die den ganzen Tag darin arbeiten und viel auf einen
 *              Blick sehen wollen.
 *   "einfach"  großzügige Zeilen mit dem Nötigsten. Ein Hausmeister, der
 *              zwischen zwei Aufträgen kurz aufs Tablet schaut, braucht keine
 *              Spalte für die Erstreaktionsfrist.
 *
 * In beiden Fällen ist die ganze Zeile ein Link. Zusätzliche Links innerhalb
 * der Zeile gäbe es damit doppelt in der Tabulator-Reihenfolge, deshalb bleibt
 * die Zeile bewusst frei von weiteren Bedienelementen.
 */
export function TicketList({
  tickets,
  variant = "voll",
  showQueue = false,
  showAssignee = true,
  emptyTitle = "Keine Vorgänge",
  emptyHint,
}: {
  tickets: TicketListItem[];
  /**
   * "voll"     dichte Tabelle für die Arbeitsansichten
   * "einfach"  großzügige Zeilen für die schlanke Ausbaustufe
   * "kompakt"  einspaltig, für schmale Bereiche wie die Übersicht
   */
  variant?: "voll" | "einfach" | "kompakt";
  /** Bei Listen über mehrere Eingänge hinweg den Eingang mit anzeigen. */
  showQueue?: boolean;
  showAssignee?: boolean;
  emptyTitle?: string;
  emptyHint?: string;
}) {
  if (tickets.length === 0) {
    return (
      <div className="flex flex-col items-center justify-center gap-1.5 px-6 py-16 text-center">
        <p className="text-sm font-medium text-fg">{emptyTitle}</p>
        {emptyHint && <p className="max-w-sm text-sm text-fg-muted">{emptyHint}</p>}
      </div>
    );
  }

  if (variant === "kompakt") {
    return (
      <ul className="divide-y divide-line">
        {tickets.map((t) => (
          <KompakteZeile key={t.id} ticket={t} showQueue={showQueue} />
        ))}
      </ul>
    );
  }

  if (variant === "einfach") {
    return (
      <ul className="divide-y divide-line">
        {tickets.map((t) => (
          <EinfacheZeile key={t.id} ticket={t} showQueue={showQueue} />
        ))}
      </ul>
    );
  }

  return (
    <div className="overflow-x-auto">
      <table className="w-full min-w-[52rem] border-collapse text-sm">
        <thead>
          <tr className="border-b border-line text-left">
            <Th className="w-32">Nummer</Th>
            <Th>Betreff</Th>
            <Th className="w-36">Status</Th>
            <Th className="w-28">Priorität</Th>
            {showQueue && <Th className="w-40">Eingang</Th>}
            <Th className="w-36">Melder</Th>
            {showAssignee && <Th className="w-36">Bearbeitung</Th>}
            <Th className="w-40">Frist</Th>
            <Th className="w-28">Aktivität</Th>
          </tr>
        </thead>
        <tbody className="divide-y divide-line">
          {tickets.map((t) => (
            <VolleZeile
              key={t.id}
              ticket={t}
              showQueue={showQueue}
              showAssignee={showAssignee}
            />
          ))}
        </tbody>
      </table>
    </div>
  );
}

function Th({
  children,
  className,
}: {
  children: React.ReactNode;
  className?: string;
}) {
  return (
    <th
      scope="col"
      className={cn(
        "px-3 py-2.5 text-xs font-medium uppercase tracking-wide text-fg-subtle",
        className,
      )}
    >
      {children}
    </th>
  );
}

function VolleZeile({
  ticket: t,
  showQueue,
  showAssignee,
}: {
  ticket: TicketListItem;
  showQueue: boolean;
  showAssignee: boolean;
}) {
  const erledigt = isErledigt(t.status);

  return (
    // Die Zeile ist der Bezugspunkt für die Klickfläche, die sich unten aus
    // dem Betreff-Link über alle Zellen legt.
    <tr className="group relative transition-colors hover:bg-surface-2 focus-within:bg-surface-2">
      <td className="whitespace-nowrap px-3 py-2.5 align-middle">
        <span className="font-mono text-xs font-medium text-primary">
          {t.number}
        </span>
      </td>

      <td className="px-3 py-2.5 align-middle">
        <div className="flex items-center gap-2">
          <Link
            href={`/tickets/${t.number}`}
            className={cn(
              "min-w-0 flex-1 truncate font-medium outline-none",
              erledigt ? "text-fg-muted" : "text-fg",
            )}
          >
            {/* Deckt die gesamte Zeile ab, damit nicht der schmale Text
                getroffen werden muss – und bleibt dabei ein einziger
                Haltepunkt in der Tabulator-Reihenfolge. */}
            <span className="absolute inset-0" aria-hidden="true" />
            {t.subject}
          </Link>
          <Merkmale ticket={t} />
        </div>
        {(t.roomOrArea || t.category) && (
          <p className="mt-0.5 truncate text-xs text-fg-subtle">
            {[t.category?.name, t.roomOrArea].filter(Boolean).join(" · ")}
          </p>
        )}
      </td>

      <td className="px-3 py-2.5 align-middle">
        <StatusBadge status={t.status} size="sm" />
      </td>

      <td className="px-3 py-2.5 align-middle">
        <PriorityBadge priority={t.priority} size="sm" />
      </td>

      {showQueue && (
        <td className="px-3 py-2.5 align-middle">
          <span className="inline-flex items-center gap-1.5 text-xs text-fg-muted">
            <span
              className="size-2 shrink-0 rounded-full"
              style={{ backgroundColor: t.team.color }}
              aria-hidden="true"
            />
            <span className="truncate">{t.queue.name}</span>
          </span>
        </td>
      )}

      <td className="px-3 py-2.5 align-middle">
        <span className="flex items-center gap-1.5">
          <Avatar name={t.requester.name} size="xs" />
          <span className="truncate text-xs text-fg-muted">
            {t.requester.name}
          </span>
        </span>
      </td>

      {showAssignee && (
        <td className="px-3 py-2.5 align-middle">
          {t.assignee ? (
            <span className="flex items-center gap-1.5">
              <Avatar name={t.assignee.name} size="xs" />
              <span className="truncate text-xs text-fg-muted">
                {t.assignee.name}
              </span>
            </span>
          ) : (
            <span className="text-xs font-medium text-primary">
              Nicht übernommen
            </span>
          )}
        </td>
      )}

      <td className="px-3 py-2.5 align-middle">
        <SlaBadge
          due={t.resolutionDue}
          start={t.createdAt}
          erledigt={erledigt}
          warningThreshold={t.slaPolicy?.warningThreshold}
          size="sm"
        />
      </td>

      <td className="px-3 py-2.5 align-middle">
        <time
          dateTime={t.lastActivityAt.toISOString()}
          className="text-xs tabular-nums text-fg-subtle"
        >
          {formatCompact(t.lastActivityAt)}
        </time>
      </td>
    </tr>
  );
}

function EinfacheZeile({
  ticket: t,
  showQueue,
}: {
  ticket: TicketListItem;
  showQueue: boolean;
}) {
  const erledigt = isErledigt(t.status);

  return (
    <li className="relative transition-colors hover:bg-surface-2 focus-within:bg-surface-2">
      <Link href={`/tickets/${t.number}`} className="block px-4 py-4 outline-none">
        <div className="flex items-start justify-between gap-3">
          <div className="min-w-0 flex-1">
            <div className="flex items-center gap-2">
              <h3
                className={cn(
                  "truncate text-base font-medium",
                  erledigt ? "text-fg-muted" : "text-fg",
                )}
              >
                {t.subject}
              </h3>
              <Merkmale ticket={t} />
            </div>

            <p className="mt-1 flex flex-wrap items-center gap-x-2 gap-y-0.5 text-sm text-fg-muted">
              <span className="font-mono text-xs">{t.number}</span>
              <span aria-hidden="true">·</span>
              <span>von {t.requester.name}</span>
              {t.site && (
                <>
                  <span aria-hidden="true">·</span>
                  <span>{t.site.shortName ?? t.site.name}</span>
                </>
              )}
              {t.roomOrArea && (
                <>
                  <span aria-hidden="true">·</span>
                  <span>{t.roomOrArea}</span>
                </>
              )}
              {showQueue && (
                <>
                  <span aria-hidden="true">·</span>
                  <span>{t.queue.name}</span>
                </>
              )}
            </p>
          </div>

          <div className="flex shrink-0 flex-col items-end gap-1.5">
            <StatusBadge status={t.status} einfach />
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
  );
}

/**
 * Einspaltige Zeile für schmale Bereiche.
 *
 * Zeigt genau das, was beim Überfliegen zählt: Betreff, Status, Frist und
 * seit wann nichts mehr passiert ist. Eine Tabelle mit acht Spalten in einer
 * halbbreiten Karte würde nur seitlich scrollen.
 */
function KompakteZeile({
  ticket: t,
  showQueue,
}: {
  ticket: TicketListItem;
  showQueue: boolean;
}) {
  const erledigt = isErledigt(t.status);

  return (
    <li className="relative transition-colors hover:bg-surface-2 focus-within:bg-surface-2">
      <Link href={`/tickets/${t.number}`} className="block px-4 py-3 outline-none">
        <div className="flex items-start justify-between gap-3">
          <div className="min-w-0 flex-1">
            <div className="flex items-center gap-2">
              <span
                className={cn(
                  "truncate text-sm font-medium",
                  erledigt ? "text-fg-muted" : "text-fg",
                )}
              >
                {t.subject}
              </span>
              <Merkmale ticket={t} />
            </div>

            <p className="mt-0.5 flex flex-wrap items-center gap-x-1.5 text-xs text-fg-subtle">
              <span className="font-mono">{t.number}</span>
              {showQueue && (
                <>
                  <span aria-hidden="true">·</span>
                  <span className="truncate">{t.queue.name}</span>
                </>
              )}
              {t.site && (
                <>
                  <span aria-hidden="true">·</span>
                  <span className="truncate">
                    {t.site.shortName ?? t.site.name}
                  </span>
                </>
              )}
              <span aria-hidden="true">·</span>
              <time dateTime={t.lastActivityAt.toISOString()}>
                {formatCompact(t.lastActivityAt)}
              </time>
            </p>
          </div>

          <div className="flex shrink-0 flex-col items-end gap-1">
            <StatusBadge status={t.status} size="sm" />
            <SlaBadge
              due={t.resolutionDue}
              start={t.createdAt}
              erledigt={erledigt}
              warningThreshold={t.slaPolicy?.warningThreshold}
              size="sm"
              showTime={false}
            />
          </div>
        </div>
      </Link>
    </li>
  );
}

/** Kleine Hinweise: Anhänge, Abspaltungen, E-Mail-Herkunft, Quarantäne. */
function Merkmale({ ticket: t }: { ticket: TicketListItem }) {
  const marks: React.ReactNode[] = [];

  if (t.quarantined) {
    marks.push(
      <span
        key="quarantaene"
        title="Absender unbekannt – bitte prüfen"
        className="text-warning"
      >
        <ShieldAlert className="size-3.5" aria-hidden="true" />
        <span className="sr-only">Absender unbekannt, bitte prüfen</span>
      </span>,
    );
  }

  if (t.waitingForChild) {
    marks.push(
      <span
        key="wartet"
        title="Wartet auf einen abgespaltenen Teilvorgang"
        className="text-fg-subtle"
      >
        <PauseCircle className="size-3.5" aria-hidden="true" />
        <span className="sr-only">Wartet auf einen abgespaltenen Teilvorgang</span>
      </span>,
    );
  }

  if (t._count.linksFrom > 0) {
    marks.push(
      <span key="split" title="Hat verknüpfte Vorgänge" className="text-fg-subtle">
        <ArrowUpRight className="size-3.5" aria-hidden="true" />
        <span className="sr-only">Hat verknüpfte Vorgänge</span>
      </span>,
    );
  }

  if (t._count.attachments > 0) {
    marks.push(
      <span key="anhang" className="text-fg-subtle">
        <Paperclip className="size-3.5" aria-hidden="true" />
        <span className="sr-only">{t._count.attachments} Anhänge</span>
      </span>,
    );
  }

  if (t.source === "EMAIL") {
    marks.push(
      <span key="mail" title="Per E-Mail eingegangen" className="text-fg-subtle">
        <Mail className="size-3.5" aria-hidden="true" />
        <span className="sr-only">Per E-Mail eingegangen</span>
      </span>,
    );
  }

  if (t._count.messages > 2) {
    marks.push(
      <span
        key="nachrichten"
        className="flex items-center gap-0.5 text-xs text-fg-subtle"
      >
        <MessageSquare className="size-3.5" aria-hidden="true" />
        {t._count.messages}
        <span className="sr-only">Nachrichten im Verlauf</span>
      </span>,
    );
  }

  if (marks.length === 0) return null;

  return <span className="flex shrink-0 items-center gap-1.5">{marks}</span>;
}
