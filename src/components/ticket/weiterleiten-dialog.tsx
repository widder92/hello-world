"use client";

import { useEffect, useState, useTransition } from "react";
import { useRouter } from "next/navigation";
import { QueueMode } from "@prisma/client";
import { AlertCircle, ArrowRight, Check, Forward } from "lucide-react";

import { teamIcon } from "@/components/layout/icon";
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
import { Field, Select, Textarea } from "@/components/ui/input";
import { ladeZielBereiche, weiterleitenAction } from "@/server/actions/tickets";
import { cn, truncate } from "@/lib/utils";

interface ZielBereich {
  id: string;
  name: string;
  color: string;
  icon: string;
  queueMode: QueueMode;
  description: string | null;
  queues: {
    id: string;
    siteId: string | null;
    site: { id: string; name: string; shortName: string | null } | null;
  }[];
}

/**
 * Ganzen Vorgang an einen anderen Bereich geben.
 *
 * Der Fall „liegt beim falschen Bereich". Anders als beim Abspalten bleibt
 * nichts zurück: Verlauf, Anhänge und Melder wandern mit, die Zuweisung wird
 * aufgehoben, damit der Vorgang im Zielbereich im Sammeleingang landet.
 *
 * Die Begründung ist Pflicht. Ohne sie steht im Zielbereich ein Ticket, dem
 * man nicht ansieht, warum es dort liegt – und das kommt erfahrungsgemäß
 * postwendend zurück.
 */
export function WeiterleitenDialog({
  ticketId,
  ticketNummer,
  aktuellerStandortId,
  aktuellerBereich,
}: {
  ticketId: string;
  ticketNummer: string;
  aktuellerStandortId: string | null;
  aktuellerBereich: string;
}) {
  const router = useRouter();
  const [offen, setOffen] = useState(false);
  const [bereiche, setBereiche] = useState<ZielBereich[] | null>(null);
  const [pending, startTransition] = useTransition();
  const [fehler, setFehler] = useState<string | null>(null);

  const [teamId, setTeamId] = useState("");
  const [siteId, setSiteId] = useState(aktuellerStandortId ?? "");
  const [begruendung, setBegruendung] = useState("");

  useEffect(() => {
    if (!offen || bereiche) return;
    void ladeZielBereiche(ticketId).then((b) => setBereiche(b as ZielBereich[]));
  }, [offen, bereiche, ticketId]);

  const zielBereich = bereiche?.find((b) => b.id === teamId);
  const brauchtStandort = zielBereich?.queueMode === QueueMode.PER_SITE;
  const verfuegbareStandorte =
    zielBereich?.queues
      .map((q) => q.site)
      .filter((s): s is NonNullable<typeof s> => s !== null) ?? [];

  useEffect(() => {
    if (!brauchtStandort) return;
    if (!verfuegbareStandorte.some((s) => s.id === siteId)) {
      setSiteId(verfuegbareStandorte[0]?.id ?? "");
    }
  }, [brauchtStandort, siteId, verfuegbareStandorte]);

  function absenden() {
    setFehler(null);

    if (!teamId) {
      setFehler("Bitte wählen Sie den Bereich, an den der Vorgang gehen soll.");
      return;
    }
    if (begruendung.trim().length < 3) {
      setFehler(
        "Bitte geben Sie kurz an, warum Sie weiterleiten – sonst weiß der " +
          "andere Bereich nicht, was er damit soll.",
      );
      return;
    }

    startTransition(async () => {
      try {
        await weiterleitenAction({
          ticketId,
          zielTeamId: teamId,
          zielSiteId: brauchtStandort ? siteId : null,
          begruendung,
        });
        setOffen(false);
        router.refresh();
      } catch (e) {
        setFehler(
          e instanceof Error
            ? e.message
            : "Der Vorgang konnte nicht weitergeleitet werden.",
        );
      }
    });
  }

  return (
    <Dialog open={offen} onOpenChange={setOffen}>
      <DialogTrigger asChild>
        <Button variant="secondary" size="sm">
          <Forward aria-hidden="true" />
          Weiterleiten
        </Button>
      </DialogTrigger>

      <DialogContent>
        <DialogHeader>
          <DialogTitle>Vorgang weiterleiten</DialogTitle>
          <DialogDescription>
            {ticketNummer} verlässt {aktuellerBereich} vollständig. Der bisherige
            Verlauf bleibt erhalten, der Melder wird informiert.
          </DialogDescription>
        </DialogHeader>

        <DialogBody className="space-y-5">
          <section>
            <h3 className="mb-2 text-sm font-semibold text-fg">
              An welchen Bereich?
            </h3>

            {bereiche === null ? (
              <p className="text-sm text-fg-muted">Bereiche werden geladen …</p>
            ) : (
              <div className="grid gap-2 sm:grid-cols-2">
                {bereiche.map((b) => {
                  const Icon = teamIcon(b.icon);
                  const aktiv = teamId === b.id;
                  return (
                    <button
                      key={b.id}
                      type="button"
                      onClick={() => setTeamId(b.id)}
                      aria-pressed={aktiv}
                      className={cn(
                        "flex items-start gap-2.5 rounded-lg border p-3 text-left transition-colors",
                        aktiv
                          ? "border-primary bg-primary-soft"
                          : "border-line hover:bg-surface-2",
                      )}
                    >
                      <span
                        className="mt-0.5 flex size-8 shrink-0 items-center justify-center rounded-md"
                        style={{ backgroundColor: `${b.color}1a`, color: b.color }}
                      >
                        <Icon className="size-4" aria-hidden="true" />
                      </span>
                      <span className="min-w-0 flex-1">
                        <span className="flex items-center gap-1.5">
                          <span className="text-sm font-medium text-fg">
                            {b.name}
                          </span>
                          {aktiv && (
                            <Check className="size-3.5 text-primary" aria-hidden="true" />
                          )}
                        </span>
                        {b.description && (
                          <span className="mt-0.5 block text-xs text-fg-muted">
                            {truncate(b.description, 70)}
                          </span>
                        )}
                      </span>
                    </button>
                  );
                })}
              </div>
            )}
          </section>

          {brauchtStandort && (
            <Field
              label="Standort"
              htmlFor="weiterleiten-standort"
              required
              help={`${zielBereich?.name} hat je Standort einen eigenen Eingang.`}
            >
              <Select value={siteId} onChange={(e) => setSiteId(e.target.value)}>
                {verfuegbareStandorte.map((s) => (
                  <option key={s.id} value={s.id}>
                    {s.name}
                  </option>
                ))}
              </Select>
            </Field>
          )}

          <Field
            label="Warum leiten Sie weiter?"
            htmlFor="weiterleiten-grund"
            required
            help="Ein Satz genügt. Er steht im Protokoll und geht an den Melder."
          >
            <Textarea
              value={begruendung}
              onChange={(e) => setBegruendung(e.target.value)}
              rows={3}
              placeholder="Zum Beispiel: Ausweise werden in der Verwaltung ausgegeben, nicht in der IT."
            />
          </Field>

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
            <ArrowRight aria-hidden="true" />
            Weiterleiten
          </Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  );
}
