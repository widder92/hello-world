import {
  AssetEventType,
  AssetStatus,
  HandoverDirection,
  type Prisma,
} from "@prisma/client";

import { db } from "@/server/db";

/**
 * Das Inventar – Geräte, ihr Zustand und ihre Geschichte.
 *
 * Der Kern dieser Datei ist `statusWechseln`. Jede Änderung am Zustand eines
 * Geräts läuft dort hindurch, und zwar aus einem einzigen Grund: Ein Gerät
 * ohne lückenlose Chronik ist im Zweifelsfall wertlos. Wenn ein Notebook
 * verschwindet, zählt nicht, wo es heute steht, sondern wer es wann bekommen
 * und wer es wann zurückgegeben hat. Verteilte `db.asset.update()`-Aufrufe
 * würden diese Kette früher oder später zerreißen – meist genau an der Stelle,
 * die später wichtig wird.
 */

// ---------------------------------------------------------------------------
// MAC-Adressen
// ---------------------------------------------------------------------------

export class InventarFehler extends Error {
  constructor(message: string) {
    super(message);
    this.name = "InventarFehler";
  }
}

/**
 * Bringt eine MAC-Adresse auf die einheitliche Form `aa:bb:cc:dd:ee:ff`.
 *
 * Abgetippt wird sie in jeder erdenklichen Schreibweise: mit Doppelpunkten vom
 * Aufkleber, mit Bindestrichen aus Windows, in Vierergruppen von einem Switch,
 * ganz ohne Trenner aus einer Tabelle. Ohne Vereinheitlichung greift weder die
 * Eindeutigkeitsprüfung – dasselbe Gerät ließe sich zweimal anlegen – noch die
 * Suche, und gerade danach sucht man im Netzwerkfall.
 */
export function macNormalisieren(roh: string): string {
  const zeichen = roh.trim().toLowerCase().replace(/[\s:.-]/g, "");

  if (!/^[0-9a-f]{12}$/.test(zeichen)) {
    throw new InventarFehler(
      `„${roh.trim()}" ist keine gültige MAC-Adresse. Erwartet werden ` +
        `12 Stellen aus 0–9 und a–f, zum Beispiel 3c:52:82:11:0a:01.`,
    );
  }

  return (zeichen.match(/.{2}/g) ?? []).join(":");
}

/** Wie `macNormalisieren`, liefert aber `null` statt eines Fehlers. */
export function macNormalisierenWeich(roh: string): string | null {
  try {
    return macNormalisieren(roh);
  } catch {
    return null;
  }
}

// ---------------------------------------------------------------------------
// Nummernvergabe
// ---------------------------------------------------------------------------

interface ZaehlerZeile {
  value: number;
}

/**
 * Nächste Inventarnummer einer Geräteart, z. B. `NB-00042`.
 *
 * Gleiche Bauart wie die Ticketnummern (`services/number.ts`): Zählerzeile
 * sperren und hochzählen, damit zwei gleichzeitig angelegte Geräte keine
 * identische Nummer bekommen. Die Nummer klebt später als Aufkleber auf dem
 * Gerät – eine doppelte wäre nicht bloß unschön, sondern würde zwei Geräte
 * ununterscheidbar machen.
 */
export async function naechsteInventarnummer(
  tx: Prisma.TransactionClient,
  kindId: string,
  kindKey: string,
): Promise<string> {
  await tx.$executeRaw`
    INSERT INTO "AssetCounter" ("id", "kindId", "value")
    VALUES (gen_random_uuid()::text, ${kindId}, 0)
    ON CONFLICT ("kindId") DO NOTHING
  `;

  const rows = await tx.$queryRaw<ZaehlerZeile[]>`
    UPDATE "AssetCounter"
    SET "value" = "value" + 1
    WHERE "kindId" = ${kindId}
    RETURNING "value"
  `;

  const wert = rows[0]?.value;
  if (typeof wert !== "number") {
    throw new InventarFehler(
      `Die laufende Nummer für die Geräteart ${kindKey} konnte nicht ` +
        `ermittelt werden.`,
    );
  }

  return `${kindKey.toUpperCase()}-${String(wert).padStart(5, "0")}`;
}

