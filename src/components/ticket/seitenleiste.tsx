import Link from "next/link";
import type { Priority, TicketStatus } from "@prisma/client";
import { Building2, Mail, MapPin, Phone, Tag as TagIcon } from "lucide-react";

import { Avatar } from "@/components/ui/avatar";
import { Badge } from "@/components/ui/badge";
import { Card, CardContent } from "@/components/ui/card";
import { Beobachter } from "@/components/ticket/beobachter";
import { Checkliste } from "@/components/ticket/checkliste";
import { PriorityBadge, StatusBadge } from "@/components/ticket/badges";
import type { TeamCapabilities } from "@/server/capabilities";
import { IMPACT, SITE_TYPE, URGENCY } from "@/lib/domain";
import { formatDateTime } from "@/lib/format";

/**
 * Eigenschaften eines Vorgangs.
 *
 * Was hier steht, richtet sich nach der Ausbaustufe: Ein Bereich im
 * Einfach-Modus sieht Melder, Standort und Zeiten – aber keine Priorität,
 * keine Fristen, keine Schlagworte und keine Geräteverknüpfung. Diese Felder
 * wären dort nicht nur unbenutzt, sie würden auch den Eindruck erwecken, man
 * müsse sich um sie kümmern.
 */

interface SeitenleistenTicket {
  id: string;
  number: string;
  status: TicketStatus;
  priority: Priority;
  impact: string | null;
  urgency: string | null;
  createdAt: Date;
  resolvedAt: Date | null;
  closedAt: Date | null;
  firstResponseAt: Date | null;
  roomOrArea: string | null;
  formData: unknown;
  requester: {
    id: string;
    name: string;
    email: string;
    phone: string | null;
    jobTitle: string | null;
  };
  assignee: { id: string; name: string } | null;
  team: { id: string; name: string; color: string };
  queue: { id: string; name: string };
  site: { id: string; name: string; type: string } | null;
  category: { id: string; name: string } | null;
  tags: { tag: { id: string; name: string; color: string } }[];
  tasks: {
    id: string;
    title: string;
    done: boolean;
    sortOrder: number;
  }[];
  assets: {
    asset: {
      id: string;
      assetTag: string;
      name: string;
      kind: { name: string };
    };
  }[];
  watchers: { userId: string; user: { id: string; name: string } }[];
}

