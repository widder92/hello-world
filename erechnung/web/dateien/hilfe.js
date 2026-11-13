// Hilfe: der Ablauf in Kürze, die Besonderheiten von REWE und EDEKA und was
// die häufigsten Fehlermeldungen bedeuten.
import { html, stamm } from './basis.js';

const REGELN = [
  ['BR-DE-15', 'Die Käuferreferenz (BT-10) fehlt. In einer XRechnung ist sie Pflicht. Das Programm trägt ersatzweise die GLN des Kunden ein.'],
  ['BR-DE-2 bis BR-DE-7', 'Kontaktdaten des Rechnungsstellers fehlen: Ansprechpartner, Telefon, E-Mail. Sie stehen unter Einstellungen.'],
  ['PEPPOL-EN16931-R010 / R020', 'Die elektronische Adresse von Käufer (R010) oder Verkäufer (R020) fehlt. Üblich ist die E-Mail-Adresse.'],
  ['BR-CO-10 bis BR-CO-17', 'Rechenfehler: Summen oder Steuerbeträge passen nicht zusammen, oft um einen Cent. Dieses Programm rechnet nach den Regeln der Norm, dort kann das nicht passieren.'],
  ['BR-CL-23', 'Die Mengeneinheit ist kein genormter Code. Erlaubt sind Codes wie H87 (Stück) oder KGM (Kilogramm), nicht „Stk“ oder „kg“.'],
  ['BR-S-08, BR-S-09', 'Die Steueraufschlüsselung je Steuersatz stimmt nicht mit den Positionen überein.'],
  ['„GLN fehlt“, „BT-46“, „BT-71“', 'Der Handel verlangt die GLN des Rechnungsempfängers (BT-46) und des Markts (BT-71) in eigenen Feldern. Im Firmennamen oder Freitext zählt sie nicht.'],
  ['„entspricht nicht PDF/A-3“', 'Das PDF ist kein Archiv-PDF der verlangten Art. Dieses Programm erzeugt gültiges PDF/A-3.'],
];

