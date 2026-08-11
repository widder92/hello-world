"use server";

import { revalidatePath } from "next/cache";
import { AssetEventType, AssetStatus, MacKind } from "@prisma/client";
import { z } from "zod";

import { db } from "@/server/db";
import {
  canCreateInventoryLookups,
  inventoryAccess,
  inventoryTeamIds,
  requireActor,
  requireInventory,
  type Actor,
} from "@/server/policy";
import { logAudit } from "@/server/services/events";
import { speichereAnhang, entferneAnhang } from "@/server/services/attachments";
import {
  InventarFehler,
  betragZuCent,
  chronikVermerk,
  macNormalisieren,
  naechsteInventarnummer,
  statusWechseln,
} from "@/server/services/inventar";

/**
 * Server-Aktionen des Inventars.
 *
 * Jede prüft zuerst das Recht über `requireInventory` und schreibt danach in
 * das Protokoll. Das Ausblenden von Knöpfen in der Oberfläche ist Bequemlich-
 * keit; der Schutz sitzt hier.
 *
 * Statusänderungen laufen ausnahmslos über `statusWechseln` aus der
 * Dienstschicht – diese Datei ändert `Asset.status` nie selbst.
 */

// ---------------------------------------------------------------------------
// Hilfen
// ---------------------------------------------------------------------------

/**
 * Bereiche, in denen diese Person Geräte führen darf.
 *
 * Für die Verwaltung sind es alle Bereiche mit Inventar, sonst die eigenen.
 * Ohne diese Einschränkung könnte ein Bereich Geräte in einen fremden
 * einbuchen und sie dort verschwinden lassen.
 */
async function erlaubteBereiche(actor: Actor): Promise<string[]> {
  if (actor.isAdmin) {
    const alle = await db.team.findMany({
      where: { hasInventory: true, isActive: true },
      select: { id: true },
    });
    return alle.map((t) => t.id);
  }
  return inventoryTeamIds(actor);
}

/** Lädt ein Gerät und stellt sicher, dass es im Zugriff liegt. */
async function geraetImZugriff(actor: Actor, assetId: string) {
  const erlaubt = await erlaubteBereiche(actor);
  const geraet = await db.asset.findFirst({
    where: { id: assetId, teamId: { in: erlaubt } },
    select: {
      id: true,
      assetTag: true,
      name: true,
      status: true,
      teamId: true,
      photoId: true,
    },
  });
  if (!geraet) {
    throw new InventarFehler("Dieses Gerät liegt nicht in Ihrem Inventar.");
  }
  return geraet;
}

function istLeitung(actor: Actor): boolean {
  return inventoryAccess(actor) === "manage";
}

// ---------------------------------------------------------------------------
// Auswahllisten für die Formulare
// ---------------------------------------------------------------------------

export async function inventarStammdaten() {
  const actor = await requireActor();
  requireInventory(actor);

  const [arten, hersteller, kostenstellen, standorte, bereiche] =
    await Promise.all([
      db.assetKind.findMany({
        where: { isActive: true },
        orderBy: [{ sortOrder: "asc" }, { name: "asc" }],
        select: {
          id: true,
          key: true,
          name: true,
          icon: true,
          requiresSerial: true,
          requiresMac: true,
          defaultCostCenterId: true,
        },
      }),
      db.manufacturer.findMany({
        where: { isActive: true },
        orderBy: { name: "asc" },
        select: {
          id: true,
          name: true,
          models: {
            where: { isActive: true },
            orderBy: { name: "asc" },
            select: {
              id: true,
              name: true,
              kindId: true,
              photo: { select: { id: true } },
            },
          },
        },
      }),
      db.costCenter.findMany({
        where: { isActive: true },
        orderBy: [{ sortOrder: "asc" }, { code: "asc" }],
        select: { id: true, code: true, name: true },
      }),
      db.site.findMany({
        where: { isActive: true },
        orderBy: [{ sortOrder: "asc" }, { name: "asc" }],
        select: { id: true, name: true },
      }),
      db.team.findMany({
        where: { id: { in: await erlaubteBereiche(actor) } },
        orderBy: [{ sortOrder: "asc" }, { name: "asc" }],
        select: { id: true, name: true },
      }),
    ]);

  return { arten, hersteller, kostenstellen, standorte, bereiche };
}

