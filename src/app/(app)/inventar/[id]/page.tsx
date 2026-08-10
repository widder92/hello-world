import type { Metadata } from "next";
import Link from "next/link";
import { notFound, redirect } from "next/navigation";
import { AssetStatus } from "@prisma/client";
import {
  ArrowLeft,
  FileText,
  Pencil,
  Phone,
  Ticket as TicketIcon,
} from "lucide-react";

import { PageHeader } from "@/components/layout/page-header";
import {
  FotoBereich,
  NotizFeld,
  StatusDialog,
} from "@/components/inventar/geraet-aktionen";
import { Avatar } from "@/components/ui/avatar";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import { Card, CardContent } from "@/components/ui/card";
import { db } from "@/server/db";
import { getActor, inventoryAccess, inventoryTeamIds } from "@/server/policy";
import {
  centAlsEuro,
  moeglicheZiele,
  uebergangErlaubt,
} from "@/server/services/inventar";
import {
  ASSET_STATUS,
  ASSET_STATUS_TON,
  HANDOVER_DIRECTION,
  MAC_KIND,
} from "@/lib/domain";
import { formatDate, formatDateTime } from "@/lib/format";

export const metadata: Metadata = { title: "Gerät" };

/** Ein Feld der Datenübersicht. Leere Werte werden gar nicht erst gezeigt. */
function Zeile({
  label,
  wert,
  mono,
}: {
  label: string;
  wert: React.ReactNode;
  mono?: boolean;
}) {
  if (wert === null || wert === undefined || wert === "") return null;
  return (
    <div className="flex flex-wrap gap-x-3 py-1.5">
      <dt className="w-44 shrink-0 text-sm text-fg-muted">{label}</dt>
      <dd className={`min-w-0 flex-1 text-sm text-fg ${mono ? "font-mono" : ""}`}>
        {wert}
      </dd>
    </div>
  );
}

