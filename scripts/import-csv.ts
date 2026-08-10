/**
 * Datenübernahme aus CSV.
 *
 * Gedacht für den Wechsel vom Altsystem. Bewusst über CSV statt über eine
 * direkte Anbindung: Damit kommt man aus praktisch jedem Vorsystem heraus –
 * OTRS, osTicket, Jira, eine SharePoint-Liste oder eine gewachsene
 * Excel-Tabelle –, ohne dass dieses System deren Schnittstelle kennen muss.
 *
 * Aufruf:
 *   npm run import -- --typ=benutzer --datei=benutzer.csv --dry-run
 *   npm run import -- --typ=standorte --datei=standorte.csv
 *   npm run import -- --typ=tickets   --datei=alttickets.csv
 *
 * Der Trockenlauf (`--dry-run`) schreibt nichts, prüft aber alle Zeilen und
 * meldet, was schiefginge. Immer zuerst so laufen lassen.
 *
 * Erwartete Spalten stehen in BETRIEB.md und werden bei falschem Aufbau
 * zusammen mit der Fehlermeldung ausgegeben.
 */

import { readFileSync } from "node:fs";

import { parse } from "csv-parse/sync";
import {
  AssetStatus,
  PrismaClient,
  SiteType,
  TicketSource,
  TicketStatus,
} from "@prisma/client";

import { betragZuCent, macNormalisieren } from "../src/server/services/inventar";

import { formatTicketNumber } from "../src/server/services/number";

const db = new PrismaClient();

// ---------------------------------------------------------------------------
// Aufrufparameter
// ---------------------------------------------------------------------------

interface Optionen {
  typ: "benutzer" | "standorte" | "geraete" | "tickets";
  datei: string;
  trockenlauf: boolean;
  trennzeichen: string;
}

function leseOptionen(): Optionen {
  const args = process.argv.slice(2);
  const werte = new Map<string, string>();
  let trockenlauf = false;

  for (const arg of args) {
    if (arg === "--dry-run" || arg === "--trockenlauf") {
      trockenlauf = true;
      continue;
    }
    const treffer = arg.match(/^--([^=]+)=(.*)$/);
    if (treffer) werte.set(treffer[1], treffer[2]);
  }

  const typ = werte.get("typ");
  const datei = werte.get("datei") ?? args.find((a) => !a.startsWith("--"));

  if (!typ || !datei) {
    console.error(
      [
        "Aufruf:",
        "  npm run import -- --typ=<art> --datei=<pfad.csv> [--dry-run] [--trennzeichen=;]",
        "",
        "Arten:",
        "  benutzer    E-Mail, Name, Funktion, Abteilung, Standort, Telefon",
        "  standorte   Name, Kurzname, Art, Strasse, PLZ, Ort",
        "  geraete     Nummer, Name, Art, Hersteller, Modell, Seriennummer, Standort, Raum, Zugewiesen",
        "  tickets     Nummer, Betreff, Beschreibung, Bereich, Standort, Melder, Status, Erstellt, Erledigt",
        "",
        "Immer zuerst mit --dry-run laufen lassen.",
      ].join("\n"),
    );
    process.exit(1);
  }

  if (!["benutzer", "standorte", "geraete", "tickets"].includes(typ)) {
    console.error(`Unbekannte Art "${typ}".`);
    process.exit(1);
  }

  return {
    typ: typ as Optionen["typ"],
    datei,
    trockenlauf,
    // Deutsche Excel-Exporte nutzen üblicherweise das Semikolon.
    trennzeichen: werte.get("trennzeichen") ?? ";",
  };
}

// ---------------------------------------------------------------------------
// Hilfsmittel
// ---------------------------------------------------------------------------

type Zeile = Record<string, string>;

interface Bericht {
  gelesen: number;
  angelegt: number;
  aktualisiert: number;
  uebersprungen: number;
  fehler: { zeile: number; grund: string }[];
}

const leererBericht = (): Bericht => ({
  gelesen: 0,
  angelegt: 0,
  aktualisiert: 0,
  uebersprungen: 0,
  fehler: [],
});

/**
 * Liest eine Spalte unabhängig von Groß-/Kleinschreibung und Umlautschreibung.
 * Exporte aus Altsystemen sind bei Spaltennamen erfahrungsgemäß kreativ.
 */