export function TicketSeitenleiste({
  ticket,
  istAgent,
  einfach,
  capabilities,
}: {
  ticket: SeitenleistenTicket;
  istAgent: boolean;
  einfach: boolean;
  capabilities: TeamCapabilities | null;
}) {
  const zusatzfelder = leseZusatzfelder(ticket.formData);

  return (
    <div className="space-y-4">
      <Card>
        <CardContent className="space-y-4 py-4">
          <Zeile label="Status">
            <StatusBadge status={ticket.status} einfach={einfach} />
          </Zeile>

          {!einfach && (
            <Zeile label="Priorität">
              <PriorityBadge priority={ticket.priority} />
            </Zeile>
          )}

          <Zeile label="Bereich">
            <span className="inline-flex items-center gap-1.5 text-sm text-fg">
              <span
                className="size-2 shrink-0 rounded-full"
                style={{ backgroundColor: ticket.team.color }}
                aria-hidden="true"
              />
              {ticket.team.name}
            </span>
          </Zeile>

          {ticket.site && (
            <Zeile label="Standort">
              <span className="flex items-start gap-1.5 text-sm text-fg">
                <Building2
                  className="mt-0.5 size-3.5 shrink-0 text-fg-subtle"
                  aria-hidden="true"
                />
                <span>
                  {ticket.site.name}
                  <span className="block text-xs text-fg-subtle">
                    {SITE_TYPE[ticket.site.type as keyof typeof SITE_TYPE]}
                  </span>
                </span>
              </span>
            </Zeile>
          )}

          {ticket.roomOrArea && (
            <Zeile label="Raum / Bereich">
              <span className="flex items-center gap-1.5 text-sm text-fg">
                <MapPin className="size-3.5 shrink-0 text-fg-subtle" aria-hidden="true" />
                {ticket.roomOrArea}
              </span>
            </Zeile>
          )}

          {ticket.category && (
            <Zeile label="Kategorie">
              <span className="text-sm text-fg">{ticket.category.name}</span>
            </Zeile>
          )}

          {istAgent && (
            <Zeile label="Bearbeitung">
              {ticket.assignee ? (
                <span className="flex items-center gap-1.5 text-sm text-fg">
                  <Avatar name={ticket.assignee.name} size="xs" />
                  {ticket.assignee.name}
                </span>
              ) : (
                <span className="text-sm font-medium text-primary">
                  Noch nicht übernommen
                </span>
              )}
            </Zeile>
          )}
        </CardContent>
      </Card>

      {/* --- Melder ------------------------------------------------------ */}
      <Card>
        <CardContent className="py-4">
          <h2 className="mb-2.5 text-xs font-semibold uppercase tracking-wide text-fg-subtle">
            Melder
          </h2>
          <div className="flex items-start gap-2.5">
            <Avatar name={ticket.requester.name} size="md" />
            <div className="min-w-0 flex-1">
              <p className="text-sm font-medium text-fg">
                {ticket.requester.name}
              </p>
              {ticket.requester.jobTitle && (
                <p className="text-xs text-fg-muted">
                  {ticket.requester.jobTitle}
                </p>
              )}
              <a
                href={`mailto:${ticket.requester.email}`}
                className="mt-1.5 flex items-center gap-1.5 text-xs text-fg-muted hover:text-primary hover:underline"
              >
                <Mail className="size-3.5 shrink-0" aria-hidden="true" />
                <span className="truncate">{ticket.requester.email}</span>
              </a>
              {ticket.requester.phone && (
                <a
                  href={`tel:${ticket.requester.phone}`}
                  className="mt-0.5 flex items-center gap-1.5 text-xs text-fg-muted hover:text-primary hover:underline"
                >
                  <Phone className="size-3.5 shrink-0" aria-hidden="true" />
                  {ticket.requester.phone}
                </a>
              )}
            </div>
          </div>

          {istAgent && (
            <div className="mt-3.5 border-t border-line pt-3">
              <Beobachter
                ticketId={ticket.id}
                beobachter={ticket.watchers.map((w) => ({
                  userId: w.userId,
                  name: w.user.name,
                }))}
                darfAendern
              />
            </div>
          )}
        </CardContent>
      </Card>

      {/* --- Angaben aus dem Meldeformular ------------------------------- */}
      {zusatzfelder.length > 0 && (
        <Card>
          <CardContent className="py-4">
            <h2 className="mb-2.5 text-xs font-semibold uppercase tracking-wide text-fg-subtle">
              Angaben aus dem Formular
            </h2>
            <dl className="space-y-2">
              {zusatzfelder.map(([schluessel, wert]) => (
                <div key={schluessel}>
                  <dt className="text-xs text-fg-subtle">{schluessel}</dt>
                  <dd className="text-sm text-fg">{wert}</dd>
                </div>
              ))}
            </dl>
          </CardContent>
        </Card>
      )}

      {/* --- Checkliste (nur Voll-Modus) --------------------------------- */}
      {istAgent && capabilities?.checklisten && ticket.tasks.length > 0 && (
        <Card>
          <CardContent className="py-4">
            <Checkliste ticketId={ticket.id} aufgaben={ticket.tasks} />
          </CardContent>
        </Card>
      )}

      {/* --- Geräte (nur Voll-Modus) ------------------------------------- */}
      {capabilities?.geraete && ticket.assets.length > 0 && (
        <Card>
          <CardContent className="py-4">
            <h2 className="mb-2.5 text-xs font-semibold uppercase tracking-wide text-fg-subtle">
              Betroffene Geräte
            </h2>
            <ul className="space-y-1.5">
              {ticket.assets.map(({ asset }) => (
                <li key={asset.id}>
                  <Link
                    href={`/inventar?q=${encodeURIComponent(asset.assetTag)}`}
                    className="block rounded-md border border-line px-2.5 py-1.5 text-sm transition-colors hover:bg-surface-2"
                  >
                    <span className="font-mono text-xs text-fg-subtle">
                      {asset.assetTag}
                    </span>
                    <span className="block text-fg">{asset.name}</span>
                    <span className="text-xs text-fg-subtle">
                      {asset.kind.name}
                    </span>
                  </Link>
                </li>
              ))}
            </ul>
          </CardContent>
        </Card>
      )}

      {/* --- Schlagworte (nur Voll-Modus) -------------------------------- */}
      {capabilities?.tags && ticket.tags.length > 0 && (
        <Card>
          <CardContent className="py-4">
            <h2 className="mb-2 text-xs font-semibold uppercase tracking-wide text-fg-subtle">
              Schlagworte
            </h2>
            <ul className="flex flex-wrap gap-1.5">
              {ticket.tags.map(({ tag }) => (
                <li key={tag.id}>
                  <Badge tone="outline">
                    <TagIcon aria-hidden="true" style={{ color: tag.color }} />
                    {tag.name}
                  </Badge>
                </li>
              ))}
            </ul>
          </CardContent>
        </Card>
      )}

      {/* --- Zeiten ------------------------------------------------------ */}
      <Card>
        <CardContent className="py-4">
          <h2 className="mb-2.5 text-xs font-semibold uppercase tracking-wide text-fg-subtle">
            Zeiten
          </h2>
          <dl className="space-y-2 text-sm">
            <Zeit label="Gemeldet" wert={ticket.createdAt} />
            {istAgent && ticket.firstResponseAt && (
              <Zeit label="Erste Antwort" wert={ticket.firstResponseAt} />
            )}
            {ticket.resolvedAt && <Zeit label="Erledigt" wert={ticket.resolvedAt} />}
            {ticket.closedAt && <Zeit label="Geschlossen" wert={ticket.closedAt} />}
          </dl>

          {!einfach && istAgent && (ticket.impact || ticket.urgency) && (
            <div className="mt-3 border-t border-line pt-3">
              <dl className="space-y-2 text-sm">
                {ticket.impact && (
                  <div>
                    <dt className="text-xs text-fg-subtle">Auswirkung</dt>
                    <dd className="text-fg">
                      {IMPACT[ticket.impact as keyof typeof IMPACT]?.label}
                    </dd>
                  </div>
                )}
                {ticket.urgency && (
                  <div>
                    <dt className="text-xs text-fg-subtle">Dringlichkeit</dt>
                    <dd className="text-fg">
                      {URGENCY[ticket.urgency as keyof typeof URGENCY]?.label}
                    </dd>
                  </div>
                )}
              </dl>
            </div>
          )}
        </CardContent>
      </Card>
    </div>
  );
}

