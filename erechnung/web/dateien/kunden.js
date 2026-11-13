// Kunden: Rechnungsempfänger mit ihren Märkten und Versandangaben.
import {
  html, useState, useEffect, api, stamm, gehe, zeige, zeigeFehler, Feld, Text, Mehrzeilig, Auswahl, Zahl, Kasten, Dialog, glnOk, setzeIn,
} from './basis.js';

export function Kundenliste() {
  const [liste, setListe] = useState(null);
  const lade = () => api('GET', '/api/kunden').then(setListe).catch(zeigeFehler);
  useEffect(() => { lade(); }, []);
  const liesDatei = async (datei) => {
    if (!datei) return;
    try {
      let inhalt;
      try { inhalt = JSON.parse(await datei.text()); } catch (e) { throw new Error('Die Datei ist keine Stammdaten-Datei von Marktrechnung.'); }
      const a = await api('POST', '/api/stammdaten', inhalt);
      zeige(`Eingelesen: ${a.kundenNeu} Kunden neu, ${a.kundenErgaenzt} ergänzt, ${a.maerkteNeu} Märkte neu${a.firma ? ', eigene Firmendaten ergänzt' : ''}.`, 'gut');
      lade();
    } catch (e) { zeigeFehler(e); }
  };
  if (!liste) return html`<p class="laden">Kunden werden geladen …</p>`;
  const profil = (id) => (stamm.profile.find((p) => p.id === id) || {}).name || '';
  return html`
    <div class="kopf">
      <div><h1>Kunden</h1><div class="leise">Rechnungsempfänger, ihre Märkte und wohin die Rechnungen gehen</div></div>
      <div class="knoepfe">
        <label class="knopf" title="Kunden und Märkte aus einer Stammdaten-Datei übernehmen. Vorhandenes bleibt erhalten.">Aus Datei einlesen …<input type="file" accept=".json,application/json" hidden
          onChange=${(e) => { liesDatei(e.target.files[0]); e.target.value = ''; }} /></label>
        <button class="haupt" onClick=${() => gehe('#/kunden/neu')}>+ Neuer Kunde</button>
      </div>
    </div>
    <div class="karte">
      ${liste.length === 0 ? html`<div class="leer">Noch kein Kunde angelegt.</div>` : html`<table class="wahl">
        <thead><tr><th>Name</th><th>Ort</th><th>GLN</th><th>Märkte</th><th>Vorgaben</th><th>Rechnungen an</th></tr></thead>
        <tbody>${liste.map((k) => html`<tr key=${k.id} onClick=${() => gehe('#/kunden/' + k.id)}>
          <td><b>${k.name}</b></td><td>${k.plz} ${k.ort}</td>
          <td>${k.gln || html`<span style="color:var(--rot)">fehlt</span>`}</td>
          <td>${k.lieferorte.length}</td><td>${profil(k.profil)}</td>
          <td>${k.rechnungsEmail || html`<span class="leise">–</span>`}</td></tr>`)}</tbody>
      </table>`}
    </div>`;
}

const leererKunde = () => ({ id: '', name: '', strasse: '', zusatz: '', plz: '', ort: '', land: 'DE', ustId: '', gln: '', kennung: '', alias: '',
  lieferantennummer: '', kaeuferreferenz: '', profil: 'handel', format: 'zugferd', rechnungsEmail: '', emailBetreff: '', emailText: '',
  zieltage: 0, skontoTage: 0, skontoProzent: '0', lieferorte: [], notiz: '' });

function GlnFeld({ titel, wert, setze, feld, breite = 4, pflicht }) {
  const falsch = wert && !/^\d{13}$/.test(wert) ? 'Eine GLN hat genau 13 Ziffern.'
    : wert && !glnOk(wert) ? 'Prüfziffer falsch – bitte auf Tippfehler prüfen.' : '';
  return html`<${Feld} titel=${titel} breite=${breite} falsch=${falsch} hilfe=${pflicht && !wert ? 'vom Handel verlangt' : ''}>
    <${Text} feld=${feld} wert=${wert} setze=${(v) => setze(v.replace(/\s/g, ''))} inputmode="numeric" maxlength="13" /><//>`;
}

