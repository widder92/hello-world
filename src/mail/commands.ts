import { TicketStatus } from "@prisma/client";

import { db } from "@/server/db";

/**
 * Steuerbefehle in Antwortmails.
 *
 * Damit ein Hausmeister, der sich nie am System anmeldet, seine Vorgänge
 * trotzdem vollständig bearbeiten kann: Er antwortet auf die
 * Benachrichtigungsmail, schreibt in die erste Zeile `#erledigt`, und das
 * Ticket ist zu.
 *
 * ## Warum das abgesichert werden muss
 *
 * Absenderadressen lassen sich fälschen – das From-Feld einer Mail ist keine
 * Identitätsprüfung. Ohne weitere Absicherung könnte jeder, der eine
 * Ticketnummer und eine Mitarbeiteradresse kennt, fremde Vorgänge schließen
 * oder an einen anderen Bereich umleiten.
 *
 * Deshalb müssen für einen Befehl **beide** Bedingungen erfüllt sein:
 *
 *   1. Die Mail ging an eine Antwortadresse mit gültigem HMAC-Token. Das Token
 *      ist an Ticket und Empfänger gebunden und steht nur in der Mail, die
 *      diese Person bekommen hat – erraten lässt es sich nicht.
 *   2. Der Absender ist Mitglied des Bereichs, in dem der Vorgang liegt.
 *
 * Fehlt eines von beidem, wird der Text als gewöhnliche Nachricht abgelegt und
 * der Befehl verworfen. Der Vorfall landet im Protokoll, damit er auffällt.
 */

export type BefehlArt =
  | "erledigt"
  | "intern"
  | "warten"
  | "uebernehmen"
  | "weiterleiten";

export interface ErkannterBefehl {
  art: BefehlArt;
  /** Bei "weiterleiten": der genannte Bereich. */
  ziel?: string;
  /** Die Zeile, in der der Befehl stand – wird aus dem Text entfernt. */
  zeile: string;
}

export interface BefehlsErgebnis {
  /** Der Nachrichtentext ohne die Befehlszeilen. */
  text: string;
  befehle: ErkannterBefehl[];
}

/** Schreibweisen je Befehl. Umlaute jeweils auch in Umschrift. */
const MUSTER: { art: BefehlArt; woerter: string[]; mitZiel?: boolean }[] = [
  {
    art: "erledigt",
    woerter: ["erledigt", "fertig", "geloest", "gelöst", "close", "done", "zu"],
  },
  { art: "intern", woerter: ["intern", "notiz", "note", "internal"] },
  {
    art: "warten",
    woerter: ["warten", "wartet", "rueckfrage", "rückfrage", "nachfrage"],
  },
  {
    art: "uebernehmen",
    woerter: ["uebernehmen", "übernehmen", "meins", "mir", "claim"],
  },
  { art: "weiterleiten", woerter: ["an", "weiter", "weiterleiten"], mitZiel: true },
];

/**
 * Liest Befehle aus dem Anfang einer Nachricht.
 *
 * Befehle stehen als eigene Zeile und beginnen mit `#`. Nur die ersten Zeilen
 * werden geprüft – sonst würde ein `#erledigt` mitten im Fließtext, etwa in
 * einem Zitat, ungewollt auslösen.
 */
export function erkenneBefehle(text: string): BefehlsErgebnis {
  const zeilen = text.split("\n");
  const befehle: ErkannterBefehl[] = [];
  let ersteInhaltszeile = 0;

  // Höchstens die ersten fünf nicht leeren Zeilen betrachten.
  let geprueft = 0;
  for (let i = 0; i < zeilen.length && geprueft < 5; i++) {
    const zeile = zeilen[i].trim();

    if (zeile === "") {
      ersteInhaltszeile = i + 1;
      continue;
    }

    geprueft++;

    if (!zeile.startsWith("#")) break;

    const ohneRaute = zeile.slice(1).trim().toLowerCase();
    const [wort, ...rest] = ohneRaute.split(/\s+/);

    const treffer = MUSTER.find((m) => m.woerter.includes(wort));
    if (!treffer) break;

    befehle.push({
      art: treffer.art,
      ziel: treffer.mitZiel ? rest.join(" ").trim() || undefined : undefined,
      zeile,
    });
    ersteInhaltszeile = i + 1;
  }

  return {
    text: zeilen.slice(ersteInhaltszeile).join("\n").trim(),
    befehle,
  };
}

