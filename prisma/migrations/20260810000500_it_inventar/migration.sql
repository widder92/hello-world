-- ---------------------------------------------------------------------------
-- IT-Inventar
--
-- Aus der bisherigen Geräteliste (acht Felder, nur lesbar) wird ein geführtes
-- Inventar: Status, Kostenstellen, Hersteller, MAC-Adressen, Fotos, Chronik
-- und Übergabeprotokolle.
--
-- Der heikle Teil steht in Abschnitt 6: Die vorhandenen Geräte müssen die
-- Umstellung überleben. Sie hängen über "TicketAsset" an Vorgängen, und ein
-- verlorener Datensatz nähme dem Vorgang die Angabe, um welches Gerät es
-- überhaupt ging. Deshalb werden die neuen Spalten erst angelegt und gefüllt
-- und die alten erst danach entfernt – nie umgekehrt.
-- ---------------------------------------------------------------------------

-- 1. Aufzählungen ----------------------------------------------------------

CREATE TYPE "AssetStatus" AS ENUM (
  'LAGER', 'RESERVIERT', 'HERAUSGEGEBEN', 'REPARATUR', 'AUSGESONDERT', 'VERLUST'
);

CREATE TYPE "MacKind" AS ENUM ('LAN', 'WLAN', 'BLUETOOTH', 'SONSTIGE');

CREATE TYPE "HandoverDirection" AS ENUM ('AUSGABE', 'RUECKNAHME');

CREATE TYPE "AssetEventType" AS ENUM (
  'ANGELEGT', 'GEAENDERT', 'STATUS', 'HERAUSGEGEBEN', 'ZURUECKGENOMMEN', 'NOTIZ'
);

-- 2. Stammdaten ------------------------------------------------------------

CREATE TABLE "CostCenter" (
  "id"          TEXT NOT NULL,
  "code"        TEXT NOT NULL,
  "name"        TEXT NOT NULL,
  "description" TEXT,
  "sortOrder"   INTEGER NOT NULL DEFAULT 0,
  "isActive"    BOOLEAN NOT NULL DEFAULT true,
  "createdAt"   TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP,
  "updatedAt"   TIMESTAMP(3) NOT NULL,
  CONSTRAINT "CostCenter_pkey" PRIMARY KEY ("id")
);
CREATE UNIQUE INDEX "CostCenter_code_key" ON "CostCenter"("code");
CREATE INDEX "CostCenter_isActive_sortOrder_idx" ON "CostCenter"("isActive", "sortOrder");

CREATE TABLE "AssetKind" (
  "id"                  TEXT NOT NULL,
  "key"                 TEXT NOT NULL,
  "name"                TEXT NOT NULL,
  "icon"                TEXT NOT NULL DEFAULT 'Package',
  "sortOrder"           INTEGER NOT NULL DEFAULT 0,
  "isActive"            BOOLEAN NOT NULL DEFAULT true,
  "requiresSerial"      BOOLEAN NOT NULL DEFAULT true,
  "requiresMac"         BOOLEAN NOT NULL DEFAULT false,
  "defaultCostCenterId" TEXT,
  "createdAt"           TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP,
  "updatedAt"           TIMESTAMP(3) NOT NULL,
  CONSTRAINT "AssetKind_pkey" PRIMARY KEY ("id")
);
CREATE UNIQUE INDEX "AssetKind_key_key" ON "AssetKind"("key");
CREATE UNIQUE INDEX "AssetKind_name_key" ON "AssetKind"("name");
CREATE INDEX "AssetKind_isActive_sortOrder_idx" ON "AssetKind"("isActive", "sortOrder");

ALTER TABLE "AssetKind" ADD CONSTRAINT "AssetKind_defaultCostCenterId_fkey"
  FOREIGN KEY ("defaultCostCenterId") REFERENCES "CostCenter"("id")
  ON DELETE SET NULL ON UPDATE CASCADE;

CREATE TABLE "Manufacturer" (
  "id"           TEXT NOT NULL,
  "name"         TEXT NOT NULL,
  "website"      TEXT,
  "supportPhone" TEXT,
  "notes"        TEXT,
  "isActive"     BOOLEAN NOT NULL DEFAULT true,
  "createdAt"    TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP,
  "updatedAt"    TIMESTAMP(3) NOT NULL,
  CONSTRAINT "Manufacturer_pkey" PRIMARY KEY ("id")
);
CREATE UNIQUE INDEX "Manufacturer_name_key" ON "Manufacturer"("name");
CREATE INDEX "Manufacturer_isActive_name_idx" ON "Manufacturer"("isActive", "name");

