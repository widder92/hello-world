import { redirect } from "next/navigation";

import { AppShell } from "@/components/layout/app-shell";
import { PortalShell } from "@/components/layout/portal-shell";
import { db } from "@/server/db";
import { buildNavigation } from "@/server/navigation";
import { getActor, isAgent } from "@/server/policy";

/**
 * Gemeinsamer Rahmen für alle angemeldeten Seiten.
 *
 * Welcher Rahmen erscheint, hängt daran, ob jemand in einem Bereich arbeitet:
 *
 *   Bearbeitende   Seitenleiste mit Eingängen, Zählern und Auswertung
 *   Meldende       schlanke Kopfzeile mit drei Punkten
 *
 * Beide teilen sich dieselbe Ticketansicht. Ein Melder, der seinen Vorgang
 * öffnet, sieht denselben Verlauf im Portal-Rahmen – ohne interne Notizen und
 * ohne Bearbeitungsknöpfe. Die einzelnen Arbeitsplatz-Seiten prüfen zusätzlich
 * selbst, ob sie aufgerufen werden dürfen.
 */
export default async function AppLayout({
  children,
}: {
  children: React.ReactNode;
}) {
  const actor = await getActor();
  if (!actor) redirect("/anmelden");

  const nutzer = {
    id: actor.id,
    name: actor.name,
    email: actor.email,
    isAdmin: actor.isAdmin,
    theme: actor.theme,
    a11yPrefs: actor.a11yPrefs,
  };

  if (!isAgent(actor)) {
    // „Meine Geräte" erscheint im Portal nur, wenn dieser Person tatsächlich
    // ein Gerät zugewiesen ist – sonst führte der Punkt auf eine leere Seite.
    const eigeneGeraete = await db.asset.count({
      where: { assignedToId: actor.id },
    });

    return (
      <PortalShell user={nutzer} eigeneGeraete={eigeneGeraete}>
        {children}
      </PortalShell>
    );
  }

  const navigation = await buildNavigation(actor);

  return (
    <AppShell navigation={navigation} user={nutzer}>
      {children}
    </AppShell>
  );
}
