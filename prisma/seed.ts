/**
 * Demo-Daten für einen Träger der Behindertenhilfe.
 *
 * Bildet die Ausgangslage ab, die im Plan beschrieben ist: mehrere Standorte,
 * die IT mit einem zentralen Eingang, die Hausmeister mit je einem Eingang pro
 * Standort, dazu einige Bereiche in der schlanken Ausbaustufe.
 *
 * Aufruf:  npm run db:seed
 * Der Seed ist wiederholbar – er räumt die Tabellen vorher ab.
 */

import {
  AssetType,
  ArticleVisibility,
  EventType,
  Impact,
  LinkType,
  MessageType,
  Priority,
  type Prisma,
  PrismaClient,
  QueueMode,
  SiteType,
  TeamMode,
  TeamRole,
  TicketSource,
  TicketStatus,
  Urgency,
  GlobalRole,
} from "@prisma/client";
import { hashPassword } from "../src/server/password";
import { formatTicketNumber } from "../src/server/services/number";

const db = new PrismaClient();

/** Einheitliches Demo-Passwort. Steht so auch im README. */
const DEMO_PASSWORT = "Willkommen-2026-Demo";

const tage = (n: number) => n * 24 * 60 * 60 * 1000;
const stunden = (n: number) => n * 60 * 60 * 1000;
const minuten = (n: number) => n * 60 * 1000;

const JETZT = new Date();
const vor = (ms: number) => new Date(JETZT.getTime() - ms);

async function leeren() {
  // Reihenfolge folgt den Fremdschlüsseln von innen nach außen.
  await db.$transaction([
    db.notification.deleteMany(),
    db.auditLog.deleteMany(),
    db.ticketAsset.deleteMany(),
    db.ticketTag.deleteMany(),
    db.ticketTask.deleteMany(),
    db.ticketWatcher.deleteMany(),
    db.ticketLink.deleteMany(),
    db.attachment.deleteMany(),
    db.ticketEvent.deleteMany(),
    db.ticketMessage.deleteMany(),
    db.ticket.deleteMany(),
    db.ticketCounter.deleteMany(),
    db.savedView.deleteMany(),
    db.kbArticle.deleteMany(),
    db.ticketTemplate.deleteMany(),
    db.cannedResponse.deleteMany(),
    db.asset.deleteMany(),
    db.tag.deleteMany(),
    db.category.deleteMany(),
    db.slaPolicy.deleteMany(),
    db.holiday.deleteMany(),
    db.businessHours.deleteMany(),
    db.mailAccount.deleteMany(),
    db.queueAccess.deleteMany(),
    db.teamMembership.deleteMany(),
    db.queue.deleteMany(),
    db.team.deleteMany(),
    db.session.deleteMany(),
    db.account.deleteMany(),
    db.user.deleteMany(),
    db.site.deleteMany(),
  ]);
}