function spalte(zeile: Zeile, ...namen: string[]): string {
  for (const name of namen) {
    for (const [key, wert] of Object.entries(zeile)) {
      const normalisiert = key
        .toLowerCase()
        .replace(/ä/g, "ae")
        .replace(/ö/g, "oe")
        .replace(/ü/g, "ue")
        .replace(/ß/g, "ss")
        .replace(/[^a-z0-9]/g, "");
      const gesucht = name
        .toLowerCase()
        .replace(/ä/g, "ae")
        .replace(/ö/g, "oe")
        .replace(/ü/g, "ue")
        .replace(/ß/g, "ss")
        .replace(/[^a-z0-9]/g, "");
      if (normalisiert === gesucht) return (wert ?? "").trim();
    }
  }
  return "";
}

/** Erkennt die gängigen deutschen Datumsformate. */
function leseDatum(wert: string): Date | null {
  if (!wert) return null;

  // 31.12.2025 oder 31.12.2025 14:30
  const de = wert.match(
    /^(\d{1,2})\.(\d{1,2})\.(\d{4})(?:\s+(\d{1,2}):(\d{2}))?/,
  );
  if (de) {
    return new Date(
      Number(de[3]),
      Number(de[2]) - 1,
      Number(de[1]),
      Number(de[4] ?? 0),
      Number(de[5] ?? 0),
    );
  }

  const iso = new Date(wert);
  return Number.isNaN(iso.getTime()) ? null : iso;
}

// ---------------------------------------------------------------------------
// Import je Art
// ---------------------------------------------------------------------------

async function importiereStandorte(
  zeilen: Zeile[],
  trocken: boolean,
): Promise<Bericht> {
  const bericht = leererBericht();

  const artZuordnung: Record<string, SiteType> = {
    schule: SiteType.SCHULE,
    wohnheim: SiteType.WOHNHEIM,
    betreuteswohnen: SiteType.BETREUTES_WOHNEN,
    verwaltung: SiteType.VERWALTUNG,
    werkstatt: SiteType.WERKSTATT,
  };

  for (const [i, zeile] of zeilen.entries()) {
    bericht.gelesen++;
    const nummer = i + 2; // Kopfzeile ist Zeile 1

    const name = spalte(zeile, "name", "standort", "bezeichnung");
    if (!name) {
      bericht.fehler.push({ zeile: nummer, grund: "Spalte Name ist leer." });
      continue;
    }

    const artText = spalte(zeile, "art", "typ")
      .toLowerCase()
      .replace(/[^a-z]/g, "");
    const art = artZuordnung[artText] ?? SiteType.SONSTIGE;

    const daten = {
      name,
      shortName: spalte(zeile, "kurzname", "kurz") || null,
      type: art,
      street: spalte(zeile, "strasse", "straße", "adresse") || null,
      zip: spalte(zeile, "plz", "postleitzahl") || null,
      city: spalte(zeile, "ort", "stadt") || null,
    };

    const vorhanden = await db.site.findUnique({
      where: { name },
      select: { id: true },
    });

    if (trocken) {
      if (vorhanden) bericht.aktualisiert++;
      else bericht.angelegt++;
      continue;
    }

    if (vorhanden) {
      await db.site.update({ where: { id: vorhanden.id }, data: daten });
      bericht.aktualisiert++;
    } else {
      await db.site.create({ data: daten });
      bericht.angelegt++;
    }
  }

  return bericht;
}

