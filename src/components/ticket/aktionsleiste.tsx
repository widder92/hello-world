"use client";

import { useState, useTransition } from "react";
import { useRouter } from "next/navigation";
import { Priority, TeamMode, TicketStatus } from "@prisma/client";
import { Check, ChevronDown, Hand, UserPlus, X } from "lucide-react";

import { Avatar } from "@/components/ui/avatar";
import { Button } from "@/components/ui/button";
import {
  DropdownMenu,
  DropdownMenuContent,
  DropdownMenuItem,
  DropdownMenuLabel,
  DropdownMenuSeparator,
  DropdownMenuTrigger,
} from "@/components/ui/dropdown";
import { StatusBadge, PriorityBadge } from "@/components/ticket/badges";
import {
  ladeZuweisbare,
  prioritaetAendernAction,
  statusAendernAction,
  uebernehmenAction,
  zuweisenAction,
} from "@/server/actions/tickets";
import { PRIORITY, STATUS, STATUS_EINFACH } from "@/lib/domain";

interface Person {
  id: string;
  name: string;
  email: string;
}

/**
 * Aktionsleiste über dem Verlauf: übernehmen, zuweisen, Status, Priorität.
 *
 * Was hier erscheint, richtet sich nach der Ausbaustufe des Bereichs. Ein
 * Bereich im Einfach-Modus bekommt Status als drei klare Wahlmöglichkeiten und
 * gar keine Priorität – nicht dieselbe Leiste mit ausgegrauten Knöpfen.
 */
export function Aktionsleiste({
  ticketId,
  status,
  prioritaet,
  bearbeiter,
  eigeneId,
  teamMode,
  statusAuswahl,
  darfZuweisen,
  darfAnAndereZuweisen,
}: {
  ticketId: string;
  status: TicketStatus;
  prioritaet: Priority;
  bearbeiter: { id: string; name: string } | null;
  eigeneId: string;
  teamMode: TeamMode;
  statusAuswahl: readonly string[];
  darfZuweisen: boolean;
  darfAnAndereZuweisen: boolean;
}) {
  const router = useRouter();
  const [pending, startTransition] = useTransition();
  const [personen, setPersonen] = useState<Person[] | null>(null);
  const [fehler, setFehler] = useState<string | null>(null);

  const einfach = teamMode === TeamMode.EINFACH;
  const istMeiner = bearbeiter?.id === eigeneId;

  function fuehreAus(fn: () => Promise<unknown>) {
    setFehler(null);
    startTransition(async () => {
      try {
        await fn();
        router.refresh();
      } catch (e) {
        setFehler(e instanceof Error ? e.message : "Die Aktion ist fehlgeschlagen.");
      }
    });
  }

  return (
    <div className="space-y-2">
      <div className="flex flex-wrap items-center gap-2">
        {/* --- Übernehmen / Zuweisung ---------------------------------- */}
        {darfZuweisen && !bearbeiter && (
          <Button
            size="sm"
            loading={pending}
            onClick={() => fuehreAus(() => uebernehmenAction(ticketId))}
          >
            <Hand aria-hidden="true" />
            Ticket ziehen
          </Button>
        )}

        {darfZuweisen && bearbeiter && (
          <ZuweisungMenue
            bearbeiter={bearbeiter}
            istMeiner={istMeiner}
            personen={personen}
            ladePersonen={() => {
              if (!personen) void ladeZuweisbare(ticketId).then(setPersonen);
            }}
            darfAnAndereZuweisen={darfAnAndereZuweisen}
            pending={pending}
            onZuweisen={(id) => fuehreAus(() => zuweisenAction(ticketId, id))}
          />
        )}

        {darfZuweisen && !bearbeiter && darfAnAndereZuweisen && (
          <ZuweisungMenue
            bearbeiter={null}
            istMeiner={false}
            personen={personen}
            ladePersonen={() => {
              if (!personen) void ladeZuweisbare(ticketId).then(setPersonen);
            }}
            darfAnAndereZuweisen
            pending={pending}
            onZuweisen={(id) => fuehreAus(() => zuweisenAction(ticketId, id))}
          />
        )}

        {/* --- Status --------------------------------------------------- */}
        <DropdownMenu>
          <DropdownMenuTrigger asChild>
            <Button variant="secondary" size="sm" disabled={pending}>
              <StatusBadge status={status} einfach={einfach} size="sm" />
              <ChevronDown aria-hidden="true" />
              <span className="sr-only">Status ändern</span>
            </Button>
          </DropdownMenuTrigger>
          <DropdownMenuContent align="start" className="w-64">
            <DropdownMenuLabel>Status setzen</DropdownMenuLabel>
            {statusAuswahl.map((s) => {
              const wert = s as TicketStatus;
              const d = STATUS[wert];
              const Icon = d.icon;
              const label = einfach ? (STATUS_EINFACH[wert] ?? d.label) : d.label;
              return (
                <DropdownMenuItem
                  key={s}
                  onSelect={() =>
                    fuehreAus(() => statusAendernAction(ticketId, wert))
                  }
                >
                  <Icon aria-hidden="true" />
                  <span className="flex-1">{label}</span>
                  {status === wert && (
                    <Check className="size-4 text-primary" aria-hidden="true" />
                  )}
                </DropdownMenuItem>
              );
            })}
          </DropdownMenuContent>
        </DropdownMenu>

        {/* --- Priorität (nur Voll-Modus) ------------------------------- */}
        {!einfach && (
          <DropdownMenu>
            <DropdownMenuTrigger asChild>
              <Button variant="secondary" size="sm" disabled={pending}>
                <PriorityBadge priority={prioritaet} size="sm" />
                <ChevronDown aria-hidden="true" />
                <span className="sr-only">Priorität ändern</span>
              </Button>
            </DropdownMenuTrigger>
            <DropdownMenuContent align="start" className="w-72">
              <DropdownMenuLabel>Priorität</DropdownMenuLabel>
              {Object.entries(PRIORITY).map(([wert, d]) => {
                const Icon = d.icon;
                return (
                  <DropdownMenuItem
                    key={wert}
                    onSelect={() =>
                      fuehreAus(() =>
                        prioritaetAendernAction(ticketId, wert as Priority),
                      )
                    }
                  >
                    <Icon aria-hidden="true" />
                    <span className="flex-1">
                      <span className="block">{d.label}</span>
                      <span className="block text-xs text-fg-subtle">
                        {d.description}
                      </span>
                    </span>
                    {prioritaet === wert && (
                      <Check className="size-4 text-primary" aria-hidden="true" />
                    )}
                  </DropdownMenuItem>
                );
              })}
            </DropdownMenuContent>
          </DropdownMenu>
        )}
      </div>

      {fehler && (
        <p
          role="alert"
          className="rounded-md border border-danger/30 bg-danger-soft px-3 py-2 text-sm text-danger-soft-fg"
        >
          {fehler}
        </p>
      )}
    </div>
  );
}

