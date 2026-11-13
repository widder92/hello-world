# Marktrechnung

E-Rechnungen für Lieferanten des Lebensmittelhandels: **erstellen, prüfen und
verschicken** – mit den Angaben, die REWE, EDEKA und andere Händler über das
gesetzliche Minimum hinaus verlangen.

Das Programm läuft auf dem eigenen Rechner (Windows, macOS, Linux). Es braucht
keine Installation, kein Konto und keine Internetverbindung außer zum eigenen
E-Mail-Postfach. Alle Daten bleiben als lesbare Dateien auf dem Rechner.

![Rechnung bearbeiten: rechts die laufende Prüfung](docs/rechnung.png)

| Übersicht | Erzeugte Rechnung (ZUGFeRD-PDF) |
|---|---|
| ![Übersicht](docs/uebersicht.png) | ![PDF](docs/pdf.png) |

## Wofür es gedacht ist

Wer an den Handel liefert und seine Rechnungen bisher mit einem allgemeinen
Rechnungsprogramm wie Lexware Office (lexoffice) schreibt, bekommt sie oft
zurück. Meist ist nicht die Datei fehlerhaft – dem Handel fehlen Angaben für
seine automatische Verarbeitung:

| Angabe | Feld der Norm | Wer sie verlangt |
|---|---|---|
| GLN des Rechnungsempfängers | BT-46 | REWE, EDEKA, Handel allgemein |
| GLN des Warenempfängers (Markt) | BT-71 | REWE, EDEKA, Handel allgemein |
| Kostenstelle, 10 Ziffern | BT-19 | REWE |
| Eigene GLN als Lieferant | BT-29 | EDEKA |
| Bestell- und Lieferscheinnummer | BT-13, BT-16 | EDEKA, Handel allgemein |
| Lieferdatum | BT-72 | alle |
| GTIN (EAN) je Artikel | BT-157 | EDEKA, Handel allgemein |

Marktrechnung führt diese Angaben als eigene Felder, prüft Prüfziffern und
Pflichtangaben **vor** dem Versand und sagt in einfachen Worten, was fehlt.

## Was das Programm kann

- **Rechnungen schreiben** mit Kunden, Märkten und Artikeln aus den Stammdaten;
  Rabatte, Zuschläge, Skonto, zwei Steuersätze, Kleinunternehmer (§ 19 UStG)
- **Laufende Prüfung** gegen die Norm EN 16931, die deutschen XRechnung-Regeln
  und die Vorgaben von REWE, EDEKA und des Handels allgemein
- **E-Rechnung erzeugen** als
  - ZUGFeRD-PDF (PDF/A-3 mit eingebetteter XML, Profil EN 16931) – empfohlen
  - XRechnung 3.0 als reine XML-Datei (Syntax CII)
- **Versand per E-Mail** über das eigene Postfach, eine Rechnung je E-Mail,
  mit Vermerk, wann sie an wen ging
- **Fremde Rechnungen prüfen**: eine abgelehnte Datei aus einem anderen
  Programm einlesen, die Ursache sehen und sie als Entwurf übernehmen, um die
  fehlenden Angaben zu ergänzen
- **Stornieren** über eine Rechnungskorrektur; fertige Rechnungen sind nicht
  mehr änderbar
- **Rechnungsliste als CSV** für Buchhaltung und Steuerberatung, **Sicherung**
  des ganzen Datenordners als ZIP

## Einrichten

1. **Programm holen.** Unter *Actions → Marktrechnung* im GitHub-Repository
   liegt nach jedem Build das Paket für Windows, macOS und Linux; nach einer
   Veröffentlichung auch unter *Releases*.