/** Personensuche für Herausgabe und Zuweisung. */
export async function suchePerson(begriff: string) {
  const actor = await requireActor();
  requireInventory(actor);

  const suche = begriff.trim();
  if (suche.length < 2) return [];

  return db.user.findMany({
    where: {
      isActive: true,
      isExternal: false,
      OR: [
        { name: { contains: suche, mode: "insensitive" } },
        { email: { contains: suche, mode: "insensitive" } },
      ],
    },
    orderBy: { name: "asc" },
    take: 8,
    select: {
      id: true,
      name: true,
      email: true,
      jobTitle: true,
      site: { select: { name: true } },
    },
  });
}

// ---------------------------------------------------------------------------
// Gerät anlegen und ändern
// ---------------------------------------------------------------------------

const leer = (max: number) => z.string().trim().max(max).optional().or(z.literal(""));

const geraetSchema = z.object({
  name: z
    .string()
    .trim()
    .min(2, "Bitte geben Sie eine Bezeichnung an.")
    .max(120),
  kindId: z.string().min(1, "Bitte wählen Sie die Geräteart."),
  teamId: z.string().min(1, "Bitte wählen Sie den Bereich."),
  manufacturerId: leer(40),
  modelId: leer(40),
  serial: leer(120),
  imei: leer(40),
  siteId: leer(40),
  room: leer(120),
  storageLocation: leer(120),
  costCenterId: leer(40),
  purchasedAt: leer(20),
  warrantyUntil: leer(20),
  /** Als Text, damit „1.299,00" so eingetippt werden kann. */
  preis: leer(20),
  supplier: leer(120),
  invoiceNumber: leer(60),
  notes: z.string().trim().max(4000).optional().or(z.literal("")),
  /** MAC-Adressen, je Zeile eine – so kommen sie aus dem Formular. */
  macs: z
    .array(z.object({ address: z.string(), kind: z.nativeEnum(MacKind) }))
    .max(8)
    .optional(),

  /** Vorab hochgeladenes Bild (siehe `fotoVorbereitenAction`). */
  fotoId: leer(40),
  /**
   * Wohin das Bild gehört.
   *
   * Der Regelfall ist `modell`: Alle Geräte desselben Modells sehen gleich
   * aus, und das Bild soll für die nächsten zwanzig mitgelten. `geraet` ist
   * die Ausnahme für ein Gerät, das anders aussieht als der Rest.
   */
  fotoZiel: z.enum(["modell", "geraet"]).optional(),
});

export type GeraetInput = z.input<typeof geraetSchema> & { id?: string };

function datumOderNull(wert: string | undefined): Date | null {
  if (!wert) return null;
  const d = new Date(wert);
  return Number.isNaN(d.getTime()) ? null : d;
}

