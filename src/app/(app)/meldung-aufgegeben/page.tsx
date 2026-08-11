import type { Metadata } from "next";
import { redirect } from "next/navigation";

import { MeldungAufgegeben } from "@/components/portal/melde-formular";
import { db } from "@/server/db";
import { getActor } from "@/server/policy";

export const metadata: Metadata = { title: "Meldung aufgegeben" };

export default async function BestaetigungPage({
  searchParams,
}: {
  searchParams: Promise<{ nummer?: string }>;
}) {
  const actor = await getActor();
  if (!actor) redirect("/anmelden");

  const { nummer } = await searchParams;
  if (!nummer) redirect("/meine-tickets");

  // Nur die eigene Meldung bestätigen – sonst ließe sich über die Adresszeile
  // prüfen, welche Ticketnummern es gibt.
  const ticket = await db.ticket.findFirst({
    where: { number: nummer.toUpperCase(), requesterId: actor.id },
    select: { number: true },
  });
  if (!ticket) redirect("/meine-tickets");

  return <MeldungAufgegeben nummer={ticket.number} />;
}
