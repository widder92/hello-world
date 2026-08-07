import { TeamRole } from "@prisma/client";
import { beforeEach, describe, expect, it } from "vitest";

import { findeZielBereich, pruefeBefehlsrecht } from "@/mail/commands";
import {
  bereichAnlegen,
  datenbankErreichbar,
  eingangAnlegen,
  leereDatenbank,
  mitgliedschaftAnlegen,
  personAnlegen,
  ticketAnlegenRoh,
} from "./datenbank";

/**
 * Steuerbefehle per Antwortmail sind die bequemste Funktion des Systems und
 * zugleich die gefährlichste: Das Absenderfeld einer Mail ist keine
 * Identitätsprüfung, es lässt sich mit drei Zeilen beliebig setzen.
 *
 * Ohne Absicherung könnte jeder, der eine Ticketnummer und eine
 * Mitarbeiteradresse kennt, fremde Vorgänge schließen oder an einen anderen
 * Bereich umleiten – und im Protokoll stünde der Name der Kollegin.
 *
 * Deshalb müssen drei Dinge gleichzeitig stimmen: gültiges Token (der Nachweis,
 * dass diese Mail tatsächlich an diese Person ging), passende Absenderadresse
 * und Mitgliedschaft im zuständigen Bereich. Jeder dieser drei Punkte wird hier
 * einzeln ausgehebelt.
 */

const verfuegbar = await datenbankErreichbar();

async function aufbau(opts: { allowEmailCommands?: boolean } = {}) {
  const team = await bereichAnlegen({
    key: "HM",
    name: "Hausmeister",
    allowEmailCommands: opts.allowEmailCommands ?? true,
  });
  const eingang = await eingangAnlegen({ teamId: team.id });

  const melderin = await personAnlegen({
    name: "Anna Bergmann",
    email: "anna.bergmann@example.org",
  });
  const hausmeister = await personAnlegen({
    name: "Petra Klein",
    email: "petra.klein@example.org",
  });
  await mitgliedschaftAnlegen(hausmeister.id, team.id, TeamRole.MEMBER);

  const ticket = await ticketAnlegenRoh({
    number: "HM-2026-000087",
    subject: "Heizung im Gruppenraum",
    queueId: eingang.id,
    teamId: team.id,
    requesterId: melderin.id,
  });

  return { team, eingang, melderin, hausmeister, ticket };
}