export async function geraetSpeichernAction(input: GeraetInput) {
  const actor = await requireActor();
  requireInventory(actor);

  const daten = geraetSchema.parse(input);
  const erlaubt = await erlaubteBereiche(actor);

  if (!erlaubt.includes(daten.teamId)) {
    throw new InventarFehler(
      "In diesen Bereich können Sie keine Geräte einbuchen.",
    );
  }

  const art = await db.assetKind.findFirst({
    where: { id: daten.kindId, isActive: true },
    select: { id: true, key: true, name: true, requiresSerial: true },
  });
  if (!art) {
    throw new InventarFehler("Diese Geräteart gibt es nicht mehr.");
  }

  if (art.requiresSerial && !daten.serial) {
    throw new InventarFehler(
      `Für die Geräteart ${art.name} ist die Seriennummer verlangt. ` +
        `Sie steht meist auf einem Aufkleber an der Unterseite.`,
    );
  }

  // Modell und Hersteller müssen zusammenpassen. Die Oberfläche stellt das
  // sicher, indem sie nur die Modelle des gewählten Herstellers anbietet –
  // aber die Kennungen kommen aus dem Browser und sind damit frei wählbar.
  if (daten.modelId) {
    const modell = await db.assetModel.findFirst({
      where: { id: daten.modelId, isActive: true },
      select: { id: true, name: true, manufacturerId: true },
    });
    if (!modell) {
      throw new InventarFehler("Dieses Modell gibt es nicht mehr.");
    }
    if (modell.manufacturerId !== daten.manufacturerId) {
      throw new InventarFehler(
        `Das Modell ${modell.name} gehört zu einem anderen Hersteller.`,
      );
    }
  }

  const preis = daten.preis ? betragZuCent(daten.preis) : null;

  // MAC-Adressen vereinheitlichen, bevor irgendetwas geschrieben wird – ein
  // Tippfehler in der letzten Adresse soll nicht ein halb gespeichertes Gerät
  // hinterlassen.
  const macs = (daten.macs ?? [])
    .filter((m) => m.address.trim())
    .map((m) => ({ address: macNormalisieren(m.address), kind: m.kind }));

  const doppelt = macs.find(
    (m, i) => macs.findIndex((x) => x.address === m.address) !== i,
  );
  if (doppelt) {
    throw new InventarFehler(
      `Die MAC-Adresse ${doppelt.address} steht doppelt in der Liste.`,
    );
  }

  const gemeinsam = {
    name: daten.name,
    kindId: daten.kindId,
    teamId: daten.teamId,
    manufacturerId: daten.manufacturerId || null,
    modelId: daten.modelId || null,
    serial: daten.serial || null,
    imei: daten.imei || null,
    siteId: daten.siteId || null,
    room: daten.room || null,
    storageLocation: daten.storageLocation || null,
    costCenterId: daten.costCenterId || null,
    purchasedAt: datumOderNull(daten.purchasedAt),
    warrantyUntil: datumOderNull(daten.warrantyUntil),
    purchasePriceCents: preis,
    supplier: daten.supplier || null,
    invoiceNumber: daten.invoiceNumber || null,
    notes: daten.notes || null,
  };

  const geraet = await db.$transaction(async (tx) => {
    let datensatz;

    if (input.id) {
      // Nur Geräte aus dem eigenen Zugriff, sonst ließe sich über eine
      // untergeschobene Kennung ein fremdes Gerät ändern.
      const vorhanden = await tx.asset.findFirst({
        where: { id: input.id, teamId: { in: erlaubt } },
        select: { id: true },
      });
      if (!vorhanden) {
        throw new InventarFehler("Dieses Gerät liegt nicht in Ihrem Inventar.");
      }
      datensatz = await tx.asset.update({
        where: { id: input.id },
        data: gemeinsam,
        select: { id: true, assetTag: true, name: true },
      });
    } else {
      const assetTag = await naechsteInventarnummer(tx, art.id, art.key);
      datensatz = await tx.asset.create({
        data: { assetTag, status: AssetStatus.LAGER, ...gemeinsam },
        select: { id: true, assetTag: true, name: true },
      });
      await tx.assetEvent.create({
        data: {
          assetId: datensatz.id,
          type: AssetEventType.ANGELEGT,
          toStatus: AssetStatus.LAGER,
          actorId: actor.id,
          actorName: actor.name,
        },
      });
    }

    // MAC-Adressen vollständig ersetzen: Das Formular schickt immer die
    // gesamte Liste, und ein Abgleich Zeile für Zeile brächte nichts als
    // Gelegenheiten für Abweichungen.
    await tx.assetMac.deleteMany({ where: { assetId: datensatz.id } });
    for (const mac of macs) {
      const belegt = await tx.assetMac.findUnique({
        where: { address: mac.address },
        select: { asset: { select: { assetTag: true } } },
      });
      if (belegt) {
        throw new InventarFehler(
          `Die MAC-Adresse ${mac.address} gehört bereits zu Gerät ` +
            `${belegt.asset.assetTag}. Meist ist das Gerät dann schon erfasst.`,
        );
      }
      await tx.assetMac.create({
        data: { assetId: datensatz.id, address: mac.address, kind: mac.kind },
      });
    }

    // Ein vorab hochgeladenes Bild jetzt verknüpfen – entweder am Modell,
    // damit alle weiteren Geräte davon zehren, oder an diesem einen Gerät.
    if (daten.fotoId) {
      const amModell = daten.fotoZiel !== "geraet" && daten.modelId;
      if (amModell) {
        const modell = await tx.assetModel.findUnique({
          where: { id: daten.modelId! },
          select: { photoId: true },
        });
        // Ein vorhandenes Modellbild wird nicht beiläufig überschrieben: Es
        // gilt für alle Geräte dieses Modells, und das gehört auf die
        // Modellseite und nicht in ein Geräteformular.
        if (modell && !modell.photoId) {
          await tx.assetModel.update({
            where: { id: daten.modelId! },
            data: { photoId: daten.fotoId },
          });
        } else {
          await tx.asset.update({
            where: { id: datensatz.id },
            data: { photoId: daten.fotoId },
          });
        }
      } else {
        await tx.asset.update({
          where: { id: datensatz.id },
          data: { photoId: daten.fotoId },
        });
      }
    }

    if (input.id) {
      await tx.assetEvent.create({
        data: {
          assetId: datensatz.id,
          type: AssetEventType.GEAENDERT,
          actorId: actor.id,
          actorName: actor.name,
        },
      });
    }

    return datensatz;
  });

  await logAudit({
    actorId: actor.id,
    action: input.id ? "asset.update" : "asset.create",
    entityType: "Asset",
    entityId: geraet.id,
    summary: `Gerät ${geraet.assetTag} „${geraet.name}" ${
      input.id ? "geändert" : "angelegt"
    }.`,
  });

  revalidatePath("/inventar");
  revalidatePath(`/inventar/${geraet.id}`);
  return { ok: true as const, id: geraet.id, assetTag: geraet.assetTag };
}

