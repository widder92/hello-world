"use client";

import { useEffect, useMemo, useState, useTransition } from "react";
import Link from "next/link";
import { useRouter } from "next/navigation";
import { QueueMode, Urgency } from "@prisma/client";
import {
  AlertCircle,
  ArrowLeft,
  CheckCircle2,
  ChevronRight,
  Lightbulb,
  Send,
} from "lucide-react";

import { teamIcon } from "@/components/layout/icon";
import { Button } from "@/components/ui/button";
import { Card, CardContent } from "@/components/ui/card";
import { Dateianhang, type AnhangEintrag } from "@/components/ui/dateianhang";
import { Field, Input, Select, Textarea } from "@/components/ui/input";
import {
  meldungAufgebenAction,
  wissensVorschlaege,
} from "@/server/actions/melden";
import { URGENCY } from "@/lib/domain";
import { cn } from "@/lib/utils";

interface Formularfeld {
  key: string;
  label: string;
  type: "text" | "textarea" | "select" | "number" | "date" | "checkbox";
  required?: boolean;
  options?: string[];
  help?: string;
}

interface Kategorie {
  id: string;
  name: string;
  description: string | null;
  formSchema: unknown;
  unterkategorien?: {
    id: string;
    name: string;
    description: string | null;
    formSchema: unknown;
  }[];
}

interface Bereich {
  id: string;
  name: string;
  color: string;
  icon: string;
  queueMode: QueueMode;
  portalLabel: string | null;
  portalDescription: string | null;
  standortIds: string[];
  kategorien: Kategorie[];
}

interface Standort {
  id: string;
  name: string;
  shortName: string | null;
  type: string;
  city: string | null;
}

/**
 * Meldeformular.
 *
 * Zwei Schritte: erst der Bereich als große Kachel, dann das Formular. Der
 * Standort ist mit dem eigenen vorbelegt, denn in aller Regel meldet man etwas
 * aus dem Haus, in dem man arbeitet.
 *
 * In der einfachen Ansicht wachsen Schrift und Trefferflächen, die
 * Beschriftungen werden knapper und optionale Felder entfallen. Sie ist für
 * Kolleginnen und Kollegen gedacht, denen ein dichtes Formular im Weg steht –
 * in einer Einrichtung der Behindertenhilfe ein realer Teil der Belegschaft.
 */
