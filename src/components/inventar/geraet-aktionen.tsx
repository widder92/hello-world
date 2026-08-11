"use client";

import { useEffect, useRef, useState, useTransition } from "react";
import { useRouter } from "next/navigation";
import { AssetStatus } from "@prisma/client";
import {
  AlertCircle,
  ArrowRightLeft,
  Camera,
  Images,
  MessageSquarePlus,
  Trash2,
} from "lucide-react";

import { Button } from "@/components/ui/button";
import {
  Dialog,
  DialogBody,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
} from "@/components/ui/dialog";
import { Field, Input, Textarea } from "@/components/ui/input";
import {
  fotoEntfernenAction,
  fotoSetzenAction,
  modellFotoSetzenAction,
  notizAction,
  statusWechselnAction,
  suchePerson,
} from "@/server/actions/inventar";
import { ASSET_STATUS, ASSET_STATUS_HINWEIS } from "@/lib/domain";

/**
 * Bedienelemente auf der Geräteseite.
 *
 * Der Statuswechsel ist ein Dialog und kein Auswahlfeld: Herausgeben und
 * Aussondern brauchen zusätzliche Angaben – Person, Zubehör, Grund –, und ein
 * Auswahlfeld, das je nach Wert weitere Felder aufklappt, wird beim ersten
 * Ablenken falsch bedient.
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

// ---------------------------------------------------------------------------
// Statuswechsel
// ---------------------------------------------------------------------------

type Person = {
  id: string;
  name: string;
  email: string;
  jobTitle: string | null;
  site: { name: string } | null;
};

export function StatusDialog({
  assetId,
  assetTag,
  von,
  ziele,
}: {
  assetId: string;
  assetTag: string;
  von: AssetStatus;
  /** Erlaubte Ziele samt der Angaben, die sie verlangen. */
  ziele: {
    status: AssetStatus;
    brauchtPerson: boolean;
    brauchtGrund: boolean;
    mitProtokoll: boolean;
  }[];
}) {
  const router = useRouter();
  const [offen, setOffen] = useState<AssetStatus | null>(null);
  const [pending, startTransition] = useTransition();
  const [fehler, setFehler] = useState<string | null>(null);

  const [suche, setSuche] = useState("");
  const [treffer, setTreffer] = useState<Person[]>([]);
  const [person, setPerson] = useState<Person | null>(null);
  const [note, setNote] = useState("");
  const [condition, setCondition] = useState("");
  const [accessories, setAccessories] = useState("");

  const regel = ziele.find((z) => z.status === offen);
  const suchZaehler = useRef(0);

  useEffect(() => {
    if (!offen || !regel?.brauchtPerson) return;
    if (suche.trim().length < 2) {
      setTreffer([]);
      return;
    }
    // Kurz warten, damit nicht jeder Tastendruck eine Abfrage auslöst.
    const lauf = ++suchZaehler.current;
    const zeit = setTimeout(async () => {
      const ergebnis = await suchePerson(suche);
      // Nur die Antwort auf die zuletzt getippte Eingabe übernehmen.
      if (lauf === suchZaehler.current) setTreffer(ergebnis);
    }, 250);
    return () => clearTimeout(zeit);
  }, [suche, offen, regel?.brauchtPerson]);

  function zuruecksetzen() {
    setSuche("");
    setTreffer([]);
    setPerson(null);
    setNote("");
    setCondition("");
    setAccessories("");
    setFehler(null);
  }

  function oeffnen(status: AssetStatus) {
    zuruecksetzen();
    setOffen(status);
  }

  function absenden() {
    if (!offen) return;
    setFehler(null);
    startTransition(async () => {
      try {
        const ergebnis = await statusWechselnAction({
          assetId,
          ziel: offen,
          userId: person?.id ?? "",
          note,
          condition,
          accessories,
        });
        setOffen(null);
        zuruecksetzen();
        // Direkt zum Protokoll, wenn eines entstanden ist – es soll ja
        // ausgedruckt und unterschrieben werden.
        if (ergebnis.protokollId) {
          router.push(`/inventar/${assetId}/protokoll/${ergebnis.protokollId}`);
        } else {
          router.refresh();
        }
      } catch (e) {
        setFehler(
          e instanceof Error
            ? e.message
            : "Der Status konnte nicht geändert werden.",
        );
      }
    });
  }

  return (
    <>
      <div className="flex flex-wrap gap-2">
        {ziele.map((z) => (
          <Button
            key={z.status}
            variant={z.status === AssetStatus.HERAUSGEGEBEN ? "primary" : "secondary"}
            size="sm"
            onClick={() => oeffnen(z.status)}
          >
            <ArrowRightLeft aria-hidden="true" />
            {beschriftung(von, z.status)}
          </Button>
        ))}
      </div>

      <Dialog
        open={offen !== null}
        onOpenChange={(o) => {
          if (!o) setOffen(null);
        }}
      >
        <DialogContent size="sm">
          <DialogHeader>
            <DialogTitle>
              {offen ? beschriftung(von, offen) : ""} – {assetTag}
            </DialogTitle>
            <DialogDescription>
              {offen ? ASSET_STATUS_HINWEIS[offen] : ""}
              {regel?.mitProtokoll &&
                " Beim Speichern entsteht ein Übergabeprotokoll zum Ausdrucken."}
            </DialogDescription>
          </DialogHeader>

          <DialogBody className="space-y-4">
            {regel?.brauchtPerson && (
              <div className="space-y-2">
                <Field
                  label="Person"
                  htmlFor="status-person"
                  required
                  help="Name oder E-Mail-Adresse eingeben."
                >
                  <Input
                    value={person ? person.name : suche}
                    onChange={(e) => {
                      setPerson(null);
                      setSuche(e.target.value);
                    }}
                    placeholder="Bergmann"
                    autoComplete="off"
                  />
                </Field>

                {person ? (
                  <p className="rounded-md border border-line bg-surface-2 px-3 py-2 text-sm">
                    <span className="font-medium text-fg">{person.name}</span>
                    <span className="block text-xs text-fg-muted">
                      {[person.jobTitle, person.site?.name, person.email]
                        .filter(Boolean)
                        .join(" · ")}
                    </span>
                  </p>
                ) : (
                  treffer.length > 0 && (
                    <ul className="max-h-48 divide-y divide-line overflow-y-auto rounded-md border border-line">
                      {treffer.map((p) => (
                        <li key={p.id}>
                          <button
                            type="button"
                            onClick={() => {
                              setPerson(p);
                              setTreffer([]);
                            }}
                            className="w-full px-3 py-2 text-left text-sm transition-colors hover:bg-surface-2"
                          >
                            <span className="font-medium text-fg">{p.name}</span>
                            <span className="block text-xs text-fg-muted">
                              {[p.jobTitle, p.site?.name].filter(Boolean).join(" · ")}
                            </span>
                          </button>
                        </li>
                      ))}
                    </ul>
                  )
                )}
              </div>
            )}

            {regel?.mitProtokoll && (
              <>
                <Field
                  label="Zustand"
                  htmlFor="status-zustand"
                  help="Steht später auf dem Protokoll."
                >
                  <Input
                    value={condition}
                    onChange={(e) => setCondition(e.target.value)}
                    placeholder="Neugerät, unbeschädigt"
                    maxLength={200}
                  />
                </Field>

                <Field label="Zubehör" htmlFor="status-zubehoer">
                  <Input
                    value={accessories}
                    onChange={(e) => setAccessories(e.target.value)}
                    placeholder="Netzteil, Tasche, Maus"
                    maxLength={500}
                  />
                </Field>
              </>
            )}

            <Field
              label={regel?.brauchtGrund ? "Grund" : "Bemerkung"}
              htmlFor="status-grund"
              required={regel?.brauchtGrund}
              help={
                regel?.brauchtGrund
                  ? "Ohne Begründung lässt sich später nicht nachvollziehen, was mit dem Gerät geschehen ist."
                  : undefined
              }
            >
              <Textarea
                value={note}
                onChange={(e) => setNote(e.target.value)}
                rows={3}
                maxLength={2000}
              />
            </Field>

            {fehler && <Fehlermeldung text={fehler} />}
          </DialogBody>

          <DialogFooter>
            <Button variant="ghost" onClick={() => setOffen(null)} disabled={pending}>
              Abbrechen
            </Button>
            <Button onClick={absenden} loading={pending}>
              Speichern
            </Button>
          </DialogFooter>
        </DialogContent>
      </Dialog>
    </>
  );
}

