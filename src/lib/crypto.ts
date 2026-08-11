import {
  createCipheriv,
  createDecipheriv,
  createHmac,
  randomBytes,
  scryptSync,
  timingSafeEqual,
} from "node:crypto";

/**
 * Verschlüsselung und Signaturen.
 *
 * Zwei Dinge werden hier abgesichert:
 *
 *  1. Postfach-Zugangsdaten. Sie liegen in der Datenbank und dürfen dort nicht
 *     im Klartext stehen – wer eine Sicherungskopie erhält, hätte sonst Zugriff
 *     auf sämtliche Bereichspostfächer.
 *
 *  2. E-Mail-Antwortadressen. Absenderadressen sind fälschbar; ein Blick auf
 *     das From-Feld reicht deshalb nicht, um einen Steuerbefehl wie
 *     "#erledigt" auszuführen. Jede ausgehende Mail bekommt eine
 *     Antwortadresse mit signiertem Token, das an Ticket und Empfänger
 *     gebunden ist.
 */

function appSecret(): string {
  const secret = process.env.APP_SECRET;
  if (!secret || secret.length < 16) {
    throw new Error(
      "APP_SECRET fehlt oder ist zu kurz. Erzeugen mit: openssl rand -base64 48",
    );
  }
  return secret;
}

/** Leitet aus APP_SECRET einen 32-Byte-Schlüssel für den genannten Zweck ab. */
function derivedKey(purpose: string): Buffer {
  return scryptSync(appSecret(), `ticketsystem:${purpose}`, 32);
}

// ---------------------------------------------------------------------------
// Symmetrische Verschlüsselung (AES-256-GCM)
// ---------------------------------------------------------------------------

const ENC_PREFIX = "v1";

/**
 * Verschlüsselt einen Text. Ergebnisformat:
 *   v1.<iv-base64url>.<authTag-base64url>.<ciphertext-base64url>
 */
export function encryptSecret(plaintext: string): string {
  const key = derivedKey("secret-box");
  const iv = randomBytes(12);
  const cipher = createCipheriv("aes-256-gcm", key, iv);
  const encrypted = Buffer.concat([
    cipher.update(plaintext, "utf8"),
    cipher.final(),
  ]);
  const tag = cipher.getAuthTag();
  return [
    ENC_PREFIX,
    iv.toString("base64url"),
    tag.toString("base64url"),
    encrypted.toString("base64url"),
  ].join(".");
}

/**
 * Entschlüsselt einen mit encryptSecret erzeugten Wert.
 * Wirft, wenn der Wert verändert wurde oder mit einem anderen APP_SECRET
 * verschlüsselt wurde – ein stillschweigendes Zurückfallen auf Klartext wäre
 * hier gefährlich.
 */
export function decryptSecret(payload: string): string {
  const parts = payload.split(".");
  if (parts.length !== 4 || parts[0] !== ENC_PREFIX) {
    throw new Error("Verschlüsselter Wert hat ein unbekanntes Format.");
  }
  const [, ivB64, tagB64, dataB64] = parts;
  const key = derivedKey("secret-box");
  const decipher = createDecipheriv(
    "aes-256-gcm",
    key,
    Buffer.from(ivB64, "base64url"),
  );
  decipher.setAuthTag(Buffer.from(tagB64, "base64url"));
  return Buffer.concat([
    decipher.update(Buffer.from(dataB64, "base64url")),
    decipher.final(),
  ]).toString("utf8");
}

/** Bequemer Umgang mit optionalen Feldern (z. B. leeres Passwortfeld). */
export function encryptOptional(value: string | null | undefined): string | null {
  if (!value) return null;
  return encryptSecret(value);
}

export function decryptOptional(value: string | null | undefined): string | null {
  if (!value) return null;
  return decryptSecret(value);
}

// ---------------------------------------------------------------------------
// Signierte Token für E-Mail-Antwortadressen
// ---------------------------------------------------------------------------

export interface ReplyTokenPayload {
  /** Ticket, auf das sich die Antwort bezieht. */
  ticketId: string;
  /** Empfänger, an den diese Mail ging. Bindet das Token an eine Person. */
  recipientId: string;
}

function sign(data: string): string {
  return createHmac("sha256", derivedKey("reply-token"))
    .update(data)
    .digest("base64url")
    .slice(0, 27);
}

/**
 * Erzeugt das Token für eine Antwortadresse der Form
 *   ticket+<token>@example.org
 *
 * Das Token bindet Ticket und Empfänger aneinander. Wer die Mail nicht
 * bekommen hat, kann die Adresse nicht erraten – und ohne gültige Adresse
 * werden Steuerbefehle nicht ausgeführt.
 */
export function createReplyToken(payload: ReplyTokenPayload): string {
  const body = `${payload.ticketId}~${payload.recipientId}`;
  const encoded = Buffer.from(body, "utf8").toString("base64url");
  return `${encoded}.${sign(encoded)}`;
}

/**
 * Prüft ein Token und gibt seinen Inhalt zurück – oder null, wenn die
 * Signatur nicht passt. Der Vergleich läuft zeitkonstant, damit sich die
 * Signatur nicht Zeichen für Zeichen erraten lässt.
 */
export function verifyReplyToken(token: string): ReplyTokenPayload | null {
  const dot = token.lastIndexOf(".");
  if (dot <= 0) return null;

  const encoded = token.slice(0, dot);
  const provided = token.slice(dot + 1);
  const expected = sign(encoded);

  const providedBuf = Buffer.from(provided);
  const expectedBuf = Buffer.from(expected);
  if (providedBuf.length !== expectedBuf.length) return null;
  if (!timingSafeEqual(providedBuf, expectedBuf)) return null;

  const body = Buffer.from(encoded, "base64url").toString("utf8");
  const sep = body.indexOf("~");
  if (sep <= 0) return null;

  return {
    ticketId: body.slice(0, sep),
    recipientId: body.slice(sep + 1),
  };
}

/** Baut die vollständige Antwortadresse für eine ausgehende Mail. */
export function buildReplyAddress(payload: ReplyTokenPayload): string | null {
  const domain = process.env.MAIL_REPLY_DOMAIN;
  const localPart = process.env.MAIL_REPLY_LOCALPART || "ticket";
  if (!domain) return null;
  return `${localPart}+${createReplyToken(payload)}@${domain}`;
}

/**
 * Liest das Token aus einer Empfängeradresse heraus.
 * Akzeptiert "Name <ticket+abc@example.org>" ebenso wie die nackte Adresse.
 */
export function extractReplyToken(address: string): string | null {
  const match = address.match(/([A-Za-z0-9._-]+)\+([A-Za-z0-9._~-]+)@/);
  if (!match) return null;
  const expectedLocal = process.env.MAIL_REPLY_LOCALPART || "ticket";
  if (match[1].toLowerCase() !== expectedLocal.toLowerCase()) return null;
  return match[2];
}
