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
- **Grafische Oberfläche**: verschiebbare Fenster, Taskleiste mit Startmenü,
  Mauszeiger, geglättete Schrift – gezeichnet direkt in den Bildspeicher der
  Grafikkarte
- **Programme**: Browser, Terminal mit Shell (mehrere gleichzeitig), Texteditor,
  Dateiübertragung, Systemmonitor, Malprogramm, Info- und Ausschalten-Dialog
- **Browser**: zeigt Webseiten als Text mit Links und Formularen, meldet sich
  an (Cookies) und prüft bei https den Server – ohne JavaScript, Gestaltung
  und Bilder
- **Eigene Dateien bleiben erhalten**: Sie werden auf dem USB-Stick gesichert,
  von dem HelloOS startet, und sind dort auch unter Windows zu sehen
- **Dateien mit einem Server austauschen** über SFTP – verschlüsselt, mit
  Prüfung des Serverschlüssels; als Fenster mit zwei Spalten oder im Terminal
- **Eingabegeräte**: USB-Tastaturen und -Mäuse (xHCI, auch An- und Abstecken
  im Betrieb) sowie PS/2, wie es viele Notebook-Tastaturen intern noch nutzen;
  deutsche und US-Tastaturbelegung
- **Netzwerk**: Treiber für Intel-Netzwerkkarten, eigener Stack mit ARP, IPv4,
  ICMP, UDP, DHCP, DNS und TCP, dazu HTTP und HTTPS (TLS 1.3 mit
  Zertifikatsprüfung)
- **Claude eingebaut**: ein Chatfenster und der Befehl `claude` – Claude
  beantwortet Fragen und kann in HelloOS nachsehen und Befehle ausführen
- **Updates**: HelloOS sucht regelmäßig nach neuen Versionen, prüft deren
  Signatur, zeigt, was neu ist, und installiert sie nach deiner Zustimmung auf
  dem USB-Stick, von dem es gestartet wurde
- 34 Shell-Befehle
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
Wirtsrechner. `make check` braucht zusätzlich das Python-Paket `cryptography`
und einen gewöhnlichen C-Compiler (`cc`).

Liegt die Firmware an einem ungewöhnlichen Ort: `make run OVMF=/pfad/zu/OVMF.fd`.
Unter macOS liefert Homebrew alles Nötige (`brew install llvm lld nasm qemu`);
`clang` und `lld-link` gibt man dann mit `make CC=... LD=...` an.

## Auf einem echten PC starten – auch neben Windows

> HelloOS wurde bisher nur in QEMU getestet, nicht auf echter Hardware. Es
> schreibt nur auf den USB-Stick, von dem es gestartet wurde (und das nur bei
> einem Update) – ein Versuch ist ungefährlich, aber es kann sein, dass es auf
> einem bestimmten Rechner nicht startet.

1. USB-Stick vorbereiten – eine der beiden Möglichkeiten:
   - Stick mit FAT32 formatieren und `build/BOOTX64.EFI` als
     `EFI/BOOT/BOOTX64.EFI` daraufkopieren (geht mit jedem Betriebssystem), oder
   - das fertige Image auf den Stick schreiben, z. B. unter Linux mit
     `sudo dd if=build/helloos.img of=/dev/sdX bs=1M conv=fsync`
     (**Achtung:** `/dev/sdX` muss wirklich der Stick sein, er wird überschrieben).
2. Im UEFI-Setup des PCs **Secure Boot ausschalten** – HelloOS ist nicht signiert.
   **Vorher unbedingt lesen:** den Hinweis zu BitLocker weiter unten.
3. Vom Stick starten (Boot-Menü, meist F12, F11, F8 oder Esc).

### Neben Windows

Der USB-Stick **ist** die Installation neben Windows: Steckt er und wählst du
ihn im Boot-Menü, startet HelloOS; sonst startet Windows wie immer. HelloOS
liest und schreibt ausschließlich auf dem Stick – die Festplatte mit Windows
fasst es nie an, es hat nicht einmal Treiber dafür. Deine Dateien, Updates und
gemerkten Server liegen alle auf dem Stick und wandern mit ihm mit.