// ---------------------------------------------------------------------------
// Statuswechsel
// ---------------------------------------------------------------------------

const statusSchema = z.object({
  assetId: z.string().min(1),
  ziel: z.nativeEnum(AssetStatus),
  userId: z.string().optional().or(z.literal("")),
  note: z.string().trim().max(2000).optional().or(z.literal("")),
  condition: z.string().trim().max(200).optional().or(z.literal("")),
  accessories: z.string().trim().max(500).optional().or(z.literal("")),
});

export type StatusInput = z.input<typeof statusSchema>;

export async function statusWechselnAction(input: StatusInput) {
  const actor = await requireActor();
  requireInventory(actor);

  const daten = statusSchema.parse(input);
  const geraet = await geraetImZugriff(actor, daten.assetId);

  const ergebnis = await statusWechseln(
    geraet.id,
    daten.ziel,
    {
      userId: daten.userId || null,
      note: daten.note || null,
      condition: daten.condition || null,
      accessories: daten.accessories || null,
      istLeitung: istLeitung(actor),
    },
    { id: actor.id, name: actor.name },
  );

  await logAudit({
    actorId: actor.id,
    action: "asset.status",
    entityType: "Asset",
    entityId: geraet.id,
    summary:
      `Gerät ${geraet.assetTag}: ${geraet.status} → ${daten.ziel}` +
      (ergebnis.protokoll ? ` (${ergebnis.protokoll.protocolNo})` : ""),
  });

  revalidatePath("/inventar");
  revalidatePath(`/inventar/${geraet.id}`);
  revalidatePath("/meine-geraete");

  return {
    ok: true as const,
    protokollId: ergebnis.protokoll?.id ?? null,
    protokollNr: ergebnis.protokoll?.protocolNo ?? null,
  };
}

/** Vermerk in der Chronik, ohne den Zustand zu ändern. */
export async function notizAction(input: { assetId: string; note: string }) {
  const actor = await requireActor();
  requireInventory(actor);

  const text = input.note.trim();
  if (text.length < 2) {
    throw new InventarFehler("Bitte schreiben Sie einen Vermerk.");
  }

  const geraet = await geraetImZugriff(actor, input.assetId);
  await chronikVermerk(geraet.id, AssetEventType.NOTIZ, actor, text);

  revalidatePath(`/inventar/${geraet.id}`);
  return { ok: true as const };
}

