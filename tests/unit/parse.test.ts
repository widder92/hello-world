import { describe, expect, it } from "vitest";

import {
  betreffBereinigen,
  entferneZitate,
  htmlZuText,
  istAutomatischeAntwort,
  nachrichtentext,
} from "@/mail/parse";
import type { EingehendeNachricht } from "@/mail/types";

/**
 * Die Aufbereitung eingehender Mails entscheidet darüber, ob ein Vorgang nach
 * fünf Antworten noch lesbar ist. Schneidet sie zu wenig ab, steht unter jeder
 * Antwort noch einmal der gesamte bisherige Verlauf; schneidet sie zu viel ab,
 * fehlt der eigentliche Text.
 */

function mail(teil: Partial<EingehendeNachricht> = {}): EingehendeNachricht {
  return {
    quellId: "1",
    messageId: "<a@example.org>",
    inReplyTo: null,
    references: [],
    betreff: "Heizung im Gruppenraum",
    vonAdresse: "anna.bergmann@example.org",
    vonName: "Anna Bergmann",
    anAdressen: ["hausmeister@example.org"],
    ccAdressen: [],
    text: null,
    html: null,
    empfangenAm: new Date("2026-08-03T09:00:00Z"),
    autoSubmitted: null,
    precedence: null,
    anhaenge: [],
    ...teil,
  };
}

describe("Zitate abschneiden", () => {
  it("schneidet den Outlook-Kopf einer Antwort ab", () => {
    const text = [
      "Danke, das hat geholfen.",
      "",
      "Von: Ticketsystem <ticket@example.org>",
      "Gesendet: Montag, 3. August 2026 09:12",
      "An: Anna Bergmann",
      "Betreff: [IT-2026-000001] PC startet nicht",
      "",
      "Guten Tag, wir haben Ihren Vorgang aufgenommen.",
    ].join("\n");

    expect(entferneZitate(text)).toBe("Danke, das hat geholfen.");
  });

  it("erkennt den deutschen Trennstrich von Outlook", () => {
    const text = [
      "Der Schlüssel liegt jetzt im Büro.",
      "",
      "-----Ursprüngliche Nachricht-----",
      "Von: Ticketsystem",
      "",
      "Bitte Rückmeldung.",
    ].join("\n");

    expect(entferneZitate(text)).toBe("Der Schlüssel liegt jetzt im Büro.");
  });

  it("erkennt die Zeile von Thunderbird und Apple Mail", () => {
    const text = [
      "Passt so, vielen Dank.",
      "",
      "Am 03.08.2026 um 09:12 schrieb Ticketsystem:",
      "> Guten Tag,",
      "> wir haben den Vorgang aufgenommen.",
    ].join("\n");

    expect(entferneZitate(text)).toBe("Passt so, vielen Dank.");
  });

  it("schneidet angehängte Zitatzeilen auch ohne Kopfzeile ab", () => {
    const text = [
      "Ist erledigt.",
      "",
      "> Guten Tag,",
      "> bitte um Rückmeldung.",
      "",
    ].join("\n");

    expect(entferneZitate(text)).toBe("Ist erledigt.");
  });

  it("lässt einen Text ohne Zitat unverändert", () => {
    const text = "Im Gruppenraum 2 tropft der Wasserhahn.\nBitte kurz schauen.";
    expect(entferneZitate(text)).toBe(text);
  });

  it("behält Zitatzeilen mitten im Text", () => {
    // Nur zusammenhängende Zitatzeilen *am Ende* werden abgeschnitten. Wer
    // mitten in seiner Antwort zitiert und darunter weiterschreibt, meint das
    // so – dieser Teil gehört in den Vorgang.
    const text = [
      "> Ist der Beamer schon bestellt?",
      "",
      "Ja, Lieferung nächste Woche.",
    ].join("\n");

    expect(entferneZitate(text)).toBe(text);
  });

  it("behält den ganzen Text, wenn nach dem Abschneiden nichts bleibt", () => {
    // Eine Antwort, die nur aus dem Zitat besteht. Eine leere Nachricht im
    // Verlauf wäre schlechter als eine mit zu viel Inhalt.
    const roh = "> Guten Tag,\n> bitte um Rückmeldung.";
    expect(nachrichtentext(mail({ text: roh }))).toBe(roh);
  });
});

