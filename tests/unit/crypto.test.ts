import { describe, expect, it } from "vitest";

import {
  buildReplyAddress,
  createReplyToken,
  decryptSecret,
  encryptSecret,
  extractReplyToken,
  verifyReplyToken,
} from "@/lib/crypto";

/**
 * Die Signatur der Antwortadresse ist die Stelle, an der ein Fehler am
 * teuersten wäre: Wer sie umgeht, kann fremde Vorgänge per gefälschter Mail
 * schließen oder an einen anderen Bereich umleiten. Entsprechend gründlich
 * wird hier geprüft, dass sich das Token nicht manipulieren lässt.
 */
describe("Verschlüsselung der Postfach-Kennwörter", () => {
  it("verschlüsselt und entschlüsselt verlustfrei", () => {
    const klartext = "Sehr geheimes Postfach-Kennwort mit Ümläuten!";
    const chiffre = encryptSecret(klartext);

    expect(chiffre).not.toContain(klartext);
    expect(decryptSecret(chiffre)).toBe(klartext);
  });

  it("erzeugt bei gleichem Klartext unterschiedliche Chiffren", () => {
    // Gleiche Chiffren würden verraten, dass zwei Postfächer dasselbe
    // Kennwort nutzen.
    const a = encryptSecret("dasselbe");
    const b = encryptSecret("dasselbe");

    expect(a).not.toBe(b);
    expect(decryptSecret(a)).toBe(decryptSecret(b));
  });

  it("weist eine veränderte Chiffre ab, statt Unsinn zurückzugeben", () => {
    const chiffre = encryptSecret("Kennwort");
    const teile = chiffre.split(".");

    // Ein Byte im Geheimtext kippen.
    const verfaelscht = Buffer.from(teile[3], "base64url");
    verfaelscht[0] ^= 0xff;
    teile[3] = verfaelscht.toString("base64url");

    expect(() => decryptSecret(teile.join("."))).toThrow();
  });

  it("weist ein unbekanntes Format ab", () => {
    expect(() => decryptSecret("nur-irgendein-text")).toThrow(/Format/);
  });
});

describe("Signierte Antwortadressen", () => {
  const payload = { ticketId: "ticket-123", recipientId: "user-456" };

  it("erzeugt ein Token, das sich wieder auflösen lässt", () => {
    const token = createReplyToken(payload);
    expect(verifyReplyToken(token)).toEqual(payload);
  });

  it("weist ein Token mit verfälschter Nutzlast ab", () => {
    const token = createReplyToken(payload);
    const [encoded, signatur] = token.split(".");

    // Die Nutzlast auf ein anderes Ticket umschreiben, Signatur behalten.
    const gefaelscht = Buffer.from("ticket-999~user-456", "utf8").toString(
      "base64url",
    );

    expect(verifyReplyToken(`${gefaelscht}.${signatur}`)).toBeNull();
    // Das unveränderte Token gilt weiterhin.
    expect(verifyReplyToken(`${encoded}.${signatur}`)).toEqual(payload);
  });

  it("weist ein Token mit verfälschter Signatur ab", () => {
    const token = createReplyToken(payload);
    const [encoded] = token.split(".");

    expect(verifyReplyToken(`${encoded}.AAAAAAAAAAAAAAAAAAAAAAAAAAA`)).toBeNull();
  });

  it("weist ein Token ohne Signatur ab", () => {
    const encoded = Buffer.from("ticket-1~user-1", "utf8").toString("base64url");
    expect(verifyReplyToken(encoded)).toBeNull();
  });

  it("bindet das Token an Ticket UND Empfänger", () => {
    // Zwei Empfänger desselben Tickets bekommen verschiedene Token – sonst
    // könnte einer im Namen des anderen handeln.
    const a = createReplyToken({ ticketId: "t1", recipientId: "u1" });
    const b = createReplyToken({ ticketId: "t1", recipientId: "u2" });

    expect(a).not.toBe(b);
    expect(verifyReplyToken(a)?.recipientId).toBe("u1");
    expect(verifyReplyToken(b)?.recipientId).toBe("u2");
  });

  it("baut eine Adresse, aus der sich das Token wieder herauslesen lässt", () => {
    const adresse = buildReplyAddress(payload);
    expect(adresse).toMatch(/^ticket\+.+@example\.org$/);

    const token = extractReplyToken(adresse!);
    expect(token).not.toBeNull();
    expect(verifyReplyToken(token!)).toEqual(payload);
  });

  it("liest das Token auch aus einer Adresse mit Anzeigenamen", () => {
    const adresse = buildReplyAddress(payload);
    const token = extractReplyToken(`Ticketsystem <${adresse}>`);

    expect(verifyReplyToken(token!)).toEqual(payload);
  });

  it("ignoriert Adressen mit falschem Namensteil", () => {
    // Eine Adresse wie "info+etwas@example.org" ist keine Antwortadresse.
    expect(extractReplyToken("info+abc.def@example.org")).toBeNull();
  });

  it("ignoriert Adressen ohne Plus-Teil", () => {
    expect(extractReplyToken("ticket@example.org")).toBeNull();
  });
});
