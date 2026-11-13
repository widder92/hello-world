// Marktrechnung - Rahmen der Oberfläche und Wechsel zwischen den Seiten.
import { html, render, useState, useEffect, api, ladeStamm, stamm, Toasts, zeigeFehler, frage, Fragen } from './basis.js';
import { Uebersicht, Rechnungsseite } from './rechnungen.js';
import { Kundenliste, Kundenseite } from './kunden.js';
import { Artikelseite } from './artikel.js';
import { Pruefseite } from './pruefen.js';
import { Einstellungsseite } from './einstellungen.js';
import { Hilfeseite } from './hilfe.js';
import { Belegliste, Belegseite } from './belege.js';
import { OffenePosten, Serienseite, Auswertungsseite } from './zusatz.js';

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
    case 'angebote': return b ? html`<${Belegseite} key=${'a' + b} art="angebot" id=${b} />` : html`<${Belegliste} art="angebot" />`;
    case 'lieferscheine': return b ? html`<${Belegseite} key=${'l' + b} art="lieferschein" id=${b} />` : html`<${Belegliste} art="lieferschein" />`;
    case 'offen': return html`<${OffenePosten} />`;
    case 'serien': return html`<${Serienseite} />`;
    case 'auswertung': return html`<${Auswertungsseite} />`;
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
    window.addEventListener('stammneu', wechsel); // Einstellungen geändert: Menü neu zeichnen
    ladeStamm().then(() => setBereit(true)).catch((e) => setFehler(e.message));
    return () => window.removeEventListener('hashchange', wechsel);
  }, []);

  if (beendet) return html`<p class="laden">Marktrechnung wurde beendet. Dieses Fenster kann geschlossen werden.</p>`;
  if (fehler) return html`<div class="laden"><h1>Marktrechnung</h1><p>${fehler}</p></div>`;
  if (!bereit) return html`<p class="laden">Marktrechnung wird geladen …</p>`;

  const beende = async () => {
    if (!await frage('Marktrechnung beenden?', { ja: 'Beenden', titel: 'Programm beenden' })) return;
    // Das Programm schließt sein Fenster selbst; in einem Browser-Tab bleibt der Hinweis stehen.
    try { await api('POST', '/api/beenden'); setBeendet(true); setTimeout(() => window.close(), 300); } catch (e) { zeigeFehler(e); }
  };
  const aktiv = ort.teile[0] === 'rechnung' ? undefined : ort.teile[0];
  const zusatz = stamm.zusatz || {};
  const punkt = (ziel, name, schluessel) => html`<a href=${ziel} class=${aktiv === schluessel ? 'aktiv' : ''}>${name}</a>`;
  return html`<div class="rahmen">
    <aside class="seitenleiste">
      <div class="marke"><img src="symbol.svg" alt="" />Marktrechnung</div>
      <nav>
        ${punkt('#/', 'Rechnungen', undefined)}
        ${punkt('#/kunden', 'Kunden', 'kunden')}
        ${punkt('#/artikel', 'Artikel', 'artikel')}
        ${zusatz.angebote ? punkt('#/angebote', 'Angebote', 'angebote') : null}
        ${zusatz.lieferscheine ? punkt('#/lieferscheine', 'Lieferscheine', 'lieferscheine') : null}
        ${zusatz.serien ? punkt('#/serien', 'Serienrechnungen', 'serien') : null}
        ${zusatz.mahnungen ? punkt('#/offen', 'Offene Posten', 'offen') : null}
        ${zusatz.auswertung ? punkt('#/auswertung', 'Auswertung', 'auswertung') : null}
        ${punkt('#/pruefen', 'Import aus Lexoffice', 'pruefen')}
        ${punkt('#/einstellungen', 'Einstellungen', 'einstellungen')}
        ${punkt('#/hilfe', 'Hilfe', 'hilfe')}
      </nav>
      <div class="fuss">Version ${stamm.version}<br /><button onClick=${beende}>Programm beenden</button></div>
    </aside>
    <main><${Seite} ort=${ort} /></main>
    <${Toasts} />
    <${Fragen} />
  </div>`;
}

// Wie in einem gewöhnlichen Programm: Das Menü der rechten Maustaste gibt es
// nur dort, wo es etwas zu kopieren oder einzufügen gibt - in Eingabefeldern
// und auf markiertem Text.
document.addEventListener('contextmenu', (e) => {
  const feld = e.target.closest && e.target.closest('input, textarea, [contenteditable="true"]');
  if (!feld && !String(window.getSelection() || '')) e.preventDefault();
});

// Strg mit Plus, Minus oder Null vergrößert und verkleinert die Darstellung;
// die Wahl bleibt erhalten.
const stufen = [0.8, 0.9, 1, 1.1, 1.25, 1.4, 1.6];
const setzeGroesse = (faktor) => {
  document.documentElement.style.zoom = faktor === 1 ? '' : String(faktor);
  try { localStorage.setItem('groesse', String(faktor)); } catch (e) { /* ohne Speicher gilt die Wahl bis zum Schließen */ }
};
let groesse = 1;
try { groesse = stufen.includes(Number(localStorage.getItem('groesse'))) ? Number(localStorage.getItem('groesse')) : 1; } catch (e) { groesse = 1; }
if (groesse !== 1) setzeGroesse(groesse);
document.addEventListener('keydown', (e) => {
  if (!(e.ctrlKey || e.metaKey) || e.altKey) return;
  const schritt = (e.key === '+' || e.key === '=') ? 1 : e.key === '-' ? -1 : e.key === '0' ? 0 : null;
  if (schritt === null) return;
  e.preventDefault();
  const ort = stufen.indexOf(groesse);
  groesse = schritt === 0 ? 1 : stufen[Math.min(stufen.length - 1, Math.max(0, ort + schritt))];
  setzeGroesse(groesse);
});

render(html`<${App} />`, document.getElementById('app'));
