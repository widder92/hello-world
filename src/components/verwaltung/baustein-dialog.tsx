"use client";

import { useState, useTransition } from "react";
import { useRouter } from "next/navigation";
import { AlertCircle, Pencil, Plus } from "lucide-react";

import { Button } from "@/components/ui/button";
import {
  Dialog,
  DialogBody,
  DialogContent,
  DialogFooter,
  DialogHeader,
  DialogTitle,
  DialogTrigger,
} from "@/components/ui/dialog";
import { Field, Input, Select, Textarea } from "@/components/ui/input";
import { Switch } from "@/components/ui/misc";
import { bausteinSpeichernAction } from "@/server/actions/verwaltung";

export function BausteinDialog({
  bereiche,
  baustein,
}: {
  bereiche: { id: string; name: string }[];
  baustein?: {
    id: string;
    teamId: string | null;
    title: string;
    shortcut: string | null;
    body: string;
    isActive: boolean;
  };
}) {
  const router = useRouter();
  const [offen, setOffen] = useState(false);
  const [pending, startTransition] = useTransition();
  const [fehler, setFehler] = useState<string | null>(null);

  const neu = !baustein;

  const [form, setForm] = useState({
    teamId: baustein?.teamId ?? "",
    title: baustein?.title ?? "",
    shortcut: baustein?.shortcut ?? "",
    body: baustein?.body ?? "",
    isActive: baustein?.isActive ?? true,
  });

  function absenden() {
    setFehler(null);
    startTransition(async () => {
      try {
        await bausteinSpeichernAction({ ...form, id: baustein?.id });
        setOffen(false);
        router.refresh();
      } catch (e) {
        setFehler(
          e instanceof Error ? e.message : "Der Baustein konnte nicht gespeichert werden.",
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
            Baustein anlegen
          </Button>
        ) : (
          <Button variant="secondary" size="sm">
            <Pencil aria-hidden="true" />
            Bearbeiten
          </Button>
        )}
      </DialogTrigger>

      <DialogContent>
        <DialogHeader>
          <DialogTitle>
            {neu ? "Neuen Textbaustein anlegen" : "Textbaustein bearbeiten"}
          </DialogTitle>
        </DialogHeader>

        <DialogBody className="space-y-4">
          <Field label="Titel" htmlFor="baustein-titel" required>
            <Input
              value={form.title}
              onChange={(e) => setForm((f) => ({ ...f, title: e.target.value }))}
              placeholder="Neustart erbitten"
              maxLength={80}
            />
          </Field>

          <div className="grid gap-4 sm:grid-cols-2">
            <Field
              label="Bereich"
              htmlFor="baustein-bereich"
              help="Leer lassen für alle Bereiche."
            >
              <Select
                value={form.teamId}
                onChange={(e) =>
                  setForm((f) => ({ ...f, teamId: e.target.value }))
                }
              >
                <option value="">Alle Bereiche</option>
                {bereiche.map((b) => (
                  <option key={b.id} value={b.id}>
                    {b.name}
                  </option>
                ))}
              </Select>
            </Field>

            <Field
              label="Kurzbefehl"
              htmlFor="baustein-kurz"
              help="Optional, zum schnellen Finden."
            >
              <Input
                value={form.shortcut}
                onChange={(e) =>
                  setForm((f) => ({ ...f, shortcut: e.target.value }))
                }
                placeholder="/neustart"
                className="font-mono"
                maxLength={30}
              />
            </Field>
          </div>

          <Field
            label="Text"
            htmlFor="baustein-text"
            required
            help="Wird beim Einfügen an den vorhandenen Text angehängt."
          >
            <Textarea
              value={form.body}
              onChange={(e) => setForm((f) => ({ ...f, body: e.target.value }))}
              rows={8}
            />
          </Field>

          <div className="flex items-center justify-between gap-4">
            <span className="text-sm text-fg">Baustein ist aktiv</span>
            <Switch
              checked={form.isActive}
              onCheckedChange={(v) => setForm((f) => ({ ...f, isActive: v }))}
              aria-label="Baustein aktiv"
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