async function importiereBenutzer(
  zeilen: Zeile[],
  trocken: boolean,
): Promise<Bericht> {
  const bericht = leererBericht();

  const standorte = new Map(
    (await db.site.findMany({ select: { id: true, name: true } })).map((s) => [
      s.name.toLowerCase(),
      s.id,
    ]),
  );

  for (const [i, zeile] of zeilen.entries()) {
    bericht.gelesen++;
    const nummer = i + 2;

    const email = spalte(zeile, "email", "emailadresse", "mail").toLowerCase();
    const name = spalte(zeile, "name", "vollername", "anzeigename");

    if (!email || !email.includes("@")) {
      bericht.fehler.push({
        zeile: nummer,
        grund: `Keine gültige E-Mail-Adresse ("${email}").`,
      });
      continue;
    }
    if (!name) {
      bericht.fehler.push({ zeile: nummer, grund: "Spalte Name ist leer." });
      continue;
    }

    const standortName = spalte(zeile, "standort", "einrichtung", "haus");
    const siteId = standortName
      ? (standorte.get(standortName.toLowerCase()) ?? null)
      : null;

    if (standortName && !siteId) {
      bericht.fehler.push({
        zeile: nummer,
        grund: `Standort "${standortName}" ist nicht angelegt. Bitte zuerst die Standorte übernehmen.`,
      });
      continue;
    }

    const daten = {
      name,
      jobTitle: spalte(zeile, "funktion", "position", "taetigkeit") || null,
      department: spalte(zeile, "abteilung", "bereich") || null,
      phone: spalte(zeile, "telefon", "tel", "durchwahl") || null,
      siteId,
    };

    const vorhanden = await db.user.findUnique({
      where: { email },
      select: { id: true },
    });

    if (trocken) {
      if (vorhanden) bericht.aktualisiert++;
      else bericht.angelegt++;
      continue;
    }

    if (vorhanden) {
      await db.user.update({ where: { id: vorhanden.id }, data: daten });
      bericht.aktualisiert++;
    } else {
      // Kein Passwort: Die Anmeldung läuft über Entra ID. Wer ein lokales
      // Konto braucht, bekommt es in der Verwaltung.
      await db.user.create({
        data: { email, ...daten, emailVerified: new Date() },
      });
      bericht.angelegt++;
    }
  }

  return bericht;
}

/**
 * Geräte einlesen.
 *
 * Gerätearten und Hersteller werden über den Namen aufgelöst und bei Bedarf
 * angelegt: Aus einem Altsystem kommt jede erdenkliche Schreibweise, und eine
 * Übernahme, die an einer unbekannten Geräteart scheitert, ist nutzlos.
 *
 * Kostenstellen dagegen werden **nicht** angelegt. Sie sind eine gepflegte
 * Liste, an der die Auswertung hängt – eine aus einem Tippfehler entstandene
 * Kostenstelle „4310 " würde dort still danebenstehen und die Summen aufteilen.
 * Unbekannte Kostenstellen führen deshalb zu einer Fehlermeldung für die Zeile.
 */
