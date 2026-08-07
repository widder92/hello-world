"use client";

import { useState, useTransition } from "react";
import { useRouter } from "next/navigation";
import { Priority } from "@prisma/client";
import { AlertCircle, Pencil, Plus } from "lucide-react";

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
import { Field, Input, Select } from "@/components/ui/input";
import { Switch } from "@/components/ui/misc";
import { slaSpeichernAction } from "@/server/actions/verwaltung";
import { PRIORITY } from "@/lib/domain";
import { formatDuration } from "@/lib/format";

/**
 * Frist anlegen oder ändern.
 *
 * Die Eingabe erfolgt in Stunden und Minuten getrennt, weil „480 Minuten"
 * niemand im Kopf in einen Arbeitstag übersetzt. Unter dem Feld steht
 * zusätzlich die Umrechnung im Klartext.
 */
export function SlaDialog({
  teamId,
  teamName,
  dienstzeiten,
  frist,
}: {
  teamId: string;
  teamName: string;
  dienstzeiten: { id: string; name: string; isDefault: boolean }[];
  frist?: {
    id: string;
    name: string;
    priority: Priority;
    firstResponseMinutes: number;
    resolutionMinutes: number;
    warningThreshold: number;
    businessHoursId: string;
    isActive: boolean;
  };
}) {
  const router = useRouter();
  const [offen, setOffen] = useState(false);
  const [pending, startTransition] = useTransition();
  const [fehler, setFehler] = useState<string | null>(null);

  const neu = !frist;

  const [form, setForm] = useState({
    priority: frist?.priority ?? Priority.NORMAL,
    name: frist?.name ?? `${teamName} – NORMAL`,
    firstResponseMinutes: frist?.firstResponseMinutes ?? 240,
    resolutionMinutes: frist?.resolutionMinutes ?? 1440,
    warningThreshold: frist?.warningThreshold ?? 0.8,
    businessHoursId:
      frist?.businessHoursId ??
      dienstzeiten.find((d) => d.isDefault)?.id ??
      dienstzeiten[0]?.id ??
      "",
    isActive: frist?.isActive ?? true,
  });

  function setzen<K extends keyof typeof form>(feld: K, wert: (typeof form)[K]) {
    setForm((f) => ({ ...f, [feld]: wert }));
  }

  function absenden() {
    setFehler(null);
    startTransition(async () => {
      try {
        await slaSpeichernAction({ ...form, teamId, id: frist?.id });
        setOffen(false);
        router.refresh();
      } catch (e) {
        setFehler(
          e instanceof Error ? e.message : "Die Frist konnte nicht gespeichert werden.",
        );
      }
    });
  }

  return (
    <Dialog open={offen} onOpenChange={setOffen}>
      <DialogTrigger asChild>
        {neu ? (
          <Button variant="secondary" size="sm">
            <Plus aria-hidden="true" />
            Frist hinzufügen
          </Button>
        ) : (
          <Button variant="secondary" size="sm">
            <Pencil aria-hidden="true" />
            Bearbeiten
          </Button>
        )}
      </DialogTrigger>

      <DialogContent size="sm">
        <DialogHeader>
          <DialogTitle>{neu ? "Frist hinzufügen" : "Frist bearbeiten"}</DialogTitle>
          <DialogDescription>
            Für den Bereich {teamName}. Die Zeit läuft nur während der gewählten
            Dienstzeit.
          </DialogDescription>
        </DialogHeader>

        <DialogBody className="space-y-4">
          <Field label="Priorität" htmlFor="sla-prio" required>
            <Select
              value={form.priority}
              onChange={(e) => {
                const p = e.target.value as Priority;
                setzen("priority", p);
                if (neu) setzen("name", `${teamName} – ${p}`);
              }}
              disabled={!neu}
            >
              {Object.entries(PRIORITY).map(([wert, d]) => (
                <option key={wert} value={wert}>
                  {d.label} – {d.description}
                </option>
              ))}
            </Select>
          </Field>

          <ZeitFeld
            label="Frist für die Erstreaktion"
            id="sla-erst"
            minuten={form.firstResponseMinutes}
            onChange={(m) => setzen("firstResponseMinutes", m)}
            help="Bis wann sich jemand meldet – nicht bis wann es gelöst ist."
          />

          <ZeitFeld
            label="Frist für die Lösung"
            id="sla-loesung"
            minuten={form.resolutionMinutes}
            onChange={(m) => setzen("resolutionMinutes", m)}
            help="Bis wann der Vorgang erledigt sein soll."
          />

          <Field
            label="Warnung ab"
            htmlFor="sla-schwelle"
            help="Ab welchem Anteil der Frist die Ampel auf Gelb springt."
          >
            <Select
              value={String(form.warningThreshold)}
              onChange={(e) => setzen("warningThreshold", Number(e.target.value))}
            >
              <option value="0.5">50 % der Frist</option>
              <option value="0.7">70 % der Frist</option>
              <option value="0.8">80 % der Frist</option>
              <option value="0.9">90 % der Frist</option>
            </Select>
          </Field>

          <Field label="Dienstzeit" htmlFor="sla-dienstzeit" required>
            <Select
              value={form.businessHoursId}
              onChange={(e) => setzen("businessHoursId", e.target.value)}
            >
              {dienstzeiten.map((d) => (
                <option key={d.id} value={d.id}>
                  {d.name}
                  {d.isDefault ? " (Vorgabe)" : ""}
                </option>
              ))}
            </Select>
          </Field>

          <div className="flex items-center justify-between gap-4">
            <span className="text-sm text-fg">Frist ist aktiv</span>
            <Switch
              checked={form.isActive}
              onCheckedChange={(v) => setzen("isActive", v)}
              aria-label="Frist aktiv"
            />
          </div>

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
            Abbrechen
          </Button>
          <Button onClick={absenden} loading={pending}>
            Speichern
          </Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  );
}

function ZeitFeld({
  label,
  id,
  minuten,
  onChange,
  help,
}: {
  label: string;
  id: string;
  minuten: number;
  onChange: (minuten: number) => void;
  help?: string;
}) {
  const stunden = Math.floor(minuten / 60);
  const rest = minuten % 60;

  return (
    <div className="space-y-1.5">
      <label htmlFor={`${id}-stunden`} className="block text-sm font-medium text-fg">
        {label}
      </label>
      {help && <p className="text-xs text-fg-muted">{help}</p>}

      <div className="flex items-center gap-2">
        <Input
          id={`${id}-stunden`}
          type="number"
          min={0}
          max={720}
          value={stunden}
          onChange={(e) => onChange(Number(e.target.value) * 60 + rest)}
          className="w-20"
          aria-label={`${label}, Stunden`}
        />
        <span className="text-sm text-fg-muted">Std.</span>

        <Input
          type="number"
          min={0}
          max={59}
          step={5}
          value={rest}
          onChange={(e) => onChange(stunden * 60 + Number(e.target.value))}
          className="w-20"
          aria-label={`${label}, Minuten`}
        />
        <span className="text-sm text-fg-muted">Min.</span>

        <span className="ml-1 text-xs text-fg-subtle">
          = {formatDuration(minuten)} Dienstzeit
        </span>
      </div>
    </div>
  );
}
