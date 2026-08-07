"use client";

import { useEffect, useState, useTransition } from "react";
import { useRouter } from "next/navigation";
import { Plus, X } from "lucide-react";

import { Avatar } from "@/components/ui/avatar";
import { Button } from "@/components/ui/button";
import {
  DropdownMenu,
  DropdownMenuContent,
  DropdownMenuItem,
  DropdownMenuLabel,
  DropdownMenuTrigger,
} from "@/components/ui/dropdown";
import { Input } from "@/components/ui/input";
import { beobachterAction, personenSuchen } from "@/server/actions/tickets";

interface Person {
  id: string;
  name: string;
  email: string;
}

/**
 * Beobachter (Kopie).
 *
 * Wer hier steht, bekommt jede öffentliche Antwort mit – per Mail und in der
 * Glocke. Gedacht für Wohnbereichsleitungen, die über einen Vorgang in ihrem
 * Haus Bescheid wissen wollen, ohne ihn selbst zu bearbeiten.
 */
export function Beobachter({
  ticketId,
  beobachter,
  darfAendern,
}: {
  ticketId: string;
  beobachter: { userId: string; name: string }[];
  darfAendern: boolean;
}) {
  const router = useRouter();
  const [pending, startTransition] = useTransition();
  const [suche, setSuche] = useState("");
  const [treffer, setTreffer] = useState<Person[]>([]);

  // Erst ab drei Zeichen suchen und mit Verzögerung – bei 1000 Konten wäre
  // eine Abfrage je Tastendruck verschwenderisch.
  useEffect(() => {
    if (suche.trim().length < 3) {
      setTreffer([]);
      return;
    }
    const timer = setTimeout(() => {
      void personenSuchen(suche).then(setTreffer);
    }, 300);
    return () => clearTimeout(timer);
  }, [suche]);

  function aendern(userId: string, hinzufuegen: boolean) {
    startTransition(async () => {
      await beobachterAction(ticketId, userId, hinzufuegen);
      setSuche("");
      setTreffer([]);
      router.refresh();
    });
  }

  if (beobachter.length === 0 && !darfAendern) return null;

  return (
    <div>
      <div className="mb-1.5 flex items-center justify-between gap-2">
        <h3 className="text-xs font-semibold uppercase tracking-wide text-fg-subtle">
          Auf Kopie
        </h3>

        {darfAendern && (
          <DropdownMenu>
            <DropdownMenuTrigger asChild>
              <Button variant="ghost" size="icon-sm" disabled={pending}>
                <Plus aria-hidden="true" />
                <span className="sr-only">Person auf Kopie setzen</span>
              </Button>
            </DropdownMenuTrigger>

            <DropdownMenuContent align="end" className="w-72 p-0">
              <DropdownMenuLabel className="px-3 pt-2.5">
                Person suchen
              </DropdownMenuLabel>
              <div className="px-2 pb-2">
                <Input
                  value={suche}
                  onChange={(e) => setSuche(e.target.value)}
                  placeholder="Name oder E-Mail"
                  className="h-9"
                  autoFocus
                  onKeyDown={(e) => e.stopPropagation()}
                />
              </div>

              <div className="max-h-64 overflow-y-auto border-t border-line">
                {suche.trim().length > 0 && suche.trim().length < 3 && (
                  <p className="px-3 py-3 text-xs text-fg-subtle">
                    Bitte mindestens drei Zeichen eingeben.
                  </p>
                )}

                {treffer.length === 0 && suche.trim().length >= 3 && (
                  <p className="px-3 py-3 text-sm text-fg-muted">
                    Keine Person gefunden.
                  </p>
                )}

                {treffer
                  .filter((p) => !beobachter.some((b) => b.userId === p.id))
                  .map((p) => (
                    <DropdownMenuItem
                      key={p.id}
                      onSelect={() => aendern(p.id, true)}
                    >
                      <Avatar name={p.name} size="xs" />
                      <span className="min-w-0 flex-1">
                        <span className="block truncate">{p.name}</span>
                        <span className="block truncate text-xs text-fg-subtle">
                          {p.email}
                        </span>
                      </span>
                    </DropdownMenuItem>
                  ))}
              </div>
            </DropdownMenuContent>
          </DropdownMenu>
        )}
      </div>

      {beobachter.length === 0 ? (
        <p className="text-xs text-fg-subtle">Niemand auf Kopie.</p>
      ) : (
        <ul className="space-y-1">
          {beobachter.map((b) => (
            <li
              key={b.userId}
              className="flex items-center gap-1.5 text-xs text-fg-muted"
            >
              <Avatar name={b.name} size="xs" />
              <span className="min-w-0 flex-1 truncate">{b.name}</span>
              {darfAendern && (
                <button
                  type="button"
                  disabled={pending}
                  onClick={() => aendern(b.userId, false)}
                  className="rounded p-0.5 text-fg-subtle transition-colors hover:text-danger disabled:opacity-50"
                >
                  <X className="size-3.5" aria-hidden="true" />
                  <span className="sr-only">{b.name} von der Kopie nehmen</span>
                </button>
              )}
            </li>
          ))}
        </ul>
      )}
    </div>
  );
}
