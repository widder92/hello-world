import type { Metadata } from "next";
import { redirect } from "next/navigation";
import { TeamMode } from "@prisma/client";
import { AlertTriangle, CheckCircle2, Clock, Inbox } from "lucide-react";

import { teamIcon } from "@/components/layout/icon";
import { PageHeader } from "@/components/layout/page-header";
import { Card } from "@/components/ui/card";
import { Select } from "@/components/ui/input";
import { db } from "@/server/db";
import { canViewStatistics, getActor } from "@/server/policy";
import { PRIORITY, STATUS_ERLEDIGT, STATUS_OFFEN } from "@/lib/domain";
import { formatDuration, formatNumber, formatPercent } from "@/lib/format";
import { cn } from "@/lib/utils";

export const metadata: Metadata = { title: "Auswertung" };

const ZEITRAEUME = [
  ["7", "Letzte 7 Tage"],
  ["30", "Letzte 30 Tage"],
  ["90", "Letztes Quartal"],
  ["365", "Letztes Jahr"],
] as const;

/**
 * Auswertung.
 *
 * Nur für Bereichsleitungen von Bereichen im vollen Arbeitsplatz. Die Zahlen
 * sind bewusst auf das beschränkt, was zu einer Entscheidung führen kann:
 * Wo staut es sich, wo werden Fristen gerissen, welches Haus meldet am
 * meisten. Kennzahlen, aus denen niemand etwas ableitet, erzeugen nur Arbeit.
 */
