import type { Metadata } from "next";

import { PageHeader } from "@/components/layout/page-header";
import { GeraetFormular } from "@/components/inventar/geraet-formular";
import { inventarStammdaten } from "@/server/actions/inventar";

export const metadata: Metadata = { title: "Gerät anlegen" };

export default async function GeraetNeuPage() {
  // Der Rahmen (layout.tsx) hat den Zugang bereits geprüft; die Aktion prüft
  // ihn erneut, bevor sie etwas schreibt.
  const stammdaten = await inventarStammdaten();

  return (
    <div className="mx-auto max-w-3xl">
      <PageHeader
        title="Gerät anlegen"
        description="Das Gerät liegt danach im Lager. Herausgeben lässt es sich auf der Geräteseite – dabei entsteht das Übergabeprotokoll."
      />
      <div className="px-4 py-5 sm:px-6">
        <GeraetFormular stammdaten={stammdaten} />
      </div>
    </div>
  );
}
