import { EventType, MessageType } from "@prisma/client";
import Link from "next/link";
import { Lock, Mail, Paperclip, Settings2 } from "lucide-react";

import { Avatar } from "@/components/ui/avatar";
import { EVENT_ICON, WICHTIGE_EREIGNISSE, describeEvent } from "@/lib/domain";
import { formatBytes, formatDateTime, formatRelative } from "@/lib/format";
import { cn } from "@/lib/utils";

/**
 * Verlauf eines Vorgangs.
 *
 * Nachrichten und Protokolleinträge stehen in einem gemeinsamen, nach Zeit
 * sortierten Strang. Ein getrennter Reiter "Historie" hilft niemandem: Beim
 * Lesen will man wissen, was zwischen zwei Antworten passiert ist – etwa dass
 * der Vorgang zwischendurch den Bereich gewechselt hat.
 *
 * Interne Notizen sind deutlich abgesetzt und tragen ein Schloss. Wer eine
 * interne Notiz für eine Antwort an den Melder hält, schreibt irgendwann etwas
 * Internes an den Melder – das darf gar nicht erst passieren können.
 */

export interface VerlaufNachricht {
  art: "nachricht";
  id: string;
  createdAt: Date;
  type: MessageType;
  body: string;
  authorId: string | null;
  authorName: string | null;
  authorEmail: string | null;
  autorAnzeige: string;
  vomMelder: boolean;
  emailSentAt: Date | null;
  viaEmailCommand: boolean;
  anhaenge: {
    id: string;
    filename: string;
    mimeType: string;
    size: number;
  }[];
}

export interface VerlaufEreignis {
  art: "ereignis";
  id: string;
  createdAt: Date;
  type: EventType;
  actorName: string | null;
  fromValue: string | null;
  toValue: string | null;
  note: string | null;
  /** Verweis auf einen anderen Vorgang, etwa bei Abspaltung. */
  verweisNummer?: string | null;
}

export type VerlaufEintrag = VerlaufNachricht | VerlaufEreignis;

export function Timeline({
  eintraege,
  zeigeIntern = true,
}: {
  eintraege: VerlaufEintrag[];
  /** Im Melder-Portal werden interne Notizen gar nicht erst geladen. */
  zeigeIntern?: boolean;
}) {
  const sichtbar = zeigeIntern
    ? eintraege
    : eintraege.filter(
        (e) => e.art !== "nachricht" || e.type !== MessageType.INTERNAL_NOTE,
      );

  return (
    <ol className="space-y-4">
      {sichtbar.map((e) =>
        e.art === "nachricht" ? (
          <NachrichtEintrag key={e.id} nachricht={e} />
        ) : (
          <EreignisEintrag key={e.id} ereignis={e} />
        ),
      )}
    </ol>
  );
}