describe("HTML zu Text", () => {
  it("erhält Absätze und Aufzählungen", () => {
    const html =
      "<p>Guten Tag,</p><ul><li>Beamer defekt</li><li>Kabel fehlt</li></ul>";
    const text = htmlZuText(html);

    expect(text).toContain("Guten Tag,");
    expect(text).toContain("• Beamer defekt");
    expect(text).toContain("• Kabel fehlt");
  });

  it("entfernt Skriptblöcke samt Inhalt", () => {
    // Was aus einer fremden Mail kommt, wird als reiner Text abgelegt. Ohne
    // diesen Schritt stünde der Skriptinhalt im Verlauf – zwar harmlos, weil
    // als Text ausgegeben, aber unbrauchbar zu lesen.
    const text = htmlZuText(
      "<p>Hallo</p><script>alert('boes')</script><style>p{color:red}</style>",
    );

    expect(text).toBe("Hallo");
    expect(text).not.toContain("alert");
    expect(text).not.toContain("color");
  });

  it("löst deutsche Entitäten auf", () => {
    expect(htmlZuText("<p>T&uuml;r &amp; Tor gepr&uuml;ft, Ma&szlig; passt</p>")).toBe(
      "Tür & Tor geprüft, Maß passt",
    );
  });

  it("nimmt HTML, wenn kein Textteil vorhanden ist", () => {
    const ergebnis = nachrichtentext(
      mail({ text: null, html: "<p>Nur als HTML geschickt.</p>" }),
    );
    expect(ergebnis).toBe("Nur als HTML geschickt.");
  });
});

describe("Automatische Antworten", () => {
  it("erkennt die Kopfzeile Auto-Submitted", () => {
    expect(istAutomatischeAntwort(mail({ autoSubmitted: "auto-replied" }))).toBe(
      true,
    );
  });

  it("lässt Auto-Submitted: no durch", () => {
    expect(istAutomatischeAntwort(mail({ autoSubmitted: "no" }))).toBe(false);
  });

  it("erkennt Precedence: bulk", () => {
    expect(istAutomatischeAntwort(mail({ precedence: "bulk" }))).toBe(true);
  });

  it("erkennt eine Abwesenheitsnotiz am Betreff", () => {
    // Nicht jedes Mailprogramm setzt die vorgesehenen Kopfzeilen. Ohne diese
    // zweite Prüfung entstünde aus jeder Urlaubsmeldung ein Vorgang – und aus
    // unserer Eingangsbestätigung darauf die nächste.
    expect(
      istAutomatischeAntwort(
        mail({ betreff: "Automatische Antwort: Heizung im Gruppenraum" }),
      ),
    ).toBe(true);
    expect(istAutomatischeAntwort(mail({ betreff: "Abwesenheitsnotiz" }))).toBe(
      true,
    );
  });

  it("erkennt einen Unzustellbarkeitsbericht", () => {
    expect(
      istAutomatischeAntwort(mail({ betreff: "Undeliverable: Ihre Nachricht" })),
    ).toBe(true);
  });

  it("hält eine gewöhnliche Meldung nicht für automatisch", () => {
    expect(istAutomatischeAntwort(mail())).toBe(false);
  });
});

describe("Betreff bereinigen", () => {
  it("entfernt gestapelte Antwortpräfixe", () => {
    expect(betreffBereinigen("AW: WG: AW: Heizung defekt")).toBe(
      "Heizung defekt",
    );
  });

  it("entfernt auch das gezählte Präfix von Outlook", () => {
    expect(betreffBereinigen("AW[2]: Heizung defekt")).toBe("Heizung defekt");
  });

  it("entfernt die Ticketkennung", () => {
    expect(betreffBereinigen("AW: [HM-2026-000087] Heizung defekt")).toBe(
      "Heizung defekt",
    );
  });

  it("liefert einen Ersatz, wenn nichts übrig bleibt", () => {
    // Ein Vorgang ohne Betreff wäre in jeder Liste eine leere Zeile.
    expect(betreffBereinigen("AW:")).toBe("(ohne Betreff)");
    expect(betreffBereinigen("   ")).toBe("(ohne Betreff)");
  });

  it("lässt einen sauberen Betreff unverändert", () => {
    expect(betreffBereinigen("Wasserhahn tropft in Gruppenraum 2")).toBe(
      "Wasserhahn tropft in Gruppenraum 2",
    );
  });
});
