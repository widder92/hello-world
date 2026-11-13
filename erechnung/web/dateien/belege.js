// Angebote und Lieferscheine: Zusatzbelege, die sich in den Einstellungen
// einschalten lassen. Beide haben denselben Aufbau wie eine Rechnung, sind
// aber keine E-Rechnung und bleiben änderbar.
import {
  html, useState, useEffect, api, stamm, geld, datumDE, heute, gehe, zeige, zeigeFehler,
  Feld, Text, Mehrzeilig, Auswahl, Zahl, Dialog, Kasten, PdfDialog, ladeDatei, frage,
} from './basis.js';

const ARTEN = {
  angebot: { ein: 'Angebot', viele: 'Angebote', pfad: 'angebote', neu: '+ Neues Angebot',
    leer: 'Noch kein Angebot geschrieben.', unter: 'Angebote an Kunden – aus einem Angebot wird mit einem Klick ein Lieferschein oder eine Rechnung' },
  lieferschein: { ein: 'Lieferschein', viele: 'Lieferscheine', pfad: 'lieferscheine', neu: '+ Neuer Lieferschein',
    leer: 'Noch kein Lieferschein geschrieben.', unter: 'Lieferscheine ohne Preise – die Rechnung dazu übernimmt Nummer, Markt und Positionen' },
};
const STAND = { offen: 'Offen', angenommen: 'Angenommen', abgelehnt: 'Abgelehnt', berechnet: 'Berechnet' };

const plusTage = (iso, tage) => { const d = new Date(iso + 'T12:00:00'); d.setDate(d.getDate() + tage); return d.toISOString().slice(0, 10); };