export default async function StatistikPage({
  searchParams,
}: {
  searchParams: Promise<{ tage?: string; bereich?: string }>;
}) {
  const actor = await getActor();
  if (!actor) redirect("/anmelden");

  const erlaubteBereiche = actor.memberships.filter(
    (m) => m.teamMode === TeamMode.VOLL && canViewStatistics(actor, m.teamId),
  );

  if (erlaubteBereiche.length === 0 && !actor.isAdmin) {
    redirect("/dashboard");
  }

  const sp = await searchParams;
  const tage = Number(sp.tage) || 30;
  const seit = new Date(Date.now() - tage * 24 * 60 * 60 * 1000);

  const bereichsIds = actor.isAdmin
    ? (
        await db.team.findMany({
          where: { mode: TeamMode.VOLL, isActive: true },
          select: { id: true },
        })
      ).map((t) => t.id)
    : erlaubteBereiche.map((m) => m.teamId);

  const gewaehlt =
    sp.bereich && bereichsIds.includes(sp.bereich) ? [sp.bereich] : bereichsIds;

  const basis = { teamId: { in: gewaehlt } };
  const imZeitraum = { ...basis, createdAt: { gte: seit } };

  const [
    bereiche,
    gemeldet,
    erledigt,
    offen,
    ueberfaellig,
    proEingang,
    proStandort,
    proKategorie,
    proBearbeiter,
    proPrioritaet,
    fristDaten,
  ] = await Promise.all([
    db.team.findMany({
      where: { id: { in: gewaehlt } },
      select: { id: true, name: true, color: true, icon: true },
      orderBy: { sortOrder: "asc" },
    }),
    db.ticket.count({ where: imZeitraum }),
    db.ticket.count({
      where: { ...basis, resolvedAt: { gte: seit } },
    }),
    db.ticket.count({ where: { ...basis, status: { in: STATUS_OFFEN } } }),
    db.ticket.count({
      where: {
        ...basis,
        status: { notIn: STATUS_ERLEDIGT },
        resolutionDue: { lt: new Date() },
      },
    }),
    db.ticket.groupBy({
      by: ["queueId"],
      where: { ...basis, status: { in: STATUS_OFFEN } },
      _count: { _all: true },
    }),
    db.ticket.groupBy({
      by: ["siteId"],
      where: imZeitraum,
      _count: { _all: true },
    }),
    db.ticket.groupBy({
      by: ["categoryId"],
      where: imZeitraum,
      _count: { _all: true },
    }),
    db.ticket.groupBy({
      by: ["assigneeId"],
      where: { ...basis, status: { in: STATUS_OFFEN }, assigneeId: { not: null } },
      _count: { _all: true },
    }),
    db.ticket.groupBy({
      by: ["priority"],
      where: imZeitraum,
      _count: { _all: true },
    }),
    db.ticket.findMany({
      where: { ...basis, resolvedAt: { gte: seit }, resolutionDue: { not: null } },
      select: { createdAt: true, resolvedAt: true, resolutionDue: true },
      take: 5000,
    }),
  ]);

  // --- Fristeinhaltung und Durchlaufzeit ----------------------------------
  const eingehalten = fristDaten.filter(
    (t) => t.resolvedAt && t.resolutionDue && t.resolvedAt <= t.resolutionDue,
  ).length;
  const quote = fristDaten.length ? eingehalten / fristDaten.length : null;

  const durchlaufMinuten = fristDaten
    .filter((t) => t.resolvedAt)
    .map((t) => (t.resolvedAt!.getTime() - t.createdAt.getTime()) / 60000);
  const median =
    durchlaufMinuten.length > 0
      ? durchlaufMinuten.sort((a, b) => a - b)[
          Math.floor(durchlaufMinuten.length / 2)
        ]
      : null;

  // --- Bezeichnungen nachladen --------------------------------------------
  const [eingaenge, standorte, kategorien, personen] = await Promise.all([
    db.queue.findMany({
      where: { id: { in: proEingang.map((e) => e.queueId) } },
      select: {
        id: true,
        name: true,
        site: { select: { shortName: true, name: true } },
        team: { select: { name: true, color: true } },
      },
    }),
    db.site.findMany({
      where: {
        id: {
          in: proStandort
            .map((s) => s.siteId)
            .filter((id): id is string => Boolean(id)),
        },
      },
      select: { id: true, name: true, shortName: true },
    }),
    db.category.findMany({
      where: {
        id: {
          in: proKategorie
            .map((k) => k.categoryId)
            .filter((id): id is string => Boolean(id)),
        },
      },
      select: { id: true, name: true },
    }),
    db.user.findMany({
      where: {
        id: {
          in: proBearbeiter
            .map((b) => b.assigneeId)
            .filter((id): id is string => Boolean(id)),
        },
      },
      select: { id: true, name: true },
    }),
  ]);

  const nameVon = <T extends { id: string }>(
    liste: T[],
    id: string | null,
    feld: (t: T) => string,
  ) => (id ? (liste.find((x) => x.id === id) ? feld(liste.find((x) => x.id === id)!) : "—") : "Ohne Angabe");

  return (
    <>
      <PageHeader
        title="Auswertung"
        description={`Zahlen für ${bereiche.map((b) => b.name).join(", ")}.`}
      >
        <form className="flex flex-wrap items-center gap-2">
          <Select
            name="tage"
            defaultValue={String(tage)}
            className="h-9 w-auto min-w-40"
            aria-label="Zeitraum"
          >
            {ZEITRAEUME.map(([wert, label]) => (
              <option key={wert} value={wert}>
                {label}
              </option>
            ))}
          </Select>

          {bereichsIds.length > 1 && (
            <Select
              name="bereich"
              defaultValue={sp.bereich ?? ""}
              className="h-9 w-auto min-w-40"
              aria-label="Bereich"
            >
              <option value="">Alle Bereiche</option>
              {bereiche.map((b) => (
                <option key={b.id} value={b.id}>
                  {b.name}
                </option>
              ))}
            </Select>
          )}

          <button
            type="submit"
            className="rounded-md border border-line px-3 py-1.5 text-sm text-fg-muted transition-colors hover:bg-surface-2 hover:text-fg"
          >
            Anwenden
          </button>
        </form>
      </PageHeader>

      <div className="space-y-6 px-4 py-5 sm:px-6">
        {/* --- Kennzahlen ------------------------------------------------ */}
        <div className="grid gap-3 sm:grid-cols-2 xl:grid-cols-4">
          <Kennzahl
            label={`Gemeldet in ${tage} Tagen`}
            wert={formatNumber(gemeldet)}
            icon={Inbox}
            tone="info"
          />
          <Kennzahl
            label="Davon erledigt"
            wert={formatNumber(erledigt)}
            zusatz={
              gemeldet > 0 ? formatPercent(erledigt / gemeldet) : undefined
            }
            icon={CheckCircle2}
            tone="success"
          />
          <Kennzahl
            label="Fristen eingehalten"
            wert={quote === null ? "—" : formatPercent(quote)}
            zusatz={
              fristDaten.length > 0
                ? `von ${fristDaten.length} mit Frist`
                : "keine Vorgänge mit Frist"
            }
            icon={Clock}
            tone={
              quote === null ? "neutral" : quote >= 0.9 ? "success" : "warning"
            }
          />
          <Kennzahl
            label="Derzeit überfällig"
            wert={formatNumber(ueberfaellig)}
            zusatz={`bei ${offen} offenen`}
            icon={AlertTriangle}
            tone={ueberfaellig > 0 ? "danger" : "success"}
          />
        </div>

        {median !== null && (
          <Card>
            <div className="px-5 py-4">
              <p className="text-sm text-fg-muted">
                Ein Vorgang war im Median nach{" "}
                <strong className="font-semibold text-fg">
                  {formatDuration(median)}
                </strong>{" "}
                erledigt (Kalenderzeit, nicht Dienstzeit).
              </p>
            </div>
          </Card>
        )}

        <div className="grid gap-5 lg:grid-cols-2">
          <Verteilung
            titel="Offen je Eingang"
            hinweis="Wo sich gerade etwas staut."
            eintraege={proEingang
              .map((e) => {
                const q = eingaenge.find((x) => x.id === e.queueId);
                return {
                  label: q?.site
                    ? `${q.team.name} – ${q.site.shortName ?? q.site.name}`
                    : (q?.name ?? "—"),
                  wert: e._count._all,
                  farbe: q?.team.color,
                };
              })
              .sort((a, b) => b.wert - a.wert)}
          />

          <Verteilung
            titel="Aufkommen je Standort"
            hinweis={`Gemeldet in den letzten ${tage} Tagen.`}
            eintraege={proStandort
              .map((s) => ({
                label: nameVon(
                  standorte,
                  s.siteId,
                  (x) => x.shortName ?? x.name,
                ),
                wert: s._count._all,
              }))
              .sort((a, b) => b.wert - a.wert)}
          />

          <Verteilung
            titel="Häufigste Kategorien"
            hinweis="Wiederkehrendes lohnt oft einen Wissensartikel."
            eintraege={proKategorie
              .map((k) => ({
                label: nameVon(kategorien, k.categoryId, (x) => x.name),
                wert: k._count._all,
              }))
              .sort((a, b) => b.wert - a.wert)
              .slice(0, 10)}
          />

          <Verteilung
            titel="Offen je Bearbeitung"
            hinweis="Nur zugewiesene Vorgänge."
            eintraege={proBearbeiter
              .map((b) => ({
                label: nameVon(personen, b.assigneeId, (x) => x.name),
                wert: b._count._all,
              }))
              .sort((a, b) => b.wert - a.wert)}
          />

          <Verteilung
            titel="Nach Priorität"
            hinweis={`Gemeldet in den letzten ${tage} Tagen.`}
            eintraege={proPrioritaet
              .map((p) => ({
                label: PRIORITY[p.priority].label,
                wert: p._count._all,
              }))
              .sort((a, b) => b.wert - a.wert)}
          />
        </div>
      </div>
    </>
  );
}