/**
 * Beschriftung eines Übergangs in Alltagssprache.
 *
 * „Herausgeben" sagt mehr als „Status auf HERAUSGEGEBEN setzen", und der
 * Rückweg heißt im Alltag „Zurücknehmen" – nicht „ins Lager buchen".
 */
function beschriftung(von: AssetStatus, nach: AssetStatus): string {
  if (nach === AssetStatus.HERAUSGEGEBEN) {
    return von === AssetStatus.HERAUSGEGEBEN ? "An andere Person" : "Herausgeben";
  }
  if (nach === AssetStatus.IN_VORBEREITUNG) {
    // Von einer Person zurück heißt: erst einsammeln, dann herrichten.
    return von === AssetStatus.HERAUSGEGEBEN
      ? "Zurücknehmen und einrichten"
      : "Einrichten";
  }
  if (nach === AssetStatus.BEREIT) return "Bereitstellen";
  if (nach === AssetStatus.LAGER) {
    return von === AssetStatus.HERAUSGEGEBEN ? "Zurücknehmen" : "Ins Lager";
  }
  if (nach === AssetStatus.REPARATUR) return "In Reparatur geben";
  if (nach === AssetStatus.RESERVIERT) return "Vormerken";
  if (nach === AssetStatus.AUSGESONDERT) return "Aussondern";
  if (nach === AssetStatus.VERLUST) return "Als Verlust melden";
  return ASSET_STATUS[nach];
}

