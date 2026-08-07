"use client";

import { useState, useTransition } from "react";
import { useRouter } from "next/navigation";
import { AlertTriangle, CheckCircle2, PlugZap, RefreshCw } from "lucide-react";

import { Button } from "@/components/ui/button";
import {
  jetztAbrufenAction,
  verbindungTestenAction,
} from "@/server/actions/verwaltung";
import { cn } from "@/lib/utils";

/**
 * Verbindung prüfen und sofort abrufen.
 *
 * Beides gehört zur Inbetriebnahme: Ein falsches Kennwort fällt sonst erst
 * dadurch auf, dass tagelang keine Vorgänge ankommen – und dann sucht niemand
 * mehr beim Postfach.
 */
export function MailkontoAktionen({ mailAccountId }: { mailAccountId: string }) {
  const router = useRouter();
  const [pending, startTransition] = useTransition();
  const [meldung, setMeldung] = useState<{
    ok: boolean;
    text: string;
  } | null>(null);

  function testen() {
    setMeldung(null);
    startTransition(async () => {
      const ergebnis = await verbindungTestenAction(mailAccountId);
      setMeldung({ ok: ergebnis.ok, text: ergebnis.meldung });
      router.refresh();
    });
  }

  function abrufen() {
    setMeldung(null);
    startTransition(async () => {
      const e = await jetztAbrufenAction(mailAccountId);
      setMeldung(
        e.fehler
          ? { ok: false, text: e.fehler }
          : {
              ok: true,
              text:
                e.geholt === 0
                  ? "Keine neuen Nachrichten."
                  : `${e.geholt} geholt: ${e.neueVorgaenge} neue Vorgänge, ` +
                    `${e.antworten} Antworten, ${e.uebersprungen} übersprungen.`,
            },
      );
      router.refresh();
    });
  }

  return (
    <div className="space-y-2">
      <div className="flex flex-wrap gap-2">
        <Button variant="secondary" size="sm" loading={pending} onClick={testen}>
          <PlugZap aria-hidden="true" />
          Verbindung testen
        </Button>
        <Button variant="secondary" size="sm" loading={pending} onClick={abrufen}>
          <RefreshCw aria-hidden="true" />
          Jetzt abrufen
        </Button>
      </div>

      {meldung && (
        <p
          role="status"
          className={cn(
            "flex max-w-72 items-start gap-1.5 rounded-md px-2.5 py-1.5 text-xs",
            meldung.ok
              ? "bg-success-soft text-success-soft-fg"
              : "bg-danger-soft text-danger-soft-fg",
          )}
        >
          {meldung.ok ? (
            <CheckCircle2 className="mt-0.5 size-3.5 shrink-0" aria-hidden="true" />
          ) : (
            <AlertTriangle className="mt-0.5 size-3.5 shrink-0" aria-hidden="true" />
          )}
          {meldung.text}
        </p>
      )}
    </div>
  );
}