// ---------------------------------------------------------------------------
// Foto
// ---------------------------------------------------------------------------

/**
 * Foto eines Geräts setzen oder ersetzen.
 *
 * Nimmt nur Bilder an. Ein PDF als „Foto" ließe sich zwar speichern, aber die
 * Liste zeigt Miniaturbilder – dort entstünde eine leere Fläche.
 */
export async function fotoSetzenAction(formular: FormData) {
  const actor = await requireActor();
  requireInventory(actor);

  const assetId = String(formular.get("assetId") ?? "");
  const datei = formular.get("datei");

  if (!(datei instanceof File) || datei.size === 0) {
    throw new InventarFehler("Bitte wählen Sie ein Bild aus.");
  }
  if (!datei.type.startsWith("image/")) {
    throw new InventarFehler(
      "Als Gerätefoto werden nur Bilder angenommen (JPG, PNG, HEIC).",
    );
  }

  const geraet = await geraetImZugriff(actor, assetId);
  const inhalt = Buffer.from(await datei.arrayBuffer());

  const anhang = await speichereAnhang({
    inhalt,
    filename: datei.name || `${geraet.assetTag}.jpg`,
    mimeType: datei.type,
    uploadedById: actor.id,
  });

  const altesFoto = geraet.photoId;

  await db.asset.update({
    where: { id: geraet.id },
    data: { photoId: anhang.id },
  });

  // Das alte Bild erst danach entfernen: Bricht das Speichern ab, hat das
  // Gerät lieber zwei Bilder als gar keines.
  if (altesFoto) await entferneAnhang(altesFoto);

  await chronikVermerk(
    geraet.id,
    AssetEventType.GEAENDERT,
    actor,
    altesFoto ? "Foto ersetzt." : "Foto hinterlegt.",
  );

  revalidatePath(`/inventar/${geraet.id}`);
  revalidatePath("/inventar");
  return { ok: true as const, fotoId: anhang.id };
}

export async function fotoEntfernenAction(assetId: string) {
  const actor = await requireActor();
  requireInventory(actor);

  const geraet = await geraetImZugriff(actor, assetId);
  if (!geraet.photoId) return { ok: true as const };

  await db.asset.update({
    where: { id: geraet.id },
    data: { photoId: null },
  });
  await entferneAnhang(geraet.photoId);
  await chronikVermerk(geraet.id, AssetEventType.GEAENDERT, actor, "Foto entfernt.");

  revalidatePath(`/inventar/${geraet.id}`);
  revalidatePath("/inventar");
  return { ok: true as const };
}

// ---------------------------------------------------------------------------
// Stammdaten: Gerätearten, Hersteller, Kostenstellen
// ---------------------------------------------------------------------------
//
// Diese drei Listen wirken auf alle Geräte gleichzeitig. Eine gelöschte
// Kostenstelle verfälscht jede vergangene Auswertung, eine umbenannte
// Geräteart verändert die Bedeutung alter Datensätze. Deshalb überall
// `requireInventory(actor, "manage")` – Leitung oder Administration – und
// deshalb wird nichts wirklich gelöscht, sondern stillgelegt.

const artSchema = z.object({
  key: z
    .string()
    .trim()
    .min(1, "Bitte ein Kürzel angeben.")
    .max(5, "Das Kürzel darf höchstens 5 Zeichen haben.")
    .regex(/^[A-Za-z0-9]+$/, "Im Kürzel sind nur Buchstaben und Ziffern erlaubt."),
  name: z.string().trim().min(2, "Bitte eine Bezeichnung angeben.").max(60),
  icon: z.string().trim().max(40).optional().or(z.literal("")),
  requiresSerial: z.boolean(),
  requiresMac: z.boolean(),
  defaultCostCenterId: z.string().optional().or(z.literal("")),
  isActive: z.boolean(),
});

