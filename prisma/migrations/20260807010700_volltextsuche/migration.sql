-- Volltextsuche mit deutscher Wortstammerkennung.
--
-- "Drucker", "Druckern" und "Druckers" sollen denselben Treffer liefern, und
-- eine Suche nach "heizung kaputt" auch Tickets finden, in denen "Heizung ist
-- defekt" im Verlauf steht. Dafür wird je Ticket und je Nachricht eine
-- tsvector-Spalte mitgeführt und per GIN-Index durchsucht.

-- --- Tickets ---------------------------------------------------------------
-- Betreff wiegt am schwersten (A), die Ticketnummer erlaubt die direkte Suche
-- nach "HM-2026-000087" (B), Raumangabe ergänzt (C).
ALTER TABLE "Ticket"
  ADD COLUMN "searchVector" tsvector
  GENERATED ALWAYS AS (
    setweight(to_tsvector('german', coalesce("subject", '')), 'A') ||
    setweight(to_tsvector('simple', coalesce("number", '')), 'B') ||
    setweight(to_tsvector('german', coalesce("roomOrArea", '')), 'C')
  ) STORED;

CREATE INDEX "Ticket_searchVector_idx" ON "Ticket" USING GIN ("searchVector");

-- --- Nachrichten -----------------------------------------------------------
-- Der reine Text der Nachricht, damit auch der Verlauf durchsuchbar ist.
ALTER TABLE "TicketMessage"
  ADD COLUMN "searchVector" tsvector
  GENERATED ALWAYS AS (
    to_tsvector('german', coalesce("bodyText", ''))
  ) STORED;

CREATE INDEX "TicketMessage_searchVector_idx" ON "TicketMessage" USING GIN ("searchVector");

-- --- Wissensdatenbank ------------------------------------------------------
ALTER TABLE "KbArticle"
  ADD COLUMN "searchVector" tsvector
  GENERATED ALWAYS AS (
    setweight(to_tsvector('german', coalesce("title", '')), 'A') ||
    setweight(to_tsvector('german', coalesce("summary", '')), 'B') ||
    setweight(to_tsvector('german', coalesce("body", '')), 'C')
  ) STORED;

CREATE INDEX "KbArticle_searchVector_idx" ON "KbArticle" USING GIN ("searchVector");

-- --- Unscharfe Suche -------------------------------------------------------
-- pg_trgm erlaubt Treffer trotz Tippfehlern ("Druker" → "Drucker") und schnelle
-- ILIKE-Suchen auf Namen. Beides brauchen die Personensuche im Zuweisen-Dialog
-- und die Gerätesuche.
CREATE EXTENSION IF NOT EXISTS pg_trgm;

CREATE INDEX "User_name_trgm_idx" ON "User" USING GIN ("name" gin_trgm_ops);
CREATE INDEX "Asset_name_trgm_idx" ON "Asset" USING GIN ("name" gin_trgm_ops);
