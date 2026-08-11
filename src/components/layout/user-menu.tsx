"use client";

import Link from "next/link";
import { useTransition } from "react";
import { signOut } from "next-auth/react";
import {
  Contrast,
  LogOut,
  Monitor,
  Moon,
  Settings,
  Sun,
  Type,
  Wind,
} from "lucide-react";

import { Avatar } from "@/components/ui/avatar";
import {
  DropdownMenu,
  DropdownMenuCheckboxItem,
  DropdownMenuContent,
  DropdownMenuItem,
  DropdownMenuLabel,
  DropdownMenuSeparator,
  DropdownMenuTrigger,
} from "@/components/ui/dropdown";
import { setA11yPrefsAction, setThemeAction } from "@/server/actions/preferences";

/**
 * Benutzermenü mit den Barrierefreiheitsmodi direkt zur Hand.
 *
 * Die Modi stehen hier und nicht nur tief in den Einstellungen, weil sie sonst
 * niemand findet. Wer im Dienstzimmer kurz an einen fremden Rechner geht,
 * schaltet die große Schrift so in zwei Klicks ein.
 */
export function UserMenu({
  user,
}: {
  user: {
    name: string;
    email: string;
    isAdmin: boolean;
    theme: string;
    a11yPrefs: Record<string, boolean>;
  };
}) {
  const [pending, startTransition] = useTransition();

  const setzeThema = (theme: "light" | "dark" | "system") =>
    startTransition(() => {
      void setThemeAction(theme);
    });

  const schalte = (key: string, wert: boolean) =>
    startTransition(() => {
      void setA11yPrefsAction({ [key]: wert });
    });

  return (
    <DropdownMenu>
      <DropdownMenuTrigger
        className="rounded-full transition-opacity hover:opacity-85 disabled:opacity-60"
        disabled={pending}
      >
        <Avatar name={user.name} size="sm" />
        <span className="sr-only">Menü für {user.name} öffnen</span>
      </DropdownMenuTrigger>

      <DropdownMenuContent align="end" className="w-64">
        <div className="px-2.5 py-2">
          <p className="truncate text-sm font-medium text-fg">{user.name}</p>
          <p className="truncate text-xs text-fg-muted">{user.email}</p>
        </div>

        <DropdownMenuSeparator />

        <DropdownMenuLabel>Darstellung</DropdownMenuLabel>
        <div className="flex gap-1 px-1.5 pb-1.5">
          {(
            [
              ["light", Sun, "Hell"],
              ["dark", Moon, "Dunkel"],
              ["system", Monitor, "System"],
            ] as const
          ).map(([wert, Icon, label]) => (
            <button
              key={wert}
              type="button"
              onClick={() => setzeThema(wert)}
              aria-pressed={user.theme === wert}
              className={
                "flex flex-1 flex-col items-center gap-1 rounded-md border px-2 py-2 text-[0.6875rem] transition-colors " +
                (user.theme === wert
                  ? "border-primary bg-primary-soft text-primary-soft-fg"
                  : "border-line text-fg-muted hover:bg-surface-2")
              }
            >
              <Icon className="size-4" aria-hidden="true" />
              {label}
            </button>
          ))}
        </div>

        <DropdownMenuSeparator />

        <DropdownMenuLabel>Barrierefreiheit</DropdownMenuLabel>
        <DropdownMenuCheckboxItem
          checked={Boolean(user.a11yPrefs.largeText)}
          onCheckedChange={(v) => schalte("largeText", Boolean(v))}
          onSelect={(e) => e.preventDefault()}
        >
          <Type className="size-4 text-fg-subtle" aria-hidden="true" />
          Große Schrift
        </DropdownMenuCheckboxItem>
        <DropdownMenuCheckboxItem
          checked={Boolean(user.a11yPrefs.highContrast)}
          onCheckedChange={(v) => schalte("highContrast", Boolean(v))}
          onSelect={(e) => e.preventDefault()}
        >
          <Contrast className="size-4 text-fg-subtle" aria-hidden="true" />
          Hoher Kontrast
        </DropdownMenuCheckboxItem>
        <DropdownMenuCheckboxItem
          checked={Boolean(user.a11yPrefs.reduceMotion)}
          onCheckedChange={(v) => schalte("reduceMotion", Boolean(v))}
          onSelect={(e) => e.preventDefault()}
        >
          <Wind className="size-4 text-fg-subtle" aria-hidden="true" />
          Bewegung reduzieren
        </DropdownMenuCheckboxItem>

        <DropdownMenuSeparator />

        <DropdownMenuItem asChild>
          <Link href="/einstellungen">
            <Settings aria-hidden="true" />
            Einstellungen
          </Link>
        </DropdownMenuItem>
        <DropdownMenuItem
          tone="danger"
          onSelect={() => void signOut({ redirectTo: "/anmelden" })}
        >
          <LogOut aria-hidden="true" />
          Abmelden
        </DropdownMenuItem>
      </DropdownMenuContent>
    </DropdownMenu>
  );
}
