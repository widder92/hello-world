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
        <li><span><b>Neue Rechnung</b> – Kunde und Markt auswählen, Lieferdatum, Bestell- und Lieferscheinnummer eintragen, Positionen erfassen.</span></li>
        <li><span><b>Prüfung beachten</b> – rechts steht laufend, ob etwas fehlt. Ein Klick auf eine Meldung führt zum Feld. Rot muss behoben werden, Gelb sollte.</span></li>
        <li><span><b>Fertigstellen</b> – die Rechnung bekommt ihre Nummer und wird als E-Rechnung abgelegt. Danach ist sie nicht mehr änderbar.</span></li>
        <li><span><b>Senden</b> – per E-Mail an das Rechnungspostfach des Kunden. Empfänger, Betreff und Anhang sind vorbereitet.</span></li>
        <li><span><b>Zahlung vermerken</b> – wenn das Geld da ist, „Als bezahlt vermerken“.</span></li>
      </ol>
    </div>

    <div class="karte">
      <h2>Warum Rechnungen aus Lexware Office (lexoffice) abgelehnt werden</h2>
      <p>Meist ist nicht die Datei kaputt, sondern dem Handel fehlen Angaben für seine automatische Verarbeitung: die <b>GLN</b> von Rechnungsempfänger und Markt, die <b>Bestell-</b> und <b>Lieferscheinnummer</b>, bei REWE die <b>Kostenstelle</b>, oft auch die <b>GTIN</b> der Artikel. Dazu kommen Versandregeln – bei REWE etwa die vorherige Anmeldung der Absenderadresse.</p>
      <p>Unter <a href="#/pruefen">Fremde Rechnung prüfen</a> lässt sich eine abgelehnte Rechnung einlesen. Das Programm sagt, was fehlt, und kann sie als Entwurf übernehmen, um die Angaben zu ergänzen.</p>
      <h3>Zusammenspiel mit Lexware Office</h3>
      <p>Lexware Office kann für Buchhaltung, Bank und Steuer weiterlaufen. Rechnungen an REWE und EDEKA werden dann hier geschrieben und verschickt und in Lexware Office als Einnahme-Beleg erfasst (die PDF-Datei aus dem Ordner „Rechnungen“ hochladen). Wichtig: getrennte Nummernkreise, damit keine Rechnungsnummer doppelt vorkommt.</p>
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
        <li><b>Fehler nach dem Versand:</b> Eine fertige Rechnung wird nicht geändert, sondern mit „Stornieren“ durch eine Rechnungskorrektur aufgehoben und neu geschrieben.</li>
        <li><b>Aufbewahrung:</b> Rechnungen müssen acht Jahre unverändert aufbewahrt werden. Bei E-Rechnungen zählt die Datei selbst, nicht ein Ausdruck.</li>
        <li><b>Grenzen:</b> Das Programm prüft die Regeln, an denen Rechnungen in der Praxis scheitern, und wird laufend mit einem amtlichen Prüfprogramm abgeglichen. Es ersetzt keine Steuerberatung, und die Vorgaben der Handelsunternehmen können sich ändern.</li>
      </ul>
      <p class="leise klein">Marktrechnung ${stamm.version} · Schrift im PDF: Liberation Sans (SIL Open Font License) · Oberfläche mit Preact und htm (MIT / Apache 2.0)</p>
    </div>`;
}
