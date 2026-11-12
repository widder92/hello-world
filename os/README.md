# HelloOS

Ein kleines, von Grund auf selbst geschriebenes Betriebssystem für PCs mit
64-Bit-Prozessor (x86-64) und UEFI – mit grafischer Oberfläche samt Startmenü,
Netzwerk, einem einfachen Browser, Texteditor, verschlüsselter Dateiübertragung
(SFTP), signierten Updates und eingebautem Claude. Kein Linux, kein GRUB, keine
fremde Bibliothek: rund 24 000 Zeilen C und etwas Assembler.

![HelloOS mit Terminal, Systemmonitor und Malprogramm](docs/screenshot.png)

## Was es kann

- **Start über UEFI** wie jedes aktuelle Betriebssystem: HelloOS ist eine
  einzige Datei (`BOOTX64.EFI`), die die Firmware direkt lädt
- **64-Bit-Kernel** mit eigenen Seitentabellen, Interrupt-Behandlung,
  Speicherverwaltung und präemptivem Multitasking
- **Grafische Oberfläche**: Fenster zum Verschieben, Vergrößern und
  Maximieren, Taskleiste mit Startmenü,
  Mauszeiger, geglättete Schrift – gezeichnet direkt in den Bildspeicher der
  Grafikkarte
- **Programme**: Browser, Terminal mit Shell (mehrere gleichzeitig), Texteditor,
  Dateiübertragung, Systemmonitor, Malprogramm, Info- und Ausschalten-Dialog
- **Browser**: zeigt Webseiten in echter Schrift mit Überschriften, Links,
  Formularen und Bildern (PNG, JPEG, GIF), wertet die Stylesheets der Seite
  aus (Farben, Hintergründe, Schriftgrößen, Ausrichtung, Ausgeblendetes),
  meldet sich an (Cookies) und prüft bei https den Server – ohne JavaScript
  und ohne mehrspaltige Anordnung
- **Echte Schriften**: ein eigener TrueType-Renderer zeichnet Text in jeder
  Größe aus den eingebauten Schriftdateien (DejaVu Sans) – in Fenstertiteln,
  Menüs, Dialogen und im Browser
- **Eigene Dateien bleiben erhalten**: Sie werden auf dem Datenträger
  gesichert, von dem HelloOS startet, und sind dort auch unter Windows zu sehen
- **Neben Windows**: vom USB-Stick – oder fest eingerichtet in einer eigenen
  Partition der internen Platte (NVMe, SATA), mit Eintrag im Startmenü des PCs
- **Dateien mit einem Server austauschen** über SFTP – verschlüsselt, mit
  Prüfung des Serverschlüssels; als Fenster mit zwei Spalten oder im Terminal
- **Eingabegeräte**: USB-Tastaturen und -Mäuse (xHCI, auch An- und Abstecken
  im Betrieb) sowie PS/2, wie es viele Notebook-Tastaturen intern noch nutzen;
  deutsche und US-Tastaturbelegung
- **Netzwerk**: Treiber für Intel-Netzwerkkarten (auch I225/I226), eigener Stack mit ARP, IPv4,
  ICMP, UDP, DHCP, DNS und TCP, dazu HTTP und HTTPS (TLS 1.3 mit
  Zertifikatsprüfung)
- **Claude eingebaut**: ein Chatfenster und der Befehl `claude` – Claude
  beantwortet Fragen und kann in HelloOS nachsehen und Befehle ausführen
- **Updates**: HelloOS sucht regelmäßig nach neuen Versionen, prüft deren
  Signatur, zeigt, was neu ist, und installiert sie nach deiner Zustimmung auf
  dem Datenträger, von dem es gestartet wurde
- **Ton**: Klänge beim Start, beim Herunterfahren, bei Rückfragen und Fehlern
  über Intel HD Audio, mit Lautstärkeknopf in der Taskleiste
- 35 Shell-Befehle
- **Serielle Konsole** mit eigener Shell – praktisch für Tests ohne Bildschirm

![Netzwerk: eine Webseite über HTTPS im Terminal](docs/screenshot-netz.png)

## Bauen und in QEMU starten

Benötigt werden `clang`, `lld`, `nasm`, `make` und `python3`, zum Ausprobieren
außerdem QEMU mit der UEFI-Firmware OVMF. Unter Debian/Ubuntu:

```sh
sudo apt install clang lld nasm make python3 qemu-system-x86 ovmf
```

Dann in diesem Verzeichnis:

```sh
make            # baut build/BOOTX64.EFI und das Disk-Image build/helloos.img
make run        # startet HelloOS in einem QEMU-Fenster
make run-usb    # dasselbe mit USB-Tastatur und -Maus statt PS/2
make test       # bootet ohne Bildschirm und prüft die wichtigsten Befehle
make check      # prüft Kryptografie und Browser-Unterbau auf dem Entwicklungsrechner
```

In QEMU steckt das Image als USB-Stick an einem emulierten USB-3-Controller,
dazu kommt eine emulierte Netzwerkkarte mit Internetzugang über den
Wirtsrechner. `make check` braucht zusätzlich die Python-Pakete `cryptography` und `Pillow`
und einen gewöhnlichen C-Compiler (`cc`).

Liegt die Firmware an einem ungewöhnlichen Ort: `make run OVMF=/pfad/zu/OVMF.fd`.
Unter macOS liefert Homebrew alles Nötige (`brew install llvm lld nasm qemu`);
`clang` und `lld-link` gibt man dann mit `make CC=... LD=...` an.

## Auf einem echten PC starten – auch neben Windows

> HelloOS ist fast nur in QEMU getestet. Auf echter Hardware lief es bisher
> auf genau einem PC (Mini-PC mit Intel Core Ultra): Start vom Stick,
> Funk-Tastatur und -Maus hinter dem eingebauten USB-Hub, Netzwerk mit einer
> I226 samt Adresszuteilung. Vom
> Stick gestartet, schreibt es nur auf diesen Stick – die interne Platte ist
> für HelloOS schreibgeschützt, solange du es dort nicht ausdrücklich
> einrichtest (siehe unten). Ein Versuch vom Stick ist also ungefährlich, aber
> es kann sein, dass es auf einem bestimmten Rechner nicht startet.

1. USB-Stick vorbereiten – eine der beiden Möglichkeiten:
   - Stick mit FAT32 formatieren und `build/BOOTX64.EFI` als
     `EFI/BOOT/BOOTX64.EFI` daraufkopieren (geht mit jedem Betriebssystem), oder
   - das fertige Image auf den Stick schreiben, z. B. unter Linux mit
     `sudo dd if=build/helloos.img of=/dev/sdX bs=1M conv=fsync`
     (**Achtung:** `/dev/sdX` muss wirklich der Stick sein, er wird überschrieben).
2. Im UEFI-Setup des PCs **Secure Boot ausschalten** – HelloOS ist nicht signiert.
   **Vorher unbedingt lesen:** den Hinweis zu BitLocker weiter unten.
3. Vom Stick starten (Boot-Menü, meist F12, F11, F8 oder Esc).

Reagieren Tastatur und Maus nicht, öffnet HelloOS nach etwa 15 Sekunden von
selbst ein Diagnosefenster mit dem Startprotokoll. Es zeigt für jeden
USB-Anschluss, welches Gerät dort erkannt wurde und warum es nicht bedient
wird – ein Foto davon ist die beste Grundlage für eine Fehlermeldung.
Dasselbe Protokoll zeigt im Terminal der Befehl `log`.

### Neben Windows: zwei Wege

