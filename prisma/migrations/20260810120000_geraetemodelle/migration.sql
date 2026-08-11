-- ---------------------------------------------------------------------------
-- Gerätemodelle und zwei neue Zustände
--
-- Zwei Dinge, die sich im Alltag zeigten:
--
--  1. Dasselbe Modell wird zwanzigmal beschafft. Als Freitextfeld hieß es beim
--     dritten Mal anders, und das Foto musste jedes Mal neu angehängt werden.
--     Modelle sind jetzt eine eigene Tabelle und tragen das Bild – jedes
--     weitere Gerät dieses Modells erbt es.
--
--  2. Zwischen "liegt im Lager" und "hat jemand" fehlten die Zustände, in denen
--     die IT tatsächlich Arbeit hat: einrichten und bereitstellen.
--
-- Wie in der vorigen Migration ohne PL/pgSQL – reines SQL, damit sie auch dort
-- läuft, wo die Bibliothek plpgsql nicht geladen werden kann.
-- ---------------------------------------------------------------------------

-- 1. Neue Zustände ---------------------------------------------------------
-- Die Reihenfolge in der Aufzählung bestimmt, wie PostgreSQL sortiert. Damit
-- die Listen dem Weg eines Geräts folgen, werden die beiden neuen Werte an
-- ihrer inhaltlich richtigen Stelle eingefügt statt hinten angehängt.

ALTER TYPE "AssetStatus" ADD VALUE IF NOT EXISTS 'IN_VORBEREITUNG' AFTER 'LAGER';
ALTER TYPE "AssetStatus" ADD VALUE IF NOT EXISTS 'BEREIT' AFTER 'IN_VORBEREITUNG';

-- 2. Modelle ---------------------------------------------------------------

CREATE TABLE "AssetModel" (
  "id"             TEXT NOT NULL,
  "manufacturerId" TEXT NOT NULL,
  "name"           TEXT NOT NULL,
  "kindId"         TEXT,
  "photoId"        TEXT,
  "notes"          TEXT,
  "isActive"       BOOLEAN NOT NULL DEFAULT true,
  "createdAt"      TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP,
  "updatedAt"      TIMESTAMP(3) NOT NULL,
  CONSTRAINT "AssetModel_pkey" PRIMARY KEY ("id")
);

CREATE UNIQUE INDEX "AssetModel_photoId_key" ON "AssetModel"("photoId");
CREATE UNIQUE INDEX "AssetModel_manufacturerId_name_key"
  ON "AssetModel"("manufacturerId", "name");
CREATE INDEX "AssetModel_isActive_name_idx" ON "AssetModel"("isActive", "name");

ALTER TABLE "AssetModel" ADD CONSTRAINT "AssetModel_manufacturerId_fkey"
  FOREIGN KEY ("manufacturerId") REFERENCES "Manufacturer"("id")
  ON DELETE CASCADE ON UPDATE CASCADE;
ALTER TABLE "AssetModel" ADD CONSTRAINT "AssetModel_kindId_fkey"
  FOREIGN KEY ("kindId") REFERENCES "AssetKind"("id")
  ON DELETE SET NULL ON UPDATE CASCADE;
ALTER TABLE "AssetModel" ADD CONSTRAINT "AssetModel_photoId_fkey"
  FOREIGN KEY ("photoId") REFERENCES "Attachment"("id")
  ON DELETE SET NULL ON UPDATE CASCADE;

-- 3. Übernahme der bisherigen Freitext-Modelle ------------------------------

ALTER TABLE "Asset" ADD COLUMN "modelId" TEXT;

-- 3a. Aus jeder Kombination Hersteller + Modellname wird ein Modell.
--
--     Ohne Hersteller lässt sich kein Modell anlegen – die Tabelle verlangt
--     ihn, und ein Modell ohne Hersteller wäre auch fachlich sinnlos. Solche
--     Geräte behalten schlicht kein Modell; der Text ist in Abschnitt 4 in den
--     Notizen gesichert, damit nichts verloren geht.
--
--     Zusammengefasst wird ohne Rücksicht auf Gross- und Kleinschreibung,
--     aus demselben Grund wie bei den Herstellern.
INSERT INTO "AssetModel" ("id", "manufacturerId", "name", "kindId", "updatedAt")
SELECT DISTINCT ON (a."manufacturerId", lower(btrim(a."model")))
  gen_random_uuid()::text,
  a."manufacturerId",
  btrim(a."model"),
  -- Die häufigste Geräteart dieser Kombination als Vorbelegung.
  (SELECT b."kindId" FROM "Asset" b
    WHERE b."manufacturerId" = a."manufacturerId"
      AND lower(btrim(b."model")) = lower(btrim(a."model"))
    GROUP BY b."kindId"
    ORDER BY count(*) DESC, b."kindId"
    LIMIT 1),
  CURRENT_TIMESTAMP
FROM "Asset" a
WHERE a."manufacturerId" IS NOT NULL
  AND btrim(coalesce(a."model", '')) <> ''
ORDER BY a."manufacturerId", lower(btrim(a."model")), btrim(a."model");

UPDATE "Asset" a
SET "modelId" = m."id"
FROM "AssetModel" m
WHERE a."manufacturerId" = m."manufacturerId"
  AND lower(btrim(coalesce(a."model", ''))) = lower(m."name");

-- 3b. Modellangaben ohne Hersteller in die Notizen retten, bevor die Spalte
--     verschwindet. Lieber eine Zeile mehr im Notizfeld als eine verlorene
--     Angabe, die jemand vor Jahren eingetippt hat.
UPDATE "Asset"
SET "notes" = trim(both E'\n' FROM
      coalesce("notes" || E'\n', '') || 'Modell (aus der Übernahme): ' || btrim("model"))
WHERE "modelId" IS NULL
  AND btrim(coalesce("model", '')) <> '';

ALTER TABLE "Asset" DROP COLUMN "model";

CREATE INDEX "Asset_modelId_idx" ON "Asset"("modelId");
ALTER TABLE "Asset" ADD CONSTRAINT "Asset_modelId_fkey"
  FOREIGN KEY ("modelId") REFERENCES "AssetModel"("id")
  ON DELETE SET NULL ON UPDATE CASCADE;