// ---------------------------------------------------------------------------
// Foto
// ---------------------------------------------------------------------------

export function FotoBereich({
  assetId,
  fotoId,
  modellFotoId,
  modellName,
  modellId,
}: {
  assetId: string;
  /** Bild genau dieses Geräts – hat Vorrang. */
  fotoId: string | null;
  /** Bild des Modells, das sonst gilt. */
  modellFotoId?: string | null;
  modellName?: string | null;
  modellId?: string | null;
}) {
  const router = useRouter();
  const [pending, startTransition] = useTransition();
  const [fehler, setFehler] = useState<string | null>(null);
  const eingabe = useRef<HTMLInputElement>(null);
  const modellEingabe = useRef<HTMLInputElement>(null);
  const [modellPending, startModell] = useTransition();

  /** Bild am Modell hinterlegen – gilt sofort für alle Geräte dieser Bauart. */
  function modellBildHochladen(datei: File) {
    if (!modellId) return;
    setFehler(null);
    const daten = new FormData();
    daten.set("modelId", modellId);
    daten.set("datei", datei);
    startModell(async () => {
      try {
        await modellFotoSetzenAction(daten);
        router.refresh();
      } catch (e) {
        setFehler(
          e instanceof Error ? e.message : "Das Bild konnte nicht gespeichert werden.",
        );
      }
    });
  }

  function hochladen(datei: File) {
    setFehler(null);
    const daten = new FormData();
    daten.set("assetId", assetId);
    daten.set("datei", datei);
    startTransition(async () => {
      try {
        await fotoSetzenAction(daten);
        router.refresh();
      } catch (e) {
        setFehler(
          e instanceof Error ? e.message : "Das Bild konnte nicht gespeichert werden.",
        );
      }
    });
  }

  function entfernen() {
    setFehler(null);
    startTransition(async () => {
      try {
        await fotoEntfernenAction(assetId);
        router.refresh();
      } catch (e) {
        setFehler(
          e instanceof Error ? e.message : "Das Bild konnte nicht entfernt werden.",
        );
      }
    });
  }

  // Das eigene Bild geht vor, sonst das des Modells.
  const gezeigt = fotoId ?? modellFotoId ?? null;
  const vomModell = !fotoId && Boolean(modellFotoId);

  return (
    <div className="space-y-2">
      <div className="overflow-hidden rounded-lg border border-line bg-surface-2">
        {gezeigt ? (
          // eslint-disable-next-line @next/next/no-img-element
          <img
            src={`/api/uploads/${gezeigt}`}
            alt={vomModell ? `Bild des Modells ${modellName ?? ""}` : "Foto des Geräts"}
            className="max-h-64 w-full object-contain"
          />
        ) : (
          <p className="px-4 py-10 text-center text-sm text-fg-muted">
            Noch kein Bild. Eines am Modell zu hinterlegen lohnt sich: Es gilt
            dann für alle Geräte derselben Bauart.
          </p>
        )}
      </div>

      {vomModell && (
        <p className="text-xs text-fg-muted">
          Bild des Modells {modellName}. Es gilt für alle Geräte dieser Bauart –
          ein eigenes Bild braucht dieses Gerät nur, wenn es anders aussieht.
        </p>
      )}

      <input
        ref={eingabe}
        type="file"
        accept="image/*"
        capture="environment"
        className="sr-only"
        aria-label="Foto des Geräts auswählen"
        tabIndex={-1}
        onChange={(e) => {
          const datei = e.target.files?.[0];
          if (datei) hochladen(datei);
          e.target.value = "";
        }}
      />

      <input
        ref={modellEingabe}
        type="file"
        accept="image/*"
        capture="environment"
        className="sr-only"
        aria-label="Bild des Modells auswählen"
        tabIndex={-1}
        onChange={(e) => {
          const datei = e.target.files?.[0];
          if (datei) modellBildHochladen(datei);
          e.target.value = "";
        }}
      />

      <div className="flex flex-wrap gap-2">
        <Button
          variant="secondary"
          size="sm"
          onClick={() => eingabe.current?.click()}
          loading={pending}
        >
          <Camera aria-hidden="true" />
          {fotoId
            ? "Gerätebild ersetzen"
            : vomModell
              ? "Abweichendes Bild für dieses Gerät"
              : "Foto aufnehmen oder wählen"}
        </Button>

        {modellId && (
          <Button
            variant="secondary"
            size="sm"
            onClick={() => modellEingabe.current?.click()}
            loading={modellPending}
          >
            <Images aria-hidden="true" />
            {modellFotoId ? "Bild des Modells ersetzen" : "Bild für das Modell"}
          </Button>
        )}

        {fotoId && (
          <Button variant="ghost" size="sm" onClick={entfernen} disabled={pending}>
            <Trash2 aria-hidden="true" />
            Gerätebild entfernen
          </Button>
        )}
      </div>

      {fehler && <Fehlermeldung text={fehler} />}
    </div>
  );
}

