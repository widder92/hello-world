"use client";

import Link from "next/link";
import { usePathname } from "next/navigation";
import {
  Boxes,
  Building2,
  ClipboardList,
  Clock,
  FileText,
  Inbox,
  Laptop,
  Mail,
  MessageSquareText,
  ScrollText,
  Tags,
  Upload,
  Users,
} from "lucide-react";

import { cn } from "@/lib/utils";

/**
 * Navigation der Verwaltung.
 *
 * Nach Nähe zum Alltag gruppiert statt alphabetisch: Was den Betrieb prägt
 * (Bereiche, Eingänge, Benutzer) steht oben, Stammdaten in der Mitte,
 * Nachschlagbares unten.
 */
const GRUPPEN = [
  {
    titel: "Struktur",
    punkte: [
      { href: "/verwaltung/bereiche", label: "Bereiche", icon: Boxes },
      { href: "/verwaltung/eingaenge", label: "Eingänge", icon: Inbox },
      { href: "/verwaltung/benutzer", label: "Benutzer", icon: Users },
    ],
  },
  {
    titel: "E-Mail und Fristen",
    punkte: [
      { href: "/verwaltung/mailkonten", label: "Postfächer", icon: Mail },
      { href: "/verwaltung/sla", label: "Fristen", icon: Clock },
      { href: "/verwaltung/dienstzeiten", label: "Dienstzeiten", icon: Clock },
    ],
  },
  {
    titel: "Stammdaten",
    punkte: [
      { href: "/verwaltung/standorte", label: "Standorte", icon: Building2 },
      { href: "/verwaltung/kategorien", label: "Kategorien", icon: Tags },
      // Das Inventar wird nicht hier gepflegt, sondern dort, wo damit
      // gearbeitet wird – die Bereichsleitung kommt sonst nicht heran.
      { href: "/inventar/stammdaten", label: "Inventar-Stammdaten", icon: Laptop },
    ],
  },
  {
    titel: "Inhalte",
    punkte: [
      {
        href: "/verwaltung/textbausteine",
        label: "Textbausteine",
        icon: MessageSquareText,
      },
      { href: "/verwaltung/vorlagen", label: "Vorlagen", icon: ClipboardList },
      { href: "/verwaltung/wissen", label: "Wissensartikel", icon: FileText },
    ],
  },
  {
    titel: "Betrieb",
    punkte: [
      { href: "/verwaltung/import", label: "Datenübernahme", icon: Upload },
      { href: "/verwaltung/protokoll", label: "Protokoll", icon: ScrollText },
    ],
  },
];

export function VerwaltungsNavigation() {
  const pathname = usePathname();

  return (
    <nav
      aria-label="Verwaltung"
      className="flex gap-4 overflow-x-auto px-3 py-3 lg:sticky lg:top-14 lg:max-h-[calc(100dvh-3.5rem)] lg:flex-col lg:gap-5 lg:overflow-y-auto lg:py-4"
    >
      {GRUPPEN.map((gruppe) => (
        <div key={gruppe.titel} className="shrink-0">
          <h2 className="mb-1 px-2 text-xs font-semibold uppercase tracking-wide text-fg-subtle">
            {gruppe.titel}
          </h2>
          <ul className="flex gap-1 lg:flex-col lg:gap-0.5">
            {gruppe.punkte.map((p) => {
              const aktiv = pathname === p.href;
              return (
                <li key={p.href}>
                  <Link
                    href={p.href}
                    aria-current={aktiv ? "page" : undefined}
                    className={cn(
                      "flex items-center gap-2 whitespace-nowrap rounded-md px-2.5 py-2 text-sm transition-colors",
                      aktiv
                        ? "bg-primary-soft font-medium text-primary-soft-fg"
                        : "text-fg-muted hover:bg-surface-2 hover:text-fg",
                    )}
                  >
                    <p.icon className="size-4 shrink-0" aria-hidden="true" />
                    {p.label}
                  </Link>
                </li>
              );
            })}
          </ul>
        </div>
      ))}
    </nav>
  );
}