-- Laufende Nummern, gleiche Bauart wie "TicketCounter".
CREATE TABLE "AssetCounter" (
  "id"     TEXT NOT NULL,
  "kindId" TEXT NOT NULL,
  "value"  INTEGER NOT NULL DEFAULT 0,
  CONSTRAINT "AssetCounter_pkey" PRIMARY KEY ("id")
);
CREATE UNIQUE INDEX "AssetCounter_kindId_key" ON "AssetCounter"("kindId");
ALTER TABLE "AssetCounter" ADD CONSTRAINT "AssetCounter_kindId_fkey"
  FOREIGN KEY ("kindId") REFERENCES "AssetKind"("id")
  ON DELETE CASCADE ON UPDATE CASCADE;

CREATE TABLE "HandoverCounter" (
  "id"    TEXT NOT NULL,
  "year"  INTEGER NOT NULL,
  "value" INTEGER NOT NULL DEFAULT 0,
  CONSTRAINT "HandoverCounter_pkey" PRIMARY KEY ("id")
);
CREATE UNIQUE INDEX "HandoverCounter_year_key" ON "HandoverCounter"("year");

-- 3. Bereichsschalter ------------------------------------------------------

ALTER TABLE "Team" ADD COLUMN "hasInventory" BOOLEAN NOT NULL DEFAULT false;

-- 4. Anhänge können jetzt auch an einem Gerät hängen -----------------------
-- Ohne diese Spalte gäbe es kein Gerätefoto: Ein Anhang ohne Ticket wird von
-- der Auslieferungsroute verworfen.

ALTER TABLE "Attachment" ADD COLUMN "assetId" TEXT;
CREATE INDEX "Attachment_assetId_idx" ON "Attachment"("assetId");

-- 5. Neue Spalten am Gerät – zunächst alle ohne Pflicht --------------------

ALTER TABLE "Asset"
  ADD COLUMN "kindId"             TEXT,
  ADD COLUMN "teamId"             TEXT,
  ADD COLUMN "status"             "AssetStatus" NOT NULL DEFAULT 'LAGER',
  ADD COLUMN "manufacturerId"     TEXT,
  ADD COLUMN "imei"               TEXT,
  ADD COLUMN "storageLocation"    TEXT,
  ADD COLUMN "assignedAt"         TIMESTAMP(3),
  ADD COLUMN "costCenterId"       TEXT,
  ADD COLUMN "purchasePriceCents" INTEGER,
  ADD COLUMN "supplier"           TEXT,
  ADD COLUMN "invoiceNumber"      TEXT,
  ADD COLUMN "photoId"            TEXT,
  ADD COLUMN "retiredAt"          TIMESTAMP(3),
  ADD COLUMN "retiredReason"      TEXT;

-- 6. Übernahme der vorhandenen Geräte --------------------------------------

-- 6a. Gerätearten aus den bisherigen Enum-Werten. Angelegt werden alle neun,
--     nicht nur die tatsächlich benutzten: Was vorher auswählbar war, soll
--     auswählbar bleiben.
INSERT INTO "AssetKind"
  ("id", "key", "name", "icon", "sortOrder", "requiresSerial", "requiresMac", "updatedAt")
VALUES
  (gen_random_uuid()::text, 'PC',  'PC',               'Monitor',    10, true,  true,  CURRENT_TIMESTAMP),
  (gen_random_uuid()::text, 'NB',  'Notebook',         'Laptop',     20, true,  true,  CURRENT_TIMESTAMP),
  (gen_random_uuid()::text, 'TAB', 'Tablet',           'Tablet',     30, true,  true,  CURRENT_TIMESTAMP),
  (gen_random_uuid()::text, 'TEL', 'Telefon',          'Smartphone', 40, true,  false, CURRENT_TIMESTAMP),
  (gen_random_uuid()::text, 'MON', 'Monitor',          'Monitor',    50, true,  false, CURRENT_TIMESTAMP),
  (gen_random_uuid()::text, 'DRU', 'Drucker',          'Printer',    60, true,  true,  CURRENT_TIMESTAMP),
  (gen_random_uuid()::text, 'NET', 'Netzwerk',         'Network',    70, true,  true,  CURRENT_TIMESTAMP),
  (gen_random_uuid()::text, 'LIZ', 'Software-Lizenz',  'KeyRound',   80, false, false, CURRENT_TIMESTAMP),
  (gen_random_uuid()::text, 'SON', 'Sonstiges',        'Package',    90, false, false, CURRENT_TIMESTAMP);

