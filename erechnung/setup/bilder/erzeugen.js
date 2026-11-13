// Erzeugt die Bilder des Setup-Assistenten aus den SVG-Vorlagen in diesem
// Ordner: je eine BMP-Datei für die Bildschirmgrößen 100 bis 200 %. Die
// BMP-Dateien liegen mit im Ordner, damit das Setup ohne dieses Werkzeug
// gebaut werden kann. Nur nach einer Änderung an den Vorlagen aufrufen:
//
//   node setup/bilder/erzeugen.js      (braucht Playwright mit Chromium)
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');

const vorlagen = [
  { datei: 'willkommen.svg', name: 'willkommen', breite: 164, hoehe: 314 }, // Seitenbild: Willkommen und Abschluss
  { datei: 'kopf.svg', name: 'kopf', breite: 150, hoehe: 57 }, // Kopfbild der übrigen Seiten
];
const stufen = [100, 125, 150, 175, 200];

// bmp schreibt ein unkomprimiertes Bild mit 24 Bit je Bildpunkt - das
// einzige Format, das der Setup-Baukasten überall annimmt.
function bmp(breite, hoehe, rgba) {
  const zeile = Math.ceil((breite * 3) / 4) * 4;
  const aus = Buffer.alloc(54 + zeile * hoehe);
  aus.write('BM', 0, 'latin1');
  aus.writeUInt32LE(aus.length, 2);
  aus.writeUInt32LE(54, 10);
  aus.writeUInt32LE(40, 14);
  aus.writeInt32LE(breite, 18);
  aus.writeInt32LE(hoehe, 22); // positiv: die unterste Zeile zuerst
  aus.writeUInt16LE(1, 26);
  aus.writeUInt16LE(24, 28);
  aus.writeUInt32LE(zeile * hoehe, 34);
  aus.writeInt32LE(2835, 38);
  aus.writeInt32LE(2835, 42);
  for (let y = 0; y < hoehe; y++) {
    let ziel = 54 + (hoehe - 1 - y) * zeile;
    for (let x = 0; x < breite; x++) {
      const q = (y * breite + x) * 4;
      aus[ziel++] = rgba[q + 2];
      aus[ziel++] = rgba[q + 1];
      aus[ziel++] = rgba[q];
    }
  }
  return aus;
}

(async () => {
  const browser = await chromium.launch();
  const seite = await browser.newPage();
  for (const v of vorlagen) {
    const svg = fs.readFileSync(path.join(__dirname, v.datei), 'utf8');
    for (const stufe of stufen) {
      const breite = Math.round((v.breite * stufe) / 100);
      const hoehe = Math.round((v.hoehe * stufe) / 100);
      const rgba = await seite.evaluate(async ({ svg, breite, hoehe }) => {
        const bild = new Image();
        bild.src = 'data:image/svg+xml;charset=utf-8,' + encodeURIComponent(svg);
        await bild.decode();
        const flaeche = document.createElement('canvas');
        flaeche.width = breite;
        flaeche.height = hoehe;
        const stift = flaeche.getContext('2d');
        stift.fillStyle = '#fff';
        stift.fillRect(0, 0, breite, hoehe);
        stift.drawImage(bild, 0, 0, breite, hoehe);
        return Array.from(stift.getImageData(0, 0, breite, hoehe).data);
      }, { svg, breite, hoehe });
      const ziel = path.join(__dirname, `${v.name}-${stufe}.bmp`);
      fs.writeFileSync(ziel, bmp(breite, hoehe, rgba));
      console.log(`${path.basename(ziel)}  ${breite} x ${hoehe}`);
    }
  }
  await browser.close();
})().catch((e) => { console.error(e); process.exit(1); });
