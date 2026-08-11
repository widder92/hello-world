"use client";

import { useMemo, useRef, useState, useTransition } from "react";
import { useRouter } from "next/navigation";
import { MacKind } from "@prisma/client";
import { AlertCircle, Camera, Check, Image as ImageIcon, Plus, X } from "lucide-react";

import { Button } from "@/components/ui/button";
import { Card, CardContent } from "@/components/ui/card";
import { Field, Input, Select, Textarea } from "@/components/ui/input";
import {
  fotoVorbereitenAction,
  geraetSpeichernAction,
  herstellerAnlegenAction,
  modellAnlegenAction,
} from "@/server/actions/inventar";
import { MAC_KIND } from "@/lib/domain";

/**
 * Formular zum Anlegen und Ändern eines Geräts.
 *
 * Die Reihenfolge folgt dem Karton: Erst was draufsteht (Art, Hersteller,
 * Modell, Seriennummer), dann wohin es gehört (Bereich, Standort,
 * Kostenstelle), dann der Beleg (Datum, Preis, Lieferant). Wer ein neues
 * Notebook auspackt, arbeitet die Felder von oben nach unten ab.
 *
 * Die Inventarnummer wird nicht abgefragt – sie entsteht beim Speichern aus
 * der Geräteart. Eine selbst vergebene Nummer wäre die erste Fehlerquelle.
 */

export interface Stammdaten {
  arten: {
    id: string;
    key: string;
    name: string;
    requiresSerial: boolean;
    requiresMac: boolean;
    defaultCostCenterId: string | null;
  }[];
  hersteller: {
    id: string;
    name: string;
    models: {
      id: string;
      name: string;
      kindId: string | null;
      photo: { id: string } | null;
    }[];
  }[];
  kostenstellen: { id: string; code: string; name: string }[];
  standorte: { id: string; name: string }[];
  bereiche: { id: string; name: string }[];
}

export interface GeraetWerte {
  id: string;
  name: string;
  kindId: string;
  teamId: string;
  manufacturerId: string | null;
  modelId: string | null;
  serial: string | null;
  imei: string | null;
  siteId: string | null;
  room: string | null;
  storageLocation: string | null;
  costCenterId: string | null;
  purchasedAt: Date | null;
  warrantyUntil: Date | null;
  purchasePriceCents: number | null;
  supplier: string | null;
  invoiceNumber: string | null;
  notes: string | null;
  macs: { address: string; kind: MacKind }[];
}

/** Date → "JJJJ-MM-TT" für das Datumsfeld. */
function alsFeldwert(d: Date | null): string {
  return d ? d.toISOString().slice(0, 10) : "";
}

/** Cent → "1299,00" für das Eingabefeld. */
function alsBetrag(cent: number | null): string {
  return cent === null ? "" : (cent / 100).toFixed(2).replace(".", ",");
}

