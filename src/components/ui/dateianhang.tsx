"use client";

import { useRef, useState, useTransition } from "react";
import { AlertCircle, FileText, ImageIcon, Paperclip, X } from "lucide-react";

import { Button } from "@/components/ui/button";
import { hochladenAction, anhangEntfernenAction } from "@/server/actions/uploads";
import { formatBytes } from "@/lib/format";
import { cn } from "@/lib/utils";

export interface AnhangEintrag {
  id: string;
  filename: string;
  mimeType: string;
  size: number;
}

/**
 * Dateien anhängen.
 *
 * Der wichtigste Fall im Wohnbereich ist das Foto vom Schaden, mit dem Handy
 * aufgenommen. Deshalb: `capture` erlaubt am Mobilgerät den direkten Zugriff
 * auf die Kamera, Bilder bekommen eine Vorschau, und der Ablagebereich ist
 * großzügig genug, um ihn auch mit Handschuhen zu treffen.
 *
 * Hochgeladen wird sofort beim Auswählen, nicht erst beim Absenden. Wer ein
 * großes Foto anhängt, sieht dadurch früh, ob es durchgeht – und nicht erst,
 * wenn der ganze Text schon geschrieben ist.
 */
export function Dateianhang({
  ticketId,
  anhaenge,
  onChange,
  gross = false,
  label = "Dateien anhängen",
}: {
  /** Bei bestehenden Vorgängen: sofort zuordnen. */
  ticketId?: string;
  anhaenge: AnhangEintrag[];
  onChange: (anhaenge: AnhangEintrag[]) => void;
  gross?: boolean;
  label?: string;
}) {
  const eingabeRef = useRef<HTMLInputElement>(null);
  const [pending, startTransition] = useTransition();
  const [fehler, setFehler] = useState<string[]>([]);
  const [ueberZiehen, setUeberZiehen] = useState(false);

  function verarbeite(dateien: FileList | null) {
    if (!dateien || dateien.length === 0) return;
    setFehler([]);

    startTransition(async () => {
      const neue: AnhangEintrag[] = [];
      const probleme: string[] = [];

      for (const datei of Array.from(dateien)) {
        const formData = new FormData();
        formData.append("datei", datei);
        if (ticketId) formData.append("ticketId", ticketId);

        const ergebnis = await hochladenAction(formData);

        if (ergebnis.ok && ergebnis.anhang) {
          neue.push(ergebnis.anhang);
        } else {
          probleme.push(`${datei.name}: ${ergebnis.fehler}`);
        }
      }

      if (neue.length) onChange([...anhaenge, ...neue]);
      if (probleme.length) setFehler(probleme);
    });
  }

  function entfernen(id: string) {
    startTransition(async () => {
      await anhangEntfernenAction(id);
      onChange(anhaenge.filter((a) => a.id !== id));
    });
  }

  return (
    <div className="space-y-2">
      <div
        onDragOver={(e) => {
          e.preventDefault();
          setUeberZiehen(true);
        }}
        onDragLeave={() => setUeberZiehen(false)}
        onDrop={(e) => {
          e.preventDefault();
          setUeberZiehen(false);
          verarbeite(e.dataTransfer.files);
        }}
        className={cn(
          "rounded-lg border border-dashed transition-colors",
          ueberZiehen
            ? "border-primary bg-primary-soft"
            : "border-line bg-surface-2",
          gross ? "p-5" : "p-3.5",
        )}
      >
        <input
          ref={eingabeRef}
          type="file"
          multiple
          // Am Handy bietet der Browser damit direkt die Kamera an.
          accept="image/*,application/pdf,.doc,.docx,.xls,.xlsx,.txt,.csv,.zip,.msg,.eml"
          className="sr-only"
          /*
           * Das Feld ist nur ausgeblendet, nicht entfernt – ein Vorleseprogramm
           * findet es also. Ohne Beschriftung meldete es sich dort als
           * namenloses Eingabefeld. Aus der Tabulatorreihenfolge bleibt es
           * heraus, weil die sichtbare Schaltfläche daneben dasselbe tut und
           * einen erkennbaren Fokusrahmen hat; zwei Haltepunkte für dieselbe
           * Handlung wären nur verwirrend.
           */
          aria-label={label}
          tabIndex={-1}
          onChange={(e) => {
            verarbeite(e.target.files);
            // Zurücksetzen, damit dieselbe Datei erneut gewählt werden kann.
            e.target.value = "";
          }}
        />

        <div className="flex flex-wrap items-center justify-center gap-2 text-center">
          <Paperclip
            className={cn("text-fg-subtle", gross ? "size-5" : "size-4")}
            aria-hidden="true"
          />
          <span className={cn("text-fg-muted", gross ? "text-base" : "text-sm")}>
            Datei hierher ziehen oder
          </span>
          <Button
            type="button"
            variant="secondary"
            size={gross ? "md" : "sm"}
            loading={pending}
            onClick={() => eingabeRef.current?.click()}
          >
            {label}
          </Button>
        </div>

        <p
          className={cn(
            "mt-1.5 text-center text-fg-subtle",
            gross ? "text-sm" : "text-xs",
          )}
        >
          Fotos, PDF und Office-Dokumente. Am Handy können Sie direkt
          fotografieren.
        </p>
      </div>

      {fehler.length > 0 && (
        <ul role="alert" className="space-y-1">
          {fehler.map((f, i) => (
            <li
              key={i}
              className="flex items-start gap-1.5 rounded-md border border-danger/30 bg-danger-soft px-2.5 py-1.5 text-xs text-danger-soft-fg"
            >
              <AlertCircle className="mt-0.5 size-3.5 shrink-0" aria-hidden="true" />
              {f}
            </li>
          ))}
        </ul>
      )}

      {anhaenge.length > 0 && (
        <ul className="flex flex-wrap gap-2">
          {anhaenge.map((a) => {
            const istBild = a.mimeType.startsWith("image/");
            const Icon = istBild ? ImageIcon : FileText;

            return (
              <li
                key={a.id}
                className="flex items-center gap-2 rounded-md border border-line bg-surface px-2.5 py-1.5"
              >
                <Icon className="size-4 shrink-0 text-fg-subtle" aria-hidden="true" />
                <span className="min-w-0">
                  <span className="block max-w-48 truncate text-xs font-medium text-fg">
                    {a.filename}
                  </span>
                  <span className="block text-[0.6875rem] text-fg-subtle">
                    {formatBytes(a.size)}
                  </span>
                </span>
                <button
                  type="button"
                  disabled={pending}
                  onClick={() => entfernen(a.id)}
                  className="shrink-0 rounded p-1 text-fg-subtle transition-colors hover:bg-surface-2 hover:text-danger disabled:opacity-50"
                >
                  <X className="size-3.5" aria-hidden="true" />
                  <span className="sr-only">{a.filename} entfernen</span>
                </button>
              </li>
            );
          })}
        </ul>
      )}
    </div>
  );
}
