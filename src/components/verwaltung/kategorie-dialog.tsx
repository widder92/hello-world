"use client";

import { useState, useTransition } from "react";
import { useRouter } from "next/navigation";
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
import { Field, Input, Select, Textarea } from "@/components/ui/input";
import { Switch } from "@/components/ui/misc";
import { kategorieSpeichernAction } from "@/server/actions/verwaltung";

export function KategorieDialog({
  teamId,
  teamName,
  oberkategorien,
  kategorie,
}: {
  teamId: string;
  teamName: string;
  oberkategorien: { id: string; name: string }[];
  kategorie?: {
    id: string;
    name: string;
    description: string | null;
    parentId: string | null;
    isActive: boolean;
  };
}) {
  const router = useRouter();
  const [offen, setOffen] = useState(false);
  const [pending, startTransition] = useTransition();
  const [fehler, setFehler] = useState<string | null>(null);

  const neu = !kategorie;

  const [form, setForm] = useState({
    name: kategorie?.name ?? "",
    description: kategorie?.description ?? "",
    parentId: kategorie?.parentId ?? "",
    isActive: kategorie?.isActive ?? true,
  });

  function absenden() {
    setFehler(null);
    startTransition(async () => {
      try {
        await kategorieSpeichernAction({ ...form, teamId, id: kategorie?.id });
        setOffen(false);
        router.refresh();
      } catch (e) {
        setFehler(
          e instanceof Error ? e.message : "Die Kategorie konnte nicht gespeichert werden.",
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
            Kategorie
          </Button>
        ) : (
          <Button variant="ghost" size="icon-sm">
            <Pencil aria-hidden="true" />
            <span className="sr-only">{kategorie.name} bearbeiten</span>
          </Button>
        )}
      </DialogTrigger>

      <DialogContent size="sm">
        <DialogHeader>
          <DialogTitle>
            {neu ? "Neue Kategorie" : `Kategorie ${kategorie.name}`}
          </DialogTitle>
          <DialogDescription>Im Bereich {teamName}.</DialogDescription>
        </DialogHeader>

        <DialogBody className="space-y-4">
          <Field label="Name" htmlFor="kat-name" required>
            <Input
              value={form.name}
              onChange={(e) => setForm((f) => ({ ...f, name: e.target.value }))}
              maxLength={80}
            />
          </Field>

          <Field
            label="Beschreibung"
            htmlFor="kat-beschreibung"
            help="Erscheint im Meldeformular unter dem Namen."
          >
            <Textarea
              value={form.description}
              onChange={(e) =>
                setForm((f) => ({ ...f, description: e.target.value }))
              }
              rows={2}
              maxLength={300}
            />
          </Field>

          {oberkategorien.length > 0 && (
            <Field
              label="Unterkategorie von"
              htmlFor="kat-eltern"
              help="Leer lassen für eine Hauptkategorie."
            >
              <Select
                value={form.parentId}
                onChange={(e) =>
                  setForm((f) => ({ ...f, parentId: e.target.value }))
                }
              >
                <option value="">Hauptkategorie</option>
                {oberkategorien.map((o) => (
                  <option key={o.id} value={o.id}>
                    {o.name}
                  </option>
                ))}
              </Select>
            </Field>
          )}

          <div className="flex items-start justify-between gap-4">
            <div>
              <p className="text-sm font-medium text-fg">Kategorie ist aktiv</p>
              <p className="mt-0.5 text-xs text-fg-muted">
                Nicht aktive Kategorien lassen sich nicht mehr auswählen.
                Bestehende Vorgänge behalten sie.
              </p>
            </div>
            <Switch
              checked={form.isActive}
              onCheckedChange={(v) => setForm((f) => ({ ...f, isActive: v }))}
              aria-label="Kategorie aktiv"
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
