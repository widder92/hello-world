import { redirect } from "next/navigation";

/**
 * Die frühere Geräteliste in der Verwaltung.
 *
 * Sie konnte nur lesen und lag an der falschen Stelle: Geräte pflegt die IT
 * täglich, das ist Arbeit und keine Systemeinstellung. Der Inhalt steckt jetzt
 * unter /inventar.
 *
 * Die Weiterleitung bleibt bestehen, weil der Pfad in Lesezeichen und in
 * älteren Vorgängen verlinkt ist – ein Klick darf nicht ins Leere gehen.
 */
export default async function GeraeteVerwaltungPage({
  searchParams,
}: {
  searchParams: Promise<{ q?: string; suche?: string; standort?: string }>;
}) {
  const sp = await searchParams;
  const suche = (sp.q ?? sp.suche ?? "").trim();

  const ziel = new URLSearchParams();
  if (suche) ziel.set("q", suche);
  if (sp.standort) ziel.set("standort", sp.standort);

  const query = ziel.toString();
  redirect(query ? `/inventar?${query}` : "/inventar");
}