function Zeile({
  label,
  children,
}: {
  label: string;
  children: React.ReactNode;
}) {
  return (
    <div className="flex flex-wrap items-center justify-between gap-2">
      <span className="text-xs font-medium uppercase tracking-wide text-fg-subtle">
        {label}
      </span>
      {children}
    </div>
  );
}

function Zeit({ label, wert }: { label: string; wert: Date }) {
  return (
    <div className="flex items-baseline justify-between gap-2">
      <dt className="text-xs text-fg-subtle">{label}</dt>
      <dd>
        <time dateTime={wert.toISOString()} className="text-xs text-fg-muted">
          {formatDateTime(wert)}
        </time>
      </dd>
    </div>
  );
}

/**
 * Liest die Antworten aus dem Kategorieformular.
 *
 * Die Schlüssel sind technische Namen aus dem Formularaufbau; für die Anzeige
 * werden sie in lesbare Wörter zerlegt, damit aus "druckerStandort" nicht
 * genau das im Bildschirm steht.
 */
function leseZusatzfelder(formData: unknown): [string, string][] {
  if (!formData || typeof formData !== "object") return [];

  return Object.entries(formData as Record<string, unknown>)
    .filter(([, wert]) => wert !== null && wert !== undefined && wert !== "")
    .map(([schluessel, wert]) => [
      schluessel
        .replace(/([A-Z])/g, " $1")
        .replace(/^./, (c) => c.toUpperCase())
        .trim(),
      String(wert),
    ]);
}
