import {
  AssetType,
  EventType,
  Impact,
  Priority,
  SiteType,
  TicketSource,
  TicketStatus,
  Urgency,
} from "@prisma/client";
import {
  AlertTriangle,
  ArrowDown,
  ArrowRight,
  ArrowUp,
  ArrowUpRight,
  CheckCircle2,
  CircleDashed,
  CircleDot,
  Clock,
  Flame,
  Hourglass,
  Lock,
  PauseCircle,
  type LucideIcon,
} from "lucide-react";

/**
 * Beschriftungen und Symbole für die Fachbegriffe.
 *
 * Alles an einer Stelle, damit "WARTET_AUF_MELDER" im ganzen System gleich
 * heißt. Jeder Eintrag hat ein Symbol **und** einen Text: Farbe allein
 * transportiert die Bedeutung nicht (WCAG 1.4.1), und wer die Oberfläche
 * vergrößert oder mit hohem Kontrast nutzt, soll sie ebenso schnell erfassen.
 */

export type Tone = "neutral" | "primary" | "success" | "warning" | "danger" | "info";

export interface Descriptor {
  label: string;
  /** Kurzform für enge Tabellen. */
  short?: string;
  icon: LucideIcon;
  tone: Tone;
  description?: string;
}

// --- Status ----------------------------------------------------------------

export const STATUS: Record<TicketStatus, Descriptor> = {
  NEU: {
    label: "Neu",
    icon: CircleDashed,
    tone: "info",
    description: "Noch nicht angesehen.",
  },
  OFFEN: {
    label: "Offen",
    icon: CircleDot,
    tone: "info",
    description: "Angenommen, Bearbeitung steht noch aus.",
  },
  IN_BEARBEITUNG: {
    label: "In Bearbeitung",
    short: "In Arbeit",
    icon: Clock,
    tone: "primary",
    description: "Wird gerade bearbeitet.",
  },
  WARTET_AUF_MELDER: {
    label: "Wartet auf Rückmeldung",
    short: "Wartet",
    icon: Hourglass,
    tone: "warning",
    description: "Wir haben nachgefragt und warten auf eine Antwort.",
  },
  WARTET_AUF_DRITTE: {
    label: "Wartet auf Dritte",
    short: "Extern",
    icon: Hourglass,
    tone: "warning",
    description: "Wartet auf eine Fremdfirma oder Lieferung.",
  },
  ZURUECKGESTELLT: {
    label: "Zurückgestellt",
    short: "Ruht",
    icon: PauseCircle,
    tone: "neutral",
    description: "Ruht vorerst, etwa bis ein Teilvorgang erledigt ist.",
  },
  GELOEST: {
    label: "Gelöst",
    icon: CheckCircle2,
    tone: "success",
    description: "Erledigt. Der Melder kann noch einmal nachfassen.",
  },
  GESCHLOSSEN: {
    label: "Geschlossen",
    short: "Zu",
    icon: Lock,
    tone: "neutral",
    description: "Abgeschlossen und abgelegt.",
  },
};

/** Beschriftungen in der schlanken Ausbaustufe – bewusst weniger Fachsprache. */
export const STATUS_EINFACH: Partial<Record<TicketStatus, string>> = {
  NEU: "Neu",
  OFFEN: "Offen",
  IN_BEARBEITUNG: "In Arbeit",
  WARTET_AUF_MELDER: "Wartet auf Antwort",
  WARTET_AUF_DRITTE: "Wartet",
  ZURUECKGESTELLT: "Ruht",
  GELOEST: "Erledigt",
  GESCHLOSSEN: "Erledigt",
};

/** Status, die als abgeschlossen gelten. */
export const STATUS_ERLEDIGT: TicketStatus[] = [
  TicketStatus.GELOEST,
  TicketStatus.GESCHLOSSEN,
];