export function Kundenseite({ id, zurueck }) {
  const [k, setK] = useState(null);
  const [vorlagen, setVorlagen] = useState(id === 'neu');
  const [speichert, setSpeichert] = useState(false);
  const [marktSuche, setMarktSuche] = useState('');
  const [offen, setOffen] = useState({});
  useEffect(() => {
    if (id === 'neu') { setK(leererKunde()); return; }
    api('GET', '/api/kunden').then((l) => {
      const x = l.find((y) => y.id === id);
      if (!x) { zeige('Diesen Kunden gibt es nicht mehr.', 'fehler'); gehe('#/kunden'); return; }
      setK(x);
    }).catch(zeigeFehler);
  }, [id]);
  if (!k) return html`<p class="laden">Kunde wird geladen …</p>`;

  const setze = (pfad) => (wert) => setK((x) => setzeIn(x, pfad, wert));
  const profil = stamm.profile.find((p) => p.id === k.profil);
  const ziel = zurueck ? '#/rechnung/' + zurueck : '#/kunden';
  const speichere = async () => {
    setSpeichert(true);
    try {
      await api('POST', '/api/kunden', { ...k, zieltage: Number(k.zieltage) || 0, skontoTage: Number(k.skontoTage) || 0 });
      zeige('Kunde gespeichert.', 'gut');
      gehe(ziel);
    } catch (e) { zeigeFehler(e); } finally { setSpeichert(false); }
  };
  const loesche = async () => {
    if (!confirm(`Kunde „${k.name}“ löschen?\n\nBestehende Rechnungen bleiben unverändert erhalten.`)) return;
    try { await api('DELETE', '/api/kunden/' + k.id); gehe('#/kunden'); } catch (e) { zeigeFehler(e); }
  };
  const viele = k.lieferorte.length > 6;
  const alleVorlagen = stamm.profile.flatMap((p) => (p.vorlagen || []).map((v) => ({ ...v, profil: p.name })));

  return html`
    <div class="kopf">
      <div><h1>${id === 'neu' ? 'Neuer Kunde' : k.name || 'Kunde'}</h1></div>
      <div class="knoepfe">
        <button onClick=${() => gehe(ziel)}>Abbrechen</button>
        ${id !== 'neu' && html`<button class="gefahr" onClick=${loesche}>Löschen</button>`}
        <button class="haupt" disabled=${speichert} onClick=${speichere}>Speichern</button>
      </div>
    </div>

    ${vorlagen && html`<${Dialog} titel="Neuen Kunden anlegen" zu=${() => setVorlagen(false)} aktionen=${html`<button onClick=${() => setVorlagen(false)}>Ohne Vorlage beginnen</button>`}>
      <p>Für REWE und EDEKA gibt es Vorlagen, in denen die Vorgaben schon eingestellt sind:</p>
      ${alleVorlagen.map((v) => html`<button style="display:block;width:100%;text-align:left;white-space:normal;margin-bottom:.5rem" onClick=${() => {
        setK({ ...leererKunde(), ...v.kunde, lieferorte: [], vorlagenHinweis: v.hinweis });
        setVorlagen(false);
      }}><b>${v.titel}</b><br /><span class="leise klein">${v.profil}</span></button>`)}
    <//>`}

    ${k.vorlagenHinweis && html`<${Kasten} art="warnung"><b>Bitte ergänzen und prüfen:</b> ${k.vorlagenHinweis}<//>`}

    <div class="spalten">
      <div>
        <div class="karte">
          <h2>Rechnungsempfänger</h2>
          <div class="raster">
            <${Feld} titel="Name der Firma (genau wie im Handelsregister)" breite="8"><${Text} feld="kaeufer.name" wert=${k.name} setze=${setze('name')} /><//>
            <${GlnFeld} titel="GLN des Rechnungsempfängers" feld="kaeufer.gln" wert=${k.gln} setze=${setze('gln')} pflicht />
            <${Feld} titel="Straße und Hausnummer" breite="6"><${Text} feld="kaeufer.strasse" wert=${k.strasse} setze=${setze('strasse')} /><//>
            <${Feld} titel="Adresszusatz" breite="6"><${Text} wert=${k.zusatz} setze=${setze('zusatz')} /><//>
            <${Feld} titel="PLZ" breite="2"><${Text} wert=${k.plz} setze=${setze('plz')} /><//>
            <${Feld} titel="Ort" breite="6"><${Text} wert=${k.ort} setze=${setze('ort')} /><//>
            <${Feld} titel="Land" breite="4"><${Auswahl} wert=${k.land || 'DE'} setze=${setze('land')} optionen=${stamm.laender} /><//>
            <${Feld} titel="USt-IdNr. des Kunden" breite="4"><${Text} feld="kaeufer.ustId" wert=${k.ustId} setze=${setze('ustId')} placeholder="DE123456789" /><//>
            <${Feld} titel="Unsere Lieferantennummer dort" breite="4" hilfe="Kreditorennummer, die der Kunde vergeben hat"><${Text} feld="verkaeufer.kennung" wert=${k.lieferantennummer} setze=${setze('lieferantennummer')} /><//>
            <${Feld} titel="Kundennummer bei uns" breite="4" hilfe="nur für das PDF"><${Text} wert=${k.kennung} setze=${setze('kennung')} /><//>
            <${Feld} titel="Name in Lexware Office, falls er dort anders lautet" breite="12" hilfe="Daran erkennt der Import den Kunden wieder. Mehrere Namen mit Semikolon trennen. In der Rechnung erscheint er nicht.">
              <${Text} wert=${k.alias || ''} setze=${setze('alias')} placeholder="z. B. eine Kurzbezeichnung" /><//>
          </div>
        </div>

        <div class="karte">
          <h2>Belieferte Märkte (Warenempfänger)</h2>
          <p class="leise klein">Jeder Markt mit seiner eigenen GLN. Beim Schreiben der Rechnung wird der Markt nur noch ausgewählt.</p>
          ${viele && html`<input style="margin-bottom:.6rem" placeholder=${`In ${k.lieferorte.length} Märkten suchen: Name, Ort, Straße oder GLN`} value=${marktSuche} onInput=${(e) => setMarktSuche(e.target.value)} />`}
          ${k.lieferorte.map((l, i) => {
            const s = (n) => setze(`lieferorte.${i}.${n}`);
            const wort = marktSuche.trim().toLowerCase();
            if (viele && wort && ![l.name, l.ort, l.strasse, l.plz, l.gln, l.kennung].some((t) => (t || '').toLowerCase().includes(wort))) return null;
            const unvollstaendig = !l.gln || !l.strasse || !l.plz || !l.ort;
            if (viele && !offen[i]) {
              return html`<div class="position" key=${i} style="display:flex;gap:.8rem;align-items:center;padding:.45rem .8rem;margin-bottom:.35rem">
                <span style="flex:1;min-width:0"><b>${l.name || 'ohne Namen'}</b> <span class="leise klein">· ${[l.strasse, (l.plz + ' ' + l.ort).trim()].filter(Boolean).join(', ')} · GLN ${l.gln || 'fehlt'}</span>
                  ${unvollstaendig ? html` <span class="klein" style="color:var(--rot)">unvollständig</span>` : null}</span>
                <button class="schlicht" onClick=${() => setOffen({ ...offen, [i]: true })}>Bearbeiten</button>
              </div>`;
            }
            return html`<div class="position" key=${i}>
              <div class="raster">
                <${Feld} titel="Name des Markts" breite="5"><${Text} wert=${l.name} setze=${s('name')} /><//>
                <${GlnFeld} titel="GLN des Markts" feld=${i === 0 ? 'lieferort.gln' : ''} wert=${l.gln} setze=${s('gln')} breite="4" pflicht />
                <${Feld} titel="Marktnummer" breite="3"><${Text} wert=${l.kennung} setze=${s('kennung')} /><//>
                <${Feld} titel="Straße und Hausnummer" breite="5"><${Text} feld=${i === 0 ? 'lieferort.strasse' : ''} wert=${l.strasse} setze=${s('strasse')} /><//>
                <${Feld} titel="PLZ" breite="2"><${Text} wert=${l.plz} setze=${s('plz')} /><//>
                <${Feld} titel="Ort" breite="5"><${Text} wert=${l.ort} setze=${s('ort')} /><//>
                <${Feld} titel="Kostenstelle" breite="5" hilfe=${k.profil === 'rewe' ? 'nur für Kostenrechnungen an REWE (10 Ziffern)' : 'falls der Kunde eine nennt'}><${Text} wert=${l.kostenstelle} setze=${s('kostenstelle')} /><//>
                <div class="feld b7" style="grid-column:span 7;justify-content:flex-end;align-items:flex-end">
                  ${viele && html`<button class="schlicht" onClick=${() => setOffen({ ...offen, [i]: false })}>Zuklappen</button>`}
                  <button class="schlicht" onClick=${() => { setOffen({}); setK((x) => ({ ...x, lieferorte: x.lieferorte.filter((_, j) => j !== i) })); }}>Markt entfernen</button></div>
              </div>
            </div>`;
          })}
          <div style="display:flex;gap:.5rem;flex-wrap:wrap">
            <button onClick=${() => { setOffen({ ...offen, [k.lieferorte.length]: true }); setMarktSuche(''); setK((x) => ({ ...x, lieferorte: [...x.lieferorte, { id: '', name: '', gln: '', kennung: '', strasse: '', plz: '', ort: '', land: 'DE', kostenstelle: '' }] })); }}>+ Markt hinzufügen</button>
            ${k.lieferorte.length === 0 && html`<button title="Für selbstständige Kaufleute: Der Rechnungsempfänger ist zugleich der belieferte Markt."
              onClick=${() => setK((x) => ({ ...x, lieferorte: [{ id: '', name: x.name, gln: x.gln, kennung: '', strasse: x.strasse, plz: x.plz, ort: x.ort, land: x.land || 'DE', kostenstelle: '' }] }))}>Der Rechnungsempfänger ist selbst der Markt</button>`}
          </div>
        </div>

        <div class="karte">
          <h2>Versand und Zahlung</h2>
          <div class="raster">
            <${Feld} titel="Vorgaben des Kunden" breite="4"><${Auswahl} wert=${k.profil} setze=${setze('profil')}
              optionen=${[...stamm.profile.map((p) => [p.id, p.name]), ['', 'keine besonderen']]} /><//>
            <${Feld} titel="Format der E-Rechnung" breite="4"><${Auswahl} wert=${k.format || 'zugferd'} setze=${setze('format')} optionen=${stamm.formate.map((f) => [f.id, f.name])} /><//>
            <${Feld} titel="Käuferreferenz" breite="4" hilfe="nur falls der Kunde eine feste vorgibt (Leitweg-ID)"><${Text} wert=${k.kaeuferreferenz} setze=${setze('kaeuferreferenz')} /><//>
            <${Feld} titel="E-Mail-Adresse für Rechnungen" breite="8" hilfe="das Rechnungspostfach des Kunden – nicht die Adresse des Ansprechpartners">
              <${Text} typ="email" wert=${k.rechnungsEmail} setze=${setze('rechnungsEmail')} /><//>
            <${Feld} titel="Zahlungsziel in Tagen" breite="4"><${Text} typ="number" min="0" wert=${String(k.zieltage || 0)} setze=${setze('zieltage')} /><//>
            <${Feld} titel="Skonto: Frist in Tagen" breite="4"><${Text} typ="number" min="0" wert=${String(k.skontoTage || 0)} setze=${setze('skontoTage')} /><//>
            <${Feld} titel="Skonto in Prozent" breite="4"><${Zahl} wert=${k.skontoProzent} max="2" leerBeiNull setze=${setze('skontoProzent')} /><//>
          </div>
          <details><summary>Eigener Betreff und Text für die E-Mail</summary>
            <div class="raster">
              <${Feld} titel="Betreff" breite="12" hilfe="Platzhalter: {NUMMER} {DATUM} {BELEG} {FIRMA} {BETRAG}. Leer = „Rechnung {NUMMER}“"><${Text} wert=${k.emailBetreff} setze=${setze('emailBetreff')} /><//>
              <${Feld} titel="Text" breite="12"><${Mehrzeilig} zeilen="5" wert=${k.emailText} setze=${setze('emailText')} /><//>
            </div>
          </details>
          <div class="raster" style="margin-top:.6rem"><${Feld} titel="Eigene Notiz (erscheint nirgends)" breite="12"><${Mehrzeilig} zeilen="2" wert=${k.notiz} setze=${setze('notiz')} /><//></div>
        </div>
      </div>

      <div class="haftend">
        ${profil ? html`<div class="karte klein">
          <h2>${profil.name}: darauf kommt es an</h2>
          <ul style="padding-left:1.1rem;margin:0">${profil.hinweise.map((h) => html`<li style="margin-bottom:.55rem">${h}</li>`)}</ul>
          <details><summary>Quellen</summary><ul style="padding-left:1.1rem">${profil.quellen.map((q) => html`<li style="overflow-wrap:anywhere">${q}</li>`)}</ul>
            <p class="leise">Stand November 2026. Vorgaben können sich ändern – maßgeblich ist, was der Kunde mitteilt.</p></details>
        </div>` : html`<div class="karte klein"><h2>Ohne besondere Vorgaben</h2><p>Die Rechnung wird nur gegen die allgemeinen Regeln der E-Rechnung geprüft.</p></div>`}
      </div>
    </div>`;
}