function NachrichtEintrag({ nachricht: n }: { nachricht: VerlaufNachricht }) {
  const intern = n.type === MessageType.INTERNAL_NOTE;
  const system = n.type === MessageType.SYSTEM;

  if (system) {
    return (
      <li>
        <div className="flex items-start gap-2.5 rounded-lg border border-dashed border-line bg-surface-2 px-4 py-3">
          <Settings2
            className="mt-0.5 size-4 shrink-0 text-fg-subtle"
            aria-hidden="true"
          />
          <div className="min-w-0 flex-1">
            <p className="text-sm text-fg-muted">{n.body}</p>
            <time
              dateTime={n.createdAt.toISOString()}
              className="mt-1 block text-xs text-fg-subtle"
            >
              {formatDateTime(n.createdAt)}
            </time>
          </div>
        </div>
      </li>
    );
  }

  return (
    <li>
      <article
        className={cn(
          "rounded-lg border",
          intern
            ? // Warnfarbe plus Schloss plus Beschriftung – dreifach, weil ein
              // Versehen hier teuer ist.
              "border-warning/40 bg-warning-soft"
            : "border-line bg-surface",
        )}
      >
        <header
          className={cn(
            "flex flex-wrap items-center gap-x-2 gap-y-1 border-b px-4 py-2.5",
            intern ? "border-warning/25" : "border-line",
          )}
        >
          <Avatar name={n.autorAnzeige} size="sm" />

          <div className="min-w-0 flex-1">
            <span className="text-sm font-medium text-fg">{n.autorAnzeige}</span>
            {n.vomMelder && (
              <span className="ml-1.5 text-xs text-fg-subtle">(Melder)</span>
            )}
          </div>

          {intern && (
            <span className="inline-flex items-center gap-1 rounded-md bg-warning/15 px-2 py-0.5 text-xs font-semibold text-warning-soft-fg">
              <Lock className="size-3" aria-hidden="true" />
              Interne Notiz – geht nicht an den Melder
            </span>
          )}

          {n.viaEmailCommand && (
            <span
              className="inline-flex items-center gap-1 text-xs text-fg-subtle"
              title="Diese Nachricht kam als Antwort per E-Mail"
            >
              <Mail className="size-3.5" aria-hidden="true" />
              per E-Mail
            </span>
          )}

          <time
            dateTime={n.createdAt.toISOString()}
            title={formatDateTime(n.createdAt)}
            className="shrink-0 text-xs text-fg-subtle"
          >
            {formatRelative(n.createdAt)}
          </time>
        </header>

        <div className="px-4 py-3">
          {/* Der Text kommt als reiner Text aus der Datenbank und wird hier
              ohne HTML-Auswertung ausgegeben – so kann eine eingehende Mail
              kein Skript in die Oberfläche schmuggeln. */}
          <p className="whitespace-pre-wrap break-words text-sm leading-relaxed text-fg">
            {n.body}
          </p>

          {n.anhaenge.length > 0 && (
            <ul className="mt-3 flex flex-wrap gap-2">
              {n.anhaenge.map((a) => (
                <li key={a.id}>
                  <Link
                    href={`/api/uploads/${a.id}`}
                    className="inline-flex items-center gap-2 rounded-md border border-line bg-surface-2 px-2.5 py-1.5 text-xs text-fg-muted transition-colors hover:bg-surface-3 hover:text-fg"
                  >
                    <Paperclip className="size-3.5 shrink-0" aria-hidden="true" />
                    <span className="max-w-52 truncate">{a.filename}</span>
                    <span className="shrink-0 text-fg-subtle">
                      {formatBytes(a.size)}
                    </span>
                  </Link>
                </li>
              ))}
            </ul>
          )}
        </div>

        {!intern && n.emailSentAt && (
          <footer className="border-t border-line px-4 py-1.5">
            <p className="flex items-center gap-1.5 text-xs text-fg-subtle">
              <Mail className="size-3.5" aria-hidden="true" />
              Per E-Mail zugestellt am {formatDateTime(n.emailSentAt)}
            </p>
          </footer>
        )}
      </article>
    </li>
  );
}

function EreignisEintrag({ ereignis: e }: { ereignis: VerlaufEreignis }) {
  const wichtig = WICHTIGE_EREIGNISSE.includes(e.type);
  const Icon = EVENT_ICON[e.type];

  return (
    <li>
      <div
        className={cn(
          "flex items-start gap-2.5 px-1 text-sm",
          wichtig && "rounded-lg border border-line bg-surface-2 px-4 py-2.5",
        )}
      >
        {Icon ? (
          <Icon
            className={cn(
              "mt-0.5 size-4 shrink-0",
              e.type === EventType.SLA_BREACHED ||
                e.type === EventType.EMAIL_COMMAND_REJECTED
                ? "text-danger"
                : "text-fg-subtle",
            )}
            aria-hidden="true"
          />
        ) : (
          <span
            className="mt-2 size-1.5 shrink-0 rounded-full bg-line-strong"
            aria-hidden="true"
          />
        )}

        <div className="min-w-0 flex-1">
          <p className="text-fg-muted">
            {e.actorName && (
              <span className="font-medium text-fg">{e.actorName} </span>
            )}
            {describeEvent(e)}
            {e.verweisNummer && (
              <>
                {" "}
                <Link
                  href={`/tickets/${e.verweisNummer}`}
                  className="font-medium text-primary hover:underline"
                >
                  {e.verweisNummer}
                </Link>
              </>
            )}
          </p>

          {e.note && (
            <p className="mt-0.5 text-xs italic text-fg-subtle">„{e.note}“</p>
          )}

          <time
            dateTime={e.createdAt.toISOString()}
            title={formatDateTime(e.createdAt)}
            className="text-xs text-fg-subtle"
          >
            {formatRelative(e.createdAt)}
          </time>
        </div>
      </div>
    </li>
  );
}
