# Ticketsystem

Ein Ticketsystem für einen Träger der Behindertenhilfe: mehrere Bereiche,
mehrere Standorte, Schule, Wohnheime und Betreutes Wohnen.

Der Unterschied zu einem gewöhnlichen IT-Helpdesk steckt in einem Satz: **Wer
dem Hausmeister schreibt, landet beim Hausmeister** – und zwar bei dem des
eigenen Hauses, nicht bei der IT und nicht an einem anderen Standort. Die IT
ist einer von mehreren Bereichen, nicht die Poststelle für alle anderen.

---

## Die drei Ideen, auf denen alles aufbaut

### Bereich → Eingang → Vorgang

Ein **Bereich** ist eine Organisationseinheit: IT, Hausmeister, Küche,
Fahrdienst, Verwaltung. Ein **Eingang** ist die Stelle, an der die Vorgänge
eines Bereichs zusammenlaufen. Beides ist getrennt, weil Bereiche verschieden
arbeiten:

- Die **IT** arbeitet zentral: ein Eingang für alle Standorte. Der Standort
  steht am Vorgang, sortiert ihn aber nicht (`queueMode = SINGLE`).
- Die **Hausmeister** arbeiten am Haus: je Standort ein eigener Eingang, und
  wer meldet, wählt beim Melden aus, welches Haus betroffen ist
  (`queueMode = PER_SITE`).

Die Freigabe für einen Eingang steuert, wer dort arbeitet. Der Hausmeister aus
Wohnheim Nord kommt so nicht an die Vorgänge der Schule, obwohl beide im selben
Bereich sind. Umstellen lässt sich das jederzeit in der Verwaltung; bestehende
Eingänge werden dabei nie gelöscht, sondern nur stillgelegt – sonst gingen die
Vorgänge darin verloren.

### Zwei Ausbaustufen

Nicht jeder Bereich braucht das volle System. Die Stufe wird je Bereich
eingestellt und ist jederzeit umschaltbar.

|                                | Schlank | Voll |
|--------------------------------|:-------:|:----:|
| Vorgänge annehmen, beantworten, abschließen | ✓ | ✓ |
| Interne Notizen                 | ✓ | ✓ |
| An anderen Bereich weiterleiten | ✓ | ✓ |
| Teilvorgang abspalten           | ✓ | ✓ |
| Bearbeitung per Antwortmail     | ✓ | ✓ |
| Sammeleingang mit „Ticket ziehen" | – | ✓ |
| Zuweisung, automatische Verteilung | – | ✓ |
| Fristen und Ampel               | – | ✓ |
| Priorität, Kategorien, Vorlagen | – | ✓ |
| Auswertung, Geräte, Checklisten | – | ✓ |

Die schlanke Stufe ist bewusst **keine ausgegraute Fassung der vollen**,
sondern eine eigene, schmalere Oberfläche. Eine Ampel, hinter der keine
Vereinbarung steht, erzeugt nur schlechtes Gewissen ohne Nutzen.

Abgeleitet wird das an einer einzigen Stelle: `src/server/capabilities.ts`.

### Teilvorgang abspalten

Der Fall aus dem Alltag: Ein Vorgang kommt in die IT – „neuer Arbeitsplatz
eingerichtet, kein Netzwerk, keine Steckdose". Die Dose zu legen ist Sache des
Hausmeisters, den Rechner einzurichten Sache der IT. Ohne Abspalten bliebe nur,
den Melder um ein zweites Ticket zu bitten oder den ganzen Vorgang wegzugeben.

Beim Abspalten wählt man aus dem bisherigen Verlauf aus, welche Nachrichten
mitkopiert werden – der interne Vermerk über einen Lieferanten hilft dem
Hausmeister nicht und gehört ihm auch nicht vorgelegt. Der Melder bleibt
derselbe und muss nichts erneut tun.

Für den eigenen Vorgang gibt es drei Fortsetzungen, weil es drei Situationen
gibt:

1. **Bleibt offen** – der eigene Anteil läuft parallel weiter.
2. **Zurückstellen, bis der Teilvorgang erledigt ist** – der Vorgang ruht und
   wird automatisch wieder geöffnet, sobald der andere Bereich fertig ist.
3. **Abschließen** – geht uns nichts mehr an.

---

