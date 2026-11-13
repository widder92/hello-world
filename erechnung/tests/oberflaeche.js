// Durchlauf durch die Bedienoberfläche mit einem echten Browser: Firma
// einrichten, Kunde und Artikel anlegen, Rechnung schreiben, fertigstellen,
// Versanddialog öffnen, fremde Rechnung prüfen, Stammdaten-Datei einlesen.
//
// Aufruf (das Programm muss mit leerem Datenordner laufen):
//   node tests/oberflaeche.js http://127.0.0.1:47411/?t=KENNWORT [Bilderordner] [fremde-rechnung.xml] [Port] [KI-Port]
//
// Mit einem Port als viertem Wert stellt der Test die Schnittstelle von
// Lexware Office nach und prüft auch den direkten Abruf. Das Programm muss
// dafür mit -lexware-adresse http://127.0.0.1:PORT gestartet sein. Mit dem
// fünften Wert stellt er die Claude-Schnittstelle nach und prüft den
// KI-Teil des Assistenten (Programmstart mit -ki-adresse http://127.0.0.1:KI-PORT).
const { chromium } = require('playwright');

const [start, bilder = '.', fremd = '', lexwarePort = '', kiPort = ''] = process.argv.slice(2);
if (!start) { console.error('Aufruf: node tests/oberflaeche.js START-ADRESSE [Bilderordner] [XML-Datei]'); process.exit(2); }
const basis = new URL(start).origin;

// Nachbau der Lexware-Schnittstelle mit den Beispielen aus deren Beschreibung.
function lexwareAttrappe(port) {
  const rechnung = { voucherNumber: 'RE1019', voucherDate: '2023-02-22T00:00:00.000+01:00', dueDate: null,
    address: { name: 'Bike & Ride GmbH & Co. KG', supplement: 'Gebäude 10', street: 'Musterstraße 42', city: 'Freiburg', zip: '79112', countryCode: 'DE' },
    lineItems: [
      { type: 'material', name: 'Abus Kabelschloss Primo 590', quantity: 2, unitName: 'Stück', unitPrice: { currency: 'EUR', netAmount: 13.4, grossAmount: 15.95, taxRatePercentage: 19 }, discountPercentage: 50, lineItemAmount: 13.4 },
      { type: 'service', name: 'Aufwändige Montage', quantity: 1, unitName: 'Stunde', unitPrice: { currency: 'EUR', netAmount: 8.32, grossAmount: 8.9, taxRatePercentage: 7 }, discountPercentage: 0, lineItemAmount: 8.32 }],
    totalPrice: { currency: 'EUR', totalNetAmount: 21.72, totalGrossAmount: 24.85, totalTaxAmount: 3.13 },
    taxConditions: { taxType: 'net' }, paymentConditions: { paymentTermDuration: 30 },
    shippingConditions: { shippingDate: '2023-04-22T00:00:00.000+02:00', shippingType: 'delivery' }, introduction: 'Ihre bestellten Positionen stellen wir Ihnen hiermit in Rechnung' };
  const antworten = {
    '/v1/profile': { companyName: 'Testfirma GmbH', smallBusiness: false },
    '/v1/voucherlist': { content: [{ id: 'bbbb', voucherType: 'invoice', voucherStatus: 'open', voucherNumber: 'RE1019', voucherDate: '2023-02-22T00:00:00.000+01:00', contactName: 'Bike & Ride GmbH & Co. KG', totalAmount: 24.85 }], last: true },
    '/v1/invoices/bbbb': rechnung,
    '/v1/contacts': { content: [{ roles: { customer: { number: 10307 } }, company: { name: 'Testfirma', vatRegistrationId: 'DE123456789' },
      addresses: { billing: [{ street: 'Hauptstr. 5', zip: '12345', city: 'Musterort', countryCode: 'DE' }] }, xRechnung: { vendorNumberAtCustomer: '70123456' } }], last: true },
    '/v1/articles': { content: [{ title: 'Holunderblütensirup 0,33 l', articleNumber: 'S-033', gtin: '4012345000054', unitName: 'Flasche', price: { netPrice: 3.2, taxRate: 19 } }], last: true },
  };
  return new Promise((bereit) => {
    const server = require('http').createServer((anfrage, antwort) => {
      const pfad = anfrage.url.split('?')[0];
      if (anfrage.headers.authorization !== 'Bearer probe-schluessel') { antwort.writeHead(401); antwort.end('{"message":"Unauthorized"}'); return; }
      if (!antworten[pfad]) { antwort.writeHead(404); antwort.end('{"status":404}'); return; }
      antwort.writeHead(200, { 'Content-Type': 'application/json' });
      antwort.end(JSON.stringify(antworten[pfad]));
    });
    server.listen(Number(port), '127.0.0.1', () => bereit(server));
  });
}

