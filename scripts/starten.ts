/**
 * Die gebaute Anwendung starten.
 *
 * Diese Anwendung wird als „standalone" gebaut – ein in sich geschlossener
 * Ordner unter `.next/standalone`, den auch das Docker-Abbild ausführt.
 * Next.js legt dort aber weder die statischen Dateien noch den Ordner `public`
 * ab; das übernimmt üblicherweise das Dockerfile. Wer ohne Docker startet,
 * müsste beides von Hand kopieren – und `next start` warnt in dieser
 * Betriebsart ausdrücklich davor, dass es nicht der vorgesehene Weg ist.
 *
 * Dieses Skript nimmt beides ab: kopieren, dann starten. Bewusst mit den
 * Bordmitteln von Node und nicht mit `cp -r`, damit es unter Windows genauso
 * läuft wie unter Mac und Linux.
 */

import { spawn } from "node:child_process";
import { cpSync, existsSync } from "node:fs";
import path from "node:path";

const WURZEL = process.cwd();
const STANDALONE = path.join(WURZEL, ".next", "standalone");

if (!existsSync(path.join(STANDALONE, "server.js"))) {
  console.error(
    "Es gibt noch keinen fertigen Bau. Bitte zuerst ausführen:\n\n" +
      "  npm run build\n",
  );
  process.exit(1);
}

/** Kopiert einen Ordner, wenn es ihn gibt. */
function uebernehmen(von: string, nach: string, was: string) {
  if (!existsSync(von)) return;
  cpSync(von, nach, { recursive: true });
  console.info(`[Starten] ${was} übernommen.`);
}

uebernehmen(
  path.join(WURZEL, ".next", "static"),
  path.join(STANDALONE, ".next", "static"),
  "Stilvorlagen und Skripte",
);
uebernehmen(
  path.join(WURZEL, "public"),
  path.join(STANDALONE, "public"),
  "Statische Dateien",
);

const port = process.env.PORT || "3000";
console.info(`[Starten] Ticketsystem läuft gleich auf http://localhost:${port}`);

const kind = spawn(process.execPath, [path.join(STANDALONE, "server.js")], {
  stdio: "inherit",
  env: { ...process.env, PORT: port },
});

kind.on("exit", (code) => process.exit(code ?? 0));