/** Status, die als offen gelten – die Standardansicht jeder Liste. */
export const STATUS_OFFEN: TicketStatus[] = [
  TicketStatus.NEU,
  TicketStatus.OFFEN,
  TicketStatus.IN_BEARBEITUNG,
  TicketStatus.WARTET_AUF_MELDER,
  TicketStatus.WARTET_AUF_DRITTE,
  TicketStatus.ZURUECKGESTELLT,
];

export function isErledigt(status: TicketStatus): boolean {
  return STATUS_ERLEDIGT.includes(status);
}

// --- Priorität -------------------------------------------------------------

export const PRIORITY: Record<Priority, Descriptor> = {
  NIEDRIG: {
    label: "Niedrig",
    icon: ArrowDown,
    tone: "neutral",
    description: "Kann warten.",
  },
  NORMAL: {
    label: "Normal",
    icon: ArrowRight,
    tone: "info",
    description: "Übliche Bearbeitungszeit.",
  },
  HOCH: {
    label: "Hoch",
    icon: ArrowUp,
    tone: "warning",
    description: "Behindert die Arbeit spürbar.",
  },
  DRINGEND: {
    label: "Dringend",
    icon: Flame,
    tone: "danger",
    description: "Arbeit steht still oder es besteht ein Risiko.",
  },
};

export const PRIORITY_ORDER: Priority[] = [
  Priority.DRINGEND,
  Priority.HOCH,
  Priority.NORMAL,
  Priority.NIEDRIG,
];

// --- Auswirkung und Dringlichkeit -----------------------------------------

export const IMPACT: Record<Impact, { label: string; description: string }> = {
  EINZELNE_PERSON: {
    label: "Eine Person",
    description: "Betrifft nur eine Person.",
  },
  GRUPPE: {
    label: "Eine Gruppe",
    description: "Betrifft ein Team, eine Wohngruppe oder eine Klasse.",
  },
  STANDORT: {
    label: "Ein ganzer Standort",
    description: "Betrifft ein komplettes Haus.",
  },
  GESAMTES_UNTERNEHMEN: {
    label: "Alle Standorte",
    description: "Betrifft das gesamte Unternehmen.",
  },
};

export const URGENCY: Record<Urgency, { label: string; description: string }> = {
  KANN_WARTEN: {
    label: "Kann warten",
    description: "Es gibt keinen Zeitdruck.",
  },
  NORMAL: {
    label: "Normal",
    description: "Sollte in den nächsten Tagen erledigt werden.",
  },
  BEEINTRAECHTIGT_ARBEIT: {
    label: "Behindert die Arbeit",
    description: "Wir kommen nur mit Umwegen weiter.",
  },
  ARBEIT_STEHT_STILL: {
    label: "Arbeit steht still",
    description: "Ohne Lösung geht es gar nicht weiter.",
  },
};

/**
 * Leitet aus Auswirkung und Dringlichkeit einen Prioritätsvorschlag ab.
 *
 * Das ist ausdrücklich nur ein Vorschlag: Wer den Vorgang bearbeitet, kennt
 * den Fall besser als eine Matrix und darf die Priorität immer überschreiben.
 */
export function suggestPriority(impact?: Impact | null, urgency?: Urgency | null): Priority {
  if (!impact || !urgency) return Priority.NORMAL;

  const impactScore: Record<Impact, number> = {
    EINZELNE_PERSON: 1,
    GRUPPE: 2,
    STANDORT: 3,
    GESAMTES_UNTERNEHMEN: 4,
  };
  const urgencyScore: Record<Urgency, number> = {
    KANN_WARTEN: 1,
    NORMAL: 2,
    BEEINTRAECHTIGT_ARBEIT: 3,
    ARBEIT_STEHT_STILL: 4,
  };

  const score = impactScore[impact] + urgencyScore[urgency];
  if (score >= 7) return Priority.DRINGEND;
  if (score >= 5) return Priority.HOCH;
  if (score >= 3) return Priority.NORMAL;
  return Priority.NIEDRIG;
}

