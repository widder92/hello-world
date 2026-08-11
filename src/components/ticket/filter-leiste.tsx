"use client";

import { useRouter, useSearchParams } from "next/navigation";
import { useState, useTransition } from "react";
import { Search, X } from "lucide-react";

import { Button } from "@/components/ui/button";
import { Input, Select } from "@/components/ui/input";
import type { QueueInfo } from "@/server/policy";
import { PRIORITY } from "@/lib/domain";

/**
 * Filterleiste über der Ticketliste.
 *
 * Der Zustand steckt in der Adresse und nicht im Bauteil: So lässt sich eine
 * Ansicht als Lesezeichen ablegen, an eine Kollegin schicken und mit dem
 * Zurück-Knopf verlassen. Ein Filterzustand, der beim Neuladen verschwindet,
 * ist im Alltag ärgerlich.
 */
export function FilterLeiste({
  bereiche,
  standorte,
  eingaenge,
}: {
  bereiche: { id: string; name: string; color: string }[];
  standorte: { id: string; name: string }[];
  eingaenge: QueueInfo[];
}) {
  const router = useRouter();
  const searchParams = useSearchParams();
  const [pending, startTransition] = useTransition();
  const [suche, setSuche] = useState(searchParams.get("q") ?? "");

  function setze(schluessel: string, wert: string | null) {
    const p = new URLSearchParams(searchParams.toString());
    if (wert) p.set(schluessel, wert);
    else p.delete(schluessel);
    // Bei jeder Filteränderung zurück auf die erste Seite.
    p.delete("seite");
    startTransition(() => router.push(`?${p.toString()}`));
  }

  function sucheAbsenden(e: React.FormEvent) {
    e.preventDefault();
    setze("q", suche.trim() || null);
  }

  const aktiveFilter = [
    "bereich",
    "standort",
    "prioritaet",
    "zuweisung",
    "eingang",
    "ueberfaellig",
    "q",
  ].filter((k) => searchParams.get(k));

  return (
    <div className="space-y-2.5">
      <div className="flex flex-wrap items-center gap-2">
        <form onSubmit={sucheAbsenden} className="relative min-w-56 flex-1">
          <label htmlFor="ticket-suche" className="sr-only">
            Vorgänge durchsuchen
          </label>
          <Search
            className="pointer-events-none absolute left-2.5 top-1/2 size-4 -translate-y-1/2 text-fg-subtle"
            aria-hidden="true"
          />
          <Input
            id="ticket-suche"
            value={suche}
            onChange={(e) => setSuche(e.target.value)}
            placeholder="Nummer, Betreff, Raum oder Melder …"
            className="h-9 pl-8.5"
          />
        </form>

        <FilterSelect
          label="Status"
          wert={searchParams.get("status") ?? "offen"}
          optionen={[
            ["offen", "Offen"],
            ["erledigt", "Erledigt"],
            ["alle", "Alle"],
          ]}
          onChange={(v) => setze("status", v === "offen" ? null : v)}
        />

        <FilterSelect
          label="Zuweisung"
          wert={searchParams.get("zuweisung") ?? ""}
          optionen={[
            ["", "Alle"],
            ["meine", "Bei mir"],
            ["offen", "Ohne Zuweisung"],
          ]}
          onChange={(v) => setze("zuweisung", v || null)}
        />

        {bereiche.length > 1 && (
          <FilterSelect
            label="Bereich"
            wert={searchParams.get("bereich") ?? ""}
            optionen={[
              ["", "Alle Bereiche"],
              ...bereiche.map((b) => [b.id, b.name] as [string, string]),
            ]}
            onChange={(v) => setze("bereich", v || null)}
          />
        )}

        {eingaenge.length > 1 && (
          <FilterSelect
            label="Eingang"
            wert={searchParams.get("eingang") ?? ""}
            optionen={[
              ["", "Alle Eingänge"],
              ...eingaenge.map(
                (q) => [q.id, q.siteName ?? q.name] as [string, string],
              ),
            ]}
            onChange={(v) => setze("eingang", v || null)}
          />
        )}

        <FilterSelect
          label="Standort"
          wert={searchParams.get("standort") ?? ""}
          optionen={[
            ["", "Alle Standorte"],
            ...standorte.map((s) => [s.id, s.name] as [string, string]),
          ]}
          onChange={(v) => setze("standort", v || null)}
        />

        <FilterSelect
          label="Priorität"
          wert={searchParams.get("prioritaet") ?? ""}
          optionen={[
            ["", "Alle"],
            ...Object.entries(PRIORITY).map(
              ([k, d]) => [k, d.label] as [string, string],
            ),
          ]}
          onChange={(v) => setze("prioritaet", v || null)}
        />

        <FilterSelect
          label="Sortierung"
          wert={searchParams.get("sortierung") ?? "aktivitaet"}
          optionen={[
            ["aktivitaet", "Zuletzt passiert"],
            ["frist", "Frist zuerst"],
            ["prioritaet", "Priorität"],
            ["neueste", "Neueste zuerst"],
            ["aelteste", "Älteste zuerst"],
          ]}
          onChange={(v) => setze("sortierung", v === "aktivitaet" ? null : v)}
        />
      </div>

      {aktiveFilter.length > 0 && (
        <div className="flex flex-wrap items-center gap-2">
          <Button
            variant="ghost"
            size="sm"
            disabled={pending}
            onClick={() => {
              setSuche("");
              startTransition(() => router.push("?"));
            }}
          >
            <X aria-hidden="true" />
            Filter zurücksetzen ({aktiveFilter.length})
          </Button>
        </div>
      )}
    </div>
  );
}

function FilterSelect({
  label,
  wert,
  optionen,
  onChange,
}: {
  label: string;
  wert: string;
  optionen: [string, string][];
  onChange: (wert: string) => void;
}) {
  const id = `filter-${label.toLowerCase()}`;
  return (
    <div className="flex items-center gap-1.5">
      <label htmlFor={id} className="sr-only">
        {label}
      </label>
      <Select
        id={id}
        value={wert}
        onChange={(e) => onChange(e.target.value)}
        className="h-9 w-auto min-w-32 text-sm"
        aria-label={label}
      >
        {optionen.map(([w, l]) => (
          <option key={w} value={w}>
            {l}
          </option>
        ))}
      </Select>
    </div>
  );
}
