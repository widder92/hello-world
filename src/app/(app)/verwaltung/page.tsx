import { redirect } from "next/navigation";

/** Die Verwaltung startet bei den Bereichen – dort liegt die Struktur. */
export default function VerwaltungPage() {
  redirect("/verwaltung/bereiche");
}