## Was das System sonst noch kann

- **E-Mail in beide Richtungen.** Mails an die Adresse eines Eingangs werden zu
  Vorgängen; Antworten auf Benachrichtigungen landen am selben Vorgang. Wer
  sich nie anmeldet, kann per Antwortmail vollständig arbeiten – `#erledigt`,
  `#intern`, `#warten`, `#übernehmen`, `#an <Bereich>`.
- **Anmeldung** über Microsoft Entra ID (SSO) und/oder lokale Konten.
- **Fristen** rechnen in Dienstzeiten, mit Feiertagen: Wer freitags um 15:50
  meldet, ist nicht um 19:50 überfällig, sondern am Montagvormittag.
- **Volltextsuche** über Vorgänge und Wissensartikel, mit deutscher
  Wortstammerkennung und Toleranz gegen Tippfehler.
- **Barrierefreiheit** nach WCAG 2.1 AA, geprüft mit axe: große Schrift, hoher
  Kontrast, Bewegung reduzieren, einfache Ansicht. Zustände nie nur über Farbe.
- **Datenübernahme** aus dem Altsystem über CSV.
- Wissensdatenbank, Textbausteine, Ticketvorlagen, Geräteverwaltung,
  Auswertung, lückenloses Protokoll.

---

## Einrichten

Voraussetzungen: Node.js 20 oder neuer und PostgreSQL 16. Mit Docker bringt die
`docker-compose.yml` beides samt Mailfänger mit.

```bash
cp .env.example .env          # und ausfüllen, siehe unten
npm install

docker compose up -d postgres # oder ein vorhandener PostgreSQL-Server
npm run db:migrate
npm run db:seed               # Demo-Daten, siehe unten

npm run dev                   # http://localhost:3000
npm run worker                # in einem zweiten Terminal: Mailabruf und Fristen
```

Mindestens auszufüllen in `.env`:

| Schlüssel | Bedeutung |
|---|---|
| `DATABASE_URL` | Verbindung zur Datenbank |
| `APP_URL` | Öffentliche Adresse, für Links in E-Mails |
| `APP_SECRET`, `AUTH_SECRET` | Zufällig, je `openssl rand -base64 48` |
| `MAIL_REPLY_DOMAIN`, `MAIL_REPLY_LOCALPART` | Basis der signierten Antwortadressen |

`APP_SECRET` verschlüsselt außerdem die hinterlegten Postfach-Kennwörter und
signiert die Antwortadressen. **Wird er ausgetauscht, sind beide unbrauchbar**
– die Kennwörter müssen dann neu eingetragen werden, und Antworten auf ältere
Benachrichtigungen finden ihren Vorgang nur noch über Betreff und Kopfzeilen.

Alles Weitere – Entra ID, Postfächer, Catch-all, Dienstzeiten, Sicherung –
steht in **[BETRIEB.md](BETRIEB.md)**.

---

## Demo-Daten

`npm run db:seed` legt sechs Standorte, sechs Bereiche in beiden Ausbaustufen,
elf Eingänge, zwanzig Personen und fünfundzwanzig Vorgänge an – samt einer
Abspaltung, einer Weiterleitung und überfälligen Fristen, damit die Ampel etwas
zu zeigen hat.

Passwort für alle Konten: **`Willkommen-2026-Demo`**

| Konto | Rolle | Zeigt |
|---|---|---|
| `anna.bergmann@example.org` | Betreuerin, Wohnheim Nord, kein Bereich | Das Melder-Portal |
| `claudia.schmitt@example.org` | Melderin, Schule | Große Schrift, einfache Ansicht |
| `frank.berger@example.org` | Hausmeister, Wohnheim Nord | Voller Arbeitsplatz, zwei Standorte freigegeben |
| `dieter.wolf@example.org` | Leitung Hausmeister | Alle Standorte des Bereichs |
| `klaus.hoffmann@example.org` | IT, Verteilung | Zentraler Eingang, darf zuweisen |
| `marion.fischer@example.org` | Küche | Schlanke Ausbaustufe |
| `admin@example.org` | Administration | Verwaltungsbereich |

Der Seed ist wiederholbar und räumt vorher auf. Nach einem Playwright-Lauf sind
die Demo-Daten nicht mehr unberührt – dann einfach neu seeden.

---

## Befehle

