"use client";

import { useState, useTransition } from "react";
import { useRouter } from "next/navigation";
import { MailTransport, UnknownSenderPolicy } from "@prisma/client";
import { AlertCircle, Mail, Pencil } from "lucide-react";

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
import { RadioGroup, RadioGroupItem, Switch } from "@/components/ui/misc";
import { mailkontoSpeichernAction } from "@/server/actions/verwaltung";
import { cn } from "@/lib/utils";

interface EingangWahl {
  id: string;
  name: string;
  teamName: string;
  siteName: string | null;
  inboundEmail: string | null;
}

interface KontoDaten {
  id: string;
  queueId: string;
  transport: MailTransport;
  address: string;
  isActive: boolean;
  unknownSender: UnknownSenderPolicy;
  imapHost: string | null;
  imapPort: number | null;
  imapSecure: boolean;
  imapUser: string | null;
  imapFolder: string;
  imapProcessedFolder: string | null;
  graphUserId: string | null;
}

/**
 * Postfach einrichten.
 *
 * Das Kennwort wird verschlüsselt abgelegt und nie wieder angezeigt. Beim
 * Bearbeiten heißt ein leeres Feld deshalb „unverändert" – sonst würde jedes
 * Speichern der übrigen Einstellungen das Kennwort löschen.
 */
