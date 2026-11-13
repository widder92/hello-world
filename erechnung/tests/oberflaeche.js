// Durchlauf durch die Bedienoberfläche mit einem echten Browser: Firma
// einrichten, Kunde und Artikel anlegen, Rechnung schreiben, fertigstellen,
// Versanddialog öffnen, fremde Rechnung prüfen.
//
// Aufruf (das Programm muss mit leerem Datenordner laufen):
//   node tests/oberflaeche.js http://127.0.0.1:47411/?t=KENNWORT [Bilderordner] [fremde-rechnung.xml]
const { chromium } = require('playwright');

const [start, bilder = '.', fremd = ''] = process.argv.slice(2);
if (!start) { console.error('Aufruf: node tests/oberflaeche.js START-ADRESSE [Bilderordner] [XML-Datei]'); process.exit(2); }
const basis = new URL(start).origin;

function pruefe(bedingung, text) {
  if (!bedingung) { console.error('FEHLGESCHLAGEN: ' + text); process.exitCode = 1; } else console.log('ok  ' + text);
}

(async () => {
  const browser = await chromium.launch({ args: ['--lang=de-DE'], env: { ...process.env, LANGUAGE: 'de_DE', LANG: 'de_DE.UTF-8' } });
  const kontext = await browser.newContext({ viewport: { width: 1400, height: 950 }, locale: 'de-DE', deviceScaleFactor: 1 });
  const p = await kontext.newPage();
  const fehler = [];
  p.on('console', (m) => { if (m.type() === 'error') fehler.push(m.text()); });
  p.on('pageerror', (e) => fehler.push(e.message));
  p.on('dialog', (d) => d.accept());
  const bild = (name, ganz = false) => p.screenshot({ path: `${bilder}/${name}.png`, fullPage: ganz });
  const feld = (titel) => p.getByLabel(titel, { exact: false }).first();

  await p.goto(start);
  await p.getByRole('heading', { name: 'Erste Schritte' }).waitFor();
  await bild('01-erste-schritte');

  // 1. Einstellungen
  await p.goto(basis + '/#/einstellungen');
  await feld('Name der Firma bzw.').fill('Hofladen Sonnenwiese Inh. Maria Muster');
  await feld('Straße und Hausnummer').fill('Wiesenweg 3');
  await feld('PLZ').fill('50999');
  await feld('Ort').fill('Köln');
  await feld('USt-IdNr.').fill('DE123456789');
  await feld('Steuernummer').fill('214/5123/4567');
  await feld('Eigene GLN').fill('4012345000009');
  await feld('Ansprechpartner').fill('Maria Muster');
  await feld('Telefon').fill('+49 221 1234567');
  await feld('E-Mail').fill('rechnung@sonnenwiese.example');
  await feld('IBAN').fill('DE89 3704 0044 0532 0130 00');
  await feld('BIC').fill('COBADEFFXXX');
  await feld('Name der Bank').fill('Commerzbank Köln');
  await p.getByRole('button', { name: 'Speichern' }).click();
  await p.getByText('Einstellungen gespeichert.').waitFor();
  await bild('02-einstellungen', true);

  // 2. Kunde aus der REWE-Vorlage, mit erfundenen Daten überschrieben
  await p.goto(basis + '/#/kunden/neu');
  await p.getByRole('button', { name: /REWE Markt GmbH/ }).click();
  pruefe(await feld('E-Mail-Adresse für Rechnungen').inputValue() === 'rechnung.rewemarkt@rewe-group.com', 'Vorlage füllt das Rechnungspostfach');
  await feld('Name der Firma (genau').fill('Muster Markt GmbH ZwNl. West');
  await feld('GLN des Rechnungsempfängers').fill('4000001000006');
  await p.getByText('Prüfziffer falsch').first().waitFor();
  pruefe(true, 'falsche GLN-Prüfziffer wird sofort gemeldet');
  await feld('GLN des Rechnungsempfängers').fill('4000001000005');
  await feld('Straße und Hausnummer').fill('Handelsstraße 20');
  await feld('USt-IdNr. des Kunden').fill('DE811111111');
  await feld('Unsere Lieferantennummer dort').fill('700123');
  await feld('E-Mail-Adresse für Rechnungen').fill('rechnungseingang@mustermarkt.example');
  await feld('Zahlungsziel in Tagen').fill('30');
  await p.getByRole('button', { name: '+ Markt hinzufügen' }).click();
  await feld('Name des Markts').fill('Muster Markt Lindenthal');
  await feld('GLN des Markts').fill('4000001000012');
  await p.locator('.position').getByLabel('Straße und Hausnummer').fill('Dürener Straße 100');
  await p.locator('.position').getByLabel('PLZ').fill('50931');
  await p.locator('.position').getByLabel('Ort').fill('Köln');
  await p.locator('.position').getByLabel('Kostenstelle').fill('1234567890');
  await bild('03-kunde', true);
  await p.getByRole('button', { name: 'Speichern' }).click();
  await p.getByRole('heading', { name: 'Kunden', exact: true }).waitFor();
  pruefe(await p.getByText('Muster Markt GmbH ZwNl. West').count() === 1, 'Kunde steht in der Liste');

  // 3. Artikel
  await p.goto(basis + '/#/artikel');
  await p.getByRole('button', { name: '+ Neuer Artikel' }).click();
  await feld('Bezeichnung').fill('Blütenhonig 500 g');
  await feld('Eigene Artikelnummer').fill('H-001');
  await feld('GTIN / EAN').fill('4012345000016');
  await feld('Preis netto je Einheit').fill('4,95');
  await p.getByRole('button', { name: 'Speichern' }).click();
  await p.getByText('Artikel gespeichert.').waitFor();
  pruefe(await p.getByRole('cell', { name: '4,95 €' }).count() === 1, 'Artikel mit Preis in der Liste');

  // 4. Rechnung
  await p.goto(basis + '/#/rechnung/neu');
  await feld('Kunde (Rechnungsempfänger)').selectOption({ label: 'Muster Markt GmbH ZwNl. West' });
  await p.waitForTimeout(600);
  pruefe(await feld('Kostenstelle').inputValue() === '1234567890', 'Kostenstelle des Markts wird übernommen');
  await p.getByPlaceholder('Artikel eintippen').fill('Blütenhonig 500 g');
  pruefe(await p.locator('[data-feld="positionen.0.gtin"]').inputValue() === '4012345000016', 'Artikel füllt die GTIN');
  await p.locator('[data-feld="positionen.0.menge"]').fill('24');
  await p.locator('[data-feld="positionen.0.menge"]').blur();
  await p.getByText('Bereit, mit Warnungen').or(p.getByText('Alles in Ordnung')).first().waitFor();
  pruefe(await p.locator('.summen .gesamt').textContent().then((t) => t.includes('127,12')), 'Summe 24 x 4,95 + 7 % = 127,12 €');
  // Ohne Lieferdatum muss ein Fehler erscheinen, der zum Feld führt.
  await feld('Lieferdatum').fill('');
  await p.getByText('1 Fehler').or(p.getByText('2 Fehler')).first().waitFor();
  pruefe(await p.getByRole('button', { name: 'Fertigstellen …' }).isDisabled(), 'mit Fehlern lässt sich nicht fertigstellen');
  await bild('04-rechnung-mit-fehler');
  await p.locator('.meldung.fehler').first().click();
  await feld('Lieferdatum').fill('2026-11-12');
  await feld('Rechnungsdatum').fill('2026-11-13');
  await feld('Bestellnummer').fill('4500123456');
  await feld('Lieferscheinnummer').fill('LS-2026-0815');
  await p.getByText('Alles in Ordnung').waitFor();
  await p.getByText('· gespeichert').waitFor();
  await bild('05-rechnung', true);

  await p.getByRole('button', { name: 'Vorschau' }).click();
  await p.locator('.dialog iframe').waitFor();
  await p.waitForTimeout(1200);
  await bild('06-vorschau');
  await p.getByRole('button', { name: 'Schließen' }).click();

  await p.getByRole('button', { name: 'Fertigstellen …' }).click();
  await p.getByText('nicht mehr ändern').waitFor();
  await bild('07-fertigstellen');
  await p.getByRole('button', { name: 'Jetzt fertigstellen' }).click();
  await p.getByRole('heading', { name: /per E-Mail senden/ }).waitFor();
  await p.waitForTimeout(400);
  pruefe(await feld('An (Rechnungseingang').inputValue() === 'rechnungseingang@mustermarkt.example', 'Versanddialog nennt das Rechnungspostfach');
  pruefe(await feld('Betreff').inputValue() === 'Rechnung RE-2026-0001', 'Betreff enthält die Rechnungsnummer');
  pruefe(await p.getByText('Rechnung_RE-2026-0001.pdf').count() >= 1, 'Anhang ist die ZUGFeRD-Datei');
  await bild('08-senden');
  await p.getByRole('button', { name: 'Abbrechen' }).click();
  await bild('09-ansicht', true);

  await p.goto(basis + '/#/');
  await p.getByRole('cell', { name: 'RE-2026-0001' }).waitFor();
  pruefe(await p.getByRole('cell', { name: '127,12 €' }).count() === 1, 'Rechnung mit Betrag in der Übersicht');
  await bild('10-uebersicht');

  // 5. Fremde Rechnung prüfen. Ohne eigene Datei dient die eben erzeugte
  // Rechnung als Prüfling - sie muss ohne Beanstandung durchgehen.
  await p.getByRole('cell', { name: 'RE-2026-0001' }).click();
  await p.getByRole('heading', { name: 'Rechnung RE-2026-0001' }).waitFor();
  const rechnungsId = p.url().split('/').pop();
  const pdf = await (await kontext.request.get(`${basis}/api/rechnungen/${rechnungsId}/datei/0`)).body();
  pruefe(pdf.subarray(0, 4).toString() === '%PDF', 'fertige Rechnung lässt sich als PDF abrufen');
  await p.goto(basis + '/#/pruefen');
  await p.locator('input[type=file]').setInputFiles(fremd || { name: 'Rechnung_RE-2026-0001.pdf', mimeType: 'application/pdf', buffer: pdf });
  await p.locator('.urteil').waitFor();
  const urteil = (await p.locator('.urteil').textContent()).trim();
  pruefe(fremd ? urteil.length > 5 : urteil === 'Keine Beanstandung', 'Prüfung einer eingelesenen Rechnung: ' + urteil);
  await bild('11-pruefen', true);
  if (fremd && await p.getByRole('button', { name: 'Als Entwurf übernehmen und ergänzen' }).count()) {
    await p.getByRole('button', { name: 'Als Entwurf übernehmen und ergänzen' }).click();
    await p.getByText('wurde aus der Datei').waitFor();
    pruefe(await feld('Rechnungsnummer').inputValue() !== '', 'übernommener Entwurf behält seine Rechnungsnummer');
    pruefe(await p.locator('[data-feld="positionen.2.rabattProzent"]').inputValue() === '10', 'Positionsrabatt der fremden Rechnung bleibt sichtbar');
    await bild('11b-uebernommen', true);
  }

  // 6. Storno: aus der Rechnung wird der Entwurf einer Rechnungskorrektur.
  await p.goto(`${basis}/#/rechnung/${rechnungsId}`);
  await p.getByRole('button', { name: /Stornieren/ }).click();
  await p.getByRole('heading', { name: /Rechnungskorrektur/ }).waitFor();
  pruefe(await feld('Korrigiert die Rechnung Nr.').inputValue() === 'RE-2026-0001', 'Rechnungskorrektur verweist auf die Rechnung');
  await p.getByText('Alles in Ordnung').or(p.getByText('Bereit, mit Warnungen')).first().waitFor();
  await bild('13-storno', true);

  await p.goto(basis + '/#/hilfe');
  await p.getByRole('heading', { name: 'REWE Group' }).waitFor();
  await bild('12-hilfe', true);

  await p.waitForTimeout(4000);
  pruefe(await p.locator('.toast').count() === 0, 'Meldungen verschwinden von selbst');
  pruefe(fehler.length === 0, 'keine Fehler in der Browserkonsole' + (fehler.length ? ': ' + fehler.join(' | ') : ''));
  await browser.close();
})().catch((e) => { console.error('ABBRUCH: ' + e.message); process.exit(1); });
