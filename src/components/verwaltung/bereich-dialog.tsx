"use client";

import { useState, useTransition } from "react";
import { useRouter } from "next/navigation";
import { QueueMode, TeamMode } from "@prisma/client";
import { AlertCircle, Inbox, MapPin, Pencil, Plus } from "lucide-react";

import { TEAM_ICON_NAMES, teamIcon } from "@/components/layout/icon";
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
import { RadioGroup, RadioGroupItem, Switch } from "@/components/ui/misc";
import {
  bereichAendernAction,
  bereichAnlegenAction,
} from "@/server/actions/verwaltung";
import { cn } from "@/lib/utils";

interface BereichDaten {
  id: string;
  key: string;
  name: string;
  mode: TeamMode;
  queueMode: QueueMode;
  description: string | null;
  color: string;
  icon: string;
  portalVisible: boolean;
  portalLabel: string | null;
  portalDescription: string | null;
  allowEmailCommands: boolean;
  notifyRequesterOnTransfer: boolean;
  isActive: boolean;
}

const FARBEN = [
  "#0f766e", "#0369a1", "#4f46e5", "#7c3aed",
  "#be123c", "#b45309", "#059669", "#475569",
];

/**
 * Bereich anlegen oder ändern.
 *
 * Die beiden folgenreichen Entscheidungen – Ausbaustufe und Eingangsstruktur –
 * stehen ganz oben und sind ausformuliert statt als Auswahlliste versteckt.
 * Wer hier klickt, ändert, wie ein ganzes Team arbeitet; das soll man beim
 * Klicken sehen und nicht erst hinterher merken.
 */
