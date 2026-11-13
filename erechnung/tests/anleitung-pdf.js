// Erzeugt die bebilderte Anleitung als PDF: aus dem Text der eingebauten
// Anleitung (web/dateien/anleitung.json) und den Bildschirmfotos, die der
// Durchlauf durch die Oberfläche (tests/oberflaeche.js) ablegt.
//
// Aufruf:
//   node tests/anleitung-pdf.js BILDERORDNER [docs/Anleitung.pdf]
//
// Fehlt ein Bild, bleibt das Kapitel ohne Bild; das Skript nennt die Namen.
const fs = require('fs');
const path = require('path');
const { chromium } = require('playwright');

const [bilder, ziel = path.join(__dirname, '..', 'docs', 'Anleitung.pdf')] = process.argv.slice(2);
if (!bilder) { console.error('Aufruf: node tests/anleitung-pdf.js BILDERORDNER [Ziel.pdf]'); process.exit(2); }

const wurzel = path.join(__dirname, '..');
const anleitung = JSON.parse(fs.readFileSync(path.join(wurzel, 'web', 'dateien', 'anleitung.json'), 'utf8'));
const symbol = fs.readFileSync(path.join(wurzel, 'web', 'dateien', 'symbol.svg'), 'utf8');
const version = (/Nummer = "([^"]+)"/.exec(fs.readFileSync(path.join(wurzel, 'internal', 'version', 'version.go'), 'utf8')) || [])[1] || '';

// Von hohen Bildern kommt nur der obere Teil aufs Blatt (Höhe in Bildpunkten).
const HOEHE = { '05-rechnung': 1010, '03-kunde': 900, '11-pruefen': 860, 'a-zusatz': 620 };
const UNTERSCHRIFT = {
  '01-erste-schritte': 'Nach dem ersten Start: die Liste „Erste Schritte“',
  'a-firma': 'Einstellungen, Abschnitt „Eigene Firma“',
  'a-email': 'Einstellungen, Abschnitt „E-Mail-Versand“',
  '03-kunde': 'Ein Kunde mit Vorlage, GLN und Märkten',
  'a-artikel': 'Die Seite „Artikel“',
  'a-rewe': 'Seite „Hilfe“: was REWE verlangt',
  '05-rechnung': 'Eine Rechnung im Entwurf – rechts die laufende Prüfung',
  '07-fertigstellen': 'Vor dem Fertigstellen fragt das Programm nach',
  '08-senden': 'Der Versand ist vorbereitet: Empfänger, Betreff, Anhang',
  '11-pruefen': 'Import aus Lexoffice: Datei hineinziehen, Ergebnis ansehen',
  '09-ansicht': 'Eine fertige Rechnung – rechts unten „Weitere Schritte“',
  'a-zusatz': 'Einstellungen, Abschnitt „Zusatzfunktionen“',
  '19-datev-export': 'Der DATEV-Export',
  'a-sicherung': 'Einstellungen, Abschnitt „Daten und Sicherung“',
  '23-assistent': 'Der Assistent (Taste F1) zeigt, was auf der Seite zu tun ist',
};

const sicher = (t) => String(t).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
const monat = new Date().toLocaleDateString('de-DE', { month: 'long', year: 'numeric' });