export function Hilfeseite() {
  return html`
    <div class="kopf"><div><h1>Hilfe</h1></div></div>

    <div class="karte">
      <h2>So geht eine Rechnung hinaus</h2>
      <ol class="schritte">
        <li><span><b>Neue Rechnung</b> oder <b>Import aus Lexoffice</b> – Kunde und Markt auswählen, Lieferdatum, Bestell- und Lieferscheinnummer eintragen, Positionen erfassen.</span></li>
        <li><span><b>Prüfung beachten</b> – rechts steht laufend, ob etwas fehlt. Ein Klick auf eine Meldung führt zum Feld. Rot muss behoben werden, Gelb sollte.</span></li>
        <li><span><b>Fertigstellen</b> – die Rechnung bekommt ihre Nummer und wird als E-Rechnung abgelegt. Danach ist sie nicht mehr änderbar.</span></li>
        <li><span><b>Senden</b> – per E-Mail an das Rechnungspostfach des Kunden. Empfänger, Betreff und Anhang sind vorbereitet.</span></li>
        <li><span><b>Zahlung vermerken</b> – wenn das Geld da ist, „Als bezahlt vermerken“.</span></li>
      </ol>
    </div>

    <div class="karte">
      <h2>Warum Rechnungen aus Lexware Office (lexoffice) abgelehnt werden</h2>
      <p>Meist ist nicht die Datei kaputt, sondern dem Handel fehlen Angaben für seine automatische Verarbeitung: die <b>GLN</b> von Rechnungsempfänger und Markt, der <b>Markt mit Anschrift</b>, die <b>Bestell-</b> und <b>Lieferscheinnummer</b>, die <b>USt-IdNr.</b> des Kunden, oft auch die <b>GTIN</b> der Artikel. In Lexware Office stehen diese Angaben häufig nur im Freitext oder in der Anschrift – dort findet sie die Maschine des Handels nicht. Dazu kommen Versandregeln – bei REWE etwa die vorherige Anmeldung der Absenderadresse.</p>
      <h3>Zusammenspiel mit Lexware Office</h3>
      <p>Lexware Office kann für Buchhaltung, Bank und Steuer weiterlaufen. Es gibt zwei Wege:</p>
      <ul>
        <li><b>Weiter in Lexware Office schreiben, hier versenden (empfohlen):</b> Die Rechnung entsteht wie gewohnt in Lexware Office – mit dessen Rechnungsnummer und gleich richtig verbucht. Unter <a href="#/pruefen">Import aus Lexoffice</a> wird sie eingelesen: als heruntergeladene Datei oder direkt über die Verbindung zu Lexware Office. Das Programm liest GLN, Lieferschein- und Bestellnummer auch aus dem Freitext und der Anschrift heraus, ergänzt aus seinen Stammdaten, was dem Handel fehlt (GLN, Markt mit Anschrift, USt-IdNr., GTIN), prüft die Rechnung und verschickt sie als vollständige E-Rechnung. Aus Lexware Office selbst wird diese Rechnung dann nicht mehr verschickt.</li>
        <li><b>Hier schreiben:</b> Rechnungen an REWE und EDEKA entstehen direkt in diesem Programm und werden in Lexware Office als Einnahme-Beleg erfasst (die PDF-Datei aus dem Ordner „Rechnungen“ hochladen). Dann getrennte Nummernkreise verwenden, damit keine Rechnungsnummer doppelt vorkommt.</li>
      </ul>
      <p>Damit der Import ohne Nacharbeit läuft, einmal die Stammdaten pflegen: beim Kunden die GLN und die Märkte mit GLN und Anschrift, bei den Artikeln die GTIN. Kunden und Artikel lassen sich auf der Importseite aus Lexware Office holen; eine fertige Liste von Kunden und Märkten lässt sich unter <a href="#/kunden">Kunden</a> mit „Aus Datei einlesen“ übernehmen.</p>
    </div>

    ${stamm.profile.map((p) => html`<div class="karte" key=${p.id}>
      <h2>${p.name}</h2>
      <ul>${p.hinweise.map((h) => html`<li style="margin-bottom:.45rem">${h}</li>`)}</ul>
      <details><summary>Quellen</summary><ul class="klein">${p.quellen.map((q) => html`<li style="overflow-wrap:anywhere">${q}</li>`)}</ul></details>
    </div>`)}

    <div class="karte">
      <h2>Was Fehlermeldungen der Empfänger bedeuten</h2>
      <table><tbody>${REGELN.map(([r, t]) => html`<tr><td class="nowrap"><b>${r}</b></td><td>${t}</td></tr>`)}</tbody></table>
    </div>

    <div class="karte">
      <h2>Gut zu wissen</h2>
      <ul>
        <li><b>ZUGFeRD</b> ist ein PDF, in dem die Rechnung zusätzlich als XML steckt – lesbar für Mensch und Maschine. <b>XRechnung</b> ist die reine XML-Datei. Beide folgen der europäischen Norm EN 16931.</li>
        <li><b>GLN</b> (Global Location Number, 13 Ziffern) bezeichnet eine Firma oder einen Ort, <b>GTIN</b> (früher EAN) einen Artikel. Beide vergibt GS1 Germany. Die GLN der Kunden und Märkte nennt der Kunde.</li>
        <li><b>Was das Programm prüft:</b> Jede Rechnung wird vor dem Fertigstellen geprüft – Pflichtangaben, Rechenwerk, Prüfziffern und die Vorgaben des Kunden; mit Fehlern lässt sie sich nicht fertigstellen. Unter <a href="#/kunden">Kunden</a> prüft „Stammdaten prüfen“ außerdem alle Kunden und Märkte auf einmal: unvollständige Anschriften, falsche Prüfziffern bei GLN, GTIN und IBAN, falsch geschriebene PLZ, USt-IdNr. und E-Mail-Adressen, doppelt vergebene GLN. <b>Nicht</b> prüfen kann das Programm, ob eine GLN wirklich zu diesem Markt gehört – ein Zahlendreher ergibt fast immer eine ungültige Prüfziffer und fällt auf, eine vertauschte, aber gültige GLN nicht. Im Zweifel beim Kunden nachfragen.</li>
        <li><b>Zusatzfunktionen:</b> Angebote, Lieferscheine, Serienrechnungen, Abschlagsrechnungen, offene Posten mit Mahnungen und eine Auswertung der Umsätze lassen sich unter <a href="#/einstellungen">Einstellungen</a>, Abschnitt Zusatzfunktionen, einzeln einschalten. Ausgeschaltet sind sie nicht zu sehen; zum Schreiben von Rechnungen braucht es keine davon. Eine Buchhaltung ist das Programm nicht: Ausgaben, Bank und die Meldungen an das Finanzamt bleiben bei der Buchhaltungssoftware oder der Steuerkanzlei.</li>
        <li><b>DATEV:</b> Unter <a href="#/">Rechnungen</a> erzeugt „DATEV-Export“ zwei Dateien: das <b>Belegpaket</b> mit den E-Rechnungen für DATEV Unternehmen online (Übertragung mit dem Programm DATEV Belegtransfer) und den <b>Buchungsstapel</b> im DATEV-Format für die Steuerkanzlei. Berater- und Mandantennummer sowie die Konten stehen unter <a href="#/einstellungen">Einstellungen</a>, Abschnitt DATEV. Vorher mit der Kanzlei klären, wer bucht: Kommen die Buchungen schon aus Lexware Office, dürfen sie nicht zusätzlich von hier kommen.</li>
        <li><b>Fehler nach dem Versand:</b> Eine fertige Rechnung wird nicht geändert, sondern mit „Stornieren“ durch eine Rechnungskorrektur aufgehoben und neu geschrieben.</li>
        <li><b>Aufbewahrung:</b> Rechnungen müssen acht Jahre unverändert aufbewahrt werden. Bei E-Rechnungen zählt die Datei selbst, nicht ein Ausdruck.</li>
        <li><b>Beenden:</b> Wird das Fenster geschlossen, ist das Programm beendet. Die Daten sind dann längst gespeichert – jede Änderung wird sofort auf dem Rechner abgelegt.</li>
        <li><b>Grenzen:</b> Das Programm prüft die Regeln, an denen Rechnungen in der Praxis scheitern, und wird laufend mit einem amtlichen Prüfprogramm abgeglichen. Es ersetzt keine Steuerberatung, und die Vorgaben der Handelsunternehmen können sich ändern.</li>
      </ul>
      <p class="leise klein">Marktrechnung ${stamm.version} · Schrift im PDF: Liberation Sans (SIL Open Font License) · Oberfläche mit Preact und htm (MIT / Apache 2.0)</p>
    </div>`;
}