**Weg 1 – der USB-Stick (einfach und ohne Risiko für Windows).** Steckt der
Stick und wählst du ihn im Boot-Menü, startet HelloOS; sonst startet Windows
wie immer. Deine Dateien, Updates und gemerkten Server liegen auf dem Stick.
Die interne Platte wird dabei nicht beschrieben.

**Weg 2 – auf der internen Platte, in einer eigenen Partition.** Dann startet
HelloOS ohne Stick aus dem Startmenü des PCs und speichert auf der internen
Platte. Das ist der nächste Abschnitt.

> **BitLocker / Geräteverschlüsselung – gilt für beide Wege:** Auf den meisten
> PCs mit Windows 11 (und vielen mit Windows 10) ist die Festplatte
> verschlüsselt, und der Schlüssel ist an Secure Boot gebunden. Schaltest du
> Secure Boot aus, verlangt Windows beim nächsten Start den **BitLocker-
> Wiederherstellungsschlüssel** (48 Ziffern). Besorge ihn dir vorher – er
> steht in deinem Microsoft-Konto unter „Geräte" → „BitLocker-Schlüssel" –
> oder halte den Schutz in Windows vor der Umstellung an („BitLocker
> verwalten" → „Schutz anhalten"). Ohne diesen Schlüssel kommst du nicht mehr
> an deine Daten. Schaltest du Secure Boot wieder ein, ist alles wie vorher.

### Auf der internen Platte einrichten

> **Bitte zuerst lesen.** Dieser Teil ist in QEMU gründlich geprüft (siehe
> unten), aber **noch nie auf einem echten PC gelaufen**. HelloOS schreibt
> dann mit eigenen Treibern auf dieselbe Platte, auf der Windows liegt. Es ist
> so gebaut, dass es nur seine eigene Partition anfassen kann – ein Fehler,
> der sich erst auf echter Hardware zeigt, lässt sich trotzdem nicht
> ausschließen. **Mach vorher eine Sicherung deiner wichtigen Daten** auf
> einen anderen Datenträger. Wer das nicht kann oder will, bleibt bei Weg 1.

Was du brauchst: einen PC mit UEFI, eine NVMe-SSD oder eine SATA-Platte (im
Modus „AHCI"), den HelloOS-Stick und – wegen Secure Boot – den
BitLocker-Schlüssel (siehe oben).

**1. In Windows eine Partition für HelloOS anlegen**

1. Rechtsklick auf den Startknopf → „Datenträgerverwaltung".
2. Rechtsklick auf das Laufwerk C: → „Volume verkleinern…" und zum Beispiel
   4000 MB abgeben. (Windows verschiebt dabei nichts Wichtiges; das dauert
   kurz.)
3. Rechtsklick auf den neuen Bereich „Nicht zugeordnet" → „Neues einfaches
   Volume…". Dateisystem **FAT32**, Volumebezeichnung **HELLOOS** – genau so
   geschrieben. An diesem Namen erkennt HelloOS seine Partition.
4. Empfohlen: In Windows den **Schnellstart ausschalten** (Systemsteuerung →
   Energieoptionen → „Auswählen, was beim Drücken von Netzschaltern geschehen
   soll" → Häkchen bei „Schnellstart aktivieren" entfernen). Sonst kann
   Windows beim nächsten Start einen veralteten Stand dieser Partition im
   Gedächtnis haben und dort Dateien von HelloOS durcheinanderbringen.

**2. Vom Stick starten und einrichten lassen**

Starte den PC vom HelloOS-Stick. Findet HelloOS eine Partition namens
HELLOOS, auf der es noch nicht eingerichtet ist, erscheint – noch vor der
Oberfläche, als Text – die Frage, ob es das jetzt tun soll. Mit **J**:

- wird die Startdatei auf diese Partition kopiert und zur Kontrolle
  zurückgelesen,
- entsteht im Startmenü des PCs der Eintrag **„HelloOS"** – hinten angestellt,
  sodass **Windows weiterhin von allein startet**.

Beides erledigt die Firmware des PCs mit ihren eigenen Treibern; HelloOS
selbst schreibt in diesem Schritt keinen einzigen Sektor. Jede andere Taste
(oder 30 Sekunden Warten) lässt alles, wie es ist.

**3. Ohne Stick starten**

Stick abziehen, PC einschalten, das Startmenü des PCs aufrufen (meist F12,
F11, F8 oder Esc) und „HelloOS" wählen. `disk` zeigt dann, wo HelloOS
speichert: in der eigenen Partition auf der internen Platte. Auch Updates
landen ab jetzt dort.

**Wie die Trennung von Windows gesichert ist**

- Interne Platten sind für HelloOS zunächst **komplett schreibgeschützt**.
  Freigegeben wird nur der Bereich der einen Partition, die HELLOOS heißt
  *und* die HelloOS-Startdatei enthält. Jeder Schreibversuch außerhalb wird
  an der letzten Stelle vor dem Treiber abgewiesen – unabhängig davon, was
  der Rest des Systems vorhat.
- Der Windows-Startbereich (die EFI-Partition) wird nie beschrieben; der
  Starteintrag liegt im Speicher der Firmware, nicht auf der Platte.
- Geprüft in QEMU mit nachgestellter Windows-Platte (Startbereich, „Windows"
  als Zufallsdaten, Wiederherstellungspartition), jeweils mit NVMe und SATA:
  einrichten, ohne Stick starten, Dateien schreiben, 3-MB-Datei laden, Update
  installieren, neu starten. Danach war **alles außerhalb der
  HelloOS-Partition Bit für Bit unverändert** (Prüfsumme über die ganze
  Platte) und das Dateisystem der Partition laut `fsck` in Ordnung.

**Was nicht geht oder zu beachten ist**

- PCs, deren SSD im Modus „Intel RST" / „RAID" / „VMD" läuft (bei vielen
  Notebooks mit Intel-Prozessor die Werkseinstellung): HelloOS sieht die
  Platte dann nicht. Es startet zwar, speichert aber nur mit eingestecktem
  Stick. Den Modus im UEFI-Setup umzustellen, kann Windows am Starten hindern
  – das bitte nicht ohne Anleitung des Herstellers tun.
