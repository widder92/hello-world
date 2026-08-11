"use client";

import { useRouter, useSearchParams } from "next/navigation";
import { MapPin } from "lucide-react";

import { Button } from "@/components/ui/button";
import {
  DropdownMenu,
  DropdownMenuCheckboxItem,
  DropdownMenuContent,
  DropdownMenuLabel,
  DropdownMenuSeparator,
  DropdownMenuTrigger,
} from "@/components/ui/dropdown";
import type { QueueInfo } from "@/server/policy";

/**
 * Standort-Umschalter.
 *
 * Wer mehrere Häuser betreut, arbeitet damit in einer Liste statt in mehreren.
 * Der eigene Standort ist die Startansicht und lässt sich nicht abwählen –
 * sonst stünde man vor einer leeren Seite und müsste raten, warum.
 *
 * Es erscheinen nur die Eingänge, für die tatsächlich eine Freigabe besteht;
 * der Server prüft das ohnehin noch einmal.
 */
export function StandortUmschalter({
  aktueller,
  eingaenge,
  gewaehlt,
}: {
  aktueller: string;
  eingaenge: QueueInfo[];
  gewaehlt: string[];
}) {
  const router = useRouter();
  const searchParams = useSearchParams();

  const weitere = eingaenge.filter((q) => q.id !== aktueller);
  const zusaetzlich = gewaehlt.filter((id) => id !== aktueller);

  function umschalten(queueId: string, an: boolean) {
    const naechste = an
      ? [...zusaetzlich, queueId]
      : zusaetzlich.filter((id) => id !== queueId);

    const params = new URLSearchParams(searchParams.toString());
    if (naechste.length) {
      params.set("standorte", naechste.join(","));
    } else {
      params.delete("standorte");
    }
    // Beim Wechsel der Auswahl zurück auf Seite 1 – sonst landet man auf einer
    // Seite, die es in der neuen Menge vielleicht gar nicht gibt.
    params.delete("seite");

    router.push(`?${params.toString()}`);
  }

  const aktuellerEingang = eingaenge.find((q) => q.id === aktueller);

  return (
    <DropdownMenu>
      <DropdownMenuTrigger asChild>
        <Button variant="secondary" size="sm">
          <MapPin aria-hidden="true" />
          {zusaetzlich.length > 0
            ? `${zusaetzlich.length + 1} Standorte`
            : (aktuellerEingang?.siteName ?? "Standort")}
        </Button>
      </DropdownMenuTrigger>

      <DropdownMenuContent align="end" className="w-64">
        <DropdownMenuLabel>Weitere Standorte einblenden</DropdownMenuLabel>

        <div className="px-2.5 py-1.5 text-sm text-fg-muted">
          <span className="font-medium text-fg">
            {aktuellerEingang?.siteName ?? aktuellerEingang?.name}
          </span>
          <span className="ml-1.5 text-xs">(Startansicht)</span>
        </div>

        {weitere.length > 0 && <DropdownMenuSeparator />}

        {weitere.map((q) => (
          <DropdownMenuCheckboxItem
            key={q.id}
            checked={zusaetzlich.includes(q.id)}
            onCheckedChange={(v) => umschalten(q.id, Boolean(v))}
            onSelect={(e) => e.preventDefault()}
          >
            {q.siteName ?? q.name}
          </DropdownMenuCheckboxItem>
        ))}

        {weitere.length === 0 && (
          <p className="px-2.5 py-2 text-xs text-fg-subtle">
            Für weitere Standorte sind Sie nicht freigegeben.
          </p>
        )}
      </DropdownMenuContent>
    </DropdownMenu>
  );
}