/** Nächste Protokollnummer, z. B. `UP-2026-000031`. */
export async function naechsteProtokollnummer(
  tx: Prisma.TransactionClient,
  now: Date = new Date(),
): Promise<string> {
  const jahr = now.getFullYear();

  await tx.$executeRaw`
    INSERT INTO "HandoverCounter" ("id", "year", "value")
    VALUES (gen_random_uuid()::text, ${jahr}, 0)
    ON CONFLICT ("year") DO NOTHING
  `;

  const rows = await tx.$queryRaw<ZaehlerZeile[]>`
    UPDATE "HandoverCounter"
    SET "value" = "value" + 1
    WHERE "year" = ${jahr}
    RETURNING "value"
  `;

  const wert = rows[0]?.value;
  if (typeof wert !== "number") {
    throw new InventarFehler(
      "Die Protokollnummer konnte nicht ermittelt werden.",
    );
  }

  return `UP-${jahr}-${String(wert).padStart(6, "0")}`;
}

// ---------------------------------------------------------------------------
// Statusübergänge
// ---------------------------------------------------------------------------

export interface UebergangsRegel {
  /** Ob eine Person angegeben werden muss (Empfänger). */
  brauchtPerson?: boolean;
  /** Ob ein Grund angegeben werden muss. */
  brauchtGrund?: boolean;
  /** Ob dieser Übergang die Bereichsleitung verlangt. */
  nurLeitung?: boolean;
  /** Was mit einer bestehenden Zuweisung geschieht. */
  zuweisung: "setzen" | "loesen" | "behalten";
  /** Ob ein Übergabeprotokoll entsteht und in welche Richtung. */
  protokoll?: HandoverDirection;
}

/**
 * Was von welchem Zustand aus erlaubt ist.
 *
 * Bewusst als Tabelle und nicht als Kette von `if`-Zweigen: So lässt sich in
 * zehn Sekunden ablesen, was das System zulässt, und ein neuer Zustand
 * vergisst keinen Fall. Fehlt ein Paar in der Tabelle, ist der Übergang
 * verboten – das ist die sichere Vorgabe.
 */
export const UEBERGAENGE: Partial<
  Record<AssetStatus, Partial<Record<AssetStatus, UebergangsRegel>>>
> = {
  LAGER: {
    RESERVIERT: { brauchtPerson: true, zuweisung: "behalten" },
    HERAUSGEGEBEN: {
      brauchtPerson: true,
      zuweisung: "setzen",
      protokoll: HandoverDirection.AUSGABE,
    },
    REPARATUR: { zuweisung: "behalten" },
    AUSGESONDERT: { brauchtGrund: true, zuweisung: "loesen" },
    VERLUST: { brauchtGrund: true, zuweisung: "loesen" },
  },
  RESERVIERT: {
    LAGER: { zuweisung: "loesen" },
    HERAUSGEGEBEN: {
      brauchtPerson: true,
      zuweisung: "setzen",
      protokoll: HandoverDirection.AUSGABE,
    },
    AUSGESONDERT: { brauchtGrund: true, zuweisung: "loesen" },
    VERLUST: { brauchtGrund: true, zuweisung: "loesen" },
  },
  HERAUSGEGEBEN: {
    // Rücknahme: Das Gerät kommt zurück ins Lager und die Zuweisung endet.
    LAGER: { zuweisung: "loesen", protokoll: HandoverDirection.RUECKNAHME },
    REPARATUR: { zuweisung: "loesen", protokoll: HandoverDirection.RUECKNAHME },
    // Weitergabe an eine andere Person, ohne Umweg über das Lager.
    HERAUSGEGEBEN: {
      brauchtPerson: true,
      zuweisung: "setzen",
      protokoll: HandoverDirection.AUSGABE,
    },
    AUSGESONDERT: { brauchtGrund: true, zuweisung: "loesen" },
    VERLUST: { brauchtGrund: true, zuweisung: "loesen" },
  },
  REPARATUR: {
    LAGER: { zuweisung: "behalten" },
    HERAUSGEGEBEN: {
      brauchtPerson: true,
      zuweisung: "setzen",
      protokoll: HandoverDirection.AUSGABE,
    },
    AUSGESONDERT: { brauchtGrund: true, zuweisung: "loesen" },
    VERLUST: { brauchtGrund: true, zuweisung: "loesen" },
  },
  // Ein ausgesondertes Gerät zurückzuholen ist ein Sonderfall und soll
  // auffallen: Meist ist es entsorgt, und wer es wiederbelebt, muss dafür
  // geradestehen. Deshalb nur mit Leitungsrecht und mit Begründung.
  AUSGESONDERT: {
    LAGER: { brauchtGrund: true, nurLeitung: true, zuweisung: "behalten" },
  },
  VERLUST: {
    LAGER: { brauchtGrund: true, nurLeitung: true, zuweisung: "behalten" },
    HERAUSGEGEBEN: {
      brauchtPerson: true,
      nurLeitung: true,
      zuweisung: "setzen",
      protokoll: HandoverDirection.AUSGABE,
    },
  },
};

