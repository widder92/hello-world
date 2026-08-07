import { hash, verify } from "@node-rs/argon2";

/**
 * Passwörter lokaler Konten.
 *
 * Argon2id ist bewusst rechenaufwendig: Wer eine Kopie der Datenbank erbeutet,
 * soll die Hashes nicht in vertretbarer Zeit durchprobieren können. Die
 * Parameter folgen der Empfehlung des OWASP Password Storage Cheat Sheet
 * (19 MiB Speicher, 2 Durchgänge).
 */
const OPTIONS = {
  memoryCost: 19456,
  timeCost: 2,
  parallelism: 1,
} as const;

export async function hashPassword(plain: string): Promise<string> {
  return hash(plain, OPTIONS);
}

/**
 * Prüft ein Passwort. Gibt bei fehlerhaftem Hash `false` zurück statt zu
 * werfen, damit ein beschädigter Datensatz die Anmeldeseite nicht abstürzen
 * lässt – der Anmeldeversuch schlägt dann schlicht fehl.
 */
export async function verifyPassword(
  storedHash: string,
  plain: string,
): Promise<boolean> {
  try {
    return await verify(storedHash, plain, OPTIONS);
  } catch {
    return false;
  }
}

/**
 * Mindestanforderungen an ein Passwort. Bewusst auf Länge statt auf
 * Zeichenklassen gesetzt: erzwungene Sonderzeichen führen erfahrungsgemäß zu
 * "Passwort1!" auf einem Zettel unter der Tastatur, Länge nicht.
 */
export function validatePasswordStrength(plain: string): string | null {
  if (plain.length < 12) {
    return "Das Passwort muss mindestens 12 Zeichen lang sein.";
  }
  if (plain.length > 200) {
    return "Das Passwort darf höchstens 200 Zeichen lang sein.";
  }
  if (/^\s|\s$/.test(plain)) {
    return "Das Passwort darf nicht mit einem Leerzeichen beginnen oder enden.";
  }
  const trivial = [
    "passwort",
    "password",
    "ticketsystem",
    "12345678",
    "qwertz",
    "willkommen",
  ];
  const lower = plain.toLowerCase();
  if (trivial.some((t) => lower.includes(t))) {
    return "Das Passwort enthält einen zu leicht erratbaren Bestandteil.";
  }
  return null;
}
