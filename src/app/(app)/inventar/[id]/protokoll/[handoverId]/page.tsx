import type { Metadata } from "next";
import Link from "next/link";
import { notFound, redirect } from "next/navigation";
import { ArrowLeft } from "lucide-react";

import { DruckKnopf } from "@/components/inventar/druck-knopf";
import { db } from "@/server/db";
import { getActor, inventoryAccess, inventoryTeamIds } from "@/server/policy";
import { HANDOVER_DIRECTION, MAC_KIND } from "@/lib/domain";
import { formatDate } from "@/lib/format";

export const metadata: Metadata = { title: "Übergabeprotokoll" };

/**
 * Übergabeprotokoll zum Ausdrucken.
 *
 * Bewusst kein PDF: Ein Ausdruck aus dem Browser genügt, wird unterschrieben
 * und abgeheftet – ein PDF-Erzeuger wäre eine weitere Abhängigkeit, die
 * gepflegt und aktuell gehalten werden müsste, ohne dass jemand etwas davon
 * hätte.
 *
 * Das Protokoll zeigt den Stand von damals, soweit er festgehalten wurde:
 * Zubehör, Zustand und Empfänger stehen im Datensatz selbst und ändern sich
 * nicht mehr, wenn das Gerät später weiterwandert.
 */