INSERT INTO "AssetCounter" ("id", "kindId", "value")
SELECT gen_random_uuid()::text, "id", 0 FROM "AssetKind";

-- 6b. Hersteller aus den bisherigen Freitextwerten.
--
--     Zusammengefasst wird ohne Rücksicht auf Gross- und Kleinschreibung:
--     "Dell" und "dell" sind derselbe Hersteller. Genau dafür gibt es die
--     Tabelle – sie einfach mit den alten Schreibweisen zu füllen, würde die
--     Unordnung nur von einer Spalte in eine Tabelle verschieben.
--
--     Welche Schreibweise gewinnt, entscheidet die alphabetische Reihenfolge.
--     Das ist willkürlich, aber vorhersehbar; nachbessern lässt sich der Name
--     danach in der Verwaltung an einer Stelle für alle Geräte.
INSERT INTO "Manufacturer" ("id", "name", "updatedAt")
SELECT DISTINCT ON (lower(btrim("manufacturer")))
  gen_random_uuid()::text, btrim("manufacturer"), CURRENT_TIMESTAMP
FROM "Asset"
WHERE btrim(coalesce("manufacturer", '')) <> ''
ORDER BY lower(btrim("manufacturer")), btrim("manufacturer")
ON CONFLICT ("name") DO NOTHING;

-- 6c. Verweise setzen.
UPDATE "Asset" a
SET "kindId" = k."id"
FROM "AssetKind" k
WHERE k."key" = CASE a."type"::text
  WHEN 'PC'              THEN 'PC'
  WHEN 'NOTEBOOK'        THEN 'NB'
  WHEN 'TABLET'          THEN 'TAB'
  WHEN 'TELEFON'         THEN 'TEL'
  WHEN 'MONITOR'         THEN 'MON'
  WHEN 'DRUCKER'         THEN 'DRU'
  WHEN 'NETZWERK'        THEN 'NET'
  WHEN 'SOFTWARE_LIZENZ' THEN 'LIZ'
  ELSE 'SON'
END;

UPDATE "Asset" a
SET "manufacturerId" = m."id"
FROM "Manufacturer" m
WHERE lower(btrim(coalesce(a."manufacturer", ''))) = lower(m."name");

-- Führender Bereich: die IT, sonst der erste aktive Bereich. Der Rückfall ist
-- kein Schönheitsfehler, sondern verhindert, dass die Migration an einem
-- Bestand ohne Bereich namens "IT" abbricht und Geräte zurücklässt.
UPDATE "Asset"
SET "teamId" = COALESCE(
  (SELECT "id" FROM "Team" WHERE "key" = 'IT' LIMIT 1),
  (SELECT "id" FROM "Team" WHERE "isActive" ORDER BY "sortOrder", "name" LIMIT 1)
)
WHERE "teamId" IS NULL;

-- 6d. Status aus den bisherigen Angaben ableiten.
UPDATE "Asset" SET
  "status" = CASE
    WHEN "isActive" = false        THEN 'AUSGESONDERT'::"AssetStatus"
    WHEN "assignedToId" IS NOT NULL THEN 'HERAUSGEGEBEN'::"AssetStatus"
    ELSE 'LAGER'::"AssetStatus"
  END,
  -- Für bereits herausgegebene Geräte fehlt das echte Übergabedatum. Das
  -- Anschaffungsdatum ist die ehrlichste verfügbare Näherung; wo auch das
  -- fehlt, bleibt das Feld leer statt geraten.
  "assignedAt" = CASE
    WHEN "assignedToId" IS NOT NULL THEN "purchasedAt"
    ELSE NULL
  END,
  "retiredAt" = CASE WHEN "isActive" = false THEN "updatedAt" ELSE NULL END,
  "retiredReason" = CASE
    WHEN "isActive" = false THEN 'Bei der Umstellung auf das Inventar als ausgesondert übernommen.'
    ELSE NULL
  END;