function seite(bildDaten) {
  const kapitel = anleitung.kapitel.map((k, i) => {
    const b = bildDaten[k.bild];
    return `<section class="kapitel">
      <div class="kopf"><span class="nr">${i + 1}</span><div><h2>${sicher(k.titel)}</h2><p class="kurz">${sicher(k.kurz)}</p></div></div>
      ${b ? `<figure><img src="${b}" alt=""><figcaption>${sicher(UNTERSCHRIFT[k.bild] || '')}</figcaption></figure>` : ''}
      <ol class="schritte">${k.schritte.map((s) => `<li><span>${sicher(s)}</span></li>`).join('')}</ol>
      ${(k.hinweise || []).map((h) => `<p class="tipp"><b>Gut zu wissen:</b> ${sicher(h)}</p>`).join('')}
    </section>`;
  }).join('\n');
  return `<!doctype html><html lang="de"><head><meta charset="utf-8"><title>${sicher(anleitung.titel)}</title><style>
    @page { size: A4; margin: 18mm 17mm 20mm; }
    * { box-sizing: border-box; }
    body { margin: 0; font-family: "Liberation Sans", Arial, Helvetica, sans-serif; font-size: 11pt; line-height: 1.45; color: #1b2320; }
    h1 { font-size: 30pt; margin: 0 0 4mm; line-height: 1.15; }
    h2 { font-size: 15pt; margin: 0; line-height: 1.25; }
    h3 { font-size: 13pt; margin: 0 0 3mm; }
    p { margin: 0 0 3mm; }
    .titel { height: 250mm; display: flex; flex-direction: column; justify-content: center; break-after: page; }
    .titel svg { width: 26mm; height: 26mm; margin-bottom: 10mm; }
    .titel .unter { font-size: 15pt; color: #17533f; margin-bottom: 14mm; }
    .titel .klein { color: #5d6a64; font-size: 10pt; }
    .streifen { height: 3mm; width: 60mm; background: #1f6f54; border-radius: 2mm; margin-bottom: 9mm; }
    .inhalt { break-after: page; }
    .inhalt ol { columns: 2; column-gap: 10mm; padding-left: 6mm; margin: 4mm 0 8mm; }
    .inhalt li { margin-bottom: 1.6mm; break-inside: avoid; }
    .kasten { border: .3mm solid #bcdccd; background: #e7f2ed; border-radius: 2.5mm; padding: 4mm 5mm; margin: 4mm 0; break-inside: avoid; }
    .kapitel { margin-bottom: 9mm; }
    .kapitel .kopf { display: flex; gap: 4mm; align-items: flex-start; margin-bottom: 3mm; break-after: avoid; break-inside: avoid; }
    .nr { flex: none; width: 10mm; height: 10mm; border-radius: 50%; background: #1f6f54; color: #fff; font-weight: 700; font-size: 13pt;
      display: flex; align-items: center; justify-content: center; }
    .kurz { color: #5d6a64; margin: .8mm 0 0; }
    figure { margin: 3mm 0 4mm; break-inside: avoid; }
    figure img { display: block; max-width: 100%; max-height: 105mm; margin: 0 auto; border: .3mm solid #c9d1cb; border-radius: 2mm; }
    figcaption { font-size: 9pt; color: #5d6a64; text-align: center; margin-top: 1.5mm; }
    ol.schritte { list-style: none; counter-reset: s; padding: 0; margin: 0 0 3mm; }
    ol.schritte li { counter-increment: s; display: flex; gap: 3.5mm; margin-bottom: 2.4mm; break-inside: avoid; }
    ol.schritte li::before { content: counter(s); flex: none; width: 6.4mm; height: 6.4mm; border-radius: 50%; background: #e7f2ed; color: #17533f;
      font-weight: 700; font-size: 10pt; display: flex; align-items: center; justify-content: center; margin-top: .2mm; }
    .tipp { background: #e8f1fa; border-radius: 2mm; padding: 2.4mm 3.5mm; margin: 0 0 2.2mm; font-size: 10.2pt; break-inside: avoid; }
    .anhang { break-before: page; }
    table { border-collapse: collapse; width: 100%; font-size: 10.2pt; }
    td { padding: 2mm 2.5mm; border-bottom: .2mm solid #d9dfda; vertical-align: top; }
    td:first-child { font-weight: 700; width: 46mm; }
    tr { break-inside: avoid; }
    dl { margin: 0; }
    dt { font-weight: 700; margin-top: 3mm; break-after: avoid; }
    dd { margin: .5mm 0 0; break-inside: avoid; }
  </style></head><body>
    <div class="titel">
      ${symbol}
      <div class="streifen"></div>
      <h1>Marktrechnung</h1>
      <div class="unter">Schritt für Schritt: einrichten, Rechnungen schreiben, prüfen und versenden</div>
      <div class="klein">Anleitung zur Version ${sicher(version)} · Stand ${sicher(monat)}</div>
    </div>
    <div class="inhalt">
      <h3>Zu dieser Anleitung</h3>
      <p>${sicher(anleitung.vorwort)}</p>
      <div class="kasten"><b>Hilfe im Programm:</b> Dieselbe Anleitung steht im Programm unter „Anleitung“. Der Assistent – Knopf „Assistent“ links unten oder die Taste F1 –
        zeigt zu jeder Seite die passenden Schritte, führt durch die Einrichtung und beantwortet Fragen.</div>
      <h3 style="margin-top:8mm">Inhalt</h3>
      <ol>${anleitung.kapitel.map((k) => `<li>${sicher(k.titel)}</li>`).join('')}</ol>
      <p style="color:#5d6a64;font-size:10pt">Im Anhang: was Fehlermeldungen der Empfänger bedeuten, und die wichtigsten Begriffe kurz erklärt.<br>
        Die Bilder zeigen das Programm mit erfundenen Beispieldaten.</p>
    </div>
    ${kapitel}
    <div class="anhang">
      <h3>Anhang A: Was Fehlermeldungen der Empfänger bedeuten</h3>
      <table>${(anleitung.meldungen || []).map((m) => `<tr><td>${sicher(m.regel)}</td><td>${sicher(m.text)}</td></tr>`).join('')}</table>
      <h3 style="margin-top:9mm">Anhang B: Begriffe kurz erklärt</h3>
      <dl>${(anleitung.begriffe || []).map((b) => `<dt>${sicher(b.wort)}</dt><dd>${sicher(b.text)}</dd>`).join('')}</dl>
    </div>
  </body></html>`;
}

