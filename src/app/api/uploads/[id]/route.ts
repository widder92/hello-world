import { createReadStream } from "node:fs";
import { stat } from "node:fs/promises";
import path from "node:path";
import { Readable } from "node:stream";

import { db } from "@/server/db";
import { getActor, inventoryAccess, ticketAccessLevel } from "@/server/policy";

/**
 * Auslieferung von Anhängen.
 *
 * Dateien liegen außerhalb des öffentlichen Verzeichnisses und werden nur über
 * diesen Weg herausgegeben – nach derselben Rechteprüfung wie der Vorgang
 * selbst. Läge ein Foto vom Schaden im Wohnbereich unter einer ratbaren
 * öffentlichen Adresse, wäre die gesamte Zugriffsprüfung wirkungslos.
 *
 * Ein Anhang hängt entweder an einem Vorgang oder an einem Gerät. Beides wird
 * getrennt geprüft; was an keinem von beiden hängt, wird nicht ausgeliefert.
 */
export async function GET(
  _request: Request,
  { params }: { params: Promise<{ id: string }> },
) {
  const actor = await getActor();
  if (!actor) {
    return new Response("Nicht angemeldet.", { status: 401 });
  }

  const { id } = await params;

  const anhang = await db.attachment.findUnique({
    where: { id },
    select: {
      filename: true,
      mimeType: true,
      size: true,
      storageKey: true,
      ticket: {
        select: {
          id: true,
          queueId: true,
          teamId: true,
          requesterId: true,
          watchers: { select: { userId: true } },
        },
      },
      asset: { select: { id: true, assignedToId: true } },
      fotoVon: { select: { id: true, assignedToId: true } },
    },
  });

  if (!anhang) {
    return new Response("Datei nicht gefunden.", { status: 404 });
  }

  // Bewusst überall 404 statt 403: Sonst ließe sich durch Durchprobieren von
  // Kennungen feststellen, welche Dateien es gibt.
  const nichtGefunden = new Response("Datei nicht gefunden.", { status: 404 });

  // Das Foto hängt über Asset.photoId am Gerät, weitere Dateien (Rechnung,
  // Lieferschein) über Attachment.assetId – beide Wege zählen.
  const geraet = anhang.asset ?? anhang.fotoVon;

  if (anhang.ticket) {
    if (ticketAccessLevel(actor, anhang.ticket) === "none") return nichtGefunden;
  } else if (geraet) {
    // Wer das Gerät in der Hand hat, darf sein Foto sehen – sonst zeigte die
    // Seite „Meine Geräte" ein leeres Bild für das eigene Notebook.
    const darf =
      inventoryAccess(actor) !== "none" || geraet.assignedToId === actor.id;
    if (!darf) return nichtGefunden;
  } else {
    return nichtGefunden;
  }

  const basis = path.resolve(process.env.UPLOAD_DIR || "./uploads");
  const datei = path.resolve(basis, anhang.storageKey);

  // Schutz vor Pfadwechseln: Ein manipulierter Schlüssel wie "../../etc/passwd"
  // darf nicht aus dem Ablageverzeichnis herausführen.
  if (!datei.startsWith(basis + path.sep)) {
    return new Response("Datei nicht gefunden.", { status: 404 });
  }

  try {
    await stat(datei);
  } catch {
    return new Response("Die Datei ist nicht mehr vorhanden.", { status: 404 });
  }

  const strom = Readable.toWeb(
    createReadStream(datei),
  ) as unknown as ReadableStream;

  return new Response(strom, {
    headers: {
      "Content-Type": anhang.mimeType,
      "Content-Length": String(anhang.size),
      // "inline" für Bilder und PDF, damit sie sich im Browser ansehen lassen;
      // alles andere wird heruntergeladen statt ausgeführt.
      "Content-Disposition": `${
        /^(image\/|application\/pdf|text\/plain)/.test(anhang.mimeType)
          ? "inline"
          : "attachment"
      }; filename="${encodeURIComponent(anhang.filename)}"`,
      // Verhindert, dass der Browser den Typ selbst errät und etwa eine
      // hochgeladene Datei als HTML ausführt.
      "X-Content-Type-Options": "nosniff",
      "Cache-Control": "private, max-age=3600",
    },
  });
}
