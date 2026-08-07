"use client";

import { useRef, useState, useTransition } from "react";
import { TicketStatus } from "@prisma/client";
import { AlertCircle, Lock, MessageSquare, Send } from "lucide-react";

import { Button } from "@/components/ui/button";
import { Dateianhang, type AnhangEintrag } from "@/components/ui/dateianhang";
import { Textarea } from "@/components/ui/input";
import { antwortenAction } from "@/server/actions/tickets";
import { cn } from "@/lib/utils";

interface Textbaustein {
  id: string;
  title: string;
  shortcut: string | null;
  body: string;
}

/**
 * Antwortfeld.
 *
 * Zwei Modi, sichtbar getrennt statt über ein leicht zu übersehendes
 * Kontrollkästchen: „Antwort an den Melder" färbt das Feld normal, „Interne
 * Notiz" färbt es warnfarben und blendet einen Hinweis ein. Ein Versehen an
 * dieser Stelle schickt Internes nach außen, deshalb ist der Unterschied auf
 * den ersten Blick erkennbar.
 */
export function Composer({
  ticketId,
  darfIntern,
  darfStatusSetzen,
  textbausteine = [],
  melderAnsicht = false,
}: {
  ticketId: string;
  darfIntern: boolean;
  darfStatusSetzen: boolean;
  textbausteine?: Textbaustein[];
  /** Im Melder-Portal fehlen interne Notizen und Statuswahl. */
  melderAnsicht?: boolean;
}) {
  const [intern, setIntern] = useState(false);
  const [text, setText] = useState("");
  const [anhaenge, setAnhaenge] = useState<AnhangEintrag[]>([]);
  const [fehler, setFehler] = useState<string | null>(null);
  const [pending, startTransition] = useTransition();
  const feldRef = useRef<HTMLTextAreaElement>(null);

  function senden(neuerStatus?: TicketStatus) {
    // Ein Anhang allein ist eine gültige Antwort – wer ein Foto nachreicht,
    // muss nicht noch einen Satz dazu schreiben.
    if (!text.trim() && anhaenge.length === 0) {
      setFehler("Bitte schreiben Sie einen Text oder hängen Sie eine Datei an.");
      feldRef.current?.focus();
      return;
    }
    setFehler(null);

    startTransition(async () => {
      try {
        await antwortenAction({
          ticketId,
          body: text.trim() || "(Datei angehängt)",
          intern,
          neuerStatus,
          anhangIds: anhaenge.map((a) => a.id),
        });
        setText("");
        setAnhaenge([]);
        setIntern(false);
      } catch (e) {
        setFehler(
          e instanceof Error ? e.message : "Die Nachricht konnte nicht gespeichert werden.",
        );
      }
    });
  }

  function bausteinEinfuegen(baustein: Textbaustein) {
    setText((vorher) => (vorher ? `${vorher}\n\n${baustein.body}` : baustein.body));
    feldRef.current?.focus();
  }

  return (
    <div
      className={cn(
        "rounded-lg border transition-colors",
        intern ? "border-warning/50 bg-warning-soft" : "border-line bg-surface",
      )}
    >
      {!melderAnsicht && darfIntern && (
        <div
          role="radiogroup"
          aria-label="Art der Nachricht"
          className="flex gap-1 border-b border-line/60 p-1.5"
        >
          <ModusKnopf
            aktiv={!intern}
            onClick={() => setIntern(false)}
            icon={MessageSquare}
          >
            Antwort an den Melder
          </ModusKnopf>
          <ModusKnopf aktiv={intern} onClick={() => setIntern(true)} icon={Lock}>
            Interne Notiz
          </ModusKnopf>
        </div>
      )}

      {intern && (
        <p className="flex items-start gap-2 border-b border-warning/25 px-4 py-2 text-xs font-medium text-warning-soft-fg">
          <Lock className="mt-0.5 size-3.5 shrink-0" aria-hidden="true" />
          Diese Notiz sieht nur Ihr Bereich. Sie wird nicht per E-Mail versendet
          und erscheint dem Melder nicht im Portal.
        </p>
      )}

      <div className="p-3">
        <label htmlFor="antwort-feld" className="sr-only">
          {intern ? "Interne Notiz" : "Antwort an den Melder"}
        </label>
        <Textarea
          id="antwort-feld"
          ref={feldRef}
          value={text}
          onChange={(e) => {
            setText(e.target.value);
            if (fehler) setFehler(null);
          }}
          rows={melderAnsicht ? 5 : 4}
          placeholder={
            intern
              ? "Was sollen die Kolleginnen und Kollegen wissen?"
              : melderAnsicht
                ? "Ihre Rückmeldung …"
                : "Ihre Antwort an den Melder …"
          }
          className={cn(
            "resize-y border-0 bg-transparent p-0 shadow-none focus-visible:ring-0",
            intern && "placeholder:text-warning-soft-fg/60",
          )}
          aria-invalid={fehler ? true : undefined}
          aria-describedby={fehler ? "antwort-fehler" : undefined}
        />

        {fehler && (
          <p
            id="antwort-fehler"
            role="alert"
            className="mt-2 flex items-center gap-1.5 text-xs font-medium text-danger"
          >
            <AlertCircle className="size-3.5" aria-hidden="true" />
            {fehler}
          </p>
        )}

        <div className="mt-3">
          <Dateianhang
            ticketId={ticketId}
            anhaenge={anhaenge}
            onChange={setAnhaenge}
            label="Datei anhängen"
          />
        </div>
      </div>

      {textbausteine.length > 0 && !intern && (
        <div className="flex flex-wrap gap-1.5 border-t border-line/60 px-3 py-2">
          <span className="self-center text-xs text-fg-subtle">Bausteine:</span>
          {textbausteine.map((b) => (
            <button
              key={b.id}
              type="button"
              onClick={() => bausteinEinfuegen(b)}
              className="rounded-md border border-line bg-surface-2 px-2 py-1 text-xs text-fg-muted transition-colors hover:bg-surface-3 hover:text-fg"
            >
              {b.title}
            </button>
          ))}
        </div>
      )}

      <div className="flex flex-wrap items-center justify-end gap-2 border-t border-line/60 px-3 py-2.5">
        {darfStatusSetzen && !intern && (
          <Button
            variant="secondary"
            size="sm"
            loading={pending}
            onClick={() => senden(TicketStatus.WARTET_AUF_MELDER)}
          >
            Senden und auf Antwort warten
          </Button>
        )}

        {darfStatusSetzen && (
          <Button
            variant="secondary"
            size="sm"
            loading={pending}
            onClick={() => senden(TicketStatus.GELOEST)}
          >
            Senden und erledigen
          </Button>
        )}

        <Button size="sm" loading={pending} onClick={() => senden()}>
          <Send aria-hidden="true" />
          {intern ? "Notiz speichern" : "Senden"}
        </Button>
      </div>
    </div>
  );
}

function ModusKnopf({
  aktiv,
  onClick,
  icon: Icon,
  children,
}: {
  aktiv: boolean;
  onClick: () => void;
  icon: React.ComponentType<{ className?: string; "aria-hidden"?: boolean }>;
  children: React.ReactNode;
}) {
  return (
    <button
      type="button"
      role="radio"
      aria-checked={aktiv}
      onClick={onClick}
      className={cn(
        "inline-flex items-center gap-1.5 rounded-md px-3 py-1.5 text-sm font-medium transition-colors",
        aktiv
          ? "bg-surface-3 text-fg shadow-card"
          : "text-fg-muted hover:text-fg",
      )}
    >
      <Icon className="size-3.5" aria-hidden={true} />
      {children}
    </button>
  );
}