// --- Fristenampel ----------------------------------------------------------

export type SlaState = "ok" | "warnung" | "ueberschritten" | "keine";

export const SLA: Record<SlaState, Descriptor> = {
  ok: { label: "Frist eingehalten", short: "Im Plan", icon: CheckCircle2, tone: "success" },
  warnung: { label: "Frist läuft ab", short: "Bald fällig", icon: Clock, tone: "warning" },
  ueberschritten: {
    label: "Frist überschritten",
    short: "Überfällig",
    icon: AlertTriangle,
    tone: "danger",
  },
  keine: { label: "Keine Frist", short: "—", icon: CircleDashed, tone: "neutral" },
};

/**
 * Zustand der Frist zu einem Zeitpunkt.
 *
 * `warningThreshold` gibt an, ab welchem Anteil der Gesamtfrist gewarnt wird
 * (0.8 = bei 80 % der Zeit).
 */
export function slaState(opts: {
  due: Date | null | undefined;
  start: Date;
  now?: Date;
  warningThreshold?: number;
  erledigt?: boolean;
}): SlaState {
  const { due, start, erledigt } = opts;
  if (!due || erledigt) return "keine";

  const now = opts.now ?? new Date();
  if (now > due) return "ueberschritten";

  const threshold = opts.warningThreshold ?? 0.8;
  const gesamt = due.getTime() - start.getTime();
  if (gesamt <= 0) return "ueberschritten";

  const verbraucht = (now.getTime() - start.getTime()) / gesamt;
  return verbraucht >= threshold ? "warnung" : "ok";
}

// --- Herkunft --------------------------------------------------------------

export const SOURCE: Record<TicketSource, string> = {
  WEB: "Über das Portal gemeldet",
  EMAIL: "Per E-Mail eingegangen",
  TELEFON: "Telefonisch aufgenommen",
  API: "Über eine Schnittstelle angelegt",
  IMPORT: "Aus dem Altsystem übernommen",
};

// --- Standorte -------------------------------------------------------------

export const SITE_TYPE: Record<SiteType, string> = {
  SCHULE: "Schule",
  WOHNHEIM: "Wohnheim",
  BETREUTES_WOHNEN: "Betreutes Wohnen",
  VERWALTUNG: "Verwaltung",
  WERKSTATT: "Werkstatt",
  SONSTIGE: "Sonstiger Standort",
};

// --- Geräte ----------------------------------------------------------------

export const ASSET_TYPE: Record<AssetType, string> = {
  PC: "PC",
  NOTEBOOK: "Notebook",
  TABLET: "Tablet",
  DRUCKER: "Drucker",
  TELEFON: "Telefon",
  MONITOR: "Bildschirm",
  NETZWERK: "Netzwerkgerät",
  SOFTWARE_LIZENZ: "Softwarelizenz",
  SONSTIGES: "Sonstiges",
};

// --- Ereignisse im Verlauf -------------------------------------------------

/**
 * Formuliert einen Protokolleintrag als lesbaren Satz.
 * Der Name der handelnden Person wird vom Aufrufer davorgesetzt.
 */
