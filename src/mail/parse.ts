import type { EingehendeNachricht } from "@/mail/types";

/**
 * Aufbereitung eingehender Mails.
 *
 * Zwei Aufgaben, die im Alltag den Unterschied machen:
 *
 *  1. Zitate abschneiden. Wer auf eine Benachrichtigung antwortet, schickt
 *     meist den gesamten bisherigen Verlauf mit. Ohne Abschneiden steht im
 *     Ticket unter jeder Antwort noch einmal alles Vorherige, und nach fünf
 *     Antworten ist der Vorgang unlesbar.
 *
 *  2. Automatische Antworten erkennen. Eine Abwesenheitsnotiz auf unsere
 *     Bestätigungsmail darf kein Ticket erzeugen und erst recht keine weitere
 *     Bestätigung auslösen – sonst schaukeln sich zwei Automaten gegenseitig
 *     hoch.
 */

/**
 * Zeilen, ab denen der zitierte Teil beginnt.
 *
 * Deckt die verbreiteten deutschen und englischen Mailprogramme ab. Bewusst
 * konservativ: Lieber ein Zitat zu viel stehen lassen als versehentlich den
 * eigentlichen Text abschneiden.
 */
const ZITAT_TRENNER: RegExp[] = [
  // Outlook deutsch
  /^\s*-{3,}\s*Ursprüngliche Nachricht\s*-{3,}/im,
  /^\s*_{5,}\s*$/m,
  /^\s*Von:\s.+$/im,
  // Outlook englisch
  /^\s*-{3,}\s*Original Message\s*-{3,}/im,
  /^\s*From:\s.+$/im,
  // Thunderbird, Apple Mail, Gmail
  /^\s*Am .+ schrieb .+:\s*$/im,
  /^\s*On .+ wrote:\s*$/im,
  /^\s*.+ <[^>]+> schrieb am .+:\s*$/im,
  // Unsere eigene Signatur in ausgehenden Mails
  /^\s*—\s*$\n^.+\n\n^Vorgang [A-Z0-9]+-\d{4}-\d+:/im,
];

/**
 * Schneidet den zitierten Teil einer Antwort ab.
 *
 * @returns Der eigentliche neue Text.
 */
export function entferneZitate(text: string): string {
  let ergebnis = text;

  // Frühesten Trenner suchen und dort abschneiden.
  let frühestesEnde = ergebnis.length;
  for (const muster of ZITAT_TRENNER) {
    const treffer = ergebnis.match(muster);
    if (treffer?.index !== undefined && treffer.index < frühestesEnde) {
      frühestesEnde = treffer.index;
    }
  }
  ergebnis = ergebnis.slice(0, frühestesEnde);

  // Zusammenhängende Zitatzeilen (">") am Ende abschneiden.
  const zeilen = ergebnis.split("\n");
  let letzteEchteZeile = zeilen.length;
  for (let i = zeilen.length - 1; i >= 0; i--) {
    const zeile = zeilen[i].trim();
    if (zeile === "" || zeile.startsWith(">")) {
      letzteEchteZeile = i;
    } else {
      break;
    }
  }

  return zeilen.slice(0, letzteEchteZeile).join("\n").trim();
}

/**
 * Wandelt HTML in lesbaren Text.
 *
 * Bewusst ohne HTML-Bibliothek und ohne Speichern des HTML: Was aus einer
 * fremden Mail kommt, wird als reiner Text abgelegt und auch als reiner Text
 * angezeigt. Damit kann eine präparierte Mail kein Skript und keinen
 * Verweis in die Oberfläche schmuggeln.
 */
