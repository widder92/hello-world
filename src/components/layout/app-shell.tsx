"use client";

import Link from "next/link";
import { useEffect, useState } from "react";
import { usePathname } from "next/navigation";
import { LifeBuoy, Menu, PenLine, Phone, X } from "lucide-react";

import { Sidebar } from "@/components/layout/sidebar";
import { UserMenu } from "@/components/layout/user-menu";
import { NotificationBell } from "@/components/layout/notification-bell";
import { Button } from "@/components/ui/button";
import type { Navigation } from "@/server/navigation";
import { cn } from "@/lib/utils";

/**
 * Rahmen des Arbeitsplatzes: feste Seitenleiste ab Tabletbreite, darunter eine
 * ausfahrbare Schublade.
 *
 * Die Schublade schließt sich beim Seitenwechsel von selbst – sonst bliebe sie
 * nach jedem Klick offen über dem Inhalt liegen.
 */
export function AppShell({
  navigation,
  user,
  children,
}: {
  navigation: Navigation;
  user: {
    id: string;
    name: string;
    email: string;
    isAdmin: boolean;
    theme: string;
    a11yPrefs: Record<string, boolean>;
  };
  children: React.ReactNode;
}) {
  const [drawerOffen, setDrawerOffen] = useState(false);
  const pathname = usePathname();

  useEffect(() => {
    setDrawerOffen(false);
  }, [pathname]);

  // Solange die Schublade offen ist, soll der Hintergrund nicht mitscrollen.
  useEffect(() => {
    document.body.style.overflow = drawerOffen ? "hidden" : "";
    return () => {
      document.body.style.overflow = "";
    };
  }, [drawerOffen]);

  return (
    <div className="flex min-h-dvh bg-bg">
      {/* Seitenleiste ab lg */}
      <aside className="sticky top-0 hidden h-dvh w-64 shrink-0 border-r border-line bg-surface lg:flex lg:flex-col">
        <Marke />
        <Sidebar navigation={navigation} isAdmin={user.isAdmin} />
      </aside>

      {/* Schublade darunter */}
      {drawerOffen && (
        <div className="fixed inset-0 z-50 lg:hidden">
          <button
            type="button"
            className="absolute inset-0 bg-black/45"
            onClick={() => setDrawerOffen(false)}
            aria-label="Navigation schließen"
          />
          <div
            role="dialog"
            aria-label="Navigation"
            aria-modal="true"
            className="relative flex h-full w-72 max-w-[85vw] flex-col bg-surface shadow-pop"
          >
            <div className="flex items-center justify-between border-b border-line pr-2">
              <Marke />
              <Button
                variant="ghost"
                size="icon-sm"
                onClick={() => setDrawerOffen(false)}
              >
                <X aria-hidden="true" />
                <span className="sr-only">Navigation schließen</span>
              </Button>
            </div>
            <Sidebar
              navigation={navigation}
              isAdmin={user.isAdmin}
              onNavigate={() => setDrawerOffen(false)}
            />
          </div>
        </div>
      )}

      <div className="flex min-w-0 flex-1 flex-col">
        <header className="sticky top-0 z-30 flex h-14 items-center gap-2 border-b border-line bg-surface/90 px-3 backdrop-blur sm:px-4">
          <Button
            variant="ghost"
            size="icon-sm"
            className="lg:hidden"
            onClick={() => setDrawerOffen(true)}
            aria-expanded={drawerOffen}
          >
            <Menu aria-hidden="true" />
            <span className="sr-only">Navigation öffnen</span>
          </Button>

          <div className="lg:hidden">
            <Link href="/" className="flex items-center gap-2">
              <LifeBuoy className="size-5 text-primary" aria-hidden="true" />
              <span className="font-semibold">Ticketsystem</span>
            </Link>
          </div>

          <div className="ml-auto flex items-center gap-1.5">
            {/* Für den Anruf, bei dem jemand nicht selbst schreibt. Steht
                bewusst neben „Neue Meldung": Der Unterschied ist nur, für wen
                der Vorgang ist, und beides gehört an dieselbe Stelle. */}
            <Button asChild variant="secondary" size="sm">
              <Link href="/aufnehmen">
                <Phone aria-hidden="true" />
                <span className="hidden md:inline">Vorgang aufnehmen</span>
                <span className="md:hidden sr-only">
                  Vorgang für jemanden aufnehmen
                </span>
              </Link>
            </Button>
            <Button asChild variant="secondary" size="sm">
              <Link href="/neu">
                <PenLine aria-hidden="true" />
                <span className="hidden sm:inline">Neue Meldung</span>
                <span className="sm:hidden sr-only">Neue Meldung schreiben</span>
              </Link>
            </Button>
            <NotificationBell initialUnread={navigation.unreadNotifications} />
            <UserMenu user={user} />
          </div>
        </header>

        <main id="hauptinhalt" className={cn("flex-1", "min-w-0")}>
          {children}
        </main>
      </div>
    </div>
  );
}

function Marke() {
  return (
    <Link
      href="/"
      className="flex h-14 shrink-0 items-center gap-2.5 px-4 text-fg"
    >
      <LifeBuoy className="size-6 text-primary" aria-hidden="true" />
      <span className="text-base font-semibold">Ticketsystem</span>
    </Link>
  );
}