export function describeEvent(event: {
  type: EventType;
  fromValue?: string | null;
  toValue?: string | null;
  note?: string | null;
}): string {
  const { type, fromValue, toValue } = event;

  switch (type) {
    case "CREATED":
      return "hat den Vorgang gemeldet";
    case "CLAIMED":
      return "hat den Vorgang übernommen";
    case "ASSIGNED":
      return toValue ? `hat den Vorgang an ${toValue} übergeben` : "hat den Vorgang zugewiesen";
    case "UNASSIGNED":
      return "hat die Zuweisung aufgehoben";
    case "TRANSFERRED":
      return fromValue && toValue
        ? `hat den Vorgang von ${fromValue} an ${toValue} weitergeleitet`
        : "hat den Vorgang weitergeleitet";
    case "SPLIT":
      return toValue
        ? `hat den Teilvorgang ${toValue} abgespalten`
        : fromValue
          ? `hat diesen Vorgang aus ${fromValue} abgespalten`
          : "hat einen Teilvorgang abgespalten";
    case "SPLIT_CHILD_RESOLVED":
      return fromValue
        ? `– der Teilvorgang ${fromValue} ist erledigt, dieser Vorgang läuft weiter`
        : "– der Teilvorgang ist erledigt";
    case "MERGED":
      return toValue ? `hat den Vorgang mit ${toValue} zusammengeführt` : "hat Vorgänge zusammengeführt";
    case "STATUS_CHANGED": {
      const bis = toValue ? STATUS[toValue as TicketStatus]?.label ?? toValue : "";
      const von = fromValue ? STATUS[fromValue as TicketStatus]?.label ?? fromValue : "";
      if (von && bis) return `hat den Status von „${von}" auf „${bis}" geändert`;
      return bis ? `hat den Status auf „${bis}" gesetzt` : "hat den Status geändert";
    }
    case "PRIORITY_CHANGED": {
      const bis = toValue ? PRIORITY[toValue as Priority]?.label ?? toValue : "";
      return bis ? `hat die Priorität auf „${bis}" gesetzt` : "hat die Priorität geändert";
    }
    case "CATEGORY_CHANGED":
      return toValue ? `hat die Kategorie auf „${toValue}" gesetzt` : "hat die Kategorie geändert";
    case "SITE_CHANGED":
      return toValue ? `hat den Standort auf „${toValue}" geändert` : "hat den Standort geändert";
    case "DUE_DATE_SET":
      return "hat eine Frist gesetzt";
    case "SLA_WARNING":
      return "– die Frist läuft bald ab";
    case "SLA_BREACHED":
      return "– die Frist wurde überschritten";
    case "FIRST_RESPONSE":
      return "hat zum ersten Mal geantwortet";
    case "REOPENED":
      return "hat den Vorgang wieder geöffnet";
    case "WATCHER_ADDED":
      return toValue ? `hat ${toValue} auf Kopie gesetzt` : "hat eine Person auf Kopie gesetzt";
    case "WATCHER_REMOVED":
      return toValue ? `hat ${toValue} von der Kopie genommen` : "hat eine Person von der Kopie genommen";
    case "ATTACHMENT_ADDED":
      return toValue ? `hat die Datei „${toValue}" angehängt` : "hat eine Datei angehängt";
    case "EMAIL_RECEIVED":
      return "– eine E-Mail ist eingegangen";
    case "EMAIL_SENT":
      return "– eine E-Mail wurde versendet";
    case "EMAIL_COMMAND":
      return toValue ? `hat per E-Mail den Befehl „${toValue}" ausgeführt` : "hat per E-Mail einen Befehl ausgeführt";
    case "EMAIL_COMMAND_REJECTED":
      return "– ein Befehl aus einer E-Mail wurde abgewiesen";
    case "IMPORTED":
      return "– aus dem Altsystem übernommen";
    default:
      return "hat den Vorgang geändert";
  }
}

/** Ereignisse, die im Verlauf hervorgehoben werden. */
export const WICHTIGE_EREIGNISSE: EventType[] = [
  EventType.SPLIT,
  EventType.TRANSFERRED,
  EventType.MERGED,
  EventType.SLA_BREACHED,
  EventType.SPLIT_CHILD_RESOLVED,
  EventType.EMAIL_COMMAND_REJECTED,
];

export const EVENT_ICON: Partial<Record<EventType, LucideIcon>> = {
  SPLIT: ArrowUpRight,
  TRANSFERRED: ArrowRight,
  SLA_BREACHED: AlertTriangle,
  SLA_WARNING: Clock,
  EMAIL_COMMAND_REJECTED: AlertTriangle,
};
