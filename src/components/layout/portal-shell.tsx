"use client";

import Link from "next/link";
import { usePathname } from "next/navigation";
import { BookOpen, Laptop, LifeBuoy, PenLine, Ticket } from "lucide-react";

import { NotificationBell } from "@/components/layout/notification-bell";
import { UserMenu } from "@/components/layout/user-menu";
import { Button } from "@/components/ui/button";
import { cn } from "@/lib/utils";

/**
 * Rahmen für Meldende – die große Mehrheit im Haus.
 *
 * Bewusst schlicht: eine Kopfzeile, drei Punkte, keine Seitenleiste. Wer
 * dreimal im Jahr eine Meldung schreibt, soll nicht erst eine Oberfläche
 * lernen müssen. Die Beschriftungen sind Alltagssprache statt Fachbegriffe:
 * „Meine Meldungen", nicht „Ticketübersicht".
 */
export function PortalShell({
  user,
  eigeneGeraete = 0,
  children,
}: {
  user: {
    id: string;
    name: string;
    email: string;
    isAdmin: boolean;
    theme: string;
    a11yPrefs: Record<string, boolean>;
  };
  /** Anzahl der Geräte, die dieser Person zugewiesen sind. */
  eigeneGeraete?: number;
  children: React.ReactNode;
}) {
  const pathname = usePathname();
  const einfacheAnsicht = Boolean(user.a11yPrefs.simpleMode);

  const punkte = [
    { href: "/meine-tickets", label: "Meine Meldungen", icon: Ticket },
    // Nur wer Geräte hat, bekommt den Punkt zu sehen.
    ...(eigeneGeraete > 0
      ? [{ href: "/meine-geraete", label: "Meine Geräte", icon: Laptop }]
      : []),
    { href: "/wissen", label: "Hilfe und Anleitungen", icon: BookOpen },
  ];

  return (
    <div className="flex min-h-dvh flex-col bg-bg">
      <header className="sticky top-0 z-30 border-b border-line bg-surface/90 backdrop-blur">
        <div className="mx-auto flex h-16 max-w-5xl items-center gap-3 px-4 sm:px-6">
          <Link href="/meine-tickets" className="flex items-center gap-2.5">
            <LifeBuoy
              className={cn("text-primary", einfacheAnsicht ? "size-8" : "size-6")}
              aria-hidden="true"
            />
            <span
              className={cn(
                "font-semibold",
                einfacheAnsicht ? "text-xl" : "text-base",
              )}
            >
              Ticketsystem
            </span>
          </Link>

          <nav aria-label="Hauptnavigation" className="ml-4 hidden gap-1 sm:flex">
            {punkte.map((p) => {
              const aktiv = pathname.startsWith(p.href);
              return (
                <Link
                  key={p.href}
                  href={p.href}
                  aria-current={aktiv ? "page" : undefined}
                  className={cn(
                    "flex items-center gap-2 rounded-md px-3 py-2 font-medium transition-colors",
                    einfacheAnsicht ? "text-base" : "text-sm",
                    aktiv
                      ? "bg-primary-soft text-primary-soft-fg"
                      : "text-fg-muted hover:bg-surface-2 hover:text-fg",
                  )}
                >
                  <p.icon className="size-4" aria-hidden="true" />
                  {p.label}
                </Link>
              );
            })}
          </nav>

          <div className="ml-auto flex items-center gap-2">
            <Button asChild size={einfacheAnsicht ? "lg" : "md"}>
              <Link href="/neu">
                <PenLine aria-hidden="true" />
                <span className="hidden sm:inline">Neue Meldung</span>
                <span className="sm:hidden">Melden</span>
              </Link>
            </Button>
            <NotificationBell initialUnread={0} />
            <UserMenu user={user} />
          </div>
        </div>

        {/* Auf schmalen Geräten wandert die Navigation unter die Kopfzeile,
            damit die Schaltfläche „Neue Meldung" oben bleibt. */}
        <nav
          aria-label="Hauptnavigation"
          className="flex gap-1 border-t border-line px-2 py-1.5 sm:hidden"
        >
          {punkte.map((p) => {
            const aktiv = pathname.startsWith(p.href);
            return (
              <Link
                key={p.href}
                href={p.href}
                aria-current={aktiv ? "page" : undefined}
                className={cn(
                  "flex flex-1 items-center justify-center gap-1.5 rounded-md px-2 py-2 text-sm font-medium transition-colors",
                  aktiv
                    ? "bg-primary-soft text-primary-soft-fg"
                    : "text-fg-muted",
                )}
              >
                <p.icon className="size-4" aria-hidden="true" />
                {p.label}
              </Link>
            );
          })}
        </nav>
      </header>

      <main id="hauptinhalt" className="mx-auto w-full max-w-5xl flex-1 px-4 py-6 sm:px-6">
        {children}
      </main>

      <footer className="border-t border-line py-4">
        <p className="mx-auto max-w-5xl px-4 text-xs text-fg-subtle sm:px-6">
          Bitte tragen Sie keine Gesundheitsdaten oder Namen von Klientinnen und
          Klienten in eine Meldung ein.
        </p>
      </footer>
    </div>
  );
}