- Platten mit 4096-Byte-Sektoren („4Kn") und Partitionen, die über Windows
  als „BitLocker To Go" verschlüsselt wurden, werden nicht unterstützt.
- Wieder entfernen: In der Datenträgerverwaltung das Volume HELLOOS löschen
  und C: wieder „erweitern". Der Eintrag „HelloOS" im Startmenü des PCs läuft
  dann ins Leere; er lässt sich im UEFI-Setup löschen oder einfach ignorieren.

Was dabei zu beachten ist:

- Stick, Tastatur und Maus dürfen direkt am Rechner oder hinter einem
  **USB-Hub** stecken – auch hinter den fest eingebauten Hubs, an denen in
  manchen PCs die Buchsen hängen, und hinter mehreren Hubs in Reihe. Geprüft
  ist das in QEMU mit dessen (langsamem) USB-1.1-Hub und auf einem echten PC
  mit langsamen Geräten an einem USB-2-Hub. Geräte an einem USB-3-Hub sind
  ungeprüft.
- Unterstützt werden USB-Tastaturen und -Mäuse, auch über Funkempfänger.
  Tastaturen liest HelloOS im einfachen „Boot-Protokoll"; bei allem anderen
  richtet es sich nach der Beschreibung, die das Gerät über seine Daten
  mitliefert. So versteht es auch Empfänger, die über einen Kanal
  Mausbewegungen, Sondertasten und anderes mischen, und übergeht, was weder
  Maus noch Tastatur ist. Das Mausrad blättert in Browser, Editor und
  Claude-Fenster. Bluetooth-Geräte und Touchpads, die nur über I²C
  angebunden sind, funktionieren nicht – dann hilft eine USB-Maus oder die
  Bedienung per Tastatur.
- **Netzwerk gibt es nur per Kabel** und nur mit Netzwerkchips von Intel:
  - die e1000-Familie („Intel Ethernet Connection I217/I218/I219" und ältere),
  - die 2,5-Gigabit-Chips **I225 und I226** aktueller PCs und Mini-PCs,
  - deren 1-Gigabit-Verwandte 82576, I210 und I211.

  Getestet sind die Treiber mit den Modellen, die QEMU nachbildet (zwei aus
  der e1000-Familie und der 82576). I225 und I226 bildet QEMU nicht nach: Der
  Treiber folgt für sie Intels Unterlagen und teilt sich den geprüften Code
  mit dem 82576; auf einem echten PC mit zwei I226-Anschlüssen hat er eine
  Adresse bezogen, mehr ist dort noch nicht geprüft. Hat der
  PC mehrere Anschlüsse, wird der benutzt, an dem ein Kabel steckt.
  **WLAN und Realtek-Karten werden nicht unterstützt.** `net` zeigt, ob eine
  Karte erkannt wurde; eine nicht unterstützte nennt HelloOS beim Start mit
  ihrer Kennung.
- Es wird nur ein Prozessorkern benutzt und ohne Grafikbeschleunigung
  gezeichnet. Bei sehr hohen Auflösungen wählt HelloOS, wenn die Firmware das
  anbietet, einen Modus bis 1920 × 1200.
- Erscheint statt der Oberfläche ein Startprotokoll, das stehen bleibt, zeigt
  dessen letzte Zeile, wie weit der Start gekommen ist.

## Bedienung

![Das Startmenü, dahinter der Browser](docs/screenshot-start.png)

| Aktion | Maus | Tastatur |
| --- | --- | --- |
| Startmenü öffnen | „Start" unten links | Windows-Taste oder Strg+Esc, dann Pfeiltasten und Enter |
| Programm starten | im Startmenü anklicken | F1 Terminal, F2 Monitor, F3 Malen, F4 Info, F5 Claude, F6 Editor, F7 Transfer, F8 Browser |
| Fenster wechseln | Fenster oder dessen Knopf in der Taskleiste anklicken | Alt+Tab |
| Fenster verschieben | an der Titelleiste ziehen | – |
| Fenstergröße ändern | am Griff in der rechten unteren Ecke ziehen | – |
| Fenster maximieren / zurück | Knopf mit dem Rechteck neben dem ✕ | Alt+F10 |
| Fenster schließen | ✕ in der Titelleiste | Alt+F4 |
| Lautstärke | Knopf „Ton" in der Taskleiste: jeder Klick eine Stufe leiser, zuletzt aus | `sound 50`, `sound aus` im Terminal |
| Ausschalten / Neustart | „Aus" rechts in der Taskleiste | `shutdown` bzw. `reboot` im Terminal |

### Befehle im Terminal

| Befehl | Wirkung |
| --- | --- |
| `help` | Befehlsübersicht |
| `info` | System, Build-Nummer, Prozessor, Speicher, Bildschirm |
| `mem` | Heap-Belegung und Speicherkarte der Firmware |
| `uptime`, `date` | Laufzeit bzw. Datum und Uhrzeit |
| `ps` | Tasks mit Zustand und verbrauchter CPU-Zeit |
| `spawn counter [n]` | Hintergrund-Task, der im Sekundentakt bis *n* zählt |
| `spawn burn` | Hintergrund-Task, der pausenlos rechnet (zeigt die Präemption) |
| `kill <pid>` | Task beenden |
| `start <programm>` | `terminal`, `monitor`, `malen`, `info`, `claude`, `editor`, `transfer`, `browser` oder `aus` öffnen |
| `net` | Netzwerkkarte, Adressen, Paketzähler; `net dns <adresse>` setzt den DNS-Server |
| `ping <ziel>` | prüft, ob ein Rechner antwortet |
| `dns <name>` | löst einen Namen in eine IPv4-Adresse auf |
| `web <adresse>` | holt eine Webseite und zeigt sie als Text |
| `browse [adresse]` | öffnet eine Webseite im Browser-Fenster (siehe [Browser](#browser)) |
| `get <adresse> <datei>` | lädt eine Datei aus dem Netz zu den eigenen Dateien |
| `sftp <benutzer>@<server>` | Dateien verschlüsselt mit einem Server austauschen (siehe [unten](#webseite-bearbeiten-editor-und-dateiübertragung)) |
| `update` | sucht jetzt nach einem Update und fragt, ob es installiert werden soll |
| `news` | zeigt, was in der laufenden Version neu ist |
| `claude <frage>` | mit Claude sprechen (siehe [Claude](#claude)) |
| `disk` | zeigt alle Datenträger, ihre FAT32-Partitionen und wo HelloOS speichert |
| `log [fenster]` | Startprotokoll: was die Treiber beim Start gemeldet haben (USB-Geräte, Netzwerk, Ton, Datenträger) |
| `ls`, `cat <datei>`, `rm <datei>` | Dateien auflisten, anzeigen, löschen |
| `edit [datei]` | Datei im Texteditor öffnen (oder neu anlegen) |
| `sha256 <datei>` | Prüfsumme einer Datei |
| `write <datei> <text>` | Datei (über)schreiben |
| `append <datei> <text>` | Zeile anhängen |
| `echo <text>` | Text ausgeben |
| `sound [0-100\|aus\|test]` | Soundkarte und Lautstärke anzeigen, Lautstärke setzen, Klang abspielen |
| `keymap [de\|us]` | Tastaturbelegung anzeigen oder umschalten (Standard: `de`) |
| `history`, `clear` | Befehlsverlauf, Terminal leeren |
| `panic` | löst absichtlich eine CPU-Ausnahme aus |
| `reboot`, `shutdown` | Neustart, Herunterfahren |

In der Größe ändern lassen sich Browser, Terminal, Texteditor,
Dateiübertragung und Claude; Systemmonitor, Malprogramm und die Dialoge haben
eine feste Größe. Beim Ziehen zeigt ein Rahmen die künftige Größe – das
Fenster folgt, sobald du loslässt.

Pfeil hoch/runter blättert im Verlauf, Strg+C verwirft die Eingabe,
Strg+L leert das Terminal.

## Ton

HelloOS meldet sich mit Klängen: einem aufsteigenden Akkord beim Start, dem
gleichen abwärts beim Herunterfahren, einem kurzen Zweiklang, wenn eine
Rückfrage auf dich wartet (Update, Erlaubnis für Claude, Serverschlüssel,
Überschreiben, ungesicherte Änderungen), und einem tieferen bei Fehlern. Die
Klänge sind nicht aufgenommen, sondern werden beim Start aus Sinustönen
errechnet.

Unterstützt wird **Intel HD Audio**, die Soundkarte fast aller PCs. Der
Treiber fragt den Wandler-Chip ab, sucht alle analogen Ausgänge (Lautsprecher,
Kopfhörer, Line-Ausgang) und schaltet sie ein. `sound` zeigt, was er gefunden
hat; ohne brauchbare Soundkarte bleibt HelloOS still und der Knopf „Ton"
erscheint nicht.

Was dabei zu wissen ist:

- **Kein Ton über HDMI oder DisplayPort.** Dafür müsste HelloOS auch die
  Grafikkarte ansteuern. Bei einem PC ohne eigene Lautsprecher kommt der Ton
  also nur aus der Kopfhörerbuchse – nicht aus den Lautsprechern des Monitors.
- **Getestet nur in QEMU**, mit drei nachgebildeten Soundkarten; dort wird die
  Ausgabe mitgeschnitten und auf die erwarteten Tonhöhen und Lautstärken
  geprüft. Echte Wandler-Chips unterscheiden sich stark – es ist gut möglich,
  dass ein bestimmter PC stumm bleibt. Die Meldung beim Start (`Ton: …`) sagt
  dann, wie weit der Treiber gekommen ist.
- Keine Aufnahme, keine Musik- oder Videowiedergabe, kein Ton aus dem Browser.

## Browser

![Der Browser mit der Bearbeitungsseite eines WordPress-Beitrags](docs/screenshot-browser.png)

Startmenü → „Browser", F8 oder `browse adresse` im Terminal. Es ist ein
**einfacher Browser**: Er holt eine Seite samt ihren Stylesheets, lässt ihre
Skripte weg und setzt sie selbst – in einer Spalte, in echter Schrift:
Überschriften, fett, kursiv, Listen, Tabellen mit Spalten, Links, Formulare
und Bilder, mit den Farben, Hintergründen und Schriftgrößen der Seite.

![Eine gestaltete Seite: dunkler Kopf mit Menü, Seitenhintergrund, farbige Überschriften, hinterlegter Kasten](docs/screenshot-gestaltung.png)

![Eine Seite in echter Schrift: Überschriften, Fließtext, Listen](docs/screenshot-schrift.png)

![Eine Seite mit Bildern: Logo mit durchsichtigem Rand und ein großes Foto](docs/screenshot-bilder.png)

Was er kann:

- Links folgen, zurückgehen, neu laden; Adressen eingeben (Strg+L)
- Formulare ausfüllen und absenden: Textfelder, Passwörter, Häkchen,
  Auswahllisten, mehrzeilige Textfelder (öffnen einen kleinen Editor)
- sich anmelden: Cookies bleiben erhalten, solange das Fenster offen ist –
  danach sind sie weg, gespeichert wird nichts
- https mit Prüfung des Servers; die Statuszeile zeigt, ob die Verbindung
  verschlüsselt ist. Soll ein Passwort unverschlüsselt (http) gesendet werden,
  fragt der Browser vorher nach.
- Dateien, die keine Webseiten sind, landen bei den eigenen Dateien
- **Bilder** in den Formaten PNG, JPEG (auch „progressiv") und GIF (das erste
  Einzelbild). Die Seite ist sofort lesbar; die Bilder lädt ein Helfer im
  Hintergrund nach, die Statuszeile zählt mit. Jedes Bild steht als Block für
  sich, große werden auf die Fensterbreite verkleinert. Ein Klick auf ein
  verlinktes Bild folgt dem Link. Was nicht geladen werden kann, erscheint
  als Ersatztext („[Bild: …]").
- Das Mausrad blättert.
- **Tabellen** stehen in Spalten: Der Browser misst jede Zelle aus, verteilt
  die Breite (schmale Spalten wie Nummern und Preise zuerst, damit sie nicht
  umbrechen) und setzt die Zellen einer Zeile nebeneinander; langer Text
  bricht in seiner Zelle um. Er kennt Zellen über mehrere Spalten oder
  Zeilen, Tabellenüberschriften, farbige Zellen und Zeilen sowie Tabellen in
  Tabellen (bis drei Stufen); Links, Bilder und Formularfelder in Zellen
  funktionieren. Rahmen und Linien zeichnet er nicht. Tabellen mit mehr als
  40 Spalten oder 6000 Zellen erscheinen zeilenweise.

  ![Eine Tabelle mit Kopfzeile, rechtsbündigen Zahlen und Summenzeile](docs/screenshot-tabelle.png)
- **Gestaltung der Seite (CSS), in Auszügen:** Der Browser lädt die
  Stylesheets der Seite (höchstens zwölf Dateien) und wendet an, was er
  kann: Textfarbe, Hintergrundfarbe von Seite, Abschnitten und Textstücken,
  fett/kursiv, Schriftgröße (10 bis 28 Pixel), Ausrichtung links/Mitte/rechts,
  Unterstreichung, ausgeblendete Elemente (`display: none`), Listen ohne
  Punkte und Einträge nebeneinander. Er versteht Auswahlen nach Element,
  Klasse und Kennung samt Verschachtelung, Rangfolge und `!important`,
  Variablen (`var(--name)`) und `@media` nach Fensterbreite. Regeln, die er
  nicht prüfen kann (`:hover`, Attribute, Nachbarn), lässt er aus. Wäre Text
  auf seinem Hintergrund kaum zu lesen – etwa helle Schrift auf einem
  Hintergrundbild, das nicht gezeigt wird –, nimmt er die übliche Farbe.

Was er **nicht** kann – und das ist bei heutigen Webseiten viel:

- **kein JavaScript.** Seiten und Funktionen, die darauf beruhen, bleiben leer,
  reagieren nicht oder sagen selbst, dass sie JavaScript brauchen.
- keine Anordnung nach der Gestaltung: keine Spalten, Raster, schwebenden
  Kästen, Abstände, Rahmen, Breiten oder Hintergrundbilder – alles steht
  untereinander in der eingebauten Schrift (die Schriftarten der Seite werden
  nicht geladen). Sie kennt lateinische, griechische und kyrillische Zeichen;
  andere (etwa chinesische) erscheinen als leeres Kästchen. Schrift von
  rechts nach links wird nicht gesetzt.
- keine Bilder in den Formaten WebP, AVIF und SVG, keine Hintergrundbilder
  (die legt die Gestaltung fest), keine bewegten GIFs, keine Videos
- kein Hochladen von Dateien, keine Lesezeichen, keine Tabs, kein Passwortspeicher
- Lange Menüs einer Seite stehen als Liste am Seitenanfang. Der Browser
  springt deshalb beim Laden zur Hauptüberschrift; mit Pos1 geht es nach oben.

### WordPress im Browser von HelloOS

Getestet mit einer echten WordPress-Installation (Version 6.4, lokal, ohne
weitere Plugins):

| Was | Ergebnis |
| --- | --- |
| Anmelden (`wp-login.php`) | funktioniert |
| Dashboard, Listen der Beiträge, Seiten, Kommentare, Benutzer, Einstellungen | lesbar; Links und einfache Formulare funktionieren |
| Beitrag bearbeiten mit dem **Block-Editor** (Standard) | **geht nicht** – WordPress meldet „Der Block-Editor benötigt JavaScript" |
| Beitrag bearbeiten mit dem **klassischen Editor** | funktioniert: Titel und Text geändert, „Aktualisieren" gedrückt, Änderung in der Datenbank geprüft. Der Text erscheint als HTML-Quelltext. |
| Bilder und andere Dateien hochladen (Mediathek) | geht nicht |
| Design anpassen (Customizer, Website-Editor), Plugins per Mausklick, alles mit Ziehen und Ablegen | geht nicht |
| **WooCommerce** (Bestellungen, Produkte) | **nicht getestet.** Teile der Verwaltung sind klassische Formulare und könnten gehen, andere brauchen JavaScript. |

Den klassischen Editor bekommt eine WordPress-Seite über das Plugin „Classic
Editor" – das muss einmal von einem normalen Browser aus installiert werden.
Unterm Strich: Nachsehen, Texte korrigieren, einfache Einstellungen – ja. Das
WordPress, wie man es aus Chrome oder Firefox kennt – nein, und daran ändert
sich in HelloOS auch nichts Grundsätzliches: Ein Browser, der JavaScript und
moderne Gestaltung beherrscht, ist ein Projekt in der Größenordnung von
Millionen Zeilen Code.

## Eigene Dateien

Was du in HelloOS anlegst – mit `write`, dem Editor, `get` oder per
Dateiübertragung –, liegt im Arbeitsspeicher und wird bei jeder Änderung
sofort auf dem Datenträger gesichert, von dem HelloOS gestartet wurde – dem
USB-Stick oder der eigenen Partition auf der internen Platte. Beim nächsten
Start ist alles wieder da.

- Dort liegen die Dateien im Ordner **`DATEIEN`**. Unter Windows kannst du sie
  ganz normal öffnen, kopieren und neue dazulegen (Stick einstecken bzw. das
  Laufwerk HELLOOS öffnen) – HelloOS lädt sie beim nächsten Start mit.
- `ls` zeigt hinter jeder Datei, ob sie gesichert ist. Steckt kein Stick
  (etwa in QEMU ohne USB-Laufwerk), arbeitet HelloOS nur im Arbeitsspeicher
  und sagt das auch.
- Geschrieben wird immer erst die neue Fassung vollständig und dann
  umgeschaltet. Ein Stromausfall hinterlässt also die alte oder die neue
  Datei, keine halbe (eine gerade erst angelegte kann dabei leer bleiben).

Grenzen: eine flache Liste ohne Unterordner, höchstens 256 Dateien von je bis
zu 32 MB; Namen bis 63 Zeichen ohne Leerzeichen und ohne `/ \ : * ? " < > |`.
Groß- und Kleinschreibung unterscheidet HelloOS bei Namen nicht (wie Windows).
In QEMU ist „der Stick" die Datei `build/helloos.img` – ein neuer Build ersetzt
sie und damit auch die darin gespeicherten Dateien.

## Webseite bearbeiten: Editor und Dateiübertragung

![Dateiübertragung: links die eigenen Dateien, rechts der Server](docs/screenshot-transfer.png)

Der Weg einer Änderung an einer Webseite: Datei vom Server holen, im Editor
ändern, wieder hochladen.

### Dateiübertragung (SFTP)

Knopf „Transfer" in der Taskleiste oder F7. Oben Server, Benutzer und Passwort
eintragen (den Port nur, wenn dein Anbieter einen anderen als 22 nennt), dann
„Verbinden". Die Zugangsdaten für **SFTP** stehen bei deinem Webhoster meist
unter „SSH-Zugang" oder „SFTP".

- **Fingerabdruck:** Beim ersten Verbinden zeigt HelloOS den Fingerabdruck
  des Serverschlüssels (`SHA256:…`). Vergleiche ihn mit der Angabe deines
  Anbieters – nur so ist sicher, dass du wirklich mit dessen Server sprichst.
  Nach deinem „Vertrauen" merkt sich HelloOS den Schlüssel auf dem Stick;
  ändert er sich später, erscheint eine deutliche Warnung.
- **Bedienung:** Doppelklick (oder Enter) öffnet einen Ordner auf dem Server
  bzw. holt eine Datei; links öffnet er die Datei im Editor. Hochladen,
  Löschen und neue Ordner gehen über die Knöpfe. Bevor etwas überschrieben
  oder gelöscht wird, fragt HelloOS nach.
- **Passwort:** Es wird nur für die Anmeldung benutzt und nirgends
  gespeichert. Server, Port und Benutzer der letzten Verbindung merkt sich
  HelloOS (Ordner `HELLOOS` auf dem Stick).
- Im Terminal geht dasselbe mit `sftp benutzer@server` und den Befehlen
  `ls`, `cd`, `get`, `put`, `rm`, `mkdir`, `mv` (`hilfe` zeigt alle).

Was unterstützt wird: SFTP über SSH 2 mit den Verfahren aktueller Server
(Schlüsselaustausch curve25519 oder ECDH P-256; Serverschlüssel Ed25519, ECDSA
oder RSA ab 2048 Bit; Verschlüsselung ChaCha20-Poly1305 oder AES-CTR).

Was **nicht** geht:

- **kein FTP und kein FTPS** – nur SFTP. Bietet dein Hoster ausschließlich FTP
  an, kann HelloOS dort nichts übertragen.
- **Anmeldung nur mit Passwort**, nicht mit Schlüsseldatei.
- Nur einzelne Dateien, keine ganzen Ordner auf einmal; Dateien bis 32 MB.
- Auf der eigenen Seite gibt es keine Unterordner. Zwei Dateien gleichen
  Namens aus verschiedenen Server-Ordnern (`index.html`) überschreiben sich
  hier gegenseitig – HelloOS fragt vorher.
- Sehr alte Server, die nur veraltete Verfahren anbieten, werden mit einer
  Meldung abgelehnt, die die angebotenen Verfahren nennt.

### Texteditor

![Der Texteditor mit einer HTML-Datei](docs/screenshot-editor.png)

Knopf „Editor", F6 oder `edit <datei>` im Terminal. Ohne Dateinamen erscheint
eine Liste zum Anklicken; ein neuer Name legt eine neue Datei an.

| Taste | Wirkung |
| --- | --- |
| Strg+S | speichern (und auf dem Stick sichern) |
| Strg+O | andere Datei öffnen |
| Pfeile, Pos1, Ende, Bild auf/ab | bewegen; Strg+Pos1 / Strg+Ende: Anfang / Ende der Datei |
| Tab | bis zur nächsten Vierer-Spalte einrücken |
| Alt+F4 oder ✕ | schließen – bei ungesicherten Änderungen mit Rückfrage |

Neue Zeilen übernehmen die Einrückung der vorigen, lange Zeilen werden nicht
umbrochen (der Ausschnitt wandert mit), Windows-Zeilenenden bleiben erhalten.
Es fehlen: Markieren, Kopieren und Einfügen, Suchen, Rückgängig sowie
farbige Hervorhebung. Darstellen kann die Schrift nur lateinische Zeichen.

### Wofür das taugt – und wofür nicht

Die Übertragung selbst ist ordentlich abgesichert: verschlüsselt, mit Prüfung
des Servers, gegen eine Referenzbibliothek und einen echten OpenSSH-Server
getestet. **Trotzdem ist HelloOS kein System für die Arbeit an einem echten
Onlineshop:**

- Getestet ist alles nur in QEMU, nie auf echter Hardware und nur gegen
  OpenSSH – nicht gegen die Server der verschiedenen Webhoster.
- HelloOS hat keinen Speicherschutz; die Kryptografie ist nicht gegen
  Seitenkanäle gehärtet. Für Zugangsdaten zu einem Shop mit Kundendaten ist
  das zu wenig.
- **WordPress lässt sich nur eingeschränkt bedienen.** Der eingebaute Browser
  kennt kein JavaScript; was damit geht und was nicht, steht unter
  [Browser](#wordpress-im-browser-von-helloos).
- Windows-Programme laufen nicht.

Zum Ausprobieren, Lernen und für eine eigene kleine Seite ist es gedacht. Für
den Shop gehört ein ausgereiftes System her – etwa ein Linux vom USB-Stick,
wenn es vom Alltags-Windows getrennt sein soll.

## Internet

Sobald ein Netzwerkkabel steckt, holt sich HelloOS per DHCP eine Adresse.
Danach funktionieren zum Beispiel:

```
ping 10.0.2.2
dns example.com
web https://example.com/
get https://raw.githubusercontent.com/widder92/hello-world/master/README.md readme.md
```

Wichtig zu wissen:

- **HTTPS prüft, mit wem es spricht.** Der TLS-Client kontrolliert die
  Zertifikatskette des Servers gegen 117 eingebaute Wurzelzertifikate (die
  Liste von Mozilla), dazu Servername und Gültigkeitszeitraum. Passt etwas
  nicht, kommt keine Verbindung zustande. Dafür muss die **Uhr des Rechners
  stimmen** – steht sie auf einem Datum weit in der Vergangenheit, gelten alle
  Zertifikate als „noch nicht gültig".
- Unterstützt wird nur TLS 1.3 mit X25519 und ChaCha20-Poly1305. Die meisten
  Server bieten das an, einzelne nicht – dann scheitert der Verbindungsaufbau.
- Wer in einem Netz arbeitet, das HTTPS mit einer eigenen Zertifizierungsstelle
  aufbricht (manche Firmennetze), kann deren Zertifikat beim Bauen mitgeben:
  `make EXTRA_CA=/pfad/zur/ca.pem`.
- Der [Browser](#browser) ist einfach gehalten und kennt kein JavaScript; `web`
  zeigt eine Seite als reinen Text im Terminal.
- Nur IPv4, keine zerteilten Pakete, keine eingehenden Verbindungen.
- In QEMU beantwortet nur der eingebaute Router (`10.0.2.2`) Pings; Pings ins
  Internet leitet QEMUs Netz nicht weiter. Namensauflösung und Webzugriffe
  funktionieren trotzdem.

## Claude

![Das Claude-Fenster: Claude fragt nach, bevor ein Befehl etwas verändert](docs/screenshot-claude.png)

Claude ist in HelloOS eingebaut – als Chatfenster (F5 oder der Knopf „Claude"
in der Taskleiste) und als Befehl im Terminal (`claude wie viel speicher ist
frei?`). Claude beantwortet nicht nur Fragen, sondern kann über die Shell von
HelloOS selbst nachsehen und Dinge erledigen: „Welche Tasks laufen gerade?",
„Lade mir die Seite example.com", „Schreib meine Einkaufsliste in eine Datei".

### Was du dafür brauchst

HelloOS spricht direkt mit der Claude-API von Anthropic. Dafür ist ein
**API-Schlüssel** nötig, den du unter <https://console.anthropic.com/> anlegst.
Die Nutzung wird dort nach Verbrauch abgerechnet – ein Abo von claude.ai
(Pro, Max) deckt das nicht ab und lässt sich hier nicht verwenden.

Den Schlüssel bekommt HelloOS auf einem von drei Wegen:

- als Datei `claude.key` (oder `claude.txt`) im Hauptverzeichnis des
  Datenträgers, von dem HelloOS startet (Stick oder Partition HELLOOS) – einfach den Schlüssel als einzige
  Zeile hineinschreiben; er wird beim ersten Gebrauch gelesen
- im Chatfenster mit `/key sk-ant-...`
- im Terminal mit `claude key sk-ant-...`

Eingetippte Schlüssel gelten bis zum Ausschalten. HelloOS speichert den
Schlüssel nirgends und sendet ihn nur an die API. Wer den Stick hat, hat aber
auch die Datei – behandle ihn entsprechend.

### Was Claude darf

Claude arbeitet mit denselben Befehlen wie du im Terminal, in drei Stufen:

| Stufe | Befehle |
| --- | --- |
| ohne Rückfrage | nur Anzeigen: `info`, `mem`, `uptime`, `date`, `ps`, `ls`, `cat`, `sha256`, `disk`, `news`, `history`, `net` und `keymap` ohne Angaben, außerdem `echo` und `start` |
| erst nach deiner Zustimmung | alles, was etwas verändert oder ins Netz geht: `write`, `append`, `rm`, `edit`, `kill`, `spawn`, `web`, `browse`, `get`, `ping`, `dns`, `update`, `reboot`, `shutdown`, … |
| nie | `panic`, `claude` selbst und `sftp` – Verbindungen zu deinem Server baust nur du auf |

Vor einer Rückfrage steht der vollständige Befehl im Verlauf; im Fenster
bestätigst du mit „Erlauben" oder J, im Terminal mit j. Dass auch reine
Netzzugriffe nachfragen, ist Absicht: Inhalte, die Claude gelesen hat – etwa
eine Webseite mit untergeschobenen Anweisungen –, sollen nichts unbemerkt
nach draußen schicken können.

### Gut zu wissen

- **Was du schreibst und was Befehle ausgeben, geht an Anthropic.** Dazu gehört
  auch der Inhalt von Dateien, die Claude mit `cat` liest.
- Antworten erscheinen am Stück, nicht Wort für Wort; bei längeren Aufgaben
  kann das eine Weile dauern. Die Statuszeile zeigt, dass Claude arbeitet.
- Das Fenster zeigt reinen Text. Zeichen außerhalb des lateinischen Alphabets
  (auch Emojis) erscheinen als „?".
- Ein Gespräch lebt im Arbeitsspeicher, bis das Fenster geschlossen wird;
  `/neu` (im Terminal `claude neu`) beginnt ein neues. Die Terminals teilen
  sich ein Gespräch, getrennt von dem im Chatfenster.
- Voreingestellt ist das Modell `claude-opus-5-5` mit Gründlichkeit `low`
  (schnelle, kurze Antworten). Ändern mit `claude modell <name>` und
  `claude effort <low|medium|high|xhigh|max>`.
- Lehnt das Modell eine Anfrage ab, darf die API sie von einem anderen
  Claude-Modell beantworten lassen (Anthropics serverseitiger Rückfall).
- Die API-Adresse lässt sich beim Bauen ändern (`make CLAUDE_API_URL=...`),
  etwa für ein eigenes Gateway.

**Stand der Erprobung:** Die Anbindung ist gegen einen nachgebauten API-Server
getestet (Antworten, Befehlsaufrufe mit und ohne Rückfrage, Ablehnungen,
Fehler und Wiederholungen) sowie gegen die echte Adresse `api.anthropic.com`
mit einem ungültigen Schlüssel, was erwartungsgemäß die Fehlermeldung der API
liefert. Ein vollständiges Gespräch mit einem echten Schlüssel wurde noch
nicht geführt. Die Antworten im Bild oben stammen vom Testserver.

## Updates

HelloOS hält sich auf Wunsch selbst aktuell – installiert aber nichts, ohne
zu fragen.

![Das Update-Fenster zeigt die Neuerungen und wartet auf Zustimmung](docs/screenshot-update.png)

1. **Prüfen.** Etwa fünf Sekunden nach dem Netzwerkstart und danach alle sechs
   Stunden wird von der Update-Quelle `helloos.version` geladen. Steht dort
   eine größere Build-Nummer als die eigene, folgt das Paket `helloos.upd`.
   Es wird nur akzeptiert, wenn seine **Ed25519-Signatur** zum öffentlichen
   Schlüssel passt, der in den Kernel eingebaut ist, und wenn die mitsignierte
   Build-Nummer wirklich neuer ist.
2. **Fragen.** Ein Fenster zeigt, was die neue Version bringt, und bietet
   „Jetzt installieren" und „Später" an. Wer „Später" wählt, wird für diese
   Version nicht noch einmal von selbst gefragt; in der Taskleiste bleibt der
   Knopf „Update verfügbar" stehen, bis das Update installiert ist. Im
   Terminal fragt der Befehl `update` mit „(j/n)".
3. **Installieren.** Auf dem Datenträger, von dem HelloOS gestartet wurde, wird
   `EFI/BOOT/BOOTX64.EFI` ausgetauscht – zuerst wird die neue Datei komplett
   geschrieben, dann mit einem einzigen Schreibzugriff umgeschaltet. Ein
   Stromausfall mittendrin hinterlässt also die alte oder die neue Version,
   aber keine halbe. Aktiv wird das Update beim nächsten Start; das Fenster
   bietet den Neustart gleich an.

Über die Echtheit eines Updates entscheidet allein diese Signatur, nicht der
Server, von dem es kommt: Ohne den geheimen Schlüssel kann niemand ein Paket
herstellen, das HelloOS annimmt.

### Woher neue Funktionen kommen

Ein Update enthält genau das, was im Repository entwickelt und veröffentlicht
wurde – von allein entstehen keine neuen Funktionen. Der Weg einer Neuerung:

1. Die Änderung wird unter `os/` entwickelt.
2. In `os/NEUES.txt` steht in ein paar Zeilen, was sich für Benutzer ändert.
   Dieser Text erscheint im Update-Fenster und unter `news`.
3. Sobald die Änderung auf dem Zweig `master` landet, baut der Workflow ein
   signiertes Paket und veröffentlicht es. Laufende Installationen finden es
   bei ihrer nächsten Prüfung und fragen nach.

### Einrichten

**Ohne Schlüssel sind Updates abgeschaltet** – so wird HelloOS standardmäßig
gebaut, und dann fragt es auch nirgends nach neuen Versionen. So schaltest du
sie ein:

1. Schlüsselpaar erzeugen: `make update-key`. Das legt zwei Dateien an:
   - `update-key.pub` – der öffentliche Teil. Einchecken; er wird beim Bauen
     in den Kernel übernommen.
   - `update-key.private` – der geheime Teil. **Niemals einchecken oder
     weitergeben** (`.gitignore` schließt ihn aus). Wer ihn hat, kann Updates
     für deine Installationen ausstellen; geht er verloren, lassen sich
     bestehende Installationen nicht mehr aktualisieren.
2. Den Inhalt von `update-key.private` bei GitHub als Repository-Secret
   `HELLOOS_UPDATE_KEY` hinterlegen (Settings → Secrets and variables → Actions).
3. HelloOS neu bauen und auf den Stick bringen – erst diese Version kennt den
   Schlüssel.

In diesem Repository ist Schritt 1 erledigt: `update-key.pub` ist eingecheckt,
jede hier gebaute Version kennt also den Schlüssel. Der geheime Teil gehört
als Secret zu GitHub (Schritt 2) und sonst nur an einen sicheren Ort beim
Besitzer des Repositorys.

Von da an baut der Workflow `.github/workflows/helloos-update.yml` bei jeder
Änderung unter `os/` auf dem Zweig `master` ein signiertes Paket und legt es
auf den Branch `helloos-updates`. Dort sucht HelloOS standardmäßig
(`UPDATE_URL` im `Makefile`). Geprüft ist dieser Weg einmal von Anfang bis
Ende: Der Workflow hat Build 21 veröffentlicht, und ein älteres HelloOS in
QEMU hat das Paket von GitHub geholt, geprüft, installiert und danach in der
neuen Version gestartet. Auch auf einem echten PC hat ein Update über das Netzwerk funktioniert (von Build 26 auf 27, vom USB-Stick gestartet). GitHub liefert die Build-Nummer bis zu fünf Minuten aus einem Zwischenspeicher – direkt nach einer Veröffentlichung meldet `update` deshalb unter Umständen noch „ist aktuell".

Es geht auch von Hand: `make update-package` erzeugt
`build/update/helloos.upd` (samt dem Text aus `NEUES.txt`) und
`helloos.version`; beide Dateien müssen dann unter der Adresse erreichbar
sein, die beim Bauen als `UPDATE_URL` galt.

Die Build-Nummer ist die Anzahl der Commits (`git rev-list --count HEAD`).
Updates brauchen einen Datenträger, den HelloOS beschreiben kann: den
USB-Stick oder die eigene Partition auf einer
internen NVMe- oder SATA-Platte.

## Wie es funktioniert

### Start

1. Die UEFI-Firmware lädt `EFI/BOOT/BOOTX64.EFI` und ruft `efi_main()` in
   `kernel/boot.c` auf – der Prozessor ist da schon im 64-Bit-Modus.
2. `efi_main()` holt sich von der Firmware den Bildspeicher (GOP), die
   Speicherkarte und die Uhrzeit und misst die Frequenz des Zeitstempelzählers.
3. Mit `ExitBootServices()` wird die Firmware verabschiedet. Ab hier gehört
   die Maschine dem Kernel: eigene Seitentabellen (der gesamte RAM 1:1
   abgebildet), eigener Stack, weiter nach `kmain()` in `kernel/main.c`.
4. `kmain()` richtet Interrupts, Speicher, Bildschirm, Systemtakt, Eingabe-
   geräte und Netzwerk ein, startet die ersten Tasks und wird zum Leerlauf-Task.

### Kernel

- **Systemtakt:** Der Timer des Local APIC unterbricht 1000-mal pro Sekunde.
  Dabei werden PS/2-Controller und serielle Schnittstelle abgefragt und der
  Scheduler aufgerufen. Es ist der einzige Interrupt, den HelloOS braucht –
  das macht den Kernel unabhängig davon, wie ein Rechner seine
  Interrupt-Leitungen verdrahtet. USB und Netzwerk haben eigene Tasks, die
  ihre Geräte regelmäßig abfragen.
- **Multitasking:** Kernel-Threads mit eigenem Stack (`kernel/task.c`). Tasks,
  die gerade aus dem Warten aufwachen (Eingaben, Oberfläche), kommen vor
  solchen dran, die nur rechnen – so bleibt die Oberfläche auch unter Last
  bedienbar.
- **Speicher:** Heap im größten freien Bereich laut Firmware, dazu ein Vorrat
  ganzer Seiten unterhalb von 4 GiB für Gerätetreiber (`kernel/memory.c`).
- **USB:** Für jeden xHCI-Controller läuft ein Task, der Anschlüsse
  beobachtet, neue Geräte einrichtet, Berichte von Tastatur und Maus auswertet
  und Lese-/Schreibaufträge für USB-Sticks ausführt (`kernel/usb.c`).
- **Netzwerk:** Der Task „netz" holt Pakete von der Karte und reicht sie durch
  die Schichten (`kernel/net.c`, `kernel/tcp.c`). Programme rufen blockierende
  Funktionen wie `tcp_connect()` auf und werden geweckt, wenn etwas ankommt.

### Oberfläche

Jedes Programm ist ein Task mit einem Fenster. Es zeichnet in eine eigene
Pixelfläche und meldet geänderte Bereiche; auf Eingaben wartet es in einer
Ereignisschleife. Ein weiterer Task, der Compositor (`kernel/gui.c`), setzt
aus Hintergrund, Fenstern, Taskleiste und Mauszeiger das Bild zusammen – immer
nur für die Bereiche, die sich geändert haben – und kopiert das Ergebnis in den
Bildspeicher. Er verteilt außerdem Tastatur und Maus an die Fenster.

## Aufbau des Quellcodes

| Datei | Aufgabe |
| --- | --- |
| `kernel/boot.c`, `kernel/efi.h` | Start über UEFI, Übergabe an den Kernel |
| `kernel/main.c` | Initialisierung, Leerlauf |
| `kernel/lowlevel.asm` | Interrupt-Einsprünge, Task-Wechsel |
| `kernel/interrupts.c` | GDT, IDT, Ausnahmen, Local APIC, Systemtakt |
| `kernel/time.c` | Zeitmessung, Datum und Uhrzeit |
| `kernel/memory.c` | Heap, Seitenvergabe, Seitentabellen |
| `kernel/task.c` | Tasks und Scheduler |
| `kernel/console.c`, `kernel/serial.c` | Konsolen, `kprintf`, serielle Schnittstelle |
| `kernel/keyboard.c`, `kernel/ps2.c` | Tastaturbelegungen, PS/2-Tastatur und -Maus |
| `kernel/pci.c`, `kernel/usb.c`, `kernel/hid.c` | PCI-Bus, xHCI-Treiber (Hubs, Tastatur, Maus, Massenspeicher), Auswertung der HID-Berichte |
| `kernel/nvme.c`, `kernel/ahci.c` | interne Datenträger: NVMe-SSDs und SATA-Platten |
| `kernel/install.c` | Start-Assistent: Einrichten auf der internen Platte über die Firmware |
| `kernel/block.c`, `kernel/fat.c` | Blockgeräte samt Schreibsperre für interne Platten, FAT32 |
| `kernel/fs.c`, `kernel/storage.c` | eigene Dateien im Arbeitsspeicher; Sicherung auf dem HelloOS-Datenträger |
| `kernel/e1000.c`, `kernel/igc.c` | Treiber für Intel-Netzwerkkarten: e1000-Familie; I225/I226 und Verwandte |
| `kernel/net.c`, `kernel/tcp.c` | ARP, IPv4, ICMP, UDP, DHCP, DNS; TCP |
| `kernel/crypto_*.c` | SHA-2, HMAC/HKDF, ChaCha20-Poly1305, AES-CTR, X25519, Ed25519, RSA, ECDSA, ECDH |
| `kernel/x509.c`, `kernel/tls_roots.*` | Zertifikate lesen und prüfen, eingebaute Wurzelzertifikate |
| `kernel/tls.c`, `kernel/http.c`, `kernel/url.c` | TLS 1.3, HTTP-Client, Web-Adressen |
| `kernel/html.c`, `kernel/browser.c` | HTML zerlegen und als Text setzen; das Browser-Fenster |
| `kernel/css.c` | Stylesheets lesen und auf Elemente anwenden |
| `kernel/image*.c`, `kernel/inflate.c` | Bilder entschlüsseln (PNG, JPEG, GIF) und verkleinern |
| `kernel/ssh.c`, `kernel/sftp.c` | SSH-Client, Dateiübertragung per SFTP |
| `kernel/json.c` | JSON lesen und schreiben |
| `kernel/claude.c`, `kernel/claude_app.c` | Anbindung an die Claude-API, Chatfenster |
| `kernel/update.c` | Selbstaktualisierung |
| `kernel/gfx.c`, `kernel/font_data.c` | Zeichenfunktionen, Rasterschrift (Terminal, Editor, Eingabefelder) |
| `kernel/truetype.c`, `kernel/font.c`, `fonts/` | TrueType-Renderer, Schriftverwaltung mit Zwischenspeicher, die eingebauten Schriftdateien (DejaVu Sans, Lizenz liegt bei) |
| `kernel/gui.c` | Compositor, Fensterverwaltung, Taskleiste, Startmenü |
| `kernel/terminal.c`, `kernel/shell.c` | Terminalfenster und Kommandozeile |
| `kernel/editor.c`, `kernel/textedit.c`, `kernel/transfer.c` | Texteditor und sein Textfeld, Fenster zur Dateiübertragung |
| `kernel/sound.c` | Ton: Treiber für Intel HD Audio, Mischer, errechnete Systemklänge |
| `kernel/sysmon.c`, `kernel/paint.c`, `kernel/about.c` | Systemmonitor, Malprogramm, Dialoge |
| `tools/mkimage.py` | baut das Disk-Image (GPT, FAT32) |
| `tools/update-tool.py` | Schlüssel erzeugen, Update-Pakete signieren und prüfen |
| `tools/mkfont.py` | erzeugt `font_data.c` aus einer TrueType-Schrift |
| `tools/mkroots.py` | erzeugt `tls_roots.bin` aus einer Liste von Wurzelzertifikaten |
| `tools/smoke-test.sh` | automatischer Boot-Test in QEMU |
| `tests/crypto_test.py` | Kryptografie gegen `hashlib` und `cryptography` prüfen |
| `tests/web_test.c` | Web-Adressen und HTML-Umsetzung prüfen, auch mit beschädigten Seiten |
| `tests/block_test.c` | Schreibsperre für interne Platten prüfen |
| `tests/hid_test.c` | Auswertung der HID-Berichte prüfen (Mäuse, Tastaturen, Funkempfänger) |
| `tests/image_test.c`, `tests/image_test.py` | Bild-Dekoder gegen eine Referenz prüfen, auch mit beschädigten Dateien |
| `tests/css_test.c` | Stylesheets prüfen: Auswahlen, Rangfolge, Farben, Variablen, `@media`, beschädigte Eingaben |
| `tests/font_test.c`, `tests/font_test.py` | Schriftrenderer Zeichen für Zeichen gegen FreeType prüfen, auch mit beschädigten Schriftdateien |

## Grenzen und Ideen zum Weiterbauen

HelloOS ist ein Lernsystem. Alles läuft im Kernel-Modus in einem gemeinsamen
Adressraum; es gibt keinen Speicherschutz und keine Programme von außerhalb
(auch keine Windows-Programme). Die Kryptografie ist geprüft,
aber nicht gegen Seitenkanäle gehärtet, und der Zufallsgenerator ist einfach
gehalten. Naheliegende nächste Schritte:

- Claudes Antworten Wort für Wort anzeigen (Streaming), Gespräche speichern
- Unterordner für eigene Dateien, ganze Ordner übertragen, Anmeldung mit Schlüsseldatei
- im Editor: Markieren, Kopieren und Einfügen, Suchen, Rückgängig
- weitere Netzwerkkarten (Realtek), WLAN
- User-Modus (Ring 3) mit Systemaufrufen und getrennten Adressräumen
- Programme aus Dateien laden (ELF), mehrere Prozessorkerne
- Symbole auf dem Desktop, ein Dateimanager, Fenster an den Kanten ziehen und minimieren
- im Browser: Bilder neben dem Text, Abstände und Rahmen (auch Tabellenlinien), WebP-Bilder, Lesezeichen (JavaScript bleibt außer Reichweite)
- echte Schrift auch in Terminal, Editor und Eingabefeldern; Hinting für schärfere kleine Schrift

## Die ältere 32-Bit-Version

In [`legacy-bios32/`](legacy-bios32/) liegt die erste Fassung von HelloOS: ein
32-Bit-System mit eigenem BIOS-Bootloader und Textoberfläche. Sie läuft auf
PCs mit klassischem BIOS (oder CSM), nicht aber auf reinen UEFI-Rechnern, und
hat weder Netzwerk noch Updates.

## Schrift

Die Schrift der Oberfläche ist eine gerasterte Fassung von DejaVu Sans Mono
(freie Lizenz, siehe Kopf von `kernel/font_data.c`). Alles andere in diesem
Verzeichnis ist für HelloOS geschrieben.