export function uebergangErlaubt(
  von: AssetStatus,
  nach: AssetStatus,
): UebergangsRegel | null {
  return UEBERGAENGE[von]?.[nach] ?? null;
}

/** Alle Zustände, die von hier aus erreichbar sind – für die Oberfläche. */
export function moeglicheZiele(von: AssetStatus): AssetStatus[] {
  return Object.keys(UEBERGAENGE[von] ?? {}) as AssetStatus[];
}

export interface StatusWechselOptionen {
  /** Empfänger bei einer Herausgabe. */
  userId?: string | null;
  /** Begründung – bei Aussonderung und Verlust Pflicht. */
  note?: string | null;
  /** Zustand des Geräts bei der Übergabe. */
  condition?: string | null;
  /** Mitgegebenes Zubehör. */
  accessories?: string | null;
  /** Ob die handelnde Person Leitungsrechte hat. */
  istLeitung?: boolean;
}

export interface InventarAkteur {
  id: string;
  name: string;
}

/**
 * Ändert den Zustand eines Geräts und schreibt die Chronik dazu.
 *
 * Läuft vollständig in einer Transaktion: Entweder stimmen Gerät, Chronik und
 * Protokoll zusammen, oder es ändert sich nichts. Ein Gerät, das laut Datenbank
 * herausgegeben ist, für das aber kein Protokoll existiert, wäre schlimmer als
 * ein fehlgeschlagener Klick.
 */
export async function statusWechseln(
  assetId: string,
  ziel: AssetStatus,
  optionen: StatusWechselOptionen,
  actor: InventarAkteur,
) {
  return db.$transaction(async (tx) => {
    const geraet = await tx.asset.findUnique({
      where: { id: assetId },
      select: {
        id: true,
        assetTag: true,
        name: true,
        status: true,
        assignedToId: true,
      },
    });

    if (!geraet) {
      throw new InventarFehler("Das Gerät wurde nicht gefunden.");
    }

    if (geraet.status === ziel && ziel !== AssetStatus.HERAUSGEGEBEN) {
      throw new InventarFehler(
        `${geraet.assetTag} steht bereits auf diesem Status.`,
      );
    }

    const regel = uebergangErlaubt(geraet.status, ziel);
    if (!regel) {
      throw new InventarFehler(
        `Von „${geraet.status}" nach „${ziel}" ist kein zulässiger Schritt.`,
      );
    }

    if (regel.nurLeitung && !optionen.istLeitung) {
      throw new InventarFehler(
        "Diesen Schritt darf nur die Bereichsleitung gehen.",
      );
    }

    const grund = optionen.note?.trim() || null;
    if (regel.brauchtGrund && !grund) {
      throw new InventarFehler(
        "Bitte halten Sie fest, warum – ohne Begründung lässt sich das " +
          "später nicht mehr nachvollziehen.",
      );
    }

    let empfaenger: { id: string; name: string } | null = null;
    if (regel.brauchtPerson) {
      if (!optionen.userId) {
        throw new InventarFehler("Bitte wählen Sie die Person aus.");
      }
      empfaenger = await tx.user.findFirst({
        where: { id: optionen.userId, isActive: true },
        select: { id: true, name: true },
      });
      if (!empfaenger) {
        throw new InventarFehler("Die gewählte Person wurde nicht gefunden.");
      }
      if (
        ziel === AssetStatus.HERAUSGEGEBEN &&
        geraet.assignedToId === empfaenger.id
      ) {
        throw new InventarFehler(
          `${geraet.assetTag} ist dieser Person bereits zugewiesen.`,
        );
      }
    }

    // Wer das Gerät bisher hatte – für das Rücknahmeprotokoll.
    const bisher = geraet.assignedToId
      ? await tx.user.findUnique({
          where: { id: geraet.assignedToId },
          select: { id: true, name: true },
        })
      : null;

    const jetzt = new Date();

    const zuweisung =
      regel.zuweisung === "setzen"
        ? { assignedToId: empfaenger!.id, assignedAt: jetzt }
        : regel.zuweisung === "loesen"
          ? { assignedToId: null, assignedAt: null }
          : {};

    const aussortiert =
      ziel === AssetStatus.AUSGESONDERT || ziel === AssetStatus.VERLUST;

    const aktualisiert = await tx.asset.update({
      where: { id: geraet.id },
      data: {
        status: ziel,
        ...zuweisung,
        retiredAt: aussortiert ? jetzt : null,
        retiredReason: aussortiert ? grund : null,
      },
      select: { id: true, assetTag: true, name: true, status: true },
    });

    // Protokoll. Bei einer Rücknahme steht der bisherige Besitzer darauf, bei
    // einer Ausgabe der neue – deshalb nicht dieselbe Person.
    let protokoll: { id: string; protocolNo: string } | null = null;
    const protokollPerson =
      regel.protokoll === HandoverDirection.RUECKNAHME ? bisher : empfaenger;

    if (regel.protokoll && protokollPerson) {
      const nummer = await naechsteProtokollnummer(tx, jetzt);
      protokoll = await tx.assetHandover.create({
        data: {
          protocolNo: nummer,
          assetId: geraet.id,
          userId: protokollPerson.id,
          userName: protokollPerson.name,
          direction: regel.protokoll,
          handedOverAt: jetzt,
          actorId: actor.id,
          actorName: actor.name,
          condition: optionen.condition?.trim() || null,
          accessories: optionen.accessories?.trim() || null,
          note: grund,
        },
        select: { id: true, protocolNo: true },
      });
    }

    await tx.assetEvent.create({
      data: {
        assetId: geraet.id,
        type:
          regel.protokoll === HandoverDirection.AUSGABE
            ? AssetEventType.HERAUSGEGEBEN
            : regel.protokoll === HandoverDirection.RUECKNAHME
              ? AssetEventType.ZURUECKGENOMMEN
              : AssetEventType.STATUS,
        fromStatus: geraet.status,
        toStatus: ziel,
        userId: protokollPerson?.id ?? null,
        actorId: actor.id,
        actorName: actor.name,
        note: protokoll
          ? [grund, `Übergabeprotokoll ${protokoll.protocolNo}`]
              .filter(Boolean)
              .join(" – ")
          : grund,
        createdAt: jetzt,
      },
    });

    return { geraet: aktualisiert, protokoll };
  });
}

