import { AssetStatus } from "@prisma/client";
import { describe, expect, it } from "vitest";

import {
  InventarFehler,
  betragZuCent,
  centAlsEuro,
  macNormalisieren,
  macNormalisierenWeich,
  moeglicheZiele,
  uebergangErlaubt,
} from "@/server/services/inventar";

/**
 * Die drei Stellen im Inventar, an denen sich ein Fehler still auswirkt:
 * MAC-Adressen (dasselbe Gerät zweimal angelegt), Beträge (Auswertung
 * daneben) und Statusübergänge (ein Gerät, das nirgends verbucht ist).
 */

describe("macNormalisieren", () => {
  it("erkennt alle gängigen Schreibweisen", () => {
    // Alle vier Formen stehen für dieselbe Adresse: vom Aufkleber, aus
    // Windows, von einem Switch und aus einer Tabelle ohne Trenner.
    const erwartet = "3c:52:82:11:0a:01";

    expect(macNormalisieren("3c:52:82:11:0a:01")).toBe(erwartet);
    expect(macNormalisieren("3C-52-82-11-0A-01")).toBe(erwartet);
    expect(macNormalisieren("3c52.8211.0a01")).toBe(erwartet);
    expect(macNormalisieren("3C5282110A01")).toBe(erwartet);
    expect(macNormalisieren("  3c:52:82:11:0A:01  ")).toBe(erwartet);
  });

  it("weist ungültige Eingaben zurück", () => {
    expect(() => macNormalisieren("")).toThrow(InventarFehler);
    // Zu kurz – eine Stelle fehlt.
    expect(() => macNormalisieren("3c:52:82:11:0a:0")).toThrow(InventarFehler);
    // Zu lang.
    expect(() => macNormalisieren("3c:52:82:11:0a:01:99")).toThrow(InventarFehler);
    // "g" ist keine Hexadezimalziffer.
    expect(() => macNormalisieren("3g:52:82:11:0a:01")).toThrow(InventarFehler);
    expect(() => macNormalisieren("Kein Netzwerk")).toThrow(InventarFehler);
  });

  it("nennt die Eingabe in der Fehlermeldung", () => {
    // Wer zehn Adressen abtippt, muss sehen, welche gemeint ist.
    expect(() => macNormalisieren("00:11:22")).toThrow(/00:11:22/);
  });

  it("liefert in der weichen Form null statt eines Fehlers", () => {
    expect(macNormalisierenWeich("3C-52-82-11-0A-01")).toBe("3c:52:82:11:0a:01");
    // Die Suche gibt beliebigen Text hinein – dort darf nichts fliegen.
    expect(macNormalisierenWeich("Notebook Anna")).toBeNull();
  });
});

describe("betragZuCent", () => {
  it("liest deutsche Beträge", () => {
    expect(betragZuCent("1.299,00")).toBe(129900);
    expect(betragZuCent("1299,00")).toBe(129900);
    expect(betragZuCent("12,99")).toBe(1299);
    expect(betragZuCent("1.299,00 €")).toBe(129900);
    expect(betragZuCent("  219 EUR ")).toBe(21900);
  });

  it("behandelt den Punkt als Tausendertrenner, nicht als Komma", () => {
    // Der eigentliche Prüfstein: Wer "1.299" als 1,299 liest, macht aus
    // 1.299 Euro einen Betrag von 1,30 Euro – und merkt es erst in der
    // Jahresauswertung.
    expect(betragZuCent("1.299")).toBe(129900);
    expect(betragZuCent("1.234.567")).toBe(123456700);
  });

  it("nimmt auch die englische Schreibweise mit einem Punkt", () => {
    // Kommt aus Exporten von Lieferantenportalen.
    expect(betragZuCent("1299.00")).toBe(129900);
    expect(betragZuCent("12.99")).toBe(1299);
  });

  it("liefert null bei leerer Eingabe", () => {
    expect(betragZuCent("")).toBeNull();
    expect(betragZuCent("   ")).toBeNull();
  });

  it("weist Unsinn und negative Beträge zurück", () => {
    expect(() => betragZuCent("teuer")).toThrow(InventarFehler);
    expect(() => betragZuCent("-50")).toThrow(InventarFehler);
  });

  it("rundet auf ganze Cent", () => {
    // Gleitkomma-Reste dürfen nicht in die Datenbank wandern.
    expect(betragZuCent("0,105")).toBe(11);
    expect(Number.isInteger(betragZuCent("19,99"))).toBe(true);
  });
});