function Kennzahl({
  label,
  wert,
  zusatz,
  icon: Icon,
  tone,
}: {
  label: string;
  wert: string;
  zusatz?: string;
  icon: React.ComponentType<{ className?: string; "aria-hidden"?: boolean }>;
  tone: "info" | "success" | "warning" | "danger" | "neutral";
}) {
  const toene = {
    info: "bg-info-soft text-info-soft-fg",
    success: "bg-success-soft text-success-soft-fg",
    warning: "bg-warning-soft text-warning-soft-fg",
    danger: "bg-danger-soft text-danger-soft-fg",
    neutral: "bg-surface-2 text-fg-muted",
  };

  return (
    <Card>
      <div className="flex items-start gap-3 px-4 py-4">
        <span
          className={cn(
            "flex size-10 shrink-0 items-center justify-center rounded-lg",
            toene[tone],
          )}
        >
          <Icon className="size-5" aria-hidden={true} />
        </span>
        <div className="min-w-0">
          <p className="text-2xl font-semibold tabular-nums text-fg">{wert}</p>
          <p className="text-xs text-fg-muted">{label}</p>
          {zusatz && <p className="text-xs text-fg-subtle">{zusatz}</p>}
        </div>
      </div>
    </Card>
  );
}

/**
 * Balkenliste.
 *
 * Jeder Balken trägt seine Zahl im Klartext daneben – die Länge allein
 * transportiert den Wert nicht, und beim Vergrößern der Schrift verschieben
 * sich Balken ohnehin.
 */
function Verteilung({
  titel,
  hinweis,
  eintraege,
}: {
  titel: string;
  hinweis?: string;
  eintraege: { label: string; wert: number; farbe?: string }[];
}) {
  const max = Math.max(1, ...eintraege.map((e) => e.wert));
  const summe = eintraege.reduce((s, e) => s + e.wert, 0);

  return (
    <Card>
      <div className="px-5 py-4">
        <h2 className="text-sm font-semibold text-fg">{titel}</h2>
        {hinweis && <p className="mt-0.5 text-xs text-fg-muted">{hinweis}</p>}

        {eintraege.length === 0 ? (
          <p className="py-6 text-center text-sm text-fg-subtle">
            Keine Daten im gewählten Zeitraum.
          </p>
        ) : (
          <ul className="mt-3 space-y-2">
            {eintraege.map((e) => (
              <li key={e.label}>
                <div className="flex items-baseline justify-between gap-3 text-sm">
                  <span className="min-w-0 truncate text-fg">{e.label}</span>
                  <span className="shrink-0 tabular-nums text-fg-muted">
                    {e.wert}
                    {summe > 0 && (
                      <span className="ml-1.5 text-xs text-fg-subtle">
                        {formatPercent(e.wert / summe)}
                      </span>
                    )}
                  </span>
                </div>
                <div
                  className="mt-1 h-1.5 overflow-hidden rounded-full bg-surface-3"
                  role="presentation"
                >
                  <div
                    className="h-full rounded-full"
                    style={{
                      width: `${(e.wert / max) * 100}%`,
                      backgroundColor: e.farbe ?? "var(--primary)",
                    }}
                  />
                </div>
              </li>
            ))}
          </ul>
        )}
      </div>
    </Card>
  );
}