async function importiereGeraete(
  zeilen: Zeile[],
  trocken: boolean,
): Promise<Bericht> {
  const bericht = leererBericht();

  const [standorte, benutzer, bereiche, arten, hersteller, kostenstellen] =
    await Promise.all([
      db.site.findMany({ select: { id: true, name: true } }),
      db.user.findMany({ select: { id: true, email: true } }),
      db.team.findMany({
        where: { hasInventory: true, isActive: true },
        select: { id: true, key: true, name: true },
        orderBy: { sortOrder: "asc" },
      }),
      db.assetKind.findMany({ select: { id: true, key: true, name: true } }),
      db.manufacturer.findMany({ select: { id: true, name: true } }),
      db.costCenter.findMany({ select: { id: true, code: true, name: true } }),
    ]);

  if (bereiche.length === 0) {
    bericht.fehler.push({
      zeile: 0,
      grund:
        "Kein Bereich führt ein Inventar. Bitte in der Verwaltung unter " +
        "Bereiche das Inventar für den zuständigen Bereich einschalten.",
    });
    return bericht;
  }

  const standortMap = new Map(standorte.map((s) => [s.name.toLowerCase(), s.id]));
  const benutzerMap = new Map(benutzer.map((u) => [u.email.toLowerCase(), u.id]));
  const bereichMap = new Map<string, string>();
  for (const b of bereiche) {
    bereichMap.set(b.key.toLowerCase(), b.id);
    bereichMap.set(b.name.toLowerCase(), b.id);
  }
  const artMap = new Map<string, string>();
  for (const a of arten) {
    artMap.set(a.key.toLowerCase(), a.id);
    artMap.set(a.name.toLowerCase(), a.id);
  }
  const herstellerMap = new Map(hersteller.map((h) => [h.name.toLowerCase(), h.id]));
  const kostenMap = new Map<string, string>();
  for (const k of kostenstellen) {
    kostenMap.set(k.code.toLowerCase(), k.id);
    kostenMap.set(k.name.toLowerCase(), k.id);
  }

  /** Geräteart holen oder anlegen. Das Kurzzeichen wird aus dem Namen gebildet. */
  async function artFinden(bezeichnung: string): Promise<string> {
    const treffer = artMap.get(bezeichnung.toLowerCase());
    if (treffer) return treffer;

    const key = bezeichnung
      .toUpperCase()
      .replace(/[^A-Z0-9]/g, "")
      .slice(0, 4) || "SON";

    const angelegt = await db.assetKind.upsert({
      where: { name: bezeichnung },
      update: {},
      create: { key, name: bezeichnung, sortOrder: 500 },
      select: { id: true },
    });
    artMap.set(bezeichnung.toLowerCase(), angelegt.id);
    return angelegt.id;
  }

  async function herstellerFinden(name: string): Promise<string> {
    const treffer = herstellerMap.get(name.toLowerCase());
    if (treffer) return treffer;

    const angelegt = await db.manufacturer.upsert({
      where: { name },
      update: {},
      create: { name },
      select: { id: true },
    });
    herstellerMap.set(name.toLowerCase(), angelegt.id);
    return angelegt.id;
  }

  for (const [i, zeile] of zeilen.entries()) {
    bericht.gelesen++;
    const nummer = i + 2;

    const assetTag = spalte(zeile, "nummer", "inventarnummer", "geraetenummer");
    const name = spalte(zeile, "name", "bezeichnung");

    if (!assetTag) {
      bericht.fehler.push({ zeile: nummer, grund: "Spalte Nummer ist leer." });
      continue;
    }
    if (!name) {
      bericht.fehler.push({ zeile: nummer, grund: "Spalte Name ist leer." });
      continue;
    }

    // --- Bereich -----------------------------------------------------------
    const bereichName = spalte(zeile, "bereich", "team");
    const teamId = bereichName
      ? bereichMap.get(bereichName.toLowerCase())
      : bereiche[0].id;
    if (!teamId) {
      bericht.fehler.push({
        zeile: nummer,
        grund: `Bereich „${bereichName}" führt kein Inventar oder ist unbekannt.`,
      });
      continue;
    }

    // --- Kostenstelle ------------------------------------------------------
    const kostenstelle = spalte(zeile, "kostenstelle", "kst");
    let costCenterId: string | null = null;
    if (kostenstelle) {
      costCenterId = kostenMap.get(kostenstelle.toLowerCase()) ?? null;
      if (!costCenterId) {
        bericht.fehler.push({
          zeile: nummer,
          grund:
            `Kostenstelle „${kostenstelle}" ist nicht angelegt. Bitte erst ` +
            `in der Verwaltung ergänzen.`,
        });
        continue;
      }
    }

    // --- Status ------------------------------------------------------------
    const statusRoh = spalte(zeile, "status", "zustand").toUpperCase();
    const zugewiesen = spalte(zeile, "zugewiesen", "nutzer", "benutzer").toLowerCase();
    const assignedToId = zugewiesen ? (benutzerMap.get(zugewiesen) ?? null) : null;

    let status: AssetStatus;
    if (statusRoh && statusRoh in AssetStatus) {
      status = statusRoh as AssetStatus;
    } else if (statusRoh) {
      bericht.fehler.push({
        zeile: nummer,
        grund:
          `Status „${statusRoh}" ist unbekannt. Möglich sind: ` +
          Object.keys(AssetStatus).join(", "),
      });
      continue;
    } else {
      // Ohne Angabe aus der Zuweisung ableiten – das trifft fast immer zu.
      status = assignedToId ? AssetStatus.HERAUSGEGEBEN : AssetStatus.LAGER;
    }

    // --- Preis -------------------------------------------------------------
    let purchasePriceCents: number | null = null;
    const preisRoh = spalte(zeile, "preis", "anschaffungspreis", "kaufpreis");
    if (preisRoh) {
      try {
        purchasePriceCents = betragZuCent(preisRoh);
      } catch (fehler) {
        bericht.fehler.push({
          zeile: nummer,
          grund: fehler instanceof Error ? fehler.message : String(fehler),
        });
        continue;
      }
    }

    // --- MAC-Adressen ------------------------------------------------------
    // Mehrere durch Komma oder Semikolon getrennt.
    const macRoh = spalte(zeile, "mac", "macadresse", "mac-adresse");
    const macs: string[] = [];
    let macFehler: string | null = null;
    for (const teil of macRoh.split(/[;,]/)) {
      if (!teil.trim()) continue;
      try {
        macs.push(macNormalisieren(teil));
      } catch (fehler) {
        macFehler = fehler instanceof Error ? fehler.message : String(fehler);
        break;
      }
    }
    if (macFehler) {
      bericht.fehler.push({ zeile: nummer, grund: macFehler });
      continue;
    }

    const standortName = spalte(zeile, "standort", "einrichtung");
    const artName = spalte(zeile, "art", "geraeteart", "geräteart") || "Sonstiges";
    const herstellerName = spalte(zeile, "hersteller");

    const vorhanden = await db.asset.findUnique({
      where: { assetTag },
      select: { id: true },
    });

    if (trocken) {
      if (vorhanden) bericht.aktualisiert++;
      else bericht.angelegt++;
      continue;
    }

    const daten = {
      name,
      kindId: await artFinden(artName),
      teamId,
      status,
      manufacturerId: herstellerName ? await herstellerFinden(herstellerName) : null,
      model: spalte(zeile, "modell", "typ") || null,
      serial: spalte(zeile, "seriennummer", "serial", "sn") || null,
      imei: spalte(zeile, "imei") || null,
      room: spalte(zeile, "raum") || null,
      siteId: standortName
        ? (standortMap.get(standortName.toLowerCase()) ?? null)
        : null,
      assignedToId,
      assignedAt: assignedToId ? (leseDatum(spalte(zeile, "uebergeben")) ?? null) : null,
      costCenterId,
      purchasedAt: leseDatum(spalte(zeile, "anschaffung", "anschaffungsdatum", "kaufdatum")),
      warrantyUntil: leseDatum(spalte(zeile, "garantie", "garantiebis")),
      purchasePriceCents,
      supplier: spalte(zeile, "lieferant") || null,
      invoiceNumber: spalte(zeile, "rechnung", "rechnungsnummer") || null,
      notes: spalte(zeile, "notizen", "bemerkung", "bemerkungen") || null,
    };

    const geraet = vorhanden
      ? await db.asset.update({ where: { id: vorhanden.id }, data: daten })
      : await db.asset.create({ data: { assetTag, ...daten } });

    if (macs.length > 0) {
      // Bestehende ersetzen: Beim erneuten Einlesen derselben Datei sollen
      // keine Doppelungen entstehen.
      await db.assetMac.deleteMany({ where: { assetId: geraet.id } });
      for (const address of macs) {
        // Eine MAC kann nur an einem Gerät hängen. Steht sie schon woanders,
        // wird sie übersprungen statt den ganzen Import abzubrechen.
        const belegt = await db.assetMac.findUnique({ where: { address } });
        if (belegt) {
          bericht.fehler.push({
            zeile: nummer,
            grund: `MAC-Adresse ${address} ist bereits einem anderen Gerät zugeordnet.`,
          });
          continue;
        }
        await db.assetMac.create({ data: { assetId: geraet.id, address } });
      }
    }

    if (vorhanden) bericht.aktualisiert++;
    else bericht.angelegt++;
  }

  return bericht;
}