// Nachbau der Claude-Schnittstelle: nimmt nur den Schlüssel "sk-ant-probe"
// an und merkt sich, was gefragt wurde.
function kiAttrappe(port) {
  const anfragen = [];
  return new Promise((bereit) => {
    const server = require('http').createServer((anfrage, antwort) => {
      let roh = '';
      anfrage.on('data', (d) => { roh += d; });
      anfrage.on('end', () => {
        antwort.setHeader('Content-Type', 'application/json');
        if (anfrage.headers.authorization !== 'Bearer sk-ant-probe') {
          antwort.writeHead(401);
          antwort.end(JSON.stringify({ type: 'error', error: { type: 'authentication_error', message: 'invalid x-api-key' } }));
          return;
        }
        if (anfrage.url.startsWith('/v1/models/')) {
          antwort.end(JSON.stringify({ id: 'claude-opus-5-5', type: 'model', display_name: 'Claude Opus 5.5', created_at: '2026-09-01T00:00:00Z' }));
          return;
        }
        anfragen.push(roh);
        antwort.end(JSON.stringify({ id: 'msg_01', type: 'message', role: 'assistant', model: 'claude-opus-5-5', stop_reason: 'end_turn', stop_sequence: null,
          usage: { input_tokens: 5000, output_tokens: 60 },
          content: [{ type: 'text', text: 'So verschicken Sie eine fertige Rechnung:\n1. Die Rechnung in der Liste anklicken.\n2. Auf „Per E-Mail senden …“ klicken.\n3. Empfänger prüfen und auf „Jetzt senden“ klicken.\n\nDie Adresse des Rechnungspostfachs steht beim Kunden: [[#/kunden|Kunden öffnen]]' }] }));
      });
    });
    server.listen(Number(port), '127.0.0.1', () => bereit({ server, anfragen }));
  });
}

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
  // Bilder einzelner Abschnitte - sie stehen in der gedruckten Anleitung.
  const kartenbild = (name, titel) => p.locator('.karte').filter({ has: p.getByRole('heading', { name: titel, exact: true }) }).first()
    .screenshot({ path: `${bilder}/${name}.png` });
  const ohneMeldungen = () => p.waitForFunction(() => document.querySelectorAll('.toast').length === 0, null, { timeout: 12000 }).catch(() => {});

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
  await ohneMeldungen();
  await kartenbild('a-firma', 'Eigene Firma');
  await kartenbild('a-email', 'E-Mail-Versand');
  // Im Bild für die Anleitung steht ein üblicher Ordner statt dem des Testlaufs.
  await p.evaluate(() => { for (const b of document.querySelectorAll('.karte b')) if (/[\\/]/.test(b.textContent) && !/\s/.test(b.textContent.trim())) b.textContent = 'C:\\Users\\Maria\\Documents\\Marktrechnung'; });
  await kartenbild('a-sicherung', 'Daten und Sicherung');

  // 2. Kunde aus der REWE-Vorlage, mit erfundenen Daten überschrieben
  await p.goto(basis + '/#/kunden/neu');
  await p.getByRole('button', { name: /REWE Markt GmbH/ }).click();
  pruefe(await feld('USt-IdNr. des Kunden').inputValue() === 'DE812706034', 'die Vorlage bringt die Stammdaten des Empfängers mit');
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
  await feld('Umsatzsteuer').selectOption('7');
  await p.getByRole('button', { name: 'Speichern' }).click();
  await p.getByText('Artikel gespeichert.').waitFor();
  pruefe(await p.getByRole('cell', { name: '4,95 €' }).count() === 1, 'Artikel mit Preis in der Liste');
  await ohneMeldungen();
  await bild('a-artikel');

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
  await ohneMeldungen();
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
  await ohneMeldungen();
  await bild('11-pruefen', true);
  pruefe(await p.getByRole('heading', { name: 'Direkt aus Lexware Office abrufen' }).count() === 1, 'die Importseite bietet den direkten Abruf an');
  if (fremd) {
    await p.getByRole('button', { name: 'Übernehmen', exact: true }).click();
    await p.getByText('wurde aus der Datei').waitFor();
    pruefe(await feld('Rechnungsnummer').inputValue() !== '', 'übernommener Entwurf behält seine Rechnungsnummer');
    pruefe(await p.locator('[data-feld="positionen.2.rabattProzent"]').inputValue() === '10', 'Positionsrabatt der fremden Rechnung bleibt sichtbar');
    pruefe(await feld('Kostenstelle').inputValue() === '1234567890', 'Kostenstelle kommt beim Import aus den Stammdaten');
    pruefe(await p.locator('[data-feld="positionen.0.gtin"]').inputValue() === '4012345000016', 'GTIN bleibt bzw. kommt aus den Stammdaten');
    await p.getByText('Alles in Ordnung').or(p.getByText('Bereit, mit Warnungen')).first().waitFor();
    pruefe(true, 'die übernommene Rechnung ist ohne Nacharbeit versandfertig');
    await bild('11b-uebernommen', true);
  }

  // 5b. Direkter Abruf aus Lexware Office gegen die nachgebaute Schnittstelle.
  if (lexwarePort) {
    const attrappe = await lexwareAttrappe(lexwarePort);
    await p.goto(basis + '/#/');
    await p.getByRole('heading', { name: 'Rechnungen', exact: true }).waitFor();
    await p.goto(basis + '/#/pruefen');
    await p.getByText('Verbindung einrichten').click();
    await feld('Zugangsschlüssel von Lexware Office').fill('falsch');
    await p.getByRole('button', { name: 'Verbinden' }).click();
    await p.getByText('lehnt den Zugangsschlüssel ab').waitFor();
    pruefe(true, 'ein falscher Zugangsschlüssel wird verständlich abgelehnt');
    // Die Absage erscheint auch als Meldung des Browsers; sie ist hier gewollt.
    for (let i = fehler.length - 1; i >= 0; i--) if (fehler[i].includes('422')) fehler.splice(i, 1);
    await feld('Zugangsschlüssel von Lexware Office').fill('probe-schluessel');
    await p.getByRole('button', { name: 'Verbinden' }).click();
    await p.getByText('Verbunden mit Lexware Office – Testfirma GmbH').waitFor();
    await feld('Belege ab dem').fill('2023-01-01');
    await p.getByRole('button', { name: 'Rechnungen anzeigen' }).click();
    await p.getByRole('cell', { name: 'RE1019' }).waitFor();
    await bild('14-lexware-belege', true);
    await p.getByRole('button', { name: 'Ausgewählten Beleg einlesen' }).click();
    await p.getByRole('cell', { name: 'Lexware Office RE1019' }).waitFor();
    pruefe(await p.getByRole('heading', { name: 'Eingelesene Rechnungen' }).count() === 1, 'der Beleg aus Lexware Office steht in der Importliste');
    await p.getByRole('button', { name: 'Kunden abgleichen' }).click();
    await p.getByText(/Kunden abgeglichen: 1 Firmenkunden/).waitFor();
    await p.getByRole('button', { name: 'Artikel abgleichen' }).click();
    await p.getByText(/Artikel abgeglichen: 1 in Lexware Office, 1 neu/).waitFor();
    pruefe(true, 'Kunden und Artikel lassen sich aus Lexware Office holen');
    await p.getByRole('button', { name: 'Übernehmen', exact: true }).click();
    await p.getByText('wurde aus der Datei').waitFor();
    pruefe(await feld('Rechnungsnummer').inputValue() === 'RE1019', 'Beleg aus Lexware Office wird mit seiner Nummer zum Entwurf');
    pruefe(await p.locator('.summen .gesamt').textContent().then((t) => t.includes('24,85')), 'Summe wie in Lexware Office: 24,85 €');
    await bild('15-lexware-entwurf', true);
    await p.goto(basis + '/#/artikel');
    await p.getByRole('heading', { name: 'Artikel', exact: true }).waitFor();
    await p.getByText('Holunderblütensirup 0,33 l').first().waitFor({ timeout: 5000 }).catch(() => {});
    pruefe(await p.getByText('Holunderblütensirup 0,33 l').count() === 1, 'Artikel aus Lexware Office steht in den Stammdaten');
    attrappe.close();
  }

  // 6. Storno: aus der Rechnung wird der Entwurf einer Rechnungskorrektur.
  await p.goto(`${basis}/#/rechnung/${rechnungsId}`);
  await p.getByRole('button', { name: /Stornieren/ }).click();
  await p.locator('[data-frage="ja"]').click(); // Rückfrage im Programm bestätigen
  await p.getByRole('heading', { name: /Rechnungskorrektur/ }).waitFor();
  pruefe(await feld('Korrigiert die Rechnung Nr.').inputValue() === 'RE-2026-0001', 'Rechnungskorrektur verweist auf die Rechnung');
  await p.getByText('Alles in Ordnung').or(p.getByText('Bereit, mit Warnungen')).first().waitFor();
  await bild('13-storno', true);

  // 7. Kunden und Märkte aus einer Stammdaten-Datei; viele Märkte bleiben
  //    über die Suche bedienbar.
  const gln = (n) => {
    const z = '43999020' + String(n).padStart(4, '0');
    const summe = [...z].reduce((a, c, i) => a + Number(c) * (i % 2 ? 3 : 1), 0);
    return z + ((10 - (summe % 10)) % 10);
  };
  const orte = ['Korbach', 'Waldeck', 'Bad Wildungen', 'Frankenberg', 'Fritzlar', 'Kassel', 'Baunatal', 'Melsungen', 'Homberg', 'Marburg', 'Wolfhagen', 'Volkmarsen'];
  const stammdaten = {
    kunden: [{
      name: 'Handelsgesellschaft Nordhessen mbH', profil: 'edeka', gln: gln(0), strasse: 'Am Lager 1', plz: '34212', ort: 'Melsungen', land: 'DE', rechnungsEmail: 'rechnung@nordhessen.example',
      lieferorte: orte.map((ort, i) => ({ name: 'Markt ' + ort, gln: gln(i + 1), strasse: 'Hauptstraße ' + (i + 1), plz: String(34100 + i), ort, land: 'DE', kundennummer: String(10100 + i) })),
    }],
  };
  await p.goto(basis + '/#/kunden');
  await p.getByText('Aus Datei einlesen').waitFor();
  await p.setInputFiles('input[type=file]', { name: 'Stammdaten.json', mimeType: 'application/json', buffer: Buffer.from(JSON.stringify(stammdaten)) });
  await p.getByText('Eingelesen: 1 Kunden neu, 0 ergänzt, 12 Märkte neu.').waitFor();
  pruefe(true, 'Stammdaten-Datei bringt Kunde und Märkte');
  await p.getByRole('button', { name: 'Stammdaten prüfen' }).click();
  await p.getByText(/Geprüft: \d+ Kunden, \d+ Märkte/).waitFor();
  pruefe(true, 'die Stammdaten lassen sich prüfen');
  await bild('18-stammdaten-pruefen', true);
  await p.getByRole('button', { name: 'Schließen' }).click();
  await p.locator('tr', { hasText: 'Nordhessen' }).first().click();
  const suche = p.getByPlaceholder(/In 12 Märkten suchen/);
  await suche.waitFor();
  await suche.fill('wildungen');
  pruefe(await p.getByRole('button', { name: 'Bearbeiten' }).count() === 1, 'die Suche findet den Markt unter vielen');
  await bild('16-viele-maerkte', true);
  await p.goto(basis + '/#/rechnung/neu');
  await p.locator('[data-feld="kundeId"]').selectOption({ label: 'Handelsgesellschaft Nordhessen mbH' });
  const markt = p.locator('input[data-feld="lieferort.gln"]');
  await markt.waitFor();
  await markt.fill(`Markt Waldeck, 34101 Waldeck · GLN ${gln(2)}`);
  await p.getByText(`Markt: Markt Waldeck · Hauptstraße 2 · 34101 Waldeck · GLN ${gln(2)}`).waitFor();
  pruefe(true, 'der Markt lässt sich in der Rechnung über die Suche wählen');
  await p.getByRole('button', { name: 'Entwurf löschen' }).click();
  await p.locator('[data-frage="ja"]').click();
  await p.getByRole('heading', { name: 'Rechnungen', exact: true }).waitFor().catch(() => {});

  // 8. Steht in der eingelesenen Rechnung ein Markt als Kunde, bietet der
  //    Import an, sie an die Zentrale zu stellen.
  if (fremd) {
    const xml = require('fs').readFileSync(fremd, 'utf8')
      .replace(/<ram:BuyerTradeParty>\s*<ram:Name>[^<]*<\/ram:Name>/, '<ram:BuyerTradeParty><ram:ID>10101</ram:ID><ram:Name>Kaufmann Waldeck e.K.</ram:Name>')
      .replace(/RE-2026-0042/g, 'RE-2026-0077');
    await p.goto(basis + '/#/pruefen');
    await p.locator('input[type=file]').setInputFiles({ name: 'RE-2026-0077.xml', mimeType: 'text/xml', buffer: Buffer.from(xml) });
    await p.getByText('ist als Markt von Handelsgesellschaft Nordhessen mbH hinterlegt').waitFor();
    await bild('17-an-die-zentrale', true);
    await p.getByRole('button', { name: 'Übernehmen', exact: true }).click();
    await p.locator('[data-feld="kundeId"]').waitFor();
    await p.getByText(`Markt: Markt Waldeck · Hauptstraße 2 · 34101 Waldeck · GLN ${gln(2)}`).waitFor();
    pruefe(await p.locator('[data-feld="kundeId"] option:checked').innerText() === 'Handelsgesellschaft Nordhessen mbH', 'Rechnung an einen Markt geht auf Wunsch an die Zentrale');
  }

  // 9. DATEV-Export: Zeitraum wählen, Belegpaket holen.
  await p.goto(basis + '/#/');
  await p.getByRole('button', { name: 'DATEV-Export' }).click();
  const jetzt = new Date();
  const monat = jetzt.getFullYear() + '-' + String(jetzt.getMonth() + 1).padStart(2, '0');
  await p.locator('input[type=month]').first().fill(monat);
  await p.locator('input[type=month]').nth(1).fill(monat);
  await p.getByText(/Rechnung(en)? · .* brutto/).waitFor();
  pruefe(await p.getByText('Dafür fehlen noch Angaben').count() === 1, 'der DATEV-Export sagt, was für den Buchungsstapel noch fehlt');
  const [paket] = await Promise.all([p.waitForEvent('download'), p.getByRole('button', { name: /Belegpaket herunterladen/ }).click()]);
  pruefe(/^DATEV-Belege_\d{4}-\d{2}\.zip$/.test(paket.suggestedFilename()), 'das Belegpaket für DATEV lässt sich herunterladen');
  await ohneMeldungen();
  await bild('19-datev-export');
  await p.getByRole('button', { name: 'Schließen' }).click();

  // 10. Zusatzfunktionen: erst unsichtbar, nach dem Einschalten im Menü.
  //     Angebot -> Lieferschein -> Rechnung, offene Posten, Serie, Auswertung.
  pruefe(await p.getByRole('link', { name: 'Angebote' }).count() === 0, 'Zusatzfunktionen sind ausgeschaltet nicht zu sehen');
  await p.goto(basis + '/#/einstellungen');
  await p.getByRole('heading', { name: 'Zusatzfunktionen' }).waitFor();
  for (const name of ['Angebote', 'Lieferscheine', 'Serienrechnungen', 'Offene Posten und Mahnungen', 'Abschlagsrechnungen', 'Auswertung']) await p.getByLabel(name, { exact: true }).check();
  await p.getByRole('button', { name: 'Speichern' }).click();
  await p.getByText('Einstellungen gespeichert.').waitFor();
  await p.getByRole('link', { name: 'Angebote' }).waitFor();
  await bild('20-zusatzfunktionen', true);
  await ohneMeldungen();
  await kartenbild('a-zusatz', 'Zusatzfunktionen');
  await p.getByRole('link', { name: 'Angebote' }).click();
  await p.getByRole('button', { name: '+ Neues Angebot' }).click();
  await p.locator('select').first().selectOption({ label: 'Muster Markt GmbH ZwNl. West' });
  await p.getByPlaceholder('Artikel eintippen').fill('Blütenhonig 500 g');
  await p.getByRole('button', { name: 'Speichern' }).click();
  await p.getByText(/Angebot AN-\d{4}-0001 gespeichert/).waitFor();
  pruefe(true, 'ein Angebot bekommt seine eigene Nummer');
  await bild('21-angebot', true);
  await p.getByRole('button', { name: 'Ansehen und drucken' }).click();
  await p.locator('iframe').waitFor();
  await p.getByRole('button', { name: 'Schließen' }).click();
  await p.getByRole('button', { name: 'Lieferschein daraus machen' }).click();
  await p.getByRole('heading', { name: /Lieferschein/ }).first().waitFor();
  await p.getByText(/LS-\d{4}-0001/).first().waitFor();
  await p.getByRole('button', { name: 'Rechnung daraus machen' }).click();
  await p.locator('[data-feld="lieferscheinnummer"]').waitFor();
  pruefe(/^LS-\d{4}-0001$/.test(await p.locator('[data-feld="lieferscheinnummer"]').inputValue()), 'die Rechnung zum Lieferschein übernimmt dessen Nummer');
  pruefe(await p.getByLabel('Art der Rechnung').count() === 1, 'die Abschlagsrechnung lässt sich wählen');
  await p.getByRole('button', { name: 'Entwurf löschen' }).click();
  await p.locator('[data-frage="ja"]').click();
  await p.getByRole('link', { name: 'Offene Posten' }).click();
  await p.getByRole('heading', { name: 'Offene Posten' }).waitFor();
  await p.getByRole('link', { name: 'Serienrechnungen' }).click();
  await p.getByText('Noch keine Serie angelegt.').waitFor();
  await p.getByRole('link', { name: 'Auswertung' }).click();
  await p.getByRole('heading', { name: /Umsätze \d{4} nach Steuersatz/ }).waitFor();
  pruefe(await p.getByText('Muster Markt GmbH ZwNl. West').count() >= 1, 'die Auswertung zeigt den Umsatz je Kunde');
  await bild('22-auswertung', true);

  await p.goto(basis + '/#/hilfe');
  await p.getByRole('heading', { name: 'REWE Group' }).waitFor();
  pruefe(await p.getByRole('cell', { name: 'BR-DE-15' }).count() === 1, 'die Hilfe erklärt die Fehlermeldungen der Empfänger');
  await bild('12-hilfe', true);
  await ohneMeldungen();
  await kartenbild('a-rewe', 'REWE Group');

  // 11. Assistent: Hilfe zur Seite, Stand der Einrichtung, Suche in der Anleitung.
  await p.goto(basis + '/#/kunden');
  await p.getByRole('heading', { name: 'Kunden', exact: true }).waitFor();
  await ohneMeldungen();
  pruefe(!await p.locator('.assistent').isVisible(), 'der Assistent ist zunächst zugeklappt');
  await p.keyboard.press('F1');
  const helfer = p.locator('.assistent');
  await helfer.getByText('Zu dieser Seite').waitFor();
  pruefe(await helfer.locator('[data-kapitel="kunden"] ol li').count() >= 5, 'F1 öffnet den Assistenten mit den Schritten zur geöffneten Seite');
  await helfer.getByText('Als Nächstes').waitFor();
  pruefe(await helfer.locator('.naechster').innerText().then((t) => t.includes('E-Mail-Versand einrichten')), 'der Assistent nennt den nächsten offenen Schritt der Einrichtung');
  await bild('23-assistent');
  const frageFeld = helfer.getByLabel('Frage an den Assistenten');
  await frageFeld.fill('Wie sichere ich meine Daten?');
  await helfer.getByRole('button', { name: 'Suchen' }).click();
  await helfer.getByText('Das habe ich in der Anleitung gefunden:').waitFor();
  pruefe(await helfer.locator('[data-kapitel="sicherung"] ol li').count() >= 2, 'eine Frage findet das passende Kapitel der Anleitung');
  await frageFeld.fill('Was ist eine GLN?');
  await frageFeld.press('Enter');
  await helfer.getByText('Global Location Number, 13 Ziffern').waitFor();
  await frageFeld.fill('Was heißt BR-DE-15?');
  await frageFeld.press('Enter');
  await helfer.getByText('Meldung BR-DE-15').waitFor();
  pruefe(true, 'Begriffe und Fehlermeldungen werden erklärt');
  await frageFeld.fill('Zebra Quark');
  await frageFeld.press('Enter');
  await helfer.getByText('Dazu habe ich in der Anleitung nichts gefunden').waitFor();
  await helfer.getByRole('button', { name: 'Neu beginnen' }).click();
  await helfer.locator('[data-kapitel="kunden"]').getByRole('button', { name: 'In der Anleitung zeigen' }).click();
  await p.getByRole('heading', { name: 'Marktrechnung – Schritt für Schritt' }).waitFor();
  pruefe(await p.locator('main .kapitel').count() >= 15, 'die Anleitung zeigt alle Kapitel');
  await helfer.getByRole('button', { name: 'Assistent schließen' }).click();
  pruefe(!await helfer.isVisible(), 'der Assistent lässt sich schließen');
  await bild('24-anleitung');

  // 12. KI-Teil des Assistenten gegen die nachgestellte Schnittstelle.
  if (kiPort) {
    const ki = await kiAttrappe(kiPort);
    await p.goto(basis + '/#/einstellungen?zu=assistent');
    const karte = p.locator('#assistent');
    await karte.getByRole('heading', { name: 'Assistent mit KI (freiwillig)' }).waitFor();
    await karte.locator('[data-feld="ki.schluessel"]').fill('sk-ant-falsch');
    await karte.getByRole('button', { name: 'KI einschalten' }).click();
    await p.getByText('Zugangsschlüssel wird nicht angenommen').waitFor();
    pruefe(await karte.getByText('Die KI ist eingeschaltet').count() === 0, 'ein falscher Zugangsschlüssel für die KI wird abgewiesen');
    // Die Abweisung meldet der Browser auch in seiner Konsole - das ist hier gewollt.
    for (let i = fehler.length - 1; i >= 0; i--) if (/status of 422/.test(fehler[i])) fehler.splice(i, 1);
    await karte.locator('[data-feld="ki.schluessel"]').fill('sk-ant-probe');
    await karte.getByRole('button', { name: 'KI einschalten' }).click();
    await karte.getByText('Die KI ist eingeschaltet').waitFor();
    await bild('25-ki-einstellung');
    await p.getByRole('button', { name: /Assistent/ }).first().click();
    await helfer.getByRole('button', { name: 'Fragen', exact: true }).waitFor();
    await frageFeld.fill('Wie verschicke ich eine Rechnung?');
    await helfer.getByRole('button', { name: 'Fragen', exact: true }).click();
    await helfer.locator('.antworttext').waitFor();
    pruefe(await helfer.locator('.antworttext').innerText().then((t) => t.includes('Jetzt senden') && !t.includes('[[')), 'die KI beantwortet eine frei gestellte Frage');
    await bild('26-assistent-ki');
    const gesendet = ki.anfragen.join('\n');
    pruefe(gesendet.includes('Geöffnete Seite: Einstellungen') && gesendet.includes('Kapitel 1:'), 'die Frage geht mit Anleitung und Lage hinaus');
    pruefe(!/Muster Markt|Sonnenwiese|4000001000005|DE89 ?3704|Blütenhonig/.test(gesendet), 'Kunden, Artikel und Firmendaten bleiben auf dem Rechner');
    await helfer.getByRole('link', { name: /Kunden öffnen/ }).click();
    await p.getByRole('heading', { name: 'Kunden', exact: true }).waitFor();
    pruefe(true, 'Verweise in der Antwort führen auf die Seite im Programm');
    await p.keyboard.press('F1');
    await p.goto(basis + '/#/einstellungen?zu=assistent');
    await karte.getByRole('button', { name: 'KI ausschalten' }).click();
    await p.locator('[data-frage="ja"]').click();
    await karte.getByRole('button', { name: 'KI einschalten' }).waitFor();
    pruefe(true, 'die KI lässt sich wieder ausschalten');
    ki.server.close();
  }

  await p.waitForFunction(() => document.querySelectorAll('.toast').length === 0, null, { timeout: 12000 }).catch(() => {});
  pruefe(await p.locator('.toast').count() === 0, 'Meldungen verschwinden von selbst');
  pruefe(fehler.length === 0, 'keine Fehler in der Browserkonsole' + (fehler.length ? ': ' + fehler.join(' | ') : ''));
  await browser.close();
})().catch((e) => { console.error('ABBRUCH: ' + e.message); process.exit(1); });
