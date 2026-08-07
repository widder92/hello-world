# Testumgebung auf dem eigenen Rechner

Zum Ausprobieren und Vorführen – ohne Server, ohne echte Postfächer, ohne dass
irgendeine E-Mail das Haus verlässt. Alles läuft auf Ihrem PC und ist mit einem
Befehl wieder weg.

> **Das ist keine Installation im Haus.** Für den echten Betrieb siehe
> [BETRIEB.md](BETRIEB.md). Die Testumgebung hat absichtlich fest eingebaute
> Kennwörter und fängt allen Mailverkehr ab; so darf kein Produktivsystem
> laufen.

---

## Der einfache Weg: mit Docker

Am wenigsten Handgriffe und räumt sich restlos wieder ab.

### Einmalig vorbereiten

1. **Docker Desktop installieren** – <https://www.docker.com/products/docker-desktop/>
   (Windows, Mac und Linux; unter Windows fragt es nach WSL 2 und richtet das
   selbst ein). Danach Docker Desktop starten und warten, bis unten links
   „Engine running" steht.

2. **Das Projekt herunterladen.** Der einfachste Weg – dieser Link lädt
   direkt das richtige ZIP:

   <https://github.com/widder92/hello-world/archive/refs/heads/claude/enterprise-ticket-system-4inmb4.zip>

   Entpacken, zum Beispiel nach `C:\ticketsystem`.

   > **Achtung:** Nehmen Sie **nicht** den grünen Knopf *Code → Download ZIP*
   > auf der Startseite des Repositories. Der lädt den Zweig `master`, und
   > darin liegt das Ticketsystem noch nicht – Sie bekämen einen leeren
   > Ordner. Entweder den Link oben nehmen oder auf der GitHub-Seite oben
   > links erst den Zweig `claude/enterprise-ticket-system-4inmb4` auswählen.

   Mit Git geht es auch:

   ```
   git clone -b claude/enterprise-ticket-system-4inmb4 https://github.com/widder92/hello-world.git
   cd hello-world
   ```

3. **Konfigurationsdatei anlegen.** Im Projektordner:

   *Windows (PowerShell):*
   ```powershell
   Copy-Item .env.example .env
   ```

   *Mac/Linux:*
   ```bash
   cp .env.example .env
   ```

   Für die Testumgebung müssen Sie darin **nichts** ändern. Die
   voreingestellten Werte reichen zum Ausprobieren.

### Starten

```
docker compose up -d
```

Der erste Start dauert ein paar Minuten – Docker baut das Abbild und lädt die
Datenbank herunter. Danach geht es in Sekunden.

Was dabei passiert: Die Datenbank wird angelegt, das Datenmodell eingerichtet
und die Demo-Daten eingefüllt – sechs Standorte, sechs Bereiche, zwanzig
Personen, fünfundzwanzig Vorgänge.

### Aufrufen

| Adresse | Was dort ist |
|---|---|
| <http://localhost:3000> | Das Ticketsystem |
| <http://localhost:8025> | Alle E-Mails, die das System verschickt hat |

Die zweite Adresse ist für eine Vorführung Gold wert: Dort sehen Sie jede
Benachrichtigung, die das System erzeugt hat, ohne dass sie jemanden erreicht.

### Anhalten und wieder starten

```
docker compose stop     # anhalten, Daten bleiben
docker compose start    # weitermachen
```

Ihre beim Ausprobieren angelegten Vorgänge bleiben erhalten – auch über einen
Neustart des Rechners hinweg.

### Restlos entfernen

```
docker compose down -v
```

Löscht auch die Datenbank. Beim nächsten `docker compose up -d` fangen die
Demo-Daten wieder von vorne an.

---

## Der andere Weg: ohne Docker

Wenn Sie Node.js und PostgreSQL ohnehin auf dem Rechner haben – oder Docker
Desktop nicht installieren dürfen.