/** Vermerk in der Chronik ohne Statusänderung. */
export async function chronikVermerk(
  assetId: string,
  type: AssetEventType,
  actor: InventarAkteur,
  note?: string | null,
) {
  return db.assetEvent.create({
    data: {
      assetId,
      type,
      actorId: actor.id,
      actorName: actor.name,
      note: note?.trim() || null,
    },
  });
}

// ---------------------------------------------------------------------------
// Geld
// ---------------------------------------------------------------------------

/**
 * Liest einen eingetippten Betrag als Cent.
 *
 * Nimmt „1.299,00", „1299", „1299.00", „1.299" und „1.299 €".
 *
 * Der Punkt ist der schwierige Teil, weil er beides sein kann: In „12.99"
 * (aus einem Lieferantenexport) trennt er die Nachkommastellen, in „1.299"
 * (deutsch getippt) die Tausender. Beides ist eine einzelne Zahl mit einem
 * Punkt, und die Verwechslung ist teuer – aus 1.299 € würden 1,30 €, was
 * erst in der Jahresauswertung auffällt.
 *
 * Die Unterscheidung liegt in der Stellenzahl dahinter: Genau drei Ziffern
 * nach dem Punkt gibt es bei Preisen nur als Tausendertrennung, zwei oder
 * eine nur als Nachkommastellen.
 */
export function betragZuCent(eingabe: string): number | null {
  const roh = eingabe.trim().replace(/\s|€|EUR/gi, "");
  if (!roh) return null;

  let normalisiert = roh;
  if (roh.includes(",")) {
    // Komma ist das Dezimaltrennzeichen, Punkte sind Tausendertrenner.
    normalisiert = roh.replace(/\./g, "").replace(",", ".");
  } else if ((roh.match(/\./g) ?? []).length > 1) {
    // Mehrere Punkte können nur Tausendertrenner sein.
    normalisiert = roh.replace(/\./g, "");
  } else if (/\.\d{3}$/.test(roh)) {
    // Ein Punkt mit genau drei Ziffern dahinter: Tausendertrenner.
    normalisiert = roh.replace(".", "");
  }

  const zahl = Number(normalisiert);
  if (!Number.isFinite(zahl) || zahl < 0) {
    throw new InventarFehler(
      `„${eingabe.trim()}" ist kein gültiger Betrag. Beispiel: 1.299,00`,
    );
  }

  return Math.round(zahl * 100);
}

/** Formatiert Cent als deutschen Eurobetrag. */
export function centAlsEuro(cent: number | null | undefined): string {
  if (cent === null || cent === undefined) return "–";
  return new Intl.NumberFormat("de-DE", {
    style: "currency",
    currency: "EUR",
  }).format(cent / 100);
}
