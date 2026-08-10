"use client";

import { useMemo, useState, useTransition } from "react";
import { useRouter } from "next/navigation";
import { MacKind } from "@prisma/client";
import { AlertCircle, Plus, X } from "lucide-react";

import { Button } from "@/components/ui/button";
import { Card, CardContent } from "@/components/ui/card";
import { Field, Input, Select, Textarea } from "@/components/ui/input";
import { geraetSpeichernAction } from "@/server/actions/inventar";
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
  hersteller: { id: string; name: string }[];
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
  model: string | null;
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
    model: geraet?.model ?? "",
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
              <Select
                value={form.manufacturerId}
                onChange={(e) => setzen("manufacturerId", e.target.value)}
              >
                <option value="">Nicht angegeben</option>
                {stammdaten.hersteller.map((h) => (
                  <option key={h.id} value={h.id}>
                    {h.name}
                  </option>
                ))}
              </Select>
            </Field>

            <Field label="Modell" htmlFor="g-modell">
              <Input
                value={form.model}
                onChange={(e) => setzen("model", e.target.value)}
                placeholder="ThinkPad L14"
                maxLength={120}
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
