import { fromZonedTime, formatInTimeZone } from "date-fns-tz";

/**
 * Rechnen mit Dienstzeiten.
 *
 * Fristen laufen nur, während tatsächlich jemand da ist. Wird ein Vorgang
 * freitags um 15:50 gemeldet und die Frist beträgt vier Stunden, ist er nicht
 * um 19:50 überfällig, sondern am Montagvormittag. Ohne diese Rechnung wäre
 * am Montagmorgen alles rot und die Ampel damit wertlos.
 *
 * Alle Rechnungen laufen in der Zeitzone der Dienstzeit, damit die
 * Sommerzeitumstellung nicht zu Verschiebungen führt.
 */

/** Wochenplan: Schlüssel 1 = Montag … 7 = Sonntag, Werte sind "HH:mm"-Paare. */
export type WeeklySchedule = Record<string, [string, string][]>;

export interface BusinessHoursConfig {
  timezone: string;
  schedule: WeeklySchedule;
  /** Feiertage als "yyyy-MM-dd" in der Zeitzone der Dienstzeit. */
  holidays: Set<string>;
}

const MINUTE = 60_000;
const MAX_TAGE = 400;

/** Datum als "yyyy-MM-dd" in der Zielzeitzone. */
function tagesSchluessel(date: Date, timezone: string): string {
  return formatInTimeZone(date, timezone, "yyyy-MM-dd");
}

/** Wochentag 1 (Montag) bis 7 (Sonntag) in der Zielzeitzone. */
function wochentag(date: Date, timezone: string): number {
  // "i" liefert 1 = Montag … 7 = Sonntag.
  return Number(formatInTimeZone(date, timezone, "i"));
}

/**
 * Die Zeitfenster eines Kalendertags als echte Zeitpunkte.
 * Leer an Wochenenden und Feiertagen.
 */
function fensterFuerTag(
  tagKey: string,
  config: BusinessHoursConfig,
  referenz: Date,
): { von: Date; bis: Date }[] {
  if (config.holidays.has(tagKey)) return [];

  const tag = wochentag(referenz, config.timezone);
  const eintraege = config.schedule[String(tag)] ?? [];

  return eintraege
    .map(([von, bis]) => ({
      von: fromZonedTime(`${tagKey} ${von}`, config.timezone),
      bis: fromZonedTime(`${tagKey} ${bis}`, config.timezone),
    }))
    .filter((f) => f.bis > f.von)
    .sort((a, b) => a.von.getTime() - b.von.getTime());
}

/**
 * Verschiebt einen Zeitpunkt auf den Beginn des nächsten Kalendertags.
 *
 * Bewusst über die Datumsanteile und nicht über „plus 24 Stunden": An der
 * Umstellung auf Winterzeit hat der Tag 25 Stunden. Wer 24 Stunden addiert,
 * landet dann wieder auf demselben Kalendertag – die Schleife käme nicht
 * voran und die Frist würde falsch berechnet. Das fiele nur einmal im Jahr
 * auf und wäre dann kaum zu finden.
 */
function naechsterTag(date: Date, timezone: string): Date {
  const [jahr, monat, tag] = tagesSchluessel(date, timezone)
    .split("-")
    .map(Number);

  // UTC-Arithmetik auf den reinen Datumsanteilen: erledigt Monats- und
  // Jahreswechsel und kennt keine Zeitumstellung.
  const naechster = new Date(Date.UTC(jahr, monat - 1, tag + 1));
  const key = formatInTimeZone(naechster, "UTC", "yyyy-MM-dd");

  return fromZonedTime(`${key} 00:00`, timezone);
}

/**
 * Addiert Minuten reiner Dienstzeit auf einen Zeitpunkt.
 *
 * @returns Der Zeitpunkt, zu dem die Frist abläuft.
 */
export function addBusinessMinutes(
  start: Date,
  minutes: number,
  config: BusinessHoursConfig,
): Date {
  if (minutes <= 0) return new Date(start);

  // Ohne jede Dienstzeit im Plan gäbe es eine Endlosschleife – dann wird die
  // Frist als reine Kalenderzeit gerechnet, damit trotzdem ein Wert entsteht.
  if (!hatDienstzeit(config)) {
    return new Date(start.getTime() + minutes * MINUTE);
  }

  let rest = minutes;
  let cursor = new Date(start);

  for (let i = 0; i < MAX_TAGE; i++) {
    const key = tagesSchluessel(cursor, config.timezone);
    const fenster = fensterFuerTag(key, config, cursor);

    for (const f of fenster) {
      if (f.bis <= cursor) continue;

      const beginn = cursor > f.von ? cursor : f.von;
      const verfuegbar = (f.bis.getTime() - beginn.getTime()) / MINUTE;

      if (verfuegbar >= rest) {
        return new Date(beginn.getTime() + rest * MINUTE);
      }
      rest -= verfuegbar;
    }

    cursor = naechsterTag(cursor, config.timezone);
  }

  // Sollte bei sinnvollen Fristen nie erreicht werden.
  return cursor;
}

/** Wie viele Minuten Dienstzeit liegen zwischen zwei Zeitpunkten? */
export function businessMinutesBetween(
  von: Date,
  bis: Date,
  config: BusinessHoursConfig,
): number {
  if (bis <= von) return 0;
  if (!hatDienstzeit(config)) {
    return (bis.getTime() - von.getTime()) / MINUTE;
  }

  let summe = 0;
  let cursor = new Date(von);

  for (let i = 0; i < MAX_TAGE && cursor < bis; i++) {
    const key = tagesSchluessel(cursor, config.timezone);

    for (const f of fensterFuerTag(key, config, cursor)) {
      const beginn = cursor > f.von ? cursor : f.von;
      const ende = bis < f.bis ? bis : f.bis;
      if (ende > beginn) {
        summe += (ende.getTime() - beginn.getTime()) / MINUTE;
      }
    }

    cursor = naechsterTag(cursor, config.timezone);
  }

  return summe;
}

/** Liegt der Zeitpunkt innerhalb der Dienstzeit? */
export function istInDienstzeit(date: Date, config: BusinessHoursConfig): boolean {
  const key = tagesSchluessel(date, config.timezone);
  return fensterFuerTag(key, config, date).some(
    (f) => date >= f.von && date < f.bis,
  );
}

function hatDienstzeit(config: BusinessHoursConfig): boolean {
  return Object.values(config.schedule).some((tage) => tage.length > 0);
}

/**
 * Baut die Konfiguration aus den Datensätzen der Datenbank.
 * Die Feiertage kommen als Datumswerte und werden auf Tagesschlüssel gebracht.
 */
export function toBusinessHoursConfig(input: {
  timezone: string;
  schedule: unknown;
  holidays: { date: Date }[];
}): BusinessHoursConfig {
  const schedule = (input.schedule ?? {}) as WeeklySchedule;

  return {
    timezone: input.timezone || "Europe/Berlin",
    schedule,
    holidays: new Set(
      input.holidays.map((h) =>
        // Feiertage sind als reines Datum gespeichert (UTC-Mitternacht); der
        // Tagesschlüssel muss deshalb ebenfalls in UTC gebildet werden, sonst
        // rutscht der 1. Mai in Zeitzonen östlich von UTC auf den 30. April.
        formatInTimeZone(h.date, "UTC", "yyyy-MM-dd"),
      ),
    ),
  };
}