export function MeldeFormular({
  bereiche,
  standorte,
  eigenerStandortId,
  einfacheAnsicht,
}: {
  bereiche: Bereich[];
  standorte: Standort[];
  eigenerStandortId: string | null;
  einfacheAnsicht: boolean;
}) {
  const router = useRouter();
  const [pending, startTransition] = useTransition();

  const [bereichId, setBereichId] = useState("");
  const [kategorieId, setKategorieId] = useState("");
  const [betreff, setBetreff] = useState("");
  const [text, setText] = useState("");
  const [standortId, setStandortId] = useState(eigenerStandortId ?? "");
  const [raum, setRaum] = useState("");
  const [dringlichkeit, setDringlichkeit] = useState<Urgency>(Urgency.NORMAL);
  const [anhaenge, setAnhaenge] = useState<AnhangEintrag[]>([]);
  const [zusatz, setZusatz] = useState<Record<string, string | boolean>>({});
  const [fehler, setFehler] = useState<string | null>(null);
  const [vorschlaege, setVorschlaege] = useState<
    { id: string; slug: string; title: string; summary: string | null }[]
  >([]);

  const bereich = bereiche.find((b) => b.id === bereichId);

  // Bei Bereichen mit einem Eingang je Standort nur die Standorte anbieten,
  // die dieser Bereich tatsächlich bedient – sonst entsteht eine Meldung, die
  // nirgends ankommt.
  const waehlbareStandorte = useMemo(() => {
    if (!bereich) return standorte;
    if (bereich.queueMode !== QueueMode.PER_SITE) return standorte;
    return standorte.filter((s) => bereich.standortIds.includes(s.id));
  }, [bereich, standorte]);

  useEffect(() => {
    if (!bereich) return;
    if (!waehlbareStandorte.some((s) => s.id === standortId)) {
      setStandortId(waehlbareStandorte[0]?.id ?? "");
    }
  }, [bereich, waehlbareStandorte, standortId]);

  // Hilfeartikel vorschlagen, während getippt wird – aber erst, wenn genug
  // dasteht, und mit Verzögerung, damit nicht bei jedem Buchstaben gesucht wird.
  useEffect(() => {
    const suchtext = `${betreff} ${text}`.trim();
    if (suchtext.length < 8) {
      setVorschlaege([]);
      return;
    }
    const timer = setTimeout(() => {
      void wissensVorschlaege(suchtext, bereichId || undefined).then(
        setVorschlaege,
      );
    }, 600);
    return () => clearTimeout(timer);
  }, [betreff, text, bereichId]);

  const gewaehlteKategorie = useMemo(() => {
    if (!bereich || !kategorieId) return null;
    for (const k of bereich.kategorien) {
      if (k.id === kategorieId) return k;
      const unter = k.unterkategorien?.find((u) => u.id === kategorieId);
      if (unter) return unter;
    }
    return null;
  }, [bereich, kategorieId]);

  const zusatzfelder: Formularfeld[] = Array.isArray(
    gewaehlteKategorie?.formSchema,
  )
    ? (gewaehlteKategorie.formSchema as Formularfeld[])
    : [];

  function absenden(e: React.FormEvent) {
    e.preventDefault();
    setFehler(null);

    if (!bereichId) {
      setFehler("Bitte wählen Sie, an wen die Meldung gehen soll.");
      return;
    }
    if (!standortId) {
      setFehler("Bitte wählen Sie den Standort.");
      return;
    }

    const fehlendesPflichtfeld = zusatzfelder.find(
      (f) => f.required && !zusatz[f.key],
    );
    if (fehlendesPflichtfeld) {
      setFehler(`Bitte füllen Sie das Feld „${fehlendesPflichtfeld.label}" aus.`);
      return;
    }

    startTransition(async () => {
      try {
        const ergebnis = await meldungAufgebenAction({
          teamId: bereichId,
          siteId: standortId,
          categoryId: kategorieId || undefined,
          subject: betreff,
          body: text,
          roomOrArea: raum || undefined,
          urgency: einfacheAnsicht ? undefined : dringlichkeit,
          formData: Object.keys(zusatz).length ? zusatz : undefined,
          anhangIds: anhaenge.map((a) => a.id),
        });
        router.push(`/meldung-aufgegeben?nummer=${ergebnis.nummer}`);
      } catch (err) {
        setFehler(
          err instanceof Error
            ? err.message
            : "Die Meldung konnte nicht gespeichert werden.",
        );
      }
    });
  }

  const gross = einfacheAnsicht;

  // --- Schritt 1: Bereich wählen -------------------------------------------
  if (!bereichId) {
    return (
      <div>
        <h1
          className={cn(
            "font-semibold tracking-tight text-fg",
            gross ? "text-3xl" : "text-2xl",
          )}
        >
          An wen geht Ihre Meldung?
        </h1>
        <p className={cn("mt-1.5 text-fg-muted", gross ? "text-lg" : "text-sm")}>
          Wählen Sie den Bereich, der sich um Ihr Anliegen kümmern soll.
        </p>

        <div
          className={cn(
            "mt-6 grid gap-3",
            gross ? "sm:grid-cols-2" : "sm:grid-cols-2 lg:grid-cols-3",
          )}
        >
          {bereiche.map((b) => {
            const Icon = teamIcon(b.icon);
            return (
              <button
                key={b.id}
                type="button"
                onClick={() => setBereichId(b.id)}
                className={cn(
                  "group flex items-start gap-3 rounded-xl border border-line bg-surface text-left shadow-card transition-all hover:border-primary hover:shadow-pop",
                  gross ? "p-6" : "p-5",
                )}
              >
                <span
                  className={cn(
                    "flex shrink-0 items-center justify-center rounded-lg",
                    gross ? "size-14" : "size-11",
                  )}
                  style={{ backgroundColor: `${b.color}1a`, color: b.color }}
                >
                  <Icon className={gross ? "size-7" : "size-5.5"} aria-hidden="true" />
                </span>

                <span className="min-w-0 flex-1">
                  <span
                    className={cn(
                      "block font-semibold text-fg",
                      gross ? "text-xl" : "text-base",
                    )}
                  >
                    {b.name}
                  </span>
                  <span
                    className={cn(
                      "mt-1 block text-fg-muted",
                      gross ? "text-base" : "text-sm",
                    )}
                  >
                    {b.portalLabel ?? b.portalDescription}
                  </span>
                </span>

                <ChevronRight
                  className="mt-1 size-5 shrink-0 text-fg-subtle transition-transform group-hover:translate-x-0.5"
                  aria-hidden="true"
                />
              </button>
            );
          })}
        </div>
      </div>
    );
  }

  // --- Schritt 2: Formular --------------------------------------------------
  const Icon = teamIcon(bereich!.icon);

  return (
    <div>
      <Button
        variant="ghost"
        size={gross ? "lg" : "sm"}
        onClick={() => {
          setBereichId("");
          setKategorieId("");
        }}
        className="mb-4 -ml-2"
      >
        <ArrowLeft aria-hidden="true" />
        Anderen Bereich wählen
      </Button>

      <div className="mb-6 flex items-center gap-3">
        <span
          className={cn(
            "flex shrink-0 items-center justify-center rounded-lg",
            gross ? "size-14" : "size-11",
          )}
          style={{ backgroundColor: `${bereich!.color}1a`, color: bereich!.color }}
        >
          <Icon className={gross ? "size-7" : "size-5.5"} aria-hidden="true" />
        </span>
        <div>
          <h1
            className={cn(
              "font-semibold tracking-tight text-fg",
              gross ? "text-3xl" : "text-2xl",
            )}
          >
            Meldung an {bereich!.name}
          </h1>
          {bereich!.portalDescription && (
            <p className={cn("text-fg-muted", gross ? "text-base" : "text-sm")}>
              {bereich!.portalDescription}
            </p>
          )}
        </div>
      </div>

      <form onSubmit={absenden} className="space-y-5">
        <Card>
          <CardContent className={cn("space-y-5", gross && "py-6")}>
            <Field
              label="Worum geht es?"
              htmlFor="betreff"
              required
              help={
                gross
                  ? undefined
                  : "Eine kurze Überschrift, zum Beispiel: Heizung im Gruppenraum wird nicht warm."
              }
            >
              <Input
                value={betreff}
                onChange={(e) => setBetreff(e.target.value)}
                maxLength={200}
                className={gross ? "h-12 text-lg" : undefined}
                autoFocus
              />
            </Field>

            <Field
              label="Beschreiben Sie es bitte genauer"
              htmlFor="text"
              required
              help={
                gross
                  ? "Was ist passiert? Seit wann?"
                  : "Was funktioniert nicht, seit wann, und was haben Sie schon versucht?"
              }
            >
              <Textarea
                value={text}
                onChange={(e) => setText(e.target.value)}
                rows={gross ? 7 : 5}
                className={gross ? "text-lg" : undefined}
              />
            </Field>

            <div>
              <p
                className={cn(
                  "mb-1.5 font-medium text-fg",
                  gross ? "text-base" : "text-sm",
                )}
              >
                Foto oder Datei anhängen
              </p>
              <Dateianhang
                anhaenge={anhaenge}
                onChange={setAnhaenge}
                gross={gross}
                label={gross ? "Datei auswählen" : "Dateien anhängen"}
              />
            </div>

            {/* Hilfeartikel, die vielleicht schon reichen. */}
            {vorschlaege.length > 0 && (
              <div className="rounded-lg border border-info/30 bg-info-soft p-3.5">
                <p className="flex items-center gap-1.5 text-sm font-medium text-info-soft-fg">
                  <Lightbulb className="size-4" aria-hidden="true" />
                  Vielleicht hilft Ihnen das schon weiter
                </p>
                <ul className="mt-2 space-y-1.5">
                  {vorschlaege.map((v) => (
                    <li key={v.id}>
                      <Link
                        href={`/wissen/${v.slug}`}
                        target="_blank"
                        className="block rounded-md bg-surface/60 px-2.5 py-2 text-sm transition-colors hover:bg-surface"
                      >
                        <span className="font-medium text-fg">{v.title}</span>
                        {v.summary && (
                          <span className="block text-xs text-fg-muted">
                            {v.summary}
                          </span>
                        )}
                      </Link>
                    </li>
                  ))}
                </ul>
              </div>
            )}
          </CardContent>
        </Card>

        <Card>
          <CardContent className={cn("space-y-5", gross && "py-6")}>
            <Field
              label="An welchem Standort?"
              htmlFor="standort"
              required
              help={
                bereich!.queueMode === QueueMode.PER_SITE
                  ? `${bereich!.name} hat an jedem Standort eigene Kolleginnen und Kollegen. Ihre Meldung geht direkt an den richtigen.`
                  : undefined
              }
            >
              <Select
                value={standortId}
                onChange={(e) => setStandortId(e.target.value)}
                className={gross ? "h-12 text-lg" : undefined}
              >
                {waehlbareStandorte.map((s) => (
                  <option key={s.id} value={s.id}>
                    {s.name}
                    {s.city ? ` – ${s.city}` : ""}
                  </option>
                ))}
              </Select>
            </Field>

            <Field
              label="Raum oder Bereich"
              htmlFor="raum"
              help="Wo genau? Zum Beispiel: Gruppenraum 2, 1. OG."
            >
              <Input
                value={raum}
                onChange={(e) => setRaum(e.target.value)}
                maxLength={120}
                className={gross ? "h-12 text-lg" : undefined}
              />
            </Field>

            {/* Kategorie und Dringlichkeit nur in der normalen Ansicht: In der
                einfachen Ansicht sind sie Ballast, und der Bereich setzt beides
                beim Bearbeiten ohnehin. */}
            {!gross && bereich!.kategorien.length > 0 && (
              <Field
                label="Kategorie"
                htmlFor="kategorie"
                help="Optional. Hilft dabei, die Meldung schneller einzuordnen."
              >
                <Select
                  value={kategorieId}
                  onChange={(e) => {
                    setKategorieId(e.target.value);
                    setZusatz({});
                  }}
                >
                  <option value="">Bitte wählen …</option>
                  {bereich!.kategorien.map((k) => (
                    <optgroup
                      key={k.id}
                      label={k.unterkategorien?.length ? k.name : ""}
                    >
                      <option value={k.id}>{k.name}</option>
                      {k.unterkategorien?.map((u) => (
                        <option key={u.id} value={u.id}>
                          {" "}
                          {u.name}
                        </option>
                      ))}
                    </optgroup>
                  ))}
                </Select>
              </Field>
            )}

            {!gross && (
              <Field
                label="Wie dringend ist es?"
                htmlFor="dringlichkeit"
                help="Eine ehrliche Einschätzung hilft uns beim Sortieren."
              >
                <Select
                  value={dringlichkeit}
                  onChange={(e) => setDringlichkeit(e.target.value as Urgency)}
                >
                  {Object.entries(URGENCY).map(([wert, d]) => (
                    <option key={wert} value={wert}>
                      {d.label} – {d.description}
                    </option>
                  ))}
                </Select>
              </Field>
            )}
          </CardContent>
        </Card>

        {/* Zusatzfelder der gewählten Kategorie */}
        {zusatzfelder.length > 0 && (
          <Card>
            <CardContent className="space-y-5">
              <h2 className="text-sm font-semibold text-fg">
                Ein paar Angaben zu „{gewaehlteKategorie?.name}"
              </h2>
              {zusatzfelder.map((f) => (
                <Zusatzfeld
                  key={f.key}
                  feld={f}
                  wert={zusatz[f.key]}
                  onChange={(v) => setZusatz((z) => ({ ...z, [f.key]: v }))}
                />
              ))}
            </CardContent>
          </Card>
        )}

        {fehler && (
          <p
            role="alert"
            className="flex items-start gap-2 rounded-lg border border-danger/30 bg-danger-soft px-4 py-3 text-sm text-danger-soft-fg"
          >
            <AlertCircle className="mt-0.5 size-4 shrink-0" aria-hidden="true" />
            {fehler}
          </p>
        )}

        <div className="flex flex-wrap items-center justify-between gap-3">
          <p className="text-xs text-fg-subtle">
            Bitte keine Gesundheitsdaten oder Namen von Klientinnen und Klienten
            eintragen.
          </p>
          <Button type="submit" size={gross ? "xl" : "lg"} loading={pending}>
            <Send aria-hidden="true" />
            Meldung absenden
          </Button>
        </div>
      </form>
    </div>
  );
}