export async function geraeteartSpeichernAction(
  input: z.infer<typeof artSchema> & { id?: string },
) {
  const actor = await requireActor();
  requireInventory(actor, "manage");
  const daten = artSchema.parse(input);

  const key = daten.key.toUpperCase();

  // Das Kürzel steckt in jeder bereits vergebenen Inventarnummer. Wird es
  // nachträglich geändert, passen Aufkleber und Datensatz nicht mehr
  // zusammen – deshalb bleibt es nach dem Anlegen fest.
  if (input.id) {
    const bisher = await db.assetKind.findUnique({
      where: { id: input.id },
      select: { key: true },
    });
    if (bisher && bisher.key !== key) {
      throw new InventarFehler(
        `Das Kürzel lässt sich nicht mehr ändern: Es steht bereits in den ` +
          `Inventarnummern der Geräte dieser Art.`,
      );
    }
  }

  const werte = {
    name: daten.name,
    icon: daten.icon || "Package",
    requiresSerial: daten.requiresSerial,
    requiresMac: daten.requiresMac,
    defaultCostCenterId: daten.defaultCostCenterId || null,
    isActive: daten.isActive,
  };

  const art = input.id
    ? await db.assetKind.update({ where: { id: input.id }, data: werte })
    : await db.assetKind.create({
        data: {
          key,
          ...werte,
          sortOrder:
            ((
              await db.assetKind.findFirst({
                orderBy: { sortOrder: "desc" },
                select: { sortOrder: true },
              })
            )?.sortOrder ?? 0) + 10,
        },
      });

  await logAudit({
    actorId: actor.id,
    action: input.id ? "assetkind.update" : "assetkind.create",
    entityType: "AssetKind",
    entityId: art.id,
    summary: `Geräteart "${art.name}" ${input.id ? "geändert" : "angelegt"}.`,
  });

  revalidatePath("/verwaltung/geraetearten");
  revalidatePath("/inventar");
  return { ok: true as const, id: art.id };
}

const herstellerSchema = z.object({
  name: z.string().trim().min(2, "Bitte einen Namen angeben.").max(60),
  website: z.string().trim().max(200).optional().or(z.literal("")),
  supportPhone: z.string().trim().max(40).optional().or(z.literal("")),
  notes: z.string().trim().max(1000).optional().or(z.literal("")),
  isActive: z.boolean(),
});

export async function herstellerSpeichernAction(
  input: z.infer<typeof herstellerSchema> & { id?: string },
) {
  const actor = await requireActor();
  requireInventory(actor, "manage");
  const daten = herstellerSchema.parse(input);

  const werte = {
    name: daten.name,
    website: daten.website || null,
    supportPhone: daten.supportPhone || null,
    notes: daten.notes || null,
    isActive: daten.isActive,
  };

  const hersteller = input.id
    ? await db.manufacturer.update({ where: { id: input.id }, data: werte })
    : await db.manufacturer.create({ data: werte });

  await logAudit({
    actorId: actor.id,
    action: input.id ? "manufacturer.update" : "manufacturer.create",
    entityType: "Manufacturer",
    entityId: hersteller.id,
    summary: `Hersteller "${hersteller.name}" ${input.id ? "geändert" : "angelegt"}.`,
  });

  revalidatePath("/verwaltung/hersteller");
  revalidatePath("/inventar");
  return { ok: true as const, id: hersteller.id };
}

const kostenstelleSchema = z.object({
  code: z
    .string()
    .trim()
    .min(1, "Bitte die Nummer der Kostenstelle angeben.")
    .max(20),
  name: z.string().trim().min(2, "Bitte eine Bezeichnung angeben.").max(80),
  description: z.string().trim().max(500).optional().or(z.literal("")),
  isActive: z.boolean(),
});

