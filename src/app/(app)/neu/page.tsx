import type { Metadata } from "next";
import { redirect } from "next/navigation";

import { MeldeFormular } from "@/components/portal/melde-formular";
import { db } from "@/server/db";
import { getActor } from "@/server/policy";
import { portalTeams } from "@/server/services/routing";

export const metadata: Metadata = { title: "Neue Meldung" };

/**
 * Neue Meldung aufgeben.
 *
 * Der Ablauf folgt genau der Frage, die sich stellt: An wen? Was ist los? Wo?
 * Die Bereichswahl steht am Anfang, damit die Meldung direkt beim zuständigen
 * Bereich landet – wer den Hausmeister meint, wählt den Hausmeister, und das
 * Ticket geht nicht erst zur IT.
 */
export default async function NeuePage({
  searchParams,
}: {
  searchParams: Promise<{ geraet?: string }>;
}) {
  const actor = await getActor();
  if (!actor) redirect("/anmelden");

  const sp = await searchParams;

  const [bereiche, standorte] = await Promise.all([
    portalTeams(),
    db.site.findMany({
      where: { isActive: true },
      select: { id: true, name: true, shortName: true, type: true, city: true },
      orderBy: [{ sortOrder: "asc" }, { name: "asc" }],
    }),
  ]);

  // Aus „Meine Geräte" kommt die Inventarnummer mit. Nur eigene Geräte werden
  // angenommen – die Server-Aktion prüft dasselbe noch einmal, bevor sie die
  // Verknüpfung schreibt.
  const geraet = sp.geraet
    ? await db.asset.findFirst({
        where: { assetTag: sp.geraet, assignedToId: actor.id },
        select: {
          assetTag: true,
          name: true,
          serial: true,
          kind: { select: { name: true } },
        },
      })
    : null;

  return (
    <MeldeFormular
      bereiche={bereiche.map((b) => ({
        id: b.id,
        name: b.name,
        color: b.color,
        icon: b.icon,
        queueMode: b.queueMode,
        portalLabel: b.portalLabel,
        portalDescription: b.portalDescription,
        // Standorte, die dieser Bereich tatsächlich bedient.
        standortIds: b.queues
          .map((q) => q.site?.id)
          .filter((id): id is string => Boolean(id)),
        kategorien: b.categories.map((k) => ({
          id: k.id,
          name: k.name,
          description: k.description,
          formSchema: k.formSchema,
          unterkategorien: k.children.map((u) => ({
            id: u.id,
            name: u.name,
            description: u.description,
            formSchema: u.formSchema,
          })),
        })),
      }))}
      standorte={standorte}
      eigenerStandortId={actor.siteId}
      einfacheAnsicht={Boolean(actor.a11yPrefs.simpleMode)}
      geraet={geraet}
    />
  );
}
