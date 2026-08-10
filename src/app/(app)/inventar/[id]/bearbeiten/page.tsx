import type { Metadata } from "next";
import { notFound, redirect } from "next/navigation";

import { PageHeader } from "@/components/layout/page-header";
import { GeraetFormular } from "@/components/inventar/geraet-formular";
import { db } from "@/server/db";
import { inventarStammdaten } from "@/server/actions/inventar";
import { getActor, inventoryAccess, inventoryTeamIds } from "@/server/policy";

export const metadata: Metadata = { title: "Gerät bearbeiten" };

export default async function GeraetBearbeitenPage({
  params,
}: {
  params: Promise<{ id: string }>;
}) {
  const actor = await getActor();
  if (!actor) redirect("/anmelden");
  if (inventoryAccess(actor) === "none") redirect("/meine-geraete");

  const { id } = await params;

  const bereiche = actor.isAdmin
    ? (
        await db.team.findMany({
          where: { hasInventory: true },
          select: { id: true },
        })
      ).map((t) => t.id)
    : inventoryTeamIds(actor);

  const [geraet, stammdaten] = await Promise.all([
    db.asset.findFirst({
      where: { id, teamId: { in: bereiche } },
      include: { macs: { orderBy: { createdAt: "asc" } } },
    }),
    inventarStammdaten(),
  ]);

  if (!geraet) notFound();

  return (
    <div className="mx-auto max-w-3xl">
      <PageHeader
        title={`${geraet.assetTag} bearbeiten`}
        description="Der Status wird hier nicht geändert – dafür gibt es auf der Geräteseite die Knöpfe zum Herausgeben, Zurücknehmen und Aussondern."
      />
      <div className="px-4 py-5 sm:px-6">
        <GeraetFormular
          stammdaten={stammdaten}
          geraet={{
            id: geraet.id,
            name: geraet.name,
            kindId: geraet.kindId,
            teamId: geraet.teamId,
            manufacturerId: geraet.manufacturerId,
            model: geraet.model,
            serial: geraet.serial,
            imei: geraet.imei,
            siteId: geraet.siteId,
            room: geraet.room,
            storageLocation: geraet.storageLocation,
            costCenterId: geraet.costCenterId,
            purchasedAt: geraet.purchasedAt,
            warrantyUntil: geraet.warrantyUntil,
            purchasePriceCents: geraet.purchasePriceCents,
            supplier: geraet.supplier,
            invoiceNumber: geraet.invoiceNumber,
            notes: geraet.notes,
            macs: geraet.macs.map((m) => ({ address: m.address, kind: m.kind })),
          }}
        />
      </div>
    </div>
  );
}