> **BitLocker / Geräteverschlüsselung:** Auf den meisten PCs mit Windows 11
> (und vielen mit Windows 10) ist die Festplatte verschlüsselt, und der
> Schlüssel ist an Secure Boot gebunden. Schaltest du Secure Boot aus,
> verlangt Windows beim nächsten Start den **BitLocker-Wiederherstellungs-
> schlüssel** (48 Ziffern). Besorge ihn dir vorher – er steht in deinem
> Microsoft-Konto unter „Geräte" → „BitLocker-Schlüssel" – oder halte den
> Schutz in Windows vor der Umstellung an („BitLocker verwalten" → „Schutz
> anhalten"). Ohne diesen Schlüssel kommst du nicht mehr an deine Daten.
> Schaltest du Secure Boot wieder ein, ist alles wie vorher.

Eine feste Installation **auf** der Festplatte, mit Auswahl beim Einschalten
ohne Stick, gibt es bewusst nicht:

- HelloOS kann interne Festplatten und SSDs gar nicht ansprechen. Von dort
  gestartet, könnte es weder Dateien speichern noch sich aktualisieren.
- Dafür müsste man den Startbereich des Windows-PCs verändern. Geht dabei
  etwas schief, startet womöglich auch Windows nicht mehr – ein Risiko, das
  ein ungetestetes Hobby-System nicht wert ist.

Was dabei zu beachten ist:

- Stick, Tastatur und Maus **direkt am Rechner** einstecken. USB-Hubs (auch
  die in Monitoren, Docks oder Tastaturen) werden nicht unterstützt.
- Unterstützt werden gewöhnliche USB-Tastaturen und -Mäuse (HID
  „Boot-Protokoll"). Bluetooth-Geräte und Touchpads, die nur über I²C
  angebunden sind, funktionieren nicht – dann hilft eine USB-Maus oder die
  Bedienung per Tastatur.
- **Netzwerk gibt es nur per Kabel** und nur mit Intel-Netzwerkkarten der
  e1000-Familie. Getestet ist der Treiber mit den beiden Modellen, die QEMU
  emuliert; die verwandten Karten in echten PCs („Intel Ethernet Connection
  I217/I218/I219") erkennt er, ob sie damit laufen, ist ungeprüft. WLAN,
  Realtek-Karten und Intels 2,5-Gigabit-Karten (I225/I226) werden nicht
  unterstützt. `net` zeigt, ob eine Karte erkannt wurde.
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
| Fenster schließen | ✕ in der Titelleiste | Alt+F4 |
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
| `disk` | zeigt angeschlossene USB-Datenträger |
| `ls`, `cat <datei>`, `rm <datei>` | Dateien auflisten, anzeigen, löschen |
| `edit [datei]` | Datei im Texteditor öffnen (oder neu anlegen) |
| `sha256 <datei>` | Prüfsumme einer Datei |
| `write <datei> <text>` | Datei (über)schreiben |
| `append <datei> <text>` | Zeile anhängen |
| `echo <text>` | Text ausgeben |
| `keymap [de\|us]` | Tastaturbelegung anzeigen oder umschalten (Standard: `de`) |
| `history`, `clear` | Befehlsverlauf, Terminal leeren |
| `panic` | löst absichtlich eine CPU-Ausnahme aus |
| `reboot`, `shutdown` | Neustart, Herunterfahren |

Pfeil hoch/runter blättert im Verlauf, Strg+C verwirft die Eingabe,
Strg+L leert das Terminal.

## Browser

![Der Browser mit der Bearbeitungsseite eines WordPress-Beitrags](docs/screenshot-browser.png)

Startmenü → „Browser", F8 oder `browse adresse` im Terminal. Es ist ein
**einfacher Browser**, in der Art eines Textbrowsers: Er holt eine Seite, lässt
Gestaltung, Bilder und Skripte weg und zeigt, was übrig bleibt – Überschriften,
Text, Listen, Tabellen (zeilenweise), Links und Formulare.

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

Was er **nicht** kann – und das ist bei heutigen Webseiten viel:

- **kein JavaScript.** Seiten und Funktionen, die darauf beruhen, bleiben leer,
  reagieren nicht oder sagen selbst, dass sie JavaScript brauchen.
- keine Gestaltung (CSS), keine Bilder, keine Videos, keine Schriften außer
  der einen eingebauten; nicht-lateinische Schrift erscheint als „?"
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
sofort auf dem USB-Stick gesichert, von dem HelloOS gestartet wurde. Beim
nächsten Start ist alles wieder da.

- Auf dem Stick liegen die Dateien im Ordner **`DATEIEN`**. Steckst du den
  Stick an einen Windows-PC, kannst du sie dort ganz normal öffnen, kopieren
  und neue dazulegen – HelloOS lädt sie beim nächsten Start mit.
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
  USB-Sticks, von dem HelloOS startet – einfach den Schlüssel als einzige
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
3. **Installieren.** Auf dem USB-Stick, von dem HelloOS gestartet wurde, wird
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

Von da an baut der Workflow `.github/workflows/helloos-update.yml` bei jeder
Änderung unter `os/` auf dem Zweig `master` ein signiertes Paket und legt es
auf den Branch `helloos-updates`. Dort sucht HelloOS standardmäßig
(`UPDATE_URL` im `Makefile`). Der Workflow ist bisher nicht in der Praxis
gelaufen.

Es geht auch von Hand: `make update-package` erzeugt
`build/update/helloos.upd` (samt dem Text aus `NEUES.txt`) und
`helloos.version`; beide Dateien müssen dann unter der Adresse erreichbar
sein, die beim Bauen als `UPDATE_URL` galt.

Die Build-Nummer ist die Anzahl der Commits (`git rev-list --count HEAD`).
Updates funktionieren nur, wenn HelloOS von einem USB-Stick läuft, der direkt
am Rechner steckt – interne Festplatten kann es nicht ansprechen.

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
| `kernel/pci.c`, `kernel/usb.c` | PCI-Bus, xHCI-Treiber (Tastatur, Maus, Massenspeicher) |
| `kernel/block.c`, `kernel/fat.c` | Blockgeräte, FAT32 (auch lange Dateinamen, Anlegen, Löschen) |
| `kernel/fs.c`, `kernel/storage.c` | eigene Dateien im Arbeitsspeicher, Sicherung auf dem Stick |
| `kernel/e1000.c` | Treiber für Intel-Netzwerkkarten |
| `kernel/net.c`, `kernel/tcp.c` | ARP, IPv4, ICMP, UDP, DHCP, DNS; TCP |
| `kernel/crypto_*.c` | SHA-2, HMAC/HKDF, ChaCha20-Poly1305, AES-CTR, X25519, Ed25519, RSA, ECDSA, ECDH |
| `kernel/x509.c`, `kernel/tls_roots.*` | Zertifikate lesen und prüfen, eingebaute Wurzelzertifikate |
| `kernel/tls.c`, `kernel/http.c`, `kernel/url.c` | TLS 1.3, HTTP-Client, Web-Adressen |
| `kernel/html.c`, `kernel/browser.c` | HTML zerlegen und als Text setzen; das Browser-Fenster |
| `kernel/ssh.c`, `kernel/sftp.c` | SSH-Client, Dateiübertragung per SFTP |
| `kernel/json.c` | JSON lesen und schreiben |
| `kernel/claude.c`, `kernel/claude_app.c` | Anbindung an die Claude-API, Chatfenster |
| `kernel/update.c` | Selbstaktualisierung |
| `kernel/gfx.c`, `kernel/font_data.c` | Zeichenfunktionen, Schrift |
| `kernel/gui.c` | Compositor, Fensterverwaltung, Taskleiste, Startmenü |
| `kernel/terminal.c`, `kernel/shell.c` | Terminalfenster und Kommandozeile |
| `kernel/editor.c`, `kernel/textedit.c`, `kernel/transfer.c` | Texteditor und sein Textfeld, Fenster zur Dateiübertragung |
| `kernel/sysmon.c`, `kernel/paint.c`, `kernel/about.c` | Systemmonitor, Malprogramm, Dialoge |
| `tools/mkimage.py` | baut das Disk-Image (GPT, FAT32) |
| `tools/update-tool.py` | Schlüssel erzeugen, Update-Pakete signieren und prüfen |
| `tools/mkfont.py` | erzeugt `font_data.c` aus einer TrueType-Schrift |
| `tools/mkroots.py` | erzeugt `tls_roots.bin` aus einer Liste von Wurzelzertifikaten |
| `tools/smoke-test.sh` | automatischer Boot-Test in QEMU |
| `tests/crypto_test.py` | Kryptografie gegen `hashlib` und `cryptography` prüfen |
| `tests/web_test.c` | Web-Adressen und HTML-Umsetzung prüfen, auch mit beschädigten Seiten |

## Grenzen und Ideen zum Weiterbauen

HelloOS ist ein Lernsystem. Alles läuft im Kernel-Modus in einem gemeinsamen
Adressraum; es gibt keinen Speicherschutz und keine Programme von außerhalb
(auch keine Windows-Programme). Die Kryptografie ist geprüft,
aber nicht gegen Seitenkanäle gehärtet, und der Zufallsgenerator ist einfach
gehalten. Naheliegende nächste Schritte:

- Claudes Antworten Wort für Wort anzeigen (Streaming), Gespräche speichern
- Unterordner für eigene Dateien, ganze Ordner übertragen, Anmeldung mit Schlüsseldatei
- im Editor: Markieren, Kopieren und Einfügen, Suchen, Rückgängig
- weitere Netzwerkkarten (Realtek, Intel I225), USB-Hubs
- User-Modus (Ring 3) mit Systemaufrufen und getrennten Adressräumen
- Programme aus Dateien laden (ELF), mehrere Prozessorkerne
- Fenster in der Größe ändern und maximieren, Symbole auf dem Desktop, ein Dateimanager
- im Browser: Bilder, einfache Gestaltung, Lesezeichen (JavaScript bleibt außer Reichweite)

## Die ältere 32-Bit-Version

In [`legacy-bios32/`](legacy-bios32/) liegt die erste Fassung von HelloOS: ein
32-Bit-System mit eigenem BIOS-Bootloader und Textoberfläche. Sie läuft auf
PCs mit klassischem BIOS (oder CSM), nicht aber auf reinen UEFI-Rechnern, und
hat weder Netzwerk noch Updates.

## Schrift

Die Schrift der Oberfläche ist eine gerasterte Fassung von DejaVu Sans Mono
(freie Lizenz, siehe Kopf von `kernel/font_data.c`). Alles andere in diesem
Verzeichnis ist für HelloOS geschrieben.
