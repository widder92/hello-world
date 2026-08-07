/**
 * Hintergrunddienst.
 *
 * Drei wiederkehrende Aufgaben:
 *   - Postfächer abrufen und eingehende Mails zu Vorgängen machen
 *   - Fristen prüfen und bei drohender oder überschrittener Frist warnen
 *   - nachts aufräumen (Löschfristen, verwaiste Dateien)
 *
 * Läuft als eigener Prozess neben der Anwendung. Getrennt, weil ein Mailabruf
 * über eine langsame Leitung sonst Seitenaufrufe blockieren würde – und weil
 * bei mehreren Anwendungsinstanzen sonst jede für sich dieselben Mails holen
 * und doppelte Vorgänge erzeugen würde.
 */

import cron from "node-cron";

import { db } from "../src/server/db";
import { alleEingaengeAbrufen } from "../src/mail/inbound";
import { entferneVerwaisteAnhaenge } from "../src/server/services/attachments";
import { pruefeFristen } from "../src/server/services/sla";

const MAIL_CRON = process.env.WORKER_MAIL_CRON || "*/2 * * * *";
const SLA_CRON = process.env.WORKER_SLA_CRON || "*/5 * * * *";
const CLEANUP_CRON = process.env.WORKER_CLEANUP_CRON || "30 3 * * *";

/** Verhindert, dass sich zwei Durchläufe derselben Aufgabe überlappen. */
const laeuft = new Set<string>();

async function einmalig(name: string, aufgabe: () => Promise<void>) {
  if (laeuft.has(name)) {
    console.warn(`[Worker] "${name}" läuft noch, dieser Durchlauf entfällt.`);
    return;
  }
  laeuft.add(name);
  const start = Date.now();
  try {
    await aufgabe();
  } catch (error) {
    // Ein Fehler darf den Dienst nicht beenden – sonst steht nach einer
    // einzelnen Störung der gesamte Mailabruf still.
    console.error(`[Worker] "${name}" ist fehlgeschlagen:`, error);
  } finally {
    laeuft.delete(name);
    console.info(`[Worker] "${name}" fertig in ${Date.now() - start} ms.`);
  }
}

async function mailAbruf() {
  const ergebnisse = await alleEingaengeAbrufen();
  if (ergebnisse.length === 0) return;

  for (const e of ergebnisse) {
    if (e.fehler) {
      console.error(`[Mail] ${e.eingang}: ${e.fehler}`);
    } else if (e.geholt > 0) {
      console.info(
        `[Mail] ${e.eingang}: ${e.geholt} geholt – ` +
          `${e.neueVorgaenge} neue Vorgänge, ${e.antworten} Antworten, ` +
          `${e.uebersprungen} übersprungen.`,
      );
    }
  }
}

async function fristen() {
  const ergebnis = await pruefeFristen();
  if (ergebnis.gewarnt > 0 || ergebnis.ueberschritten > 0) {
    console.info(
      `[Fristen] ${ergebnis.gewarnt} Warnungen, ` +
        `${ergebnis.ueberschritten} Überschreitungen ` +
        `(${ergebnis.geprueft} Vorgänge geprüft).`,
    );
  }
}

async function aufraeumen() {
  const tage = Number(process.env.RETENTION_DAYS || 0);

  // Anhänge, die im Meldeformular hochgeladen wurden, ohne dass die Meldung
  // je abgeschickt wurde. Ohne diesen Schritt sammelt sich die Dateiablage
  // still mit Fotos voll, die zu keinem Vorgang gehören.
  const verwaist = await entferneVerwaisteAnhaenge(24);
  if (verwaist > 0) {
    console.info(`[Aufräumen] ${verwaist} nicht zugeordnete Anhänge entfernt.`);
  }

  // Alte Benachrichtigungen verschwinden immer nach 90 Tagen – sie sind reine
  // Hinweise und niemand liest sie später noch.
  const grenze = new Date(Date.now() - 90 * 24 * 60 * 60 * 1000);
  const hinweise = await db.notification.deleteMany({
    where: { createdAt: { lt: grenze }, readAt: { not: null } },
  });
  if (hinweise.count > 0) {
    console.info(`[Aufräumen] ${hinweise.count} alte Hinweise entfernt.`);
  }

  // Vorgänge nur löschen, wenn eine Frist ausdrücklich eingestellt ist. Die
  // Vorgabe 0 bedeutet: nichts löschen. Ein unbeabsichtigtes Löschen von
  // Vorgängen wäre nicht rückgängig zu machen.
  if (tage <= 0) return;

  const loeschGrenze = new Date(Date.now() - tage * 24 * 60 * 60 * 1000);
  const alte = await db.ticket.findMany({
    where: { status: "GESCHLOSSEN", closedAt: { lt: loeschGrenze } },
    select: { id: true, number: true },
    take: 500,
  });

  if (alte.length === 0) return;

  await db.ticket.deleteMany({ where: { id: { in: alte.map((t) => t.id) } } });
  console.info(
    `[Aufräumen] ${alte.length} Vorgänge nach ${tage} Tagen Aufbewahrung gelöscht.`,
  );
}

async function main() {
  console.info("[Worker] Startet.");
  console.info(`[Worker] Mailabruf:   ${MAIL_CRON}`);
  console.info(`[Worker] Fristen:     ${SLA_CRON}`);
  console.info(`[Worker] Aufräumen:   ${CLEANUP_CRON}`);

  // Beim Start einmal sofort laufen, damit nach einem Neustart nicht bis zum
  // nächsten Zeitpunkt gewartet wird.
  await einmalig("Mailabruf", mailAbruf);
  await einmalig("Fristen", fristen);

  cron.schedule(MAIL_CRON, () => void einmalig("Mailabruf", mailAbruf));
  cron.schedule(SLA_CRON, () => void einmalig("Fristen", fristen));
  cron.schedule(CLEANUP_CRON, () => void einmalig("Aufräumen", aufraeumen));

  const beenden = async (signal: string) => {
    console.info(`[Worker] ${signal} empfangen, beende.`);
    await db.$disconnect();
    process.exit(0);
  };

  process.on("SIGTERM", () => void beenden("SIGTERM"));
  process.on("SIGINT", () => void beenden("SIGINT"));
}

void main();
