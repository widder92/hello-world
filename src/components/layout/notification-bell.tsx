"use client";

import Link from "next/link";
import { useEffect, useState, useTransition } from "react";
import { Bell, CheckCheck } from "lucide-react";

import { Button } from "@/components/ui/button";
import {
  DropdownMenu,
  DropdownMenuContent,
  DropdownMenuTrigger,
} from "@/components/ui/dropdown";
import {
  ladeBenachrichtigungen,
  markiereAlleGelesen,
} from "@/server/actions/notifications";
import { formatRelative } from "@/lib/format";
import { cn } from "@/lib/utils";

interface Eintrag {
  id: string;
  title: string;
  body: string | null;
  createdAt: string;
  readAt: string | null;
  ticketNumber: string | null;
}

/**
 * Glocke mit den ungelesenen Hinweisen.
 *
 * Die Liste wird erst beim Öffnen geladen – bei 1000 Konten wäre es
 * verschwenderisch, sie bei jedem Seitenaufruf mitzuschicken, obwohl sie fast
 * nie geöffnet wird.
 */
export function NotificationBell({ initialUnread }: { initialUnread: number }) {
  const [offen, setOffen] = useState(false);
  const [ungelesen, setUngelesen] = useState(initialUnread);
  const [eintraege, setEintraege] = useState<Eintrag[] | null>(null);
  const [pending, startTransition] = useTransition();

  useEffect(() => {
    setUngelesen(initialUnread);
  }, [initialUnread]);

  useEffect(() => {
    if (!offen || eintraege) return;
    void ladeBenachrichtigungen().then(setEintraege);
  }, [offen, eintraege]);

  return (
    <DropdownMenu open={offen} onOpenChange={setOffen}>
      <DropdownMenuTrigger asChild>
        <Button variant="ghost" size="icon-sm" className="relative">
          <Bell aria-hidden="true" />
          {ungelesen > 0 && (
            <span
              className="absolute -right-0.5 -top-0.5 flex min-w-4 items-center justify-center rounded-full bg-danger px-1 text-[0.625rem] font-semibold text-white"
              aria-hidden="true"
            >
              {ungelesen > 9 ? "9+" : ungelesen}
            </span>
          )}
          <span className="sr-only">
            Benachrichtigungen
            {ungelesen > 0 ? ` – ${ungelesen} ungelesen` : " – keine ungelesenen"}
          </span>
        </Button>
      </DropdownMenuTrigger>

      <DropdownMenuContent align="end" className="w-80 p-0">
        <div className="flex items-center justify-between border-b border-line px-3 py-2.5">
          <h2 className="text-sm font-semibold">Benachrichtigungen</h2>
          {ungelesen > 0 && (
            <button
              type="button"
              disabled={pending}
              onClick={() =>
                startTransition(async () => {
                  await markiereAlleGelesen();
                  setUngelesen(0);
                  setEintraege(
                    (e) =>
                      e?.map((x) => ({
                        ...x,
                        readAt: x.readAt ?? new Date().toISOString(),
                      })) ?? null,
                  );
                })
              }
              className="flex items-center gap-1 text-xs font-medium text-primary hover:underline disabled:opacity-50"
            >
              <CheckCheck className="size-3.5" aria-hidden="true" />
              Alle gelesen
            </button>
          )}
        </div>

        <div className="max-h-96 overflow-y-auto">
          {eintraege === null && (
            <p className="px-3 py-6 text-center text-sm text-fg-muted">
              Wird geladen …
            </p>
          )}

          {eintraege?.length === 0 && (
            <p className="px-3 py-8 text-center text-sm text-fg-muted">
              Nichts Neues.
            </p>
          )}

          {eintraege?.map((e) => {
            const inhalt = (
              <>
                <div className="flex items-start gap-2">
                  {!e.readAt && (
                    <span
                      className="mt-1.5 size-2 shrink-0 rounded-full bg-primary"
                      aria-hidden="true"
                    />
                  )}
                  <div className={cn("min-w-0 flex-1", e.readAt && "pl-4")}>
                    <p className="text-sm font-medium text-fg">{e.title}</p>
                    {e.body && (
                      <p className="mt-0.5 line-clamp-2 text-xs text-fg-muted">
                        {e.body}
                      </p>
                    )}
                    <p className="mt-1 text-[0.6875rem] text-fg-subtle">
                      {formatRelative(e.createdAt)}
                      {e.ticketNumber && ` · ${e.ticketNumber}`}
                    </p>
                  </div>
                </div>
              </>
            );

            return e.ticketNumber ? (
              <Link
                key={e.id}
                href={`/tickets/${e.ticketNumber}`}
                className="block border-b border-line px-3 py-2.5 transition-colors last:border-0 hover:bg-surface-2"
              >
                {inhalt}
              </Link>
            ) : (
              <div
                key={e.id}
                className="border-b border-line px-3 py-2.5 last:border-0"
              >
                {inhalt}
              </div>
            );
          })}
        </div>
      </DropdownMenuContent>
    </DropdownMenu>
  );
}
