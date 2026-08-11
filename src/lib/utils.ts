import { type ClassValue, clsx } from "clsx";
import { twMerge } from "tailwind-merge";

/** Klassen zusammenführen, wobei spätere Tailwind-Klassen frühere überschreiben. */
export function cn(...inputs: ClassValue[]) {
  return twMerge(clsx(inputs));
}

/** Kürzt einen Text auf eine Höchstlänge und hängt ein Auslassungszeichen an. */
export function truncate(text: string, max: number): string {
  if (text.length <= max) return text;
  return text.slice(0, max - 1).trimEnd() + "…";
}

/** Initialen für Avatare, z. B. "Anna Bergmann" → "AB". */
export function initials(name: string): string {
  const parts = name.trim().split(/\s+/).filter(Boolean);
  if (parts.length === 0) return "?";
  if (parts.length === 1) return parts[0].slice(0, 2).toUpperCase();
  return (parts[0][0] + parts[parts.length - 1][0]).toUpperCase();
}

/** Kombinierende Akzentzeichen (U+0300–U+036F), die nach NFD übrig bleiben. */
const COMBINING_MARKS = new RegExp("[\\u0300-\\u036f]", "g");

/**
 * Erzeugt aus einem Text eine URL-taugliche Kennung. Deutsche Umlaute werden
 * vorher ausgeschrieben, damit aus "Wohnheim Grünanger" nicht
 * "wohnheim-grnanger" wird, sondern "wohnheim-gruenanger".
 */
export function slugify(text: string): string {
  return text
    .toLowerCase()
    .replace(/ä/g, "ae")
    .replace(/ö/g, "oe")
    .replace(/ü/g, "ue")
    .replace(/ß/g, "ss")
    .normalize("NFD")
    .replace(COMBINING_MARKS, "")
    .replace(/[^a-z0-9]+/g, "-")
    .replace(/^-+|-+$/g, "")
    .slice(0, 64);
}
