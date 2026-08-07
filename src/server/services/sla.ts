import { EventType, Priority, TeamMode, type Prisma } from "@prisma/client";

import { db } from "@/server/db";
import { logEvent } from "@/server/services/events";
import {
  addBusinessMinutes,
  toBusinessHoursConfig,
  type BusinessHoursConfig,
} from "@/lib/business-hours";
import { STATUS_ERLEDIGT } from "@/lib/domain";

/**
 * Fristen.
 *
 * Nur Bereiche der vollen Ausbaustufe haben Fristen. Für die schlanken
 * Bereiche werden bewusst keine gesetzt: Eine Ampel, hinter der keine
 * Vereinbarung steht, erzeugt nur schlechtes Gewissen ohne Nutzen.
 */

export interface FristenErgebnis {
  slaPolicyId: string | null;
  firstResponseDue: Date | null;
  resolutionDue: Date | null;
}

const LEER: FristenErgebnis = {
  slaPolicyId: null,
  firstResponseDue: null,
  resolutionDue: null,
};

/**
 * Ermittelt die Fristen für einen Vorgang.
 *
 * Vorrang hat die Frist der Kategorie; gibt es dort keine, greift die des
 * Bereichs für diese Priorität.
 */
export async function berechneFristen(opts: {
  tx: Prisma.TransactionClient | typeof db;
  teamId: string;
  teamMode: TeamMode;
  priority: Priority;
  categorySlaPolicyId?: string | null;
  ab: Date;
}): Promise<FristenErgebnis> {
  if (opts.teamMode !== TeamMode.VOLL) return LEER;

  const policy = opts.categorySlaPolicyId
    ? await opts.tx.slaPolicy.findFirst({
        where: { id: opts.categorySlaPolicyId, isActive: true },
        include: { businessHours: { include: { holidays: true } } },
      })
    : null;

  const wirksam =
    policy ??
    (await opts.tx.slaPolicy.findFirst({
      where: { teamId: opts.teamId, priority: opts.priority, isActive: true },
      include: { businessHours: { include: { holidays: true } } },
    }));

  if (!wirksam) return LEER;

  const config = toBusinessHoursConfig({
    timezone: wirksam.businessHours.timezone,
    schedule: wirksam.businessHours.schedule,
    holidays: wirksam.businessHours.holidays,
  });

  return {
    slaPolicyId: wirksam.id,
    firstResponseDue: addBusinessMinutes(
      opts.ab,
      wirksam.firstResponseMinutes,
      config,
    ),
    resolutionDue: addBusinessMinutes(opts.ab, wirksam.resolutionMinutes, config),
  };
}

/**
 * Prüft alle offenen Vorgänge auf drohende und überschrittene Fristen.
 *
 * Läuft im Hintergrunddienst. Warnungen werden nur einmal je Vorgang
 * ausgelöst (`slaWarningNotified`), sonst käme bei jedem Durchlauf eine neue
 * Meldung und niemand würde die Hinweise mehr lesen.
 *
 * @returns Zahl der neu gewarnten und neu überschrittenen Vorgänge.
 */
export async function pruefeFristen(jetzt: Date = new Date()) {
  const offene = await db.ticket.findMany({
    where: {
      status: { notIn: STATUS_ERLEDIGT },
      slaPolicyId: { not: null },
      OR: [{ slaBreached: false }, { slaWarningNotified: false }],
    },
    select: {
      id: true,
      number: true,
      subject: true,
      createdAt: true,
      resolutionDue: true,
      slaBreached: true,
      slaWarningNotified: true,
      assigneeId: true,
      teamId: true,
      slaPolicy: { select: { warningThreshold: true } },
    },
    take: 2000,
  });

  let gewarnt = 0;
  let ueberschritten = 0;

  // Bereichsleitungen einmal vorab laden statt je Vorgang.
  const leitungen = new Map<string, string[]>();

  for (const t of offene) {
    if (!t.resolutionDue) continue;

    const istUeberschritten = jetzt > t.resolutionDue;
    const gesamt = t.resolutionDue.getTime() - t.createdAt.getTime();
    const anteil =
      gesamt > 0 ? (jetzt.getTime() - t.createdAt.getTime()) / gesamt : 1;
    const schwelle = t.slaPolicy?.warningThreshold ?? 0.8;

    if (istUeberschritten && !t.slaBreached) {
      await db.ticket.update({
        where: { id: t.id },
        data: { slaBreached: true, slaWarningNotified: true },
      });
      await logEvent(db, {
        ticketId: t.id,
        type: EventType.SLA_BREACHED,
        actorName: "System",
        note: `Die Lösungsfrist war am ${t.resolutionDue.toLocaleString("de-DE")} erreicht.`,
      });

      if (!leitungen.has(t.teamId)) {
        const mitglieder = await db.teamMembership.findMany({
          where: { teamId: t.teamId, role: "LEAD" },
          select: { userId: true },
        });
        leitungen.set(t.teamId, mitglieder.map((m) => m.userId));
      }

      const empfaenger = new Set(
        [t.assigneeId, ...(leitungen.get(t.teamId) ?? [])].filter(
          (id): id is string => Boolean(id),
        ),
      );

      for (const userId of empfaenger) {
        await db.notification.create({
          data: {
            userId,
            ticketId: t.id,
            type: "SLA_BREACHED",
            title: `Frist überschritten: ${t.number}`,
            body: t.subject,
          },
        });
      }

      ueberschritten++;
      continue;
    }

    if (!istUeberschritten && anteil >= schwelle && !t.slaWarningNotified) {
      await db.ticket.update({
        where: { id: t.id },
        data: { slaWarningNotified: true },
      });
      await logEvent(db, {
        ticketId: t.id,
        type: EventType.SLA_WARNING,
        actorName: "System",
      });

      if (t.assigneeId) {
        await db.notification.create({
          data: {
            userId: t.assigneeId,
            ticketId: t.id,
            type: "SLA_WARNING",
            title: `Frist läuft ab: ${t.number}`,
            body: t.subject,
          },
        });
      }

      gewarnt++;
    }
  }

  return { gewarnt, ueberschritten, geprueft: offene.length };
}

/** Lädt die Standard-Dienstzeit, etwa für Anzeigen in der Verwaltung. */
export async function standardDienstzeit(): Promise<BusinessHoursConfig | null> {
  const bh = await db.businessHours.findFirst({
    where: { isDefault: true },
    include: { holidays: true },
  });
  if (!bh) return null;

  return toBusinessHoursConfig({
    timezone: bh.timezone,
    schedule: bh.schedule,
    holidays: bh.holidays,
  });
}