async function importiereTickets(
  zeilen: Zeile[],
  trocken: boolean,
): Promise<Bericht> {
  const bericht = leererBericht();

  const [bereiche, standorte] = await Promise.all([
    db.team.findMany({
      select: { id: true, key: true, name: true, queues: { select: { id: true, siteId: true } } },
    }),
    db.site.findMany({ select: { id: true, name: true } }),
  ]);

  const bereichMap = new Map<string, (typeof bereiche)[number]>();
  for (const b of bereiche) {
    bereichMap.set(b.key.toLowerCase(), b);
    bereichMap.set(b.name.toLowerCase(), b);
  }
  const standortMap = new Map(standorte.map((s) => [s.name.toLowerCase(), s.id]));

  const statusZuordnung: Record<string, TicketStatus> = {
    neu: TicketStatus.NEU,
    offen: TicketStatus.OFFEN,
    inbearbeitung: TicketStatus.IN_BEARBEITUNG,
    wartet: TicketStatus.WARTET_AUF_MELDER,
    geloest: TicketStatus.GELOEST,
    gelöst: TicketStatus.GELOEST,
    erledigt: TicketStatus.GELOEST,
    geschlossen: TicketStatus.GESCHLOSSEN,
    closed: TicketStatus.GESCHLOSSEN,
  };

  // Ein Sammelkonto für Melder, die es im neuen System nicht mehr gibt –
  // sonst müsste jedes Altticket mit unbekanntem Melder verworfen werden.
  let sammelMelder = await db.user.findUnique({
    where: { email: "import@example.invalid" },
    select: { id: true },
  });

  if (!sammelMelder && !trocken) {
    sammelMelder = await db.user.create({
      data: {
        email: "import@example.invalid",
        name: "Übernommen aus dem Altsystem",
        isExternal: true,
        isActive: false,
      },
      select: { id: true },
    });
  }

  // Zähler je Bereich, damit die neuen Nummern nicht mit bestehenden kollidieren.
  const zaehler = new Map<string, number>();

  for (const [i, zeile] of zeilen.entries()) {
    bericht.gelesen++;
    const nummer = i + 2;

    const betreff = spalte(zeile, "betreff", "titel", "subject");
    const bereichText = spalte(zeile, "bereich", "team", "queue", "gruppe");

    if (!betreff) {
      bericht.fehler.push({ zeile: nummer, grund: "Spalte Betreff ist leer." });
      continue;
    }

    const bereich = bereichMap.get(bereichText.toLowerCase());
    if (!bereich) {
      bericht.fehler.push({
        zeile: nummer,
        grund: `Bereich "${bereichText}" ist nicht angelegt.`,
      });
      continue;
    }

    const standortName = spalte(zeile, "standort", "einrichtung");
    const siteId = standortName
      ? (standortMap.get(standortName.toLowerCase()) ?? null)
      : null;

    // Passenden Eingang wählen: bei Standort-Eingängen den zum Standort,
    // sonst den ersten.
    const queue =
      bereich.queues.find((q) => q.siteId === siteId) ?? bereich.queues[0];
    if (!queue) {
      bericht.fehler.push({
        zeile: nummer,
        grund: `Der Bereich "${bereich.name}" hat keinen Eingang.`,
      });
      continue;
    }

    const melderMail = spalte(zeile, "melder", "ersteller", "kunde", "email")
      .toLowerCase();
    const melder = melderMail
      ? await db.user.findUnique({
          where: { email: melderMail },
          select: { id: true },
        })
      : null;

    const statusText = spalte(zeile, "status", "zustand")
      .toLowerCase()
      .replace(/[^a-zäöü]/g, "");
    const status = statusZuordnung[statusText] ?? TicketStatus.GESCHLOSSEN;

    const erstellt =
      leseDatum(spalte(zeile, "erstellt", "erstelltam", "created")) ?? new Date();
    const erledigt = leseDatum(spalte(zeile, "erledigt", "geschlossenam", "closed"));

    const altNummer = spalte(zeile, "nummer", "ticketnummer", "id");

    if (trocken) {
      bericht.angelegt++;
      continue;
    }

    const jahr = erstellt.getFullYear();
    const schluessel = `${bereich.id}:${jahr}`;

    if (!zaehler.has(schluessel)) {
      const stand = await db.ticketCounter.findUnique({
        where: { teamId_year: { teamId: bereich.id, year: jahr } },
        select: { value: true },
      });
      zaehler.set(schluessel, stand?.value ?? 0);
    }

    const naechste = zaehler.get(schluessel)! + 1;
    zaehler.set(schluessel, naechste);

    const ticket = await db.ticket.create({
      data: {
        number: formatTicketNumber(bereich.key, jahr, naechste),
        subject: betreff.slice(0, 200),
        queueId: queue.id,
        teamId: bereich.id,
        siteId,
        requesterId: melder?.id ?? sammelMelder!.id,
        status,
        source: TicketSource.IMPORT,
        createdAt: erstellt,
        lastActivityAt: erledigt ?? erstellt,
        resolvedAt: erledigt,
        closedAt: status === TicketStatus.GESCHLOSSEN ? erledigt : null,
      },
      select: { id: true },
    });

    const beschreibung = spalte(zeile, "beschreibung", "text", "body", "inhalt");
    if (beschreibung) {
      await db.ticketMessage.create({
        data: {
          ticketId: ticket.id,
          type: "PUBLIC_REPLY",
          authorId: melder?.id ?? null,
          authorEmail: melderMail || null,
          body: beschreibung,
          bodyText: beschreibung,
          createdAt: erstellt,
        },
      });
    }

    await db.ticketEvent.create({
      data: {
        ticketId: ticket.id,
        type: "IMPORTED",
        actorName: "Datenübernahme",
        note: altNummer
          ? `Aus dem Altsystem übernommen, dortige Nummer: ${altNummer}.`
          : "Aus dem Altsystem übernommen.",
      },
    });

    bericht.angelegt++;
  }

  // Zählerstände festschreiben, damit neue Vorgänge nicht kollidieren.
  if (!trocken) {
    for (const [schluessel, stand] of zaehler) {
      const [teamId, jahrText] = schluessel.split(":");
      await db.ticketCounter.upsert({
        where: { teamId_year: { teamId, year: Number(jahrText) } },
        create: { teamId, year: Number(jahrText), value: stand },
        update: { value: stand },
      });
    }
  }

  return bericht;
}

