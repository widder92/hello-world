"use client";

import { useState, useTransition } from "react";
import { useRouter } from "next/navigation";
import { AlertCircle, GitMerge } from "lucide-react";

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
import { Field, Input } from "@/components/ui/input";
import { zusammenfuehrenAction } from "@/server/actions/tickets";

/**
 * Doppelte Vorgänge zusammenführen.
 *
 * Kommt vor allem bei Störungen vor, die viele betreffen: Beim Netzwerkausfall
 * meldet sich ein ganzes Haus. Der zusammengeführte Vorgang wird geschlossen,
 * sein Melder wird beim führenden Vorgang auf Kopie gesetzt – sonst verlöre er
 * die Sache aus den Augen und meldete sie ein drittes Mal.
 */
export function ZusammenfuehrenDialog({
  ticketId,
  ticketNummer,
}: {
  ticketId: string;
  ticketNummer: string;
}) {
  const router = useRouter();
  const [offen, setOffen] = useState(false);
  const [pending, startTransition] = useTransition();
  const [fehler, setFehler] = useState<string | null>(null);
  const [zielNummer, setZielNummer] = useState("");

  function absenden() {
    setFehler(null);

    const nummer = zielNummer.trim().toUpperCase();
    if (!nummer) {
      setFehler("Bitte geben Sie die Nummer des führenden Vorgangs an.");
      return;
    }
    if (nummer === ticketNummer.toUpperCase()) {
      setFehler("Ein Vorgang lässt sich nicht mit sich selbst zusammenführen.");
      return;
    }

    startTransition(async () => {
      try {
        const ergebnis = await zusammenfuehrenAction(ticketId, nummer);
        setOffen(false);
        router.push(`/tickets/${ergebnis.zielNummer}`);
      } catch (e) {
        setFehler(
          e instanceof Error
            ? e.message
            : "Die Vorgänge konnten nicht zusammengeführt werden.",
        );
      }
    });
  }

  return (
    <Dialog open={offen} onOpenChange={setOffen}>
      <DialogTrigger asChild>
        <Button variant="secondary" size="sm">
          <GitMerge aria-hidden="true" />
          Zusammenführen
        </Button>
      </DialogTrigger>

      <DialogContent size="sm">
        <DialogHeader>
          <DialogTitle>Vorgang zusammenführen</DialogTitle>
          <DialogDescription>
            {ticketNummer} wird in einen anderen Vorgang eingegliedert und
            danach geschlossen. Nachrichten und Anhänge wandern mit.
          </DialogDescription>
        </DialogHeader>

        <DialogBody className="space-y-4">
          <Field
            label="Nummer des führenden Vorgangs"
            htmlFor="merge-nummer"
            required
            help="Der Vorgang, der bestehen bleibt. Er muss in einem Ihrer Eingänge liegen."
          >
            <Input
              value={zielNummer}
              onChange={(e) => setZielNummer(e.target.value)}
              placeholder="IT-2026-000042"
              className="font-mono uppercase"
              autoFocus
            />
          </Field>

          <p className="rounded-md bg-info-soft px-3 py-2.5 text-xs text-info-soft-fg">
            Der Melder von {ticketNummer} wird beim führenden Vorgang auf Kopie
            gesetzt und bleibt so auf dem Laufenden. Zusammenführen lässt sich
            nicht rückgängig machen.
          </p>

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
            Zusammenführen
          </Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  );
}
