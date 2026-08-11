/**
 * Datums- und Zahlenformate.
 *
 * Durchgehend deutsche Schreibweise und die Zeitzone aus der Konfiguration,
 * damit auf Server und Client dieselbe Uhrzeit erscheint – sonst zeigt eine
 * serverseitig gerenderte Seite eine andere Zeit als dieselbe Seite nach dem
 * Aktualisieren im Browser.
 */

const ZEITZONE = process.env.TZ || "Europe/Berlin";

const datumKurz = new Intl.DateTimeFormat("de-DE", {
  day: "2-digit",
  month: "2-digit",
  year: "numeric",
  timeZone: ZEITZONE,
});

const datumLang = new Intl.DateTimeFormat("de-DE", {
  day: "numeric",
  month: "long",
  year: "numeric",
  timeZone: ZEITZONE,
});

const datumZeit = new Intl.DateTimeFormat("de-DE", {
  day: "2-digit",
  month: "2-digit",
  year: "numeric",
  hour: "2-digit",
  minute: "2-digit",
  timeZone: ZEITZONE,
});

const nurZeit = new Intl.DateTimeFormat("de-DE", {
  hour: "2-digit",
  minute: "2-digit",
  timeZone: ZEITZONE,
});

const wochentagZeit = new Intl.DateTimeFormat("de-DE", {
  weekday: "short",
  day: "2-digit",
  month: "2-digit",
  hour: "2-digit",
  minute: "2-digit",
  timeZone: ZEITZONE,
});

export function formatDate(date: Date | string): string {
  return datumKurz.format(new Date(date));
}

export function formatDateLong(date: Date | string): string {
  return datumLang.format(new Date(date));
}

export function formatDateTime(date: Date | string): string {
  return datumZeit.format(new Date(date)) + " Uhr";
}

export function formatTime(date: Date | string): string {
  return nurZeit.format(new Date(date)) + " Uhr";
}

/**
 * Kompakte Angabe für Tabellen: heute nur die Uhrzeit, in dieser Woche
 * Wochentag und Uhrzeit, sonst das Datum.
 */
export function formatCompact(date: Date | string): string {
  const d = new Date(date);
  const jetzt = new Date();
  const diff = jetzt.getTime() - d.getTime();

  const gleicherTag =
    d.getDate() === jetzt.getDate() &&
    d.getMonth() === jetzt.getMonth() &&
    d.getFullYear() === jetzt.getFullYear();

  if (gleicherTag) return nurZeit.format(d) + " Uhr";
  if (diff > 0 && diff < 6 * 24 * 60 * 60 * 1000) return wochentagZeit.format(d);
  return datumKurz.format(d);
}

/**
 * Zeitabstand in Worten: „vor 3 Stunden", „in 2 Tagen".
 *
 * Bewusst grob – auf die Minute genau interessiert bei einem Ticket niemanden,
 * und gerundete Angaben lesen sich schneller.
 */
export function formatRelative(date: Date | string, now: Date = new Date()): string {
  const d = new Date(date);
  const diffMs = d.getTime() - now.getTime();
  const zukunft = diffMs > 0;
  const abs = Math.abs(diffMs);

  const minute = 60 * 1000;
  const stunde = 60 * minute;
  const tag = 24 * stunde;
  const woche = 7 * tag;
  const monat = 30 * tag;

  const wrap = (text: string) => (zukunft ? `in ${text}` : `vor ${text}`);

  if (abs < minute) return zukunft ? "gleich" : "gerade eben";
  if (abs < stunde) {
    const n = Math.round(abs / minute);
    return wrap(n === 1 ? "einer Minute" : `${n} Minuten`);
  }
  if (abs < tag) {
    const n = Math.round(abs / stunde);
    return wrap(n === 1 ? "einer Stunde" : `${n} Stunden`);
  }
  if (abs < woche) {
    const n = Math.round(abs / tag);
    return wrap(n === 1 ? "einem Tag" : `${n} Tagen`);
  }
  if (abs < monat) {
    const n = Math.round(abs / woche);
    return wrap(n === 1 ? "einer Woche" : `${n} Wochen`);
  }
  const n = Math.round(abs / monat);
  return wrap(n === 1 ? "einem Monat" : `${n} Monaten`);
}

/** Dauer in Minuten als lesbare Angabe: 90 → „1 Std. 30 Min." */
export function formatDuration(minutes: number): string {
  if (minutes < 1) return "unter einer Minute";
  if (minutes < 60) return `${Math.round(minutes)} Min.`;

  const stunden = Math.floor(minutes / 60);
  const rest = Math.round(minutes % 60);

  if (stunden < 24) {
    return rest > 0 ? `${stunden} Std. ${rest} Min.` : `${stunden} Std.`;
  }

  const tage = Math.floor(stunden / 24);
  const restStunden = stunden % 24;
  return restStunden > 0 ? `${tage} Tg. ${restStunden} Std.` : `${tage} Tg.`;
}

/** Dateigröße: 2411724 → „2,3 MB" */
export function formatBytes(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  const einheiten = ["KB", "MB", "GB"];
  let wert = bytes / 1024;
  let i = 0;
  while (wert >= 1024 && i < einheiten.length - 1) {
    wert /= 1024;
    i++;
  }
  return `${wert.toLocaleString("de-DE", { maximumFractionDigits: 1 })} ${einheiten[i]}`;
}

/** Prozentwert: 0.874 → „87 %" */
export function formatPercent(value: number, digits = 0): string {
  return `${(value * 100).toLocaleString("de-DE", {
    minimumFractionDigits: digits,
    maximumFractionDigits: digits,
  })} %`;
}

export function formatNumber(value: number): string {
  return value.toLocaleString("de-DE");
}

/** Für <time dateTime="…"> – maschinenlesbar. */
export function isoDate(date: Date | string): string {
  return new Date(date).toISOString();
}
