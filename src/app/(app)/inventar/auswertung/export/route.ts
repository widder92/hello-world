import { AssetStatus } from "@prisma/client";

import { db } from "@/server/db";
import { getActor, inventoryAccess, inventoryTeamIds } from "@/server/policy";
import { ASSET_STATUS } from "@/lib/domain";

/**
 * Inventar als CSV.
 *
 * Für die Buchhaltung und für alles, was hier nicht ausgewertet wird. Bewusst
 * die vollständige Geräteliste und keine fertige Summenaufstellung: In Excel
 * ist eine Pivot-Tabelle in zwei Minuten gebaut, aber aus einer fertigen
 * Summe lassen sich die Einzelposten nicht zurückgewinnen.
 *
 * Semikolon als Trennzeichen, Komma als Dezimalzeichen und ein BOM am Anfang –
 * so öffnet deutsches Excel die Datei ohne Nachfrage und ohne zerlegte
 * Umlaute.
 */

function feld(wert: string | number | null | undefined): string {
  if (wert === null || wert === undefined) return "";
  const text = String(wert);
  // Anführungszeichen verdoppeln, sonst zerfällt die Zeile beim Einlesen.
  return /[";\n\r]/.test(text) ? `"${text.replace(/"/g, '""')}"` : text;
}

function datum(d: Date | null): string {
  return d ? d.toLocaleDateString("de-DE") : "";
}

export async function GET() {
  const actor = await getActor();
  if (!actor) return new Response("Nicht angemeldet.", { status: 401 });
  if (inventoryAccess(actor) === "none") {
    return new Response("Kein Zugriff auf das Inventar.", { status: 403 });
  }

  const bereiche = actor.isAdmin
    ? (
        await db.team.findMany({
          where: { hasInventory: true },
          select: { id: true },
        })
      ).map((t) => t.id)
    : inventoryTeamIds(actor);

  const geraete = await db.asset.findMany({
    where: { teamId: { in: bereiche } },
    orderBy: { assetTag: "asc" },
    include: {
      kind: { select: { name: true } },
      team: { select: { name: true } },
      manufacturer: { select: { name: true } },
      model: { select: { name: true } },
      site: { select: { name: true } },
      costCenter: { select: { code: true, name: true } },
      assignedTo: { select: { name: true, email: true } },
      macs: { select: { address: true } },
    },
  });

  const kopf = [
    "Inventarnummer",
    "Bezeichnung",
    "Geraeteart",
    "Status",
    "Bereich",
    "Hersteller",
    "Modell",
    "Seriennummer",
    "IMEI",
    "MAC-Adressen",
    "Standort",
    "Raum",
    "Lagerort",
    "Kostenstelle",
    "Kostenstelle Bezeichnung",
    "Zugewiesen an",
    "E-Mail",
    "Uebergeben am",
    "Anschaffung",
    "Garantie bis",
    "Anschaffungspreis EUR",
    "Lieferant",
    "Rechnungsnummer",
    "Ausgesondert am",
    "Grund",
  ];

  const zeilen = geraete.map((g) =>
    [
      g.assetTag,
      g.name,
      g.kind.name,
      ASSET_STATUS[g.status],
      g.team.name,
      g.manufacturer?.name,
      g.model?.name,
      g.serial,
      g.imei,
      g.macs.map((m) => m.address).join(" "),
      g.site?.name,
      g.room,
      g.storageLocation,
      g.costCenter?.code,
      g.costCenter?.name,
      g.assignedTo?.name,
      g.assignedTo?.email,
      datum(g.assignedAt),
      datum(g.purchasedAt),
      datum(g.warrantyUntil),
      g.purchasePriceCents === null
        ? ""
        : (g.purchasePriceCents / 100).toFixed(2).replace(".", ","),
      g.supplier,
      g.invoiceNumber,
      g.status === AssetStatus.AUSGESONDERT ? datum(g.retiredAt) : "",
      g.retiredReason,
    ]
      .map(feld)
      .join(";"),
  );

  const heute = new Date().toISOString().slice(0, 10);
  const inhalt = "﻿" + [kopf.join(";"), ...zeilen].join("\r\n") + "\r\n";

  return new Response(inhalt, {
    headers: {
      "Content-Type": "text/csv; charset=utf-8",
      "Content-Disposition": `attachment; filename="inventar-${heute}.csv"`,
      "Cache-Control": "no-store",
    },
  });
}
