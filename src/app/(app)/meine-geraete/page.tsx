import type { Metadata } from "next";
import Link from "next/link";
import { redirect } from "next/navigation";
import { Laptop, LifeBuoy } from "lucide-react";

import { PageHeader } from "@/components/layout/page-header";
import { Badge } from "@/components/ui/badge";
import { Card, CardContent } from "@/components/ui/card";
import { db } from "@/server/db";
import { getActor } from "@/server/policy";
import { ASSET_STATUS, ASSET_STATUS_TON, MAC_KIND } from "@/lib/domain";
import { formatDate } from "@/lib/format";

export const metadata: Metadata = { title: "Meine Geräte" };

/**
 * Die eigenen Geräte – nur zum Ansehen.
 *
 * Bewusst ohne jeden Bedienknopf. Wer sein Notebook zurückgeben will, sagt der
 * IT Bescheid; ein Knopf „zurückgeben" würde das Gerät im System zurückbuchen,
 * während es noch auf dem Schreibtisch steht, und niemand wüsste danach, wo es
 * wirklich ist.
 *
 * Was hier zählt, ist die Antwort auf zwei Fragen: Welche Geräte laufen auf
 * meinen Namen? Und wie melde ich ein Problem damit?
 */
export default async function MeineGeraetePage() {
  const actor = await getActor();
  if (!actor) redirect("/anmelden");

  const geraete = await db.asset.findMany({
    where: { assignedToId: actor.id },
    orderBy: [{ kind: { sortOrder: "asc" } }, { assetTag: "asc" }],
    include: {
      kind: { select: { name: true } },
      manufacturer: { select: { name: true } },
      site: { select: { name: true } },
      photo: { select: { id: true } },
      macs: { orderBy: { createdAt: "asc" } },
    },
  });

  return (
    <div className="mx-auto max-w-4xl">
      <PageHeader
        title="Meine Geräte"
        description="Was auf Ihren Namen läuft. Wenn etwas nicht stimmt oder ein Gerät fehlt, schreiben Sie bitte der IT."
      />

      <div className="space-y-4 px-4 py-5 sm:px-6">
        {geraete.length === 0 && (
          <Card>
            <CardContent className="py-12 text-center">
              <Laptop
                className="mx-auto size-8 text-fg-subtle"
                aria-hidden="true"
              />
              <p className="mt-2 text-sm text-fg-muted">
                Auf Ihren Namen ist zurzeit kein Gerät eingetragen.
              </p>
            </CardContent>
          </Card>
        )}

        {geraete.map((g) => (
          <Card key={g.id}>
            <CardContent className="flex flex-wrap gap-5 py-5">
              <span className="flex size-24 shrink-0 items-center justify-center overflow-hidden rounded-lg border border-line bg-surface-2">
                {g.photo ? (
                  // eslint-disable-next-line @next/next/no-img-element
                  <img
                    src={`/api/uploads/${g.photo.id}`}
                    alt={`Foto von ${g.name}`}
                    className="size-full object-cover"
                  />
                ) : (
                  <Laptop className="size-8 text-fg-subtle" aria-hidden="true" />
                )}
              </span>

              <div className="min-w-56 flex-1">
                <div className="flex flex-wrap items-center gap-2">
                  <h2 className="text-base font-semibold text-fg">{g.name}</h2>
                  <Badge tone={ASSET_STATUS_TON[g.status]} size="sm">
                    {ASSET_STATUS[g.status]}
                  </Badge>
                </div>

                <dl className="mt-2 space-y-1 text-sm">
                  <Angabe label="Inventarnummer" wert={g.assetTag} mono />
                  <Angabe label="Art" wert={g.kind.name} />
                  <Angabe
                    label="Gerät"
                    wert={[g.manufacturer?.name, g.model].filter(Boolean).join(" ")}
                  />
                  <Angabe label="Seriennummer" wert={g.serial} mono />
                  <Angabe
                    label="Bei Ihnen seit"
                    wert={g.assignedAt ? formatDate(g.assignedAt) : null}
                  />
                  <Angabe label="Standort" wert={g.site?.name} />
                  {g.macs.length > 0 && (
                    <Angabe
                      label="Netzwerkadresse"
                      wert={g.macs
                        .map((m) => `${m.address} (${MAC_KIND[m.kind]})`)
                        .join(", ")}
                      mono
                    />
                  )}
                </dl>

                <p className="mt-3">
                  <Link
                    href={`/neu?geraet=${encodeURIComponent(g.assetTag)}`}
                    className="inline-flex items-center gap-1.5 text-sm text-primary hover:underline"
                  >
                    <LifeBuoy className="size-4" aria-hidden="true" />
                    Problem mit diesem Gerät melden
                  </Link>
                </p>
              </div>
            </CardContent>
          </Card>
        ))}

        {geraete.length > 0 && (
          <p className="px-1 text-xs text-fg-subtle">
            Diese Übersicht dient nur zur Ansicht. Änderungen – etwa eine
            Rückgabe oder ein Tausch – nimmt die IT vor, damit im System und im
            Regal dasselbe steht.
          </p>
        )}
      </div>
    </div>
  );
}

function Angabe({
  label,
  wert,
  mono,
}: {
  label: string;
  wert: string | null | undefined;
  mono?: boolean;
}) {
  if (!wert) return null;
  return (
    <div className="flex flex-wrap gap-x-2">
      <dt className="w-36 shrink-0 text-fg-muted">{label}</dt>
      <dd className={`min-w-0 flex-1 text-fg ${mono ? "font-mono text-xs" : ""}`}>
        {wert}
      </dd>
    </div>
  );
}
