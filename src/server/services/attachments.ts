import { createHash, randomUUID } from "node:crypto";
import { mkdir, unlink, writeFile } from "node:fs/promises";
import path from "node:path";

import { db } from "@/server/db";

/**
 * Dateiablage für Anhänge.
 *
 * Eine Stelle für beide Wege – aus dem Browser und aus eingehenden Mails.
 * Getrennte Umsetzungen wären auseinandergelaufen, und die Größenbegrenzung
 * hätte irgendwann nur noch auf einer der beiden Seiten gegolten.
 *
 * Dateien liegen außerhalb des öffentlichen Verzeichnisses und werden
 * ausschließlich über `/api/uploads/[id]` mit Rechteprüfung herausgegeben.
 * Der Dateiname aus der Quelle landet nie im Pfad – gespeichert wird unter
 * einer zufälligen Kennung, sonst könnte ein Name wie `../../etc/passwd`
 * aus dem Ablageverzeichnis herausführen.
 */

export class UploadFehler extends Error {}

/** Was hochgeladen werden darf. */
const ERLAUBTE_TYPEN = [
  // Bilder – der häufigste Fall: Foto vom Schaden mit dem Handy
  "image/jpeg",
  "image/png",
  "image/gif",
  "image/webp",
  "image/heic",
  "image/heif",
  // Dokumente
  "application/pdf",
  "text/plain",
  "text/csv",
  "application/msword",
  "application/vnd.openxmlformats-officedocument.wordprocessingml.document",
  "application/vnd.ms-excel",
  "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet",
  "application/vnd.ms-outlook",
  "message/rfc822",
  // Archive – kommen bei Protokolldateien vor
  "application/zip",
  "application/x-zip-compressed",
];

/**
 * Typen, die niemals angenommen werden – auch nicht, wenn sie in der Liste
 * oben stünden. Ausführbares landet nicht in der Ablage, egal wie es
 * deklariert ist.
 */
const VERBOTENE_ENDUNGEN = [
  ".exe", ".bat", ".cmd", ".com", ".scr", ".pif", ".msi", ".msp",
  ".vbs", ".vbe", ".js", ".jse", ".wsf", ".wsh", ".ps1", ".psm1",
  ".jar", ".sh", ".dll", ".hta", ".lnk", ".reg",
];

export function maxUploadBytes(): number {
  return Number(process.env.MAX_UPLOAD_MB || 25) * 1024 * 1024;
}

function ablageVerzeichnis(): string {
  return path.resolve(process.env.UPLOAD_DIR || "./uploads");
}

/**
 * Bereinigt einen Dateinamen für die Anzeige.
 *
 * Der Name wird nur gespeichert, nie als Pfad verwendet – trotzdem werden
 * Pfadanteile entfernt, damit in der Oberfläche nicht plötzlich ein Pfad
 * aus dem Rechner des Absenders steht.
 */
export function bereinigeDateinamen(name: string): string {
  const nurName = name.split(/[/\\]/).pop() ?? "anhang";
  const sauber = nurName
    // Steuerzeichen entfernen (U+0000–U+001F).
    .replace(new RegExp("[\\u0000-\\u001f]", "g"), "")
    .trim();
  return sauber.slice(0, 200) || "anhang";
}

export interface UploadPruefung {
  ok: boolean;
  grund?: string;
}

/** Prüft Größe und Art einer Datei, bevor sie geschrieben wird. */
export function pruefeDatei(opts: {
  filename: string;
  mimeType: string;
  size: number;
}): UploadPruefung {
  if (opts.size <= 0) {
    return { ok: false, grund: "Die Datei ist leer." };
  }

  const max = maxUploadBytes();
  if (opts.size > max) {
    return {
      ok: false,
      grund: `Die Datei ist größer als ${Math.round(max / 1024 / 1024)} MB.`,
    };
  }

  const endung = path.extname(opts.filename).toLowerCase();
  if (VERBOTENE_ENDUNGEN.includes(endung)) {
    return {
      ok: false,
      grund: `Dateien vom Typ ${endung} werden aus Sicherheitsgründen nicht angenommen.`,
    };
  }

  const typ = opts.mimeType.split(";")[0].trim().toLowerCase();
  if (!ERLAUBTE_TYPEN.includes(typ)) {
    return {
      ok: false,
      grund:
        `Dateien vom Typ "${typ}" werden nicht angenommen. Erlaubt sind Bilder, ` +
        `PDF, Office-Dokumente und Textdateien.`,
    };
  }

  return { ok: true };
}

export interface GespeicherterAnhang {
  id: string;
  filename: string;
  mimeType: string;
  size: number;
}

/**
 * Schreibt eine Datei in die Ablage und legt den Datensatz an.
 *
 * @throws UploadFehler bei zu großer oder nicht erlaubter Datei.
 */
