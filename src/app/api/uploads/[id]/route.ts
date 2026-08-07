import { createReadStream } from "node:fs";
import { stat } from "node:fs/promises";
import path from "node:path";
import { Readable } from "node:stream";

import { db } from "@/server/db";
import { getActor, ticketAccessLevel } from "@/server/policy";

/**
 * Auslieferung von Anhängen.
 *
 * Dateien liegen außerhalb des öffentlichen Verzeichnisses und werden nur über
 * diesen Weg herausgegeben – nach derselben Rechteprüfung wie der Vorgang
 * selbst. Läge ein Foto vom Schaden im Wohnbereich unter einer ratbaren
 * öffentlichen Adresse, wäre die gesamte Zugriffsprüfung wirkungslos.
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
    },
  });

  if (!anhang?.ticket) {
    return new Response("Datei nicht gefunden.", { status: 404 });
  }

  if (ticketAccessLevel(actor, anhang.ticket) === "none") {
    // Bewusst 404 statt 403: Sonst ließe sich durch Durchprobieren von
    // Kennungen feststellen, welche Dateien es gibt.
    return new Response("Datei nicht gefunden.", { status: 404 });
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
