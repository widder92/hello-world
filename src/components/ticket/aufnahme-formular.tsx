"use client";

import { useEffect, useMemo, useState, useTransition } from "react";
import { useRouter } from "next/navigation";
import { Priority, QueueMode, TeamMode } from "@prisma/client";
import { AlertCircle, Check, Phone, Search, UserPlus, X } from "lucide-react";

import { teamIcon } from "@/components/layout/icon";
import { Button } from "@/components/ui/button";
import { Card, CardContent } from "@/components/ui/card";
import { Field, Input, Select, Textarea } from "@/components/ui/input";
import { Switch } from "@/components/ui/misc";
import {
  sucheMelder,
  vorgangAufnehmenAction,
} from "@/server/actions/aufnehmen";
import { PRIORITY } from "@/lib/domain";
import { cn } from "@/lib/utils";

interface Bereich {
  id: string;
  name: string;
  mode: TeamMode;
  queueMode: QueueMode;
  color: string;
  icon: string;
  queues: { id: string; site: { id: string; name: string } | null }[];
  categories: { id: string; name: string }[];
}

interface Standort {
  id: string;
  name: string;
  city: string | null;
}

interface Treffer {
  id: string;
  name: string;
  email: string;
  isExternal: boolean;
  site: { id: string; name: string } | null;
}

/**
 * Vorgang für eine andere Person aufnehmen – der Telefonfall.
 *
 * Die Reihenfolge folgt dem Gespräch: erst wer anruft, dann worum es geht,
 * dann wohin es gehört. Wer am Telefon mitschreibt, soll nicht springen
 * müssen.
 *
 * Der Melder wird gesucht statt aus einer Liste gewählt: Bei tausend
 * Mitarbeitenden ist eine Auswahlliste unbenutzbar, und am Telefon hat man
 * ohnehin einen Namen im Ohr.
 */
