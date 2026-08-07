import { TeamMode } from "@prisma/client";

/**
 * Was kann ein Bereich in seiner Ausbaustufe?
 *
 * Diese Datei ist die einzige Stelle, an der aus `Team.mode` abgeleitet wird,
 * welche Funktionen zur Verfügung stehen. Oberfläche und Server-Aktionen
 * fragen beide hier an – sonst würden Anzeige und tatsächliche Berechtigung
 * mit der Zeit auseinanderlaufen, und ein ausgeblendeter Knopf wäre kein
 * Schutz, sondern nur eine Höflichkeit.
 */
export interface TeamCapabilities {
  /** Sammeleingang mit nicht zugewiesenen Tickets zum Selbstziehen. */
  sammeleingang: boolean;
  /** Tickets sich selbst zuweisen oder Kolleg:innen zuteilen. */
  zuweisung: boolean;
  /** Automatische Verteilung (Round-Robin). */
  autoVerteilung: boolean;
  /** Fristen, Ampel und Fristüberwachung. */
  sla: boolean;
  /** Prioritätsfeld inklusive Auswirkung und Dringlichkeit. */
  prioritaet: boolean;
  /** Auswertung, Kennzahlen, Export. */
  statistik: boolean;
  /** Kategorien mit Zusatzfeldern im Meldeformular. */
  kategorien: boolean;
  /** Textbausteine und Ticketvorlagen. */
  vorlagen: boolean;
  /** Schlagworte. */
  tags: boolean;
  /** Checklisten im Ticket. */
  checklisten: boolean;
  /** Geräteverknüpfung (leichtes Inventar). */
  geraete: boolean;
  /** Tickets zusammenführen. */
  zusammenfuehren: boolean;
  /** Gespeicherte Filteransichten. */
  gespeicherteAnsichten: boolean;
  /** Voller Statussatz statt nur offen/wartet/erledigt. */
  detaillierteStatus: boolean;

  // In beiden Ausbaustufen vorhanden – hier aufgeführt, damit die
  // Funktionsliste vollständig ist und nichts implizit bleibt.
  /** Tickets empfangen, lesen, beantworten, abschließen. */
  bearbeiten: boolean;
  /** Interne Notizen, die den Melder nie erreichen. */
  interneNotizen: boolean;
  /** Ticket an einen anderen Bereich weiterleiten. */
  weiterleiten: boolean;
  /** Teilvorgang für einen anderen Bereich abspalten. */
  abspalten: boolean;
  /** Bearbeitung per Antwortmail inklusive Steuerbefehlen. */
  emailBearbeitung: boolean;
}

const VOLL: TeamCapabilities = {
  sammeleingang: true,
  zuweisung: true,
  autoVerteilung: true,
  sla: true,
  prioritaet: true,
  statistik: true,
  kategorien: true,
  vorlagen: true,
  tags: true,
  checklisten: true,
  geraete: true,
  zusammenfuehren: true,
  gespeicherteAnsichten: true,
  detaillierteStatus: true,
  bearbeiten: true,
  interneNotizen: true,
  weiterleiten: true,
  abspalten: true,
  emailBearbeitung: true,
};

const EINFACH: TeamCapabilities = {
  sammeleingang: false,
  zuweisung: false,
  autoVerteilung: false,
  sla: false,
  prioritaet: false,
  statistik: false,
  kategorien: false,
  vorlagen: false,
  tags: false,
  checklisten: false,
  geraete: false,
  zusammenfuehren: false,
  gespeicherteAnsichten: false,
  detaillierteStatus: false,
  bearbeiten: true,
  interneNotizen: true,
  weiterleiten: true,
  abspalten: true,
  emailBearbeitung: true,
};

/** Liefert die Funktionsliste zu einer Ausbaustufe. */
export function capabilitiesFor(mode: TeamMode): TeamCapabilities {
  return mode === TeamMode.VOLL ? VOLL : EINFACH;
}

/**
 * Wirft, wenn ein Bereich eine Funktion nicht hat. In Server-Aktionen
 * aufzurufen, bevor die Funktion ausgeführt wird.
 */
export function requireCapability(
  mode: TeamMode,
  capability: keyof TeamCapabilities,
): void {
  if (!capabilitiesFor(mode)[capability]) {
    throw new Error(
      `Diese Funktion (${capability}) steht in einem Bereich der Ausbaustufe ` +
        `"${mode}" nicht zur Verfügung.`,
    );
  }
}

/**
 * Auf welche Status lässt sich in dieser Ausbaustufe umschalten?
 *
 * Der Einfach-Modus kennt nur drei Zustände. Die feineren Status bleiben im
 * Datenmodell erhalten – ein Ticket, das aus einem Voll-Bereich weitergeleitet
 * wurde, behält also seinen Status, und ein späterer Wechsel der Ausbaustufe
 * braucht keine Datenwanderung.
 */
export const STATUS_AUSWAHL_EINFACH = [
  "OFFEN",
  "WARTET_AUF_MELDER",
  "GELOEST",
] as const;

export const STATUS_AUSWAHL_VOLL = [
  "NEU",
  "OFFEN",
  "IN_BEARBEITUNG",
  "WARTET_AUF_MELDER",
  "WARTET_AUF_DRITTE",
  "ZURUECKGESTELLT",
  "GELOEST",
  "GESCHLOSSEN",
] as const;

export function statusAuswahlFor(mode: TeamMode): readonly string[] {
  return mode === TeamMode.VOLL ? STATUS_AUSWAHL_VOLL : STATUS_AUSWAHL_EINFACH;
}
