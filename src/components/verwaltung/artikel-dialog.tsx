"use client";

import { useState, useTransition } from "react";
import { useRouter } from "next/navigation";
import { ArticleVisibility } from "@prisma/client";
import { AlertCircle, Eye, Pencil, Plus } from "lucide-react";

import { Markdown } from "@/components/ui/markdown";
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
import { Switch, Tabs, TabsContent, TabsList, TabsTrigger } from "@/components/ui/misc";
import { artikelSpeichernAction } from "@/server/actions/verwaltung";

/**
 * Wissensartikel schreiben.
 *
 * Mit Vorschau, weil Markdown ohne sie schwer einzuschätzen ist – und weil ein
 * Artikel, der im Portal falsch formatiert erscheint, mehr Rückfragen erzeugt
 * als er beantwortet.
 */
export function ArtikelDialog({
  bereiche,
  artikel,
}: {
  bereiche: { id: string; name: string }[];
  artikel?: {
    id: string;
    title: string;
    summary: string | null;
    body: string;
    teamId: string | null;
    visibility: ArticleVisibility;
    published: boolean;
  };
}) {
  const router = useRouter();
  const [offen, setOffen] = useState(false);
  const [pending, startTransition] = useTransition();
  const [fehler, setFehler] = useState<string | null>(null);

  const neu = !artikel;

  const [form, setForm] = useState({
    title: artikel?.title ?? "",
    summary: artikel?.summary ?? "",
    body: artikel?.body ?? "",
    teamId: artikel?.teamId ?? "",
    visibility: artikel?.visibility ?? ArticleVisibility.OEFFENTLICH,
    published: artikel?.published ?? false,
  });

  function setzen<K extends keyof typeof form>(feld: K, wert: (typeof form)[K]) {
    setForm((f) => ({ ...f, [feld]: wert }));
  }

  function absenden() {
    setFehler(null);
    startTransition(async () => {
      try {
        await artikelSpeichernAction({ ...form, id: artikel?.id });
        setOffen(false);
        router.refresh();
      } catch (e) {
        setFehler(
          e instanceof Error ? e.message : "Der Artikel konnte nicht gespeichert werden.",
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
            Artikel schreiben
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
            {neu ? "Neuen Artikel schreiben" : "Artikel bearbeiten"}
          </DialogTitle>
        </DialogHeader>

        <DialogBody className="space-y-4">
          <Field label="Titel" htmlFor="artikel-titel" required>
            <Input
              value={form.title}
              onChange={(e) => setzen("title", e.target.value)}
              placeholder="Drucker druckt nicht – das können Sie selbst prüfen"
              maxLength={160}
            />
          </Field>

          <Field
            label="Kurzfassung"
            htmlFor="artikel-kurz"
            help="Ein Satz. Erscheint in der Übersicht und im Vorschlag beim Melden."
          >
            <Input
              value={form.summary}
              onChange={(e) => setzen("summary", e.target.value)}
              maxLength={300}
            />
          </Field>

          <div className="grid gap-4 sm:grid-cols-2">
            <Field
              label="Bereich"
              htmlFor="artikel-bereich"
              help="Leer lassen für allgemeine Artikel."
            >
              <Select
                value={form.teamId}
                onChange={(e) => setzen("teamId", e.target.value)}
              >
                <option value="">Kein Bereich</option>
                {bereiche.map((b) => (
                  <option key={b.id} value={b.id}>
                    {b.name}
                  </option>
                ))}
              </Select>
            </Field>

            <Field label="Sichtbarkeit" htmlFor="artikel-sichtbarkeit" required>
              <Select
                value={form.visibility}
                onChange={(e) =>
                  setzen("visibility", e.target.value as ArticleVisibility)
                }
              >
                <option value={ArticleVisibility.OEFFENTLICH}>
                  Öffentlich – alle Mitarbeitenden
                </option>
                <option value={ArticleVisibility.INTERN}>
                  Intern – nur Bereichsmitglieder
                </option>
              </Select>
            </Field>
          </div>

          <Tabs defaultValue="schreiben">
            <TabsList>
              <TabsTrigger value="schreiben">
                <Pencil className="size-3.5" aria-hidden="true" />
                Schreiben
              </TabsTrigger>
              <TabsTrigger value="vorschau">
                <Eye className="size-3.5" aria-hidden="true" />
                Vorschau
              </TabsTrigger>
            </TabsList>

            <TabsContent value="schreiben" className="mt-3">
              <label htmlFor="artikel-text" className="sr-only">
                Artikeltext
              </label>
              <Textarea
                id="artikel-text"
                value={form.body}
                onChange={(e) => setzen("body", e.target.value)}
                rows={16}
                className="font-mono text-sm"
                placeholder={
                  "## Überschrift\n\nFließtext mit **fett** und *kursiv*.\n\n" +
                  "1. Erster Schritt\n2. Zweiter Schritt\n\n" +
                  "- Aufzählung\n- Noch ein Punkt\n\n" +
                  "> Wichtiger Hinweis"
                }
              />
              <p className="mt-1.5 text-xs text-fg-subtle">
                Markdown: ## für Überschriften, **fett**, *kursiv*, - für
                Aufzählungen, 1. für nummerierte Listen, &gt; für Hinweise.
              </p>
            </TabsContent>

            <TabsContent value="vorschau" className="mt-3">
              <div className="min-h-64 rounded-lg border border-line bg-surface-2 p-4">
                {form.body ? (
                  <Markdown text={form.body} />
                ) : (
                  <p className="text-sm text-fg-subtle">
                    Noch kein Text zum Anzeigen.
                  </p>
                )}
              </div>
            </TabsContent>
          </Tabs>

          <div className="flex items-start justify-between gap-4 rounded-lg border border-line p-3">
            <div>
              <p className="text-sm font-medium text-fg">Veröffentlicht</p>
              <p className="mt-0.5 text-xs text-fg-muted">
                Nur veröffentlichte Artikel erscheinen in der Wissensdatenbank
                und werden beim Melden vorgeschlagen.
              </p>
            </div>
            <Switch
              checked={form.published}
              onCheckedChange={(v) => setzen("published", v)}
              aria-label="Veröffentlicht"
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
