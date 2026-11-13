// Marktrechnung - Rahmen der Oberfläche und Wechsel zwischen den Seiten.
import { html, render, useState, useEffect, api, ladeStamm, stamm, Toasts, zeigeFehler } from './basis.js';
import { Uebersicht, Rechnungsseite } from './rechnungen.js';
import { Kundenliste, Kundenseite } from './kunden.js';
import { Artikelseite } from './artikel.js';
import { Pruefseite } from './pruefen.js';
import { Einstellungsseite } from './einstellungen.js';
import { Hilfeseite } from './hilfe.js';

function leseHash() {
  const [pfad, frage = ''] = (location.hash.replace(/^#/, '') || '/').split('?');
  return { teile: pfad.split('/').filter(Boolean), frage: new URLSearchParams(frage) };
}

function Seite({ ort }) {
  const [a, b] = ort.teile;
  switch (a) {
    case undefined: return html`<${Uebersicht} />`;
    case 'rechnung': return html`<${Rechnungsseite} key=${b} id=${b} />`;
    case 'kunden': return b ? html`<${Kundenseite} key=${b} id=${b} zurueck=${ort.frage.get('zurueck')} />` : html`<${Kundenliste} />`;
    case 'artikel': return html`<${Artikelseite} />`;
    case 'pruefen': return html`<${Pruefseite} />`;
    case 'einstellungen': return html`<${Einstellungsseite} />`;
    case 'hilfe': return html`<${Hilfeseite} />`;
    default: return html`<p class="laden">Diese Seite gibt es nicht. <a href="#/">Zur Übersicht</a></p>`;
  }
}

function App() {
  const [ort, setOrt] = useState(leseHash());
  const [bereit, setBereit] = useState(false);
  const [fehler, setFehler] = useState('');
  const [beendet, setBeendet] = useState(false);
  useEffect(() => {
    const wechsel = () => { setOrt(leseHash()); window.scrollTo(0, 0); };
    window.addEventListener('hashchange', wechsel);
    ladeStamm().then(() => setBereit(true)).catch((e) => setFehler(e.message));
    return () => window.removeEventListener('hashchange', wechsel);
  }, []);

  if (beendet) return html`<p class="laden">Marktrechnung wurde beendet. Dieses Fenster kann geschlossen werden.</p>`;
  if (fehler) return html`<div class="laden"><h1>Marktrechnung</h1><p>${fehler}</p></div>`;
  if (!bereit) return html`<p class="laden">Marktrechnung wird geladen …</p>`;

  const beende = async () => {
    if (!confirm('Marktrechnung beenden?')) return;
    try { await api('POST', '/api/beenden'); setBeendet(true); } catch (e) { zeigeFehler(e); }
  };
  const aktiv = ort.teile[0] === 'rechnung' ? undefined : ort.teile[0];
  const punkt = (ziel, name, schluessel) => html`<a href=${ziel} class=${aktiv === schluessel ? 'aktiv' : ''}>${name}</a>`;
  return html`<div class="rahmen">
    <aside class="seitenleiste">
      <div class="marke"><img src="symbol.svg" alt="" />Marktrechnung</div>
      <nav>
        ${punkt('#/', 'Rechnungen', undefined)}
        ${punkt('#/kunden', 'Kunden', 'kunden')}
        ${punkt('#/artikel', 'Artikel', 'artikel')}
        ${punkt('#/pruefen', 'Import aus Lexoffice', 'pruefen')}
        ${punkt('#/einstellungen', 'Einstellungen', 'einstellungen')}
        ${punkt('#/hilfe', 'Hilfe', 'hilfe')}
      </nav>
      <div class="fuss">Version ${stamm.version}<br /><button onClick=${beende}>Programm beenden</button></div>
    </aside>
    <main><${Seite} ort=${ort} /></main>
    <${Toasts} />
  </div>`;
}

render(html`<${App} />`, document.getElementById('app'));