export function Belegliste({ art }) {
  const a = ARTEN[art];
  const [liste, setListe] = useState(null);
  useEffect(() => { setListe(null); api('GET', '/api/belege?art=' + art).then(setListe).catch(zeigeFehler); }, [art]);
  if (!liste) return html`<p class="laden">${a.viele} werden geladen …</p>`;
  return html`
    <div class="kopf">
      <div><h1>${a.viele}</h1><div class="leise">${a.unter}</div></div>
      <div class="knoepfe"><button class="haupt" onClick=${() => gehe(`#/${a.pfad}/neu`)}>${a.neu}</button></div>
    </div>
    <div class="karte">
      ${liste.length === 0 ? html`<div class="leer">${a.leer}</div>` : html`<table class="wahl">
        <thead><tr><th>Nummer</th><th>Datum</th><th>Kunde</th>${art === 'angebot' ? html`<th class="zahl">Betrag</th><th>Gültig bis</th>` : html`<th>Markt</th>`}<th>Stand</th></tr></thead>
        <tbody>${liste.map((b) => html`<tr key=${b.id} onClick=${() => gehe(`#/${a.pfad}/${b.id}`)}>
          <td><b>${b.nummer}</b></td><td class="nowrap">${datumDE(b.datum)}</td>
          <td>${b.kaeufer.name}${art === 'angebot' && b.lieferort ? html`<br /><span class="leise klein">${b.lieferort.name}</span>` : null}</td>
          ${art === 'angebot'
            ? html`<td class="zahl">${b.summen ? geld(b.summen.brutto) : ''}</td><td class="nowrap">${datumDE(b.gueltigBis)}</td>`
            : html`<td>${b.lieferort ? b.lieferort.name : html`<span class="leise">–</span>`}</td>`}
          <td>${STAND[b.status] || b.status}${b.folgebeleg ? html`<br /><span class="leise klein">${b.folgebeleg}</span>` : null}</td></tr>`)}</tbody>
      </table>`}
    </div>`;
}

export function Belegseite({ art, id }) {
  const a = ARTEN[art];
  const [b, setB] = useState(null);
  const [kunden, setKunden] = useState([]);
  const [artikel, setArtikel] = useState([]);
  const [einst, setEinst] = useState(null);
  const [pdf, setPdf] = useState(false);
  const [mail, setMail] = useState(null);
  const [laeuft, setLaeuft] = useState(false);
  const leerePosition = (e) => ({ bezeichnung: '', artikelnummer: '', gtin: '', beschreibung: '', menge: '1', einheit: e.standardEinheit || 'H87',
    einzelpreis: '0', rabattProzent: '0', steuerKat: 'S', steuerSatz: e.standardSteuer || '19' });
  useEffect(() => {
    setB(null);
    Promise.all([api('GET', '/api/kunden'), api('GET', '/api/artikel'), api('GET', '/api/einstellungen'), id === 'neu' ? null : api('GET', '/api/belege/' + id)])
      .then(([k, ar, e, vorhanden]) => {
        setKunden(k); setArtikel(ar); setEinst(e);
        setB(vorhanden || { id: '', belegart: art, nummer: '', datum: heute(), kundeId: '', lieferort: null, bestellnummer: '',
          gueltigBis: art === 'angebot' ? plusTage(heute(), e.zusatz.angebotTage || 30) : '', leistungsdatum: art === 'lieferschein' ? heute() : '',
          positionen: [leerePosition(e)], einleitung: art === 'angebot' ? 'Gern bieten wir Ihnen an:' : '', schluss: '', status: 'offen' });
      }).catch((e) => { zeigeFehler(e); gehe('#/' + a.pfad); });
  }, [id, art]);
  if (!b || !einst) return html`<p class="laden">${a.ein} wird geladen …</p>`;

  const kunde = kunden.find((k) => k.id === b.kundeId);
  const setze = (name) => (wert) => setB((x) => ({ ...x, [name]: wert }));
  const setzePos = (i, name, wert) => setB((x) => { const pos = [...x.positionen]; pos[i] = { ...pos[i], [name]: wert }; return { ...x, positionen: pos }; });
  const waehleArtikel = (i, name) => {
    const ar = artikel.find((x) => x.bezeichnung === name);
    setB((x) => {
      const pos = [...x.positionen];
      pos[i] = { ...pos[i], bezeichnung: name };
      if (ar) {
        pos[i] = { ...pos[i], artikelnummer: ar.artikelnummer || '', gtin: ar.gtin || '', beschreibung: ar.beschreibung || '',
          einheit: ar.einheit || pos[i].einheit, einzelpreis: ar.preis || '0', steuerSatz: ar.steuerSatz || pos[i].steuerSatz };
      }
      return { ...x, positionen: pos };
    });
  };
  const waehleKunde = (kid) => setB((x) => {
    const k = kunden.find((y) => y.id === kid);
    return { ...x, kundeId: kid, lieferort: k && k.lieferorte.length === 1 ? { ...k.lieferorte[0] } : null };
  });
  const waehleMarkt = (lid) => setB((x) => ({ ...x, lieferort: kunde ? (kunde.lieferorte.find((l) => l.id === lid) || null) : null }));

  const speichere = async () => {
    const gespeichert = await api('POST', '/api/belege', b);
    setB(gespeichert);
    if (id === 'neu') gehe(`#/${a.pfad}/${gespeichert.id}`);
    return gespeichert;
  };
  const tue = async (f) => { setLaeuft(true); try { await f(); } catch (e) { zeigeFehler(e); } finally { setLaeuft(false); } };
  const sichere = () => tue(async () => { const g = await speichere(); zeige(`${a.ein} ${g.nummer} gespeichert.`, 'gut'); });
  const zeigePdf = () => tue(async () => { await speichere(); setPdf(true); });
  const weiter = (ziel) => tue(async () => {
    const g = await speichere();
    const neu = await api('POST', `/api/belege/${g.id}/weiter/${ziel}`);
    if (ziel === 'rechnung') { zeige('Der Entwurf der Rechnung ist angelegt.', 'gut'); gehe('#/rechnung/' + neu.id); } else { zeige(`Lieferschein ${neu.nummer} ist angelegt.`, 'gut'); gehe('#/lieferscheine/' + neu.id); }
  });
  const loesche = async () => {
    if (!await frage(`${a.ein} ${b.nummer || ''} löschen?`, { ja: 'Löschen', gefahr: true, titel: a.ein + ' löschen' })) return;
    tue(async () => { if (b.id) await api('DELETE', '/api/belege/' + b.id); gehe('#/' + a.pfad); });
  };
  const oeffneMail = () => tue(async () => {
    const g = await speichere();
    setMail({ an: '', betreff: `${a.ein} ${g.nummer}`, text: `Guten Tag,\n\nim Anhang erhalten Sie unser${art === 'angebot' ? '' : 'en'} ${a.ein} ${g.nummer}.\n\nMit freundlichen Grüßen\n${einst.firma.name}` });
  });
  const sende = () => tue(async () => {
    const r = await api('POST', `/api/belege/${b.id}/senden`, mail);
    zeige('Verschickt an ' + r.an + '.', 'gut'); setMail(null);
  });

  const steuer = [['19', '19 %'], ['7', '7 %'], ['0', '0 %']];
  return html`
    <div class="kopf">
      <div><h1>${a.ein} ${b.nummer ? html`<span class="leise">· ${b.nummer}</span>` : html`<span class="leise">· neu</span>`}</h1>
        <div class="leise">${b.nummer ? `${STAND[b.status] || b.status}${b.folgebeleg ? ' · ' + b.folgebeleg : ''}` : 'Die Nummer wird beim ersten Speichern vergeben.'}</div></div>
      <div class="knoepfe">
        <button onClick=${() => gehe('#/' + a.pfad)}>Zur Liste</button>
        ${b.id && html`<button class="gefahr" onClick=${loesche}>Löschen</button>`}
        <button onClick=${zeigePdf} disabled=${laeuft}>Ansehen und drucken</button>
        <button class="haupt" onClick=${sichere} disabled=${laeuft}>Speichern</button>
      </div>
    </div>
    <div class="karte">
      <h2>Empfänger</h2>
      <div class="raster">
        <${Feld} titel="Kunde" breite="6"><${Auswahl} wert=${b.kundeId} setze=${waehleKunde} optionen=${[['', '– bitte wählen –'], ...kunden.map((k) => [k.id, k.name])]} /><//>
        <${Feld} titel=${art === 'lieferschein' ? 'Belieferter Markt' : 'Markt (falls das Angebot einen bestimmten betrifft)'} breite="6">
          <${Auswahl} wert=${b.lieferort ? b.lieferort.id : ''} setze=${waehleMarkt} disabled=${!kunde}
            optionen=${[['', kunde && kunde.lieferorte.length ? '– keiner –' : '– keiner hinterlegt –'], ...(kunde ? kunde.lieferorte.map((l) => [l.id, [l.name, [l.plz, l.ort].filter(Boolean).join(' ')].filter(Boolean).join(', ')]) : [])]} /><//>
      </div>
      ${!kunden.length && html`<${Kasten} art="warnung">Es ist noch kein Kunde angelegt. <a href="#/kunden/neu">Jetzt einen Kunden anlegen</a><//>`}
    </div>
    <div class="karte">
      <h2>Angaben</h2>
      <div class="raster">
        <${Feld} titel="Datum" breite="3"><${Text} typ="date" wert=${b.datum} setze=${setze('datum')} /><//>
        ${art === 'angebot'
          ? html`<${Feld} titel="Gültig bis" breite="3"><${Text} typ="date" wert=${b.gueltigBis || ''} setze=${setze('gueltigBis')} /><//>`
          : html`<${Feld} titel="Lieferdatum" breite="3"><${Text} typ="date" wert=${b.leistungsdatum || ''} setze=${setze('leistungsdatum')} /><//>`}
        <${Feld} titel="Bestellnummer des Kunden" breite="3"><${Text} wert=${b.bestellnummer || ''} setze=${setze('bestellnummer')} /><//>
        <${Feld} titel="Nummer" breite="3" hilfe="leer lassen = automatisch"><${Text} wert=${b.nummer || ''} setze=${setze('nummer')} /><//>
        <${Feld} titel="Text über den Positionen" breite="12"><${Mehrzeilig} wert=${b.einleitung || ''} setze=${setze('einleitung')} zeilen="2" /><//>
      </div>
    </div>
    <div class="karte">
      <h2>Positionen</h2>
      <datalist id="belegartikel">${artikel.map((x) => html`<option value=${x.bezeichnung}>${[x.artikelnummer, x.gtin].filter(Boolean).join(' · ')}</option>`)}</datalist>
      ${b.positionen.map((p, i) => html`<div class="position" key=${i}>
        <div class="zeile1">
          <div class="nr">${i + 1}</div>
          <label>Bezeichnung<input list="belegartikel" value=${p.bezeichnung} placeholder="Artikel eintippen oder aus der Liste wählen" onInput=${(e) => waehleArtikel(i, e.target.value)} /></label>
          <button class="weg" title="Position entfernen" disabled=${b.positionen.length <= 1}
            onClick=${() => setB((x) => ({ ...x, positionen: x.positionen.filter((_, j) => j !== i) }))}>✕</button>
        </div>
        <div class="zeileB">
          <div></div>
          <label>Menge<${Zahl} wert=${p.menge} max="3" setze=${(v) => setzePos(i, 'menge', v)} /></label>
          <label>Einheit<${Auswahl} wert=${p.einheit} setze=${(v) => setzePos(i, 'einheit', v)} optionen=${stamm.einheiten.map((e) => [e.code, e.name])} /></label>
          ${art === 'angebot' ? html`
            <label>Einzelpreis netto<${Zahl} wert=${p.einzelpreis} min="2" max="4" setze=${(v) => setzePos(i, 'einzelpreis', v)} /></label>
            <label>Rabatt %<${Zahl} wert=${p.rabattProzent} max="2" leerBeiNull setze=${(v) => setzePos(i, 'rabattProzent', v)} /></label>
            <label>USt<${Auswahl} wert=${String(Number(p.steuerSatz))} setze=${(v) => setzePos(i, 'steuerSatz', v)}
              optionen=${steuer.some(([w]) => w === String(Number(p.steuerSatz))) ? steuer : [...steuer, [String(Number(p.steuerSatz)), Number(p.steuerSatz) + ' %']]} /></label>
            <div class="netto">${geld(Number(p.menge || 0) * Number(p.einzelpreis || 0) * (1 - Number(p.rabattProzent || 0) / 100))}</div>`
          : html`<label style="grid-column: span 3">Zusatztext<input value=${p.beschreibung || ''} onInput=${(e) => setzePos(i, 'beschreibung', e.target.value)} /></label><div></div>`}
        </div>
      </div>`)}
      <button onClick=${() => setB((x) => ({ ...x, positionen: [...x.positionen, leerePosition(einst)] }))}>+ Position hinzufügen</button>
      ${art === 'lieferschein' && html`<p class="leise klein" style="margin:.6rem 0 0">Der Lieferschein nennt Ware und Menge, keine Preise. Die Preise der Artikel bleiben hinterlegt und erscheinen in der Rechnung.</p>`}
    </div>
    <div class="karte">
      <h2>Schlusstext</h2>
      <${Mehrzeilig} wert=${b.schluss || ''} setze=${setze('schluss')} zeilen="2" />
    </div>
    ${b.id && html`<div class="karte">
      <h2>Wie es weitergeht</h2>
      <p style="display:flex;gap:.5rem;flex-wrap:wrap;margin:0">
        ${art === 'angebot' && html`<button onClick=${() => weiter('lieferschein')} disabled=${laeuft}>Lieferschein daraus machen</button>`}
        <button class="haupt" onClick=${() => weiter('rechnung')} disabled=${laeuft}>Rechnung daraus machen</button>
        <button onClick=${oeffneMail} disabled=${laeuft}>Per E-Mail senden …</button>
        <button onClick=${() => ladeDatei(`/api/belege/${b.id}/pdf?laden=1`)}>Als PDF speichern</button>
        ${art === 'angebot' && b.status === 'offen' && html`<button class="schlicht" onClick=${() => { setB((x) => ({ ...x, status: 'abgelehnt' })); zeige('Als abgelehnt vermerkt – bitte speichern.'); }}>Als abgelehnt vermerken</button>`}
      </p>
      <p class="leise klein" style="margin:.6rem 0 0">Die Rechnung entsteht als Entwurf mit denselben Positionen${art === 'lieferschein' ? ', der Nummer dieses Lieferscheins und dem Lieferdatum' : ''}; dort wird sie geprüft und fertiggestellt.</p>
    </div>`}
    ${pdf && html`<${PdfDialog} titel=${a.ein + ' ' + b.nummer} adresse=${`/api/belege/${b.id}/pdf?z=${Date.now()}`} zu=${() => setPdf(false)} />`}
    ${mail && html`<${Dialog} titel=${a.ein + ' per E-Mail senden'} zu=${() => setMail(null)} aktionen=${html`
        <button onClick=${() => setMail(null)}>Abbrechen</button><button class="haupt" onClick=${sende} disabled=${laeuft}>Senden</button>`}>
      <div class="raster">
        <${Feld} titel="An" breite="12"><${Text} typ="email" wert=${mail.an} setze=${(v) => setMail({ ...mail, an: v })} /><//>
        <${Feld} titel="Betreff" breite="12"><${Text} wert=${mail.betreff} setze=${(v) => setMail({ ...mail, betreff: v })} /><//>
        <${Feld} titel="Text" breite="12"><${Mehrzeilig} wert=${mail.text} setze=${(v) => setMail({ ...mail, text: v })} zeilen="7" /><//>
      </div>
      <p class="leise klein">Angehängt wird das PDF. Verschickt wird über das unter Einstellungen eingerichtete Postfach.</p>
    <//>`}`;
}