(async () => {
  const browser = await chromium.launch();
  const p = await browser.newPage();
  await p.setContent('<!doctype html><html><body></body></html>');

  // Die Bilder zuschneiden: nur der obere Teil, damit das PDF klein bleibt.
  const bildDaten = {};
  const fehlt = [];
  for (const name of new Set(anleitung.kapitel.map((k) => k.bild).filter(Boolean))) {
    const datei = path.join(bilder, name + '.png');
    if (!fs.existsSync(datei)) { fehlt.push(name); continue; }
    const roh = 'data:image/png;base64,' + fs.readFileSync(datei).toString('base64');
    bildDaten[name] = await p.evaluate(async ({ quelle, hoechstens }) => {
      const bild = new Image();
      bild.src = quelle;
      await bild.decode();
      const hoehe = Math.min(bild.naturalHeight, hoechstens);
      const flaeche = document.createElement('canvas');
      flaeche.width = bild.naturalWidth;
      flaeche.height = hoehe;
      flaeche.getContext('2d').drawImage(bild, 0, 0);
      return flaeche.toDataURL('image/png');
    }, { quelle: roh, hoechstens: HOEHE[name] || 800 });
  }

  await p.setContent(seite(bildDaten), { waitUntil: 'load' });
  await p.evaluate(() => Promise.all([...document.images].map((b) => b.decode().catch(() => {}))));
  const fuss = '<div style="width:100%;font-family:Liberation Sans,Arial,sans-serif;font-size:8pt;color:#5d6a64;padding:0 17mm;display:flex;justify-content:space-between">'
    + '<span>Marktrechnung – Anleitung</span><span>Seite <span class="pageNumber"></span> von <span class="totalPages"></span></span></div>';
  fs.mkdirSync(path.dirname(ziel), { recursive: true });
  await p.pdf({ path: ziel, format: 'A4', printBackground: true, preferCSSPageSize: true, displayHeaderFooter: true, headerTemplate: '<span></span>', footerTemplate: fuss });
  await browser.close();
  const groesse = Math.round(fs.statSync(ziel).size / 1024);
  console.log(`${ziel}: ${groesse} KB, ${anleitung.kapitel.length} Kapitel, ${Object.keys(bildDaten).length} Bilder` + (fehlt.length ? ` – es fehlen: ${fehlt.join(', ')}` : ''));
  if (fehlt.length) process.exitCode = 1;
})().catch((e) => { console.error('ABBRUCH: ' + e.message); process.exit(1); });
