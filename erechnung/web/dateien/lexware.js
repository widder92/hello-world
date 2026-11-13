// Direkter Abruf aus Lexware Office (lexoffice): Rechnungen, Kunden und
// Artikel über die Schnittstelle holen, ohne Dateien herunterzuladen.
import { html, useState, useEffect, api, geld, datumDE, zeige, zeigeFehler, Kasten, frage } from './basis.js';

function vorTagen(n) {
  const d = new Date(Date.now() - n * 86400000);
  const z = (x) => String(x).padStart(2, '0');
  return `${d.getFullYear()}-${z(d.getMonth() + 1)}-${z(d.getDate())}`;
}

const STAND = { neu: 'neu', entwurf: 'schon als Entwurf übernommen', vorhanden: 'schon fertiggestellt' };

// nimm(liste) bekommt die eingelesenen Belege als [{name, erg}].
export function LexwareAbruf({ profil, nimm }) {
  const [verbunden, setVerbunden] = useState(null);
  const [schluessel, setSchluessel] = useState('');
  const [von, setVon] = useState(vorTagen(30));
  const [belege, setBelege] = useState(null);
  const [auswahl, setAuswahl] = useState({});
  const [laeuft, setLaeuft] = useState('');

  useEffect(() => { api('GET', '/api/lexware').then((s) => setVerbunden(s.verbunden)).catch(() => setVerbunden(false)); }, []);

  const tue = async (was, f) => {
    setLaeuft(was);
    try { await f(); } catch (e) { zeigeFehler(e); } finally { setLaeuft(''); }
  };
  const verbinde = () => tue('verbinden', async () => {
    const a = await api('POST', '/api/lexware/schluessel', { schluessel });
    setVerbunden(true); setSchluessel('');
    zeige(`Verbunden mit Lexware Office${a.firma ? ' – ' + a.firma : ''}.`, 'gut');
  });
  const trenne = () => tue('trennen', async () => {
    if (!await frage('Die Verbindung zu Lexware Office trennen? Der gespeicherte Zugangsschlüssel wird gelöscht.', { ja: 'Trennen', titel: 'Verbindung trennen' })) return;
    await api('POST', '/api/lexware/schluessel', { schluessel: '' });
    setVerbunden(false); setBelege(null);
  });
  const zeigeBelege = () => tue('liste', async () => {
    const l = await api('GET', '/api/lexware/belege?von=' + encodeURIComponent(von));
    setBelege(l);
    setAuswahl(Object.fromEntries(l.filter((b) => b.stand === 'neu').map((b) => [b.id, true])));
  });
  const gewaehlt = (belege || []).filter((b) => auswahl[b.id]);
  const liesEin = () => tue('einlesen', async () => {
    // In kleinen Portionen, damit bei vielen Belegen der Fortschritt sichtbar bleibt.
    let n = 0;
    for (let i = 0; i < gewaehlt.length; i += 5) {
      const teil = gewaehlt.slice(i, i + 5).map((b) => ({ id: b.id, typ: b.typ, nummer: b.nummer }));
      setLaeuft(`einlesen ${Math.min(i + 5, gewaehlt.length)}/${gewaehlt.length}`);
      nimm(await api('POST', '/api/lexware/abrufen', { belege: teil, profil }));
      n += teil.length;
    }
    setAuswahl({});
    zeige(n === 1 ? 'Ein Beleg eingelesen – siehe unten.' : `${n} Belege eingelesen – siehe unten.`, 'gut');
  });
  const gleicheKunden = () => tue('kunden', async () => {
    const a = await api('POST', '/api/lexware/kunden');
    zeige(`Kunden abgeglichen: ${a.gefunden} Firmenkunden in Lexware Office, ${a.neu} neu angelegt, ${a.ergaenzt} ergänzt.`, 'gut');
  });
  const gleicheArtikel = () => tue('artikel', async () => {
    const a = await api('POST', '/api/lexware/artikel');
    zeige(`Artikel abgeglichen: ${a.gefunden} in Lexware Office, ${a.neu} neu angelegt, ${a.geaendert} aktualisiert.`, 'gut');
    if (a.hinweise.length) zeige(a.hinweise.join(' · '));
  });

  if (verbunden === null) return null;
  return html`<div class="karte">
    <h2>Direkt aus Lexware Office abrufen</h2>
    ${!verbunden ? html`
      <p class="leise klein">Statt Dateien herunterzuladen, kann das Programm Rechnungen, Kunden und Artikel direkt aus Lexware Office holen. Es liest nur – in Lexware Office wird nichts verändert.</p>
      <details><summary>Verbindung einrichten</summary>
        <ol class="klein">
          <li>In Lexware Office anmelden und die Seite <b>app.lexware.de/addons/public-api</b> öffnen.</li>
          <li>Dort einen Schlüssel („API-Key“) erzeugen. Leserechte für Rechnungen, Kontakte und Artikel genügen.</li>
          <li>Den Schlüssel hier einfügen. Er wird geschützt auf diesem Rechner gespeichert.</li>
        </ol>
        <div class="raster">
          <label class="feld b8"><span>Zugangsschlüssel von Lexware Office</span>
            <input type="password" autocomplete="off" value=${schluessel} onInput=${(e) => setSchluessel(e.target.value)} /></label>
          <div class="feld b4" style="justify-content:flex-end"><button class="haupt" disabled=${!schluessel.trim() || !!laeuft} onClick=${verbinde}>${laeuft === 'verbinden' ? 'Wird geprüft …' : 'Verbinden'}</button></div>
        </div>
      </details>` : html`
      <div class="raster">
        <label class="feld b3"><span>Belege ab dem</span><input type="date" value=${von} onInput=${(e) => setVon(e.target.value)} /></label>
        <div class="feld b9" style="flex-direction:row;align-items:flex-end;gap:.5rem;flex-wrap:wrap">
          <button class="haupt" disabled=${!!laeuft} onClick=${zeigeBelege}>${laeuft === 'liste' ? 'Wird geladen …' : 'Rechnungen anzeigen'}</button>
          <button disabled=${!!laeuft} onClick=${gleicheKunden} title="Firmenkunden aus Lexware Office in die Stammdaten holen">${laeuft === 'kunden' ? 'Kunden werden geholt …' : 'Kunden abgleichen'}</button>
          <button disabled=${!!laeuft} onClick=${gleicheArtikel} title="Artikel mit Preis und GTIN aus Lexware Office holen">${laeuft === 'artikel' ? 'Artikel werden geholt …' : 'Artikel abgleichen'}</button>
          <button class="schlicht" disabled=${!!laeuft} onClick=${trenne}>Verbindung trennen</button>
        </div>
      </div>
      ${belege && belege.length === 0 && html`<p class="leise" style="margin-top:.8rem">In diesem Zeitraum gibt es in Lexware Office keine Rechnungen.</p>`}
      ${belege && belege.length > 0 && html`
        <table style="margin-top:.8rem">
          <thead><tr><th><input type="checkbox" title="alle neuen auswählen" checked=${gewaehlt.length > 0 && gewaehlt.length === belege.filter((b) => b.stand !== 'vorhanden').length}
            onChange=${(e) => setAuswahl(e.target.checked ? Object.fromEntries(belege.filter((b) => b.stand !== 'vorhanden').map((b) => [b.id, true])) : {})} /></th>
            <th>Nummer</th><th>Datum</th><th>Kunde</th><th class="zahl">Betrag</th><th>Stand hier</th></tr></thead>
          <tbody>${belege.map((b) => html`<tr key=${b.id}>
            <td><input type="checkbox" disabled=${b.stand === 'vorhanden'} checked=${!!auswahl[b.id]} onChange=${(e) => setAuswahl({ ...auswahl, [b.id]: e.target.checked })} /></td>
            <td class="nowrap"><b>${b.nummer}</b>${b.typ === 'creditnote' ? html`<br /><span class="leise klein">Gutschrift</span>` : null}</td>
            <td class="nowrap">${datumDE(b.datum)}</td><td>${b.kunde}</td><td class="zahl">${geld(b.betrag)}</td>
            <td class=${b.stand === 'neu' ? '' : 'leise'}>${STAND[b.stand] || b.stand}</td></tr>`)}</tbody>
        </table>
        <p style="margin-top:.8rem"><button class="haupt" disabled=${!gewaehlt.length || !!laeuft} onClick=${liesEin}>
          ${laeuft.startsWith('einlesen') ? 'Wird eingelesen … ' + laeuft.slice(9) : gewaehlt.length === 1 ? 'Ausgewählten Beleg einlesen' : `${gewaehlt.length} ausgewählte Belege einlesen`}</button>
          <span class="leise klein"> Eingelesene Belege erscheinen unten in der Liste und werden dort geprüft.</span></p>`}
    `}
  </div>`;
}