-- 6e. Kein Gerät darf ohne Geräteart oder Bereich zurückbleiben.
--
--     Geprüft wird das über zwei benannte Bedingungen statt über einen
--     PL/pgSQL-Block: Diese Migration soll mit reinem SQL auskommen. Ein
--     `DO $$ … $$` verlangt die Erweiterung plpgsql, und wo deren Bibliothek
--     nicht geladen werden kann – unter Windows blockieren
--     Virenschutzprogramme sie gelegentlich –, scheitert sonst die gesamte
--     Umstellung an einer Sicherheitsprüfung, die selbst nichts verändert.
--
--     Die Bedingungen tragen sprechende Namen, damit die Fehlermeldung sagt,
--     woran es liegt. Direkt danach werden sie wieder entfernt: Ihr Zweck ist
--     die einmalige Prüfung, nicht die dauerhafte Regel – die steht ab
--     Abschnitt 7 als NOT NULL in der Tabelle.
ALTER TABLE "Asset"
  ADD CONSTRAINT "uebernahme_jedes_geraet_braucht_eine_geraeteart"
    CHECK ("kindId" IS NOT NULL),
  ADD CONSTRAINT "uebernahme_jedes_geraet_braucht_einen_bereich"
    CHECK ("teamId" IS NOT NULL);

ALTER TABLE "Asset"
  DROP CONSTRAINT "uebernahme_jedes_geraet_braucht_eine_geraeteart",
  DROP CONSTRAINT "uebernahme_jedes_geraet_braucht_einen_bereich";

-- 7. Alte Spalten entfernen – erst jetzt, wo alles übernommen ist ----------

ALTER TABLE "Asset"
  ALTER COLUMN "kindId" SET NOT NULL,
  ALTER COLUMN "teamId" SET NOT NULL,
  DROP COLUMN "type",
  DROP COLUMN "manufacturer",
  DROP COLUMN "isActive";

DROP TYPE "AssetType";

-- 8. Kindtabellen ----------------------------------------------------------

CREATE TABLE "AssetMac" (
  "id"        TEXT NOT NULL,
  "assetId"   TEXT NOT NULL,
  "address"   TEXT NOT NULL,
  "kind"      "MacKind" NOT NULL DEFAULT 'LAN',
  "label"     TEXT,
  "createdAt" TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP,
  CONSTRAINT "AssetMac_pkey" PRIMARY KEY ("id")
);
CREATE UNIQUE INDEX "AssetMac_address_key" ON "AssetMac"("address");
CREATE INDEX "AssetMac_assetId_idx" ON "AssetMac"("assetId");

CREATE TABLE "AssetEvent" (
  "id"         TEXT NOT NULL,
  "assetId"    TEXT NOT NULL,
  "type"       "AssetEventType" NOT NULL,
  "fromStatus" "AssetStatus",
  "toStatus"   "AssetStatus",
  "userId"     TEXT,
  "actorId"    TEXT,
  "actorName"  TEXT NOT NULL,
  "note"       TEXT,
  "createdAt"  TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP,
  CONSTRAINT "AssetEvent_pkey" PRIMARY KEY ("id")
);
CREATE INDEX "AssetEvent_assetId_createdAt_idx" ON "AssetEvent"("assetId", "createdAt");

CREATE TABLE "AssetHandover" (
  "id"           TEXT NOT NULL,
  "protocolNo"   TEXT NOT NULL,
  "assetId"      TEXT NOT NULL,
  "userId"       TEXT,
  "userName"     TEXT NOT NULL,
  "direction"    "HandoverDirection" NOT NULL,
  "handedOverAt" TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP,
  "actorId"      TEXT,
  "actorName"    TEXT NOT NULL,
  "condition"    TEXT,
  "accessories"  TEXT,
  "note"         TEXT,
  "createdAt"    TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP,
  CONSTRAINT "AssetHandover_pkey" PRIMARY KEY ("id")
);
CREATE UNIQUE INDEX "AssetHandover_protocolNo_key" ON "AssetHandover"("protocolNo");
CREATE INDEX "AssetHandover_assetId_handedOverAt_idx" ON "AssetHandover"("assetId", "handedOverAt");
CREATE INDEX "AssetHandover_userId_idx" ON "AssetHandover"("userId");