export async function speichereAnhang(opts: {
  inhalt: Buffer;
  filename: string;
  mimeType: string;
  ticketId?: string | null;
  messageId?: string | null;
  uploadedById?: string | null;
  fromEmail?: boolean;
}): Promise<GespeicherterAnhang> {
  const filename = bereinigeDateinamen(opts.filename);
  const mimeType = opts.mimeType.split(";")[0].trim().toLowerCase();

  const pruefung = pruefeDatei({
    filename,
    mimeType,
    size: opts.inhalt.length,
  });
  if (!pruefung.ok) throw new UploadFehler(pruefung.grund);

  // Nach Monat gruppiert, damit ein Verzeichnis nach Jahren nicht Hunderttausende
  // Einträge enthält – manche Dateisysteme werden dabei spürbar langsam.
  const unterordner = new Date().toISOString().slice(0, 7);
  const schluessel = path.join(unterordner, randomUUID());
  const ziel = path.join(ablageVerzeichnis(), schluessel);

  await mkdir(path.dirname(ziel), { recursive: true });
  await writeFile(ziel, opts.inhalt);

  const pruefsumme = createHash("sha256").update(opts.inhalt).digest("hex");

  const datensatz = await db.attachment.create({
    data: {
      ticketId: opts.ticketId ?? null,
      messageId: opts.messageId ?? null,
      filename,
      mimeType,
      size: opts.inhalt.length,
      storageKey: schluessel,
      checksum: pruefsumme,
      uploadedById: opts.uploadedById ?? null,
      fromEmail: opts.fromEmail ?? false,
    },
    select: { id: true, filename: true, mimeType: true, size: true },
  });

  return datensatz;
}

/**
 * Entfernt einen Anhang samt Datei.
 *
 * Die Datei wird nur gelöscht, wenn kein anderer Datensatz mehr darauf zeigt:
 * Beim Abspalten verweisen Eltern- und Kindvorgang auf dieselbe Datei, und
 * ein Löschen im einen Vorgang darf sie im anderen nicht verschwinden lassen.
 */
export async function entferneAnhang(attachmentId: string): Promise<void> {
  const anhang = await db.attachment.findUnique({
    where: { id: attachmentId },
    select: { storageKey: true },
  });
  if (!anhang) return;

  await db.attachment.delete({ where: { id: attachmentId } });

  const weitere = await db.attachment.count({
    where: { storageKey: anhang.storageKey },
  });
  if (weitere > 0) return;

  const datei = path.resolve(ablageVerzeichnis(), anhang.storageKey);
  if (!datei.startsWith(ablageVerzeichnis() + path.sep)) return;

  await unlink(datei).catch(() => {
    // Eine bereits verschwundene Datei ist kein Fehler – der Datensatz ist weg,
    // und darum ging es.
  });
}

/**
 * Hängt bereits gespeicherte Anhänge an einen Vorgang bzw. eine Nachricht.
 *
 * Wird gebraucht, weil Anhänge im Meldeformular hochgeladen werden, bevor der
 * Vorgang existiert: Sie entstehen zunächst ohne Zuordnung und werden beim
 * Absenden nachgetragen.
 */
export async function ordneAnhaengeZu(opts: {
  attachmentIds: string[];
  ticketId: string;
  messageId?: string | null;
  /** Nur Anhänge dieser Person zuordnen. */
  uploadedById: string;
}): Promise<number> {
  if (opts.attachmentIds.length === 0) return 0;

  // Die Beschränkung auf noch nicht zugeordnete eigene Anhänge verhindert,
  // dass sich durch Raten einer Kennung ein fremder Anhang an den eigenen
  // Vorgang heften lässt.
  const ergebnis = await db.attachment.updateMany({
    where: {
      id: { in: opts.attachmentIds },
      ticketId: null,
      uploadedById: opts.uploadedById,
    },
    data: {
      ticketId: opts.ticketId,
      messageId: opts.messageId ?? null,
    },
  });

  return ergebnis.count;
}

/**
 * Räumt Anhänge auf, die hochgeladen, aber nie einem Vorgang zugeordnet
 * wurden – etwa weil das Meldeformular abgebrochen wurde.
 */
export async function entferneVerwaisteAnhaenge(
  aelterAlsStunden = 24,
): Promise<number> {
  const grenze = new Date(Date.now() - aelterAlsStunden * 60 * 60 * 1000);

  const verwaist = await db.attachment.findMany({
    where: { ticketId: null, createdAt: { lt: grenze } },
    select: { id: true },
    take: 500,
  });

  for (const a of verwaist) {
    await entferneAnhang(a.id);
  }

  return verwaist.length;
}