function ZuweisungMenue({
  bearbeiter,
  istMeiner,
  personen,
  ladePersonen,
  darfAnAndereZuweisen,
  pending,
  onZuweisen,
}: {
  bearbeiter: { id: string; name: string } | null;
  istMeiner: boolean;
  personen: Person[] | null;
  ladePersonen: () => void;
  darfAnAndereZuweisen: boolean;
  pending: boolean;
  onZuweisen: (id: string | null) => void;
}) {
  return (
    <DropdownMenu onOpenChange={(o) => o && ladePersonen()}>
      <DropdownMenuTrigger asChild>
        <Button variant="secondary" size="sm" disabled={pending}>
          {bearbeiter ? (
            <>
              <Avatar name={bearbeiter.name} size="xs" />
              {istMeiner ? "Bei mir" : bearbeiter.name}
            </>
          ) : (
            <>
              <UserPlus aria-hidden="true" />
              Zuweisen
            </>
          )}
          <ChevronDown aria-hidden="true" />
        </Button>
      </DropdownMenuTrigger>

      <DropdownMenuContent align="start" className="w-64">
        <DropdownMenuLabel>Bearbeitung</DropdownMenuLabel>

        {!darfAnAndereZuweisen && (
          <p className="px-2.5 py-2 text-xs text-fg-subtle">
            Andere zuzuteilen ist der Verteilung und der Bereichsleitung
            vorbehalten.
          </p>
        )}

        {darfAnAndereZuweisen && personen === null && (
          <p className="px-2.5 py-2 text-sm text-fg-muted">Wird geladen …</p>
        )}

        {darfAnAndereZuweisen &&
          personen?.map((p) => (
            <DropdownMenuItem key={p.id} onSelect={() => onZuweisen(p.id)}>
              <Avatar name={p.name} size="xs" />
              <span className="flex-1 truncate">{p.name}</span>
              {bearbeiter?.id === p.id && (
                <Check className="size-4 text-primary" aria-hidden="true" />
              )}
            </DropdownMenuItem>
          ))}

        {bearbeiter && (
          <>
            <DropdownMenuSeparator />
            <DropdownMenuItem onSelect={() => onZuweisen(null)}>
              <X aria-hidden="true" />
              Zuweisung aufheben
            </DropdownMenuItem>
          </>
        )}
      </DropdownMenuContent>
    </DropdownMenu>
  );
}
