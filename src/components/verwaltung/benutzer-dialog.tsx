"use client";

import { useState, useTransition } from "react";
import { useRouter } from "next/navigation";
import { GlobalRole } from "@prisma/client";
import { AlertCircle, KeyRound, Pencil, UserPlus } from "lucide-react";

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
import { Field, Input, Select } from "@/components/ui/input";
import { Switch } from "@/components/ui/misc";
import {
  benutzerAendernAction,
  benutzerAnlegenAction,
  passwortZuruecksetzenAction,
} from "@/server/actions/verwaltung";

interface BenutzerDaten {
  id: string;
  email: string;
  name: string;
  globalRole: GlobalRole;
  siteId: string | null;
  phone: string | null;
  jobTitle: string | null;
  department: string | null;
  isActive: boolean;
}

/**
 * Konto anlegen oder ändern.
 *
 * Das Passwort ist optional: Wer sich über Entra ID anmeldet, braucht keines.
 * Ein lokales Passwort setzt man für Personen ohne M365-Konto und für den
 * Notfall-Zugang, wenn die SSO-Anbindung einmal ausfällt.
 */
export function BenutzerDialog({
  benutzer,
  standorte,
}: {
  benutzer?: BenutzerDaten;
  standorte: { id: string; name: string }[];
}) {
  const router = useRouter();
  const [offen, setOffen] = useState(false);
  const [pending, startTransition] = useTransition();
  const [fehler, setFehler] = useState<string | null>(null);
  const [erfolg, setErfolg] = useState<string | null>(null);

  const neu = !benutzer;

  const [form, setForm] = useState({
    email: benutzer?.email ?? "",
    name: benutzer?.name ?? "",
    globalRole: benutzer?.globalRole ?? GlobalRole.USER,
    siteId: benutzer?.siteId ?? "",
    phone: benutzer?.phone ?? "",
    jobTitle: benutzer?.jobTitle ?? "",
    department: benutzer?.department ?? "",
    isActive: benutzer?.isActive ?? true,
  });
  const [passwort, setPasswort] = useState("");

  function setzen<K extends keyof typeof form>(feld: K, wert: (typeof form)[K]) {
    setForm((f) => ({ ...f, [feld]: wert }));
  }

  function absenden() {
    setFehler(null);
    setErfolg(null);

    startTransition(async () => {
      try {
        if (benutzer) {
          await benutzerAendernAction(benutzer.id, form);
        } else {
          await benutzerAnlegenAction({ ...form, passwort: passwort || undefined });
        }
        setOffen(false);
        router.refresh();
      } catch (e) {
        setFehler(
          e instanceof Error ? e.message : "Das Konto konnte nicht gespeichert werden.",
        );
      }
    });
  }

  function passwortSetzen() {
    setFehler(null);
    setErfolg(null);

    if (!benutzer) return;

    startTransition(async () => {
      try {
        await passwortZuruecksetzenAction(benutzer.id, passwort);
        setPasswort("");
        setErfolg(
          "Das Passwort wurde gesetzt. Geben Sie es der Person auf einem " +
            "anderen Weg als per E-Mail durch – zum Beispiel telefonisch.",
        );
      } catch (e) {
        setFehler(
          e instanceof Error ? e.message : "Das Passwort konnte nicht gesetzt werden.",
        );
      }
    });
  }

  return (
    <Dialog open={offen} onOpenChange={setOffen}>
      <DialogTrigger asChild>
        {neu ? (
          <Button size="sm">
            <UserPlus aria-hidden="true" />
            Konto anlegen
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
            {neu ? "Neues Konto anlegen" : `Konto ${benutzer.name}`}
          </DialogTitle>
          <DialogDescription>
            In welchen Bereichen diese Person arbeitet, wird anschließend über
            die Schaltfläche „Bereiche" gesetzt.
          </DialogDescription>
        </DialogHeader>

        <DialogBody className="space-y-4">
          <Field label="Name" htmlFor="benutzer-name" required>
            <Input
              value={form.name}
              onChange={(e) => setzen("name", e.target.value)}
              maxLength={120}
            />
          </Field>

          <Field
            label="E-Mail-Adresse"
            htmlFor="benutzer-email"
            required
            help="Dient als Anmeldename und für die Zuordnung bei der SSO-Anmeldung."
          >
            <Input
              type="email"
              value={form.email}
              onChange={(e) => setzen("email", e.target.value)}
            />
          </Field>

          <div className="grid gap-4 sm:grid-cols-2">
            <Field label="Funktion" htmlFor="benutzer-funktion">
              <Input
                value={form.jobTitle}
                onChange={(e) => setzen("jobTitle", e.target.value)}
                placeholder="Heilerziehungspflegerin"
                maxLength={120}
              />
            </Field>

            <Field label="Abteilung" htmlFor="benutzer-abteilung">
              <Input
                value={form.department}
                onChange={(e) => setzen("department", e.target.value)}
                placeholder="Wohnbereich"
                maxLength={120}
              />
            </Field>
          </div>

          <div className="grid gap-4 sm:grid-cols-2">
            <Field
              label="Eigener Standort"
              htmlFor="benutzer-standort"
              help="Belegt das Meldeformular vor und bestimmt die Startansicht."
            >
              <Select
                value={form.siteId}
                onChange={(e) => setzen("siteId", e.target.value)}
              >
                <option value="">Kein Standort</option>
                {standorte.map((s) => (
                  <option key={s.id} value={s.id}>
                    {s.name}
                  </option>
                ))}
              </Select>
            </Field>

            <Field label="Telefon" htmlFor="benutzer-telefon">
              <Input
                value={form.phone}
                onChange={(e) => setzen("phone", e.target.value)}
                maxLength={40}
              />
            </Field>
          </div>

          <div className="flex items-start justify-between gap-4 rounded-lg border border-line p-3">
            <div>
              <p className="text-sm font-medium text-fg">Administration</p>
              <p className="mt-0.5 text-xs text-fg-muted">
                Vollzugriff auf alle Bereiche, Vorgänge und Einstellungen.
                Sparsam vergeben.
              </p>
            </div>
            <Switch
              checked={form.globalRole === GlobalRole.ADMIN}
              onCheckedChange={(v) =>
                setzen("globalRole", v ? GlobalRole.ADMIN : GlobalRole.USER)
              }
              aria-label="Administrationsrechte"
              className="mt-0.5 shrink-0"
            />
          </div>

          <div className="flex items-start justify-between gap-4 rounded-lg border border-line p-3">
            <div>
              <p className="text-sm font-medium text-fg">Konto ist aktiv</p>
              <p className="mt-0.5 text-xs text-fg-muted">
                Gesperrte Konten können sich nicht anmelden. Ihre Vorgänge
                bleiben erhalten.
              </p>
            </div>
            <Switch
              checked={form.isActive}
              onCheckedChange={(v) => setzen("isActive", v)}
              aria-label="Konto aktiv"
              className="mt-0.5 shrink-0"
            />
          </div>

          {/* --- Passwort ---------------------------------------------- */}
          <div className="rounded-lg border border-line p-3">
            <p className="flex items-center gap-1.5 text-sm font-medium text-fg">
              <KeyRound className="size-4 text-fg-subtle" aria-hidden="true" />
              {neu ? "Lokales Passwort (optional)" : "Passwort zurücksetzen"}
            </p>
            <p className="mt-0.5 text-xs text-fg-muted">
              {neu
                ? "Nur nötig für Personen ohne Microsoft-365-Konto. Wer sich über SSO anmeldet, braucht keines."
                : "Setzt ein neues Passwort. Geben Sie es telefonisch durch, nicht per E-Mail."}
            </p>

            <div className="mt-2 flex gap-2">
              <Input
                type="text"
                value={passwort}
                onChange={(e) => setPasswort(e.target.value)}
                placeholder="Mindestens 12 Zeichen"
                autoComplete="new-password"
                className="font-mono"
              />
              {!neu && (
                <Button
                  variant="secondary"
                  onClick={passwortSetzen}
                  loading={pending}
                  disabled={!passwort}
                >
                  Setzen
                </Button>
              )}
            </div>
          </div>

          {erfolg && (
            <p
              role="status"
              className="rounded-md border border-success/30 bg-success-soft px-3 py-2.5 text-sm text-success-soft-fg"
            >
              {erfolg}
            </p>
          )}

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
            Schließen
          </Button>
          <Button onClick={absenden} loading={pending}>
            {neu ? "Konto anlegen" : "Speichern"}
          </Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  );
}