function Zusatzfeld({
  feld,
  wert,
  onChange,
}: {
  feld: Formularfeld;
  wert: string | boolean | undefined;
  onChange: (wert: string | boolean) => void;
}) {
  const id = `zusatz-${feld.key}`;

  if (feld.type === "checkbox") {
    return (
      <label className="flex cursor-pointer items-start gap-2.5">
        <input
          id={id}
          type="checkbox"
          checked={Boolean(wert)}
          onChange={(e) => onChange(e.target.checked)}
          className="mt-0.5 size-4.5 rounded border-line-strong"
        />
        <span className="text-sm text-fg">{feld.label}</span>
      </label>
    );
  }

  return (
    <Field label={feld.label} htmlFor={id} help={feld.help} required={feld.required}>
      {feld.type === "textarea" ? (
        <Textarea
          value={String(wert ?? "")}
          onChange={(e) => onChange(e.target.value)}
          rows={3}
        />
      ) : feld.type === "select" ? (
        <Select
          value={String(wert ?? "")}
          onChange={(e) => onChange(e.target.value)}
        >
          <option value="">Bitte wählen …</option>
          {feld.options?.map((o) => (
            <option key={o} value={o}>
              {o}
            </option>
          ))}
        </Select>
      ) : (
        <Input
          type={feld.type === "number" ? "number" : feld.type === "date" ? "date" : "text"}
          value={String(wert ?? "")}
          onChange={(e) => onChange(e.target.value)}
        />
      )}
    </Field>
  );
}

/** Bestätigungsseite nach dem Absenden. */
export function MeldungAufgegeben({ nummer }: { nummer: string }) {
  return (
    <div className="mx-auto max-w-lg py-10 text-center">
      <span className="mx-auto flex size-16 items-center justify-center rounded-full bg-success-soft">
        <CheckCircle2 className="size-8 text-success-soft-fg" aria-hidden="true" />
      </span>

      <h1 className="mt-5 text-2xl font-semibold text-fg">
        Ihre Meldung ist angekommen
      </h1>
      <p className="mt-2 text-fg-muted">
        Sie hat die Nummer{" "}
        <span className="font-mono font-semibold text-fg">{nummer}</span>. Wir
        haben Ihnen eine Bestätigung per E-Mail geschickt.
      </p>

      <div className="mt-7 flex flex-wrap justify-center gap-3">
        <Button asChild size="lg">
          <Link href={`/tickets/${nummer}`}>Meldung ansehen</Link>
        </Button>
        <Button asChild variant="secondary" size="lg">
          <Link href="/meine-tickets">Zu meinen Meldungen</Link>
        </Button>
      </div>
    </div>
  );
}
