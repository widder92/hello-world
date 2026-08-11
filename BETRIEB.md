# Betrieb

Alles, was zwischen „läuft auf dem Entwicklungsrechner" und „läuft im Haus"
liegt. Für die Einrichtung und den Aufbau des Systems siehe
[README.md](README.md).

---

## Inhalt

1. [Vor dem ersten Start](#1-vor-dem-ersten-start)
2. [Anmeldung mit Microsoft Entra ID](#2-anmeldung-mit-microsoft-entra-id)
3. [Postfächer je Eingang](#3-postfächer-je-eingang)
4. [Antwortadressen und Catch-all](#4-antwortadressen-und-catch-all) ← **der Punkt, der am ehesten übersehen wird**
5. [Steuerbefehle in Antwortmails](#5-steuerbefehle-in-antwortmails)
6. [Dienstzeiten, Feiertage und Fristen](#6-dienstzeiten-feiertage-und-fristen)
7. [Bereiche und Eingänge einrichten](#7-bereiche-und-eingänge-einrichten)
8. [Datenübernahme aus dem Altsystem](#8-datenübernahme-aus-dem-altsystem)
9. [Das IT-Inventar](#9-das-it-inventar)
10. [Der Hintergrunddienst](#10-der-hintergrunddienst)
11. [Sicherung und Wiederherstellung](#11-sicherung-und-wiederherstellung)
12. [Löschfristen und Datenschutz](#12-löschfristen-und-datenschutz)
13. [Wenn etwas klemmt](#13-wenn-etwas-klemmt)

---

## 1. Vor dem ersten Start

### Geheimnisse erzeugen

```bash
openssl rand -base64 48   # für APP_SECRET
openssl rand -base64 48   # für AUTH_SECRET
```

`APP_SECRET` tut drei Dinge gleichzeitig:

- er verschlüsselt die hinterlegten Postfach-Kennwörter (AES-256-GCM),
- er signiert die Antwortadressen (HMAC-SHA256),
- er sichert die Sitzungen ab.

**Wird er ausgetauscht, geht beides Erste verloren.** Die Postfach-Kennwörter
lassen sich dann nicht mehr entschlüsseln und müssen in der Verwaltung neu
eingetragen werden; Antworten auf ältere Benachrichtigungen finden ihren
Vorgang nur noch über Betreff und Kopfzeilen, und Steuerbefehle darin werden
nicht mehr ausgeführt. Der Schlüssel gehört ins Passwortverwaltungsprogramm,
nicht nur in die `.env`.

### Erstes Konto

Der Seed legt `admin@example.org` an. Für den echten Betrieb: eigenes Konto
anlegen (oder per SSO anmelden), in der Datenbank auf `globalRole = 'ADMIN'`
setzen, danach das Demo-Konto löschen.

```sql
UPDATE "User" SET "globalRole" = 'ADMIN' WHERE email = 'ihr.name@ihre-domain.de';
```

### Checkliste

- [ ] `APP_SECRET` und `AUTH_SECRET` erzeugt und sicher hinterlegt
- [ ] `APP_URL` auf die tatsächliche Adresse gesetzt (steht in jeder Mail)
- [ ] `TZ=Europe/Berlin`
- [ ] `UPLOAD_DIR` zeigt auf ein gesichertes Verzeichnis, nicht ins Projekt
- [ ] Datenbank gesichert (siehe [10.](#10-sicherung-und-wiederherstellung))
- [ ] HTTPS davor (Reverse Proxy); Anmeldedaten gehören nicht über HTTP
- [ ] Demo-Konten gelöscht

---

## 2. Anmeldung mit Microsoft Entra ID

Zu erledigen von Ihrem Microsoft-365-Administrator.

### App-Registrierung

1. **Entra Admin Center → Identität → Anwendungen → App-Registrierungen → Neue Registrierung**
2. Name: *Ticketsystem*
3. Kontotypen: **Nur Konten in diesem Organisationsverzeichnis**
4. Umleitungs-URI, Typ **Web**:
   `https://ticket.ihre-domain.de/api/auth/callback/microsoft-entra-id`
   (für die Entwicklung zusätzlich `http://localhost:3000/api/auth/callback/microsoft-entra-id`)
5. **Zertifikate & Geheimnisse → Neuer geheimer Clientschlüssel**, Laufzeit
   notieren – **er läuft ab, und dann meldet sich niemand mehr an.** Termin in
   den Kalender.
6. **API-Berechtigungen**: `openid`, `profile`, `email`, `User.Read`
   (delegiert). Mehr braucht die Anmeldung nicht.

### In die `.env`

```dotenv
AUTH_MICROSOFT_ENTRA_ID_ID="<Anwendungs-ID (Client)>"
AUTH_MICROSOFT_ENTRA_ID_SECRET="<geheimer Clientschlüssel>"
AUTH_MICROSOFT_ENTRA_ID_ISSUER="https://login.microsoftonline.com/<Verzeichnis-ID>/v2.0"
```

Bleiben diese Werte leer, blendet die Anmeldeseite die SSO-Schaltfläche
automatisch aus.

### Lokale Konten

`LOCAL_LOGIN_ENABLED="true"` lässt zusätzlich Konten mit Kennwort zu (argon2id).
Gedacht für Personen ohne M365-Konto – Hausmeister, Küche, Fahrdienst haben
erfahrungsgemäß nicht alle eines – und als Rückfalltür, wenn Entra ID einmal
nicht erreichbar ist. **Mindestens ein lokales Administrationskonto behalten**,
sonst kommt bei einer Störung im Verzeichnisdienst niemand mehr in die
Verwaltung.

Auf `"false"` setzen, wenn ausschließlich SSO erlaubt sein soll.

### Erstanmeldung: das Konto muss vorher da sein

**Über SSO entsteht kein neues Konto.** Die Anmeldung ordnet die
E-Mail-Adresse aus dem Token einem bereits vorhandenen Konto zu; gibt es keines
oder ist es stillgelegt, wird die Anmeldung abgewiesen.

Das ist Absicht: Sonst könnte sich jede Person im Mandanten selbst Zugang
verschaffen – und in einem Mandanten mit knapp tausend Konten ist das ein
Unterschied.

Für die Einführung heißt das: **erst die Belegschaft anlegen, dann SSO
freigeben.** Am einfachsten über den CSV-Import (siehe
[8.](#8-datenübernahme-aus-dem-altsystem)) mit einem Export aus dem
Personalverzeichnis; einzelne Konten gehen unter **Verwaltung → Benutzer**.

Beim ersten SSO-Login wird die Entra-Objekt-ID am Konto festgehalten, damit
eine spätere Namens- oder Adressänderung die Zuordnung nicht zerreißt.

---

## 3. Postfächer je Eingang

Jeder Eingang kann ein eigenes Postfach haben. Mail an
`hausmeister-nord@ihre-domain.de` wird zu einem Vorgang im Eingang *Hausmeister
Wohnheim Nord* – ohne Umweg über die IT.

Einzurichten unter **Verwaltung → Postfächer**. Zwei Wege, gleichwertig:

### IMAP

Funktioniert mit jedem Mailserver.

| Feld | Beispiel |
|---|---|
| Empfangsadresse | `hausmeister-nord@ihre-domain.de` |
| Server | `imap.ihre-domain.de` |
| Port | `993` |
| Verschlüsselt | ja |
| Benutzer | meist die Adresse |
| Kennwort | wird verschlüsselt abgelegt, nie im Klartext |
| Ordner | `INBOX` |
| Ordner für Verarbeitetes | z. B. `Verarbeitet` – leer = nur als gelesen markieren |

Bei Microsoft 365 mit IMAP: Basisauthentifizierung ist abgeschaltet. Entweder
OAuth am Postfach einrichten oder gleich Graph nehmen.

### Microsoft Graph

Für M365 der bequemere Weg: keine SMTP-Freischaltung, keine Postfachkennwörter.

1. Eigene App-Registrierung (getrennt von der Anmeldung, andere Rechte)
2. **API-Berechtigungen → Microsoft Graph → Anwendungsberechtigungen**:
   `Mail.ReadWrite` und `Mail.Send`
3. **Administratorzustimmung erteilen** – ohne sie passiert nichts
4. Geheimen Clientschlüssel erzeugen

```dotenv
GRAPH_TENANT_ID="<Verzeichnis-ID>"
GRAPH_CLIENT_ID="<Anwendungs-ID>"
GRAPH_CLIENT_SECRET="<geheimer Clientschlüssel>"
```

Im Eingang dann `graphUserId` eintragen (die Objekt-ID oder der
Benutzerprinzipalname des Postfachs).

> **Anwendungsberechtigungen gelten für alle Postfächer im Mandanten.** Wer sie
> erteilt, gibt der Anwendung Lese- und Schreibzugriff auf jedes Postfach im
> Haus. Ihr M365-Administrator sollte das über eine
> **Anwendungszugriffsrichtlinie** (`New-ApplicationAccessPolicy`) auf die
> Ticket-Postfächer einschränken. Das ist kein Beiwerk, sondern der
> Unterschied zwischen „darf drei Funktionspostfächer lesen" und „darf die
> Personalabteilung mitlesen".

### Verbindung prüfen

In der Verwaltung gibt es je Postfach **„Verbindung testen"**. Ergebnis, letzter
Abruf und letzter Fehler stehen direkt daneben. Erst wenn das grün ist, hat es
Sinn, eine Testmail zu schicken.

### Unbekannte Absender

Je Postfach einstellbar:

- **Kontakt anlegen** (Voreinstellung) – ein externer Melder ohne Konto wird
  angelegt und kann per Mail antworten, sich aber nicht anmelden.
- **Zur Sichtung** – der Vorgang entsteht, wird aber markiert.
- **Verwerfen** – für Postfächer, die nur intern erreichbar sein sollen.

---

## 4. Antwortadressen und Catch-all

**Der Punkt, an dem eine Einrichtung am häufigsten hängen bleibt.** Ohne ihn
funktioniert die Bearbeitung per Antwortmail nicht, und niemand sieht sofort,
woran es liegt.

### Was passiert

Jede Benachrichtigung trägt eine eigene Absenderadresse:

```
ticket+dGlja2V0LTEyMw.9f3a1c8e7b2d4f6a@ihre-domain.de
└──┬──┘ └──────────────┬──────────────┘
 fest        Vorgang + Empfänger, HMAC-signiert
```

Antwortet die Empfängerin, kommt die Mail an genau diese Adresse zurück. Damit
weiß das System **ohne jeden Zweifel**, zu welchem Vorgang die Antwort gehört –
und zusätzlich, dass die Absenderin diese Mail tatsächlich bekommen hat.
Betreffzeilen werden von Mailprogrammen abgeschnitten, Kopfzeilen von
Verteilern umgeschrieben; diese Adresse überlebt beides.

### Was Ihr Mailserver dafür können muss

Alle Adressen der Form `ticket+<irgendetwas>@ihre-domain.de` müssen im
Systempostfach ankommen. Zwei Wege:

**Plus-Adressierung** – Exchange Online, Postfix, Google Workspace und die
meisten anderen liefern `name+zusatz@domain` an `name@domain` aus.

- *Exchange Online:* standardmäßig aktiv. Prüfen mit
  `Get-OrganizationConfig | Select DisablePlusAddressInRecipients` – der Wert
  muss `False` sein.
- *Postfix:* `recipient_delimiter = +` in der `main.cf`.

**Catch-all** – eine Weiterleitungsregel `ticket*@ihre-domain.de` →
Systempostfach. Nötig, wenn Plus-Adressierung nicht in Frage kommt.

### Einstellen

```dotenv
MAIL_REPLY_DOMAIN="ihre-domain.de"
MAIL_REPLY_LOCALPART="ticket"
```

### Prüfen

1. Vorgang anlegen, Benachrichtigung abwarten
2. Im Mailprogramm auf **Antworten** – im An-Feld muss `ticket+…@…` stehen
3. Antworten und einen Moment warten (der Abruf läuft alle zwei Minuten)
4. Der Text muss **am selben Vorgang** stehen, nicht als neuer Vorgang

Steht er als neuer Vorgang da, kommen die Plus-Adressen nicht an. Dann greifen
nur noch die Ersatzwege (Kopfzeile, Ticketnummer im Betreff) – und
Steuerbefehle bleiben dauerhaft wirkungslos, denn ohne gültiges Token werden
sie grundsätzlich nicht ausgeführt.

---

## 5. Steuerbefehle in Antwortmails

Damit auch arbeiten kann, wer sich nie anmeldet: Auf die Benachrichtigung
antworten, in die erste Zeile einen Befehl schreiben, fertig.

| Befehl | Wirkung |
|---|---|
| `#erledigt` | Vorgang auf *gelöst* (auch `#fertig`, `#gelöst`, `#done`) |
| `#intern` | Der Text wird interne Notiz und erreicht den Melder nicht |
| `#warten` | Wartet auf Rückmeldung des Melders |
| `#übernehmen` | Weist den Vorgang der antwortenden Person zu |
| `#an Hausmeister` | Leitet an den genannten Bereich weiter |

Befehle stehen **als eigene Zeile am Anfang** und beginnen mit `#`. Nur die
ersten Zeilen werden geprüft – sonst löste ein `#erledigt` in einem zitierten
Text ungewollt aus.

### Die Absicherung

Ein `From`-Feld ist keine Identitätsprüfung; es lässt sich mit drei Zeilen
beliebig setzen. Für einen Befehl müssen deshalb **alle drei** Bedingungen
gleichzeitig erfüllt sein:

1. Die Mail kam an eine Antwortadresse mit **gültiger Signatur**. Sie steht nur
   in der Mail, die diese Person bekommen hat, und lässt sich nicht erraten.
2. Die **Absenderadresse stimmt** mit der überein, an die das Token ausgestellt
   wurde. Ein abgefangenes Token nützt mit eigener Adresse nichts.
3. Die Person **arbeitet im Bereich**, in dem der Vorgang liegt. Ein Melder
   kann seinen eigenen Vorgang so nicht weiterleiten oder zuweisen.

Fehlt eines davon, wird der Text als gewöhnliche Nachricht abgelegt, der Befehl
verworfen und der Vorfall als `EMAIL_COMMAND_REJECTED` protokolliert. Nachlesen
unter **Verwaltung → Protokoll**.

Je Bereich abschaltbar (**Verwaltung → Bereiche → Steuerbefehle**).

---

## 6. Dienstzeiten, Feiertage und Fristen

**Verwaltung → Dienstzeiten**

Fristen laufen nur, während jemand da ist. Wer freitags um 15:50 meldet und
eine Frist von vier Stunden hat, ist nicht um 19:50 überfällig, sondern am
Montagvormittag. Ohne diese Rechnung stünde montagfrüh alles auf Rot, und die
Ampel wäre wertlos.

Der Wochenplan wird je Zeitfenster gepflegt; geteilte Dienste (Vormittag,
Pause, Nachmittag) sind vorgesehen. Mehrere Pläne sind möglich – der Fahrdienst
fängt früher an als die Verwaltung.

### Feiertage

Von Hand einzutragen, je Plan. **Einmal im Jahr nachpflegen**, am besten im
Dezember für das Folgejahr. Zu bedenken:

- die gesetzlichen Feiertage Ihres Bundeslandes,
- Schließtage der Einrichtung zwischen den Jahren,
- bewegliche Feiertage (Ostern, Pfingsten, Fronleichnam).

Fehlt ein Feiertag, laufen an diesem Tag Fristen weiter, und die Ampel steht am
nächsten Arbeitstag ohne Grund auf Rot.

### Fristen je Bereich

**Verwaltung → Fristen**: je Bereich und Priorität eine Reaktions- und eine
Lösungszeit, dazu die Schwelle für die Vorwarnung (0.8 = bei 80 % der Frist).

**Nur Bereiche der vollen Ausbaustufe haben Fristen.** Für die schlanken wird
bewusst keine gesetzt.

---

## 7. Bereiche und Eingänge einrichten

### Ausbaustufe

**Verwaltung → Bereiche**, umschaltbar zwischen *Voller Arbeitsplatz* und
*Schlanke Ansicht*. Die Umstellung wirkt sofort und lässt bestehende Vorgänge
unangetastet – ein Vorgang, der aus einem vollen Bereich weitergeleitet wurde,
behält seinen Status, auch wenn die schlanke Ansicht ihn nicht anbietet.

Faustregel: **voll**, wenn der Bereich Vorgänge untereinander verteilt und
Fristen einhalten muss (IT, Hausmeister). **Schlank**, wenn er sie nur
entgegennimmt, beantwortet und abschließt (Küche, Fahrdienst, Personal).

### Struktur der Eingänge

| | Wann | Beispiel |
|---|---|---|
| **Ein gemeinsamer Eingang** | Der Bereich arbeitet standortübergreifend | IT |
| **Je Standort ein Eingang** | Zuständigkeit hängt am Haus | Hausmeister |

Beim Umstellen legt das System die passenden Eingänge an. **Bestehende werden
nie gelöscht, nur stillgelegt** – sonst gingen die Vorgänge darin verloren.
Wer versehentlich umstellt und zurückstellt, bekommt seine Eingänge samt
Freigaben zurück.

### Wer arbeitet wo

**Verwaltung → Benutzer**: Mitgliedschaft im Bereich *und* Freigabe für die
Eingänge. Beides zusammen ergibt erst den Zugriff.

- Wer zwei Häuser betreut, bekommt zwei Freigaben.
- Eine davon lässt sich als Startansicht setzen.
- **Bereichsleitungen sehen alle Eingänge ihres Bereichs** auch ohne
  ausdrückliche Freigabe – sonst müsste bei jedem neuen Standort daran gedacht
  werden.

### Neuer Standort

1. **Verwaltung → Standorte** anlegen
2. Bei Bereichen mit Eingang je Standort entsteht der Eingang automatisch
3. Empfangsadresse und Postfach eintragen
4. Zuständige freigeben – **bis dahin kann dorthin niemand melden**, und die
   Meldung sagt das auch so

---

## 8. Datenübernahme aus dem Altsystem

Über CSV, weil man damit aus praktisch jedem Vorsystem herauskommt – OTRS,
osTicket, Jira, eine SharePoint-Liste oder eine gewachsene Excel-Tabelle.

```bash
npm run import -- --typ=standorte --datei=standorte.csv --dry-run
npm run import -- --typ=standorte --datei=standorte.csv
```

**Reihenfolge einhalten**, weil die späteren auf die früheren verweisen:

```
standorte → benutzer → geraete → tickets
```

**Immer zuerst mit `--dry-run`.** Der Trockenlauf schreibt nichts, prüft aber
jede Zeile und meldet mit Zeilennummer, was schiefginge.

### Format

Trennzeichen `;` (mit `--trennzeichen=,` umstellbar), Kopfzeile erforderlich,
UTF-8. Spaltennamen werden unabhängig von Groß-/Kleinschreibung und
Umlautschreibung erkannt; ein BOM aus Excel stört nicht.

**Standorte** – `name` (Pflicht), `kurzname`, `art`, `strasse`, `plz`, `ort`

`art`: `schule`, `wohnheim`, `betreuteswohnen`, `verwaltung`, `werkstatt`,
sonst *Sonstige*.

**Benutzer** – `email` und `name` (Pflicht), `standort`, `funktion`,
`abteilung`, `telefon`

`standort` muss auf einen bereits angelegten Standortnamen passen.

**Geräte** – `nummer` und `name` (Pflicht), `hersteller`, `modell`,
`seriennummer`, `standort`, `raum`, `zugewiesen` (E-Mail-Adresse)

**Tickets** – `betreff` und `bereich` (Pflicht), `beschreibung`, `standort`,
`melder`, `status`, `erstellt`, `erledigt`, `nummer`

- `bereich` muss auf ein Kürzel oder einen Bereichsnamen passen.
- `status`: `neu`, `offen`, `inbearbeitung`, `wartet`, `gelöst`, `erledigt`,
  `geschlossen`. Unbekanntes wird *geschlossen* – Alttickets sind das in aller
  Regel.
- `melder` unbekannt? Dann wird der Vorgang einem Sammelkonto
  (`import@example.invalid`) zugeordnet, statt die Zeile zu verwerfen.
- Die alte Nummer wird im Vorgang vermerkt, damit Rückfragen zuzuordnen
  bleiben; die neue Nummer vergibt das System.

### Nach dem Import

- Stichproben prüfen: Landen die Vorgänge im richtigen Bereich?
- Ticketzähler prüfen – der Import setzt sie hoch, damit neue Vorgänge nicht
  mit bestehenden Nummern kollidieren.
- Das Sammelkonto durchsehen und, wo möglich, richtige Melder zuordnen.

---

## 9. Das IT-Inventar

Geräte, die an Mitarbeitende ausgegeben werden: Notebooks, Telefone, Tablets,
Monitore – und alles andere, was Sie erfassen wollen, bis hin zu Headsets ab
einem Wert, den Sie selbst festlegen.

### Einschalten

Das Inventar hängt **nicht** an der Ausbaustufe VOLL/EINFACH, sondern ist ein
eigener Schalter je Bereich:

> *Verwaltung → Bereiche → Bereich öffnen → «Führt ein Inventar»*

Im Auslieferungszustand führt nur die IT eines. Ein zweiter Bereich – etwa der
Hausmeister für Werkzeug und Leitern – bekommt damit sein eigenes, getrenntes
Inventar. Geräte wandern nicht zwischen Bereichen: Wer in Bereich A arbeitet,
sieht die Geräte von B nicht und kann auch keine dorthin einbuchen.

### Wer darf was

| | Geräte sehen und pflegen | Gerätearten, Hersteller, Kostenstellen |
|---|:---:|:---:|
| Mitglied eines Inventarbereichs | ✓ | – |
| Leitung eines Inventarbereichs | ✓ | ✓ |
| Administration | ✓ | ✓ |
| Alle anderen | nur die eigenen unter *Meine Geräte* | – |

Die Stammdatenlisten bleiben bewusst bei der Leitung: Sie wirken auf alle
Geräte gleichzeitig, und eine stillgelegte Kostenstelle verändert rückwirkend
jede Auswertung.

### Bevor Sie Geräte erfassen

Legen Sie zuerst die **Kostenstellen** an (*Inventar → Stammdaten*), und zwar
genau so, wie sie in Ihrer Buchhaltung heißen. Sie sind die Achse der
Auswertung, und der CSV-Import lehnt unbekannte Kostenstellen ausdrücklich ab,
statt sie stillschweigend anzulegen – eine aus einem Tippfehler entstandene
Kostenstelle würde die Summen aufteilen, ohne dass es jemandem auffiele.

Danach die **Gerätearten**. Jede Art bestimmt zweierlei: womit die
Inventarnummer beginnt (`NB` → `NB-00042`) und welche Angaben das Formular
verlangt. Das Kürzel lässt sich später **nicht mehr ändern** – es steht dann
bereits auf den Aufklebern.

**Hersteller und Modelle** müssen Sie *nicht* vorbereiten. Sie entstehen beim
Erfassen: Im Geräteformular steht in beiden Auswahlfeldern ganz unten
*+ Neuer Hersteller …* bzw. *+ Neues Modell …*. Das darf jede Person, die
Geräte erfasst – wer ein neu geliefertes Notebook einbuchen will, soll nicht
auf die Bereichsleitung warten müssen. Gerätearten und Kostenstellen bleiben
dagegen der Leitung vorbehalten, weil sie Nummernkreise und Summen berühren.

### Modelle und Bilder

Ein **Modell** ist die Bauart: „Lenovo ThinkPad L14". Dasselbe Modell wird
zwanzigmal beschafft und unterscheidet sich nur in Seriennummer und Besitzer.
Zwei Dinge folgen daraus:

- Beim Erfassen wird das Modell **ausgewählt statt getippt**. Ein Freitextfeld
  enthält nach dem dritten Mal „Thinkpad L 14" und „T14L", und keine Auswertung
  fasst das je wieder zusammen.
- Das **Bild hängt am Modell**, nicht am einzelnen Gerät. Einmal aufgenommen,
  erbt es jedes weitere Gerät derselben Bauart – beim Anlegen erscheint dann
  nur noch der Hinweis *Bild vom Modell wird übernommen*, und Sie müssen nichts
  tun.

Ein Bild direkt am Gerät gibt es weiterhin, aber nur für den Ausnahmefall: ein
Schaden, ein Umbau, ein Aufkleber, den man wiedererkennen will. Es hat dann
Vorrang vor dem Bild des Modells.

### Status und Übergabeprotokoll

| Status | Bedeutung |
|---|---|
| Im Lager | da, aber noch nicht eingerichtet – der Zustand nach dem Auspacken |
| In Vorbereitung | wird gerade eingerichtet: System, Programme, Konten |
| Bereit zur Übergabe | fertig eingerichtet, wartet nur noch auf die Abholung |
| Vorgemerkt | für eine bestimmte Person reserviert |
| Herausgegeben | bei einer Person im Einsatz |
| In Reparatur | beim Hersteller oder in der Werkstatt |
| Ausgesondert | ausgemustert, verkauft, entsorgt |
| Verlust | verloren oder gestohlen |

Der Regelweg eines neuen Geräts lautet **Lager → In Vorbereitung → Bereit →
Herausgegeben**, und jeder Schritt lässt sich zurückgehen. Zubehör, das keine
Einrichtung braucht – ein Monitor, eine Tastatur –, geht direkt vom Lager auf
*Bereit*. Beim Personalwechsel führt *Zurücknehmen und einrichten* in einem
Schritt zurück in die Vorbereitung; das Rücknahmeprotokoll entsteht dabei
trotzdem.

*Bereit zur Übergabe* ist der einzige Status mit grüner Plakette. Ein Blick in
die gefilterte Liste beantwortet damit die Frage, die am häufigsten gestellt
wird: Was kann raus?

Jeder Wechsel landet in der Gerätechronik, und bei Ausgabe wie Rücknahme
entsteht ein **Übergabeprotokoll** mit fortlaufender Nummer (`UP-2026-000031`).
Es wird im Browser gedruckt – Menü und Bedienelemente bleiben vom Papier weg –,
unterschrieben und abgeheftet. Aussondern und Verlust verlangen eine
Begründung; ohne sie speichert das System nicht.

Ein ausgesondertes Gerät zurückzuholen darf nur die Bereichsleitung, und auch
nur mit Begründung. Das ist Absicht: Meist ist das Gerät entsorgt, und wer es
wiederbelebt, soll dafür geradestehen.

### Fotos

Ein Gerätefoto zeigt bei der Rückgabe, in welchem Zustand das Gerät ausgegeben
wurde. Aufgenommen wird es direkt auf der Geräteseite, auf dem Handy über die
Kamera. HEIC vom iPhone wird angenommen.

**Für die Sicherung wichtig:** Gerätefotos liegen im selben Verzeichnis wie die
Ticketanhänge (`UPLOAD_DIR`) und sind von Abschnitt 11 mit abgedeckt. Eine
reine Datenbanksicherung lässt sie zurück.

### Datenübernahme

```bash
npm run import -- --typ=geraete --datei=geraete.csv --dry-run
```

| Spalte | Pflicht | Hinweis |
|---|---|---|
| `Nummer` | ✓ | Inventarnummer aus dem Altsystem, wird übernommen |
| `Name` | ✓ | Bezeichnung |
| `Art` | | Geräteart; unbekannte werden angelegt |
| `Hersteller` | | unbekannte werden angelegt |
| `Modell` | | wird beim Hersteller angelegt, falls neu; ohne Hersteller landet der Text in den Notizen |
| `Seriennummer`, `IMEI` | | |
| `MAC` | | mehrere durch Komma oder Semikolon; Schreibweise egal |
| `Bereich` | | Kürzel oder Name; ohne Angabe der erste Inventarbereich |
| `Kostenstelle` | | Nummer oder Bezeichnung – **muss vorher angelegt sein** |
| `Status` | | ohne Angabe aus der Zuweisung abgeleitet |
| `Zugewiesen` | | E-Mail-Adresse der Person |
| `Standort`, `Raum` | | |
| `Anschaffung`, `Garantie` | | `31.12.2025` oder ISO |
| `Preis` | | `1.299,00` – der Punkt wird als Tausendertrenner gelesen |
| `Lieferant`, `Rechnung`, `Notizen` | | |

Immer zuerst mit `--dry-run`. Eine MAC-Adresse, die schon zu einem anderen
Gerät gehört, wird gemeldet und übersprungen, ohne den Lauf abzubrechen – das
ist meist der Hinweis, dass ein Gerät im Altsystem doppelt geführt wurde.

## 10. Der Hintergrunddienst

```bash
npm run worker
```

Muss dauerhaft laufen, sonst wird keine Mail abgeholt und keine Frist geprüft.
Als systemd-Unit oder als `worker`-Dienst aus der `docker-compose.yml`.

| Aufgabe | Voreinstellung | Schlüssel |
|---|---|---|
| Postfächer abfragen | alle 2 Minuten | `WORKER_MAIL_CRON` |
| Fristen prüfen | alle 5 Minuten | `WORKER_SLA_CRON` |
| Aufräumen | täglich 3:30 Uhr | `WORKER_CLEANUP_CRON` |

Die Fristenprüfung lässt sich zur Abnahme von Hand anstoßen:

```bash
npm run sla:check
```

---

## 11. Sicherung und Wiederherstellung

Zu sichern sind **zwei** Dinge – die Datenbank *und* das Verzeichnis mit den
Anhängen. Eine Datenbanksicherung allein hinterlässt Vorgänge, deren Foto vom
Schaden fehlt.

```bash
# Datenbank
pg_dump --format=custom --file=ticketsystem-$(date +%F).dump "$DATABASE_URL"

# Anhänge
tar czf uploads-$(date +%F).tar.gz "$UPLOAD_DIR"
```

Wiederherstellen:

```bash
pg_restore --clean --if-exists --dbname="$DATABASE_URL" ticketsystem-2026-08-07.dump
tar xzf uploads-2026-08-07.tar.gz -C /
```

**Eine Sicherung, die nie zurückgespielt wurde, ist keine Sicherung.** Einmal
im Jahr auf einem Testsystem üben.

Ebenfalls sichern, außerhalb des Systems: `APP_SECRET` und `AUTH_SECRET`. Ohne
sie ist eine wiederhergestellte Datenbank nur zur Hälfte brauchbar – die
Postfach-Kennwörter darin lassen sich nicht mehr entschlüsseln.

### Aktualisieren

```bash
git pull
npm ci
npm run db:migrate     # Migrationen sind vorwärtsgerichtet
npm run build
# Dienste neu starten
```

Vorher sichern. Migrationen laufen mit `migrate deploy`, nicht mit
`migrate dev` – letzteres darf die Datenbank zurücksetzen und hat auf einem
Produktivsystem nichts zu suchen.

---

## 12. Löschfristen und Datenschutz

### Automatisches Löschen

```dotenv
RETENTION_DAYS="0"     # 0 = nie löschen
```

Ist ein Wert gesetzt, entfernt der Aufräumjob geschlossene Vorgänge samt
Nachrichten und Anhängen nach dieser Zahl von Tagen. **Vorher mit
Datenschutzbeauftragter und Personalvertretung klären** – für einen Träger sind
je nach Inhalt unterschiedliche Fristen einschlägig, und eine einmal gelöschte
Nachvollziehbarkeit kommt nicht zurück.

Zwei Dinge räumt der Job unabhängig davon auf, weil sie sonst still anwachsen:

- **Nicht zugeordnete Anhänge** nach 24 Stunden – Fotos, die jemand im
  Meldeformular hochgeladen, die Meldung dann aber nie abgeschickt hat.
- **Gelesene Benachrichtigungen** nach 90 Tagen. Sie sind reine Hinweise; im
  Vorgang selbst steht ohnehin alles.

### Was ohnehin gilt

- Das Meldeformular weist ausdrücklich darauf hin, **keine Gesundheitsdaten und
  keine Namen von Klientinnen und Klienten** einzutragen. Ein Ticketsystem ist
  keine Akte.
- Interne Notizen erreichen den Melder nie – auch nicht per Mail, auch nicht in
  einer Abspaltung. Sie bleiben aber im Zielbereich für alle Mitglieder
  sichtbar; darauf weist der Abspalten-Dialog hin.
- Anhänge werden nur nach Prüfung der Rechte am Vorgang ausgeliefert. Die
  Kenntnis der Kennung allein reicht nicht.
- Das Protokoll zeichnet jede Zustandsänderung mit Person und Zeitpunkt auf.
  Das ist gewollt – und mitbestimmungsrelevant. Bitte vorab mit der
  Personalvertretung besprechen.
- Postfach-Kennwörter liegen verschlüsselt (AES-256-GCM), nie im Klartext.

---

## 13. Wenn etwas klemmt

### Mails werden nicht abgeholt

1. Läuft der Hintergrunddienst? (`npm run worker`, Protokoll ansehen)
2. **Verwaltung → Postfächer**: Was steht bei *letzter Abruf* und *letzter
   Fehler*?
3. **„Verbindung testen"** drücken.
4. Bei M365 mit IMAP: Basisauthentifizierung ist abgeschaltet – auf Graph
   wechseln.
5. Bei Graph: Wurde die **Administratorzustimmung** erteilt? Ohne sie schlägt
   jeder Aufruf fehl.

### Antworten erzeugen einen neuen Vorgang statt am alten zu landen

Fast immer die Plus-Adressierung. Siehe [4.](#4-antwortadressen-und-catch-all).
Testen lässt sich das ohne Umweg: Antwortadresse aus einer Benachrichtigung
kopieren und von Hand eine Mail dorthin schicken. Kommt sie nicht an, liegt es
am Mailserver.

Zweithäufigster Grund: `APP_SECRET` wurde ausgetauscht. Dann sind alle älteren
Antwortadressen ungültig.

### Steuerbefehle werden nicht ausgeführt

Im Protokoll nach `EMAIL_COMMAND_REJECTED` suchen – dort steht der Grund. Die
drei häufigsten: die Antwort ging nicht an die signierte Adresse, die
Absenderadresse weicht ab, oder die Person ist nicht Mitglied des Bereichs.

### Ein Vorgang landet im falschen Eingang

- **Verwaltung → Bereiche**: Steht der Bereich auf *je Standort* oder
  *gemeinsam*?
- **Verwaltung → Postfächer**: Hängt die Empfangsadresse am richtigen Eingang?
- Bei Meldungen über das Portal entscheidet der beim Melden gewählte Standort.
  Ist bei den Meldenden der eigene Standort hinterlegt? Er ist die Vorbelegung,
  und was vorbelegt ist, wird selten geändert.

### Die Ampel steht überall auf Rot

Meist ein fehlender Feiertag oder ein zu enger Wochenplan. **Verwaltung →
Dienstzeiten** prüfen. Auch die Zeitzone: `TZ=Europe/Berlin`.

### Niemand kann sich mehr anmelden

- Ist der geheime Clientschlüssel der Entra-App abgelaufen? Das ist der
  häufigste Grund und passiert genau einmal – am Tag des Ablaufs.
- Mit einem lokalen Administrationskonto hineinkommen (deshalb sollte es
  eines geben) und einen neuen Schlüssel eintragen.

### Nach dem Wiederherstellen fehlen Anhänge

Das Verzeichnis aus `UPLOAD_DIR` wurde nicht mitgesichert. Siehe
[10.](#10-sicherung-und-wiederherstellung).

---

## Offene Punkte, die nur Sie klären können

Im Bau sind Demo-Werte hinterlegt; für den Betrieb brauchen wir von Ihnen:

- [ ] App-Registrierung in Entra ID durch Ihren M365-Administrator
- [ ] Die tatsächliche Liste der Standorte
- [ ] Die tatsächliche Liste der Bereiche samt Zuordnung *voll* / *schlank*
- [ ] Echte Postfachadressen je Eingang
- [ ] Plus-Adressierung oder Catch-all für die Antwortadressen
- [ ] Dienstzeiten je Bereich und der Feiertagskalender Ihres Bundeslandes
- [ ] Wer in welchem Bereich und an welchen Standorten arbeitet
- [ ] CSV-Export aus dem Altsystem
- [ ] Löschfristen, abgestimmt mit Datenschutz und Personalvertretung
