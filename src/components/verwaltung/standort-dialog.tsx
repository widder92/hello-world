"use client";

import { useState, useTransition } from "react";
import { useRouter } from "next/navigation";
import { SiteType } from "@prisma/client";
import { AlertCircle, Building2, Pencil } from "lucide-react";

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
import { standortSpeichernAction } from "@/server/actions/verwaltung";
import { SITE_TYPE } from "@/lib/domain";

export function StandortDialog({
  standort,
}: {
  standort?: {
    id: string;
    name: string;
    shortName: string | null;
    type: SiteType;
    street: string | null;
    zip: string | null;
    city: string | null;
    isActive: boolean;
  };
}) {
  const router = useRouter();
  const [offen, setOffen] = useState(false);
  const [pending, startTransition] = useTransition();
  const [fehler, setFehler] = useState<string | null>(null);

  const neu = !standort;

  const [form, setForm] = useState({
    name: standort?.name ?? "",
    shortName: standort?.shortName ?? "",
    type: standort?.type ?? SiteType.SONSTIGE,
    street: standort?.street ?? "",
    zip: standort?.zip ?? "",
    city: standort?.city ?? "",
    isActive: standort?.isActive ?? true,
  });

  function setzen<K extends keyof typeof form>(feld: K, wert: (typeof form)[K]) {
    setForm((f) => ({ ...f, [feld]: wert }));
  }

  function absenden() {
    setFehler(null);
    startTransition(async () => {
      try {
        await standortSpeichernAction({ ...form, id: standort?.id });
        setOffen(false);
        router.refresh();
      } catch (e) {
        setFehler(
          e instanceof Error ? e.message : "Der Standort konnte nicht gespeichert werden.",
        );
      }
    });
  }

  return (
    <Dialog open={offen} onOpenChange={setOffen}>
      <DialogTrigger asChild>
        {neu ? (
          <Button size="sm">
            <Building2 aria-hidden="true" />
            Standort anlegen
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
          <DialogTitle>
            {neu ? "Neuen Standort anlegen" : `Standort ${standort.name}`}
          </DialogTitle>
          {neu && (
            <DialogDescription>
              In allen Bereichen mit Eingang je Standort entsteht automatisch
              ein neuer Eingang.
            </DialogDescription>
          )}
        </DialogHeader>

        <DialogBody className="space-y-4">
          <Field label="Name" htmlFor="standort-name" required>
            <Input
              value={form.name}
              onChange={(e) => setzen("name", e.target.value)}
              placeholder="Wohnheim Nord"
              maxLength={80}
            />
          </Field>

          <Field
            label="Kurzname"
            htmlFor="standort-kurz"
            help="Wird in Listen und Eingangsnamen verwendet, wo wenig Platz ist."
          >
            <Input
              value={form.shortName}
              onChange={(e) => setzen("shortName", e.target.value)}
              placeholder="Nord"
              maxLength={40}
            />
          </Field>

          <Field label="Art" htmlFor="standort-typ" required>
            <Select
              value={form.type}
              onChange={(e) => setzen("type", e.target.value as SiteType)}
            >
              {Object.entries(SITE_TYPE).map(([wert, label]) => (
                <option key={wert} value={wert}>
                  {label}
                </option>
              ))}
            </Select>
          </Field>

          <Field label="Straße und Hausnummer" htmlFor="standort-strasse">
            <Input
              value={form.street}
              onChange={(e) => setzen("street", e.target.value)}
              maxLength={120}
            />
          </Field>

          <div className="grid gap-4 sm:grid-cols-[7rem_1fr]">
            <Field label="PLZ" htmlFor="standort-plz">
              <Input
                value={form.zip}
                onChange={(e) => setzen("zip", e.target.value)}
                maxLength={10}
              />
            </Field>
            <Field label="Ort" htmlFor="standort-ort">
              <Input
                value={form.city}
                onChange={(e) => setzen("city", e.target.value)}
                maxLength={80}
              />
            </Field>
          </div>

          <div className="flex items-start justify-between gap-4">
            <div>
              <p className="text-sm font-medium text-fg">Standort ist aktiv</p>
              <p className="mt-0.5 text-xs text-fg-muted">
                Nicht aktive Standorte lassen sich beim Melden nicht mehr
                auswählen. Bestehende Vorgänge bleiben erhalten.
              </p>
            </div>
            <Switch
              checked={form.isActive}
              onCheckedChange={(v) => setzen("isActive", v)}
              aria-label="Standort aktiv"
              className="mt-0.5 shrink-0"
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
