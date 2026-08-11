"use client";

import { useState, useTransition } from "react";
import { useRouter } from "next/navigation";
import { AlertCircle, CalendarPlus, Pencil, Plus, X } from "lucide-react";

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
import { Field, Input } from "@/components/ui/input";
import { Switch } from "@/components/ui/misc";
import {
  dienstzeitSpeichernAction,
  feiertagAction,
} from "@/server/actions/verwaltung";
import { formatDate } from "@/lib/format";

type Plan = Record<string, [string, string][]>;

const WOCHENTAGE: [string, string][] = [
  ["1", "Montag"],
  ["2", "Dienstag"],
  ["3", "Mittwoch"],
  ["4", "Donnerstag"],
  ["5", "Freitag"],
  ["6", "Samstag"],
  ["7", "Sonntag"],
];

const LEER: Plan = {
  "1": [["08:00", "16:30"]],
  "2": [["08:00", "16:30"]],
  "3": [["08:00", "16:30"]],
  "4": [["08:00", "16:30"]],
  "5": [["08:00", "14:00"]],
  "6": [],
  "7": [],
};

/**
 * Dienstzeit bearbeiten.
 *
 * Je Tag lassen sich mehrere Zeitfenster hinterlegen – für Bereiche mit
 * Mittagspause oder geteiltem Dienst. Feiertage werden separat gepflegt, weil
 * sie sich jährlich ändern und die Wochenstruktur nicht berühren.
 */
