"use client";

import { useState, useTransition } from "react";
import { useRouter } from "next/navigation";
import { TeamMode, TeamRole } from "@prisma/client";
import { AlertCircle, Home, Users } from "lucide-react";

import { Button } from "@/components/ui/button";
import {
  Dialog,
  DialogBody,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
  DialogTrigger,
} from "@/components/ui/dialog";
import { Select } from "@/components/ui/input";
import { Checkbox } from "@/components/ui/misc";
import {
  eingangsFreigabeAction,
  mitgliedschaftAction,
} from "@/server/actions/verwaltung";
import { cn } from "@/lib/utils";

interface BereichMitEingaengen {
  id: string;
  name: string;
  color: string;
  mode: TeamMode;
  queues: {
    id: string;
    name: string;
    site: { name: string; shortName: string | null } | null;
  }[];
}

/**
 * Mitgliedschaften und Eingangsfreigaben einer Person.
 *
 * Zwei Ebenen, die zusammenwirken und deshalb in einem Dialog stehen:
 *
 *   Mitgliedschaft   Darf diese Person im Bereich überhaupt arbeiten?
 *   Freigabe         Welche Standort-Eingänge sieht sie dort?
 *
 * Die Rechteprüfung verlangt beides. Eine Freigabe ohne Mitgliedschaft wäre
 * wirkungslos – deshalb erscheinen die Eingänge erst, wenn eine Rolle gesetzt
 * ist, und die Server-Aktion lehnt den Fall zusätzlich ab.
 */