2. **Einrichten und starten.**
   - **Windows:** `Marktrechnung-Setup-….exe` ausführen. Das Setup braucht
     keine Administratorrechte, legt Symbole im Startmenü und auf dem Desktop
     an und trägt das Programm unter *Einstellungen → Apps* ein, wo es sich
     wieder entfernen lässt. Beim ersten Start warnt Windows vor einem
     unbekannten Herausgeber, weil das Setup nicht signiert ist: *Weitere
     Informationen → Trotzdem ausführen*.
     Eine neue Fassung wird einfach darüber eingerichtet; Rechnungen und
     Daten bleiben erhalten. Wer nichts einrichten will, nimmt das
     Windows-ZIP und startet `Marktrechnung.exe` direkt.
   - **macOS:** ZIP entpacken, Rechtsklick auf `Marktrechnung` → *Öffnen*.
   - **Linux:** ZIP entpacken, `./marktrechnung` starten.

   Das Programm öffnet sich im Browser.
3. **Erste Schritte** auf der Startseite abarbeiten:
   - eigene Firmendaten, Steuernummer oder USt-IdNr., GLN, Bankverbindung
   - Kunden anlegen – für REWE und EDEKA gibt es Vorlagen
   - E-Mail-Versand einrichten und mit einer Probe-E-Mail testen
4. **Vor der ersten echten Rechnung** beim Kunden klären (siehe unten).

Das Programm läuft unauffällig im Hintergrund weiter, wenn das Browserfenster
geschlossen wird. Ein erneuter Doppelklick öffnet es wieder; *Programm
beenden* steht unten links.

### Was vorab mit dem Kunden zu klären ist

**REWE Group**

- E-Rechnungen müssen angemeldet werden: Format und Versandweg werden
  vereinbart und getestet. Anmeldung über
  `e-rechnungen.onboarding@rewe-group.com`, dabei die eigene Kreditorennummer
  nennen. An dieses Postfach keine echten Rechnungen schicken.
- REWE nimmt Rechnungen **nur von angemeldeten Absenderadressen** und erst ab
  dem bestätigten Starttermin an. Alles andere wird automatisch abgelehnt –
  das ist eine häufige Ursache für „Fehler“, die mit dem Inhalt der Rechnung
  nichts zu tun haben.
- Beim Auftraggeber erfragen: Kostenstelle (10 Ziffern), GLN des
  Rechnungsempfängers und GLN jedes belieferten Markts.
- Der öffentlich beschriebene E-Mail-Weg gilt laut REWE derzeit für
  Kostenrechnungen. Für Warenrechnungen beim Onboarding nachfragen.

**EDEKA**

- EDEKA veröffentlicht keine Rechnungsadressen. Bei der abrechnenden
  Gesellschaft (Regionalgesellschaft oder Markt) erfragen: E-Mail-Adresse für
  E-Rechnungen, den Leitfaden zum elektronischen Rechnungsversand, die GLN von
  Rechnungsempfänger und Märkten.
- Die eigene GLN und die GTIN der Artikel vergibt GS1 Germany.
- EDEKA prüft auch das PDF streng auf PDF/A-3.

Die Hinweise stehen im Programm beim jeweiligen Kunden und unter *Hilfe*,
jeweils mit Quellen. Stand: November 2026. Vorgaben ändern sich – maßgeblich
ist, was der Kunde mitteilt.

## Zusammenspiel mit Lexware Office

Lexware Office kann für Buchhaltung, Bank und Steuer weiterlaufen:

- Rechnungen an den Handel werden in Marktrechnung geschrieben und verschickt.
- In Lexware Office werden sie als Einnahme-Beleg erfasst (die PDF-Datei aus
  dem Ordner `Rechnungen` hochladen) oder über die CSV-Liste nachgebucht.
- Getrennte Nummernkreise verwenden, damit keine Rechnungsnummer doppelt
  vorkommt (Einstellungen → *Aufbau der Rechnungsnummer*, zum Beispiel
  `ER-{JJJJ}-{NNNN}`).

Eine bereits in Lexware Office geschriebene, abgelehnte Rechnung lässt sich
unter *Fremde Rechnung prüfen* einlesen und mit unveränderter Rechnungsnummer
als vollständige E-Rechnung neu erzeugen.

## Wo die Daten liegen

```
Dokumente/Marktrechnung/
  Rechnungen/2026/Rechnung_RE-2026-0001.pdf   fertige E-Rechnungen, nach Jahren
  daten/einstellungen.json                    Firmendaten, Nummernkreis
  daten/kunden.json, artikel.json             Stammdaten
  daten/rechnungen/…                          Entwürfe und Rechnungsdaten
  daten/versand/…                             Kopien der verschickten E-Mails
```

