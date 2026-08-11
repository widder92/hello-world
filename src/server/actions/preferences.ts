"use server";

import { revalidatePath } from "next/cache";
import { z } from "zod";

import { db } from "@/server/db";
import { requireActor } from "@/server/policy";

/**
 * Persönliche Einstellungen: Thema und Barrierefreiheitsmodi.
 *
 * Bewusst am Benutzerkonto gespeichert und nicht nur im Browser: Wer die
 * vergrößerte Darstellung braucht, braucht sie auch am Vertretungsrechner im
 * Dienstzimmer und nicht nur am eigenen Platz.
 */

const a11ySchema = z.object({
  largeText: z.boolean().optional(),
  highContrast: z.boolean().optional(),
  reduceMotion: z.boolean().optional(),
  simpleMode: z.boolean().optional(),
});

export type A11yPrefs = z.infer<typeof a11ySchema>;

export async function setThemeAction(theme: "light" | "dark" | "system") {
  const actor = await requireActor();

  if (!["light", "dark", "system"].includes(theme)) {
    throw new Error("Unbekanntes Thema.");
  }

  await db.user.update({
    where: { id: actor.id },
    data: { theme },
  });

  revalidatePath("/", "layout");
}

export async function setA11yPrefsAction(prefs: A11yPrefs) {
  const actor = await requireActor();
  const parsed = a11ySchema.parse(prefs);

  // Teilweise Angaben mit dem Bestand zusammenführen, damit ein Umschalten
  // eines einzelnen Modus die anderen nicht zurücksetzt.
  const merged = { ...actor.a11yPrefs, ...parsed };

  await db.user.update({
    where: { id: actor.id },
    data: { a11yPrefs: merged },
  });

  revalidatePath("/", "layout");
}

const profilSchema = z.object({
  name: z.string().trim().min(2, "Bitte geben Sie Ihren Namen an."),
  phone: z.string().trim().max(40).optional().or(z.literal("")),
  siteId: z.string().trim().optional().or(z.literal("")),
});

export async function updateProfileAction(input: z.infer<typeof profilSchema>) {
  const actor = await requireActor();
  const data = profilSchema.parse(input);

  await db.user.update({
    where: { id: actor.id },
    data: {
      name: data.name,
      phone: data.phone || null,
      // Der eigene Standort belegt später das Meldeformular vor.
      siteId: data.siteId || null,
    },
  });

  revalidatePath("/", "layout");
  return { ok: true as const };
}
