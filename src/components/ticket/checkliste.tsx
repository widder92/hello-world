"use client";

import { useState, useTransition } from "react";
import { useRouter } from "next/navigation";
import { Plus } from "lucide-react";

import { Checkbox } from "@/components/ui/misc";
import { Input } from "@/components/ui/input";
import {
  aufgabeAnlegenAction,
  aufgabeUmschaltenAction,
} from "@/server/actions/tickets";
import { cn } from "@/lib/utils";

/**
 * Checkliste im Vorgang.
 *
 * Nützlich bei allem, was aus mehreren Schritten besteht – das Einrichten
 * einer neuen Kollegin etwa. Ohne Liste bleibt regelmäßig ein Schritt liegen,
 * und der fällt erst am ersten Arbeitstag auf.
 */
export function Checkliste({
  ticketId,
  aufgaben,
}: {
  ticketId: string;
  aufgaben: { id: string; title: string; done: boolean }[];
}) {
  const router = useRouter();
  const [pending, startTransition] = useTransition();
  const [neu, setNeu] = useState("");

  const erledigt = aufgaben.filter((a) => a.done).length;

  function umschalten(id: string, done: boolean) {
    startTransition(async () => {
      await aufgabeUmschaltenAction(id, done);
      router.refresh();
    });
  }

  function anlegen(e: React.FormEvent) {
    e.preventDefault();
    const text = neu.trim();
    if (!text) return;

    startTransition(async () => {
      await aufgabeAnlegenAction(ticketId, text);
      setNeu("");
      router.refresh();
    });
  }

  return (
    <div>
      <div className="mb-2.5 flex items-baseline justify-between gap-2">
        <h2 className="text-xs font-semibold uppercase tracking-wide text-fg-subtle">
          Checkliste
        </h2>
        <span className="text-xs tabular-nums text-fg-subtle">
          {erledigt} von {aufgaben.length}
        </span>
      </div>

      {/* Fortschritt zusätzlich als Text, nicht nur als Balken. */}
      <div
        className="mb-3 h-1.5 overflow-hidden rounded-full bg-surface-3"
        role="progressbar"
        aria-valuenow={erledigt}
        aria-valuemin={0}
        aria-valuemax={aufgaben.length}
        aria-label={`${erledigt} von ${aufgaben.length} Schritten erledigt`}
      >
        <div
          className="h-full rounded-full bg-primary transition-[width]"
          style={{
            width: aufgaben.length
              ? `${(erledigt / aufgaben.length) * 100}%`
              : "0%",
          }}
        />
      </div>

      <ul className="space-y-1">
        {aufgaben.map((a) => (
          <li key={a.id}>
            <label className="flex cursor-pointer items-start gap-2 rounded-md px-1 py-1 transition-colors hover:bg-surface-2">
              <Checkbox
                className="mt-0.5"
                checked={a.done}
                disabled={pending}
                onCheckedChange={(v) => umschalten(a.id, Boolean(v))}
              />
              <span
                className={cn(
                  "text-sm leading-snug",
                  a.done ? "text-fg-subtle line-through" : "text-fg",
                )}
              >
                {a.title}
              </span>
            </label>
          </li>
        ))}
      </ul>

      <form onSubmit={anlegen} className="mt-2.5 flex gap-1.5">
        <label htmlFor="neue-aufgabe" className="sr-only">
          Neuer Schritt
        </label>
        <Input
          id="neue-aufgabe"
          value={neu}
          onChange={(e) => setNeu(e.target.value)}
          placeholder="Schritt hinzufügen …"
          className="h-8 text-sm"
          disabled={pending}
        />
        <button
          type="submit"
          disabled={pending || !neu.trim()}
          className="shrink-0 rounded-md border border-line px-2 text-fg-muted transition-colors hover:bg-surface-2 hover:text-fg disabled:opacity-50"
        >
          <Plus className="size-4" aria-hidden="true" />
          <span className="sr-only">Schritt hinzufügen</span>
        </button>
      </form>
    </div>
  );
}
