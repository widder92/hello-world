import type { Prisma } from "@prisma/client";

/**
 * Vergabe der Ticketnummern, z. B. "HM-2026-000087".
 *
 * Aufbau: <Bereichskürzel>-<Jahr>-<laufende Nummer, 6-stellig>
 *
 * Die Nummer ist sprechend: Wer sie am Telefon hört, weiß sofort, welcher
 * Bereich zuständig ist und aus welchem Jahr der Vorgang stammt. Sie ist
 * außerdem der Anker für die Zuordnung von Antwortmails, wenn die
 * Mail-Kopfzeilen unterwegs verloren gehen.
 *
 * Die laufende Nummer wird in einer Zählertabelle geführt und mit einer
 * Zeilensperre hochgezählt. Zwei gleichzeitig eingehende Mails an denselben
 * Bereich können dadurch keine identische Nummer bekommen: die zweite
 * Transaktion wartet, bis die erste ihre Zeile freigibt.
 */

interface CounterRow {
  value: number;
}

export async function nextTicketNumber(
  tx: Prisma.TransactionClient,
  teamId: string,
  teamKey: string,
  now: Date = new Date(),
): Promise<string> {
  const year = now.getFullYear();

  // Zählerzeile anlegen, falls sie für dieses Jahr noch nicht existiert.
  // ON CONFLICT DO NOTHING, damit gleichzeitige Anlagen sich nicht gegenseitig
  // mit einem Unique-Fehler abschießen.
  await tx.$executeRaw`
    INSERT INTO "TicketCounter" ("id", "teamId", "year", "value")
    VALUES (gen_random_uuid()::text, ${teamId}, ${year}, 0)
    ON CONFLICT ("teamId", "year") DO NOTHING
  `;

  // Sperrt die Zeile bis zum Ende der Transaktion und liefert den neuen Stand.
  const rows = await tx.$queryRaw<CounterRow[]>`
    UPDATE "TicketCounter"
    SET "value" = "value" + 1
    WHERE "teamId" = ${teamId} AND "year" = ${year}
    RETURNING "value"
  `;

  const value = rows[0]?.value;
  if (typeof value !== "number") {
    throw new Error(
      `Die laufende Nummer für den Bereich ${teamKey} konnte nicht ermittelt werden.`,
    );
  }

  return formatTicketNumber(teamKey, year, value);
}

export function formatTicketNumber(
  teamKey: string,
  year: number,
  value: number,
): string {
  return `${teamKey.toUpperCase()}-${year}-${String(value).padStart(6, "0")}`;
}

/**
 * Sucht eine Ticketnummer in einem Freitext, etwa im Betreff einer
 * Antwortmail ("AW: [HM-2026-000087] Heizung defekt").
 *
 * Bewusst großzügig: Groß-/Kleinschreibung egal, Klammern optional.
 */
const TICKET_NUMBER_PATTERN = /\b([A-Z][A-Z0-9]{1,7})-(\d{4})-(\d{4,8})\b/i;

export function findTicketNumber(text: string): string | null {
  const match = text.match(TICKET_NUMBER_PATTERN);
  if (!match) return null;
  return `${match[1].toUpperCase()}-${match[2]}-${match[3]}`;
}
