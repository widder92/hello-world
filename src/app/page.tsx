import { redirect } from "next/navigation";

import { getActor, isAgent } from "@/server/policy";

/**
 * Startseite: leitet dorthin, wo die angemeldete Person tatsächlich arbeitet.
 *
 * Wer in keinem Bereich Mitglied ist – die große Mehrheit im Haus – landet im
 * Melder-Portal. Wer Tickets bearbeitet, kommt direkt in seinen Arbeitsplatz.
 */
export default async function StartPage() {
  const actor = await getActor();
  if (!actor) redirect("/anmelden");

  if (isAgent(actor)) redirect("/dashboard");
  redirect("/meine-tickets");
}