async function main() {
  console.log("Räume vorhandene Daten ab …");
  await leeren();

  // =========================================================================
  // Standorte
  // =========================================================================
  console.log("Lege Standorte an …");

  const standorte = {
    verwaltung: await db.site.create({
      data: {
        name: "Verwaltung Musterstadt",
        shortName: "Verwaltung",
        type: SiteType.VERWALTUNG,
        street: "Lindenallee 12",
        zip: "12345",
        city: "Musterstadt",
        sortOrder: 0,
      },
    }),
    schule: await db.site.create({
      data: {
        name: "Förderschule Musterstadt",
        shortName: "Schule",
        type: SiteType.SCHULE,
        street: "Lindenallee 14",
        zip: "12345",
        city: "Musterstadt",
        sortOrder: 1,
      },
    }),
    wohnheimNord: await db.site.create({
      data: {
        name: "Wohnheim Nord",
        shortName: "Wohnheim Nord",
        type: SiteType.WOHNHEIM,
        street: "Am Nordpark 3",
        zip: "12345",
        city: "Musterstadt",
        sortOrder: 2,
      },
    }),
    wohnheimSued: await db.site.create({
      data: {
        name: "Wohnheim Süd",
        shortName: "Wohnheim Süd",
        type: SiteType.WOHNHEIM,
        street: "Talstraße 40",
        zip: "12347",
        city: "Musterstadt 2",
        sortOrder: 3,
      },
    }),
    betreutesWohnen: await db.site.create({
      data: {
        name: "Betreutes Wohnen Musterstadt 2",
        shortName: "Betreutes Wohnen",
        type: SiteType.BETREUTES_WOHNEN,
        street: "Talstraße 44",
        zip: "12347",
        city: "Musterstadt 2",
        sortOrder: 4,
      },
    }),
    werkstatt: await db.site.create({
      data: {
        name: "Werkstatt Musterstadt 2",
        shortName: "Werkstatt",
        type: SiteType.WERKSTATT,
        street: "Industriering 8",
        zip: "12347",
        city: "Musterstadt 2",
        sortOrder: 5,
      },
    }),
  };

  const alleStandorte = Object.values(standorte);

  // =========================================================================
  // Dienstzeiten und Feiertage
  // =========================================================================
  console.log("Lege Dienstzeiten an …");

  const dienstzeitRegel = await db.businessHours.create({
    data: {
      name: "Regeldienstzeit",
      timezone: "Europe/Berlin",
      isDefault: true,
      // 1 = Montag … 7 = Sonntag
      schedule: {
        "1": [["07:30", "16:30"]],
        "2": [["07:30", "16:30"]],
        "3": [["07:30", "16:30"]],
        "4": [["07:30", "16:30"]],
        "5": [["07:30", "14:00"]],
        "6": [],
        "7": [],
      },
    },
  });

  const dienstzeitErweitert = await db.businessHours.create({
    data: {
      name: "Erweiterte Dienstzeit (Wohnbereiche)",
      timezone: "Europe/Berlin",
      schedule: {
        "1": [["06:00", "20:00"]],
        "2": [["06:00", "20:00"]],
        "3": [["06:00", "20:00"]],
        "4": [["06:00", "20:00"]],
        "5": [["06:00", "20:00"]],
        "6": [["08:00", "14:00"]],
        "7": [],
      },
    },
  });

  const jahr = JETZT.getFullYear();
  const feiertage = [
    [`${jahr}-01-01`, "Neujahr"],
    [`${jahr}-05-01`, "Tag der Arbeit"],
    [`${jahr}-10-03`, "Tag der Deutschen Einheit"],
    [`${jahr}-12-24`, "Heiligabend"],
    [`${jahr}-12-25`, "1. Weihnachtsfeiertag"],
    [`${jahr}-12-26`, "2. Weihnachtsfeiertag"],
    [`${jahr}-12-31`, "Silvester"],
  ] as const;

  for (const bh of [dienstzeitRegel, dienstzeitErweitert]) {
    await db.holiday.createMany({
      data: feiertage.map(([datum, name]) => ({
        businessHoursId: bh.id,
        date: new Date(`${datum}T00:00:00Z`),
        name,
      })),
    });
  }

  // =========================================================================
  // Bereiche und Eingänge
  // =========================================================================
  console.log("Lege Bereiche und Eingänge an …");

  // --- IT: voller Arbeitsplatz, ein Eingang für alle Standorte -------------
  const it = await db.team.create({
    data: {
      key: "IT",
      name: "IT",
      mode: TeamMode.VOLL,
      queueMode: QueueMode.SINGLE,
      description:
        "Betreuung von Arbeitsplätzen, Fachanwendungen, Telefonie und Netzwerk an allen Standorten.",
      color: "#0f766e",
      icon: "Monitor",
      sortOrder: 0,
      portalVisible: true,
      portalLabel: "Computer, Programme, Telefon, Drucker",
      portalDescription:
        "Alles rund um PC, Notebook, Anmeldung, Fachprogramme, E-Mail, Telefon und Drucker.",
      autoAssign: "NONE",
    },
  });

  const itEingang = await db.queue.create({
    data: {
      teamId: it.id,
      siteId: null,
      name: "IT",
      inboundEmail: "it@example.org",
      sortOrder: 0,
    },
  });

  // --- Hausmeister: voller Arbeitsplatz, ein Eingang je Standort -----------
  const hausmeister = await db.team.create({
    data: {
      key: "HM",
      name: "Hausmeister",
      mode: TeamMode.VOLL,
      queueMode: QueueMode.PER_SITE,
      description:
        "Gebäudetechnik, Reparaturen, Außenanlagen und Kleinaufträge an den einzelnen Standorten.",
      color: "#b45309",
      icon: "Wrench",
      sortOrder: 1,
      portalVisible: true,
      portalLabel: "Gebäude, Heizung, Licht, Reparaturen",
      portalDescription:
        "Defekte an Gebäude und Technik, Heizung, Beleuchtung, Türen, Fenster, Möbel und Außenanlagen.",
    },
  });

  const hausmeisterEingaenge: Record<string, string> = {};
  for (const [index, standort] of alleStandorte.entries()) {
    const q = await db.queue.create({
      data: {
        teamId: hausmeister.id,
        siteId: standort.id,
        name: `Hausmeister ${standort.shortName ?? standort.name}`,
        inboundEmail: `hausmeister-${standort
          .shortName!.toLowerCase()
          .replace(/ä/g, "ae")
          .replace(/ö/g, "oe")
          .replace(/ü/g, "ue")
          .replace(/ß/g, "ss")
          .replace(/[^a-z0-9]+/g, "-")
          .replace(/^-|-$/g, "")}@example.org`,
        sortOrder: index,
      },
    });
    hausmeisterEingaenge[standort.id] = q.id;
  }

  // --- Bereiche in der schlanken Ausbaustufe -------------------------------
  const verwaltung = await db.team.create({
    data: {
      key: "VW",
      name: "Verwaltung",
      mode: TeamMode.EINFACH,
      queueMode: QueueMode.SINGLE,
      description: "Anträge, Bescheinigungen, Dienstausweise, Beschaffung.",
      color: "#4f46e5",
      icon: "FileText",
      sortOrder: 2,
      portalVisible: true,
      portalLabel: "Anträge, Bescheinigungen, Beschaffung",
      portalDescription:
        "Bescheinigungen, Dienstausweis, Bestellungen von Büromaterial und allgemeine Verwaltungsfragen.",
    },
  });
  const verwaltungEingang = await db.queue.create({
    data: {
      teamId: verwaltung.id,
      name: "Verwaltung",
      inboundEmail: "verwaltung@example.org",
    },
  });

  const kueche = await db.team.create({
    data: {
      key: "KU",
      name: "Küche",
      mode: TeamMode.EINFACH,
      queueMode: QueueMode.SINGLE,
      description: "Verpflegung, Sonderkost, Bestellungen für die Wohnbereiche.",
      color: "#059669",
      icon: "UtensilsCrossed",
      sortOrder: 3,
      portalVisible: true,
      portalLabel: "Essen, Bestellungen, Sonderkost",
      portalDescription:
        "Essensbestellungen, Unverträglichkeiten und Sonderkost, Rückmeldungen zur Verpflegung.",
    },
  });
  const kuecheEingang = await db.queue.create({
    data: {
      teamId: kueche.id,
      name: "Küche",
      inboundEmail: "kueche@example.org",
    },
  });

  const fahrdienst = await db.team.create({
    data: {
      key: "FD",
      name: "Fahrdienst",
      mode: TeamMode.EINFACH,
      queueMode: QueueMode.SINGLE,
      description: "Fahrten zu Terminen, Fahrzeugdisposition, Schülerbeförderung.",
      color: "#7c3aed",
      icon: "Bus",
      sortOrder: 4,
      portalVisible: true,
      portalLabel: "Fahrten und Fahrzeuge",
      portalDescription:
        "Fahrten zu Terminen anmelden, Änderungen bei der Beförderung, Fragen zu den Fahrzeugen.",
    },
  });
  const fahrdienstEingang = await db.queue.create({
    data: {
      teamId: fahrdienst.id,
      name: "Fahrdienst",
      inboundEmail: "fahrdienst@example.org",
    },
  });

  // Personal nimmt bewusst keine Selbstmeldungen entgegen: erreichbar nur über
  // die Weiterleitung aus einem anderen Bereich.
  const personal = await db.team.create({
    data: {
      key: "PE",
      name: "Personal",
      mode: TeamMode.EINFACH,
      queueMode: QueueMode.SINGLE,
      description:
        "Personalangelegenheiten. Nimmt keine offenen Meldungen entgegen, sondern nur Weiterleitungen.",
      color: "#be123c",
      icon: "Users",
      sortOrder: 5,
      portalVisible: false,
    },
  });
  const personalEingang = await db.queue.create({
    data: {
      teamId: personal.id,
      name: "Personal",
      inboundEmail: "personal@example.org",
      portalVisible: false,
    },
  });

  // =========================================================================
  // Fristen
  // =========================================================================
  console.log("Lege Fristen an …");

  const slaDaten = [
    // Bereich, Priorität, Erstreaktion (min), Lösung (min)
    [it, Priority.DRINGEND, 30, 4 * 60],
    [it, Priority.HOCH, 2 * 60, 8 * 60],
    [it, Priority.NORMAL, 4 * 60, 3 * 8 * 60],
    [it, Priority.NIEDRIG, 8 * 60, 10 * 8 * 60],
    [hausmeister, Priority.DRINGEND, 30, 4 * 60],
    [hausmeister, Priority.HOCH, 4 * 60, 2 * 8 * 60],
    [hausmeister, Priority.NORMAL, 8 * 60, 5 * 8 * 60],
    [hausmeister, Priority.NIEDRIG, 2 * 8 * 60, 15 * 8 * 60],
  ] as const;

  const slaMap = new Map<string, string>();
  for (const [team, prioritaet, erst, loesung] of slaDaten) {
    const policy = await db.slaPolicy.create({
      data: {
        teamId: team.id,
        name: `${team.name} – ${prioritaet}`,
        priority: prioritaet,
        firstResponseMinutes: erst,
        resolutionMinutes: loesung,
        businessHoursId:
          team.id === hausmeister.id
            ? dienstzeitErweitert.id
            : dienstzeitRegel.id,
      },
    });
    slaMap.set(`${team.id}:${prioritaet}`, policy.id);
  }

  // =========================================================================
  // Kategorien (nur Voll-Bereiche)
  // =========================================================================
  console.log("Lege Kategorien an …");

  const itKategorien = [
    {
      name: "Anmeldung und Konto",
      description: "Passwort, Kontosperrung, Berechtigungen",
      formSchema: [
        {
          key: "betroffenesKonto",
          label: "Betroffenes Benutzerkonto",
          type: "text",
          required: false,
          help: "Falls es nicht um Ihr eigenes Konto geht.",
        },
        {
          key: "fehlermeldung",
          label: "Genaue Fehlermeldung",
          type: "textarea",
          required: false,
        },
      ],
    },
    {
      name: "Computer und Notebook",
      description: "Gerät startet nicht, ist langsam oder defekt",
      formSchema: [
        {
          key: "geraetenummer",
          label: "Gerätenummer (Aufkleber am Gerät)",
          type: "text",
          required: false,
          help: 'Zum Beispiel "IT-00123".',
        },
        {
          key: "seitWann",
          label: "Seit wann tritt das Problem auf?",
          type: "text",
          required: false,
        },
      ],
    },
    {
      name: "Drucker und Kopierer",
      description: "Druckaufträge, Papierstau, Toner",
      formSchema: [
        {
          key: "druckerStandort",
          label: "Wo steht der Drucker?",
          type: "text",
          required: true,
        },
        {
          key: "fehlerAmDisplay",
          label: "Anzeige am Gerät",
          type: "text",
          required: false,
        },
      ],
    },
    {
      name: "Fachanwendung",
      description: "Dokumentation, Dienstplan, Abrechnung",
      formSchema: [
        {
          key: "programm",
          label: "Welches Programm?",
          type: "select",
          required: true,
          options: [
            "Klientendokumentation",
            "Dienstplanung",
            "Abrechnung",
            "E-Mail / Kalender",
            "Anderes",
          ],
        },
      ],
    },
    { name: "Telefon", description: "Festnetz, Mobiltelefon, Weiterleitung" },
    {
      name: "Netzwerk und WLAN",
      description: "Keine Verbindung, langsames Netz",
    },
    {
      name: "Neue Mitarbeiterin / neuer Mitarbeiter",
      description: "Konto, Ausstattung und Berechtigungen einrichten",
      formSchema: [
        { key: "personName", label: "Name der Person", type: "text", required: true },
        { key: "eintritt", label: "Eintrittsdatum", type: "date", required: true },
        {
          key: "bereichNeu",
          label: "Einsatzbereich",
          type: "text",
          required: true,
        },
        {
          key: "ausstattung",
          label: "Benötigte Ausstattung",
          type: "select",
          required: true,
          options: ["Nur Konto", "Konto und PC", "Konto, Notebook und Telefon"],
        },
      ],
    },
  ];

  const itKategorieIds: Record<string, string> = {};
  for (const [index, k] of itKategorien.entries()) {
    const cat = await db.category.create({
      data: {
        teamId: it.id,
        name: k.name,
        description: k.description,
        formSchema: k.formSchema ?? undefined,
        sortOrder: index,
        slaPolicyId: slaMap.get(`${it.id}:${Priority.NORMAL}`),
      },
    });
    itKategorieIds[k.name] = cat.id;
  }

  const hmKategorien = [
    {
      name: "Heizung und Lüftung",
      description: "Zu kalt, zu warm, Geräusche",
      formSchema: [
        {
          key: "raum",
          label: "Welcher Raum?",
          type: "text",
          required: true,
        },
        {
          key: "seitWann",
          label: "Seit wann?",
          type: "text",
          required: false,
        },
      ],
    },
    { name: "Elektro und Beleuchtung", description: "Licht, Steckdosen, Sicherungen" },
    { name: "Sanitär", description: "Wasserhahn, WC, Abfluss, Dusche" },
    { name: "Türen, Fenster, Schlösser", description: "Auch Schließanlage und Schlüssel" },
    { name: "Möbel und Einrichtung", description: "Reparatur, Aufbau, Transport" },
    { name: "Außenanlagen", description: "Wege, Grünflächen, Winterdienst" },
    {
      name: "Barrierefreiheit",
      description: "Rampen, automatische Türen, Aufzug, Handläufe",
      formSchema: [
        {
          key: "beeintraechtigung",
          label: "Wer ist beeinträchtigt?",
          type: "textarea",
          required: false,
          help: "Bitte ohne Namen oder Gesundheitsdaten – nur, was für die Reparatur nötig ist.",
        },
      ],
    },
  ];

  const hmKategorieIds: Record<string, string> = {};
  for (const [index, k] of hmKategorien.entries()) {
    const cat = await db.category.create({
      data: {
        teamId: hausmeister.id,
        name: k.name,
        description: k.description,
        formSchema: k.formSchema ?? undefined,
        sortOrder: index,
        slaPolicyId: slaMap.get(`${hausmeister.id}:${Priority.NORMAL}`),
      },
    });
    hmKategorieIds[k.name] = cat.id;
  }

  // =========================================================================
  // Benutzer
  // =========================================================================
  console.log("Lege Benutzer an …");

  const passwortHash = await hashPassword(DEMO_PASSWORT);

  async function benutzer(opts: {
    email: string;
    name: string;
    siteId?: string;
    jobTitle?: string;
    department?: string;
    globalRole?: GlobalRole;
    a11y?: Record<string, boolean>;
  }) {
    return db.user.create({
      data: {
        email: opts.email,
        name: opts.name,
        passwordHash: passwortHash,
        siteId: opts.siteId,
        jobTitle: opts.jobTitle,
        department: opts.department,
        globalRole: opts.globalRole ?? GlobalRole.USER,
        a11yPrefs: opts.a11y ?? {},
        emailVerified: JETZT,
      },
    });
  }

  // Verwaltung / Leitung
  const admin = await benutzer({
    email: "admin@example.org",
    name: "Sabine Reuter",
    siteId: standorte.verwaltung.id,
    jobTitle: "Leitung IT",
    department: "IT",
    globalRole: GlobalRole.ADMIN,
  });

  // IT
  const itLeitung = admin;
  const itKlaus = await benutzer({
    email: "klaus.hoffmann@example.org",
    name: "Klaus Hoffmann",
    siteId: standorte.verwaltung.id,
    jobTitle: "Systemadministrator",
    department: "IT",
  });
  const itMehmet = await benutzer({
    email: "mehmet.yilmaz@example.org",
    name: "Mehmet Yılmaz",
    siteId: standorte.verwaltung.id,
    jobTitle: "IT-Support",
    department: "IT",
  });
  const itJana = await benutzer({
    email: "jana.krause@example.org",
    name: "Jana Krause",
    siteId: standorte.schule.id,
    jobTitle: "IT-Support (Schule)",
    department: "IT",
  });

  // Hausmeister
  const hmLeitung = await benutzer({
    email: "dieter.wolf@example.org",
    name: "Dieter Wolf",
    siteId: standorte.verwaltung.id,
    jobTitle: "Leitung Technischer Dienst",
    department: "Hausmeister",
  });
  const hmFrank = await benutzer({
    email: "frank.berger@example.org",
    name: "Frank Berger",
    siteId: standorte.wohnheimNord.id,
    jobTitle: "Hausmeister",
    department: "Hausmeister",
  });
  const hmTobias = await benutzer({
    email: "tobias.lang@example.org",
    name: "Tobias Lang",
    siteId: standorte.wohnheimSued.id,
    jobTitle: "Hausmeister",
    department: "Hausmeister",
  });
  const hmAndreas = await benutzer({
    email: "andreas.schuster@example.org",
    name: "Andreas Schuster",
    siteId: standorte.schule.id,
    jobTitle: "Hausmeister",
    department: "Hausmeister",
  });

  // Bereiche in der schlanken Ausbaustufe
  const vwPetra = await benutzer({
    email: "petra.neumann@example.org",
    name: "Petra Neumann",
    siteId: standorte.verwaltung.id,
    jobTitle: "Sachbearbeitung",
    department: "Verwaltung",
  });
  const kuMarion = await benutzer({
    email: "marion.fischer@example.org",
    name: "Marion Fischer",
    siteId: standorte.verwaltung.id,
    jobTitle: "Küchenleitung",
    department: "Küche",
  });
  const fdRalf = await benutzer({
    email: "ralf.moeller@example.org",
    name: "Ralf Möller",
    siteId: standorte.verwaltung.id,
    jobTitle: "Disposition Fahrdienst",
    department: "Fahrdienst",
  });
  const peBirgit = await benutzer({
    email: "birgit.walter@example.org",
    name: "Birgit Walter",
    siteId: standorte.verwaltung.id,
    jobTitle: "Personalsachbearbeitung",
    department: "Personal",
  });

  // Meldende Mitarbeitende – der Normalfall im Unternehmen.
  const anna = await benutzer({
    email: "anna.bergmann@example.org",
    name: "Anna Bergmann",
    siteId: standorte.wohnheimNord.id,
    jobTitle: "Heilerziehungspflegerin",
    department: "Wohnbereich",
  });
  const thomas = await benutzer({
    email: "thomas.richter@example.org",
    name: "Thomas Richter",
    siteId: standorte.wohnheimSued.id,
    jobTitle: "Wohnbereichsleitung",
    department: "Wohnbereich",
  });
  const claudia = await benutzer({
    email: "claudia.schmitt@example.org",
    name: "Claudia Schmitt",
    siteId: standorte.schule.id,
    jobTitle: "Lehrerin",
    department: "Schule",
    // Nutzt die vergrößerte Darstellung und die einfache Ansicht.
    a11y: { largeText: true, simpleMode: true },
  });
  const martin = await benutzer({
    email: "martin.gruber@example.org",
    name: "Martin Gruber",
    siteId: standorte.betreutesWohnen.id,
    jobTitle: "Ambulante Betreuung",
    department: "Betreutes Wohnen",
  });
  const sonja = await benutzer({
    email: "sonja.peters@example.org",
    name: "Sonja Peters",
    siteId: standorte.werkstatt.id,
    jobTitle: "Gruppenleitung Werkstatt",
    department: "Werkstatt",
    a11y: { highContrast: true },
  });
  const lukas = await benutzer({
    email: "lukas.brandt@example.org",
    name: "Lukas Brandt",
    siteId: standorte.schule.id,
    jobTitle: "Schulbegleitung",
    department: "Schule",
  });
  const heike = await benutzer({
    email: "heike.sommer@example.org",
    name: "Heike Sommer",
    siteId: standorte.wohnheimNord.id,
    jobTitle: "Pflegefachkraft",
    department: "Wohnbereich",
  });
  const jonas = await benutzer({
    email: "jonas.keller@example.org",
    name: "Jonas Keller",
    siteId: standorte.werkstatt.id,
    jobTitle: "Arbeitserzieher",
    department: "Werkstatt",
  });

  const melder = [anna, thomas, claudia, martin, sonja, lukas, heike, jonas];

  // =========================================================================
  // Mitgliedschaften und Eingangs-Freigaben
  // =========================================================================
  console.log("Lege Mitgliedschaften an …");

  async function mitglied(userId: string, teamId: string, role: TeamRole) {
    await db.teamMembership.create({ data: { userId, teamId, role } });
  }

  async function eingangsZugriff(
    userId: string,
    queueId: string,
    isDefault = false,
  ) {
    await db.queueAccess.create({ data: { userId, queueId, isDefault } });
  }

  // IT: ein gemeinsamer Eingang, alle Mitglieder sehen ihn.
  await mitglied(itLeitung.id, it.id, TeamRole.LEAD);
  await mitglied(itKlaus.id, it.id, TeamRole.DISPATCHER);
  await mitglied(itMehmet.id, it.id, TeamRole.MEMBER);
  await mitglied(itJana.id, it.id, TeamRole.MEMBER);
  for (const u of [itLeitung, itKlaus, itMehmet, itJana]) {
    await eingangsZugriff(u.id, itEingang.id, true);
  }

  // Hausmeister: je Standort ein Eingang, die Freigaben bilden die
  // Zuständigkeit ab. Dieter Wolf ist Leitung und sieht ohnehin alles.
  await mitglied(hmLeitung.id, hausmeister.id, TeamRole.LEAD);
  await mitglied(hmFrank.id, hausmeister.id, TeamRole.MEMBER);
  await mitglied(hmTobias.id, hausmeister.id, TeamRole.MEMBER);
  await mitglied(hmAndreas.id, hausmeister.id, TeamRole.DISPATCHER);

  // Frank betreut Wohnheim Nord und die Verwaltung.
  await eingangsZugriff(
    hmFrank.id,
    hausmeisterEingaenge[standorte.wohnheimNord.id],
    true,
  );
  await eingangsZugriff(
    hmFrank.id,
    hausmeisterEingaenge[standorte.verwaltung.id],
  );
  // Tobias betreut Wohnheim Süd, Betreutes Wohnen und die Werkstatt.
  await eingangsZugriff(
    hmTobias.id,
    hausmeisterEingaenge[standorte.wohnheimSued.id],
    true,
  );
  await eingangsZugriff(
    hmTobias.id,
    hausmeisterEingaenge[standorte.betreutesWohnen.id],
  );
  await eingangsZugriff(
    hmTobias.id,
    hausmeisterEingaenge[standorte.werkstatt.id],
  );
  // Andreas betreut die Schule.
  await eingangsZugriff(
    hmAndreas.id,
    hausmeisterEingaenge[standorte.schule.id],
    true,
  );

  // Schlanke Bereiche
  await mitglied(vwPetra.id, verwaltung.id, TeamRole.LEAD);
  await eingangsZugriff(vwPetra.id, verwaltungEingang.id, true);
  await mitglied(kuMarion.id, kueche.id, TeamRole.LEAD);
  await eingangsZugriff(kuMarion.id, kuecheEingang.id, true);
  await mitglied(fdRalf.id, fahrdienst.id, TeamRole.LEAD);
  await eingangsZugriff(fdRalf.id, fahrdienstEingang.id, true);
  await mitglied(peBirgit.id, personal.id, TeamRole.LEAD);
  await eingangsZugriff(peBirgit.id, personalEingang.id, true);

  // =========================================================================
  // Schlagworte, Textbausteine, Vorlagen, Wissensdatenbank, Geräte
  // =========================================================================
  console.log("Lege Hilfsmittel an …");

  const tags = await Promise.all(
    [
      ["Wiederkehrend", "#dc2626"],
      ["Ersatzteil bestellt", "#d97706"],
      ["Wartet auf Lieferung", "#7c3aed"],
      ["Barrierefreiheit", "#0891b2"],
      ["Sicherheit", "#be123c"],
    ].map(([name, color]) => db.tag.create({ data: { name, color } })),
  );

  await db.cannedResponse.createMany({
    data: [
      {
        teamId: it.id,
        title: "Eingangsbestätigung",
        shortcut: "/eingang",
        body:
          "Guten Tag,\n\nvielen Dank für Ihre Meldung. Wir haben den Vorgang aufgenommen " +
          "und melden uns, sobald wir mehr wissen.\n\nFreundliche Grüße\nIhre IT",
        sortOrder: 0,
      },
      {
        teamId: it.id,
        title: "Neustart erbitten",
        shortcut: "/neustart",
        body:
          "Guten Tag,\n\nbitte starten Sie den Rechner einmal vollständig neu: " +
          "Start → Ein/Aus → Neu starten. Bitte nicht nur den Bildschirm " +
          "ausschalten oder den Deckel zuklappen.\n\nBleibt das Problem bestehen, " +
          "melden Sie sich bitte einfach auf diese Nachricht zurück.\n\n" +
          "Freundliche Grüße\nIhre IT",
        sortOrder: 1,
      },
      {
        teamId: it.id,
        title: "Passwort zurückgesetzt",
        shortcut: "/passwort",
        body:
          "Guten Tag,\n\nwir haben Ihr Passwort zurückgesetzt. Sie erhalten das neue " +
          "Kennwort aus Sicherheitsgründen telefonisch. Bitte ändern Sie es nach der " +
          "ersten Anmeldung.\n\nFreundliche Grüße\nIhre IT",
        sortOrder: 2,
      },
      {
        teamId: hausmeister.id,
        title: "Termin ankündigen",
        shortcut: "/termin",
        body:
          "Guten Tag,\n\nwir kommen am ___ zwischen ___ und ___ Uhr vorbei. " +
          "Bitte sorgen Sie dafür, dass der Raum in dieser Zeit zugänglich ist.\n\n" +
          "Freundliche Grüße\nIhr Technischer Dienst",
        sortOrder: 0,
      },
      {
        teamId: hausmeister.id,
        title: "Ersatzteil bestellt",
        shortcut: "/ersatzteil",
        body:
          "Guten Tag,\n\nwir haben das benötigte Ersatzteil bestellt. Die Lieferzeit " +
          "beträgt erfahrungsgemäß etwa eine Woche. Wir melden uns, sobald wir den " +
          "Einbau durchführen können.\n\nFreundliche Grüße\nIhr Technischer Dienst",
        sortOrder: 1,
      },
    ],
  });

  await db.ticketTemplate.create({
    data: {
      teamId: it.id,
      name: "Neue Kollegin / neuer Kollege einrichten",
      description:
        "Alle Schritte, damit eine neue Person am ersten Tag arbeitsfähig ist.",
      subjectTemplate: "Einrichtung neue Person: {{name}} ({{eintritt}})",
      bodyTemplate:
        "Name: {{name}}\nEintritt: {{eintritt}}\nBereich: {{bereich}}\n" +
        "Standort: {{standort}}\n\nBenötigte Ausstattung: {{ausstattung}}",
      defaultPriority: Priority.NORMAL,
      checklist: [
        "Benutzerkonto anlegen",
        "E-Mail-Postfach einrichten",
        "Berechtigungen für den Bereich vergeben",
        "Zugang zur Klientendokumentation freischalten",
        "Gerät vorbereiten und ausliefern",
        "Telefon einrichten",
        "Einweisung Datenschutz dokumentieren",
      ],
    },
  });

  await db.ticketTemplate.create({
    data: {
      teamId: it.id,
      name: "Austritt bearbeiten",
      description: "Konten und Geräte beim Ausscheiden sicher einziehen.",
      subjectTemplate: "Austritt: {{name}} zum {{datum}}",
      bodyTemplate: "Name: {{name}}\nLetzter Arbeitstag: {{datum}}",
      defaultPriority: Priority.HOCH,
      checklist: [
        "Anmeldung sperren",
        "Postfach umleiten",
        "Geräte einsammeln und dokumentieren",
        "Schlüssel und Ausweis einziehen",
        "Berechtigungen entfernen",
        "Konto nach Aufbewahrungsfrist löschen",
      ],
    },
  });

  await db.kbArticle.createMany({
    data: [
      {
        slug: "passwort-selbst-aendern",
        title: "Passwort selbst ändern",
        summary:
          "So ändern Sie Ihr Passwort am PC – ohne Anruf bei der IT.",
        body:
          "## Am Computer\n\n1. Drücken Sie gleichzeitig die Tasten **Strg**, **Alt** und **Entf**.\n" +
          "2. Wählen Sie **Kennwort ändern**.\n" +
          "3. Geben Sie zuerst das alte, dann zweimal das neue Passwort ein.\n\n" +
          "## Was ein gutes Passwort ausmacht\n\n" +
          "Wählen Sie lieber einen langen Satz als ein kurzes Wort mit Sonderzeichen. " +
          "*Mein Fahrrad steht im Keller!* ist sicherer und leichter zu merken als *Xk7!p2*.\n\n" +
          "## Es klappt nicht?\n\nSchreiben Sie uns ein Ticket an die IT.",
        visibility: ArticleVisibility.OEFFENTLICH,
        teamId: it.id,
        authorId: itKlaus.id,
        published: true,
        viewCount: 214,
        helpfulYes: 38,
        helpfulNo: 2,
      },
      {
        slug: "drucker-druckt-nicht",
        title: "Drucker druckt nicht – das können Sie selbst prüfen",
        summary: "Vier schnelle Prüfschritte, bevor Sie ein Ticket schreiben.",
        body:
          "1. **Ist der Drucker eingeschaltet?** Leuchtet eine Lampe an der Vorderseite?\n" +
          "2. **Steht etwas im Display?** Notieren Sie den Text – er hilft uns sehr.\n" +
          "3. **Ist Papier eingelegt?** Ziehen Sie die Schublade heraus und wieder hinein.\n" +
          "4. **Hängt ein alter Auftrag fest?** Öffnen Sie unten rechts das Druckersymbol " +
          "und löschen Sie wartende Aufträge.\n\n" +
          "Hilft nichts davon, schreiben Sie uns bitte ein Ticket und geben Sie an, " +
          "**wo der Drucker steht** und **was im Display angezeigt wird**.",
        visibility: ArticleVisibility.OEFFENTLICH,
        teamId: it.id,
        authorId: itMehmet.id,
        published: true,
        viewCount: 167,
        helpfulYes: 29,
        helpfulNo: 4,
      },
      {
        slug: "heizung-wird-nicht-warm",
        title: "Heizkörper wird nicht warm",
        summary: "Erst entlüften, dann melden – meist ist es nur Luft im System.",
        body:
          "Wenn ein Heizkörper oben kalt und unten warm ist, ist Luft im System.\n\n" +
          "**Was Sie tun können:** Thermostat ganz aufdrehen und eine halbe Stunde warten.\n\n" +
          "**Was wir tun:** Entlüften gehört zum Technischen Dienst. Bitte melden Sie den " +
          "Heizkörper mit **Raumnummer** und **Gebäude**, dann kommen wir mit dem passenden " +
          "Werkzeug direkt vorbei.\n\n" +
          "In der Heizperiode zwischen Oktober und April behandeln wir kalte Räume in " +
          "Wohnbereichen vorrangig.",
        visibility: ArticleVisibility.OEFFENTLICH,
        teamId: hausmeister.id,
        authorId: hmLeitung.id,
        published: true,
        viewCount: 91,
        helpfulYes: 17,
        helpfulNo: 1,
      },
      {
        slug: "ticket-richtig-schreiben",
        title: "So schreiben Sie ein Ticket, das schnell bearbeitet wird",
        summary: "Drei Angaben, die uns Rückfragen ersparen.",
        body:
          "Damit wir Ihnen schnell helfen können, brauchen wir drei Dinge:\n\n" +
          "1. **Was funktioniert nicht?** Beschreiben Sie, was Sie erwartet haben " +
          "und was stattdessen passiert ist.\n" +
          "2. **Wo?** Standort und Raum. Bei Geräten die Nummer vom Aufkleber.\n" +
          "3. **Seit wann?** Und ob es vorher schon einmal vorkam.\n\n" +
          "> **Wichtig:** Bitte schreiben Sie keine Gesundheitsdaten oder Namen von " +
          "Klientinnen und Klienten in ein Ticket. Für die Bearbeitung sind sie nicht " +
          "nötig, und Tickets sind für mehrere Personen im Bereich sichtbar.",
        visibility: ArticleVisibility.OEFFENTLICH,
        authorId: admin.id,
        published: true,
        viewCount: 302,
        helpfulYes: 51,
        helpfulNo: 3,
      },
      {
        slug: "intern-serverraum-zugang",
        title: "Intern: Zugang zum Serverraum",
        summary: "Nur für die IT.",
        body:
          "Der Serverraum in der Verwaltung ist über die Schließanlage Gruppe S " +
          "zugänglich. Zutritt haben ausschließlich die Mitglieder der IT sowie die " +
          "Leitung des Technischen Dienstes.\n\n" +
          "Jeder Zutritt wird im Zutrittsprotokoll erfasst. Fremdfirmen werden begleitet.",
        visibility: ArticleVisibility.INTERN,
        teamId: it.id,
        authorId: admin.id,
        published: true,
        viewCount: 23,
      },
    ],
  });

  const geraete = await Promise.all(
    [
      {
        assetTag: "IT-00101",
        name: "Notebook Wohnbereichsleitung Nord",
        type: AssetType.NOTEBOOK,
        manufacturer: "Lenovo",
        model: "ThinkPad L14",
        serial: "PF3KL201",
        siteId: standorte.wohnheimNord.id,
        assignedToId: anna.id,
      },
      {
        assetTag: "IT-00102",
        name: "PC Dienstzimmer Nord",
        type: AssetType.PC,
        manufacturer: "Dell",
        model: "OptiPlex 5000",
        serial: "DL9928KK",
        siteId: standorte.wohnheimNord.id,
        room: "Dienstzimmer",
      },
      {
        assetTag: "IT-00210",
        name: "Etagendrucker Wohnheim Nord",
        type: AssetType.DRUCKER,
        manufacturer: "Kyocera",
        model: "ECOSYS M3145",
        serial: "KY44120",
        siteId: standorte.wohnheimNord.id,
        room: "Flur 1. OG",
      },
      {
        assetTag: "IT-00211",
        name: "Kopierer Lehrerzimmer",
        type: AssetType.DRUCKER,
        manufacturer: "Kyocera",
        model: "TASKalfa 3554ci",
        serial: "KY77315",
        siteId: standorte.schule.id,
        room: "Lehrerzimmer",
      },
      {
        assetTag: "IT-00330",
        name: "Tablet Klasse 3b",
        type: AssetType.TABLET,
        manufacturer: "Apple",
        model: "iPad 10",
        serial: "AP553120",
        siteId: standorte.schule.id,
        assignedToId: claudia.id,
      },
      {
        assetTag: "IT-00415",
        name: "Diensthandy Fahrdienst",
        type: AssetType.TELEFON,
        manufacturer: "Samsung",
        model: "Galaxy A54",
        serial: "SM998210",
        siteId: standorte.verwaltung.id,
        assignedToId: fdRalf.id,
      },
      {
        assetTag: "IT-00520",
        name: "PC Gruppenraum Werkstatt",
        type: AssetType.PC,
        manufacturer: "Dell",
        model: "OptiPlex 3000",
        serial: "DL7712XX",
        siteId: standorte.werkstatt.id,
        assignedToId: sonja.id,
      },
    ].map((data) => db.asset.create({ data })),
  );

  // =========================================================================
  // Tickets
  // =========================================================================
  console.log("Lege Tickets an …");

  const zaehler = new Map<string, number>();

  interface NachrichtEingabe {
    typ?: MessageType;
    autor?: { id: string; name: string; email: string } | null;
    autorName?: string;
    autorEmail?: string;
    text: string;
    vorMs: number;
  }

  interface TicketEingabe {
    team: { id: string; key: string; mode: TeamMode };
    queueId: string;
    betreff: string;
    melder: { id: string; name: string; email: string };
    bearbeiter?: { id: string; name: string } | null;
    status: TicketStatus;
    prioritaet?: Priority;
    impact?: Impact;
    urgency?: Urgency;
    siteId?: string | null;
    raum?: string;
    kategorieId?: string;
    quelle?: TicketSource;
    erstelltVorMs: number;
    geloestVorMs?: number;
    formData?: Prisma.InputJsonValue;
    nachrichten: NachrichtEingabe[];
    tags?: string[];
    geraetIds?: string[];
    aufgaben?: { titel: string; erledigt: boolean }[];
    beobachter?: string[];
    wartetAufKind?: boolean;
  }

  async function ticketAnlegen(e: TicketEingabe) {
    const jahr = new Date(JETZT.getTime() - e.erstelltVorMs).getFullYear();
    const key = `${e.team.id}:${jahr}`;
    const naechste = (zaehler.get(key) ?? 0) + 1;
    zaehler.set(key, naechste);

    const erstellt = vor(e.erstelltVorMs);
    const prioritaet = e.prioritaet ?? Priority.NORMAL;
    const slaId =
      e.team.mode === TeamMode.VOLL
        ? slaMap.get(`${e.team.id}:${prioritaet}`)
        : undefined;

    const letzteNachricht = e.nachrichten.reduce(
      (min, n) => Math.min(min, n.vorMs),
      e.erstelltVorMs,
    );

    const erstAntwort = e.nachrichten.find(
      (n) => n.typ === MessageType.PUBLIC_REPLY && n.autor?.id !== e.melder.id,
    );

    const ticket = await db.ticket.create({
      data: {
        number: formatTicketNumber(e.team.key, jahr, naechste),
        subject: e.betreff,
        queueId: e.queueId,
        teamId: e.team.id,
        categoryId: e.kategorieId,
        siteId: e.siteId ?? null,
        roomOrArea: e.raum,
        requesterId: e.melder.id,
        assigneeId: e.bearbeiter?.id ?? null,
        status: e.status,
        priority: prioritaet,
        impact: e.impact,
        urgency: e.urgency,
        source: e.quelle ?? TicketSource.WEB,
        formData: e.formData ?? undefined,
        slaPolicyId: slaId,
        firstResponseAt: erstAntwort ? vor(erstAntwort.vorMs) : null,
        resolvedAt:
          e.geloestVorMs !== undefined ? vor(e.geloestVorMs) : null,
        closedAt:
          e.status === TicketStatus.GESCHLOSSEN && e.geloestVorMs !== undefined
            ? vor(e.geloestVorMs)
            : null,
        waitingForChild: e.wartetAufKind ?? false,
        createdAt: erstellt,
        updatedAt: vor(letzteNachricht),
        lastActivityAt: vor(letzteNachricht),
      },
    });

    await db.ticketEvent.create({
      data: {
        ticketId: ticket.id,
        type: EventType.CREATED,
        actorId: e.melder.id,
        actorName: e.melder.name,
        createdAt: erstellt,
      },
    });

    for (const n of e.nachrichten) {
      await db.ticketMessage.create({
        data: {
          ticketId: ticket.id,
          type: n.typ ?? MessageType.PUBLIC_REPLY,
          authorId: n.autor?.id ?? null,
          authorName: n.autor?.name ?? n.autorName,
          authorEmail: n.autor?.email ?? n.autorEmail,
          body: n.text,
          bodyText: n.text,
          createdAt: vor(n.vorMs),
          updatedAt: vor(n.vorMs),
        },
      });
    }

    if (e.bearbeiter) {
      await db.ticketEvent.create({
        data: {
          ticketId: ticket.id,
          type: EventType.CLAIMED,
          actorId: e.bearbeiter.id,
          actorName: e.bearbeiter.name,
          toValue: e.bearbeiter.name,
          createdAt: vor(e.erstelltVorMs - minuten(20)),
        },
      });
    }

    if (e.geloestVorMs !== undefined) {
      await db.ticketEvent.create({
        data: {
          ticketId: ticket.id,
          type: EventType.STATUS_CHANGED,
          actorId: e.bearbeiter?.id,
          actorName: e.bearbeiter?.name,
          fromValue: TicketStatus.IN_BEARBEITUNG,
          toValue: e.status,
          createdAt: vor(e.geloestVorMs),
        },
      });
    }

    if (e.tags?.length) {
      for (const name of e.tags) {
        const tag = tags.find((t) => t.name === name);
        if (tag) {
          await db.ticketTag.create({
            data: { ticketId: ticket.id, tagId: tag.id },
          });
        }
      }
    }

    if (e.geraetIds?.length) {
      for (const assetId of e.geraetIds) {
        await db.ticketAsset.create({ data: { ticketId: ticket.id, assetId } });
      }
    }

    if (e.aufgaben?.length) {
      for (const [i, a] of e.aufgaben.entries()) {
        await db.ticketTask.create({
          data: {
            ticketId: ticket.id,
            title: a.titel,
            done: a.erledigt,
            doneAt: a.erledigt ? vor(e.erstelltVorMs - stunden(4)) : null,
            sortOrder: i,
          },
        });
      }
    }

    if (e.beobachter?.length) {
      for (const userId of e.beobachter) {
        await db.ticketWatcher.create({ data: { ticketId: ticket.id, userId } });
      }
    }

    return ticket;
  }

  // --- Der Vorgang aus der Abnahme: Meldung an den Hausmeister -------------
  const heizung = await ticketAnlegen({
    team: hausmeister,
    queueId: hausmeisterEingaenge[standorte.wohnheimNord.id],
    betreff: "Heizung im Gruppenraum wird nicht warm",
    melder: anna,
    bearbeiter: hmFrank,
    status: TicketStatus.IN_BEARBEITUNG,
    prioritaet: Priority.HOCH,
    impact: Impact.GRUPPE,
    urgency: Urgency.BEEINTRAECHTIGT_ARBEIT,
    siteId: standorte.wohnheimNord.id,
    raum: "Gruppenraum 2, 1. OG",
    kategorieId: hmKategorieIds["Heizung und Lüftung"],
    erstelltVorMs: tage(2) + stunden(3),
    formData: { raum: "Gruppenraum 2", seitWann: "seit gestern Morgen" },
    tags: ["Ersatzteil bestellt"],
    nachrichten: [
      {
        autor: anna,
        text:
          "Der Heizkörper im Gruppenraum 2 wird seit gestern nicht mehr warm. " +
          "Oben ist er kalt, unten leicht lauwarm. Das Thermostat steht auf 5. " +
          "Der Raum wird von unserer Wohngruppe den ganzen Tag genutzt, im Moment " +
          "sind es dort knapp 17 Grad.",
        vorMs: tage(2) + stunden(3),
      },
      {
        autor: hmFrank,
        text:
          "Guten Tag Frau Bergmann,\n\nich schaue mir das heute Nachmittag an. " +
          "Klingt nach Luft im Heizkörper, das bekommen wir meist schnell hin.\n\n" +
          "Freundliche Grüße\nFrank Berger",
        vorMs: tage(2) + stunden(1),
      },
      {
        typ: MessageType.INTERNAL_NOTE,
        autor: hmFrank,
        text:
          "Entlüftet, brachte nur kurz Besserung. Das Thermostatventil ist fest. " +
          "Ersatzventil bestellt, Lieferung laut Händler Ende der Woche.",
        vorMs: tage(1) + stunden(20),
      },
      {
        autor: hmFrank,
        text:
          "Guten Tag Frau Bergmann,\n\nich war da und habe den Heizkörper entlüftet. " +
          "Das Ventil ist allerdings defekt und muss getauscht werden. Das Ersatzteil " +
          "ist bestellt und kommt Ende der Woche, dann baue ich es ein.\n\n" +
          "Bis dahin habe ich das Ventil so weit geöffnet, dass der Raum warm wird. " +
          "Regeln lässt er sich vorübergehend leider nicht.\n\n" +
          "Freundliche Grüße\nFrank Berger",
        vorMs: tage(1) + stunden(19),
      },
    ],
  });

  // --- Ein Vorgang mit Abspaltung: IT-Ticket, das auch den Hausmeister braucht
  const netzwerkdose = await ticketAnlegen({
    team: it,
    queueId: itEingang.id,
    betreff: "Neuer Arbeitsplatz Dienstzimmer: kein Netzwerk und keine Steckdose",
    melder: thomas,
    bearbeiter: itKlaus,
    status: TicketStatus.ZURUECKGESTELLT,
    prioritaet: Priority.NORMAL,
    siteId: standorte.wohnheimSued.id,
    raum: "Dienstzimmer EG",
    kategorieId: itKategorieIds["Netzwerk und WLAN"],
    erstelltVorMs: tage(5),
    wartetAufKind: true,
    nachrichten: [
      {
        autor: thomas,
        text:
          "Wir haben im Dienstzimmer einen zweiten Arbeitsplatz eingerichtet. " +
          "Dort fehlen aber eine Netzwerkdose und eine Steckdose. Der PC steht " +
          "schon da, wir können ihn nur nicht anschließen.",
        vorMs: tage(5),
      },
      {
        autor: itKlaus,
        text:
          "Guten Tag Herr Richter,\n\nden PC richte ich ein, sobald die Anschlüsse " +
          "liegen. Das Verlegen der Dosen ist Sache des Technischen Dienstes – ich " +
          "gebe den Teil direkt an die Hausmeister weiter, damit Sie sich nicht " +
          "erneut melden müssen. Sie bekommen gleich eine Nachricht mit der neuen " +
          "Vorgangsnummer.\n\nFreundliche Grüße\nKlaus Hoffmann",
        vorMs: tage(4) + stunden(20),
      },
      {
        typ: MessageType.INTERNAL_NOTE,
        autor: itKlaus,
        text:
          "Elektro- und Netzwerkdose an Hausmeister Wohnheim Süd abgespalten. " +
          "Unser Teil (PC einrichten, Patchen im Verteiler) wartet darauf.",
        vorMs: tage(4) + stunden(20),
      },
    ],
  });

  const netzwerkdoseKind = await ticketAnlegen({
    team: hausmeister,
    queueId: hausmeisterEingaenge[standorte.wohnheimSued.id],
    betreff: "Netzwerk- und Steckdose im Dienstzimmer setzen",
    melder: thomas,
    bearbeiter: hmTobias,
    status: TicketStatus.IN_BEARBEITUNG,
    prioritaet: Priority.NORMAL,
    siteId: standorte.wohnheimSued.id,
    raum: "Dienstzimmer EG",
    kategorieId: hmKategorieIds["Elektro und Beleuchtung"],
    erstelltVorMs: tage(4) + stunden(20),
    nachrichten: [
      {
        typ: MessageType.SYSTEM,
        text:
          "Dieser Vorgang wurde aus " +
          netzwerkdose.number +
          " abgespalten. Die dort ausgewählten Nachrichten sind unten angefügt.",
        vorMs: tage(4) + stunden(20),
      },
      {
        autor: thomas,
        text:
          "Wir haben im Dienstzimmer einen zweiten Arbeitsplatz eingerichtet. " +
          "Dort fehlen aber eine Netzwerkdose und eine Steckdose.",
        vorMs: tage(4) + stunden(20),
      },
      {
        autor: hmTobias,
        text:
          "Guten Tag Herr Richter,\n\nich habe das eingeplant. Material ist da, " +
          "ich komme am Donnerstag Vormittag. Bitte räumen Sie die Wand hinter dem " +
          "Schreibtisch frei.\n\nFreundliche Grüße\nTobias Lang",
        vorMs: tage(3) + stunden(6),
      },
    ],
  });

  await db.ticketLink.create({
    data: {
      fromId: netzwerkdose.id,
      toId: netzwerkdoseKind.id,
      type: LinkType.SPLIT_FROM,
      createdAt: vor(tage(4) + stunden(20)),
    },
  });
  await db.ticketEvent.createMany({
    data: [
      {
        ticketId: netzwerkdose.id,
        type: EventType.SPLIT,
        actorId: itKlaus.id,
        actorName: itKlaus.name,
        toValue: netzwerkdoseKind.number,
        note: "Elektro- und Netzwerkarbeiten an Hausmeister Wohnheim Süd abgespalten.",
        createdAt: vor(tage(4) + stunden(20)),
      },
      {
        ticketId: netzwerkdoseKind.id,
        type: EventType.SPLIT,
        actorId: itKlaus.id,
        actorName: itKlaus.name,
        fromValue: netzwerkdose.number,
        note: "Aus " + netzwerkdose.number + " abgespalten.",
        createdAt: vor(tage(4) + stunden(20)),
      },
    ],
  });

  // --- Ein weitergeleiteter Vorgang ---------------------------------------
  const dienstausweis = await ticketAnlegen({
    team: verwaltung,
    queueId: verwaltungEingang.id,
    betreff: "Dienstausweis verloren – Ersatz benötigt",
    melder: lukas,
    status: TicketStatus.OFFEN,
    siteId: standorte.schule.id,
    quelle: TicketSource.EMAIL,
    erstelltVorMs: tage(1) + stunden(5),
    nachrichten: [
      {
        autor: lukas,
        text:
          "Hallo,\n\nich habe meinen Dienstausweis verloren, vermutlich auf dem " +
          "Schulgelände. Wie bekomme ich einen Ersatz? Ich brauche ihn für die " +
          "Türen im Hauptgebäude.\n\nViele Grüße\nLukas Brandt",
        vorMs: tage(1) + stunden(5),
      },
    ],
  });

  await db.ticketEvent.create({
    data: {
      ticketId: dienstausweis.id,
      type: EventType.TRANSFERRED,
      actorId: itMehmet.id,
      actorName: itMehmet.name,
      fromValue: "IT",
      toValue: "Verwaltung",
      note:
        "Ausweise und Schließberechtigungen werden in der Verwaltung ausgegeben, " +
        "nicht in der IT.",
      createdAt: vor(tage(1) + stunden(4)),
    },
  });

  // --- Weitere Vorgänge, damit Listen und Auswertung etwas hergeben --------
  const weitere: TicketEingabe[] = [
    {
      team: it,
      queueId: itEingang.id,
      betreff: "Anmeldung am PC nicht möglich – Konto gesperrt",
      melder: heike,
      bearbeiter: itMehmet,
      status: TicketStatus.GELOEST,
      prioritaet: Priority.HOCH,
      urgency: Urgency.ARBEIT_STEHT_STILL,
      impact: Impact.EINZELNE_PERSON,
      siteId: standorte.wohnheimNord.id,
      kategorieId: itKategorieIds["Anmeldung und Konto"],
      erstelltVorMs: stunden(26),
      geloestVorMs: stunden(24),
      nachrichten: [
        {
          autor: heike,
          text:
            "Ich komme nicht mehr in meinen Computer. Es kommt die Meldung, dass " +
            "das Konto gesperrt ist. Ich habe das Passwort dreimal probiert.",
          vorMs: stunden(26),
        },
        {
          autor: itMehmet,
          text:
            "Guten Tag Frau Sommer,\n\nich habe die Sperre aufgehoben und das Passwort " +
            "zurückgesetzt. Das neue Kennwort gebe ich Ihnen gleich telefonisch durch. " +
            "Bitte ändern Sie es nach der ersten Anmeldung.\n\n" +
            "Freundliche Grüße\nMehmet Yılmaz",
          vorMs: stunden(25),
        },
        {
          autor: heike,
          text: "Hat geklappt, vielen Dank!",
          vorMs: stunden(24) + minuten(10),
        },
      ],
    },
    {
      team: it,
      queueId: itEingang.id,
      betreff: "Etagendrucker Wohnheim Nord zieht kein Papier ein",
      melder: anna,
      status: TicketStatus.NEU,
      prioritaet: Priority.NORMAL,
      siteId: standorte.wohnheimNord.id,
      raum: "Flur 1. OG",
      kategorieId: itKategorieIds["Drucker und Kopierer"],
      erstelltVorMs: stunden(3),
      formData: {
        druckerStandort: "Flur 1. OG, gegenüber Dienstzimmer",
        fehlerAmDisplay: "Papierstau prüfen – obere Kassette",
      },
      geraetIds: [geraete[2].id],
      nachrichten: [
        {
          autor: anna,
          text:
            "Der Drucker im Flur zieht kein Papier mehr ein. Im Display steht " +
            '"Papierstau prüfen – obere Kassette". Ich habe die Kassette schon ' +
            "herausgezogen, sehe aber kein eingeklemmtes Papier.",
          vorMs: stunden(3),
        },
      ],
    },
    {
      team: it,
      queueId: itEingang.id,
      betreff: "Dienstplanprogramm startet nicht mehr",
      melder: thomas,
      bearbeiter: itKlaus,
      status: TicketStatus.IN_BEARBEITUNG,
      prioritaet: Priority.HOCH,
      impact: Impact.GRUPPE,
      urgency: Urgency.BEEINTRAECHTIGT_ARBEIT,
      siteId: standorte.wohnheimSued.id,
      kategorieId: itKategorieIds["Fachanwendung"],
      erstelltVorMs: stunden(7),
      formData: { programm: "Dienstplanung" },
      tags: ["Wiederkehrend"],
      nachrichten: [
        {
          autor: thomas,
          text:
            "Die Dienstplanung lässt sich seit heute Morgen nicht mehr öffnen. " +
            'Es erscheint kurz ein Fenster und dann die Meldung "Verbindung zum ' +
            'Server konnte nicht hergestellt werden". Bei zwei Kolleginnen ist es ' +
            "genauso. Wir müssen heute den Plan für nächste Woche fertig machen.",
          vorMs: stunden(7),
        },
        {
          autor: itKlaus,
          text:
            "Guten Tag Herr Richter,\n\ndas Problem liegt beim Anbieter, wir haben " +
            "dort ein Ticket eröffnet. Ich melde mich, sobald es eine Rückmeldung gibt.\n\n" +
            "Freundliche Grüße\nKlaus Hoffmann",
          vorMs: stunden(6),
        },
        {
          typ: MessageType.INTERNAL_NOTE,
          autor: itKlaus,
          text:
            "Anbieter bestätigt Störung am Lizenzserver, Referenz DP-88213. " +
            "Angekündigte Behebung heute Nachmittag. Dritter Ausfall in diesem " +
            "Quartal – beim nächsten Mal beim Vertrag ansprechen.",
          vorMs: stunden(5) + minuten(30),
        },
      ],
    },
    {
      team: it,
      queueId: itEingang.id,
      betreff: "Neue Kollegin ab 01. des Monats – Konto und Notebook",
      melder: peBirgit,
      bearbeiter: itJana,
      status: TicketStatus.IN_BEARBEITUNG,
      prioritaet: Priority.NORMAL,
      siteId: standorte.schule.id,
      kategorieId: itKategorieIds["Neue Mitarbeiterin / neuer Mitarbeiter"],
      erstelltVorMs: tage(3),
      formData: {
        personName: "Nadine Kraus",
        eintritt: "01.",
        bereichNeu: "Förderschule, Klasse 2a",
        ausstattung: "Konto, Notebook und Telefon",
      },
      aufgaben: [
        { titel: "Benutzerkonto anlegen", erledigt: true },
        { titel: "E-Mail-Postfach einrichten", erledigt: true },
        { titel: "Berechtigungen für den Bereich vergeben", erledigt: true },
        { titel: "Zugang zur Klientendokumentation freischalten", erledigt: false },
        { titel: "Gerät vorbereiten und ausliefern", erledigt: false },
        { titel: "Telefon einrichten", erledigt: false },
        { titel: "Einweisung Datenschutz dokumentieren", erledigt: false },
      ],
      beobachter: [admin.id],
      nachrichten: [
        {
          autor: peBirgit,
          text:
            "Zum Monatsersten fängt Frau Nadine Kraus in der Förderschule an " +
            "(Klasse 2a). Bitte Konto, Notebook und ein Diensthandy einrichten. " +
            "Sie braucht Zugriff auf die Klientendokumentation.",
          vorMs: tage(3),
        },
        {
          autor: itJana,
          text:
            "Guten Tag Frau Walter,\n\nKonto und Postfach sind angelegt. Das Notebook " +
            "bereite ich diese Woche vor, das Telefon kommt mit der nächsten Lieferung. " +
            "Zum Zugriff auf die Dokumentation brauche ich noch die Freigabe der " +
            "Schulleitung.\n\nFreundliche Grüße\nJana Krause",
          vorMs: tage(2) + stunden(2),
        },
      ],
    },
    {
      team: it,
      queueId: itEingang.id,
      betreff: "WLAN im Werkstattgebäude bricht ständig ab",
      melder: sonja,
      status: TicketStatus.NEU,
      prioritaet: Priority.NORMAL,
      impact: Impact.STANDORT,
      siteId: standorte.werkstatt.id,
      kategorieId: itKategorieIds["Netzwerk und WLAN"],
      erstelltVorMs: stunden(11),
      tags: ["Wiederkehrend"],
      nachrichten: [
        {
          autor: sonja,
          text:
            "Das WLAN in der Halle 2 bricht mehrmals täglich ab. Die Tablets, mit " +
            "denen die Beschäftigten ihre Aufträge abrufen, verlieren dann die " +
            "Verbindung. Besonders schlimm ist es vormittags.",
          vorMs: stunden(11),
        },
      ],
    },
    {
      team: it,
      queueId: itEingang.id,
      betreff: "Bildschirm im Gruppenraum bleibt schwarz",
      melder: sonja,
      bearbeiter: itMehmet,
      status: TicketStatus.WARTET_AUF_MELDER,
      prioritaet: Priority.NORMAL,
      siteId: standorte.werkstatt.id,
      kategorieId: itKategorieIds["Computer und Notebook"],
      erstelltVorMs: tage(1) + stunden(9),
      geraetIds: [geraete[6].id],
      nachrichten: [
        {
          autor: sonja,
          text: "Der Bildschirm am PC im Gruppenraum bleibt schwarz, der Rechner läuft aber.",
          vorMs: tage(1) + stunden(9),
        },
        {
          autor: itMehmet,
          text:
            "Guten Tag Frau Peters,\n\nkönnten Sie bitte einmal prüfen, ob das Kabel " +
            "zwischen Bildschirm und Rechner an beiden Enden fest sitzt, und ob am " +
            "Bildschirm eine kleine Lampe leuchtet? Das grenzt die Ursache stark ein.\n\n" +
            "Freundliche Grüße\nMehmet Yılmaz",
          vorMs: tage(1) + stunden(7),
        },
      ],
    },
    {
      team: it,
      queueId: itEingang.id,
      betreff: "Telefon im Dienstzimmer klingelt nicht durch",
      melder: martin,
      status: TicketStatus.NEU,
      prioritaet: Priority.NIEDRIG,
      siteId: standorte.betreutesWohnen.id,
      kategorieId: itKategorieIds["Telefon"],
      erstelltVorMs: stunden(30),
      quelle: TicketSource.EMAIL,
      nachrichten: [
        {
          autor: martin,
          text:
            "Anrufe von außen kommen bei uns im Dienstzimmer nicht an, sie landen " +
            "direkt auf der Mailbox. Intern klingelt es normal.",
          vorMs: stunden(30),
        },
      ],
    },
    {
      team: it,
      queueId: itEingang.id,
      betreff: "Kopierer im Lehrerzimmer: Toner leer",
      melder: claudia,
      bearbeiter: itJana,
      status: TicketStatus.GESCHLOSSEN,
      prioritaet: Priority.NIEDRIG,
      siteId: standorte.schule.id,
      raum: "Lehrerzimmer",
      kategorieId: itKategorieIds["Drucker und Kopierer"],
      erstelltVorMs: tage(9),
      geloestVorMs: tage(8) + stunden(4),
      geraetIds: [geraete[3].id],
      nachrichten: [
        {
          autor: claudia,
          text: "Der Kopierer im Lehrerzimmer meldet, dass der Toner leer ist.",
          vorMs: tage(9),
        },
        {
          autor: itJana,
          text:
            "Guten Tag Frau Schmitt,\n\nich habe den Toner getauscht, das Gerät " +
            "läuft wieder. Ein Ersatzkartusche liegt jetzt im Schrank daneben.\n\n" +
            "Freundliche Grüße\nJana Krause",
          vorMs: tage(8) + stunden(4),
        },
      ],
    },
    {
      team: hausmeister,
      queueId: hausmeisterEingaenge[standorte.schule.id],
      betreff: "Automatiktür Haupteingang schließt zu schnell",
      melder: claudia,
      bearbeiter: hmAndreas,
      status: TicketStatus.IN_BEARBEITUNG,
      prioritaet: Priority.HOCH,
      impact: Impact.STANDORT,
      siteId: standorte.schule.id,
      raum: "Haupteingang",
      kategorieId: hmKategorieIds["Barrierefreiheit"],
      erstelltVorMs: tage(1) + stunden(2),
      tags: ["Barrierefreiheit", "Sicherheit"],
      nachrichten: [
        {
          autor: claudia,
          text:
            "Die Automatiktür am Haupteingang schließt deutlich schneller als früher. " +
            "Für unsere Schülerinnen und Schüler mit Rollstuhl oder Rollator ist die " +
            "Zeit zu knapp, gestern ist eine Tür gegen einen Rollator gefahren. Das " +
            "sollte dringend eingestellt werden.",
          vorMs: tage(1) + stunden(2),
        },
        {
          autor: hmAndreas,
          text:
            "Guten Tag Frau Schmitt,\n\ndanke für den Hinweis, das nehme ich ernst. " +
            "Ich habe die Offenhaltezeit heute provisorisch verlängert. Die " +
            "Wartungsfirma kommt am Montag und stellt die Tür sauber ein.\n\n" +
            "Freundliche Grüße\nAndreas Schuster",
          vorMs: tage(1),
        },
        {
          typ: MessageType.INTERNAL_NOTE,
          autor: hmAndreas,
          text:
            "Wartungsfirma informiert (Auftrag 2026-4471). Bei der Gelegenheit die " +
            "Sensorleiste mit prüfen lassen – sie hat gestern nicht ausgelöst.",
          vorMs: tage(1) - minuten(30),
        },
      ],
    },
    {
      team: hausmeister,
      queueId: hausmeisterEingaenge[standorte.wohnheimNord.id],
      betreff: "Wasserhahn im Bad Zimmer 14 tropft",
      melder: heike,
      status: TicketStatus.NEU,
      prioritaet: Priority.NIEDRIG,
      siteId: standorte.wohnheimNord.id,
      raum: "Zimmer 14, Bad",
      kategorieId: hmKategorieIds["Sanitär"],
      erstelltVorMs: stunden(20),
      nachrichten: [
        {
          autor: heike,
          text: "Der Wasserhahn im Bad von Zimmer 14 tropft dauerhaft, auch fest zugedreht.",
          vorMs: stunden(20),
        },
      ],
    },
    {
      team: hausmeister,
      queueId: hausmeisterEingaenge[standorte.wohnheimNord.id],
      betreff: "Licht im Treppenhaus flackert",
      melder: anna,
      bearbeiter: hmFrank,
      status: TicketStatus.GELOEST,
      prioritaet: Priority.NORMAL,
      siteId: standorte.wohnheimNord.id,
      raum: "Treppenhaus zwischen EG und 1. OG",
      kategorieId: hmKategorieIds["Elektro und Beleuchtung"],
      erstelltVorMs: tage(6),
      geloestVorMs: tage(5) + stunden(3),
      nachrichten: [
        {
          autor: anna,
          text:
            "Das Licht im Treppenhaus flackert seit ein paar Tagen. Für einige " +
            "Bewohnerinnen und Bewohner ist das sehr unangenehm.",
          vorMs: tage(6),
        },
        {
          autor: hmFrank,
          text:
            "Guten Tag Frau Bergmann,\n\ndie Leuchtstoffröhre war defekt, ich habe sie " +
            "getauscht. Bei der Gelegenheit habe ich gleich auf LED umgestellt – die " +
            "flackert prinzipbedingt nicht.\n\nFreundliche Grüße\nFrank Berger",
          vorMs: tage(5) + stunden(3),
        },
      ],
    },
    {
      team: hausmeister,
      queueId: hausmeisterEingaenge[standorte.werkstatt.id],
      betreff: "Rolltor Halle 2 lässt sich nur mit Mühe öffnen",
      melder: jonas,
      bearbeiter: hmTobias,
      status: TicketStatus.OFFEN,
      prioritaet: Priority.HOCH,
      siteId: standorte.werkstatt.id,
      raum: "Halle 2",
      kategorieId: hmKategorieIds["Türen, Fenster, Schlösser"],
      erstelltVorMs: tage(2) + stunden(6),
      tags: ["Sicherheit"],
      nachrichten: [
        {
          autor: jonas,
          text:
            "Das Rolltor an Halle 2 hakt beim Öffnen und bleibt manchmal auf halber " +
            "Höhe stehen. Wir bringen dort morgens die Lieferungen rein.",
          vorMs: tage(2) + stunden(6),
        },
      ],
    },
    {
      team: hausmeister,
      queueId: hausmeisterEingaenge[standorte.betreutesWohnen.id],
      betreff: "Handlauf im Flur hat sich gelöst",
      melder: martin,
      bearbeiter: hmTobias,
      status: TicketStatus.GELOEST,
      prioritaet: Priority.DRINGEND,
      impact: Impact.GRUPPE,
      urgency: Urgency.ARBEIT_STEHT_STILL,
      siteId: standorte.betreutesWohnen.id,
      raum: "Flur EG",
      kategorieId: hmKategorieIds["Barrierefreiheit"],
      erstelltVorMs: tage(4) + stunden(2),
      geloestVorMs: tage(4),
      tags: ["Barrierefreiheit", "Sicherheit"],
      nachrichten: [
        {
          autor: martin,
          text:
            "Der Handlauf im Flur im Erdgeschoss hat sich an zwei Halterungen gelöst " +
            "und wackelt stark. Mehrere Bewohnerinnen und Bewohner stützen sich beim " +
            "Gehen darauf. Das ist gerade ein echtes Sturzrisiko.",
          vorMs: tage(4) + stunden(2),
        },
        {
          autor: hmTobias,
          text:
            "Guten Tag Herr Gruber,\n\nich bin sofort gekommen und habe den Handlauf " +
            "neu verdübelt und alle Halterungen im Flur nachgezogen. Er sitzt jetzt " +
            "wieder fest.\n\nFreundliche Grüße\nTobias Lang",
          vorMs: tage(4),
        },
      ],
    },
    {
      team: hausmeister,
      queueId: hausmeisterEingaenge[standorte.verwaltung.id],
      betreff: "Zwei Schreibtische in den 2. Stock bringen",
      melder: vwPetra,
      status: TicketStatus.NEU,
      prioritaet: Priority.NIEDRIG,
      siteId: standorte.verwaltung.id,
      kategorieId: hmKategorieIds["Möbel und Einrichtung"],
      erstelltVorMs: stunden(50),
      nachrichten: [
        {
          autor: vwPetra,
          text:
            "Die zwei neuen Schreibtische stehen noch im Lager im Keller. Könnten Sie " +
            "sie in Raum 2.14 bringen und aufbauen? Es eilt nicht, in den nächsten " +
            "zwei Wochen reicht völlig.",
          vorMs: stunden(50),
        },
      ],
    },
    {
      team: hausmeister,
      queueId: hausmeisterEingaenge[standorte.wohnheimSued.id],
      betreff: "Außenbeleuchtung Parkplatz ausgefallen",
      melder: thomas,
      status: TicketStatus.NEU,
      prioritaet: Priority.HOCH,
      siteId: standorte.wohnheimSued.id,
      raum: "Parkplatz / Zuwegung",
      kategorieId: hmKategorieIds["Außenanlagen"],
      erstelltVorMs: stunden(14),
      tags: ["Sicherheit"],
      nachrichten: [
        {
          autor: thomas,
          text:
            "Die Beleuchtung am Parkplatz und auf dem Weg zum Haupteingang ist " +
            "komplett aus. Beim Spätdienst ist es dort jetzt stockdunkel.",
          vorMs: stunden(14),
        },
      ],
    },
    {
      team: kueche,
      queueId: kuecheEingang.id,
      betreff: "Sonderkost für neuen Bewohner ab Montag",
      melder: anna,
      bearbeiter: kuMarion,
      status: TicketStatus.GELOEST,
      siteId: standorte.wohnheimNord.id,
      erstelltVorMs: tage(3) + stunden(4),
      geloestVorMs: tage(3),
      nachrichten: [
        {
          autor: anna,
          text:
            "Ab Montag zieht ein neuer Bewohner bei uns ein, der pürierte Kost " +
            "benötigt. Können Sie das ab Montag Mittag einplanen? Details bespreche " +
            "ich gern telefonisch.",
          vorMs: tage(3) + stunden(4),
        },
        {
          autor: kuMarion,
          text:
            "Guten Tag Frau Bergmann,\n\nist eingetragen, ab Montag Mittag. Bitte rufen " +
            "Sie mich kurz an, damit wir die Einzelheiten ohne schriftliche " +
            "Gesundheitsangaben klären.\n\nFreundliche Grüße\nMarion Fischer",
          vorMs: tage(3),
        },
      ],
    },
    {
      team: kueche,
      queueId: kuecheEingang.id,
      betreff: "Essenslieferung Werkstatt kam heute zu spät",
      melder: jonas,
      status: TicketStatus.OFFEN,
      siteId: standorte.werkstatt.id,
      erstelltVorMs: stunden(6),
      nachrichten: [
        {
          autor: jonas,
          text:
            "Das Mittagessen kam heute erst um 12:40 statt um 12:00. Die Pausenzeiten " +
            "der Beschäftigten sind dadurch durcheinandergeraten. Kann das wieder " +
            "pünktlich klappen?",
          vorMs: stunden(6),
        },
      ],
    },
    {
      team: fahrdienst,
      queueId: fahrdienstEingang.id,
      betreff: "Fahrt zum Facharzttermin am Donnerstag",
      melder: heike,
      bearbeiter: fdRalf,
      status: TicketStatus.GELOEST,
      siteId: standorte.wohnheimNord.id,
      erstelltVorMs: tage(2) + stunden(8),
      geloestVorMs: tage(2) + stunden(5),
      nachrichten: [
        {
          autor: heike,
          text:
            "Für Donnerstag um 10:30 brauchen wir eine Fahrt zum Facharzt in " +
            "Musterstadt 2. Hin- und Rückfahrt, Rollstuhltransport nötig.",
          vorMs: tage(2) + stunden(8),
        },
        {
          autor: fdRalf,
          text:
            "Guten Tag Frau Sommer,\n\ndie Fahrt ist eingeplant. Abholung 09:45 am " +
            "Haupteingang, mit dem Rollstuhlbus.\n\nFreundliche Grüße\nRalf Möller",
          vorMs: tage(2) + stunden(5),
        },
      ],
    },
    {
      team: fahrdienst,
      queueId: fahrdienstEingang.id,
      betreff: "Schulbus: Abholzeit für zwei Kinder ändern",
      melder: claudia,
      status: TicketStatus.OFFEN,
      siteId: standorte.schule.id,
      erstelltVorMs: stunden(28),
      nachrichten: [
        {
          autor: claudia,
          text:
            "Ab nächster Woche endet der Unterricht der Klasse 2a dienstags eine " +
            "Stunde früher. Zwei Kinder müssten dann entsprechend früher abgeholt " +
            "werden. Geht das?",
          vorMs: stunden(28),
        },
      ],
    },
    {
      team: verwaltung,
      queueId: verwaltungEingang.id,
      betreff: "Bescheinigung für die Elternzeit benötigt",
      melder: lukas,
      bearbeiter: vwPetra,
      status: TicketStatus.WARTET_AUF_MELDER,
      siteId: standorte.schule.id,
      erstelltVorMs: tage(2),
      nachrichten: [
        {
          autor: lukas,
          text: "Ich brauche eine Bescheinigung über mein Beschäftigungsverhältnis.",
          vorMs: tage(2),
        },
        {
          autor: vwPetra,
          text:
            "Guten Tag Herr Brandt,\n\nfür welche Stelle brauchen Sie die Bescheinigung " +
            "und bis wann? Danach kann ich sie passend ausstellen.\n\n" +
            "Freundliche Grüße\nPetra Neumann",
          vorMs: tage(1) + stunden(18),
        },
      ],
    },
    {
      team: verwaltung,
      queueId: verwaltungEingang.id,
      betreff: "Büromaterial für die Wohngruppe bestellen",
      melder: martin,
      status: TicketStatus.NEU,
      siteId: standorte.betreutesWohnen.id,
      erstelltVorMs: stunden(9),
      nachrichten: [
        {
          autor: martin,
          text:
            "Wir brauchen für das Dienstzimmer: 5 Ordner breit, 2 Päckchen " +
            "Druckerpapier, Textmarker gemischt und Haftnotizen.",
          vorMs: stunden(9),
        },
      ],
    },
  ];

  for (const t of weitere) {
    await ticketAnlegen(t);
  }

  // --- Gespeicherte Ansichten für die Voll-Bereiche -----------------------
  await db.savedView.createMany({
    data: [
      {
        userId: itKlaus.id,
        name: "Meine offenen",
        filters: { assignee: "me", status: ["NEU", "OFFEN", "IN_BEARBEITUNG"] },
        sortOrder: 0,
      },
      {
        userId: itKlaus.id,
        name: "Ohne Zuweisung",
        filters: { assignee: "none", status: ["NEU", "OFFEN"] },
        sortOrder: 1,
      },
      {
        userId: hmFrank.id,
        name: "Dringend an meinen Standorten",
        filters: { priority: ["DRINGEND", "HOCH"], status: ["NEU", "OFFEN"] },
        sortOrder: 0,
      },
    ],
  });

  // =========================================================================
  // Zusammenfassung
  // =========================================================================
  const anzahlTickets = await db.ticket.count();
  const anzahlBenutzer = await db.user.count();

  console.log(`
Fertig.

  Standorte:  ${alleStandorte.length}
  Bereiche:   6  (IT und Hausmeister im Voll-Modus, 4 im Einfach-Modus)
  Eingänge:   ${await db.queue.count()}
  Benutzer:   ${anzahlBenutzer}
  Tickets:    ${anzahlTickets}

Anmeldung – Passwort für alle Demo-Konten: ${DEMO_PASSWORT}

  admin@example.org             Sabine Reuter     Administration, Leitung IT
  klaus.hoffmann@example.org    Klaus Hoffmann    IT (Voll-Modus)
  frank.berger@example.org      Frank Berger      Hausmeister Wohnheim Nord + Verwaltung
  tobias.lang@example.org       Tobias Lang       Hausmeister Süd, Betr. Wohnen, Werkstatt
  andreas.schuster@example.org  Andreas Schuster  Hausmeister Schule
  dieter.wolf@example.org       Dieter Wolf       Leitung Technischer Dienst (sieht alle Standorte)
  marion.fischer@example.org    Marion Fischer    Küche (Einfach-Modus)
  anna.bergmann@example.org     Anna Bergmann     Melderin, Wohnheim Nord (kein Bereich)
  claudia.schmitt@example.org   Claudia Schmitt   Melderin, Schule (große Schrift, einfache Ansicht)
`);
}

main()
  .catch((error) => {
    console.error(error);
    process.exit(1);
  })
  .finally(async () => {
    await db.$disconnect();
  });