export function htmlZuText(html: string): string {
  return (
    html
      // Blockelemente in Zeilenumbrüche überführen, damit Absätze erhalten bleiben.
      .replace(/<br\s*\/?>/gi, "\n")
      .replace(/<\/(p|div|tr|li|h[1-6])>/gi, "\n")
      .replace(/<li[^>]*>/gi, "• ")
      // Skript- und Stilblöcke samt Inhalt entfernen.
      .replace(/<(script|style)[^>]*>[\s\S]*?<\/\1>/gi, "")
      // Alle übrigen Auszeichnungen entfernen.
      .replace(/<[^>]+>/g, "")
      // Häufige Entitäten auflösen.
      .replace(/&nbsp;/g, " ")
      .replace(/&auml;/g, "ä")
      .replace(/&ouml;/g, "ö")
      .replace(/&uuml;/g, "ü")
      .replace(/&Auml;/g, "Ä")
      .replace(/&Ouml;/g, "Ö")
      .replace(/&Uuml;/g, "Ü")
      .replace(/&szlig;/g, "ß")
      .replace(/&amp;/g, "&")
      .replace(/&lt;/g, "<")
      .replace(/&gt;/g, ">")
      .replace(/&quot;/g, '"')
      .replace(/&#(\d+);/g, (_, code) => String.fromCharCode(Number(code)))
      // Mehr als zwei Leerzeilen zusammenfassen.
      .replace(/\n{3,}/g, "\n\n")
      .trim()
  );
}

/** Der Nachrichtentext einer Mail, bereinigt und ohne Zitate. */
export function nachrichtentext(mail: EingehendeNachricht): string {
  const roh = mail.text ?? (mail.html ? htmlZuText(mail.html) : "");
  const ohneZitat = entferneZitate(roh);

  // Falls nach dem Abschneiden nichts übrig bleibt (etwa bei einer Antwort,
  // die nur aus einem Zitat besteht), lieber den ganzen Text behalten als eine
  // leere Nachricht abzulegen.
  return ohneZitat || roh.trim();
}

/**
 * Ist das eine automatische Antwort?
 *
 * Prüft die dafür vorgesehenen Kopfzeilen und zusätzlich typische
 * Betreffzeilen – nicht jedes Mailprogramm setzt die Kopfzeilen korrekt.
 */
export function istAutomatischeAntwort(mail: EingehendeNachricht): boolean {
  const auto = mail.autoSubmitted?.toLowerCase();
  if (auto && auto !== "no") return true;

  const precedence = mail.precedence?.toLowerCase();
  if (precedence && ["bulk", "auto_reply", "junk", "list"].includes(precedence)) {
    return true;
  }

  const betreff = mail.betreff.toLowerCase();
  const muster = [
    "abwesenheit",
    "abwesenheitsnotiz",
    "automatische antwort",
    "automatic reply",
    "out of office",
    "autoreply",
    "urlaubsmeldung",
    "nicht im büro",
  ];
  if (muster.some((m) => betreff.includes(m))) return true;

  // Unzustellbarkeitsberichte.
  const bounce = [
    "unzustellbar",
    "undeliverable",
    "delivery status notification",
    "mail delivery failed",
    "returned mail",
    "delivery has failed",
  ];
  return bounce.some((m) => betreff.includes(m));
}

/** Entfernt Antwort- und Weiterleitungspräfixe aus dem Betreff. */
export function betreffBereinigen(betreff: string): string {
  let ergebnis = betreff.trim();
  const praefixe = /^\s*(AW|WG|RE|FW|FWD|ANTW|SV|VS)\s*(\[\d+\])?\s*:\s*/i;

  // Mehrfach angewandt, weil sich Präfixe stapeln ("AW: WG: AW: …").
  let vorher: string;
  do {
    vorher = ergebnis;
    ergebnis = ergebnis.replace(praefixe, "");
  } while (ergebnis !== vorher);

  // Die Ticketkennung gehört nicht in den Betreff des Vorgangs.
  ergebnis = ergebnis.replace(/\[[A-Z][A-Z0-9]{1,7}-\d{4}-\d{4,8}\]\s*/g, "");

  return ergebnis.trim() || "(ohne Betreff)";
}