export function DienstzeitDialog({
  dienstzeit,
  feiertage = [],
}: {
  dienstzeit?: {
    id: string;
    name: string;
    timezone: string;
    schedule: Plan;
    isDefault: boolean;
  };
  feiertage?: { id: string; date: string; name: string }[];
}) {
  const router = useRouter();
  const [offen, setOffen] = useState(false);
  const [pending, startTransition] = useTransition();
  const [fehler, setFehler] = useState<string | null>(null);

  const neu = !dienstzeit;

  const [name, setName] = useState(dienstzeit?.name ?? "");
  const [timezone, setTimezone] = useState(
    dienstzeit?.timezone ?? "Europe/Berlin",
  );
  const [isDefault, setIsDefault] = useState(dienstzeit?.isDefault ?? false);
  const [plan, setPlan] = useState<Plan>(dienstzeit?.schedule ?? LEER);

  const [feiertagDatum, setFeiertagDatum] = useState("");
  const [feiertagName, setFeiertagName] = useState("");

  function fensterAendern(
    tag: string,
    index: number,
    stelle: 0 | 1,
    wert: string,
  ) {
    setPlan((p) => {
      const tage = [...(p[tag] ?? [])];
      const fenster: [string, string] = [...tage[index]] as [string, string];
      fenster[stelle] = wert;
      tage[index] = fenster;
      return { ...p, [tag]: tage };
    });
  }

  function fensterHinzufuegen(tag: string) {
    setPlan((p) => ({ ...p, [tag]: [...(p[tag] ?? []), ["08:00", "16:00"]] }));
  }

  function fensterEntfernen(tag: string, index: number) {
    setPlan((p) => ({
      ...p,
      [tag]: (p[tag] ?? []).filter((_, i) => i !== index),
    }));
  }

  function absenden() {
    setFehler(null);
    startTransition(async () => {
      try {
        await dienstzeitSpeichernAction({
          id: dienstzeit?.id,
          name,
          timezone,
          schedule: plan,
          isDefault,
        });
        setOffen(false);
        router.refresh();
      } catch (e) {
        setFehler(
          e instanceof Error ? e.message : "Die Dienstzeit konnte nicht gespeichert werden.",
        );
      }
    });
  }

  function feiertagSetzen() {
    if (!dienstzeit || !feiertagDatum) return;
    setFehler(null);

    startTransition(async () => {
      try {
        await feiertagAction(dienstzeit.id, feiertagDatum, feiertagName);
        setFeiertagDatum("");
        setFeiertagName("");
        router.refresh();
      } catch (e) {
        setFehler(
          e instanceof Error ? e.message : "Der Feiertag konnte nicht gespeichert werden.",
        );
      }
    });
  }

  return (
    <Dialog open={offen} onOpenChange={setOffen}>
      <DialogTrigger asChild>
        {neu ? (
          <Button size="sm">
            <Plus aria-hidden="true" />
            Dienstzeit anlegen
          </Button>
        ) : (
          <Button variant="secondary" size="sm">
            <Pencil aria-hidden="true" />
            Bearbeiten
          </Button>
        )}
      </DialogTrigger>

      <DialogContent size="lg">
        <DialogHeader>
          <DialogTitle>
            {neu ? "Neue Dienstzeit anlegen" : `Dienstzeit ${dienstzeit.name}`}
          </DialogTitle>
          <DialogDescription>
            Innerhalb dieser Zeiten laufen die Fristen. Mehrere Fenster je Tag
            sind möglich – etwa bei geteiltem Dienst.
          </DialogDescription>
        </DialogHeader>

        <DialogBody className="space-y-5">
          <div className="grid gap-4 sm:grid-cols-2">
            <Field label="Name" htmlFor="dz-name" required>
              <Input
                value={name}
                onChange={(e) => setName(e.target.value)}
                placeholder="Regeldienstzeit"
                maxLength={80}
              />
            </Field>
            <Field
              label="Zeitzone"
              htmlFor="dz-zeitzone"
              required
              help="Damit die Sommerzeitumstellung die Fristen nicht verschiebt."
            >
              <Input
                value={timezone}
                onChange={(e) => setTimezone(e.target.value)}
                placeholder="Europe/Berlin"
              />
            </Field>
          </div>

          <section>
            <h3 className="mb-2 text-sm font-semibold text-fg">Wochenplan</h3>
            <div className="space-y-1.5">
              {WOCHENTAGE.map(([nummer, tagName]) => {
                const fenster = plan[nummer] ?? [];
                return (
                  <div
                    key={nummer}
                    className="flex flex-wrap items-center gap-2 rounded-md border border-line px-3 py-2"
                  >
                    <span className="w-24 shrink-0 text-sm font-medium text-fg">
                      {tagName}
                    </span>

                    {fenster.length === 0 && (
                      <span className="text-sm text-fg-subtle">frei</span>
                    )}

                    {fenster.map((f, i) => (
                      <span key={i} className="flex items-center gap-1">
                        <Input
                          type="time"
                          value={f[0]}
                          onChange={(e) =>
                            fensterAendern(nummer, i, 0, e.target.value)
                          }
                          className="h-8 w-28"
                          aria-label={`${tagName}, Beginn`}
                        />
                        <span className="text-fg-subtle" aria-hidden="true">
                          –
                        </span>
                        <Input
                          type="time"
                          value={f[1]}
                          onChange={(e) =>
                            fensterAendern(nummer, i, 1, e.target.value)
                          }
                          className="h-8 w-28"
                          aria-label={`${tagName}, Ende`}
                        />
                        <button
                          type="button"
                          onClick={() => fensterEntfernen(nummer, i)}
                          className="rounded p-1 text-fg-subtle hover:bg-surface-2 hover:text-danger"
                        >
                          <X className="size-3.5" aria-hidden="true" />
                          <span className="sr-only">
                            Zeitfenster am {tagName} entfernen
                          </span>
                        </button>
                      </span>
                    ))}

                    <button
                      type="button"
                      onClick={() => fensterHinzufuegen(nummer)}
                      className="ml-auto rounded p-1 text-fg-subtle hover:bg-surface-2 hover:text-fg"
                    >
                      <Plus className="size-4" aria-hidden="true" />
                      <span className="sr-only">
                        Zeitfenster am {tagName} hinzufügen
                      </span>
                    </button>
                  </div>
                );
              })}
            </div>
          </section>

          <div className="flex items-start justify-between gap-4 rounded-lg border border-line p-3">
            <div>
              <p className="text-sm font-medium text-fg">Als Vorgabe verwenden</p>
              <p className="mt-0.5 text-xs text-fg-muted">
                Neue Fristen bekommen diese Dienstzeit vorausgewählt. Es gibt
                immer genau eine Vorgabe.
              </p>
            </div>
            <Switch
              checked={isDefault}
              onCheckedChange={setIsDefault}
              aria-label="Als Vorgabe verwenden"
              className="mt-0.5 shrink-0"
            />
          </div>

          {/* --- Feiertage (nur bei bestehender Dienstzeit) ------------- */}
          {dienstzeit && (
            <section>
              <h3 className="mb-2 text-sm font-semibold text-fg">Feiertage</h3>
              <p className="mb-2 text-xs text-fg-muted">
                An diesen Tagen steht die Uhr still, auch wenn im Wochenplan
                Dienstzeit steht.
              </p>

              <div className="flex flex-wrap items-end gap-2">
                <Field label="Datum" htmlFor="feiertag-datum" className="w-40">
                  <Input
                    type="date"
                    value={feiertagDatum}
                    onChange={(e) => setFeiertagDatum(e.target.value)}
                  />
                </Field>
                <Field label="Bezeichnung" htmlFor="feiertag-name" className="flex-1">
                  <Input
                    value={feiertagName}
                    onChange={(e) => setFeiertagName(e.target.value)}
                    placeholder="Tag der Deutschen Einheit"
                  />
                </Field>
                <Button
                  variant="secondary"
                  onClick={feiertagSetzen}
                  loading={pending}
                  disabled={!feiertagDatum}
                >
                  <CalendarPlus aria-hidden="true" />
                  Hinzufügen
                </Button>
              </div>

              {feiertage.length > 0 && (
                <ul className="mt-2.5 flex flex-wrap gap-1.5">
                  {feiertage.map((h) => (
                    <li
                      key={h.id}
                      className="flex items-center gap-1.5 rounded border border-line bg-surface-2 px-2 py-1 text-xs"
                    >
                      <span className="text-fg">{formatDate(h.date)}</span>
                      <span className="text-fg-muted">{h.name}</span>
                      <button
                        type="button"
                        disabled={pending}
                        onClick={() =>
                          startTransition(async () => {
                            await feiertagAction(
                              dienstzeit.id,
                              h.date,
                              h.name,
                              true,
                            );
                            router.refresh();
                          })
                        }
                        className="rounded p-0.5 text-fg-subtle hover:text-danger"
                      >
                        <X className="size-3" aria-hidden="true" />
                        <span className="sr-only">{h.name} entfernen</span>
                      </button>
                    </li>
                  ))}
                </ul>
              )}
            </section>
          )}

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
          <Button variant="ghost" onClick={() => setOffen(false)} disabled={pending}>
            Schließen
          </Button>
          <Button onClick={absenden} loading={pending}>
            Speichern
          </Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  );
}
