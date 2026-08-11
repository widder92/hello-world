"use client";

import Link from "next/link";
import { usePathname } from "next/navigation";
import { useState } from "react";
import { TeamMode } from "@prisma/client";
import {
  BarChart3,
  BookOpen,
  Boxes,
  ChevronDown,
  Inbox,
  Laptop,
  LayoutDashboard,
  ListChecks,
  Search,
  Settings,
  UserCircle,
} from "lucide-react";

import { teamIcon } from "@/components/layout/icon";
import type { Navigation } from "@/server/navigation";
import { cn } from "@/lib/utils";

/**
 * Seitennavigation des Arbeitsplatzes.
 *
 * Die Bereiche stehen im Mittelpunkt, weil dort die Arbeit liegt. Zwei Zahlen
 * je Eingang: die offenen Vorgänge insgesamt und – hervorgehoben – die, die
 * noch niemand übernommen hat. Letztere sind das eigentliche Signal, dass
 * jemand etwas tun muss.
 */
export function Sidebar({
  navigation,
  isAdmin,
  onNavigate,
}: {
  navigation: Navigation;
  isAdmin: boolean;
  onNavigate?: () => void;
}) {
  const pathname = usePathname();

  return (
    <nav aria-label="Hauptnavigation" className="flex h-full flex-col gap-6 overflow-y-auto px-3 py-4">
      <div className="space-y-0.5">
        <NavLink
          href="/dashboard"
          icon={LayoutDashboard}
          active={pathname === "/dashboard"}
          onNavigate={onNavigate}
        >
          Übersicht
        </NavLink>
        <NavLink
          href="/meine-tickets-bearbeitung"
          icon={ListChecks}
          active={pathname === "/meine-tickets-bearbeitung"}
          badge={navigation.myOpenCount}
          onNavigate={onNavigate}
        >
          Meine Vorgänge
        </NavLink>
        <NavLink
          href="/suche"
          icon={Search}
          active={pathname.startsWith("/suche")}
          onNavigate={onNavigate}
        >
          Suche
        </NavLink>
      </div>

      {navigation.teams.map((team) => (
        <TeamSection
          key={team.id}
          team={team}
          pathname={pathname}
          onNavigate={onNavigate}
        />
      ))}

      <div className="mt-auto space-y-0.5 border-t border-line pt-4">
        <NavLink
          href="/wissen"
          icon={BookOpen}
          active={pathname.startsWith("/wissen")}
          onNavigate={onNavigate}
        >
          Wissensdatenbank
        </NavLink>
        {navigation.hasInventory && (
          <NavLink
            href="/inventar"
            icon={Boxes}
            active={pathname.startsWith("/inventar")}
            onNavigate={onNavigate}
          >
            Inventar
          </NavLink>
        )}
        {navigation.hasStatistics && (
          <NavLink
            href="/statistik"
            icon={BarChart3}
            active={pathname.startsWith("/statistik")}
            onNavigate={onNavigate}
          >
            Auswertung
          </NavLink>
        )}
        {navigation.meineGeraeteCount > 0 && (
          <NavLink
            href="/meine-geraete"
            icon={Laptop}
            active={pathname.startsWith("/meine-geraete")}
            badge={navigation.meineGeraeteCount}
            onNavigate={onNavigate}
          >
            Meine Geräte
          </NavLink>
        )}
        <NavLink
          href="/meine-tickets"
          icon={UserCircle}
          active={pathname.startsWith("/meine-tickets") && pathname !== "/meine-tickets-bearbeitung"}
          onNavigate={onNavigate}
        >
          Meine eigenen Meldungen
        </NavLink>
        {isAdmin && (
          <NavLink
            href="/verwaltung"
            icon={Settings}
            active={pathname.startsWith("/verwaltung")}
            onNavigate={onNavigate}
          >
            Verwaltung
          </NavLink>
        )}
      </div>
    </nav>
  );
}