export function GeraetFormular({
  stammdaten,
  geraet,
}: {
  stammdaten: Stammdaten;
  geraet?: GeraetWerte;
}) {
  const router = useRouter();
  const [pending, startTransition] = useTransition();
  const [fehler, setFehler] = useState<string | null>(null);

  const neu = !geraet;

  const [form, setForm] = useState({
    name: geraet?.name ?? "",
    kindId: geraet?.kindId ?? (stammdaten.arten[0]?.id ?? ""),
    teamId: geraet?.teamId ?? (stammdaten.bereiche[0]?.id ?? ""),
    manufacturerId: geraet?.manufacturerId ?? "",
    modelId: geraet?.modelId ?? "",
    serial: geraet?.serial ?? "",
    imei: geraet?.imei ?? "",
    siteId: geraet?.siteId ?? "",
    room: geraet?.room ?? "",
    storageLocation: geraet?.storageLocation ?? "",
    costCenterId: geraet?.costCenterId ?? "",
    purchasedAt: alsFeldwert(geraet?.purchasedAt ?? null),
    warrantyUntil: alsFeldwert(geraet?.warrantyUntil ?? null),
    preis: alsBetrag(geraet?.purchasePriceCents ?? null),
    supplier: geraet?.supplier ?? "",
    invoiceNumber: geraet?.invoiceNumber ?? "",
    notes: geraet?.notes ?? "",
  });

  const [macs, setMacs] = useState<{ address: string; kind: MacKind }[]>(
    geraet?.macs.length ? geraet.macs : [],
  );

  // Hersteller und Modelle wachsen während des Ausfüllens, wenn jemand neue
  // anlegt – deshalb im Zustand und nicht direkt aus den Eigenschaften.
  const [hersteller, setHersteller] = useState(stammdaten.hersteller);

  // Vor dem Speichern hochgeladenes Bild.
  const [fotoId, setFotoId] = useState<string | null>(null);
  const [fotoFehler, setFotoFehler] = useState<string | null>(null);
  const [fotoLaeuft, setFotoLaeuft] = useState(false);
  const fotoEingabe = useRef<HTMLInputElement>(null);

  const modelle = useMemo(
    () => hersteller.find((h) => h.id === form.manufacturerId)?.models ?? [],
    [hersteller, form.manufacturerId],
  );

  const gewaehltesModell = useMemo(
    () => modelle.find((m) => m.id === form.modelId) ?? null,
    [modelle, form.modelId],
  );

  /**
   * Welches Bild angezeigt wird und wohin ein neues gehört.
   *
   * Hat das Modell schon eines, ist die Sache erledigt – genau dafür hängt das
   * Bild am Modell. Erst wenn keines da ist, wird eines verlangt, und es gilt
   * dann für alle weiteren Geräte desselben Modells.
   */
  const modellFoto = gewaehltesModell?.photo?.id ?? null;

  function fotoHochladen(datei: File) {
    setFotoFehler(null);
    setFotoLaeuft(true);
    const daten = new FormData();
    daten.set("datei", datei);
    void fotoVorbereitenAction(daten)
      .then((e) => setFotoId(e.fotoId))
      .catch((e) =>
        setFotoFehler(
          e instanceof Error ? e.message : "Das Bild konnte nicht gespeichert werden.",
        ),
      )
      .finally(() => setFotoLaeuft(false));
  }

  const art = useMemo(
    () => stammdaten.arten.find((a) => a.id === form.kindId),
    [stammdaten.arten, form.kindId],
  );

  function setzen<K extends keyof typeof form>(feld: K, wert: (typeof form)[K]) {
    setForm((f) => ({ ...f, [feld]: wert }));
  }

  /** Beim Wechsel der Geräteart die hinterlegte Kostenstelle vorschlagen. */
  function artWechseln(kindId: string) {
    const gewaehlt = stammdaten.arten.find((a) => a.id === kindId);
    setForm((f) => ({
      ...f,
      kindId,
      costCenterId:
        // Nur vorbelegen, wenn noch nichts gewählt wurde – eine bewusste
        // Auswahl darf ein Artwechsel nicht überschreiben.
        f.costCenterId || gewaehlt?.defaultCostCenterId || "",
    }));
  }

  function absenden(e: React.FormEvent) {
    e.preventDefault();
    setFehler(null);
    startTransition(async () => {
      try {
        const ergebnis = await geraetSpeichernAction({
          ...form,
          macs: macs.filter((m) => m.address.trim()),
          fotoId: fotoId ?? undefined,
          // Ohne Modell kann das Bild nur am Gerät hängen.
          fotoZiel: form.modelId ? "modell" : "geraet",
          id: geraet?.id,
        });
        router.push(`/inventar/${ergebnis.id}`);
        router.refresh();
      } catch (e) {
        setFehler(
          e instanceof Error
            ? e.message
            : "Das Gerät konnte nicht gespeichert werden.",
        );
      }
    });
  }

  return (
    <form onSubmit={absenden} className="space-y-5">
      {/* --- Was für ein Gerät ---------------------------------------- */}
      <Card>
        <CardContent className="space-y-4 py-5">
          <h2 className="text-sm font-semibold text-fg">Um welches Gerät geht es?</h2>

          <div className="grid gap-4 sm:grid-cols-2">
            <Field label="Geräteart" htmlFor="g-art" required>
              <Select
                value={form.kindId}
                onChange={(e) => artWechseln(e.target.value)}
              >
                {stammdaten.arten.map((a) => (
                  <option key={a.id} value={a.id}>
                    {a.name}
                  </option>
                ))}
              </Select>
            </Field>

            <Field
              label="Bezeichnung"
              htmlFor="g-name"
              required
              help="Wonach würden Sie das Gerät suchen? Zum Beispiel: Notebook Wohnbereichsleitung Nord"
            >
              <Input
                value={form.name}
                onChange={(e) => setzen("name", e.target.value)}
                maxLength={120}
              />
            </Field>

            <Field label="Hersteller" htmlFor="g-hersteller">
              <AuswahlMitAnlegen
                id="g-hersteller"
                wert={form.manufacturerId}
                eintraege={hersteller}
                leerText="Nicht angegeben"
                anlegenText="Neuer Hersteller"
                platzhalter="Framework"
                onWaehlen={(id) =>
                  setForm((f) => ({
                    ...f,
                    manufacturerId: id,
                    // Das bisherige Modell gehört zum alten Hersteller.
                    modelId: "",
                  }))
                }
                onAnlegen={async (name) => {
                  const neu = await herstellerAnlegenAction(name);
                  setHersteller((liste) =>
                    liste.some((h) => h.id === neu.id)
                      ? liste
                      : [...liste, { id: neu.id, name: neu.name, models: [] }].sort(
                          (a, b) => a.name.localeCompare(b.name, "de"),
                        ),
                  );
                  return neu.id;
                }}
              />
            </Field>

            <Field
              label="Modell"
              htmlFor="g-modell"
              help={
                form.manufacturerId
                  ? "Dasselbe Modell wird oft mehrfach beschafft – deshalb ausgewählt statt getippt."
                  : "Bitte zuerst den Hersteller wählen."
              }
            >
              <AuswahlMitAnlegen
                id="g-modell"
                wert={form.modelId}
                eintraege={modelle}
                leerText="Nicht angegeben"
                anlegenText="Neues Modell"
                platzhalter="ThinkPad L14"
                gesperrt={!form.manufacturerId}
                onWaehlen={(id) => {
                  const m = modelle.find((x) => x.id === id);
                  setForm((f) => ({
                    ...f,
                    modelId: id,
                    // Die übliche Geräteart des Modells vorbelegen, solange
                    // niemand bewusst etwas anderes gewählt hat.
                    kindId: m?.kindId ?? f.kindId,
                  }));
                }}
                onAnlegen={async (name) => {
                  const neu = await modellAnlegenAction({
                    manufacturerId: form.manufacturerId,
                    name,
                    kindId: form.kindId,
                    fotoId: fotoId ?? undefined,
                  });
                  setHersteller((liste) =>
                    liste.map((h) =>
                      h.id === form.manufacturerId
                        ? {
                            ...h,
                            models: h.models.some((m) => m.id === neu.id)
                              ? h.models
                              : [
                                  ...h.models,
                                  {
                                    id: neu.id,
                                    name: neu.name,
                                    kindId: form.kindId,
                                    photo: fotoId ? { id: fotoId } : null,
                                  },
                                ].sort((a, b) => a.name.localeCompare(b.name, "de")),
                          }
                        : h,
                    ),
                  );
                  return neu.id;
                }}
              />
            </Field>

            <Field
              label="Seriennummer"
              htmlFor="g-serial"
              required={art?.requiresSerial}
              help="Steht meist auf einem Aufkleber an der Unterseite oder Rückseite."
            >
              <Input
                value={form.serial}
                onChange={(e) => setzen("serial", e.target.value)}
                className="font-mono"
                maxLength={120}
              />
            </Field>

            <Field
              label="IMEI"
              htmlFor="g-imei"
              help="Nur bei Mobilfunkgeräten. Abfragbar über *#06#."
            >
              <Input
                value={form.imei}
                onChange={(e) => setzen("imei", e.target.value)}
                className="font-mono"
                maxLength={40}
              />
            </Field>
          </div>

          {/* --- MAC-Adressen ------------------------------------------ */}
          {(art?.requiresMac || macs.length > 0) && (
            <fieldset className="space-y-2 border-t border-line pt-4">
              <legend className="text-sm font-medium text-fg">
                Netzwerkadressen
              </legend>
              <p className="text-xs text-fg-muted">
                Schreibweise egal – 3C-52-82-11-0A-01 und 3c52.8211.0a01 werden
                beide erkannt und einheitlich abgelegt.
              </p>

              {macs.map((mac, i) => (
                <div key={i} className="flex flex-wrap items-end gap-2">
                  <div className="min-w-48 flex-1">
                    <label htmlFor={`mac-${i}`} className="sr-only">
                      MAC-Adresse {i + 1}
                    </label>
                    <Input
                      id={`mac-${i}`}
                      value={mac.address}
                      onChange={(e) =>
                        setMacs((liste) =>
                          liste.map((m, j) =>
                            j === i ? { ...m, address: e.target.value } : m,
                          ),
                        )
                      }
                      placeholder="3c:52:82:11:0a:01"
                      className="font-mono"
                    />
                  </div>
                  <div className="w-40">
                    <label htmlFor={`mac-art-${i}`} className="sr-only">
                      Art der Adresse {i + 1}
                    </label>
                    <Select
                      id={`mac-art-${i}`}
                      value={mac.kind}
                      onChange={(e) =>
                        setMacs((liste) =>
                          liste.map((m, j) =>
                            j === i
                              ? { ...m, kind: e.target.value as MacKind }
                              : m,
                          ),
                        )
                      }
                    >
                      {Object.entries(MAC_KIND).map(([wert, label]) => (
                        <option key={wert} value={wert}>
                          {label}
                        </option>
                      ))}
                    </Select>
                  </div>
                  <Button
                    type="button"
                    variant="ghost"
                    size="sm"
                    onClick={() =>
                      setMacs((liste) => liste.filter((_, j) => j !== i))
                    }
                    aria-label={`Adresse ${i + 1} entfernen`}
                  >
                    <X aria-hidden="true" />
                  </Button>
                </div>
              ))}

              {macs.length < 8 && (
                <Button
                  type="button"
                  variant="secondary"
                  size="sm"
                  onClick={() =>
                    setMacs((liste) => [
                      ...liste,
                      { address: "", kind: MacKind.LAN },
                    ])
                  }
                >
                  <Plus aria-hidden="true" />
                  Adresse hinzufügen
                </Button>
              )}
            </fieldset>
          )}

          {/* --- Foto --------------------------------------------------- */}
          <div className="space-y-2 border-t border-line pt-4">
            <h3 className="text-sm font-medium text-fg">Foto</h3>

            {modellFoto && !fotoId ? (
              // Der Regelfall ab dem zweiten Gerät eines Modells: Das Bild ist
              // schon da und wird übernommen.
              <div className="flex flex-wrap items-center gap-3 rounded-md border border-line bg-surface-2 p-3">
                {/* eslint-disable-next-line @next/next/no-img-element */}
                <img
                  src={`/api/uploads/${modellFoto}`}
                  alt=""
                  className="size-16 shrink-0 rounded-md object-cover"
                />
                <div className="min-w-0 flex-1">
                  <p className="flex items-center gap-1.5 text-sm font-medium text-success">
                    <Check className="size-4" aria-hidden="true" />
                    Bild vom Modell wird übernommen
                  </p>
                  <p className="mt-0.5 text-xs text-fg-muted">
                    {gewaehltesModell?.name} – hinterlegt am Modell, gilt für
                    alle Geräte dieser Bauart. Sie müssen nichts tun.
                  </p>
                </div>
              </div>
            ) : (
              <>
                {fotoId ? (
                  <div className="flex flex-wrap items-center gap-3 rounded-md border border-line bg-surface-2 p-3">
                    {/* eslint-disable-next-line @next/next/no-img-element */}
                    <img
                      src={`/api/uploads/${fotoId}`}
                      alt=""
                      className="size-16 shrink-0 rounded-md object-cover"
                    />
                    <div className="min-w-0 flex-1">
                      <p className="text-sm text-fg">Bild ausgewählt.</p>
                      <p className="mt-0.5 text-xs text-fg-muted">
                        {form.modelId
                          ? "Wird beim Speichern am Modell hinterlegt – jedes weitere Gerät dieser Bauart übernimmt es dann."
                          : "Wird beim Speichern an diesem Gerät hinterlegt."}
                      </p>
                    </div>
                    <Button
                      type="button"
                      variant="ghost"
                      size="sm"
                      onClick={() => setFotoId(null)}
                    >
                      <X aria-hidden="true" />
                      Entfernen
                    </Button>
                  </div>
                ) : (
                  <p className="text-xs text-fg-muted">
                    {form.modelId
                      ? "Für dieses Modell ist noch kein Bild hinterlegt. Einmal aufnehmen genügt – alle weiteren Geräte derselben Bauart übernehmen es."
                      : "Ohne Modell gehört das Bild nur zu diesem einen Gerät."}
                  </p>
                )}

                <input
                  ref={fotoEingabe}
                  type="file"
                  accept="image/*"
                  capture="environment"
                  className="sr-only"
                  aria-label="Foto auswählen"
                  tabIndex={-1}
                  onChange={(e) => {
                    const datei = e.target.files?.[0];
                    if (datei) fotoHochladen(datei);
                    e.target.value = "";
                  }}
                />

                <Button
                  type="button"
                  variant="secondary"
                  size="sm"
                  loading={fotoLaeuft}
                  onClick={() => fotoEingabe.current?.click()}
                >
                  {fotoId ? (
                    <ImageIcon aria-hidden="true" />
                  ) : (
                    <Camera aria-hidden="true" />
                  )}
                  {fotoId ? "Anderes Bild wählen" : "Foto aufnehmen oder wählen"}
                </Button>
              </>
            )}

            {fotoFehler && (
              <p
                role="alert"
                className="flex items-start gap-2 rounded-md border border-danger/30 bg-danger-soft px-3 py-2 text-sm text-danger-soft-fg"
              >
                <AlertCircle className="mt-0.5 size-4 shrink-0" aria-hidden="true" />
                {fotoFehler}
              </p>
            )}
          </div>
        </CardContent>
      </Card>

      {/* --- Wohin es gehört ------------------------------------------ */}
      <Card>
        <CardContent className="space-y-4 py-5">
          <h2 className="text-sm font-semibold text-fg">Zuordnung</h2>

          <div className="grid gap-4 sm:grid-cols-2">
            {stammdaten.bereiche.length > 1 && (
              <Field label="Bereich" htmlFor="g-bereich" required>
                <Select
                  value={form.teamId}
                  onChange={(e) => setzen("teamId", e.target.value)}
                >
                  {stammdaten.bereiche.map((b) => (
                    <option key={b.id} value={b.id}>
                      {b.name}
                    </option>
                  ))}
                </Select>
              </Field>
            )}

            <Field
              label="Kostenstelle"
              htmlFor="g-kst"
              help="Grundlage der Auswertung. Bei Unsicherheit lieber leer lassen als raten."
            >
              <Select
                value={form.costCenterId}
                onChange={(e) => setzen("costCenterId", e.target.value)}
              >
                <option value="">Keine</option>
                {stammdaten.kostenstellen.map((k) => (
                  <option key={k.id} value={k.id}>
                    {k.code} – {k.name}
                  </option>
                ))}
              </Select>
            </Field>

            <Field label="Standort" htmlFor="g-standort">
              <Select
                value={form.siteId}
                onChange={(e) => setzen("siteId", e.target.value)}
              >
                <option value="">Nicht festgelegt</option>
                {stammdaten.standorte.map((s) => (
                  <option key={s.id} value={s.id}>
                    {s.name}
                  </option>
                ))}
              </Select>
            </Field>

            <Field label="Raum" htmlFor="g-raum">
              <Input
                value={form.room}
                onChange={(e) => setzen("room", e.target.value)}
                placeholder="Dienstzimmer"
                maxLength={120}
              />
            </Field>

            <Field
              label="Lagerort"
              htmlFor="g-lager"
              help="Wo das Gerät liegt, solange es niemand hat."
              className="sm:col-span-2"
            >
              <Input
                value={form.storageLocation}
                onChange={(e) => setzen("storageLocation", e.target.value)}
                placeholder="IT-Lager, Schrank 2, Fach C"
                maxLength={120}
              />
            </Field>
          </div>
        </CardContent>
      </Card>

      {/* --- Beleg ----------------------------------------------------- */}
      <Card>
        <CardContent className="space-y-4 py-5">
          <h2 className="text-sm font-semibold text-fg">Anschaffung</h2>

          <div className="grid gap-4 sm:grid-cols-2">
            <Field label="Anschaffungsdatum" htmlFor="g-kaufdatum">
              <Input
                type="date"
                value={form.purchasedAt}
                onChange={(e) => setzen("purchasedAt", e.target.value)}
              />
            </Field>

            <Field label="Garantie bis" htmlFor="g-garantie">
              <Input
                type="date"
                value={form.warrantyUntil}
                onChange={(e) => setzen("warrantyUntil", e.target.value)}
              />
            </Field>

            <Field
              label="Anschaffungspreis"
              htmlFor="g-preis"
              help="Brutto in Euro, z. B. 1.299,00"
            >
              <Input
                value={form.preis}
                onChange={(e) => setzen("preis", e.target.value)}
                inputMode="decimal"
                placeholder="1.299,00"
                maxLength={20}
              />
            </Field>

            <Field label="Lieferant" htmlFor="g-lieferant">
              <Input
                value={form.supplier}
                onChange={(e) => setzen("supplier", e.target.value)}
                maxLength={120}
              />
            </Field>

            <Field
              label="Rechnungsnummer"
              htmlFor="g-rechnung"
              className="sm:col-span-2"
            >
              <Input
                value={form.invoiceNumber}
                onChange={(e) => setzen("invoiceNumber", e.target.value)}
                maxLength={60}
              />
            </Field>
          </div>

          <Field label="Notizen" htmlFor="g-notizen">
            <Textarea
              value={form.notes}
              onChange={(e) => setzen("notes", e.target.value)}
              rows={3}
              maxLength={4000}
              placeholder="Besonderheiten, Zubehör, Auffälligkeiten"
            />
          </Field>
        </CardContent>
      </Card>

      {fehler && (
        <p
          role="alert"
          className="flex items-start gap-2 rounded-md border border-danger/30 bg-danger-soft px-3 py-2.5 text-sm text-danger-soft-fg"
        >
          <AlertCircle className="mt-0.5 size-4 shrink-0" aria-hidden="true" />
          {fehler}
        </p>
      )}

      <div className="flex flex-wrap items-center gap-2">
        <Button type="submit" loading={pending}>
          {neu ? "Gerät anlegen" : "Änderungen speichern"}
        </Button>
        <Button
          type="button"
          variant="ghost"
          onClick={() => router.back()}
          disabled={pending}
        >
          Abbrechen
        </Button>
        {neu && (
          <p className="text-xs text-fg-subtle">
            Die Inventarnummer vergibt das System beim Speichern. Das Foto lässt
            sich danach auf der Geräteseite hinterlegen.
          </p>
        )}
      </div>
    </form>
  );
}