export async function kostenstelleSpeichernAction(
  input: z.infer<typeof kostenstelleSchema> & { id?: string },
) {
  const actor = await requireActor();
  requireInventory(actor, "manage");
  const daten = kostenstelleSchema.parse(input);

  const werte = {
    code: daten.code,
    name: daten.name,
    description: daten.description || null,
    isActive: daten.isActive,
  };

  const kostenstelle = input.id
    ? await db.costCenter.update({ where: { id: input.id }, data: werte })
    : await db.costCenter.create({
        data: {
          ...werte,
          sortOrder:
            ((
              await db.costCenter.findFirst({
                orderBy: { sortOrder: "desc" },
                select: { sortOrder: true },
              })
            )?.sortOrder ?? 0) + 10,
        },
      });

  await logAudit({
    actorId: actor.id,
    action: input.id ? "costcenter.update" : "costcenter.create",
    entityType: "CostCenter",
    entityId: kostenstelle.id,
    summary: `Kostenstelle ${kostenstelle.code} "${kostenstelle.name}" ${
      input.id ? "geändert" : "angelegt"
    }.`,
  });

  revalidatePath("/verwaltung/kostenstellen");
  revalidatePath("/inventar/auswertung");
  return { ok: true as const, id: kostenstelle.id };
}

// ---------------------------------------------------------------------------
// Hersteller und Modelle beim Erfassen anlegen
// ---------------------------------------------------------------------------
//
// Beides entsteht dort, wo es gebraucht wird: mitten im Geräteformular. Wer
// ein neu geliefertes Gerät einbuchen will, dessen Hersteller noch niemand
// eingetragen hat, soll nicht abbrechen und auf die Bereichsleitung warten
// müssen.

function pruefeAnlegen(actor: Actor) {
  if (!canCreateInventoryLookups(actor)) {
    throw new InventarFehler(
      "Hersteller und Modelle darf nur anlegen, wer Geräte erfasst.",
    );
  }
}

export async function herstellerAnlegenAction(name: string) {
  const actor = await requireActor();
  pruefeAnlegen(actor);

  const bezeichnung = name.trim();
  if (bezeichnung.length < 2) {
    throw new InventarFehler("Bitte geben Sie den Namen des Herstellers an.");
  }

  // Groß- und Kleinschreibung ignorieren: „lenovo" soll den vorhandenen
  // Eintrag „Lenovo" finden und nicht danebenstellen.
  const vorhanden = await db.manufacturer.findFirst({
    where: { name: { equals: bezeichnung, mode: "insensitive" } },
    select: { id: true, name: true, isActive: true },
  });

  if (vorhanden) {
    // Ein stillgelegter Hersteller wird wiederbelebt statt verdoppelt.
    if (!vorhanden.isActive) {
      await db.manufacturer.update({
        where: { id: vorhanden.id },
        data: { isActive: true },
      });
    }
    revalidatePath("/inventar");
    return { ok: true as const, id: vorhanden.id, name: vorhanden.name };
  }

  const hersteller = await db.manufacturer.create({
    data: { name: bezeichnung },
    select: { id: true, name: true },
  });

  await logAudit({
    actorId: actor.id,
    action: "manufacturer.create",
    entityType: "Manufacturer",
    entityId: hersteller.id,
    summary: `Hersteller "${hersteller.name}" beim Erfassen eines Geräts angelegt.`,
  });

  revalidatePath("/inventar");
  return { ok: true as const, id: hersteller.id, name: hersteller.name };
}