export function BereichDialog({
  bereich,
  standortAnzahl,
  offeneVorgaenge = 0,
}: {
  bereich?: BereichDaten;
  standortAnzahl: number;
  offeneVorgaenge?: number;
}) {
  const router = useRouter();
  const [offen, setOffen] = useState(false);
  const [pending, startTransition] = useTransition();
  const [fehler, setFehler] = useState<string | null>(null);

  const neu = !bereich;

  const [form, setForm] = useState({
    key: bereich?.key ?? "",
    name: bereich?.name ?? "",
    mode: bereich?.mode ?? TeamMode.EINFACH,
    queueMode: bereich?.queueMode ?? QueueMode.SINGLE,
    description: bereich?.description ?? "",
    color: bereich?.color ?? FARBEN[0],
    icon: bereich?.icon ?? "Wrench",
    portalVisible: bereich?.portalVisible ?? true,
    portalLabel: bereich?.portalLabel ?? "",
    portalDescription: bereich?.portalDescription ?? "",
    allowEmailCommands: bereich?.allowEmailCommands ?? true,
    notifyRequesterOnTransfer: bereich?.notifyRequesterOnTransfer ?? true,
    isActive: bereich?.isActive ?? true,
  });

  function setzen<K extends keyof typeof form>(
    feld: K,
    wert: (typeof form)[K],
  ) {
    setForm((f) => ({ ...f, [feld]: wert }));
  }

  const strukturWechsel =
    bereich && bereich.queueMode !== form.queueMode;
  const stufenWechsel = bereich && bereich.mode !== form.mode;

  function absenden() {
    setFehler(null);

    startTransition(async () => {
      try {
        if (bereich) {
          await bereichAendernAction(bereich.id, form);
        } else {
          await bereichAnlegenAction(form);
        }
        setOffen(false);
        router.refresh();
      } catch (e) {
        setFehler(
          e instanceof Error ? e.message : "Der Bereich konnte nicht gespeichert werden.",
        );
      }
    });
  }

  const IconVorschau = teamIcon(form.icon);

  return (
    <Dialog open={offen} onOpenChange={setOffen}>
      <DialogTrigger asChild>
        {neu ? (
          <Button size="sm">
            <Plus aria-hidden="true" />
            Bereich anlegen
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
            {neu ? "Neuen Bereich anlegen" : `Bereich „${bereich.name}"`}
          </DialogTitle>
          <DialogDescription>
            Ausbaustufe und Eingangsstruktur bestimmen, wie dieser Bereich
            arbeitet. Beides lässt sich später ändern, ohne dass Vorgänge
            verloren gehen.
          </DialogDescription>
        </DialogHeader>

        <DialogBody className="space-y-6">
          {/* --- Ausbaustufe ------------------------------------------- */}
          <section>
            <h3 className="mb-2 text-sm font-semibold text-fg">Ausbaustufe</h3>
            <RadioGroup
              value={form.mode}
              onValueChange={(v) => setzen("mode", v as TeamMode)}
              className="space-y-1.5"
            >
              <StufenOption
                wert={TeamMode.EINFACH}
                aktiv={form.mode === TeamMode.EINFACH}
                titel="Schlanke Ansicht"
                text={
                  "Eine Liste mit den Reitern offen und erledigt. Empfangen, " +
                  "antworten, abschließen, weitergeben, abspalten. Kein " +
                  "Verteilerboard, keine Fristen, keine Priorität, keine Auswertung."
                }
                fuer="Für Bereiche, die gelegentlich Meldungen bekommen – Küche, Fahrdienst, Verwaltung."
              />
              <StufenOption
                wert={TeamMode.VOLL}
                aktiv={form.mode === TeamMode.VOLL}
                titel="Voller Arbeitsplatz"
                text={
                  "Zusätzlich ein Sammeleingang zum Selbstziehen, Zuweisung im " +
                  "Team, Fristen mit Ampel, Priorität, Kategorien, Vorlagen, " +
                  "Schlagworte, Checklisten und Auswertung."
                }
                fuer="Für Bereiche, die den ganzen Tag darin arbeiten – IT, Hausmeister."
              />
            </RadioGroup>

            {stufenWechsel && (
              <Hinweis>
                {form.mode === TeamMode.VOLL
                  ? "Beim Wechsel auf den vollen Arbeitsplatz kommen Ansichten und Felder hinzu. Bestehende Vorgänge bleiben unverändert."
                  : `Beim Wechsel auf die schlanke Ansicht werden Fristen, Priorität und Auswertung ausgeblendet. Die Daten bleiben erhalten${offeneVorgaenge > 0 ? ` – auch bei den ${offeneVorgaenge} offenen Vorgängen` : ""}, sie sind nur nicht mehr sichtbar.`}
              </Hinweis>
            )}
          </section>

          {/* --- Eingangsstruktur -------------------------------------- */}
          <section>
            <h3 className="mb-2 text-sm font-semibold text-fg">
              Eingangsstruktur
            </h3>
            <RadioGroup
              value={form.queueMode}
              onValueChange={(v) => setzen("queueMode", v as QueueMode)}
              className="space-y-1.5"
            >
              <StufenOption
                wert={QueueMode.SINGLE}
                aktiv={form.queueMode === QueueMode.SINGLE}
                icon={Inbox}
                titel="Ein Eingang für alle Standorte"
                text="Alle Meldungen sammeln sich in einem Topf. Der Standort wird trotzdem am Vorgang erfasst und ist Filter- und Auswertungsachse."
                fuer="Für zentrale Bereiche wie die IT."
              />
              <StufenOption
                wert={QueueMode.PER_SITE}
                aktiv={form.queueMode === QueueMode.PER_SITE}
                icon={MapPin}
                titel="Ein Eingang je Standort"
                text={`Es entsteht je Standort ein eigener Eingang mit eigener Empfangsadresse (derzeit ${standortAnzahl} Standorte). Der Melder wählt beim Melden den Standort, und die Meldung geht direkt an die dortigen Kolleg:innen.`}
                fuer="Für Bereiche mit festen Zuständigkeiten pro Haus – Hausmeister."
              />
            </RadioGroup>

            {strukturWechsel && (
              <Hinweis warnend>
                {form.queueMode === QueueMode.PER_SITE
                  ? `Es werden ${standortAnzahl} Eingänge angelegt, einer je Standort. Der bisherige gemeinsame Eingang wird stillgelegt – seine Vorgänge bleiben dort erhalten und weiter erreichbar, neue Meldungen laufen aber über die Standort-Eingänge. Tragen Sie danach die Empfangsadressen nach.`
                  : `Ein gemeinsamer Eingang wird angelegt, die Standort-Eingänge werden stillgelegt. Die dort liegenden Vorgänge bleiben erhalten und erreichbar, neue Meldungen kommen künftig im gemeinsamen Eingang an.`}
              </Hinweis>
            )}
          </section>

          {/* --- Grunddaten -------------------------------------------- */}
          <section className="space-y-4">
            <h3 className="text-sm font-semibold text-fg">Grunddaten</h3>

            <div className="grid gap-4 sm:grid-cols-[1fr_8rem]">
              <Field label="Name" htmlFor="bereich-name" required>
                <Input
                  value={form.name}
                  onChange={(e) => setzen("name", e.target.value)}
                  maxLength={60}
                />
              </Field>

              <Field
                label="Kürzel"
                htmlFor="bereich-key"
                required
                help="Steht in der Ticketnummer."
              >
                <Input
                  value={form.key}
                  onChange={(e) =>
                    setzen("key", e.target.value.toUpperCase().slice(0, 8))
                  }
                  className="font-mono uppercase"
                  maxLength={8}
                  disabled={Boolean(bereich)}
                />
              </Field>
            </div>

            {bereich && (
              <p className="-mt-2 text-xs text-fg-subtle">
                Das Kürzel steckt in allen bisherigen Ticketnummern dieses
                Bereichs und lässt sich deshalb nicht mehr ändern.
              </p>
            )}

            <Field
              label="Beschreibung"
              htmlFor="bereich-beschreibung"
              help="Erscheint intern in Listen und Dialogen."
            >
              <Textarea
                value={form.description}
                onChange={(e) => setzen("description", e.target.value)}
                rows={2}
                maxLength={500}
              />
            </Field>

            <div className="grid gap-4 sm:grid-cols-2">
              <div>
                <p className="mb-1.5 text-sm font-medium text-fg">Farbe</p>
                <div className="flex flex-wrap gap-1.5">
                  {FARBEN.map((f) => (
                    <button
                      key={f}
                      type="button"
                      onClick={() => setzen("color", f)}
                      aria-label={`Farbe ${f}`}
                      aria-pressed={form.color === f}
                      className={cn(
                        "size-8 rounded-md border-2 transition-transform",
                        form.color === f
                          ? "border-fg scale-110"
                          : "border-transparent hover:scale-105",
                      )}
                      style={{ backgroundColor: f }}
                    />
                  ))}
                </div>
              </div>

              <Field label="Symbol" htmlFor="bereich-icon">
                <div className="flex items-center gap-2">
                  <span
                    className="flex size-10 shrink-0 items-center justify-center rounded-md"
                    style={{
                      backgroundColor: `${form.color}1a`,
                      color: form.color,
                    }}
                  >
                    <IconVorschau className="size-5" aria-hidden="true" />
                  </span>
                  <Select
                    value={form.icon}
                    onChange={(e) => setzen("icon", e.target.value)}
                  >
                    {TEAM_ICON_NAMES.map((n) => (
                      <option key={n} value={n}>
                        {n}
                      </option>
                    ))}
                  </Select>
                </div>
              </Field>
            </div>
          </section>

          {/* --- Portal ------------------------------------------------- */}
          <section className="space-y-4">
            <h3 className="text-sm font-semibold text-fg">Melder-Portal</h3>

            <Schalter
              an={form.portalVisible}
              onChange={(v) => setzen("portalVisible", v)}
              titel="Als Kachel im Portal anbieten"
              text="Mitarbeitende können diesem Bereich direkt schreiben. Ausgeschaltet bleibt der Bereich nur über Weiterleitung erreichbar – sinnvoll etwa für die Personalabteilung."
            />

            {form.portalVisible && (
              <>
                <Field
                  label="Beschriftung der Kachel"
                  htmlFor="bereich-portal-label"
                  help="In Alltagssprache, nicht als Fachbegriff. Zum Beispiel: Gebäude, Heizung, Licht, Reparaturen."
                >
                  <Input
                    value={form.portalLabel}
                    onChange={(e) => setzen("portalLabel", e.target.value)}
                    maxLength={120}
                    placeholder="Gebäude, Heizung, Licht, Reparaturen"
                  />
                </Field>

                <Field
                  label="Erklärung im Formular"
                  htmlFor="bereich-portal-beschreibung"
                  help="Erscheint über dem Meldeformular."
                >
                  <Textarea
                    value={form.portalDescription}
                    onChange={(e) =>
                      setzen("portalDescription", e.target.value)
                    }
                    rows={2}
                    maxLength={300}
                  />
                </Field>
              </>
            )}
          </section>

          {/* --- Verhalten ---------------------------------------------- */}
          <section className="space-y-4">
            <h3 className="text-sm font-semibold text-fg">Verhalten</h3>

            <Schalter
              an={form.allowEmailCommands}
              onChange={(v) => setzen("allowEmailCommands", v)}
              titel="Steuerbefehle per E-Mail zulassen"
              text="Mitglieder können in Antwortmails #erledigt, #intern oder #an <bereich> schreiben. Wirkt nur bei Antworten an die signierte Antwortadresse."
            />

            <Schalter
              an={form.notifyRequesterOnTransfer}
              onChange={(v) => setzen("notifyRequesterOnTransfer", v)}
              titel="Melder bei Weiterleitung informieren"
              text="Der Melder bekommt eine Mail, wenn sein Vorgang diesen Bereich verlässt."
            />

            <Schalter
              an={form.isActive}
              onChange={(v) => setzen("isActive", v)}
              titel="Bereich ist aktiv"
              text="Stillgelegte Bereiche nehmen keine neuen Vorgänge an. Bestehende bleiben erhalten."
            />
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
          <Button variant="ghost" onClick={() => setOffen(false)} disabled={pending}>
            Abbrechen
          </Button>
          <Button onClick={absenden} loading={pending}>
            {neu ? "Bereich anlegen" : "Speichern"}
          </Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  );
}

function StufenOption({
  wert,
  aktiv,
  titel,
  text,
  fuer,
  icon: Icon,
}: {
  wert: string;
  aktiv: boolean;
  titel: string;
  text: string;
  fuer: string;
  icon?: React.ComponentType<{ className?: string; "aria-hidden"?: boolean }>;
}) {
  const id = `option-${wert}`;
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
        <span className="flex items-center gap-1.5 text-sm font-medium text-fg">
          {Icon && <Icon className="size-4" aria-hidden={true} />}
          {titel}
        </span>
        <span className="mt-0.5 block text-xs text-fg-muted">{text}</span>
        <span className="mt-1 block text-xs italic text-fg-subtle">{fuer}</span>
      </span>
    </label>
  );
}

function Schalter({
  an,
  onChange,
  titel,
  text,
}: {
  an: boolean;
  onChange: (wert: boolean) => void;
  titel: string;
  text: string;
}) {
  return (
    <div className="flex items-start justify-between gap-4">
      <div className="min-w-0 flex-1">
        <p className="text-sm font-medium text-fg">{titel}</p>
        <p className="mt-0.5 text-xs text-fg-muted">{text}</p>
      </div>
      <Switch
        checked={an}
        onCheckedChange={onChange}
        aria-label={titel}
        className="mt-0.5 shrink-0"
      />
    </div>
  );
}

function Hinweis({
  children,
  warnend,
}: {
  children: React.ReactNode;
  warnend?: boolean;
}) {
  return (
    <p
      className={cn(
        "mt-2 flex items-start gap-2 rounded-md px-3 py-2 text-xs",
        warnend
          ? "bg-warning-soft text-warning-soft-fg"
          : "bg-info-soft text-info-soft-fg",
      )}
    >
      <AlertCircle className="mt-0.5 size-3.5 shrink-0" aria-hidden="true" />
      {children}
    </p>
  );
}
