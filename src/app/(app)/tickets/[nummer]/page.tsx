import type { Metadata } from "next";
import Link from "next/link";
import { notFound, redirect } from "next/navigation";
import { LinkType, MessageType, TeamMode } from "@prisma/client";
import { ArrowUpRight, GitMerge, Split } from "lucide-react";

import { AbspaltenDialog } from "@/components/ticket/abspalten-dialog";
import { Aktionsleiste } from "@/components/ticket/aktionsleiste";
import { Composer } from "@/components/ticket/composer";
import { SlaBadge } from "@/components/ticket/badges";
import { TicketSeitenleiste } from "@/components/ticket/seitenleiste";
import { Timeline, type VerlaufEintrag } from "@/components/ticket/timeline";
import { WeiterleitenDialog } from "@/components/ticket/weiterleiten-dialog";
import { ZusammenfuehrenDialog } from "@/components/ticket/zusammenfuehren-dialog";
import { Card } from "@/components/ui/card";
import { statusAuswahlFor } from "@/server/capabilities";
import { db } from "@/server/db";
import {
  canAssignToOthers,
  getActor,
  membershipFor,
  ticketAccessLevel,
} from "@/server/policy";
import { SOURCE, isErledigt } from "@/lib/domain";
import { formatDateTime } from "@/lib/format";

export async function generateMetadata({
  params,
}: {
  params: Promise<{ nummer: string }>;
}): Promise<Metadata> {
  const { nummer } = await params;
  const ticket = await db.ticket.findUnique({
    where: { number: nummer.toUpperCase() },
    select: { number: true, subject: true },
  });
  return { title: ticket ? `${ticket.number} – ${ticket.subject}` : "Vorgang" };
}

/**
 * Ansicht eines Vorgangs.
 *
 * Aufbau: links der Verlauf mit dem Antwortfeld, rechts die Eigenschaften. Was
 * an Aktionen erscheint, entscheidet die Ausbaustufe des Bereichs zusammen mit
 * der Rolle – ein Melder, der seinen eigenen Vorgang öffnet, sieht denselben
 * Verlauf ohne interne Notizen und ohne Bearbeitungsknöpfe.
 */
