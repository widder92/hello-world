"use client";

import { useState, useTransition } from "react";
import { useRouter } from "next/navigation";
import { AlertCircle, Factory, Package, Pencil, Wallet } from "lucide-react";

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
import {
  geraeteartSpeichernAction,
  herstellerSpeichernAction,
  kostenstelleSpeichernAction,
} from "@/server/actions/inventar";

/**
 * Dialoge für die Stammdaten des Inventars.
 *
 * Alle drei nach demselben Muster wie der Standort-Dialog: derselbe Dialog für
 * Anlegen und Bearbeiten, Fehler aus der Server-Aktion werden im Dialog
 * angezeigt statt die Seite zu verlassen – wer zehn Kostenstellen einträgt,
 * soll bei einem Tippfehler nicht von vorn anfangen.
 */

function Fehlermeldung({ text }: { text: string }) {
  return (
    <p
      role="alert"
      className="flex items-start gap-2 rounded-md border border-danger/30 bg-danger-soft px-3 py-2.5 text-sm text-danger-soft-fg"
    >
      <AlertCircle className="mt-0.5 size-4 shrink-0" aria-hidden="true" />
      {text}
    </p>
  );
}

function AktivSchalter({
  checked,
  onChange,
  titel,
  hinweis,
  label,
}: {
  checked: boolean;
  onChange: (v: boolean) => void;
  titel: string;
  hinweis: string;
  label: string;
}) {
  return (
    <div className="flex items-start justify-between gap-4">
      <div>
        <p className="text-sm font-medium text-fg">{titel}</p>
        <p className="mt-0.5 text-xs text-fg-muted">{hinweis}</p>
      </div>
      <Switch
        checked={checked}
        onCheckedChange={onChange}
        aria-label={label}
        className="mt-0.5 shrink-0"
      />
    </div>
  );
}

// ---------------------------------------------------------------------------
// Geräteart
// ---------------------------------------------------------------------------