-- 9. Indizes und Fremdschlüssel am Gerät -----------------------------------

CREATE UNIQUE INDEX "Asset_photoId_key" ON "Asset"("photoId");
CREATE INDEX "Asset_teamId_status_idx" ON "Asset"("teamId", "status");
CREATE INDEX "Asset_kindId_idx" ON "Asset"("kindId");
CREATE INDEX "Asset_costCenterId_idx" ON "Asset"("costCenterId");
CREATE INDEX "Asset_serial_idx" ON "Asset"("serial");

ALTER TABLE "Asset" ADD CONSTRAINT "Asset_kindId_fkey"
  FOREIGN KEY ("kindId") REFERENCES "AssetKind"("id") ON DELETE RESTRICT ON UPDATE CASCADE;
ALTER TABLE "Asset" ADD CONSTRAINT "Asset_teamId_fkey"
  FOREIGN KEY ("teamId") REFERENCES "Team"("id") ON DELETE RESTRICT ON UPDATE CASCADE;
ALTER TABLE "Asset" ADD CONSTRAINT "Asset_manufacturerId_fkey"
  FOREIGN KEY ("manufacturerId") REFERENCES "Manufacturer"("id") ON DELETE SET NULL ON UPDATE CASCADE;
ALTER TABLE "Asset" ADD CONSTRAINT "Asset_costCenterId_fkey"
  FOREIGN KEY ("costCenterId") REFERENCES "CostCenter"("id") ON DELETE SET NULL ON UPDATE CASCADE;
ALTER TABLE "Asset" ADD CONSTRAINT "Asset_photoId_fkey"
  FOREIGN KEY ("photoId") REFERENCES "Attachment"("id") ON DELETE SET NULL ON UPDATE CASCADE;

ALTER TABLE "Attachment" ADD CONSTRAINT "Attachment_assetId_fkey"
  FOREIGN KEY ("assetId") REFERENCES "Asset"("id") ON DELETE CASCADE ON UPDATE CASCADE;

ALTER TABLE "AssetMac" ADD CONSTRAINT "AssetMac_assetId_fkey"
  FOREIGN KEY ("assetId") REFERENCES "Asset"("id") ON DELETE CASCADE ON UPDATE CASCADE;

ALTER TABLE "AssetEvent" ADD CONSTRAINT "AssetEvent_assetId_fkey"
  FOREIGN KEY ("assetId") REFERENCES "Asset"("id") ON DELETE CASCADE ON UPDATE CASCADE;
ALTER TABLE "AssetEvent" ADD CONSTRAINT "AssetEvent_userId_fkey"
  FOREIGN KEY ("userId") REFERENCES "User"("id") ON DELETE SET NULL ON UPDATE CASCADE;
ALTER TABLE "AssetEvent" ADD CONSTRAINT "AssetEvent_actorId_fkey"
  FOREIGN KEY ("actorId") REFERENCES "User"("id") ON DELETE SET NULL ON UPDATE CASCADE;

ALTER TABLE "AssetHandover" ADD CONSTRAINT "AssetHandover_assetId_fkey"
  FOREIGN KEY ("assetId") REFERENCES "Asset"("id") ON DELETE CASCADE ON UPDATE CASCADE;
ALTER TABLE "AssetHandover" ADD CONSTRAINT "AssetHandover_userId_fkey"
  FOREIGN KEY ("userId") REFERENCES "User"("id") ON DELETE SET NULL ON UPDATE CASCADE;
ALTER TABLE "AssetHandover" ADD CONSTRAINT "AssetHandover_actorId_fkey"
  FOREIGN KEY ("actorId") REFERENCES "User"("id") ON DELETE SET NULL ON UPDATE CASCADE;

-- 10. Suche ----------------------------------------------------------------
-- Der Trigram-Index auf "Asset"."name" aus der Volltextsuche-Migration bleibt
-- bestehen. Ergänzt wird die Seriennummer: Danach wird am häufigsten gesucht,
-- meist von einem Aufkleber abgetippt und deshalb oft mit Tippfehlern.

CREATE INDEX IF NOT EXISTS "Asset_serial_trgm_idx"
  ON "Asset" USING gin ("serial" gin_trgm_ops);