export interface BefehlsPruefung {
  erlaubt: boolean;
  /** Grund der Ablehnung, für das Protokoll und die interne Notiz. */
  grund?: string;
  /** Die Person, die den Befehl ausführt. */
  userId?: string;
  name?: string;
}

/**
 * Darf dieser Absender Befehle auf diesem Vorgang ausführen?
 *
 * @param bestaetigterEmpfaengerId Aus dem signierten Token. `null`, wenn die
 *        Mail über Betreff oder Kopfzeile zugeordnet wurde – dann fehlt der
 *        Nachweis, dass der Absender die Mail überhaupt bekommen hat.
 */
export async function pruefeBefehlsrecht(opts: {
  ticketId: string;
  absenderAdresse: string;
  bestaetigterEmpfaengerId: string | null;
}): Promise<BefehlsPruefung> {
  const ticket = await db.ticket.findUnique({
    where: { id: opts.ticketId },
    select: {
      teamId: true,
      queueId: true,
      team: { select: { allowEmailCommands: true, name: true } },
    },
  });

  if (!ticket) {
    return { erlaubt: false, grund: "Der Vorgang wurde nicht gefunden." };
  }

  if (!ticket.team.allowEmailCommands) {
    return {
      erlaubt: false,
      grund: `Der Bereich ${ticket.team.name} hat Steuerbefehle per E-Mail abgeschaltet.`,
    };
  }

  // Ohne gültiges Token fehlt der Nachweis, dass der Absender diese Mail
  // bekommen hat. Eine gefälschte Absenderadresse allein reicht nicht.
  if (!opts.bestaetigterEmpfaengerId) {
    return {
      erlaubt: false,
      grund:
        "Die Antwort kam nicht an die signierte Antwortadresse. Befehle " +
        "werden deshalb nicht ausgeführt – der Text wurde als Nachricht " +
        "gespeichert.",
    };
  }

  const person = await db.user.findUnique({
    where: { id: opts.bestaetigterEmpfaengerId },
    select: { id: true, name: true, email: true, isActive: true },
  });

  if (!person?.isActive) {
    return { erlaubt: false, grund: "Das Konto ist nicht aktiv." };
  }

  // Der Absender muss die Person sein, an die das Token ausgestellt wurde.
  // Sonst könnte jemand ein abgefangenes Token mit eigener Adresse nutzen.
  if (person.email.toLowerCase() !== opts.absenderAdresse.toLowerCase()) {
    return {
      erlaubt: false,
      grund:
        "Die Absenderadresse stimmt nicht mit der Adresse überein, an die " +
        "diese Nachricht ging.",
    };
  }

  // Und im Bereich arbeiten – ein Melder darf seinen Vorgang nicht per Mail
  // weiterleiten oder anderen zuweisen.
  const mitglied = await db.teamMembership.findFirst({
    where: { userId: person.id, teamId: ticket.teamId },
    select: { id: true },
  });

  if (!mitglied) {
    return {
      erlaubt: false,
      grund: `${person.name} arbeitet nicht im Bereich ${ticket.team.name}.`,
    };
  }

  return { erlaubt: true, userId: person.id, name: person.name };
}

/** Übersetzt einen Befehl in den Zielstatus, sofern er einen setzt. */
export function statusFuerBefehl(art: BefehlArt): TicketStatus | null {
  switch (art) {
    case "erledigt":
      return TicketStatus.GELOEST;
    case "warten":
      return TicketStatus.WARTET_AUF_MELDER;
    default:
      return null;
  }
}

/** Sucht den in `#an <bereich>` genannten Bereich. */
export async function findeZielBereich(bezeichnung: string) {
  const suche = bezeichnung.trim();
  if (!suche) return null;

  return db.team.findFirst({
    where: {
      isActive: true,
      OR: [
        { key: { equals: suche, mode: "insensitive" } },
        { name: { equals: suche, mode: "insensitive" } },
        { name: { startsWith: suche, mode: "insensitive" } },
      ],
    },
    select: { id: true, name: true, key: true },
  });
}

/** Lesbare Beschreibung eines Befehls für das Protokoll. */
export function beschreibeBefehl(befehl: ErkannterBefehl): string {
  switch (befehl.art) {
    case "erledigt":
      return "#erledigt";
    case "intern":
      return "#intern";
    case "warten":
      return "#warten";
    case "uebernehmen":
      return "#übernehmen";
    case "weiterleiten":
      return `#an ${befehl.ziel ?? ""}`.trim();
  }
}
