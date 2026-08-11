"use client";

import { useState, useTransition } from "react";
import { useRouter } from "next/navigation";
import {
  CheckCircle2,
  Contrast,
  Monitor,
  Moon,
  Sun,
  Type,
  Wind,
  Sparkles,
} from "lucide-react";

import { Button } from "@/components/ui/button";
import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card";
import { Field, Input, Select } from "@/components/ui/input";
import { Switch } from "@/components/ui/misc";
import {
  setA11yPrefsAction,
  setThemeAction,
  updateProfileAction,
} from "@/server/actions/preferences";
import { cn } from "@/lib/utils";

/**
 * Persönliche Einstellungen.
 *
 * Die Barrierefreiheitsmodi stehen bewusst gleichrangig neben dem Profil und
 * nicht in einem Untermenü: In einer Einrichtung der Behindertenhilfe sind sie
 * für einen Teil der Belegschaft die Voraussetzung dafür, das System überhaupt
 * bedienen zu können – kein Beiwerk.
 *
 * Jeder Modus wird sofort angewendet, damit man die Wirkung sieht, statt sie
 * sich vorstellen zu müssen.
 */
export function EinstellungenFormular({
  nutzer,
  standorte,
  theme,
  a11yPrefs,
}: {
  nutzer: {
    name: string;
    email: string;
    phone: string | null;
    siteId: string | null;
    jobTitle: string | null;
    hatPasswort: boolean;
  };
  standorte: { id: string; name: string }[];
  theme: string;
  a11yPrefs: Record<string, boolean>;
}) {
  const router = useRouter();
  const [pending, startTransition] = useTransition();
  const [gespeichert, setGespeichert] = useState(false);

  const [profil, setProfil] = useState({
    name: nutzer.name,
    phone: nutzer.phone ?? "",
    siteId: nutzer.siteId ?? "",
  });

  function profilSpeichern(e: React.FormEvent) {
    e.preventDefault();
    setGespeichert(false);

    startTransition(async () => {
      await updateProfileAction(profil);
      setGespeichert(true);
      router.refresh();
      setTimeout(() => setGespeichert(false), 4000);
    });
  }

  function schalte(key: string, wert: boolean) {
    startTransition(async () => {
      await setA11yPrefsAction({ [key]: wert });
      router.refresh();
    });
  }

  return (
    <div className="mx-auto max-w-2xl space-y-5">
      {/* --- Profil ------------------------------------------------------ */}
      <Card>
        <CardHeader>
          <CardTitle>Ihr Profil</CardTitle>
        </CardHeader>
        <CardContent>
          <form onSubmit={profilSpeichern} className="space-y-4">
            <Field label="Name" htmlFor="einst-name" required>
              <Input
                value={profil.name}
                onChange={(e) =>
                  setProfil((p) => ({ ...p, name: e.target.value }))
                }
                maxLength={120}
              />
            </Field>

            <Field
              label="E-Mail-Adresse"
              htmlFor="einst-email"
              help="Ändert die Administration – sie ist zugleich Ihr Anmeldename."
            >
              <Input value={nutzer.email} disabled />
            </Field>

            <Field
              label="Ihr Standort"
              htmlFor="einst-standort"
              help="Wird beim Melden vorausgewählt. Wenn Sie das Haus wechseln, stellen Sie es hier um."
            >
              <Select
                value={profil.siteId}
                onChange={(e) =>
                  setProfil((p) => ({ ...p, siteId: e.target.value }))
                }
              >
                <option value="">Kein Standort</option>
                {standorte.map((s) => (
                  <option key={s.id} value={s.id}>
                    {s.name}
                  </option>
                ))}
              </Select>
            </Field>

            <Field
              label="Telefon"
              htmlFor="einst-telefon"
              help="Damit wir bei Rückfragen anrufen können, statt zu schreiben."
            >
              <Input
                value={profil.phone}
                onChange={(e) =>
                  setProfil((p) => ({ ...p, phone: e.target.value }))
                }
                maxLength={40}
              />
            </Field>

            <div className="flex items-center gap-3">
              <Button type="submit" loading={pending}>
                Speichern
              </Button>
              {gespeichert && (
                <span
                  role="status"
                  className="flex items-center gap-1.5 text-sm text-success-soft-fg"
                >
                  <CheckCircle2 className="size-4" aria-hidden="true" />
                  Gespeichert.
                </span>
              )}
            </div>
          </form>
        </CardContent>
      </Card>

      {/* --- Darstellung -------------------------------------------------- */}
      <Card>
        <CardHeader>
          <CardTitle>Darstellung</CardTitle>
        </CardHeader>
        <CardContent>
          <fieldset>
            <legend className="mb-2 text-sm font-medium text-fg">
              Farbschema
            </legend>
            <div className="flex gap-2">
              {(
                [
                  ["light", Sun, "Hell"],
                  ["dark", Moon, "Dunkel"],
                  ["system", Monitor, "Wie das Gerät"],
                ] as const
              ).map(([wert, Icon, label]) => (
                <button
                  key={wert}
                  type="button"
                  onClick={() =>
                    startTransition(async () => {
                      await setThemeAction(wert);
                      router.refresh();
                    })
                  }
                  aria-pressed={theme === wert}
                  className={cn(
                    "flex flex-1 flex-col items-center gap-1.5 rounded-lg border p-3 text-sm transition-colors",
                    theme === wert
                      ? "border-primary bg-primary-soft text-primary-soft-fg"
                      : "border-line text-fg-muted hover:bg-surface-2",
                  )}
                >
                  <Icon className="size-5" aria-hidden="true" />
                  {label}
                </button>
              ))}
            </div>
          </fieldset>
        </CardContent>
      </Card>

      {/* --- Barrierefreiheit --------------------------------------------- */}
      <Card>
        <CardHeader>
          <CardTitle>Barrierefreiheit</CardTitle>
        </CardHeader>
        <CardContent className="space-y-1">
          <Modus
            an={Boolean(a11yPrefs.largeText)}
            onChange={(v) => schalte("largeText", v)}
            icon={Type}
            titel="Große Schrift"
            text="Vergrößert Schrift, Abstände und Schaltflächen im ganzen System."
            pending={pending}
          />
          <Modus
            an={Boolean(a11yPrefs.highContrast)}
            onChange={(v) => schalte("highContrast", v)}
            icon={Contrast}
            titel="Hoher Kontrast"
            text="Verstärkt Linien, Text und Fokusrahmen deutlich."
            pending={pending}
          />
          <Modus
            an={Boolean(a11yPrefs.reduceMotion)}
            onChange={(v) => schalte("reduceMotion", v)}
            icon={Wind}
            titel="Bewegung reduzieren"
            text="Schaltet Übergänge und Animationen ab."
            pending={pending}
          />
          <Modus
            an={Boolean(a11yPrefs.simpleMode)}
            onChange={(v) => schalte("simpleMode", v)}
            icon={Sparkles}
            titel="Einfache Ansicht"
            text="Größere Schaltflächen, weniger Felder und kürzere Texte beim Melden. Optionale Angaben entfallen."
            pending={pending}
          />
        </CardContent>
      </Card>

      {/* --- Anmeldung ---------------------------------------------------- */}
      <Card>
        <CardHeader>
          <CardTitle>Anmeldung</CardTitle>
        </CardHeader>
        <CardContent>
          <p className="text-sm text-fg-muted">
            {nutzer.hatPasswort
              ? "Für Ihr Konto ist ein lokales Passwort hinterlegt. Wenn Sie es ändern oder zurücksetzen möchten, wenden Sie sich an die IT."
              : "Sie melden sich über Ihr Dienstkonto an. Ein eigenes Passwort für das Ticketsystem gibt es nicht."}
          </p>
        </CardContent>
      </Card>
    </div>
  );
}

function Modus({
  an,
  onChange,
  icon: Icon,
  titel,
  text,
  pending,
}: {
  an: boolean;
  onChange: (wert: boolean) => void;
  icon: React.ComponentType<{ className?: string; "aria-hidden"?: boolean }>;
  titel: string;
  text: string;
  pending: boolean;
}) {
  return (
    <div className="flex items-start gap-3 rounded-lg py-2.5">
      <span
        className={cn(
          "flex size-9 shrink-0 items-center justify-center rounded-lg",
          an ? "bg-primary-soft text-primary-soft-fg" : "bg-surface-2 text-fg-subtle",
        )}
      >
        <Icon className="size-4.5" aria-hidden={true} />
      </span>

      <div className="min-w-0 flex-1">
        <p className="text-sm font-medium text-fg">{titel}</p>
        <p className="mt-0.5 text-sm text-fg-muted">{text}</p>
      </div>

      <Switch
        checked={an}
        onCheckedChange={onChange}
        disabled={pending}
        aria-label={titel}
        className="mt-1 shrink-0"
      />
    </div>
  );
}