export default async function TicketSeite({
  params,
}: {
  params: Promise<{ nummer: string }>;
}) {
  const actor = await getActor();
  if (!actor) redirect("/anmelden");

  const { nummer } = await params;

  const ticket = await db.ticket.findUnique({
    where: { number: decodeURIComponent(nummer).toUpperCase() },
    include: {
      team: true,
      queue: { include: { site: true } },
      category: { select: { id: true, name: true } },
      site: true,
      requester: {
        select: { id: true, name: true, email: true, phone: true, jobTitle: true },
      },
      assignee: { select: { id: true, name: true } },
      slaPolicy: { select: { warningThreshold: true } },
      watchers: {
        select: { userId: true, user: { select: { id: true, name: true } } },
      },
      tags: { select: { tag: { select: { id: true, name: true, color: true } } } },
      tasks: { orderBy: { sortOrder: "asc" } },
      assets: {
        select: {
          asset: {
            select: {
              id: true,
              assetTag: true,
              name: true,
              kind: { select: { name: true } },
            },
          },
        },
      },
      linksFrom: {
        select: {
          type: true,
          to: { select: { number: true, subject: true, status: true, team: { select: { name: true } } } },
        },
      },
      linksTo: {
        select: {
          type: true,
          from: { select: { number: true, subject: true, status: true, team: { select: { name: true } } } },
        },
      },
    },
  });

  if (!ticket) notFound();

  const level = ticketAccessLevel(actor, ticket);
  if (level === "none") notFound();

  const istAgent = level === "agent";
  const einfach = ticket.team.mode === TeamMode.EINFACH;
  const membership = membershipFor(actor, ticket.teamId);
  const capabilities = membership?.capabilities;

  // --- Verlauf zusammenstellen ---------------------------------------------
  const [nachrichten, ereignisse, textbausteine] = await Promise.all([
    db.ticketMessage.findMany({
      where: {
        ticketId: ticket.id,
        // Melder sehen interne Notizen gar nicht erst – sie werden nicht
        // geladen, statt sie nur im Frontend auszublenden.
        ...(istAgent ? {} : { type: { not: MessageType.INTERNAL_NOTE } }),
      },
      orderBy: { createdAt: "asc" },
      include: {
        author: { select: { id: true, name: true } },
        attachments: {
          select: { id: true, filename: true, mimeType: true, size: true },
        },
      },
    }),
    istAgent
      ? db.ticketEvent.findMany({
          where: { ticketId: ticket.id },
          orderBy: { createdAt: "asc" },
          include: { actor: { select: { name: true } } },
        })
      : db.ticketEvent.findMany({
          // Für Melder nur die Ereignisse, die sie betreffen.
          where: {
            ticketId: ticket.id,
            type: { in: ["CREATED", "TRANSFERRED", "SPLIT", "STATUS_CHANGED", "MERGED"] },
          },
          orderBy: { createdAt: "asc" },
          include: { actor: { select: { name: true } } },
        }),
    istAgent && capabilities?.vorlagen
      ? db.cannedResponse.findMany({
          where: { isActive: true, OR: [{ teamId: ticket.teamId }, { teamId: null }] },
          orderBy: { sortOrder: "asc" },
          select: { id: true, title: true, shortcut: true, body: true },
          take: 12,
        })
      : Promise.resolve([]),
  ]);

  const verlauf: VerlaufEintrag[] = [
    ...nachrichten.map((m) => ({
      art: "nachricht" as const,
      id: m.id,
      createdAt: m.createdAt,
      type: m.type,
      body: m.bodyText || m.body,
      authorId: m.authorId,
      authorName: m.authorName,
      authorEmail: m.authorEmail,
      autorAnzeige:
        m.author?.name ?? m.authorName ?? m.authorEmail ?? "Unbekannt",
      vomMelder: m.authorId === ticket.requesterId,
      emailSentAt: m.emailSentAt,
      viaEmailCommand: m.viaEmailCommand,
      anhaenge: m.attachments,
    })),
    ...ereignisse
      // Der Anlege-Eintrag steht schon als erste Nachricht im Verlauf.
      .filter((e) => e.type !== "CREATED")
      .map((e) => ({
        art: "ereignis" as const,
        id: e.id,
        createdAt: e.createdAt,
        type: e.type,
        actorName: e.actor?.name ?? e.actorName,
        fromValue: e.fromValue,
        toValue: e.toValue,
        note: e.note,
      })),
  ].sort((a, b) => a.createdAt.getTime() - b.createdAt.getTime());

  // Auswahl für den Abspalten-Dialog.
  const nachrichtenZurAuswahl = nachrichten
    .filter((m) => m.type !== MessageType.SYSTEM)
    .map((m) => ({
      id: m.id,
      autor: m.author?.name ?? m.authorName ?? m.authorEmail ?? "Unbekannt",
      intern: m.type === MessageType.INTERNAL_NOTE,
      vorschau: m.bodyText || m.body,
      createdAt: m.createdAt,
    }));

  // --- Verknüpfte Vorgänge -------------------------------------------------
  const abgespaltene = ticket.linksFrom.filter((l) => l.type === LinkType.SPLIT_FROM);
  const stammtAus = ticket.linksTo.find((l) => l.type === LinkType.SPLIT_FROM);
  const zusammengefuehrtIn = ticket.linksFrom.find(
    (l) => l.type === LinkType.MERGED_INTO,
  );

  return (
    <div className="mx-auto max-w-7xl px-4 py-5 sm:px-6">
      {/* --- Kopf ------------------------------------------------------- */}
      <div className="mb-5">
        <div className="flex flex-wrap items-center gap-2 text-sm text-fg-muted">
          <span className="font-mono font-medium text-primary">
            {ticket.number}
          </span>
          <span aria-hidden="true">·</span>
          <Link
            href={
              einfach
                ? `/unsere-tickets/${ticket.queueId}`
                : `/eingang/${ticket.queueId}`
            }
            className="hover:text-fg hover:underline"
          >
            {ticket.queue.name}
          </Link>
          <span aria-hidden="true">·</span>
          <span>{SOURCE[ticket.source]}</span>
          <span aria-hidden="true">·</span>
          <time dateTime={ticket.createdAt.toISOString()}>
            {formatDateTime(ticket.createdAt)}
          </time>
        </div>

        <h1 className="mt-1.5 text-2xl font-semibold tracking-tight text-fg text-balance">
          {ticket.subject}
        </h1>

        {!einfach && ticket.resolutionDue && (
          <div className="mt-2">
            <SlaBadge
              due={ticket.resolutionDue}
              start={ticket.createdAt}
              erledigt={isErledigt(ticket.status)}
              warningThreshold={ticket.slaPolicy?.warningThreshold}
            />
          </div>
        )}
      </div>

      {/* --- Verknüpfungen ---------------------------------------------- */}
      {(stammtAus || abgespaltene.length > 0 || zusammengefuehrtIn) && (
        <div className="mb-5 space-y-2">
          {stammtAus && (
            <VerknuepfungsHinweis
              icon={Split}
              text="Dieser Vorgang wurde abgespalten aus"
              nummer={stammtAus.from.number}
              betreff={stammtAus.from.subject}
              bereich={stammtAus.from.team.name}
            />
          )}

          {abgespaltene.map((l) => (
            <VerknuepfungsHinweis
              key={l.to.number}
              icon={ArrowUpRight}
              text={
                ticket.waitingForChild
                  ? "Wartet auf den abgespaltenen Teilvorgang"
                  : "Teilvorgang abgespalten an"
              }
              nummer={l.to.number}
              betreff={l.to.subject}
              bereich={l.to.team.name}
              hervorgehoben={ticket.waitingForChild}
            />
          ))}

          {zusammengefuehrtIn && (
            <VerknuepfungsHinweis
              icon={GitMerge}
              text="Dieser Vorgang wurde zusammengeführt mit"
              nummer={zusammengefuehrtIn.to.number}
              betreff={zusammengefuehrtIn.to.subject}
              bereich={zusammengefuehrtIn.to.team.name}
            />
          )}
        </div>
      )}

      <div className="grid gap-5 lg:grid-cols-[minmax(0,1fr)_20rem]">
        {/* --- Verlauf und Antwort --------------------------------------- */}
        <div className="min-w-0 space-y-5">
          {istAgent && (
            <div className="flex flex-wrap items-start justify-between gap-3">
              <Aktionsleiste
                ticketId={ticket.id}
                status={ticket.status}
                prioritaet={ticket.priority}
                bearbeiter={ticket.assignee}
                eigeneId={actor.id}
                teamMode={ticket.team.mode}
                statusAuswahl={statusAuswahlFor(ticket.team.mode)}
                darfZuweisen={Boolean(capabilities?.zuweisung) || actor.isAdmin}
                darfAnAndereZuweisen={canAssignToOthers(actor, ticket.teamId)}
              />

              <div className="flex flex-wrap gap-2">
                {(capabilities?.weiterleiten ?? true) && (
                  <WeiterleitenDialog
                    ticketId={ticket.id}
                    ticketNummer={ticket.number}
                    aktuellerStandortId={ticket.siteId}
                    aktuellerBereich={ticket.team.name}
                  />
                )}
                {(capabilities?.abspalten ?? true) && (
                  <AbspaltenDialog
                    ticketId={ticket.id}
                    ticketNummer={ticket.number}
                    ticketBetreff={ticket.subject}
                    aktuellerStandortId={ticket.siteId}
                    nachrichten={nachrichtenZurAuswahl}
                  />
                )}
                {capabilities?.zusammenfuehren && (
                  <ZusammenfuehrenDialog
                    ticketId={ticket.id}
                    ticketNummer={ticket.number}
                  />
                )}
              </div>
            </div>
          )}

          <section aria-label="Verlauf">
            <Timeline eintraege={verlauf} zeigeIntern={istAgent} />
          </section>

          <section aria-label={istAgent ? "Antworten" : "Rückmeldung schreiben"}>
            <h2 className="sr-only">
              {istAgent ? "Antworten" : "Rückmeldung schreiben"}
            </h2>
            <Composer
              ticketId={ticket.id}
              darfIntern={istAgent && Boolean(capabilities?.interneNotizen)}
              darfStatusSetzen={istAgent}
              textbausteine={textbausteine}
              melderAnsicht={!istAgent}
            />
          </section>
        </div>

        {/* --- Eigenschaften -------------------------------------------- */}
        <aside className="lg:sticky lg:top-20 lg:self-start">
          <TicketSeitenleiste
            ticket={ticket}
            istAgent={istAgent}
            einfach={einfach}
            capabilities={capabilities ?? null}
          />
        </aside>
      </div>
    </div>
  );
}