export default async function ProtokollPage({
  params,
}: {
  params: Promise<{ id: string; handoverId: string }>;
}) {
  const actor = await getActor();
  if (!actor) redirect("/anmelden");
  if (inventoryAccess(actor) === "none") redirect("/meine-geraete");

  const { id, handoverId } = await params;

  const bereiche = actor.isAdmin
    ? (
        await db.team.findMany({
          where: { hasInventory: true },
          select: { id: true },
        })
      ).map((t) => t.id)
    : inventoryTeamIds(actor);

  const protokoll = await db.assetHandover.findFirst({
    where: { id: handoverId, assetId: id, asset: { teamId: { in: bereiche } } },
    include: {
      asset: {
        include: {
          kind: { select: { name: true } },
          manufacturer: { select: { name: true } },
          model: { select: { name: true } },
          site: { select: { name: true } },
          costCenter: { select: { code: true, name: true } },
          macs: { orderBy: { createdAt: "asc" } },
        },
      },
      user: { select: { name: true, email: true, jobTitle: true } },
    },
  });

  if (!protokoll) notFound();

  const g = protokoll.asset;
  const ausgabe = protokoll.direction === "AUSGABE";

  return (
    <div className="mx-auto max-w-3xl px-4 py-6 sm:px-6">
      {/* Kopfleiste – im Ausdruck ausgeblendet. */}
      <div className="mb-6 flex flex-wrap items-center justify-between gap-3 print:hidden">
        <Link
          href={`/inventar/${g.id}`}
          className="inline-flex items-center gap-1.5 rounded-md px-2.5 py-1.5 text-sm text-fg-muted transition-colors hover:bg-surface-2 hover:text-fg"
        >
          <ArrowLeft className="size-4" aria-hidden="true" />
          Zurück zum Gerät
        </Link>
        <DruckKnopf />
      </div>

      {/* Das eigentliche Protokoll. */}
      <article className="rounded-lg border border-line bg-surface p-8 print:rounded-none print:border-0 print:p-0">
        <header className="border-b border-line pb-4">
          <p className="text-xs uppercase tracking-wide text-fg-muted">
            {HANDOVER_DIRECTION[protokoll.direction]} eines Arbeitsmittels
          </p>
          <h1 className="mt-1 text-2xl font-semibold text-fg">
            Übergabeprotokoll
          </h1>
          <p className="mt-1 font-mono text-sm text-fg-muted">
            {protokoll.protocolNo}
          </p>
        </header>

        {/* --- Gerät ------------------------------------------------- */}
        <section className="mt-6">
          <h2 className="mb-2 text-sm font-semibold uppercase tracking-wide text-fg-muted">
            Gerät
          </h2>
          <dl className="divide-y divide-line">
            <Feld label="Inventarnummer" wert={g.assetTag} mono />
            <Feld label="Bezeichnung" wert={g.name} />
            <Feld label="Geräteart" wert={g.kind.name} />
            <Feld
              label="Hersteller und Modell"
              wert={[g.manufacturer?.name, g.model?.name]
                .filter(Boolean)
                .join(" ")}
            />
            <Feld label="Seriennummer" wert={g.serial} mono />
            <Feld label="IMEI" wert={g.imei} mono />
            {g.macs.length > 0 && (
              <Feld
                label="Netzwerkadressen"
                wert={g.macs
                  .map((m) => `${m.address} (${MAC_KIND[m.kind]})`)
                  .join(", ")}
                mono
              />
            )}
            <Feld
              label="Kostenstelle"
              wert={
                g.costCenter ? `${g.costCenter.code} – ${g.costCenter.name}` : null
              }
            />
          </dl>
        </section>

        {/* --- Übergabe ---------------------------------------------- */}
        <section className="mt-6">
          <h2 className="mb-2 text-sm font-semibold uppercase tracking-wide text-fg-muted">
            {ausgabe ? "Empfängerin oder Empfänger" : "Rückgabe durch"}
          </h2>
          <dl className="divide-y divide-line">
            <Feld label="Name" wert={protokoll.userName} />
            <Feld label="Funktion" wert={protokoll.user?.jobTitle} />
            <Feld label="Standort" wert={g.site?.name} />
            <Feld label="Datum" wert={formatDate(protokoll.handedOverAt)} />
            <Feld label="Übergeben durch" wert={protokoll.actorName} />
            <Feld label="Zustand" wert={protokoll.condition} />
            <Feld label="Mitgegebenes Zubehör" wert={protokoll.accessories} />
            <Feld label="Bemerkung" wert={protokoll.note} />
          </dl>
        </section>

        {/* --- Hinweistext -------------------------------------------- */}
        {ausgabe && (
          <section className="mt-6 rounded-md border border-line bg-surface-2 p-4 text-sm leading-relaxed text-fg-muted print:bg-transparent">
            <p>
              Das Gerät bleibt Eigentum des Trägers und wird für dienstliche
              Zwecke überlassen. Es ist sorgfältig zu behandeln und vor Verlust,
              Beschädigung und dem Zugriff Dritter zu schützen. Schäden, Verlust
              und Diebstahl sind der IT unverzüglich zu melden. Bei Beendigung
              des Beschäftigungsverhältnisses oder auf Verlangen ist das Gerät
              samt Zubehör zurückzugeben.
            </p>
          </section>
        )}

        {/* --- Unterschriften ----------------------------------------- */}
        <section className="mt-10 grid gap-8 sm:grid-cols-2">
          <div>
            <div className="h-12 border-b border-fg-subtle" />
            <p className="mt-1 text-xs text-fg-muted">
              Ort, Datum, Unterschrift {ausgabe ? "Empfänger" : "Rückgebender"}
            </p>
            <p className="text-xs text-fg-subtle">{protokoll.userName}</p>
          </div>
          <div>
            <div className="h-12 border-b border-fg-subtle" />
            <p className="mt-1 text-xs text-fg-muted">
              Ort, Datum, Unterschrift IT
            </p>
            <p className="text-xs text-fg-subtle">{protokoll.actorName}</p>
          </div>
        </section>
      </article>
    </div>
  );
}

function Feld({
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
    <div className="flex flex-wrap gap-x-3 py-2">
      <dt className="w-52 shrink-0 text-sm text-fg-muted">{label}</dt>
      <dd className={`min-w-0 flex-1 text-sm text-fg ${mono ? "font-mono" : ""}`}>
        {wert}
      </dd>
    </div>
  );
}