function TeamSection({
  team,
  pathname,
  onNavigate,
}: {
  team: Navigation["teams"][number];
  pathname: string;
  onNavigate?: () => void;
}) {
  const Icon = teamIcon(team.icon);
  const mehrereEingaenge = team.queues.length > 1;
  const [offen, setOffen] = useState(true);

  const einfach = team.mode === TeamMode.EINFACH;
  // Die schlanke Ausbaustufe kennt keinen Sammeleingang – dort führt der
  // Bereichseintrag direkt auf die eine Liste, die es gibt.
  const basisPfad = einfach ? "/unsere-tickets" : "/eingang";

  // Bereiche mit genau einem Eingang bekommen keine eigene Überschrift: Bei
  // der IT stünde sonst zweimal "IT" untereinander. Der Bereich erscheint dann
  // als einzelner Eintrag mit seinem Symbol.
  if (!mehrereEingaenge) {
    const queue = team.queues[0];
    if (!queue) return null;
    const href = `${basisPfad}/${queue.id}`;

    return (
      <div className="space-y-0.5">
        <NavLink
          href={href}
          icon={Icon}
          active={pathname === href || pathname.startsWith(`${href}/`)}
          badge={einfach ? queue.openCount : queue.unassignedCount}
          badgeTone={einfach ? "neutral" : "primary"}
          secondary={einfach ? undefined : `${queue.openCount} offen`}
          onNavigate={onNavigate}
        >
          {team.name}
        </NavLink>
        {!einfach && (
          <NavLink
            href={`/tickets?bereich=${team.id}`}
            icon={ListChecks}
            active={false}
            onNavigate={onNavigate}
            muted
          >
            Alle Vorgänge
          </NavLink>
        )}
      </div>
    );
  }

  return (
    <div>
      <div className="mb-1 flex items-center gap-1.5 px-2">
        <span
          className="size-2 shrink-0 rounded-full"
          style={{ backgroundColor: team.color }}
          aria-hidden="true"
        />
        <h2 className="flex-1 truncate text-xs font-semibold uppercase tracking-wide text-fg-subtle">
          {team.name}
        </h2>
        {einfach && (
          <span
            className="rounded bg-surface-2 px-1.5 py-0.5 text-[0.625rem] font-medium text-fg-subtle"
            title="Dieser Bereich nutzt die schlanke Ausbaustufe: empfangen, antworten, abschließen, weitergeben."
          >
            einfach
          </span>
        )}
        <button
          type="button"
          onClick={() => setOffen((o) => !o)}
          aria-expanded={offen}
          className="rounded p-0.5 text-fg-subtle transition-colors hover:bg-surface-2 hover:text-fg"
        >
          <ChevronDown
            className={cn("size-3.5 transition-transform", !offen && "-rotate-90")}
            aria-hidden="true"
          />
          <span className="sr-only">
            Eingänge von {team.name} {offen ? "einklappen" : "ausklappen"}
          </span>
        </button>
      </div>

      {offen && (
        <div className="space-y-0.5">
          {team.queues.map((queue) => {
            const href = `${basisPfad}/${queue.id}`;
            return (
              <NavLink
                key={queue.id}
                href={href}
                icon={mehrereEingaenge ? Inbox : Icon}
                active={pathname === href || pathname.startsWith(`${href}/`)}
                badge={einfach ? queue.openCount : queue.unassignedCount}
                badgeTone={einfach ? "neutral" : "primary"}
                secondary={einfach ? undefined : `${queue.openCount} offen`}
                onNavigate={onNavigate}
              >
                {/* Bei mehreren Eingängen ist der Standort die eigentliche
                    Information – der Bereichsname steht schon in der
                    Überschrift darüber. */}
                {mehrereEingaenge ? (queue.siteName ?? queue.name) : queue.name}
              </NavLink>
            );
          })}

          {!einfach && (
            <NavLink
              href={`/tickets?bereich=${team.id}`}
              icon={ListChecks}
              active={pathname === "/tickets"}
              onNavigate={onNavigate}
              muted
            >
              Alle Vorgänge
            </NavLink>
          )}
        </div>
      )}
    </div>
  );
}

function NavLink({
  href,
  icon: Icon,
  children,
  active,
  badge,
  badgeTone = "primary",
  secondary,
  muted,
  onNavigate,
}: {
  href: string;
  icon: React.ComponentType<{ className?: string; "aria-hidden"?: boolean }>;
  children: React.ReactNode;
  active?: boolean;
  badge?: number;
  badgeTone?: "primary" | "neutral";
  secondary?: string;
  muted?: boolean;
  onNavigate?: () => void;
}) {
  return (
    <Link
      href={href}
      onClick={onNavigate}
      aria-current={active ? "page" : undefined}
      className={cn(
        "group flex items-center gap-2.5 rounded-md px-2 py-2 text-sm transition-colors",
        active
          ? "bg-primary-soft font-medium text-primary-soft-fg"
          : muted
            ? "text-fg-subtle hover:bg-surface-2 hover:text-fg"
            : "text-fg-muted hover:bg-surface-2 hover:text-fg",
      )}
    >
      <Icon className="size-4 shrink-0" aria-hidden={true} />
      <span className="min-w-0 flex-1 truncate">{children}</span>

      {secondary && !badge && (
        <span className="shrink-0 text-xs tabular-nums text-fg-subtle">
          {secondary}
        </span>
      )}

      {badge !== undefined && badge > 0 && (
        <span
          className={cn(
            "shrink-0 rounded-full px-1.5 py-0.5 text-[0.6875rem] font-semibold tabular-nums",
            badgeTone === "primary"
              ? "bg-primary text-primary-fg"
              : "bg-surface-3 text-fg-muted",
          )}
        >
          {badge > 99 ? "99+" : badge}
          <span className="sr-only">
            {badgeTone === "primary"
              ? " Vorgänge warten auf Übernahme"
              : " offene Vorgänge"}
          </span>
        </span>
      )}
    </Link>
  );
}
