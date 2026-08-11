import { redirect } from "next/navigation";

import { VerwaltungsNavigation } from "@/components/verwaltung/navigation";
import { getActor } from "@/server/policy";

/**
 * Rahmen für die Verwaltung.
 *
 * Nur für die Administration. Bereichsleitungen pflegen ihren Bereich über die
 * Einstellungen im Arbeitsplatz – hier geht es um Dinge, die das ganze Haus
 * betreffen: Bereiche, Konten, Postfächer, Stammdaten.
 */
export default async function VerwaltungLayout({
  children,
}: {
  children: React.ReactNode;
}) {
  const actor = await getActor();
  if (!actor) redirect("/anmelden");
  if (!actor.isAdmin) redirect("/");

  return (
    <div className="flex min-h-full flex-col lg:flex-row">
      <div className="shrink-0 border-b border-line bg-surface lg:w-60 lg:border-b-0 lg:border-r">
        <VerwaltungsNavigation />
      </div>
      <div className="min-w-0 flex-1">{children}</div>
    </div>
  );
}