/**
 * Auswahlfeld, in dem sich ein fehlender Eintrag sofort anlegen lässt.
 *
 * Der Grund ist banal und trotzdem entscheidend: Ein neu geliefertes Gerät
 * lässt sich nicht erfassen, wenn sein Hersteller noch nicht in der Liste
 * steht. Wer dafür die Seite verlassen, in die Stammdaten wechseln und von
 * vorn anfangen muss, tippt beim dritten Mal irgendetwas in ein anderes Feld –
 * und genau so entstehen die Datenbestände, die niemand mehr auswerten kann.
 *
 * Bewusst ein einfaches `select` mit einem zusätzlichen Eintrag statt eines
 * selbstgebauten Auswahlfelds: Das native Element funktioniert mit Tastatur,
 * Vorleseprogramm und auf dem Handy so, wie die Person es kennt.
 */
function AuswahlMitAnlegen({
  id,
  wert,
  eintraege,
  leerText,
  anlegenText,
  platzhalter,
  gesperrt,
  onWaehlen,
  onAnlegen,
}: {
  id: string;
  wert: string;
  eintraege: { id: string; name: string }[];
  leerText: string;
  anlegenText: string;
  platzhalter: string;
  gesperrt?: boolean;
  onWaehlen: (id: string) => void;
  onAnlegen: (name: string) => Promise<string>;
}) {
  const [anlegen, setAnlegen] = useState(false);
  const [name, setName] = useState("");
  const [laeuft, setLaeuft] = useState(false);
  const [fehler, setFehler] = useState<string | null>(null);

  async function speichern() {
    const bezeichnung = name.trim();
    if (!bezeichnung) return;
    setFehler(null);
    setLaeuft(true);
    try {
      const neueId = await onAnlegen(bezeichnung);
      onWaehlen(neueId);
      setAnlegen(false);
      setName("");
    } catch (e) {
      setFehler(
        e instanceof Error ? e.message : "Der Eintrag konnte nicht angelegt werden.",
      );
    } finally {
      setLaeuft(false);
    }
  }

  if (anlegen) {
    return (
      <div className="space-y-2">
        <div className="flex flex-wrap items-center gap-2">
          <Input
            id={id}
            value={name}
            onChange={(e) => setName(e.target.value)}
            placeholder={platzhalter}
            autoFocus
            onKeyDown={(e) => {
              // Enter legt an, statt das ganze Formular abzuschicken.
              if (e.key === "Enter") {
                e.preventDefault();
                void speichern();
              }
              if (e.key === "Escape") setAnlegen(false);
            }}
            className="min-w-40 flex-1"
          />
          <Button type="button" size="sm" loading={laeuft} onClick={speichern}>
            <Check aria-hidden="true" />
            Anlegen
          </Button>
          <Button
            type="button"
            variant="ghost"
            size="sm"
            disabled={laeuft}
            onClick={() => {
              setAnlegen(false);
              setFehler(null);
            }}
          >
            Abbrechen
          </Button>
        </div>
        {fehler && (
          <p role="alert" className="text-sm text-danger">
            {fehler}
          </p>
        )}
      </div>
    );
  }

  return (
    <Select
      id={id}
      value={wert}
      disabled={gesperrt}
      onChange={(e) => {
        if (e.target.value === "__neu__") {
          setAnlegen(true);
          return;
        }
        onWaehlen(e.target.value);
      }}
    >
      <option value="">{leerText}</option>
      {eintraege.map((e) => (
        <option key={e.id} value={e.id}>
          {e.name}
        </option>
      ))}
      {!gesperrt && <option value="__neu__">+ {anlegenText} …</option>}
    </Select>
  );
}