describe.runIf(verfuegbar)("Recht auf Steuerbefehle per Mail", () => {
  beforeEach(leereDatenbank);

  it("lässt den zuständigen Hausmeister seinen Vorgang per Mail schließen", async () => {
    // Der eigentliche Zweck: Wer sich nie anmeldet, arbeitet trotzdem
    // vollständig – antworten, `#erledigt` darüber, fertig.
    const { ticket, hausmeister } = await aufbau();

    const pruefung = await pruefeBefehlsrecht({
      ticketId: ticket.id,
      absenderAdresse: "petra.klein@example.org",
      bestaetigterEmpfaengerId: hausmeister.id,
    });

    expect(pruefung.erlaubt).toBe(true);
    expect(pruefung.userId).toBe(hausmeister.id);
    expect(pruefung.name).toBe("Petra Klein");
  });

  it("verweigert den Befehl ohne gültiges Token", async () => {
    // Der Hauptfall: Die Mail wurde über Betreff oder Kopfzeile zugeordnet.
    // Dann fehlt jeder Nachweis, dass der Absender die Mail bekommen hat –
    // die Absenderadresse allein reicht ausdrücklich nicht.
    const { ticket } = await aufbau();

    const pruefung = await pruefeBefehlsrecht({
      ticketId: ticket.id,
      absenderAdresse: "petra.klein@example.org",
      bestaetigterEmpfaengerId: null,
    });

    expect(pruefung.erlaubt).toBe(false);
    expect(pruefung.grund).toMatch(/signierte Antwortadresse/);
  });

  it("verweigert den Befehl bei abweichender Absenderadresse", async () => {
    // Selbst ein abgefangenes Token nützt nichts, wenn man es mit der eigenen
    // Adresse benutzt.
    const { ticket, hausmeister } = await aufbau();

    const pruefung = await pruefeBefehlsrecht({
      ticketId: ticket.id,
      absenderAdresse: "fremde.person@example.net",
      bestaetigterEmpfaengerId: hausmeister.id,
    });

    expect(pruefung.erlaubt).toBe(false);
    expect(pruefung.grund).toMatch(/Absenderadresse/);
  });

  it("achtet nicht auf Groß- und Kleinschreibung der Adresse", async () => {
    // Manche Mailserver schreiben den Namensteil groß zurück. Das darf keinen
    // rechtmäßigen Befehl abweisen.
    const { ticket, hausmeister } = await aufbau();

    const pruefung = await pruefeBefehlsrecht({
      ticketId: ticket.id,
      absenderAdresse: "Petra.Klein@Example.ORG",
      bestaetigterEmpfaengerId: hausmeister.id,
    });

    expect(pruefung.erlaubt).toBe(true);
  });

  it("verweigert dem Melder Befehle auf seinen eigenen Vorgang", async () => {
    // Die Melderin hat ein gültiges Token – sie hat die Mail ja bekommen.
    // Trotzdem darf sie ihren Vorgang nicht selbst weiterleiten oder jemandem
    // zuweisen: Sie arbeitet nicht in diesem Bereich.
    const { ticket, melderin } = await aufbau();

    const pruefung = await pruefeBefehlsrecht({
      ticketId: ticket.id,
      absenderAdresse: "anna.bergmann@example.org",
      bestaetigterEmpfaengerId: melderin.id,
    });

    expect(pruefung.erlaubt).toBe(false);
    expect(pruefung.grund).toMatch(/arbeitet nicht im Bereich/);
  });

  it("verweigert Befehle aus einem fremden Bereich", async () => {
    const { ticket } = await aufbau();
    const andererBereich = await bereichAnlegen({ key: "IT", name: "IT" });
    const itKollege = await personAnlegen({
      name: "Frank Berger",
      email: "frank.berger@example.org",
    });
    await mitgliedschaftAnlegen(itKollege.id, andererBereich.id);

    const pruefung = await pruefeBefehlsrecht({
      ticketId: ticket.id,
      absenderAdresse: "frank.berger@example.org",
      bestaetigterEmpfaengerId: itKollege.id,
    });

    expect(pruefung.erlaubt).toBe(false);
  });

  it("verweigert Befehle eines stillgelegten Kontos", async () => {
    // Wer das Haus verlassen hat, soll auch mit einer alten Mail nichts mehr
    // auslösen können.
    const { team, ticket } = await aufbau();
    const ehemalig = await personAnlegen({
      name: "Ehemalige Kollegin",
      email: "ehemalig@example.org",
      isActive: false,
    });
    await mitgliedschaftAnlegen(ehemalig.id, team.id);

    const pruefung = await pruefeBefehlsrecht({
      ticketId: ticket.id,
      absenderAdresse: "ehemalig@example.org",
      bestaetigterEmpfaengerId: ehemalig.id,
    });

    expect(pruefung.erlaubt).toBe(false);
    expect(pruefung.grund).toMatch(/nicht aktiv/);
  });

  it("achtet auf die Abschaltung der Befehle im Bereich", async () => {
    const { ticket, hausmeister } = await aufbau({ allowEmailCommands: false });

    const pruefung = await pruefeBefehlsrecht({
      ticketId: ticket.id,
      absenderAdresse: "petra.klein@example.org",
      bestaetigterEmpfaengerId: hausmeister.id,
    });

    expect(pruefung.erlaubt).toBe(false);
    expect(pruefung.grund).toMatch(/abgeschaltet/);
  });

  it("weist einen unbekannten Vorgang ab", async () => {
    const { hausmeister } = await aufbau();

    const pruefung = await pruefeBefehlsrecht({
      ticketId: "gibt-es-nicht",
      absenderAdresse: "petra.klein@example.org",
      bestaetigterEmpfaengerId: hausmeister.id,
    });

    expect(pruefung.erlaubt).toBe(false);
  });
});

describe.runIf(verfuegbar)("Zielbereich aus #an <bereich> auflösen", () => {
  beforeEach(leereDatenbank);

  it("findet den Bereich am Kürzel und am Namen", async () => {
    const hm = await bereichAnlegen({ key: "HM", name: "Hausmeister" });

    expect((await findeZielBereich("HM"))?.id).toBe(hm.id);
    expect((await findeZielBereich("Hausmeister"))?.id).toBe(hm.id);
  });

  it("achtet nicht auf Groß- und Kleinschreibung", async () => {
    // Der Befehl kommt aus einer Mail; niemand tippt dort exakt.
    const hm = await bereichAnlegen({ key: "HM", name: "Hausmeister" });

    expect((await findeZielBereich("hausmeister"))?.id).toBe(hm.id);
    expect((await findeZielBereich("hm"))?.id).toBe(hm.id);
  });

  it("findet den Bereich auch am Wortanfang", async () => {
    const td = await bereichAnlegen({ key: "TD", name: "Technischer Dienst" });

    expect((await findeZielBereich("Technischer"))?.id).toBe(td.id);
  });

  it("liefert nichts bei leerer oder unbekannter Angabe", async () => {
    await bereichAnlegen({ key: "HM", name: "Hausmeister" });

    expect(await findeZielBereich("")).toBeNull();
    expect(await findeZielBereich("Kantine")).toBeNull();
  });

  it("übergeht stillgelegte Bereiche", async () => {
    await bereichAnlegen({ key: "ALT", name: "Alter Bereich", isActive: false });

    expect(await findeZielBereich("Alter Bereich")).toBeNull();
  });
});
