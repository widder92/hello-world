import { redirect } from "next/navigation";

import { getActor, inventoryAccess } from "@/server/policy";

/**
 * Rahmen für das Inventar.
 *
 * Der Zugang hängt am Bereich, nicht an der Administration: Wer in einem
 * Bereich arbeitet, der ein Inventar führt, kommt herein. Für alle anderen
 * gibt es „Meine Geräte" – dort stehen die eigenen, und mehr braucht es nicht.
 *
 * Die einzelnen Seiten prüfen zusätzlich selbst; diese Weiche erspart nur den
 * Weg auf eine Seite, die ohnehin nichts anzeigen würde.
 */
export default async function InventarLayout({
  children,
}: {
  children: React.ReactNode;
}) {
  const actor = await getActor();
  if (!actor) redirect("/anmelden");
  if (inventoryAccess(actor) === "none") redirect("/meine-geraete");

  return <>{children}</>;
}