export function MailkontoDialog({
  eingaenge,
  konto,
}: {
  eingaenge: EingangWahl[];
  konto?: KontoDaten;
}) {
  const router = useRouter();
  const [offen, setOffen] = useState(false);
  const [pending, startTransition] = useTransition();
  const [fehler, setFehler] = useState<string | null>(null);

  const neu = !konto;
  const ersterEingang = eingaenge[0];

  const [form, setForm] = useState({
    queueId: konto?.queueId ?? ersterEingang?.id ?? "",
    transport: konto?.transport ?? MailTransport.IMAP,
    address: konto?.address ?? ersterEingang?.inboundEmail ?? "",
    isActive: konto?.isActive ?? true,
    unknownSender: konto?.unknownSender ?? UnknownSenderPolicy.CREATE_CONTACT,
    imapHost: konto?.imapHost ?? "",
    imapPort: konto?.imapPort ?? 993,
    imapSecure: konto?.imapSecure ?? true,
    imapUser: konto?.imapUser ?? "",
    imapPassword: "",
    imapFolder: konto?.imapFolder ?? "INBOX",
    imapProcessedFolder: konto?.imapProcessedFolder ?? "",
    graphUserId: konto?.graphUserId ?? "",
  });

  function setzen<K extends keyof typeof form>(feld: K, wert: (typeof form)[K]) {
    setForm((f) => ({ ...f, [feld]: wert }));
  }

  const imap = form.transport === MailTransport.IMAP;

  function absenden() {
    setFehler(null);
    startTransition(async () => {
      try {
        await mailkontoSpeichernAction({ ...form, id: konto?.id });
        setOffen(false);
        router.refresh();
      } catch (e) {
        setFehler(
          e instanceof Error ? e.message : "Das Postfach konnte nicht gespeichert werden.",
        );
      }
    });
  }

  if (neu && eingaenge.length === 0) return null;

  return (
    <Dialog open={offen} onOpenChange={setOffen}>
      <DialogTrigger asChild>
        {neu ? (
          <Button size="sm">
            <Mail aria-hidden="true" />
            Postfach einrichten
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
            {neu ? "Postfach einrichten" : "Postfach bearbeiten"}
          </DialogTitle>
          <DialogDescription>
            Alle Nachrichten aus diesem Postfach werden zu Vorgängen im
            gewählten Eingang. Prüfen Sie die Verbindung nach dem Speichern.
          </DialogDescription>
        </DialogHeader>

        <DialogBody className="space-y-4">
          <Field label="Eingang" htmlFor="mail-eingang" required>
            <Select
              value={form.queueId}
              onChange={(e) => setzen("queueId", e.target.value)}
              disabled={!neu}
            >
              {eingaenge.map((q) => (
                <option key={q.id} value={q.id}>
                  {q.siteName ? `${q.teamName} – ${q.siteName}` : q.name}
                </option>
              ))}
            </Select>
          </Field>

          <Field
            label="E-Mail-Adresse des Postfachs"
            htmlFor="mail-adresse"
            required
            help="Die Adresse, an die Mitarbeitende schreiben."
          >
            <Input
              type="email"
              value={form.address}
              onChange={(e) => setzen("address", e.target.value)}
              placeholder="hausmeister-wohnheim-nord@example.org"
            />
          </Field>

          {/* --- Anbindung --------------------------------------------- */}
          <div>
            <p className="mb-1.5 text-sm font-medium text-fg">Anbindung</p>
            <RadioGroup
              value={form.transport}
              onValueChange={(v) => setzen("transport", v as MailTransport)}
              className="space-y-1.5"
            >
              <TransportOption
                wert={MailTransport.IMAP}
                aktiv={imap}
                titel="IMAP"
                text="Funktioniert mit Exchange, Microsoft 365 und jedem anderen Mailserver. Der einfachste Einstieg."
              />
              <TransportOption
                wert={MailTransport.GRAPH}
                aktiv={!imap}
                titel="Microsoft Graph"
                text="Für Microsoft 365 ohne IMAP-Freischaltung. Erfordert eine App-Registrierung durch den M365-Administrator; siehe BETRIEB.md."
              />
            </RadioGroup>
          </div>

          {imap ? (
            <>
              <div className="grid gap-4 sm:grid-cols-[1fr_6rem]">
                <Field label="Server" htmlFor="mail-host" required>
                  <Input
                    value={form.imapHost}
                    onChange={(e) => setzen("imapHost", e.target.value)}
                    placeholder="outlook.office365.com"
                  />
                </Field>
                <Field label="Port" htmlFor="mail-port">
                  <Input
                    type="number"
                    value={form.imapPort}
                    onChange={(e) => setzen("imapPort", Number(e.target.value))}
                  />
                </Field>
              </div>

              <Field label="Benutzername" htmlFor="mail-user" required>
                <Input
                  value={form.imapUser}
                  onChange={(e) => setzen("imapUser", e.target.value)}
                />
              </Field>

              <Field
                label={neu ? "Kennwort" : "Neues Kennwort"}
                htmlFor="mail-passwort"
                required={neu}
                help={
                  neu
                    ? "Wird verschlüsselt abgelegt und nie wieder angezeigt."
                    : "Leer lassen, um das bestehende Kennwort beizubehalten."
                }
              >
                <Input
                  type="password"
                  value={form.imapPassword}
                  onChange={(e) => setzen("imapPassword", e.target.value)}
                  autoComplete="new-password"
                />
              </Field>

              <div className="grid gap-4 sm:grid-cols-2">
                <Field label="Ordner" htmlFor="mail-ordner">
                  <Input
                    value={form.imapFolder}
                    onChange={(e) => setzen("imapFolder", e.target.value)}
                  />
                </Field>
                <Field
                  label="Verarbeitet verschieben nach"
                  htmlFor="mail-zielordner"
                  help="Leer lassen, um nur als gelesen zu markieren."
                >
                  <Input
                    value={form.imapProcessedFolder}
                    onChange={(e) =>
                      setzen("imapProcessedFolder", e.target.value)
                    }
                    placeholder="Verarbeitet"
                  />
                </Field>
              </div>

              <div className="flex items-center justify-between gap-4">
                <span className="text-sm text-fg">Verschlüsselte Verbindung (TLS)</span>
                <Switch
                  checked={form.imapSecure}
                  onCheckedChange={(v) => setzen("imapSecure", v)}
                  aria-label="TLS verwenden"
                />
              </div>
            </>
          ) : (
            <Field
              label="Postfachkennung"
              htmlFor="mail-graph-user"
              required
              help="Die E-Mail-Adresse oder die Objekt-ID des Postfachs in Entra ID."
            >
              <Input
                value={form.graphUserId}
                onChange={(e) => setzen("graphUserId", e.target.value)}
                placeholder="hausmeister-wohnheim-nord@example.org"
              />
            </Field>
          )}

          {/* --- Verhalten ---------------------------------------------- */}
          <Field
            label="Mails von unbekannten Absendern"
            htmlFor="mail-unbekannt"
            help="Absender, für die es im System kein Konto gibt – etwa Handwerksfirmen."
          >
            <Select
              value={form.unknownSender}
              onChange={(e) =>
                setzen("unknownSender", e.target.value as UnknownSenderPolicy)
              }
            >
              <option value={UnknownSenderPolicy.CREATE_CONTACT}>
                Als externen Melder anlegen und Vorgang erzeugen
              </option>
              <option value={UnknownSenderPolicy.QUARANTINE}>
                Vorgang erzeugen, aber zur Sichtung markieren
              </option>
              <option value={UnknownSenderPolicy.REJECT}>
                Verwerfen und nur protokollieren
              </option>
            </Select>
          </Field>

          <div className="flex items-start justify-between gap-4 rounded-lg border border-line p-3">
            <div>
              <p className="text-sm font-medium text-fg">Abruf ist aktiv</p>
              <p className="mt-0.5 text-xs text-fg-muted">
                Abgeschaltet wird das Postfach nicht mehr abgefragt. Bestehende
                Vorgänge bleiben unberührt.
              </p>
            </div>
            <Switch
              checked={form.isActive}
              onCheckedChange={(v) => setzen("isActive", v)}
              aria-label="Abruf aktiv"
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

function TransportOption({
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
  const id = `transport-${wert}`;
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
