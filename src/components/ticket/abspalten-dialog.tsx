"use client";

import { useEffect, useState, useTransition } from "react";
import { useRouter } from "next/navigation";
import { Priority, QueueMode } from "@prisma/client";
import { AlertCircle, ArrowUpRight, Check, Split } from "lucide-react";

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
import { Field, Input, Select, Textarea } from "@/components/ui/input";
import { Checkbox, RadioGroup, RadioGroupItem } from "@/components/ui/misc";
import { abspaltenAction, ladeZielBereiche } from "@/server/actions/tickets";
import { PRIORITY } from "@/lib/domain";
import { formatDateTime } from "@/lib/format";
import { cn, truncate } from "@/lib/utils";

interface ZielBereich {
  id: string;
  name: string;
  key: string;
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

export interface VerlaufAuswahl {
  id: string;
  autor: string;
  intern: boolean;
  vorschau: string;
  createdAt: Date;
}

/**
 * Teilvorgang abspalten.
 *
 * Der Ablauf folgt der Frage, die sich beim Bearbeiten stellt: Wer soll den
 * anderen Teil machen – wo – was genau – was der andere Bereich vom bisherigen
 * Verlauf braucht – und was mit dem eigenen Vorgang passiert.
 *
 * Der letzte Schritt ist der wichtigste und deshalb nicht vorbelegt versteckt:
 * „offen lassen" und „warten" und „schließen" sind drei verschiedene
 * Situationen, und wer sie verwechselt, verliert entweder den eigenen Anteil
 * aus den Augen oder lässt einen erledigten Vorgang ewig offen stehen.
 */
export function AbspaltenDialog({
  ticketId,
  ticketNummer,
  ticketBetreff,
  aktuellerStandortId,
  nachrichten,
}: {
  ticketId: string;
  ticketNummer: string;
  ticketBetreff: string;
  aktuellerStandortId: string | null;
  nachrichten: VerlaufAuswahl[];
}) {
  const router = useRouter();
  const [offen, setOffen] = useState(false);
  const [bereiche, setBereiche] = useState<ZielBereich[] | null>(null);
  const [pending, startTransition] = useTransition();
  const [fehler, setFehler] = useState<string | null>(null);

  const [teamId, setTeamId] = useState("");
  const [siteId, setSiteId] = useState(aktuellerStandortId ?? "");
  const [betreff, setBetreff] = useState("");
  const [beschreibung, setBeschreibung] = useState("");
  const [prioritaet, setPrioritaet] = useState<Priority>(Priority.NORMAL);
  const [gewaehlteNachrichten, setGewaehlteNachrichten] = useState<string[]>([]);
  const [nachbehandlung, setNachbehandlung] = useState<
    "offen_lassen" | "warten_auf_teilvorgang" | "schliessen"
  >("offen_lassen");
  const [abschlussnotiz, setAbschlussnotiz] = useState("");

  useEffect(() => {
    if (!offen || bereiche) return;
    void ladeZielBereiche(ticketId).then((b) => setBereiche(b as ZielBereich[]));
  }, [offen, bereiche, ticketId]);

  // Betreff sinnvoll vorbelegen, sobald der Dialog aufgeht.
  useEffect(() => {
    if (offen && !betreff) setBetreff(ticketBetreff);
  }, [offen, betreff, ticketBetreff]);

  const zielBereich = bereiche?.find((b) => b.id === teamId);
  const brauchtStandort = zielBereich?.queueMode === QueueMode.PER_SITE;
  const verfuegbareStandorte =
    zielBereich?.queues
      .map((q) => q.site)
      .filter((s): s is NonNullable<typeof s> => s !== null) ?? [];

  // Deckt der Zielbereich den Standort des Vorgangs ab? Wenn nicht, muss
  // bewusst ein anderer gewählt werden.
  useEffect(() => {
    if (!brauchtStandort) return;
    const passt = verfuegbareStandorte.some((s) => s.id === siteId);
    if (!passt) setSiteId(verfuegbareStandorte[0]?.id ?? "");
  }, [brauchtStandort, siteId, verfuegbareStandorte]);

  function absenden() {
    setFehler(null);

    if (!teamId) {
      setFehler("Bitte wählen Sie den Bereich, der den Teilvorgang übernimmt.");
      return;
    }
    if (brauchtStandort && !siteId) {
      setFehler("Bitte wählen Sie den Standort.");
      return;
    }
    if (betreff.trim().length < 3) {
      setFehler("Bitte geben Sie dem Teilvorgang einen Betreff.");
      return;
    }
    if (beschreibung.trim().length < 3) {
      setFehler("Bitte beschreiben Sie, was der andere Bereich tun soll.");
      return;
    }

    startTransition(async () => {
      try {
        const ergebnis = await abspaltenAction({
          ticketId,
          zielTeamId: teamId,
          zielSiteId: brauchtStandort ? siteId : null,
          betreff,
          beschreibung,
          prioritaet,
          nachrichtenIds: gewaehlteNachrichten,
          nachbehandlung,
          abschlussnotiz:
            nachbehandlung === "schliessen" ? abschlussnotiz : undefined,
        });

        setOffen(false);
        router.push(`/tickets/${ergebnis.kindNummer}`);
      } catch (e) {
        setFehler(
          e instanceof Error
            ? e.message
            : "Der Teilvorgang konnte nicht angelegt werden.",
        );
      }
    });
  }

  return (
    <Dialog open={offen} onOpenChange={setOffen}>
      <DialogTrigger asChild>
        <Button variant="secondary" size="sm">
          <Split aria-hidden="true" />
          Teil abspalten
        </Button>
      </DialogTrigger>

      <DialogContent size="lg">
        <DialogHeader>
          <DialogTitle>Teilvorgang abspalten</DialogTitle>
          <DialogDescription>
            Erzeugt einen neuen Vorgang für einen anderen Bereich. Der Melder
            bleibt derselbe und muss nichts erneut melden – beide Vorgänge
            bleiben miteinander verknüpft.
          </DialogDescription>
        </DialogHeader>

        <DialogBody className="space-y-6">
          {/* --- 1. Wer übernimmt? ------------------------------------ */}
          <section>
            <h3 className="mb-2 text-sm font-semibold text-fg">
              1. Welcher Bereich übernimmt den Teil?
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
                            <Check
                              className="size-3.5 text-primary"
                              aria-hidden="true"
                            />
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

            {brauchtStandort && (
              <div className="mt-3">
                <Field
                  label="Standort"
                  htmlFor="abspalten-standort"
                  required
                  help={`${zielBereich?.name} hat je Standort einen eigenen Eingang.`}
                >
                  <Select
                    value={siteId}
                    onChange={(e) => setSiteId(e.target.value)}
                  >
                    {verfuegbareStandorte.map((s) => (
                      <option key={s.id} value={s.id}>
                        {s.name}
                      </option>
                    ))}
                  </Select>
                </Field>
              </div>
            )}
          </section>

          {/* --- 2. Was soll getan werden? ---------------------------- */}
          <section className="space-y-3">
            <h3 className="text-sm font-semibold text-fg">
              2. Was soll der Bereich tun?
            </h3>

            <Field label="Betreff" htmlFor="abspalten-betreff" required>
              <Input
                value={betreff}
                onChange={(e) => setBetreff(e.target.value)}
                maxLength={200}
              />
            </Field>

            <Field
              label="Beschreibung"
              htmlFor="abspalten-beschreibung"
              required
              help="Nur der Teil, um den es geht – der Rest bleibt in Ihrem Vorgang."
            >
              <Textarea
                value={beschreibung}
                onChange={(e) => setBeschreibung(e.target.value)}
                rows={4}
              />
            </Field>

            <Field label="Priorität" htmlFor="abspalten-prioritaet">
              <Select
                value={prioritaet}
                onChange={(e) => setPrioritaet(e.target.value as Priority)}
              >
                {Object.entries(PRIORITY).map(([wert, d]) => (
                  <option key={wert} value={wert}>
                    {d.label} – {d.description}
                  </option>
                ))}
              </Select>
            </Field>
          </section>

          {/* --- 3. Verlauf mitgeben ---------------------------------- */}
          {nachrichten.length > 0 && (
            <section>
              <h3 className="text-sm font-semibold text-fg">
                3. Was vom bisherigen Verlauf soll mit?
              </h3>
              <p className="mt-0.5 text-xs text-fg-muted">
                Optional. Der Zielbereich sieht diese Nachrichten dann in seinem
                Vorgang – ohne dass Sie alles abtippen müssen.
              </p>

              <ul className="mt-2 max-h-56 space-y-1 overflow-y-auto rounded-md border border-line p-1.5">
                {nachrichten.map((n) => {
                  const gewaehlt = gewaehlteNachrichten.includes(n.id);
                  return (
                    <li key={n.id}>
                      <label
                        className={cn(
                          "flex cursor-pointer items-start gap-2.5 rounded-md p-2 transition-colors",
                          gewaehlt ? "bg-primary-soft" : "hover:bg-surface-2",
                        )}
                      >
                        <Checkbox
                          className="mt-0.5"
                          checked={gewaehlt}
                          onCheckedChange={(v) =>
                            setGewaehlteNachrichten((vorher) =>
                              v
                                ? [...vorher, n.id]
                                : vorher.filter((id) => id !== n.id),
                            )
                          }
                        />
                        <span className="min-w-0 flex-1">
                          <span className="flex flex-wrap items-center gap-1.5 text-xs">
                            <span className="font-medium text-fg">{n.autor}</span>
                            {n.intern && (
                              <span className="rounded bg-warning-soft px-1.5 text-[0.6875rem] font-medium text-warning-soft-fg">
                                intern
                              </span>
                            )}
                            <span className="text-fg-subtle">
                              {formatDateTime(n.createdAt)}
                            </span>
                          </span>
                          <span className="mt-0.5 block text-xs text-fg-muted">
                            {truncate(n.vorschau, 140)}
                          </span>
                        </span>
                      </label>
                    </li>
                  );
                })}
              </ul>

              {gewaehlteNachrichten.some(
                (id) => nachrichten.find((n) => n.id === id)?.intern,
              ) && (
                <p className="mt-2 flex items-start gap-1.5 rounded-md bg-warning-soft px-2.5 py-1.5 text-xs text-warning-soft-fg">
                  <AlertCircle className="mt-0.5 size-3.5 shrink-0" aria-hidden="true" />
                  Sie geben eine interne Notiz weiter. Sie bleibt auch im
                  Zielbereich intern, ist dort aber für alle Mitglieder sichtbar.
                </p>
              )}
            </section>
          )}

          {/* --- 4. Und Ihr Vorgang? --------------------------------- */}
          <section>
            <h3 className="text-sm font-semibold text-fg">
              4. Was passiert mit {ticketNummer}?
            </h3>

            <RadioGroup
              value={nachbehandlung}
              onValueChange={(v) =>
                setNachbehandlung(v as typeof nachbehandlung)
              }
              className="mt-2 space-y-1.5"
            >
              <NachbehandlungOption
                wert="offen_lassen"
                aktiv={nachbehandlung === "offen_lassen"}
                titel="Bleibt offen"
                text="Ihr Anteil läuft parallel weiter. Wählen Sie das, wenn Sie selbst noch etwas zu tun haben."
              />
              <NachbehandlungOption
                wert="warten_auf_teilvorgang"
                aktiv={nachbehandlung === "warten_auf_teilvorgang"}
                titel="Zurückstellen, bis der Teilvorgang erledigt ist"
                text="Ihr Vorgang ruht und wird automatisch wieder geöffnet, sobald der andere Bereich fertig ist. Sie werden dann benachrichtigt."
              />
              <NachbehandlungOption
                wert="schliessen"
                aktiv={nachbehandlung === "schliessen"}
                titel="Abschließen – geht uns nichts mehr an"
                text="Der ganze verbleibende Teil liegt beim anderen Bereich."
              />
            </RadioGroup>

            {nachbehandlung === "schliessen" && (
              <div className="mt-3">
                <Field
                  label="Abschlussnotiz an den Melder"
                  htmlFor="abspalten-notiz"
                  help="Optional. Wird als Antwort im Vorgang gespeichert und per E-Mail zugestellt."
                >
                  <Textarea
                    value={abschlussnotiz}
                    onChange={(e) => setAbschlussnotiz(e.target.value)}
                    rows={3}
                    placeholder={`Der verbleibende Teil wird von ${zielBereich?.name ?? "dem anderen Bereich"} übernommen.`}
                  />
                </Field>
              </div>
            )}
          </section>

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
          <Button
            variant="ghost"
            onClick={() => setOffen(false)}
            disabled={pending}
          >
            Abbrechen
          </Button>
          <Button onClick={absenden} loading={pending}>
            <ArrowUpRight aria-hidden="true" />
            Teilvorgang anlegen
          </Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  );
}

function NachbehandlungOption({
  wert,
  aktiv,
  titel,
  text,
}: {
  wert: string;
  aktiv: boolean;
  titel: string;
  text: string;
}) {
  const id = `nachbehandlung-${wert}`;
  return (
    <label
      htmlFor={id}
      className={cn(
        "flex cursor-pointer items-start gap-2.5 rounded-lg border p-3 transition-colors",
        aktiv ? "border-primary bg-primary-soft" : "border-line hover:bg-surface-2",
      )}
    >
      <RadioGroupItem value={wert} id={id} className="mt-0.5" />
      <span className="min-w-0 flex-1">
        <span className="block text-sm font-medium text-fg">{titel}</span>
        <span className="mt-0.5 block text-xs text-fg-muted">{text}</span>
      </span>
    </label>
  );
}
