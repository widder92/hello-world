"use client";

import { useState, useTransition } from "react";
import { useRouter } from "next/navigation";
import { AlertCircle, Pencil } from "lucide-react";

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
import { eingangAendernAction } from "@/server/actions/verwaltung";

export function EingangDialog({
  eingang,
  bereichName,
}: {
  eingang: {
    id: string;
    name: string;
    inboundEmail: string | null;
    portalVisible: boolean;
    isActive: boolean;
  };
  bereichName: string;
}) {
  const router = useRouter();
  const [offen, setOffen] = useState(false);
  const [pending, startTransition] = useTransition();
  const [fehler, setFehler] = useState<string | null>(null);

  const [form, setForm] = useState({
    name: eingang.name,
    inboundEmail: eingang.inboundEmail ?? "",
    portalVisible: eingang.portalVisible,
    isActive: eingang.isActive,
  });

  function absenden() {
    setFehler(null);
    startTransition(async () => {
      try {
        await eingangAendernAction(eingang.id, form);
        setOffen(false);
        router.refresh();
      } catch (e) {
        setFehler(
          e instanceof Error ? e.message : "Der Eingang konnte nicht gespeichert werden.",
        );
      }
    });
  }

  return (
    <Dialog open={offen} onOpenChange={setOffen}>
      <DialogTrigger asChild>
        <Button variant="secondary" size="sm">
          <Pencil aria-hidden="true" />
          Bearbeiten
        </Button>
      </DialogTrigger>

      <DialogContent size="sm">
        <DialogHeader>
          <DialogTitle>Eingang bearbeiten</DialogTitle>
          <DialogDescription>Im Bereich {bereichName}.</DialogDescription>
        </DialogHeader>

        <DialogBody className="space-y-4">
          <Field label="Name" htmlFor="eingang-name" required>
            <Input
              value={form.name}
              onChange={(e) => setForm((f) => ({ ...f, name: e.target.value }))}
              maxLength={80}
            />
          </Field>

          <Field
            label="Empfangsadresse"
            htmlFor="eingang-mail"
            help="Mails an diese Adresse werden zu Vorgängen in diesem Eingang. Das Postfach dazu richten Sie unter Postfächer ein."
          >
            <Input
              type="email"
              value={form.inboundEmail}
              onChange={(e) =>
                setForm((f) => ({ ...f, inboundEmail: e.target.value }))
              }
              placeholder="hausmeister-wohnheim-nord@example.org"
            />
          </Field>

          <div className="flex items-start justify-between gap-4">
            <div>
              <p className="text-sm font-medium text-fg">Im Portal wählbar</p>
              <p className="mt-0.5 text-xs text-fg-muted">
                Melder können diesen Standort beim Melden auswählen.
              </p>
            </div>
            <Switch
              checked={form.portalVisible}
              onCheckedChange={(v) =>
                setForm((f) => ({ ...f, portalVisible: v }))
              }
              aria-label="Im Portal wählbar"
              className="mt-0.5 shrink-0"
            />
          </div>

          <div className="flex items-start justify-between gap-4">
            <div>
              <p className="text-sm font-medium text-fg">Eingang ist aktiv</p>
              <p className="mt-0.5 text-xs text-fg-muted">
                Stillgelegte Eingänge nehmen nichts Neues an. Die dort liegenden
                Vorgänge bleiben erhalten und erreichbar.
              </p>
            </div>
            <Switch
              checked={form.isActive}
              onCheckedChange={(v) => setForm((f) => ({ ...f, isActive: v }))}
              aria-label="Eingang aktiv"
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