export function AufnahmeFormular({
  bereiche,
  standorte,
  eigeneBereichsIds,
}: {
  bereiche: Bereich[];
  standorte: Standort[];
  eigeneBereichsIds: string[];
}) {
  const router = useRouter();
  const [pending, startTransition] = useTransition();
  const [fehler, setFehler] = useState<string | null>(null);

  // --- Melder ---------------------------------------------------------------
  const [suche, setSuche] = useState("");
  const [treffer, setTreffer] = useState<Treffer[]>([]);
  const [melder, setMelder] = useState<Treffer | null>(null);
  const [ohneKonto, setOhneKonto] = useState(false);
  const [neuerName, setNeuerName] = useState("");
  const [neueEmail, setNeueEmail] = useState("");

  // --- Anliegen -------------------------------------------------------------
  const [teamId, setTeamId] = useState("");
  const [siteId, setSiteId] = useState("");
  const [categoryId, setCategoryId] = useState("");
  const [betreff, setBetreff] = useState("");
  const [text, setText] = useState("");
  const [raum, setRaum] = useState("");
  const [prioritaet, setPrioritaet] = useState<Priority>(Priority.NORMAL);

  const [mirZuweisen, setMirZuweisen] = useState(true);
  const [melderInformieren, setMelderInformieren] = useState(true);

  // Suche mit kurzer Verzögerung, damit nicht bei jedem Tastendruck eine
  // Abfrage losläuft.
  useEffect(() => {
    if (melder || ohneKonto || suche.trim().length < 2) {
      setTreffer([]);
      return;
    }
    const timer = setTimeout(() => {
      void sucheMelder(suche).then((t) => setTreffer(t as Treffer[]));
    }, 250);
    return () => clearTimeout(timer);
  }, [suche, melder, ohneKonto]);

  const bereich = bereiche.find((b) => b.id === teamId);
  const brauchtStandort = bereich?.queueMode === QueueMode.PER_SITE;
  const voll = bereich?.mode === TeamMode.VOLL;

  /**
   * Bei Eingang je Standort nur die Standorte anbieten, für die es dort
   * tatsächlich einen Eingang gibt – sonst ließe sich ein Vorgang in ein Haus
   * schicken, in dem ihn niemand sieht.
   */
  const waehlbareStandorte = useMemo(() => {
    if (!bereich) return standorte;
    if (bereich.queueMode !== QueueMode.PER_SITE) return standorte;
    return bereich.queues
      .map((q) => q.site)
      .filter((s): s is NonNullable<typeof s> => s !== null)
      .map((s) => ({ ...s, city: null }));
  }, [bereich, standorte]);

  // Standort des Melders vorbelegen, sobald beides feststeht.
  useEffect(() => {
    if (!melder?.site) return;
    const passt = waehlbareStandorte.some((s) => s.id === melder.site!.id);
    if (passt) setSiteId(melder.site.id);
  }, [melder, waehlbareStandorte]);

  // Ist der Zielbereich keiner der eigenen, ergibt „mir zuweisen" keinen Sinn.
  useEffect(() => {
    if (teamId && !eigeneBereichsIds.includes(teamId)) setMirZuweisen(false);
  }, [teamId, eigeneBereichsIds]);

  const melderGesetzt = Boolean(melder) || (ohneKonto && neuerName && neueEmail);

  function absenden() {
    setFehler(null);

    startTransition(async () => {
      try {
        const ergebnis = await vorgangAufnehmenAction({
          melderId: melder?.id ?? "",
          melderName: ohneKonto ? neuerName : "",
          melderEmail: ohneKonto ? neueEmail : "",
          teamId,
          siteId,
          categoryId,
          subject: betreff,
          body: text,
          roomOrArea: raum,
          priority: prioritaet,
          mirZuweisen,
          melderInformieren,
        });
        router.push(`/tickets/${ergebnis.nummer}`);
      } catch (e) {
        setFehler(
          e instanceof Error
            ? e.message
            : "Der Vorgang konnte nicht aufgenommen werden.",
        );
      }
    });
  }

  return (
    <div className="space-y-5">
      {/* --- 1. Wer meldet? ------------------------------------------------ */}
      <Card>
        <CardContent className="space-y-4">
          <h2 className="text-sm font-semibold text-fg">1. Wer meldet?</h2>

          {melder ? (
            <div className="flex items-start gap-3 rounded-lg border border-primary bg-primary-soft p-3">
              <Check className="mt-0.5 size-4 shrink-0 text-primary" aria-hidden="true" />
              <div className="min-w-0 flex-1">
                <p className="font-medium text-fg">{melder.name}</p>
                <p className="text-sm text-fg-muted">{melder.email}</p>
                {melder.site && (
                  <p className="text-sm text-fg-muted">{melder.site.name}</p>
                )}
              </div>
              <Button
                variant="ghost"
                size="sm"
                onClick={() => {
                  setMelder(null);
                  setSuche("");
                }}
              >
                <X aria-hidden="true" />
                Ändern
              </Button>
            </div>
          ) : ohneKonto ? (
            <div className="space-y-3">
              <Field label="Name" htmlFor="melder-name" required>
                <Input
                  value={neuerName}
                  onChange={(e) => setNeuerName(e.target.value)}
                  placeholder="Vorname Nachname"
                />
              </Field>
              <Field
                label="E-Mail-Adresse"
                htmlFor="melder-email"
                required
                help="Dorthin gehen die Bestätigung und alle Antworten."
              >
                <Input
                  type="email"
                  value={neueEmail}
                  onChange={(e) => setNeueEmail(e.target.value)}
                />
              </Field>
              <Button
                variant="ghost"
                size="sm"
                onClick={() => setOhneKonto(false)}
                className="-ml-2"
              >
                <Search aria-hidden="true" />
                Doch nach einem Konto suchen
              </Button>
            </div>
          ) : (
            <div className="space-y-2">
              <Field
                label="Nach Person suchen"
                htmlFor="melder-suche"
                help="Name oder E-Mail-Adresse, ab zwei Zeichen."
              >
                <Input
                  value={suche}
                  onChange={(e) => setSuche(e.target.value)}
                  placeholder="z. B. Bergmann"
                  autoFocus
                />
              </Field>

              {treffer.length > 0 && (
                <ul className="divide-y divide-line overflow-hidden rounded-lg border border-line">
                  {treffer.map((t) => (
                    <li key={t.id}>
                      <button
                        type="button"
                        onClick={() => setMelder(t)}
                        className="flex w-full items-baseline gap-2 px-3 py-2 text-left transition-colors hover:bg-surface-2"
                      >
                        <span className="font-medium text-fg">{t.name}</span>
                        <span className="text-sm text-fg-muted">{t.email}</span>
                        {t.site && (
                          <span className="ml-auto shrink-0 text-sm text-fg-subtle">
                            {t.site.name}
                          </span>
                        )}
                      </button>
                    </li>
                  ))}
                </ul>
              )}

              {suche.trim().length >= 2 && treffer.length === 0 && (
                <p className="text-sm text-fg-muted">Keine Person gefunden.</p>
              )}

              <Button
                variant="ghost"
                size="sm"
                onClick={() => setOhneKonto(true)}
                className="-ml-2"
              >
                <UserPlus aria-hidden="true" />
                Person hat kein Konto
              </Button>
            </div>
          )}
        </CardContent>
      </Card>

      {/* --- 2. Worum geht es? --------------------------------------------- */}
      <Card>
        <CardContent className="space-y-4">
          <h2 className="text-sm font-semibold text-fg">2. Worum geht es?</h2>

          <Field label="Betreff" htmlFor="betreff" required>
            <Input
              value={betreff}
              onChange={(e) => setBetreff(e.target.value)}
              maxLength={200}
              placeholder="Kurz und sprechend, z. B. Drucker im Sekretariat druckt nicht"
            />
          </Field>

          <Field
            label="Mitschrift des Gesprächs"
            htmlFor="text"
            required
            help="Was geschildert wurde – und was Sie am Telefon schon versucht haben."
          >
            <Textarea
              value={text}
              onChange={(e) => setText(e.target.value)}
              rows={6}
            />
          </Field>

          <Field label="Raum oder Bereich" htmlFor="raum">
            <Input
              value={raum}
              onChange={(e) => setRaum(e.target.value)}
              maxLength={120}
              placeholder="z. B. Gruppenraum 2, 1. OG"
            />
          </Field>
        </CardContent>
      </Card>

      {/* --- 3. Wohin gehört er? ------------------------------------------- */}
      <Card>
        <CardContent className="space-y-4">
          <h2 className="text-sm font-semibold text-fg">3. Wer übernimmt?</h2>

          <div className="grid gap-2 sm:grid-cols-2">
            {bereiche.map((b) => {
              const Icon = teamIcon(b.icon);
              const aktiv = teamId === b.id;
              return (
                <button
                  key={b.id}
                  type="button"
                  onClick={() => {
                    setTeamId(b.id);
                    setCategoryId("");
                  }}
                  aria-pressed={aktiv}
                  className={cn(
                    "flex items-center gap-2.5 rounded-lg border p-3 text-left transition-colors",
                    aktiv
                      ? "border-primary bg-primary-soft"
                      : "border-line hover:bg-surface-2",
                  )}
                >
                  <span
                    className="flex size-8 shrink-0 items-center justify-center rounded-md"
                    style={{ backgroundColor: `${b.color}1a`, color: b.color }}
                  >
                    <Icon className="size-4" aria-hidden="true" />
                  </span>
                  <span className="flex-1 text-sm font-medium text-fg">
                    {b.name}
                  </span>
                  {aktiv && (
                    <Check className="size-4 text-primary" aria-hidden="true" />
                  )}
                </button>
              );
            })}
          </div>

          {bereich && (
            <Field
              label="Standort"
              htmlFor="standort"
              required={brauchtStandort}
              help={
                brauchtStandort
                  ? `${bereich.name} hat je Standort eigene Kolleginnen und Kollegen.`
                  : "Wird am Vorgang festgehalten."
              }
            >
              <Select
                value={siteId}
                onChange={(e) => setSiteId(e.target.value)}
              >
                <option value="">Bitte wählen …</option>
                {waehlbareStandorte.map((s) => (
                  <option key={s.id} value={s.id}>
                    {s.name}
                  </option>
                ))}
              </Select>
            </Field>
          )}

          {voll && bereich && bereich.categories.length > 0 && (
            <Field label="Kategorie" htmlFor="kategorie">
              <Select
                value={categoryId}
                onChange={(e) => setCategoryId(e.target.value)}
              >
                <option value="">Ohne Kategorie</option>
                {bereich.categories.map((k) => (
                  <option key={k.id} value={k.id}>
                    {k.name}
                  </option>
                ))}
              </Select>
            </Field>
          )}

          {voll && (
            <Field label="Priorität" htmlFor="prioritaet">
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
          )}
        </CardContent>
      </Card>

      {/* --- 4. Und dann? --------------------------------------------------- */}
      <Card>
        <CardContent className="space-y-3">
          <h2 className="text-sm font-semibold text-fg">4. Was soll danach geschehen?</h2>

          <label className="flex items-start gap-3">
            <Switch
              checked={mirZuweisen}
              onCheckedChange={setMirZuweisen}
              disabled={Boolean(teamId) && !eigeneBereichsIds.includes(teamId)}
            />
            <span className="min-w-0 flex-1">
              <span className="block text-sm font-medium text-fg">
                Mir zuweisen
              </span>
              <span className="block text-sm text-fg-muted">
                {teamId && !eigeneBereichsIds.includes(teamId)
                  ? "Nicht möglich – Sie arbeiten nicht in diesem Bereich."
                  : "Sie waren am Telefon, also bleibt der Vorgang bei Ihnen."}
              </span>
            </span>
          </label>

          <label className="flex items-start gap-3">
            <Switch
              checked={melderInformieren}
              onCheckedChange={setMelderInformieren}
            />
            <span className="min-w-0 flex-1">
              <span className="block text-sm font-medium text-fg">
                Melder per E-Mail informieren
              </span>
              <span className="block text-sm text-fg-muted">
                Die Person bekommt die Ticketnummer und kann per Antwort
                mitreden. Nur abschalten, wenn das am Telefon ausdrücklich
                besprochen wurde.
              </span>
            </span>
          </label>
        </CardContent>
      </Card>

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
          Der Vorgang gehört dem Melder – er sieht ihn unter seinen Meldungen.
          Dass Sie ihn aufgenommen haben, steht im Verlauf.
        </p>
        <Button
          size="lg"
          onClick={absenden}
          loading={pending}
          disabled={!melderGesetzt || !teamId}
        >
          <Phone aria-hidden="true" />
          Vorgang aufnehmen
        </Button>
      </div>
    </div>
  );
}