```bash
npm run dev          # Entwicklungsserver
npm run build        # Produktionsbau
npm run start        # Produktionsserver
npm run worker       # Mailabruf, Fristenprüfung, Aufräumen

npm run db:migrate   # Migrationen anwenden
npm run db:seed      # Demo-Daten
npm run db:studio    # Datenbank ansehen

npm run typecheck    # tsc --noEmit
npm run lint         # ESLint
npm test             # Vitest
npm run test:e2e     # Playwright inklusive Prüfung mit axe

npm run import -- --typ=benutzer --datei=benutzer.csv --dry-run
npm run sla:check    # Fristenprüfung von Hand anstoßen
```

---

## Tests

**`npm test`** – 159 Tests in zwei Gruppen.

`tests/unit` läuft ohne Datenbank: Rechteprüfung, Ausbaustufen, Aufbereitung
eingehender Mails, Signatur der Antwortadressen, Dienstzeiten-Rechnung.

`tests/db` läuft gegen PostgreSQL, weil sich manches nicht sinnvoll nachbauen
lässt – die Nummernvergabe lebt von einer Zeilensperre, die Zuordnung von
Antwortmails von Unique-Indizes, das Abspalten von einer Transaktion über acht
Tabellen. Einmalig einrichten:

```bash
createdb ticketsystem_test
DATABASE_URL="postgresql://ticket:ticket@localhost:5432/ticketsystem_test" \
  npx prisma migrate deploy
```

Diese Tests **löschen alle Zeilen** und laufen deshalb nur gegen eine
Datenbank, deren Name auf `_test` endet. Ist keine erreichbar, überspringen sie
sich, statt den Lauf abzubrechen.

**`npm run test:e2e`** – 24 Playwright-Tests über den Ablauf aus der Abnahme,
jede Hauptseite zusätzlich mit `@axe-core/playwright` gegen WCAG 2.1 AA
geprüft. Sie laufen gegen die Anwendung mit den Demo-Daten und starten den
Entwicklungsserver selbst.

---

## Aufbau

```
prisma/schema.prisma            Datenmodell; die Kette Bereich → Eingang → Vorgang
prisma/seed.ts                  Demo-Daten

src/app/(app)/                  Angemeldeter Bereich
  eingang/[queueId]/            Sammeleingang (volle Stufe)
  unsere-tickets/[queueId]/     Ticketliste (schlanke Stufe)
  tickets/[nummer]/             Vorgang mit Verlauf
  neu/                          Melde-Formular
  verwaltung/                   15 Seiten Einstellungen

src/server/policy.ts            Die einzige Stelle für Sichtbarkeit und Rechte
src/server/capabilities.ts      Die einzige Stelle für die Ausbaustufen
src/server/services/
  routing.ts                    Bereich + Standort → Eingang
  split.ts                      Abspalten und Zusammenführen
  number.ts                     Ticketnummern, gesperrt vergeben
  sla.ts                        Fristen
src/lib/business-hours.ts       Rechnen mit Dienstzeiten und Feiertagen
src/lib/crypto.ts               Verschlüsselung und signierte Antwortadressen

src/mail/
  types.ts                      Schnittstelle MailSource
  imap.ts, graph.ts             Die beiden Postfach-Anbindungen
  threading.ts                  Antwort → Vorgang, über vier Wege
  commands.ts                   Steuerbefehle samt Rechteprüfung
  parse.ts                      Zitate abschneiden, automatische Antworten

worker/index.ts                 Mailabruf, Fristenprüfung, Aufräumen
scripts/import-csv.ts           Datenübernahme aus dem Altsystem
```

Zwei Dateien tragen die Rechte: **`src/server/policy.ts`** entscheidet, wer was
sehen und tun darf, **`src/server/capabilities.ts`**, was eine Ausbaustufe
kann. Jede Seite und jede Server-Aktion fragt dort an. Dass die Oberfläche
einen Knopf ausblendet, ist Bequemlichkeit – nie der Schutz.

---

## Technik

Next.js 15 (App Router, Server Actions) · React 19 · TypeScript ·
Tailwind CSS v4 · Prisma 6 auf PostgreSQL 16 · Auth.js v5 mit Entra ID und
lokalen Konten (argon2id) · Radix UI · Vitest · Playwright mit axe
