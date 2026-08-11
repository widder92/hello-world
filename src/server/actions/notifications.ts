"use server";

import { revalidatePath } from "next/cache";

import { db } from "@/server/db";
import { requireActor } from "@/server/policy";

/** Die letzten Hinweise der angemeldeten Person. */
export async function ladeBenachrichtigungen() {
  const actor = await requireActor();

  const eintraege = await db.notification.findMany({
    where: { userId: actor.id },
    orderBy: { createdAt: "desc" },
    take: 25,
    select: {
      id: true,
      title: true,
      body: true,
      createdAt: true,
      readAt: true,
      ticket: { select: { number: true } },
    },
  });

  return eintraege.map((e) => ({
    id: e.id,
    title: e.title,
    body: e.body,
    createdAt: e.createdAt.toISOString(),
    readAt: e.readAt?.toISOString() ?? null,
    ticketNumber: e.ticket?.number ?? null,
  }));
}

export async function markiereAlleGelesen() {
  const actor = await requireActor();

  await db.notification.updateMany({
    where: { userId: actor.id, readAt: null },
    data: { readAt: new Date() },
  });

  revalidatePath("/", "layout");
}

export async function markiereGelesen(notificationId: string) {
  const actor = await requireActor();

  // Über die Benutzer-Kennung mitgefiltert, damit sich nicht durch Raten einer
  // fremden Kennung die Hinweise anderer Personen als gelesen markieren lassen.
  await db.notification.updateMany({
    where: { id: notificationId, userId: actor.id },
    data: { readAt: new Date() },
  });

  revalidatePath("/", "layout");
}