function VerknuepfungsHinweis({
  icon: Icon,
  text,
  nummer,
  betreff,
  bereich,
  hervorgehoben,
}: {
  icon: React.ComponentType<{ className?: string; "aria-hidden"?: boolean }>;
  text: string;
  nummer: string;
  betreff: string;
  bereich: string;
  hervorgehoben?: boolean;
}) {
  return (
    <Card
      className={
        hervorgehoben
          ? "border-warning/40 bg-warning-soft"
          : "border-line bg-surface-2"
      }
    >
      <div className="flex items-start gap-2.5 px-4 py-3">
        <Icon
          className={`mt-0.5 size-4 shrink-0 ${hervorgehoben ? "text-warning-soft-fg" : "text-fg-subtle"}`}
          aria-hidden={true}
        />
        <div className="min-w-0 flex-1 text-sm">
          <p
            className={
              hervorgehoben ? "text-warning-soft-fg" : "text-fg-muted"
            }
          >
            {text}{" "}
            <Link
              href={`/tickets/${nummer}`}
              className="font-mono font-medium text-primary hover:underline"
            >
              {nummer}
            </Link>
          </p>
          <p className="mt-0.5 truncate text-xs text-fg-subtle">
            {betreff} · {bereich}
          </p>
        </div>
      </div>
    </Card>
  );
}