export function GeraeteartDialog({
  art,
  kostenstellen,
}: {
  art?: {
    id: string;
    key: string;
    name: string;
    icon: string;
    requiresSerial: boolean;
    requiresMac: boolean;
    defaultCostCenterId: string | null;
    isActive: boolean;
  };
  kostenstellen: { id: string; code: string; name: string }[];
}) {
  const router = useRouter();
  const [offen, setOffen] = useState(false);
  const [pending, startTransition] = useTransition();
  const [fehler, setFehler] = useState<string | null>(null);

  const neu = !art;

  const [form, setForm] = useState({
    key: art?.key ?? "",
    name: art?.name ?? "",
    icon: art?.icon ?? "Package",
    requiresSerial: art?.requiresSerial ?? true,
    requiresMac: art?.requiresMac ?? false,
    defaultCostCenterId: art?.defaultCostCenterId ?? "",
    isActive: art?.isActive ?? true,
  });

  function setzen<K extends keyof typeof form>(feld: K, wert: (typeof form)[K]) {
    setForm((f) => ({ ...f, [feld]: wert }));
  }

  function absenden() {
    setFehler(null);
    startTransition(async () => {
      try {
        await geraeteartSpeichernAction({ ...form, id: art?.id });
        setOffen(false);
        router.refresh();
      } catch (e) {
        setFehler(
          e instanceof Error
            ? e.message
            : "Die Geräteart konnte nicht gespeichert werden.",
        );
      }
    });
  }

  return (
    <Dialog open={offen} onOpenChange={setOffen}>
      <DialogTrigger asChild>
        {neu ? (
          <Button size="sm">
            <Package aria-hidden="true" />
            Geräteart anlegen
          </Button>
        ) : (
          <Button variant="secondary" size="sm">
            <Pencil aria-hidden="true" />
            Bearbeiten
          </Button>
        )}
      </DialogTrigger>

      <DialogContent size="sm">
        <DialogHeader>
          <DialogTitle>
            {neu ? "Neue Geräteart anlegen" : `Geräteart ${art.name}`}
          </DialogTitle>
          <DialogDescription>
            Gerätearten bestimmen, welche Angaben beim Anlegen verlangt werden
            und womit die Inventarnummer beginnt.
          </DialogDescription>
        </DialogHeader>

        <DialogBody className="space-y-4">
          <Field label="Bezeichnung" htmlFor="art-name" required>
            <Input
              value={form.name}
              onChange={(e) => setzen("name", e.target.value)}
              placeholder="Headset"
              maxLength={60}
            />
          </Field>

          <Field
            label="Kürzel"
            htmlFor="art-key"
            required
            help={
              neu
                ? "Beginnt jede Inventarnummer dieser Art, z. B. HS-00001. Danach nicht mehr änderbar."
                : "Steht bereits in den Inventarnummern und lässt sich deshalb nicht mehr ändern."
            }
          >
            <Input
              value={form.key}
              onChange={(e) => setzen("key", e.target.value.toUpperCase())}
              placeholder="HS"
              maxLength={5}
              disabled={!neu}
              className="font-mono uppercase"
            />
          </Field>

          <Field
            label="Kostenstelle als Vorschlag"
            htmlFor="art-kst"
            help="Wird beim Anlegen eines Geräts dieser Art vorbelegt und lässt sich dort ändern."
          >
            <Select
              value={form.defaultCostCenterId}
              onChange={(e) => setzen("defaultCostCenterId", e.target.value)}
            >
              <option value="">Keine Vorbelegung</option>
              {kostenstellen.map((k) => (
                <option key={k.id} value={k.id}>
                  {k.code} – {k.name}
                </option>
              ))}
            </Select>
          </Field>

          <AktivSchalter
            checked={form.requiresSerial}
            onChange={(v) => setzen("requiresSerial", v)}
            titel="Seriennummer verlangen"
            hinweis="Sinnvoll bei allem, was eine hat. Bei Lizenzen und Zubehör abschalten."
            label="Seriennummer verlangen"
          />

          <AktivSchalter
            checked={form.requiresMac}
            onChange={(v) => setzen("requiresMac", v)}
            titel="Nach MAC-Adresse fragen"
            hinweis="Blendet die Felder für Netzwerkadressen im Formular ein."
            label="Nach MAC-Adresse fragen"
          />

          <AktivSchalter
            checked={form.isActive}
            onChange={(v) => setzen("isActive", v)}
            titel="Geräteart ist aktiv"
            hinweis="Nicht aktive Arten lassen sich nicht mehr auswählen. Bestehende Geräte bleiben unverändert."
            label="Geräteart aktiv"
          />

          {fehler && <Fehlermeldung text={fehler} />}
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

// ---------------------------------------------------------------------------
// Hersteller
// ---------------------------------------------------------------------------

export function HerstellerDialog({
  hersteller,
}: {
  hersteller?: {
    id: string;
    name: string;
    website: string | null;
    supportPhone: string | null;
    notes: string | null;
    isActive: boolean;
  };
}) {
  const router = useRouter();
  const [offen, setOffen] = useState(false);
  const [pending, startTransition] = useTransition();
  const [fehler, setFehler] = useState<string | null>(null);

  const neu = !hersteller;

  const [form, setForm] = useState({
    name: hersteller?.name ?? "",
    website: hersteller?.website ?? "",
    supportPhone: hersteller?.supportPhone ?? "",
    notes: hersteller?.notes ?? "",
    isActive: hersteller?.isActive ?? true,
  });

  function setzen<K extends keyof typeof form>(feld: K, wert: (typeof form)[K]) {
    setForm((f) => ({ ...f, [feld]: wert }));
  }

  function absenden() {
    setFehler(null);
    startTransition(async () => {
      try {
        await herstellerSpeichernAction({ ...form, id: hersteller?.id });
        setOffen(false);
        router.refresh();
      } catch (e) {
        setFehler(
          e instanceof Error
            ? e.message
            : "Der Hersteller konnte nicht gespeichert werden.",
        );
      }
    });
  }

  return (
    <Dialog open={offen} onOpenChange={setOffen}>
      <DialogTrigger asChild>
        {neu ? (
          <Button size="sm">
            <Factory aria-hidden="true" />
            Hersteller anlegen
          </Button>
        ) : (
          <Button variant="secondary" size="sm">
            <Pencil aria-hidden="true" />
            Bearbeiten
          </Button>
        )}
      </DialogTrigger>

      <DialogContent size="sm">
        <DialogHeader>
          <DialogTitle>
            {neu ? "Neuen Hersteller anlegen" : `Hersteller ${hersteller.name}`}
          </DialogTitle>
        </DialogHeader>

        <DialogBody className="space-y-4">
          <Field label="Name" htmlFor="hersteller-name" required>
            <Input
              value={form.name}
              onChange={(e) => setzen("name", e.target.value)}
              placeholder="Lenovo"
              maxLength={60}
            />
          </Field>

          <Field
            label="Support-Telefon"
            htmlFor="hersteller-telefon"
            help="Steht später beim Gerät – im Störungsfall muss sie niemand suchen."
          >
            <Input
              value={form.supportPhone}
              onChange={(e) => setzen("supportPhone", e.target.value)}
              placeholder="0800 500 4618"
              maxLength={40}
            />
          </Field>

          <Field label="Internetseite" htmlFor="hersteller-web">
            <Input
              type="url"
              value={form.website}
              onChange={(e) => setzen("website", e.target.value)}
              placeholder="https://www.lenovo.com"
              maxLength={200}
            />
          </Field>

          <Field
            label="Notizen"
            htmlFor="hersteller-notizen"
            help="Etwa Kundennummer, Rahmenvertrag oder Ansprechpartner."
          >
            <Textarea
              value={form.notes}
              onChange={(e) => setzen("notes", e.target.value)}
              rows={3}
              maxLength={1000}
            />
          </Field>

          <AktivSchalter
            checked={form.isActive}
            onChange={(v) => setzen("isActive", v)}
            titel="Hersteller ist aktiv"
            hinweis="Nicht aktive Hersteller lassen sich nicht mehr auswählen. Bestehende Geräte behalten ihn."
            label="Hersteller aktiv"
          />

          {fehler && <Fehlermeldung text={fehler} />}
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

// ---------------------------------------------------------------------------
// Kostenstelle
// ---------------------------------------------------------------------------

export function KostenstelleDialog({
  kostenstelle,
}: {
  kostenstelle?: {
    id: string;
    code: string;
    name: string;
    description: string | null;
    isActive: boolean;
  };
}) {
  const router = useRouter();
  const [offen, setOffen] = useState(false);
  const [pending, startTransition] = useTransition();
  const [fehler, setFehler] = useState<string | null>(null);

  const neu = !kostenstelle;

  const [form, setForm] = useState({
    code: kostenstelle?.code ?? "",
    name: kostenstelle?.name ?? "",
    description: kostenstelle?.description ?? "",
    isActive: kostenstelle?.isActive ?? true,
  });

  function setzen<K extends keyof typeof form>(feld: K, wert: (typeof form)[K]) {
    setForm((f) => ({ ...f, [feld]: wert }));
  }

  function absenden() {
    setFehler(null);
    startTransition(async () => {
      try {
        await kostenstelleSpeichernAction({ ...form, id: kostenstelle?.id });
        setOffen(false);
        router.refresh();
      } catch (e) {
        setFehler(
          e instanceof Error
            ? e.message
            : "Die Kostenstelle konnte nicht gespeichert werden.",
        );
      }
    });
  }

  return (
    <Dialog open={offen} onOpenChange={setOffen}>
      <DialogTrigger asChild>
        {neu ? (
          <Button size="sm">
            <Wallet aria-hidden="true" />
            Kostenstelle anlegen
          </Button>
        ) : (
          <Button variant="secondary" size="sm">
            <Pencil aria-hidden="true" />
            Bearbeiten
          </Button>
        )}
      </DialogTrigger>

      <DialogContent size="sm">
        <DialogHeader>
          <DialogTitle>
            {neu
              ? "Neue Kostenstelle anlegen"
              : `Kostenstelle ${kostenstelle.code}`}
          </DialogTitle>
          <DialogDescription>
            Kostenstellen sind die Achse der Auswertung. Bitte genau so
            anlegen, wie sie in der Buchhaltung heißen.
          </DialogDescription>
        </DialogHeader>

        <DialogBody className="space-y-4">
          <Field label="Nummer" htmlFor="kst-code" required>
            <Input
              value={form.code}
              onChange={(e) => setzen("code", e.target.value)}
              placeholder="4310"
              maxLength={20}
              className="font-mono"
            />
          </Field>

          <Field label="Bezeichnung" htmlFor="kst-name" required>
            <Input
              value={form.name}
              onChange={(e) => setzen("name", e.target.value)}
              placeholder="Wohnheim Nord"
              maxLength={80}
            />
          </Field>

          <Field label="Beschreibung" htmlFor="kst-beschreibung">
            <Textarea
              value={form.description}
              onChange={(e) => setzen("description", e.target.value)}
              rows={2}
              maxLength={500}
            />
          </Field>

          <AktivSchalter
            checked={form.isActive}
            onChange={(v) => setzen("isActive", v)}
            titel="Kostenstelle ist aktiv"
            hinweis="Nicht aktive Kostenstellen lassen sich nicht mehr zuordnen, bleiben in der Auswertung vergangener Jahre aber sichtbar."
            label="Kostenstelle aktiv"
          />

          {fehler && <Fehlermeldung text={fehler} />}
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
