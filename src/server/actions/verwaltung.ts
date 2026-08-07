"use server";

import { revalidatePath } from "next/cache";
import {
  GlobalRole,
  MailTransport,
  QueueMode,
  SiteType,
  TeamMode,
  TeamRole,
  UnknownSenderPolicy,
} from "@prisma/client";
import { z } from "zod";

import { db } from "@/server/db";
import { encryptOptional } from "@/lib/crypto";
import { hashPassword, validatePasswordStrength } from "@/server/password";
import { canManageTeam, requireActor, requireAdmin } from "@/server/policy";
import { logAudit } from "@/server/services/events";
import { syncQueuesForTeam } from "@/server/services/routing";
import { slugify } from "@/lib/utils";

/**
 * Verwaltung.
 *
 * Jede Aktion prüft zuerst die Berechtigung und schreibt danach einen
 * Protokolleintrag. Änderungen an Bereichen, Freigaben und Postfächern greifen
 * tief in das System ein – wer sie vorgenommen hat, muss nachvollziehbar sein.
 */

// ===========================================================================
// Bereiche
// ===========================================================================

const bereichSchema = z.object({
  key: z
    .string()
    .trim()
    .min(2, "Das Kürzel braucht mindestens 2 Zeichen.")
    .max(8, "Das Kürzel darf höchstens 8 Zeichen haben.")
    .regex(
      /^[A-Za-z][A-Za-z0-9]*$/,
      "Das Kürzel darf nur Buchstaben und Ziffern enthalten und muss mit einem Buchstaben beginnen.",
    ),
  name: z.string().trim().min(2, "Bitte geben Sie einen Namen an.").max(60),
  mode: z.nativeEnum(TeamMode),
  queueMode: z.nativeEnum(QueueMode),
  description: z.string().trim().max(500).optional().or(z.literal("")),
  color: z.string().regex(/^#[0-9a-fA-F]{6}$/, "Bitte eine Farbe wählen."),
  icon: z.string().trim().min(1),
  portalVisible: z.boolean(),
  portalLabel: z.string().trim().max(120).optional().or(z.literal("")),
  portalDescription: z.string().trim().max(300).optional().or(z.literal("")),
  allowEmailCommands: z.boolean(),
  notifyRequesterOnTransfer: z.boolean(),
  isActive: z.boolean(),
});

export async function bereichAnlegenAction(input: z.infer<typeof bereichSchema>) {
  const actor = await requireAdmin();
  const daten = bereichSchema.parse(input);
  const key = daten.key.toUpperCase();

  const vorhanden = await db.team.findFirst({
    where: { OR: [{ key }, { name: daten.name }] },
    select: { key: true, name: true },
  });
  if (vorhanden) {
    throw new Error(
      vorhanden.key === key
        ? `Das Kürzel "${key}" ist bereits vergeben.`
        : `Ein Bereich mit dem Namen "${daten.name}" existiert bereits.`,
    );
  }

  const letzter = await db.team.findFirst({
    orderBy: { sortOrder: "desc" },
    select: { sortOrder: true },
  });

  const team = await db.team.create({
    data: {
      key,
      name: daten.name,
      mode: daten.mode,
      queueMode: daten.queueMode,
      description: daten.description || null,
      color: daten.color,
      icon: daten.icon,
      portalVisible: daten.portalVisible,
      portalLabel: daten.portalLabel || null,
      portalDescription: daten.portalDescription || null,
      allowEmailCommands: daten.allowEmailCommands,
      notifyRequesterOnTransfer: daten.notifyRequesterOnTransfer,
      isActive: daten.isActive,
      sortOrder: (letzter?.sortOrder ?? -1) + 1,
    },
  });

  // Ein Bereich ohne Eingang kann keine Vorgänge annehmen – deshalb entstehen
  // die Eingänge direkt mit.
  await syncQueuesForTeam(team.id);

  await logAudit({
    actorId: actor.id,
    action: "team.create",
    entityType: "Team",
    entityId: team.id,
    summary: `Bereich "${team.name}" angelegt (${daten.mode}, ${daten.queueMode}).`,
  });

  revalidatePath("/verwaltung/bereiche");
  revalidatePath("/", "layout");
  return { ok: true as const, id: team.id };
}

export async function bereichAendernAction(
  teamId: string,
  input: z.infer<typeof bereichSchema>,
) {
  const actor = await requireActor();
  if (!canManageTeam(actor, teamId)) {
    throw new Error("Diesen Bereich dürfen Sie nicht ändern.");
  }

  const daten = bereichSchema.parse(input);
  const vorher = await db.team.findUniqueOrThrow({
    where: { id: teamId },
    select: { key: true, name: true, mode: true, queueMode: true },
  });

  // Kürzel und Struktur darf nur die Administration ändern – das Kürzel steckt
  // in allen bisherigen Ticketnummern, die Struktur bestimmt die Zustellung.
  const strukturAenderung =
    vorher.queueMode !== daten.queueMode ||
    vorher.key !== daten.key.toUpperCase();

  if (strukturAenderung && !actor.isAdmin) {
    throw new Error(
      "Kürzel und Eingangsstruktur kann nur die Administration ändern.",
    );
  }

  const team = await db.team.update({
    where: { id: teamId },
    data: {
      key: daten.key.toUpperCase(),
      name: daten.name,
      mode: daten.mode,
      queueMode: daten.queueMode,
      description: daten.description || null,
      color: daten.color,
      icon: daten.icon,
      portalVisible: daten.portalVisible,
      portalLabel: daten.portalLabel || null,
      portalDescription: daten.portalDescription || null,
      allowEmailCommands: daten.allowEmailCommands,
      notifyRequesterOnTransfer: daten.notifyRequesterOnTransfer,
      isActive: daten.isActive,
    },
  });

  // Bei geänderter Struktur die Eingänge nachziehen. Bestehende Eingänge
  // werden dabei nie gelöscht, nur stillgelegt – sonst gingen die darin
  // liegenden Vorgänge verloren.
  if (vorher.queueMode !== daten.queueMode) {
    await syncQueuesForTeam(teamId);
  }

  const aenderungen: string[] = [];
  if (vorher.mode !== daten.mode) {
    aenderungen.push(`Ausbaustufe ${vorher.mode} → ${daten.mode}`);
  }
  if (vorher.queueMode !== daten.queueMode) {
    aenderungen.push(`Eingangsstruktur ${vorher.queueMode} → ${daten.queueMode}`);
  }

  await logAudit({
    actorId: actor.id,
    action: "team.update",
    entityType: "Team",
    entityId: teamId,
    summary:
      `Bereich "${team.name}" geändert` +
      (aenderungen.length ? `: ${aenderungen.join(", ")}.` : "."),
    meta: { vorher, nachher: { mode: daten.mode, queueMode: daten.queueMode } },
  });

  revalidatePath("/verwaltung/bereiche");
  revalidatePath("/", "layout");
  return { ok: true as const };
}

// ===========================================================================
// Eingänge
// ===========================================================================

const eingangSchema = z.object({
  name: z.string().trim().min(2).max(80),
  inboundEmail: z
    .string()
    .trim()
    .email("Bitte eine gültige E-Mail-Adresse angeben.")
    .optional()
    .or(z.literal("")),
  portalVisible: z.boolean(),
  isActive: z.boolean(),
});

export async function eingangAendernAction(
  queueId: string,
  input: z.infer<typeof eingangSchema>,
) {
  const actor = await requireActor();
  const queue = await db.queue.findUniqueOrThrow({
    where: { id: queueId },
    select: { teamId: true, name: true },
  });

  if (!canManageTeam(actor, queue.teamId)) {
    throw new Error("Diesen Eingang dürfen Sie nicht ändern.");
  }

  const daten = eingangSchema.parse(input);

  if (daten.inboundEmail) {
    const belegt = await db.queue.findFirst({
      where: { inboundEmail: daten.inboundEmail, id: { not: queueId } },
      select: { name: true },
    });
    if (belegt) {
      throw new Error(
        `Die Adresse ${daten.inboundEmail} ist bereits dem Eingang "${belegt.name}" zugeordnet.`,
      );
    }
  }

  await db.queue.update({
    where: { id: queueId },
    data: {
      name: daten.name,
      inboundEmail: daten.inboundEmail || null,
      portalVisible: daten.portalVisible,
      isActive: daten.isActive,
    },
  });

  await logAudit({
    actorId: actor.id,
    action: "queue.update",
    entityType: "Queue",
    entityId: queueId,
    summary: `Eingang "${daten.name}" geändert.`,
  });

  revalidatePath("/verwaltung/eingaenge");
  revalidatePath("/", "layout");
  return { ok: true as const };
}

// ===========================================================================
// Benutzer, Mitgliedschaften, Freigaben
// ===========================================================================

const benutzerSchema = z.object({
  email: z.string().trim().email("Bitte eine gültige E-Mail-Adresse angeben."),
  name: z.string().trim().min(2, "Bitte geben Sie den Namen an.").max(120),
  globalRole: z.nativeEnum(GlobalRole),
  siteId: z.string().optional().or(z.literal("")),
  phone: z.string().trim().max(40).optional().or(z.literal("")),
  jobTitle: z.string().trim().max(120).optional().or(z.literal("")),
  department: z.string().trim().max(120).optional().or(z.literal("")),
  isActive: z.boolean(),
});

export async function benutzerAnlegenAction(
  input: z.infer<typeof benutzerSchema> & { passwort?: string },
) {
  const actor = await requireAdmin();
  const daten = benutzerSchema.parse(input);
  const email = daten.email.toLowerCase();

  const vorhanden = await db.user.findUnique({
    where: { email },
    select: { id: true },
  });
  if (vorhanden) {
    throw new Error(`Für ${email} gibt es bereits ein Konto.`);
  }

  let passwordHash: string | null = null;
  if (input.passwort) {
    const problem = validatePasswordStrength(input.passwort);
    if (problem) throw new Error(problem);
    passwordHash = await hashPassword(input.passwort);
  }

  const user = await db.user.create({
    data: {
      email,
      name: daten.name,
      globalRole: daten.globalRole,
      siteId: daten.siteId || null,
      phone: daten.phone || null,
      jobTitle: daten.jobTitle || null,
      department: daten.department || null,
      isActive: daten.isActive,
      passwordHash,
      emailVerified: new Date(),
    },
  });

  await logAudit({
    actorId: actor.id,
    action: "user.create",
    entityType: "User",
    entityId: user.id,
    summary: `Konto für ${user.name} (${email}) angelegt.`,
  });

  revalidatePath("/verwaltung/benutzer");
  return { ok: true as const, id: user.id };
}

export async function benutzerAendernAction(
  userId: string,
  input: z.infer<typeof benutzerSchema>,
) {
  const actor = await requireAdmin();
  const daten = benutzerSchema.parse(input);

  // Die eigene Administrationsrolle darf man sich nicht selbst entziehen –
  // sonst sperrt sich die letzte Administration versehentlich aus.
  if (userId === actor.id && daten.globalRole !== GlobalRole.ADMIN) {
    throw new Error(
      "Sie können sich die eigenen Administrationsrechte nicht entziehen. " +
        "Lassen Sie das von einer anderen Administration erledigen.",
    );
  }
  if (userId === actor.id && !daten.isActive) {
    throw new Error("Sie können Ihr eigenes Konto nicht deaktivieren.");
  }

  const user = await db.user.update({
    where: { id: userId },
    data: {
      email: daten.email.toLowerCase(),
      name: daten.name,
      globalRole: daten.globalRole,
      siteId: daten.siteId || null,
      phone: daten.phone || null,
      jobTitle: daten.jobTitle || null,
      department: daten.department || null,
      isActive: daten.isActive,
    },
  });

  await logAudit({
    actorId: actor.id,
    action: "user.update",
    entityType: "User",
    entityId: userId,
    summary: `Konto ${user.name} geändert.`,
  });

  revalidatePath("/verwaltung/benutzer");
  revalidatePath("/", "layout");
  return { ok: true as const };
}

export async function passwortZuruecksetzenAction(
  userId: string,
  neuesPasswort: string,
) {
  const actor = await requireAdmin();

  const problem = validatePasswordStrength(neuesPasswort);
  if (problem) throw new Error(problem);

  const user = await db.user.update({
    where: { id: userId },
    data: { passwordHash: await hashPassword(neuesPasswort) },
    select: { name: true, email: true },
  });

  // Das Passwort selbst wird nie protokolliert – nur die Tatsache.
  await logAudit({
    actorId: actor.id,
    action: "user.reset-password",
    entityType: "User",
    entityId: userId,
    summary: `Passwort für ${user.name} zurückgesetzt.`,
  });

  return { ok: true as const };
}

/** Mitgliedschaft in einem Bereich setzen oder aufheben. */
export async function mitgliedschaftAction(
  userId: string,
  teamId: string,
  role: TeamRole | null,
) {
  const actor = await requireActor();
  if (!canManageTeam(actor, teamId)) {
    throw new Error("Mitgliedschaften in diesem Bereich dürfen Sie nicht ändern.");
  }

  const [user, team] = await Promise.all([
    db.user.findUniqueOrThrow({
      where: { id: userId },
      select: { name: true, isExternal: true },
    }),
    db.team.findUniqueOrThrow({ where: { id: teamId }, select: { name: true } }),
  ]);

  // Externe Melder entstehen aus eingehenden Mails und haben kein Passwort –
  // sie können sich nicht anmelden und deshalb auch nicht mitarbeiten.
  if (user.isExternal && role) {
    throw new Error(
      `${user.name} ist ein externer Melder ohne Anmeldemöglichkeit und kann ` +
        `nicht in einem Bereich mitarbeiten.`,
    );
  }

  if (role) {
    await db.teamMembership.upsert({
      where: { userId_teamId: { userId, teamId } },
      create: { userId, teamId, role },
      update: { role },
    });
  } else {
    await db.teamMembership.deleteMany({ where: { userId, teamId } });
    // Ohne Mitgliedschaft ergeben die Eingangsfreigaben keinen Sinn mehr.
    await db.queueAccess.deleteMany({
      where: { userId, queue: { teamId } },
    });
  }

  await logAudit({
    actorId: actor.id,
    action: role ? "membership.set" : "membership.remove",
    entityType: "User",
    entityId: userId,
    summary: role
      ? `${user.name} ist jetzt ${role} im Bereich ${team.name}.`
      : `${user.name} wurde aus dem Bereich ${team.name} entfernt.`,
  });

  revalidatePath("/verwaltung/benutzer");
  revalidatePath("/", "layout");
  return { ok: true as const };
}

/** Freigabe für einen Standort-Eingang setzen oder aufheben. */
export async function eingangsFreigabeAction(
  userId: string,
  queueId: string,
  freigeben: boolean,
  alsStartansicht = false,
) {
  const actor = await requireActor();
  const queue = await db.queue.findUniqueOrThrow({
    where: { id: queueId },
    select: { teamId: true, name: true },
  });

  if (!canManageTeam(actor, queue.teamId)) {
    throw new Error("Freigaben für diesen Eingang dürfen Sie nicht ändern.");
  }

  const user = await db.user.findUniqueOrThrow({
    where: { id: userId },
    select: { name: true },
  });

  if (freigeben) {
    // Ohne Mitgliedschaft im Bereich bringt die Freigabe nichts – die
    // Rechteprüfung verlangt beides.
    const mitglied = await db.teamMembership.findFirst({
      where: { userId, teamId: queue.teamId },
      select: { id: true },
    });
    if (!mitglied) {
      throw new Error(
        `${user.name} ist kein Mitglied dieses Bereichs. Bitte zuerst die ` +
          `Mitgliedschaft setzen – ohne sie bleibt die Freigabe wirkungslos.`,
      );
    }

    if (alsStartansicht) {
      await db.queueAccess.updateMany({
        where: { userId, queue: { teamId: queue.teamId } },
        data: { isDefault: false },
      });
    }

    await db.queueAccess.upsert({
      where: { userId_queueId: { userId, queueId } },
      create: { userId, queueId, isDefault: alsStartansicht },
      update: { isDefault: alsStartansicht },
    });
  } else {
    await db.queueAccess.deleteMany({ where: { userId, queueId } });
  }

  await logAudit({
    actorId: actor.id,
    action: freigeben ? "queue-access.grant" : "queue-access.revoke",
    entityType: "User",
    entityId: userId,
    summary: freigeben
      ? `${user.name} für Eingang "${queue.name}" freigegeben.`
      : `Freigabe von ${user.name} für Eingang "${queue.name}" aufgehoben.`,
  });

  revalidatePath("/verwaltung/benutzer");
  revalidatePath("/", "layout");
  return { ok: true as const };
}

// ===========================================================================
// Standorte
// ===========================================================================

const standortSchema = z.object({
  name: z.string().trim().min(2, "Bitte geben Sie einen Namen an.").max(80),
  shortName: z.string().trim().max(40).optional().or(z.literal("")),
  type: z.nativeEnum(SiteType),
  street: z.string().trim().max(120).optional().or(z.literal("")),
  zip: z.string().trim().max(10).optional().or(z.literal("")),
  city: z.string().trim().max(80).optional().or(z.literal("")),
  isActive: z.boolean(),
});

export async function standortSpeichernAction(
  input: z.infer<typeof standortSchema> & { id?: string },
) {
  const actor = await requireAdmin();
  const daten = standortSchema.parse(input);

  const site = input.id
    ? await db.site.update({
        where: { id: input.id },
        data: {
          name: daten.name,
          shortName: daten.shortName || null,
          type: daten.type,
          street: daten.street || null,
          zip: daten.zip || null,
          city: daten.city || null,
          isActive: daten.isActive,
        },
      })
    : await db.site.create({
        data: {
          name: daten.name,
          shortName: daten.shortName || null,
          type: daten.type,
          street: daten.street || null,
          zip: daten.zip || null,
          city: daten.city || null,
          isActive: daten.isActive,
          sortOrder:
            ((
              await db.site.findFirst({
                orderBy: { sortOrder: "desc" },
                select: { sortOrder: true },
              })
            )?.sortOrder ?? -1) + 1,
        },
      });

  // Ein neuer Standort braucht in allen Bereichen mit Eingang je Standort
  // sofort einen Eingang – sonst könnte dorthin nichts gemeldet werden.
  if (!input.id) {
    const perSite = await db.team.findMany({
      where: { queueMode: QueueMode.PER_SITE, isActive: true },
      select: { id: true },
    });
    for (const team of perSite) {
      await syncQueuesForTeam(team.id);
    }
  }

  await logAudit({
    actorId: actor.id,
    action: input.id ? "site.update" : "site.create",
    entityType: "Site",
    entityId: site.id,
    summary: `Standort "${site.name}" ${input.id ? "geändert" : "angelegt"}.`,
  });

  revalidatePath("/verwaltung/standorte");
  revalidatePath("/", "layout");
  return { ok: true as const, id: site.id };
}

// ===========================================================================
// Mailkonten
// ===========================================================================

const mailkontoSchema = z.object({
  queueId: z.string().min(1, "Bitte wählen Sie einen Eingang."),
  transport: z.nativeEnum(MailTransport),
  address: z.string().trim().email("Bitte eine gültige E-Mail-Adresse angeben."),
  isActive: z.boolean(),
  unknownSender: z.nativeEnum(UnknownSenderPolicy),

  imapHost: z.string().trim().max(200).optional().or(z.literal("")),
  imapPort: z.coerce.number().int().min(1).max(65535).optional(),
  imapSecure: z.boolean().optional(),
  imapUser: z.string().trim().max(200).optional().or(z.literal("")),
  /** Leer lassen heißt: bestehendes Kennwort behalten. */
  imapPassword: z.string().optional().or(z.literal("")),
  imapFolder: z.string().trim().max(100).optional().or(z.literal("")),
  imapProcessedFolder: z.string().trim().max(100).optional().or(z.literal("")),

  graphUserId: z.string().trim().max(200).optional().or(z.literal("")),
});

export async function mailkontoSpeichernAction(
  input: z.infer<typeof mailkontoSchema> & { id?: string },
) {
  const actor = await requireAdmin();
  const daten = mailkontoSchema.parse(input);

  if (daten.transport === MailTransport.IMAP) {
    if (!daten.imapHost || !daten.imapUser) {
      throw new Error("Für IMAP werden Server und Benutzername benötigt.");
    }
    if (!input.id && !daten.imapPassword) {
      throw new Error("Für ein neues IMAP-Konto wird ein Kennwort benötigt.");
    }
  } else if (!daten.graphUserId) {
    throw new Error(
      "Für Microsoft Graph wird die Postfachkennung benötigt (die " +
        "E-Mail-Adresse oder die Objekt-ID des Postfachs).",
    );
  }

  const gemeinsam = {
    transport: daten.transport,
    address: daten.address,
    isActive: daten.isActive,
    unknownSender: daten.unknownSender,
    imapHost: daten.imapHost || null,
    imapPort: daten.imapPort ?? 993,
    imapSecure: daten.imapSecure ?? true,
    imapUser: daten.imapUser || null,
    imapFolder: daten.imapFolder || "INBOX",
    imapProcessedFolder: daten.imapProcessedFolder || null,
    graphUserId: daten.graphUserId || null,
  };

  const konto = input.id
    ? await db.mailAccount.update({
        where: { id: input.id },
        data: {
          ...gemeinsam,
          // Leeres Feld heißt "unverändert" – sonst würde jedes Speichern der
          // übrigen Einstellungen das Kennwort löschen.
          ...(daten.imapPassword
            ? { imapPasswordEnc: encryptOptional(daten.imapPassword) }
            : {}),
        },
      })
    : await db.mailAccount.create({
        data: {
          ...gemeinsam,
          queueId: daten.queueId,
          imapPasswordEnc: encryptOptional(daten.imapPassword || null),
        },
      });

  await logAudit({
    actorId: actor.id,
    action: input.id ? "mail-account.update" : "mail-account.create",
    entityType: "MailAccount",
    entityId: konto.id,
    summary: `Postfach ${daten.address} (${daten.transport}) ${input.id ? "geändert" : "eingerichtet"}.`,
  });

  revalidatePath("/verwaltung/mailkonten");
  return { ok: true as const, id: konto.id };
}

/**
 * Prüft die Verbindung zu einem Postfach.
 *
 * Wichtig für die Inbetriebnahme: Ein falsches Kennwort fällt sonst erst
 * dadurch auf, dass tagelang keine Tickets ankommen.
 */
export async function verbindungTestenAction(mailAccountId: string) {
  await requireAdmin();

  const konto = await db.mailAccount.findUniqueOrThrow({
    where: { id: mailAccountId },
  });

  try {
    const { pruefeMailkonto } = await import("@/mail/inbound");
    await pruefeMailkonto(konto.id);

    await db.mailAccount.update({
      where: { id: konto.id },
      data: { lastSyncError: null },
    });

    return { ok: true as const, meldung: "Die Verbindung steht." };
  } catch (error) {
    const meldung =
      error instanceof Error ? error.message : "Die Verbindung schlug fehl.";

    await db.mailAccount.update({
      where: { id: konto.id },
      data: { lastSyncError: meldung },
    });

    return { ok: false as const, meldung };
  }
}

/** Ruft ein Postfach sofort ab, ohne auf den Hintergrunddienst zu warten. */
export async function jetztAbrufenAction(mailAccountId: string) {
  await requireAdmin();

  const { eingangAbrufen } = await import("@/mail/inbound");
  const ergebnis = await eingangAbrufen(mailAccountId);

  revalidatePath("/verwaltung/mailkonten");
  revalidatePath("/", "layout");
  return ergebnis;
}

// ===========================================================================
// Kategorien und Textbausteine
// ===========================================================================

const kategorieSchema = z.object({
  teamId: z.string().min(1),
  name: z.string().trim().min(2).max(80),
  description: z.string().trim().max(300).optional().or(z.literal("")),
  parentId: z.string().optional().or(z.literal("")),
  isActive: z.boolean(),
});

export async function kategorieSpeichernAction(
  input: z.infer<typeof kategorieSchema> & { id?: string },
) {
  const actor = await requireActor();
  const daten = kategorieSchema.parse(input);

  if (!canManageTeam(actor, daten.teamId)) {
    throw new Error("Kategorien dieses Bereichs dürfen Sie nicht ändern.");
  }

  const kategorie = input.id
    ? await db.category.update({
        where: { id: input.id },
        data: {
          name: daten.name,
          description: daten.description || null,
          parentId: daten.parentId || null,
          isActive: daten.isActive,
        },
      })
    : await db.category.create({
        data: {
          teamId: daten.teamId,
          name: daten.name,
          description: daten.description || null,
          parentId: daten.parentId || null,
          isActive: daten.isActive,
        },
      });

  await logAudit({
    actorId: actor.id,
    action: input.id ? "category.update" : "category.create",
    entityType: "Category",
    entityId: kategorie.id,
    summary: `Kategorie "${kategorie.name}" ${input.id ? "geändert" : "angelegt"}.`,
  });

  revalidatePath("/verwaltung/kategorien");
  return { ok: true as const };
}

const bausteinSchema = z.object({
  teamId: z.string().optional().or(z.literal("")),
  title: z.string().trim().min(2).max(80),
  shortcut: z.string().trim().max(30).optional().or(z.literal("")),
  body: z.string().trim().min(5, "Der Baustein braucht einen Text."),
  isActive: z.boolean(),
});

export async function bausteinSpeichernAction(
  input: z.infer<typeof bausteinSchema> & { id?: string },
) {
  const actor = await requireActor();
  const daten = bausteinSchema.parse(input);

  if (daten.teamId && !canManageTeam(actor, daten.teamId)) {
    throw new Error("Textbausteine dieses Bereichs dürfen Sie nicht ändern.");
  }
  if (!daten.teamId && !actor.isAdmin) {
    throw new Error(
      "Bereichsübergreifende Textbausteine kann nur die Administration pflegen.",
    );
  }

  const baustein = input.id
    ? await db.cannedResponse.update({
        where: { id: input.id },
        data: {
          teamId: daten.teamId || null,
          title: daten.title,
          shortcut: daten.shortcut || null,
          body: daten.body,
          isActive: daten.isActive,
        },
      })
    : await db.cannedResponse.create({
        data: {
          teamId: daten.teamId || null,
          title: daten.title,
          shortcut: daten.shortcut || null,
          body: daten.body,
          isActive: daten.isActive,
        },
      });

  await logAudit({
    actorId: actor.id,
    action: input.id ? "canned.update" : "canned.create",
    entityType: "CannedResponse",
    entityId: baustein.id,
    summary: `Textbaustein "${baustein.title}" ${input.id ? "geändert" : "angelegt"}.`,
  });

  revalidatePath("/verwaltung/textbausteine");
  return { ok: true as const };
}

// ===========================================================================
// Fristen und Dienstzeiten
// ===========================================================================

const slaSchema = z.object({
  teamId: z.string().min(1, "Bitte wählen Sie einen Bereich."),
  priority: z.enum(["NIEDRIG", "NORMAL", "HOCH", "DRINGEND"]),
  name: z.string().trim().min(2).max(80),
  firstResponseMinutes: z.coerce
    .number()
    .int()
    .min(5, "Die Frist für die Erstreaktion sollte mindestens 5 Minuten betragen.")
    .max(60 * 24 * 30),
  resolutionMinutes: z.coerce
    .number()
    .int()
    .min(15)
    .max(60 * 24 * 90),
  warningThreshold: z.coerce.number().min(0.1).max(1),
  businessHoursId: z.string().min(1, "Bitte wählen Sie eine Dienstzeit."),
  isActive: z.boolean(),
});

export async function slaSpeichernAction(
  input: z.infer<typeof slaSchema> & { id?: string },
) {
  const actor = await requireActor();
  const daten = slaSchema.parse(input);

  if (!canManageTeam(actor, daten.teamId)) {
    throw new Error("Fristen dieses Bereichs dürfen Sie nicht ändern.");
  }

  if (daten.resolutionMinutes < daten.firstResponseMinutes) {
    throw new Error(
      "Die Lösungsfrist kann nicht kürzer sein als die Frist für die Erstreaktion.",
    );
  }

  const policy = input.id
    ? await db.slaPolicy.update({
        where: { id: input.id },
        data: {
          name: daten.name,
          priority: daten.priority,
          firstResponseMinutes: daten.firstResponseMinutes,
          resolutionMinutes: daten.resolutionMinutes,
          warningThreshold: daten.warningThreshold,
          businessHoursId: daten.businessHoursId,
          isActive: daten.isActive,
        },
      })
    : await db.slaPolicy.create({
        data: {
          teamId: daten.teamId,
          name: daten.name,
          priority: daten.priority,
          firstResponseMinutes: daten.firstResponseMinutes,
          resolutionMinutes: daten.resolutionMinutes,
          warningThreshold: daten.warningThreshold,
          businessHoursId: daten.businessHoursId,
          isActive: daten.isActive,
        },
      });

  await logAudit({
    actorId: actor.id,
    action: input.id ? "sla.update" : "sla.create",
    entityType: "SlaPolicy",
    entityId: policy.id,
    summary: `Frist "${policy.name}" ${input.id ? "geändert" : "angelegt"}.`,
  });

  revalidatePath("/verwaltung/sla");
  return { ok: true as const };
}

const dienstzeitSchema = z.object({
  name: z.string().trim().min(2).max(80),
  timezone: z.string().trim().min(3).max(60),
  /** Wochenplan: { "1": [["07:30","16:30"]], … } */
  schedule: z.record(
    z.string(),
    z.array(z.tuple([z.string(), z.string()])),
  ),
  isDefault: z.boolean(),
});

export async function dienstzeitSpeichernAction(
  input: z.infer<typeof dienstzeitSchema> & { id?: string },
) {
  const actor = await requireAdmin();
  const daten = dienstzeitSchema.parse(input);

  // Zeiten prüfen: "HH:mm" und Ende nach Beginn.
  const zeitMuster = /^([01]\d|2[0-3]):([0-5]\d)$/;
  for (const [tag, fenster] of Object.entries(daten.schedule)) {
    for (const [von, bis] of fenster) {
      if (!zeitMuster.test(von) || !zeitMuster.test(bis)) {
        throw new Error(
          `Ungültige Zeitangabe an Tag ${tag}: Bitte im Format HH:MM angeben.`,
        );
      }
      if (bis <= von) {
        throw new Error(
          `An Tag ${tag} liegt das Ende (${bis}) nicht nach dem Beginn (${von}).`,
        );
      }
    }
  }

  // Es soll immer genau eine Vorgabe geben.
  if (daten.isDefault) {
    await db.businessHours.updateMany({
      where: input.id ? { id: { not: input.id } } : {},
      data: { isDefault: false },
    });
  }

  const bh = input.id
    ? await db.businessHours.update({
        where: { id: input.id },
        data: {
          name: daten.name,
          timezone: daten.timezone,
          schedule: daten.schedule,
          isDefault: daten.isDefault,
        },
      })
    : await db.businessHours.create({
        data: {
          name: daten.name,
          timezone: daten.timezone,
          schedule: daten.schedule,
          isDefault: daten.isDefault,
        },
      });

  await logAudit({
    actorId: actor.id,
    action: input.id ? "business-hours.update" : "business-hours.create",
    entityType: "BusinessHours",
    entityId: bh.id,
    summary: `Dienstzeit "${bh.name}" ${input.id ? "geändert" : "angelegt"}.`,
  });

  revalidatePath("/verwaltung/dienstzeiten");
  revalidatePath("/verwaltung/sla");
  return { ok: true as const };
}

export async function feiertagAction(
  businessHoursId: string,
  datum: string,
  name: string,
  entfernen = false,
) {
  const actor = await requireAdmin();

  // Feiertage werden als reines Datum in UTC abgelegt – so verschiebt keine
  // Zeitzone den 1. Mai auf den 30. April.
  const date = new Date(`${datum}T00:00:00Z`);
  if (Number.isNaN(date.getTime())) {
    throw new Error("Bitte ein gültiges Datum angeben.");
  }

  if (entfernen) {
    await db.holiday.deleteMany({ where: { businessHoursId, date } });
  } else {
    await db.holiday.upsert({
      where: { businessHoursId_date: { businessHoursId, date } },
      create: { businessHoursId, date, name: name.trim() || "Feiertag" },
      update: { name: name.trim() || "Feiertag" },
    });
  }

  await logAudit({
    actorId: actor.id,
    action: entfernen ? "holiday.remove" : "holiday.set",
    entityType: "BusinessHours",
    entityId: businessHoursId,
    summary: `Feiertag ${datum} ${entfernen ? "entfernt" : "gesetzt"}.`,
  });

  revalidatePath("/verwaltung/dienstzeiten");
  return { ok: true as const };
}

// ===========================================================================
// Wissensartikel
// ===========================================================================

const artikelSchema = z.object({
  title: z.string().trim().min(3).max(160),
  summary: z.string().trim().max(300).optional().or(z.literal("")),
  body: z.string().trim().min(20, "Der Artikel braucht etwas mehr Inhalt."),
  teamId: z.string().optional().or(z.literal("")),
  visibility: z.enum(["OEFFENTLICH", "INTERN"]),
  published: z.boolean(),
});

export async function artikelSpeichernAction(
  input: z.infer<typeof artikelSchema> & { id?: string },
) {
  const actor = await requireActor();
  const daten = artikelSchema.parse(input);

  if (daten.teamId && !canManageTeam(actor, daten.teamId) && !actor.isAdmin) {
    throw new Error("Artikel dieses Bereichs dürfen Sie nicht ändern.");
  }

  const slug = input.id
    ? undefined
    : await freierSlug(slugify(daten.title) || "artikel");

  const artikel = input.id
    ? await db.kbArticle.update({
        where: { id: input.id },
        data: {
          title: daten.title,
          summary: daten.summary || null,
          body: daten.body,
          teamId: daten.teamId || null,
          visibility: daten.visibility,
          published: daten.published,
        },
      })
    : await db.kbArticle.create({
        data: {
          slug: slug!,
          title: daten.title,
          summary: daten.summary || null,
          body: daten.body,
          teamId: daten.teamId || null,
          visibility: daten.visibility,
          published: daten.published,
          authorId: actor.id,
        },
      });

  await logAudit({
    actorId: actor.id,
    action: input.id ? "kb.update" : "kb.create",
    entityType: "KbArticle",
    entityId: artikel.id,
    summary: `Wissensartikel "${artikel.title}" ${input.id ? "geändert" : "angelegt"}.`,
  });

  revalidatePath("/wissen");
  revalidatePath(`/wissen/${artikel.slug}`);
  return { ok: true as const, slug: artikel.slug };
}

/** Hängt eine Zahl an, bis die Kennung frei ist. */
async function freierSlug(basis: string): Promise<string> {
  let kandidat = basis;
  let n = 1;

  while (
    await db.kbArticle.findUnique({
      where: { slug: kandidat },
      select: { id: true },
    })
  ) {
    n++;
    kandidat = `${basis}-${n}`;
  }

  return kandidat;
}