describe("centAlsEuro", () => {
  it("formatiert deutsch", () => {
    // Nicht auf das Leerzeichen vor dem Euro festnageln – Intl setzt dort
    // je nach Node-Version ein schmales oder ein normales.
    expect(centAlsEuro(129900).replace(/\s/g, " ")).toBe("1.299,00 €");
    expect(centAlsEuro(0).replace(/\s/g, " ")).toBe("0,00 €");
  });

  it("zeigt einen Strich, wenn kein Preis erfasst ist", () => {
    // Wichtiger als es aussieht: "0,00 €" würde behaupten, das Gerät sei
    // umsonst gewesen.
    expect(centAlsEuro(null)).toBe("–");
    expect(centAlsEuro(undefined)).toBe("–");
  });
});

describe("Statusübergänge", () => {
  it("erlaubt den Alltagsweg Lager → herausgegeben → zurück", () => {
    const raus = uebergangErlaubt(AssetStatus.LAGER, AssetStatus.HERAUSGEGEBEN);
    expect(raus).not.toBeNull();
    expect(raus!.brauchtPerson).toBe(true);
    expect(raus!.zuweisung).toBe("setzen");
    expect(raus!.protokoll).toBe("AUSGABE");

    const zurueck = uebergangErlaubt(
      AssetStatus.HERAUSGEGEBEN,
      AssetStatus.LAGER,
    );
    expect(zurueck).not.toBeNull();
    expect(zurueck!.zuweisung).toBe("loesen");
    expect(zurueck!.protokoll).toBe("RUECKNAHME");
  });

  it("erlaubt die Weitergabe an eine andere Person ohne Umweg über das Lager", () => {
    // Kommt ständig vor: Jemand geht, die Nachfolge übernimmt das Notebook.
    const weiter = uebergangErlaubt(
      AssetStatus.HERAUSGEGEBEN,
      AssetStatus.HERAUSGEGEBEN,
    );
    expect(weiter).not.toBeNull();
    expect(weiter!.brauchtPerson).toBe(true);
  });

  it("verlangt für Aussonderung und Verlust eine Begründung", () => {
    for (const von of [
      AssetStatus.LAGER,
      AssetStatus.HERAUSGEGEBEN,
      AssetStatus.REPARATUR,
    ]) {
      expect(uebergangErlaubt(von, AssetStatus.AUSGESONDERT)?.brauchtGrund).toBe(
        true,
      );
      expect(uebergangErlaubt(von, AssetStatus.VERLUST)?.brauchtGrund).toBe(true);
    }
  });

  it("löst die Zuweisung, wenn ein Gerät ausgesondert wird", () => {
    // Sonst stünde das Gerät weiter unter Meine Geräte, obwohl es entsorgt ist.
    expect(
      uebergangErlaubt(AssetStatus.HERAUSGEGEBEN, AssetStatus.AUSGESONDERT)
        ?.zuweisung,
    ).toBe("loesen");
  });

  it("lässt die Rückholung aus der Aussonderung nur der Leitung", () => {
    const zurueck = uebergangErlaubt(AssetStatus.AUSGESONDERT, AssetStatus.LAGER);
    expect(zurueck?.nurLeitung).toBe(true);
    expect(zurueck?.brauchtGrund).toBe(true);
  });

  it("verbietet, was nicht in der Tabelle steht", () => {
    // Ein ausgesondertes Gerät direkt an jemanden herausgeben – nein.
    expect(
      uebergangErlaubt(AssetStatus.AUSGESONDERT, AssetStatus.HERAUSGEGEBEN),
    ).toBeNull();
    // Aus dem Lager direkt in den Verlust ist erlaubt, aber nicht umgekehrt
    // ohne Leitungsrecht – geprüft wird hier nur, dass der Rückweg aus der
    // Reparatur in die Reservierung fehlt.
    expect(
      uebergangErlaubt(AssetStatus.REPARATUR, AssetStatus.RESERVIERT),
    ).toBeNull();
  });

  it("nennt für jeden Zustand die erreichbaren Ziele", () => {
    expect(moeglicheZiele(AssetStatus.LAGER)).toContain(
      AssetStatus.HERAUSGEGEBEN,
    );
    expect(moeglicheZiele(AssetStatus.LAGER)).not.toContain(AssetStatus.LAGER);

    // Aus der Aussonderung führt genau ein Weg zurück.
    expect(moeglicheZiele(AssetStatus.AUSGESONDERT)).toEqual([
      AssetStatus.LAGER,
    ]);
  });
});