export default async function GeraetPage({
  params,
}: {
  params: Promise<{ id: string }>;
}) {
  const actor = await getActor();
  if (!actor) redirect("/anmelden");

  const stufe = inventoryAccess(actor);
  if (stufe === "none") redirect("/meine-geraete");

  const { id } = await params;

  const bereiche = actor.isAdmin
    ? (
        await db.team.findMany({
          where: { hasInventory: true },
          select: { id: true },
        })
      ).map((t) => t.id)
    : inventoryTeamIds(actor);

  const geraet = await db.asset.findFirst({
    where: { id, teamId: { in: bereiche } },
    include: {
      kind: true,
      team: { select: { name: true } },
      manufacturer: true,
      site: { select: { name: true } },
      costCenter: { select: { code: true, name: true } },
      assignedTo: { select: { id: true, name: true, email: true, jobTitle: true } },
      photo: { select: { id: true } },
      macs: { orderBy: { createdAt: "asc" } },
      events: { orderBy: { createdAt: "desc" }, take: 50 },
      handovers: { orderBy: { handedOverAt: "desc" } },
      tickets: {
        select: {
          ticket: {
            select: {
              id: true,
              number: true,
              subject: true,
              status: true,
              createdAt: true,
            },
          },
        },
      },
    },
  });

  if (!geraet) notFound();

  // Nur Übergänge anbieten, die auch erlaubt sind – ein Knopf, der beim
  // Klicken eine Fehlermeldung bringt, ist schlimmer als keiner.
  const ziele = moeglicheZiele(geraet.status)
    .map((status) => {
      const regel = uebergangErlaubt(geraet.status, status)!;
      return {
        status,
        brauchtPerson: Boolean(regel.brauchtPerson),
        brauchtGrund: Boolean(regel.brauchtGrund),
        mitProtokoll: Boolean(regel.protokoll),
        nurLeitung: Boolean(regel.nurLeitung),
      };
    })
    .filter((z) => !z.nurLeitung || stufe === "manage");

  const garantieAbgelaufen =
    geraet.warrantyUntil !== null && geraet.warrantyUntil < new Date();

  // Chronik und Protokolle gemeinsam, absteigend – wie der Verlauf eines
  // Vorgangs. Getrennte Listen zwängen zum Hin- und Herspringen.
  const chronik = [
    ...geraet.events.map((e) => ({
      art: "ereignis" as const,
      zeit: e.createdAt,
      daten: e,
    })),
    ...geraet.handovers.map((h) => ({
      art: "protokoll" as const,
      zeit: h.handedOverAt,
      daten: h,
    })),
  ].sort((a, b) => b.zeit.getTime() - a.zeit.getTime());

  return (
    <>
      <PageHeader
        title={geraet.name}
        description={`${geraet.kind.name}${
          geraet.manufacturer ? ` · ${geraet.manufacturer.name}` : ""
        }${geraet.model ? ` ${geraet.model}` : ""}`}
        actions={
          <div className="flex flex-wrap items-center gap-2">
            <Link
              href="/inventar"
              className="inline-flex items-center gap-1.5 rounded-md px-2.5 py-1.5 text-sm text-fg-muted transition-colors hover:bg-surface-2 hover:text-fg"
            >
              <ArrowLeft className="size-4" aria-hidden="true" />
              Inventar
            </Link>
            <Button asChild variant="secondary" size="sm">
              <Link href={`/inventar/${geraet.id}/bearbeiten`}>
                <Pencil aria-hidden="true" />
                Bearbeiten
              </Link>
            </Button>
          </div>
        }
      />

      <div className="grid gap-5 px-4 py-5 sm:px-6 lg:grid-cols-[1fr_20rem]">
        <div className="min-w-0 space-y-5">
          {/* --- Kopf mit Status ------------------------------------- */}
          <Card>
            <CardContent className="space-y-4 py-5">
              <div className="flex flex-wrap items-center gap-2">
                <span className="font-mono text-sm text-primary">
                  {geraet.assetTag}
                </span>
                <Badge tone={ASSET_STATUS_TON[geraet.status]}>
                  {ASSET_STATUS[geraet.status]}
                </Badge>
                {garantieAbgelaufen && (
                  <Badge tone="warning" size="sm">
                    Garantie abgelaufen
                  </Badge>
                )}
              </div>

              {geraet.assignedTo && (
                <div className="flex items-center gap-2.5 rounded-md border border-line bg-surface-2 px-3 py-2.5">
                  <Avatar name={geraet.assignedTo.name} size="sm" />
                  <div className="min-w-0">
                    <p className="text-sm font-medium text-fg">
                      {geraet.assignedTo.name}
                    </p>
                    <p className="text-xs text-fg-muted">
                      {[
                        geraet.assignedTo.jobTitle,
                        geraet.assignedAt
                          ? `seit ${formatDate(geraet.assignedAt)}`
                          : null,
                      ]
                        .filter(Boolean)
                        .join(" · ")}
                    </p>
                  </div>
                </div>
              )}

              {geraet.status === AssetStatus.AUSGESONDERT && geraet.retiredReason && (
                <p className="rounded-md border border-line bg-surface-2 px-3 py-2.5 text-sm text-fg-muted">
                  Ausgesondert
                  {geraet.retiredAt ? ` am ${formatDate(geraet.retiredAt)}` : ""}:{" "}
                  {geraet.retiredReason}
                </p>
              )}

              <StatusDialog
                assetId={geraet.id}
                assetTag={geraet.assetTag}
                von={geraet.status}
                ziele={ziele}
              />
            </CardContent>
          </Card>

          {/* --- Daten ------------------------------------------------ */}
          <Card>
            <CardContent className="py-5">
              <h2 className="mb-2 text-sm font-semibold text-fg">Angaben</h2>
              <dl className="divide-y divide-line">
                <Zeile label="Geräteart" wert={geraet.kind.name} />
                <Zeile label="Bereich" wert={geraet.team.name} />
                <Zeile label="Hersteller" wert={geraet.manufacturer?.name} />
                <Zeile label="Modell" wert={geraet.model} />
                <Zeile label="Seriennummer" wert={geraet.serial} mono />
                <Zeile label="IMEI" wert={geraet.imei} mono />
                <Zeile
                  label="Netzwerkadressen"
                  wert={
                    geraet.macs.length > 0 ? (
                      <ul className="space-y-0.5">
                        {geraet.macs.map((m) => (
                          <li key={m.id} className="font-mono text-xs">
                            {m.address}{" "}
                            <span className="font-sans text-fg-muted">
                              {MAC_KIND[m.kind]}
                            </span>
                          </li>
                        ))}
                      </ul>
                    ) : null
                  }
                />
                <Zeile label="Standort" wert={geraet.site?.name} />
                <Zeile label="Raum" wert={geraet.room} />
                <Zeile label="Lagerort" wert={geraet.storageLocation} />
                <Zeile
                  label="Kostenstelle"
                  wert={
                    geraet.costCenter
                      ? `${geraet.costCenter.code} – ${geraet.costCenter.name}`
                      : null
                  }
                />
                <Zeile
                  label="Anschaffung"
                  wert={geraet.purchasedAt ? formatDate(geraet.purchasedAt) : null}
                />
                <Zeile
                  label="Garantie bis"
                  wert={
                    geraet.warrantyUntil ? formatDate(geraet.warrantyUntil) : null
                  }
                />
                <Zeile
                  label="Anschaffungspreis"
                  wert={
                    geraet.purchasePriceCents !== null
                      ? centAlsEuro(geraet.purchasePriceCents)
                      : null
                  }
                />
                <Zeile label="Lieferant" wert={geraet.supplier} />
                <Zeile label="Rechnungsnummer" wert={geraet.invoiceNumber} />
                <Zeile
                  label="Notizen"
                  wert={
                    geraet.notes ? (
                      <span className="whitespace-pre-wrap">{geraet.notes}</span>
                    ) : null
                  }
                />
              </dl>
            </CardContent>
          </Card>

          {/* --- Chronik ---------------------------------------------- */}
          <Card>
            <CardContent className="py-5">
              <h2 className="mb-3 text-sm font-semibold text-fg">Chronik</h2>

              <ol className="space-y-3">
                {chronik.map((eintrag) => {
                  if (eintrag.art === "protokoll") {
                    const h = eintrag.daten;
                    return (
                      <li
                        key={`h-${h.id}`}
                        className="rounded-md border border-line px-3 py-2.5"
                      >
                        <div className="flex flex-wrap items-center gap-2">
                          <Badge tone="info" size="sm">
                            {HANDOVER_DIRECTION[h.direction]}
                          </Badge>
                          <Link
                            href={`/inventar/${geraet.id}/protokoll/${h.id}`}
                            className="inline-flex items-center gap-1 font-mono text-xs text-primary hover:underline"
                          >
                            <FileText className="size-3.5" aria-hidden="true" />
                            {h.protocolNo}
                          </Link>
                          <span className="text-xs text-fg-subtle">
                            {formatDateTime(h.handedOverAt)}
                          </span>
                        </div>
                        <p className="mt-1 text-sm text-fg">
                          {h.direction === "AUSGABE" ? "An" : "Von"} {h.userName},
                          bearbeitet von {h.actorName}
                        </p>
                        {(h.accessories || h.condition) && (
                          <p className="mt-0.5 text-xs text-fg-muted">
                            {[h.condition, h.accessories].filter(Boolean).join(" · ")}
                          </p>
                        )}
                      </li>
                    );
                  }

                  const e = eintrag.daten;
                  return (
                    <li key={`e-${e.id}`} className="flex gap-3 px-1">
                      <span className="mt-1.5 size-1.5 shrink-0 rounded-full bg-line" />
                      <div className="min-w-0 flex-1">
                        <p className="text-sm text-fg">
                          {beschreibeEreignis(e)}
                        </p>
                        <p className="text-xs text-fg-subtle">
                          {e.actorName} · {formatDateTime(e.createdAt)}
                        </p>
                        {e.note && (
                          <p className="mt-0.5 whitespace-pre-wrap text-sm text-fg-muted">
                            {e.note}
                          </p>
                        )}
                      </div>
                    </li>
                  );
                })}
              </ol>

              <div className="mt-4 border-t border-line pt-4">
                <NotizFeld assetId={geraet.id} />
              </div>
            </CardContent>
          </Card>
        </div>

        {/* --- Seitenspalte ------------------------------------------- */}
        <div className="space-y-5">
          <Card>
            <CardContent className="py-4">
              <h2 className="mb-2.5 text-xs font-semibold uppercase tracking-wide text-fg-subtle">
                Foto
              </h2>
              <FotoBereich assetId={geraet.id} fotoId={geraet.photo?.id ?? null} />
            </CardContent>
          </Card>

          {geraet.manufacturer?.supportPhone && (
            <Card>
              <CardContent className="py-4">
                <h2 className="mb-2 text-xs font-semibold uppercase tracking-wide text-fg-subtle">
                  Hersteller-Support
                </h2>
                <a
                  href={`tel:${geraet.manufacturer.supportPhone.replace(/\s/g, "")}`}
                  className="inline-flex items-center gap-2 text-sm text-primary hover:underline"
                >
                  <Phone className="size-4" aria-hidden="true" />
                  {geraet.manufacturer.supportPhone}
                </a>
                {geraet.manufacturer.website && (
                  <p className="mt-1 truncate text-xs">
                    <a
                      href={geraet.manufacturer.website}
                      target="_blank"
                      rel="noreferrer noopener"
                      className="text-fg-muted hover:underline"
                    >
                      {geraet.manufacturer.website.replace(/^https?:\/\//, "")}
                    </a>
                  </p>
                )}
              </CardContent>
            </Card>
          )}

          {geraet.tickets.length > 0 && (
            <Card>
              <CardContent className="py-4">
                <h2 className="mb-2.5 text-xs font-semibold uppercase tracking-wide text-fg-subtle">
                  Vorgänge zu diesem Gerät
                </h2>
                <ul className="space-y-1.5">
                  {geraet.tickets.map(({ ticket }) => (
                    <li key={ticket.id}>
                      <Link
                        href={`/tickets/${ticket.number}`}
                        className="block rounded-md border border-line px-2.5 py-1.5 text-sm transition-colors hover:bg-surface-2"
                      >
                        <span className="inline-flex items-center gap-1 font-mono text-xs text-fg-subtle">
                          <TicketIcon className="size-3" aria-hidden="true" />
                          {ticket.number}
                        </span>
                        <span className="block truncate text-fg">
                          {ticket.subject}
                        </span>
                      </Link>
                    </li>
                  ))}
                </ul>
              </CardContent>
            </Card>
          )}
        </div>
      </div>
    </>
  );
}

/** Formuliert einen Chronikeintrag als lesbaren Satz. */
function beschreibeEreignis(e: {
  type: string;
  fromStatus: AssetStatus | null;
  toStatus: AssetStatus | null;
}): string {
  switch (e.type) {
    case "ANGELEGT":
      return "Gerät angelegt.";
    case "GEAENDERT":
      return "Angaben geändert.";
    case "NOTIZ":
      return "Vermerk:";
    case "HERAUSGEGEBEN":
      return "Herausgegeben.";
    case "ZURUECKGENOMMEN":
      return "Zurückgenommen.";
    default:
      return e.fromStatus && e.toStatus
        ? `Status: ${ASSET_STATUS[e.fromStatus]} → ${ASSET_STATUS[e.toStatus]}`
        : "Status geändert.";
  }
}