// ---------------------------------------------------------------------------
// Vermerk
// ---------------------------------------------------------------------------

export function NotizFeld({ assetId }: { assetId: string }) {
  const router = useRouter();
  const [text, setText] = useState("");
  const [pending, startTransition] = useTransition();
  const [fehler, setFehler] = useState<string | null>(null);

  function absenden(e: React.FormEvent) {
    e.preventDefault();
    setFehler(null);
    startTransition(async () => {
      try {
        await notizAction({ assetId, note: text });
        setText("");
        router.refresh();
      } catch (e) {
        setFehler(
          e instanceof Error ? e.message : "Der Vermerk konnte nicht gespeichert werden.",
        );
      }
    });
  }

  return (
    <form onSubmit={absenden} className="space-y-2">
      <Field
        label="Vermerk in der Chronik"
        htmlFor="geraet-notiz"
        help="Für alles, was keinen Statuswechsel bedeutet – Reparaturtermin, Auffälligkeit, Rücksprache."
      >
        <Textarea
          value={text}
          onChange={(e) => setText(e.target.value)}
          rows={2}
          maxLength={2000}
        />
      </Field>
      {fehler && <Fehlermeldung text={fehler} />}
      <Button type="submit" size="sm" variant="secondary" loading={pending} disabled={text.trim().length < 2}>
        <MessageSquarePlus aria-hidden="true" />
        Vermerken
      </Button>
    </form>
  );
}