export async function modellAnlegenAction(input: {
  manufacturerId: string;
  name: string;
  kindId?: string;
  /** Zuvor hochgeladenes Bild, das für alle Geräte dieses Modells gilt. */
  fotoId?: string;
}) {
  const actor = await requireActor();
  pruefeAnlegen(actor);

  const bezeichnung = input.name.trim();
  if (bezeichnung.length < 1) {
    throw new InventarFehler("Bitte geben Sie die Modellbezeichnung an.");
  }

  const hersteller = await db.manufacturer.findFirst({
    where: { id: input.manufacturerId, isActive: true },
    select: { id: true, name: true },
  });
  if (!hersteller) {
    throw new InventarFehler(
      "Bitte wählen Sie zuerst den Hersteller – ein Modell gehört immer zu einem.",
    );
  }

  const vorhanden = await db.assetModel.findFirst({
    where: {
      manufacturerId: hersteller.id,
      name: { equals: bezeichnung, mode: "insensitive" },
    },
    select: { id: true, name: true, isActive: true, photoId: true },
  });

  if (vorhanden) {
    await db.assetModel.update({
      where: { id: vorhanden.id },
      data: {
        isActive: true,
        // Ein mitgeschicktes Bild nur übernehmen, wenn noch keines da ist –
        // sonst überschriebe ein Nebenbei-Klick das Bild aller Geräte dieses
        // Modells.
        ...(input.fotoId && !vorhanden.photoId ? { photoId: input.fotoId } : {}),
      },
    });
    revalidatePath("/inventar");
    return { ok: true as const, id: vorhanden.id, name: vorhanden.name };
  }

  const modell = await db.assetModel.create({
    data: {
      manufacturerId: hersteller.id,
      name: bezeichnung,
      kindId: input.kindId || null,
      photoId: input.fotoId || null,
    },
    select: { id: true, name: true },
  });

  await logAudit({
    actorId: actor.id,
    action: "assetmodel.create",
    entityType: "AssetModel",
    entityId: modell.id,
    summary: `Modell "${hersteller.name} ${modell.name}" angelegt.`,
  });

  revalidatePath("/inventar");
  return { ok: true as const, id: modell.id, name: modell.name };
}

/** Foto eines Modells nachträglich setzen oder austauschen. */
export async function modellFotoSetzenAction(formular: FormData) {
  const actor = await requireActor();
  pruefeAnlegen(actor);

  const modelId = String(formular.get("modelId") ?? "");
  const datei = formular.get("datei");

  if (!(datei instanceof File) || datei.size === 0) {
    throw new InventarFehler("Bitte wählen Sie ein Bild aus.");
  }
  if (!datei.type.startsWith("image/")) {
    throw new InventarFehler("Als Bild werden nur Bilddateien angenommen.");
  }

  const modell = await db.assetModel.findUnique({
    where: { id: modelId },
    select: { id: true, name: true, photoId: true },
  });
  if (!modell) throw new InventarFehler("Dieses Modell wurde nicht gefunden.");

  const anhang = await speichereAnhang({
    inhalt: Buffer.from(await datei.arrayBuffer()),
    filename: datei.name || `${modell.name}.jpg`,
    mimeType: datei.type,
    uploadedById: actor.id,
  });

  const altes = modell.photoId;
  await db.assetModel.update({
    where: { id: modell.id },
    data: { photoId: anhang.id },
  });
  if (altes) await entferneAnhang(altes);

  revalidatePath("/inventar");
  return { ok: true as const, fotoId: anhang.id };
}

// ---------------------------------------------------------------------------
// Foto schon beim Anlegen
// ---------------------------------------------------------------------------

/**
 * Nimmt ein Bild entgegen, bevor es ein Gerät oder Modell gibt.
 *
 * Beim Anlegen existiert noch kein Datensatz, an den das Bild gehängt werden
 * könnte. Es wird deshalb zunächst ohne Zuordnung abgelegt und erst beim
 * Speichern verknüpft – dasselbe Vorgehen wie bei den Anhängen im
 * Meldeformular. Bricht jemand ab, räumt `entferneVerwaisteAnhaenge` die
 * Datei nach einem Tag weg.
 */
export async function fotoVorbereitenAction(formular: FormData) {
  const actor = await requireActor();
  requireInventory(actor);

  const datei = formular.get("datei");
  if (!(datei instanceof File) || datei.size === 0) {
    throw new InventarFehler("Bitte wählen Sie ein Bild aus.");
  }
  if (!datei.type.startsWith("image/")) {
    throw new InventarFehler(
      "Als Foto werden nur Bilder angenommen (JPG, PNG, HEIC).",
    );
  }

  const anhang = await speichereAnhang({
    inhalt: Buffer.from(await datei.arrayBuffer()),
    filename: datei.name || "geraet.jpg",
    mimeType: datei.type,
    uploadedById: actor.id,
  });

  return { ok: true as const, fotoId: anhang.id, dateiname: anhang.filename };
}