export function MitgliedschaftDialog({
  benutzer,
  bereiche,
}: {
  benutzer: {
    id: string;
    name: string;
    memberships: { teamId: string; role: TeamRole }[];
    queueAccess: { queueId: string; isDefault: boolean }[];
  };
  bereiche: BereichMitEingaengen[];
}) {
  const router = useRouter();
  const [offen, setOffen] = useState(false);
  const [pending, startTransition] = useTransition();
  const [fehler, setFehler] = useState<string | null>(null);

  function rolleFuer(teamId: string): TeamRole | "" {
    return benutzer.memberships.find((m) => m.teamId === teamId)?.role ?? "";
  }

  function hatFreigabe(queueId: string): boolean {
    return benutzer.queueAccess.some((a) => a.queueId === queueId);
  }

  function istStartansicht(queueId: string): boolean {
    return benutzer.queueAccess.some((a) => a.queueId === queueId && a.isDefault);
  }

  function fuehreAus(fn: () => Promise<unknown>) {
    setFehler(null);
    startTransition(async () => {
      try {
        await fn();
        router.refresh();
      } catch (e) {
        setFehler(
          e instanceof Error ? e.message : "Die Änderung ist fehlgeschlagen.",
        );
      }
    });
  }

  return (
    <Dialog open={offen} onOpenChange={setOffen}>
      <DialogTrigger asChild>
        <Button variant="secondary" size="sm">
          <Users aria-hidden="true" />
          Bereiche
        </Button>
      </DialogTrigger>

      <DialogContent size="lg">
        <DialogHeader>
          <DialogTitle>Bereiche von {benutzer.name}</DialogTitle>
          <DialogDescription>
            Die Rolle im Bereich entscheidet, ob und wie diese Person dort
            arbeitet. Bei Bereichen mit mehreren Eingängen legen die Freigaben
            fest, welche Standorte sie sieht.
          </DialogDescription>
        </DialogHeader>

        <DialogBody className="space-y-4">
          {bereiche.map((b) => {
            const rolle = rolleFuer(b.id);
            const istMitglied = rolle !== "";
            const istLeitung = rolle === TeamRole.LEAD;
            const mehrereEingaenge = b.queues.length > 1;

            return (
              <div
                key={b.id}
                className={cn(
                  "rounded-lg border p-3.5",
                  istMitglied ? "border-primary/40 bg-primary-soft/30" : "border-line",
                )}
              >
                <div className="flex flex-wrap items-center justify-between gap-3">
                  <div className="flex items-center gap-2">
                    <span
                      className="size-2.5 rounded-full"
                      style={{ backgroundColor: b.color }}
                      aria-hidden="true"
                    />
                    <span className="font-medium text-fg">{b.name}</span>
                    <span className="text-xs text-fg-subtle">
                      {b.mode === TeamMode.VOLL
                        ? "voller Arbeitsplatz"
                        : "schlanke Ansicht"}
                    </span>
                  </div>

                  <div className="flex items-center gap-2">
                    <label
                      htmlFor={`rolle-${b.id}`}
                      className="text-xs text-fg-muted"
                    >
                      Rolle
                    </label>
                    <Select
                      id={`rolle-${b.id}`}
                      value={rolle}
                      disabled={pending}
                      className="h-9 w-44 text-sm"
                      onChange={(e) =>
                        fuehreAus(() =>
                          mitgliedschaftAction(
                            benutzer.id,
                            b.id,
                            e.target.value === ""
                              ? null
                              : (e.target.value as TeamRole),
                          ),
                        )
                      }
                    >
                      <option value="">Kein Mitglied</option>
                      <option value={TeamRole.MEMBER}>Mitglied</option>
                      <option value={TeamRole.DISPATCHER}>
                        Verteilung (darf zuteilen)
                      </option>
                      <option value={TeamRole.LEAD}>
                        Leitung (alle Eingänge)
                      </option>
                    </Select>
                  </div>
                </div>

                {istMitglied && mehrereEingaenge && (
                  <div className="mt-3 border-t border-line/60 pt-3">
                    {istLeitung ? (
                      <p className="text-xs text-fg-muted">
                        Als Leitung sieht {benutzer.name} automatisch alle
                        Eingänge dieses Bereichs – auch neu hinzukommende
                        Standorte.
                      </p>
                    ) : (
                      <>
                        <p className="mb-2 text-xs font-medium text-fg">
                          Freigegebene Standorte
                        </p>
                        <ul className="space-y-1">
                          {b.queues.map((q) => {
                            const frei = hatFreigabe(q.id);
                            const start = istStartansicht(q.id);
                            return (
                              <li
                                key={q.id}
                                className="flex flex-wrap items-center gap-2"
                              >
                                <label className="flex flex-1 cursor-pointer items-center gap-2 text-sm">
                                  <Checkbox
                                    checked={frei}
                                    disabled={pending}
                                    onCheckedChange={(v) =>
                                      fuehreAus(() =>
                                        eingangsFreigabeAction(
                                          benutzer.id,
                                          q.id,
                                          Boolean(v),
                                          false,
                                        ),
                                      )
                                    }
                                  />
                                  <span className="text-fg">
                                    {q.site?.shortName ?? q.site?.name ?? q.name}
                                  </span>
                                </label>

                                {frei && (
                                  <button
                                    type="button"
                                    disabled={pending || start}
                                    onClick={() =>
                                      fuehreAus(() =>
                                        eingangsFreigabeAction(
                                          benutzer.id,
                                          q.id,
                                          true,
                                          true,
                                        ),
                                      )
                                    }
                                    className={cn(
                                      "inline-flex items-center gap-1 rounded px-1.5 py-0.5 text-xs transition-colors",
                                      start
                                        ? "bg-primary-soft font-medium text-primary-soft-fg"
                                        : "text-fg-subtle hover:bg-surface-2 hover:text-fg",
                                    )}
                                  >
                                    <Home className="size-3" aria-hidden="true" />
                                    {start ? "Startansicht" : "als Startansicht"}
                                  </button>
                                )}
                              </li>
                            );
                          })}
                        </ul>
                      </>
                    )}
                  </div>
                )}
              </div>
            );
          })}

          {fehler && (
            <p
              role="alert"
              className="flex items-start gap-2 rounded-md border border-danger/30 bg-danger-soft px-3 py-2.5 text-sm text-danger-soft-fg"
            >
              <AlertCircle className="mt-0.5 size-4 shrink-0" aria-hidden="true" />
              {fehler}
            </p>
          )}
        </DialogBody>

        <DialogFooter>
          <Button onClick={() => setOffen(false)}>Fertig</Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  );
}