// ---------------------------------------------------------------------------

async function main() {
  const opt = leseOptionen();

  console.log(
    `Datenübernahme: ${opt.typ} aus ${opt.datei}` +
      (opt.trockenlauf ? "  [Trockenlauf – es wird nichts geschrieben]" : ""),
  );

  let inhalt: string;
  try {
    inhalt = readFileSync(opt.datei, "utf8");
  } catch {
    console.error(`Die Datei "${opt.datei}" konnte nicht gelesen werden.`);
    process.exit(1);
  }

  // Byte Order Mark entfernen – Excel schreibt sie beim UTF-8-Export mit, und
  // sie landet sonst im ersten Spaltennamen.
  if (inhalt.charCodeAt(0) === 0xfeff) inhalt = inhalt.slice(1);

  let zeilen: Zeile[];
  try {
    zeilen = parse(inhalt, {
      columns: true,
      delimiter: opt.trennzeichen,
      skip_empty_lines: true,
      trim: true,
      relax_column_count: true,
    }) as Zeile[];
  } catch (error) {
    console.error(
      `Die Datei konnte nicht gelesen werden. Stimmt das Trennzeichen ` +
        `(derzeit "${opt.trennzeichen}")?\n${error}`,
    );
    process.exit(1);
  }

  if (zeilen.length === 0) {
    console.log("Die Datei enthält keine Datenzeilen.");
    return;
  }

  console.log(`Spalten: ${Object.keys(zeilen[0]).join(", ")}`);
  console.log("");

  const bericht = await (opt.typ === "standorte"
    ? importiereStandorte(zeilen, opt.trockenlauf)
    : opt.typ === "benutzer"
      ? importiereBenutzer(zeilen, opt.trockenlauf)
      : opt.typ === "geraete"
        ? importiereGeraete(zeilen, opt.trockenlauf)
        : importiereTickets(zeilen, opt.trockenlauf));

  console.log("Ergebnis:");
  console.log(`  gelesen:       ${bericht.gelesen}`);
  console.log(`  angelegt:      ${bericht.angelegt}`);
  console.log(`  aktualisiert:  ${bericht.aktualisiert}`);
  console.log(`  übersprungen:  ${bericht.uebersprungen}`);
  console.log(`  Fehler:        ${bericht.fehler.length}`);

  if (bericht.fehler.length > 0) {
    console.log("");
    console.log("Fehlerhafte Zeilen:");
    for (const f of bericht.fehler.slice(0, 50)) {
      console.log(`  Zeile ${f.zeile}: ${f.grund}`);
    }
    if (bericht.fehler.length > 50) {
      console.log(`  … und ${bericht.fehler.length - 50} weitere.`);
    }
  }

  if (opt.trockenlauf) {
    console.log("");
    console.log(
      "Trockenlauf beendet – es wurde nichts geschrieben. Zum Übernehmen " +
        "denselben Aufruf ohne --dry-run wiederholen.",
    );
  }
}

main()
  .catch((error) => {
    console.error(error);
    process.exit(1);
  })
  .finally(async () => {
    await db.$disconnect();
  });
