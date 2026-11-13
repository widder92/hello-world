// Artikel: Waren und Leistungen, die auf Rechnungen immer wieder vorkommen.
import { html, useState, useEffect, api, stamm, zahlDE, geld, zeige, zeigeFehler, Feld, Text, Auswahl, Zahl, Dialog, gtinOk } from './basis.js';

export function Artikelseite() {
  const [liste, setListe] = useState(null);
  const [einst, setEinst] = useState(null);
  const [offen, setOffen] = useState(null);
  const lade = () => api('GET', '/api/artikel').then(setListe).catch(zeigeFehler);
  useEffect(() => { lade(); api('GET', '/api/einstellungen').then(setEinst).catch(zeigeFehler); }, []);
  if (!liste || !einst) return html`<p class="laden">Artikel werden geladen …</p>`;
  const neu = () => setOffen({ id: '', artikelnummer: '', gtin: '', bezeichnung: '', beschreibung: '', einheit: einst.standardEinheit || 'H87',
    preis: '0', steuerSatz: einst.standardSteuer || '7', herkunft: '' });
  const speichere = async () => {
    try { await api('POST', '/api/artikel', offen); setOffen(null); zeige('Artikel gespeichert.', 'gut'); lade(); } catch (e) { zeigeFehler(e); }
  };
  const loesche = async () => {
    if (!confirm(`Artikel „${offen.bezeichnung}“ löschen?`)) return;
    try { await api('DELETE', '/api/artikel/' + offen.id); setOffen(null); lade(); } catch (e) { zeigeFehler(e); }
  };
  const s = (n) => (w) => setOffen((x) => ({ ...x, [n]: w }));
  const einheit = (c) => (stamm.einheiten.find((e) => e.code === c) || {}).name || c;
  const gtinFalsch = offen && offen.gtin && !gtinOk(offen.gtin) ? 'Länge oder Prüfziffer stimmt nicht – bitte mit dem Etikett vergleichen.' : '';
  return html`
    <div class="kopf">
      <div><h1>Artikel</h1><div class="leise">Beim Schreiben einer Rechnung genügt es, den Namen anzutippen – Preis, GTIN und Steuersatz kommen von hier.</div></div>
      <div class="knoepfe"><button class="haupt" onClick=${neu}>+ Neuer Artikel</button></div>
    </div>
    <div class="karte">
      ${liste.length === 0 ? html`<div class="leer">Noch kein Artikel angelegt.</div>` : html`<table class="wahl">
        <thead><tr><th>Bezeichnung</th><th>Artikelnr.</th><th>GTIN / EAN</th><th>Einheit</th><th class="zahl">Preis netto</th><th class="zahl">USt</th></tr></thead>
        <tbody>${liste.map((a) => html`<tr key=${a.id} onClick=${() => setOffen({ ...a })}>
          <td><b>${a.bezeichnung}</b>${a.beschreibung ? html`<br /><span class="leise klein">${a.beschreibung}</span>` : null}</td>
          <td>${a.artikelnummer}</td><td>${a.gtin || html`<span class="leise">–</span>`}</td><td>${einheit(a.einheit)}</td>
          <td class="zahl">${geld(a.preis)}</td><td class="zahl">${zahlDE(a.steuerSatz)} %</td></tr>`)}</tbody>
      </table>`}
    </div>
    ${offen && html`<${Dialog} titel=${offen.id ? 'Artikel bearbeiten' : 'Neuer Artikel'} zu=${() => setOffen(null)} aktionen=${html`
        ${offen.id && html`<button class="gefahr" style="margin-right:auto" onClick=${loesche}>Löschen</button>`}
        <button onClick=${() => setOffen(null)}>Abbrechen</button><button class="haupt" onClick=${speichere}>Speichern</button>`}>
      <div class="raster">
        <${Feld} titel="Bezeichnung" breite="12"><${Text} wert=${offen.bezeichnung} setze=${s('bezeichnung')} /><//>
        <${Feld} titel="Eigene Artikelnummer" breite="6"><${Text} wert=${offen.artikelnummer} setze=${s('artikelnummer')} /><//>
        <${Feld} titel="GTIN / EAN (Strichcode)" breite="6" falsch=${gtinFalsch} hilfe="vom Handel für Ware erwartet"><${Text} wert=${offen.gtin} setze=${(v) => s('gtin')(v.replace(/\s/g, ''))} inputmode="numeric" /><//>
        <${Feld} titel="Einheit" breite="4"><${Auswahl} wert=${offen.einheit} setze=${s('einheit')} optionen=${stamm.einheiten.map((e) => [e.code, e.name])} /><//>
        <${Feld} titel="Preis netto je Einheit" breite="4"><${Zahl} wert=${offen.preis} min="2" max="4" setze=${s('preis')} /><//>
        <${Feld} titel="Umsatzsteuer" breite="4"><${Auswahl} wert=${String(Number(offen.steuerSatz))} setze=${s('steuerSatz')} optionen=${[['19', '19 % (Regelsatz, auch Getränke)'], ['7', '7 % (ermäßigt, viele Lebensmittel)'], ['0', '0 %']]} /><//>
        <${Feld} titel="Zusatztext auf der Rechnung" breite="12"><${Text} wert=${offen.beschreibung} setze=${s('beschreibung')} /><//>
      </div>
    <//>`}`;
}