Rechnungen müssen acht Jahre unverändert aufbewahrt werden; bei E-Rechnungen
zählt die Datei, nicht ein Ausdruck. Den Ordner deshalb regelmäßig sichern.

Das E-Mail-Passwort liegt **nicht** im Datenordner, sondern im
Einstellungsordner des Benutzerkontos; unter Windows ist es zusätzlich an das
Windows-Konto gebunden (Data Protection API).

## Sicherheit

- Der eingebaute Server ist nur vom eigenen Rechner erreichbar (127.0.0.1) und
  verlangt ein Kennwort, das beim Start gesetzt wird.
- E-Mails gehen nur über verschlüsselte Verbindungen hinaus; ohne gültiges
  Zertifikat des Mailservers wird kein Passwort übertragen.
- Das Programm lädt nichts aus dem Internet nach und sendet nichts außer den
  E-Mails, die man selbst abschickt.

## Grenzen

- Die eingebaute Prüfung deckt die Regeln ab, an denen Rechnungen in der
  Praxis scheitern, und wird in den Tests mit einem amtlichen Prüfprogramm
  abgeglichen. Sie ist selbst kein zertifizierter Validator.
- Was REWE und EDEKA verlangen, ist nur teilweise öffentlich dokumentiert. Was
  aus Drittquellen stammt, ist als Warnung statt als Fehler eingestuft.
- Kein EDI (EDIFACT), kein PEPPOL, keine UBL-Syntax, keine Anhänge an der
  Rechnung, nur Euro.
- Das Programm ersetzt keine Steuerberatung.

## Für Entwickler

Geschrieben in Go ohne fremde Bibliotheken; die Oberfläche nutzt Preact und
htm (im Programm eingebaut, kein Build-Schritt).

```
cmd/marktrechnung    Programmstart, Browser öffnen, Betriebssystem-Eigenheiten
internal/dec         Festkomma-Arithmetik für Geldbeträge
internal/modell      Datenmodell nach EN 16931 (Feldnummern BT-… in Kommentaren)
internal/rechnen     Summen, Steuer, Fälligkeit
internal/pruefung    Prüfregeln mit verständlichen Meldungen
internal/partner     Vorgaben von REWE, EDEKA, Handel – mit Quellen
internal/cii         E-Rechnungs-XML schreiben und lesen (Syntax CII)
internal/pdf         PDF/A-3-Erzeuger mit Schrifteinbettung
internal/beleg       Layout der Rechnung
internal/zugferd     PDF und XML zusammenfügen, XML aus PDF lesen
internal/speicher    Datenablage als Dateien, Nummernvergabe
internal/post        E-Mail-Versand (SMTP)
internal/server      Schnittstelle zwischen Oberfläche und Programm
web/dateien          Bedienoberfläche
setup                Windows-Setup (NSIS-Skript, Symbol)
```

Bauen und testen:

```sh
go test ./...                       # alle Tests
go run ./cmd/marktrechnung          # starten
./bauen.sh                          # Setup und Pakete nach dist/ (Setup braucht "makensis")
```

Abgleich mit dem amtlichen Prüfprogramm
([Mustang](https://www.mustangproject.org), prüft EN 16931, XRechnung und
PDF/A-3):

```sh
MUSTANG_JAR=/pfad/Mustang-CLI-2.26.0.jar go test ./internal/zugferd/ -run Amtlich -v
```

Durchlauf der Oberfläche mit einem echten Browser (Playwright):

```sh
go run ./cmd/marktrechnung -daten /tmp/probe -kein-browser   # zeigt die Start-Adresse
node tests/oberflaeche.js 'http://127.0.0.1:47411/?t=…' /tmp/bilder
```

Die Schrift im PDF ist Liberation Sans (SIL Open Font License, siehe
`internal/pdf/schriften/LIZENZ-Liberation.txt`). Lizenzen der
Oberflächen-Bibliotheken: `web/dateien/fremd/LIZENZEN.txt`.
