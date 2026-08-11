import {
  Boxes,
  Bus,
  FileText,
  HeartHandshake,
  Home,
  Landmark,
  Laptop,
  Monitor,
  Package,
  Phone,
  Printer,
  School,
  Settings,
  Sparkles,
  UtensilsCrossed,
  Users,
  Wrench,
  type LucideIcon,
} from "lucide-react";

/**
 * Symbole, die Bereiche in der Verwaltung frei wählen können.
 *
 * Bewusst eine feste Auswahl statt eines beliebigen Namens: So lässt sich das
 * Symbol serverseitig auflösen, ohne die gesamte Symbolbibliothek an den
 * Browser auszuliefern – und ein Tippfehler in der Verwaltung führt zu einem
 * Ersatzsymbol statt zu einer leeren Stelle.
 */
export const TEAM_ICONS: Record<string, LucideIcon> = {
  Monitor,
  Laptop,
  Printer,
  Phone,
  Wrench,
  Home,
  School,
  Landmark,
  Users,
  FileText,
  UtensilsCrossed,
  Bus,
  Package,
  Boxes,
  HeartHandshake,
  Settings,
  Sparkles,
};

export const TEAM_ICON_NAMES = Object.keys(TEAM_ICONS);

export function teamIcon(name: string | null | undefined): LucideIcon {
  return (name && TEAM_ICONS[name]) || Wrench;
}
