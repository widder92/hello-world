import type { Metadata } from "next";
import { redirect } from "next/navigation";

import { PageHeader } from "@/components/layout/page-header";
import { AufnahmeFormular } from "@/components/ticket/aufnahme-formular";
import {
  bereicheFuerAufnahme,
  standorteFuerAufnahme,
} from "@/server/actions/aufnehmen";
import { getActor, isAgent } from "@/server/policy";

export const metadata: Metadata = { title: "Vorgang aufnehmen" };

/**
 * Vorgang für eine andere Person aufnehmen.
 *
 * Bewusst eine eigene Seite und kein Dialog: Wer am Telefon mitschreibt,
 * braucht Platz und darf nicht mit einem falschen Klick alles verlieren.
 *
 * Nur für Personen mit Bereich. Wer selbst meldet, nimmt das Portal – dort
 * steht der eigene Name schon fest, und das ist genau richtig so.
 */
export default async function AufnehmenPage() {
  const actor = await getActor();
  if (!actor) redirect("/anmelden");
  if (!isAgent(actor)) redirect("/neu");

  const [bereiche, standorte] = await Promise.all([
    bereicheFuerAufnahme(),
    standorteFuerAufnahme(),
  ]);

  return (
    <div className="mx-auto max-w-3xl">
      <PageHeader
        title="Vorgang aufnehmen"
        description={
          "Für jemanden, der angerufen hat. Der Vorgang gehört danach dem " +
          "Melder – er sieht ihn unter seinen Meldungen und kann per " +
          "Antwortmail mitreden."
        }
      />

      <AufnahmeFormular
        bereiche={bereiche}
        standorte={standorte}
        eigeneBereichsIds={actor.memberships.map((m) => m.teamId)}
      />
    </div>
  );
}