**Voraussetzungen:** Node.js 20 oder neuer (<https://nodejs.org>, der
LTS-Knopf) und PostgreSQL 16 oder neuer
(<https://www.postgresql.org/download/>). Die Download-Seite bietet
inzwischen Version 18 an – die passt ebenso.

### PostgreSQL installieren (Windows)

Beim Installationsprogramm können Sie alles so lassen, wie es vorgeschlagen
wird. Zwei Stellen sind wichtig:

- **Password** – ein Kennwort für den Benutzer `postgres` vergeben und
  **merken**. Es wird gleich noch gebraucht.
- **Advanced Options → Locale** – auf **`DEFAULT`** stehen lassen und weiter.
  Die Auswahl bestimmt nur die Sortierreihenfolge; für das Ticketsystem
  ist sie ohne Belang.

Den *Stack Builder* am Ende können Sie abwählen.

### Datenbank anlegen

Aus dem Startmenü **SQL Shell (psql)** öffnen. Fünfmal Enter für die
Vorgaben, dann das eben vergebene Kennwort eingeben. Dort diese eine Zeile
eintippen (mit Semikolon am Ende):

```sql
CREATE DATABASE ticketsystem WITH ENCODING 'UTF8' TEMPLATE template0;
```

> **Warum so umständlich und nicht einfach `CREATE DATABASE ticketsystem`?**
> Unter Windows richtet PostgreSQL eine neue Datenbank je nach Locale gern in
> einem westeuropäischen Zeichensatz statt in UTF-8 ein. Umlaute überleben das
> noch, Gedankenstriche und typografische Anführungszeichen nicht – und davon
> ist die Oberfläche voll. Der Einbau würde dann mitten im Lauf abbrechen.
> Mit `ENCODING 'UTF8' TEMPLATE template0` ist die Frage ein für alle Mal vom
> Tisch.

Mit `\q` und Enter wieder heraus.

### Einrichten und starten

Im entpackten Projektordner ein Fenster öffnen (im Explorer oben in die
Adresszeile `cmd` tippen, Enter) und der Reihe nach:

```
copy .env.example .env
```

Dann die Datei `.env` mit dem Editor öffnen und die Zeile `DATABASE_URL` auf
Ihr Kennwort anpassen:

```
DATABASE_URL="postgresql://postgres:IHRKENNWORT@localhost:5432/ticketsystem?schema=public"
```

Speichern, schließen, und zurück im Fenster:

```
npm install
npm run demo
```

`npm run demo` richtet die Datenbank ein, füllt die Demo-Daten, baut die
Anwendung und startet sie unter <http://localhost:3000>. Der erste Bau dauert
ein bis zwei Minuten.

Zum späteren Weiterstarten reicht `npm run start` – der Bau bleibt erhalten.
Beim Entwickeln ist `npm run dev` bequemer (übernimmt Änderungen sofort, läuft
aber langsamer).

E-Mails werden auf diesem Weg nur ins Protokoll geschrieben, nicht verschickt –
außer Sie tragen in der `.env` einen SMTP-Server ein.

---

## Anmelden

Passwort für **alle** Demo-Konten: `Willkommen-2026-Demo`

| Konto | Wer das ist | Was Sie sehen |
|---|---|---|
| `anna.bergmann@example.org` | Betreuerin im Wohnheim Nord, kein Bereich | Das Melder-Portal – so sieht es für die allermeisten Mitarbeitenden aus |
| `claudia.schmitt@example.org` | Melderin an der Schule | Dieselbe Oberfläche in großer Schrift und einfacher Ansicht |
| `frank.berger@example.org` | Hausmeister, Wohnheim Nord | Voller Arbeitsplatz, freigegeben für zwei Standorte |
| `dieter.wolf@example.org` | Leitung Hausmeister | Alle Standorte des Bereichs |
| `klaus.hoffmann@example.org` | IT, darf zuteilen | Zentraler Eingang der IT |
| `marion.fischer@example.org` | Küche | Die schlanke Ausbaustufe |
| `admin@example.org` | Administration | Den Verwaltungsbereich |

---

## Ein Vorschlag für die Vorführung

Zwölf Minuten, und die drei Punkte, auf die es ankommt, sind gezeigt.

**1. Die Meldung geht direkt an den richtigen Bereich** (3 Min.)

Als `anna.bergmann@example.org` anmelden → *Neue Meldung* → Kachel
**Hausmeister** → Betreff und Text eintragen, gern ein Foto anhängen.

Hier auf den Standort zeigen: Er steht bereits auf *Wohnheim Nord*, weil das
Anna Bergmanns Haus ist. Absenden.

> Der Satz dazu: „Sie hat den Hausmeister gewählt, nicht die IT. Und nicht
> irgendeinen Hausmeister, sondern den von ihrem Haus."

**2. Der Hausmeister zieht sich das Ticket** (2 Min.)

Abmelden, als `frank.berger@example.org` anmelden. Die Meldung liegt im
Sammeleingang *Wohnheim Nord*. Öffnen → **Ticket ziehen**.

Dann links auf die Navigation zeigen: Er sieht *Wohnheim Nord* und
*Verwaltung* – die Schule nicht, dafür ist er nicht freigegeben.

**3. Einen Teil abspalten** (4 Min.)

Als `klaus.hoffmann@example.org` anmelden, Vorgang `IT-2026-000001` öffnen
(„Neuer Arbeitsplatz Dienstzimmer: kein Netzwerk und keine Steckdose").
**Teil abspalten** → Hausmeister → Standort → beschreiben, was der Hausmeister
tun soll → aus dem Verlauf auswählen, welche Nachrichten er mitbekommt.

Unten die drei Möglichkeiten zeigen und **„Zurückstellen, bis der Teilvorgang
erledigt ist"** wählen.

> Der Satz dazu: „Die Dose legt der Hausmeister, den Rechner richten wir ein.
> Der Melder muss nichts erneut schreiben. Und wenn der Hausmeister fertig ist,
> macht sich unser Vorgang von selbst wieder auf."

**4. Nicht jeder Bereich braucht alles** (2 Min.)

Als `marion.fischer@example.org` anmelden: kein Sammeleingang, keine Ampel,
keine Priorität – nur die Vorgänge der Küche, offen und erledigt.

Dann als `admin@example.org` → *Verwaltung → Bereiche*: Dort steht je Bereich,
welche Ausbaustufe er hat, und das lässt sich umschalten.

**5. E-Mail** (1 Min.)

<http://localhost:8025> aufrufen (nur beim Docker-Weg): Dort liegen die
Benachrichtigungen, die das System während der Vorführung verschickt hat.

---

## Bildschirmfotos statt Vorführung

Wenn Sie es zeigen müssen, ohne es laufen zu lassen – in einer Besprechung, in
einer Vorlage, per Mail:

```
npm run bilder
```

Legt dreizehn Bilder der Hauptansichten in den Ordner `bildschirmfotos/`.
Die Anwendung muss dabei laufen.

---

## Wenn etwas klemmt

**„Port is already allocated" / „Address already in use"** – auf Ihrem Rechner
belegt schon etwas den Port 3000 oder 5432. Das andere Programm beenden, oder
in der `docker-compose.yml` die linke Zahl ändern, z. B. `"127.0.0.1:3100:3000"`,
und dann <http://localhost:3100> aufrufen.

**Die Seite lädt nicht, obwohl Docker läuft** – der erste Start braucht länger.
Nachsehen mit:
```
docker compose logs -f app
```

**„Cannot connect to the Docker daemon"** – Docker Desktop ist nicht gestartet.

### Docker Desktop startet gar nicht erst

Unter Windows fast immer eine von fünf Ursachen. In dieser Reihenfolge prüfen:

**1. Virtualisierung ist im BIOS abgeschaltet.** Die häufigste Ursache.
Task-Manager öffnen (Strg+Umschalt+Esc) → *Leistung* → *CPU*. Rechts unten
steht *Virtualisierung*. Steht dort **Deaktiviert**, muss sie im BIOS/UEFI
eingeschaltet werden – bei Intel heißt sie *VT-x* oder *Intel Virtualization
Technology*, bei AMD *SVM Mode*.

**2. WSL 2 fehlt oder ist veraltet.** In der PowerShell **als Administrator**:

```powershell
wsl --install
wsl --update
wsl --status
```

Danach den Rechner neu starten. Das ist kein optionaler Schritt – ohne
Neustart bleibt es beim selben Fehler.

**3. Windows-Features fehlen.** *Windows-Features aktivieren oder deaktivieren*
öffnen. Diese beiden müssen angehakt sein:

- *Windows-Subsystem für Linux*
- *Plattform für virtuelle Computer* (englisch: *Virtual Machine Platform*)

Danach neu starten.

**4. Sie haben keine Administratorrechte.** Docker Desktop braucht sie zur
Installation und richtet einen Systemdienst ein. Auf einem verwalteten
Firmenrechner ist das oft gesperrt – dann hilft nur die eigene IT, oder Sie
nehmen den Weg ohne Docker (siehe unten).

**5. Ein anderer Hypervisor ist im Weg.** VirtualBox oder VMware Workstation
in älteren Fassungen vertragen sich nicht mit der Windows-Hypervisor-Plattform.
Beenden und noch einmal versuchen.

> **Kommt Docker nicht zum Laufen, ist das kein Beinbruch.** Der Weg ohne
> Docker weiter unten führt zum selben Ergebnis und braucht keine
> Virtualisierung – nur zwei gewöhnliche Installationsprogramme.

**Anmeldung schlägt fehl** – Passwort ist `Willkommen-2026-Demo`, mit großem W
und dem Bindestrich. Wenn keines der Konten geht, sind vermutlich die
Demo-Daten nicht eingespielt worden:
```
docker compose logs vorbereiten
```

**Alles auf Anfang:**
```
docker compose down -v
docker compose up -d
```

---

## Was diese Testumgebung nicht kann

Damit Sie beim Vorführen nicht in eine Frage laufen, auf die es hier keine
Antwort gibt:

- **Keine Anmeldung über Microsoft.** Dafür braucht es eine App-Registrierung
  in Ihrem Mandanten (siehe BETRIEB.md). In der Testumgebung sind es lokale
  Konten mit Kennwort.
- **Kein Mailempfang.** Das System kann eingehende Mails zu Vorgängen machen –
  dafür braucht es echte Postfächer. Der Weg hinaus ist zu sehen (unter
  Port 8025), der Weg hinein nicht.
- **Die Demo-Daten sind erfunden.** Standorte, Bereiche und Namen sind
  Platzhalter. Wenn Sie mit Ihren echten Bezeichnungen vorführen möchten, sagen
  Sie Bescheid – die Demo-Daten sind eine einzelne Datei
  (`prisma/seed.ts`) und schnell angepasst.
