// Import: Rechnungen aus Lexware Office (lexoffice) oder einem anderen
// Programm einlesen - mehrere auf einmal -, prüfen und als Entwürfe
// übernehmen. Was in der Datei fehlt, ergänzt das Programm aus den eigenen
// Stammdaten.
import {
  html, useState, api, stamm, geld, datumDE, gehe, zeige, zeigeFehler, Auswahl, Haken, Kasten, Meldungsliste, zaehle,
} from './basis.js';
import { LexwareAbruf } from './lexware.js';

let laufendeNr = 0;

export function Pruefseite() {
  const [profil, setProfil] = useState('auto');
  const [artikelAnlegen, setArtikelAnlegen] = useState(true);
  const [eintraege, setEintraege] = useState([]);
  const [ueber, setUeber] = useState(false);
  const [beschaeftigt, setBeschaeftigt] = useState(false);

  const aendere = (nr, neu) => setEintraege((l) => l.map((e) => (e.nr === nr ? { ...e, ...neu } : e)));

  const pruefeEine = async (e, p) => {
    try {
      const erg = await api('POST', `/api/import?profil=${encodeURIComponent(p)}&name=${encodeURIComponent(e.name)}`, e.datei);
      aendere(e.nr, { erg, laeuft: false });
    } catch (f) { aendere(e.nr, { laeuft: false, erg: { fehler: f.message, meldungen: [], auffaellig: [] } }); }
  };
  const nimmDateien = async (dateien) => {
    const neu = [...dateien].map((datei) => ({ nr: ++laufendeNr, datei, name: datei.name, laeuft: true, erg: null, offen: false, entwurf: '' }));
    if (!neu.length) return;
    const alle = [...eintraege, ...neu];
    if (alle.length === 1) neu[0].offen = true;
    setEintraege(alle);
    for (const e of neu) await pruefeEine(e, profil);
  };
  const pruefeAlleNeu = async (p) => {
    setEintraege((l) => l.map((e) => (e.entwurf ? e : { ...e, laeuft: true })));
    for (const e of eintraege) if (!e.entwurf && e.datei) await pruefeEine(e, p);
  };
  const nimmAbruf = (liste) => {
    const neu = liste.map((x) => ({ nr: ++laufendeNr, datei: null, name: x.name, laeuft: false, erg: x.erg, offen: false, entwurf: '' }));
    setEintraege((l) => [...l, ...neu]);
  };

  const uebernimm = async (e, danachOeffnen) => {
    try {
      const r = await api('POST', '/api/import/uebernehmen', { rechnung: e.erg.rechnung, artikelAnlegen });
      aendere(e.nr, { entwurf: r.id });
      if (danachOeffnen) { zeige('Als Entwurf übernommen. Bitte die fehlenden Angaben ergänzen.', 'gut'); gehe('#/rechnung/' + r.id); }
      return true;
    } catch (f) { zeigeFehler(f); return false; }
  };
  const bereit = (e) => e.erg && e.erg.lesbar && e.erg.uebernehmen && e.erg.stand !== 'vorhanden' && !e.entwurf;
  const uebernimmAlle = async () => {
    setBeschaeftigt(true);
    let n = 0;
    for (const e of eintraege.filter(bereit)) if (await uebernimm(e, false)) n++;
    setBeschaeftigt(false);
    if (n) zeige(n === 1 ? 'Eine Rechnung als Entwurf übernommen.' : `${n} Rechnungen als Entwürfe übernommen.`, 'gut');
  };
  const uebernimmFirma = async (e) => {
    try {
      await api('POST', '/api/import/firma', { verkaeufer: e.erg.rechnung.verkaeufer, zahlung: e.erg.rechnung.zahlung });
      Object.assign(stamm, await api('GET', '/api/stamm'));
      zeige('Die Firmendaten wurden in die Einstellungen übernommen. Bitte dort prüfen und ergänzen.', 'gut');
      pruefeAlleNeu(profil);
    } catch (f) { zeigeFehler(f); }
  };

  const fertig = eintraege.filter((e) => e.erg);
  const ohneFirma = fertig.find((e) => e.erg.lesbar && e.erg.firmaLeer);
  const anzahlBereit = eintraege.filter(bereit).length;
  const uebernommen = eintraege.filter((e) => e.entwurf).length;

  return html`
    <div class="kopf"><div><h1>Import aus Lexoffice</h1>
      <div class="leise">Rechnungen aus Lexware Office (lexoffice) oder einem anderen Programm einlesen, prüfen und als Entwürfe übernehmen.</div></div></div>

    <div class="karte">
      <h2>So geht es</h2>
      <ol class="schritte">
        <li><span><b>Rechnung wie gewohnt in Lexware Office schreiben</b> und dort herunterladen (Rechnung öffnen → Herunterladen). Die PDF-Datei enthält die Rechnungsdaten.</span></li>
        <li><span><b>Dateien hier hineinziehen</b> – auch viele auf einmal. Das Programm zeigt, was dem Handel daran fehlt.</span></li>
        <li><span><b>Übernehmen.</b> Die Rechnungsnummer bleibt. GLN, Markt, Kostenstelle und GTIN ergänzt das Programm aus den Stammdaten, sobald Kunde und Artikel hier einmal gepflegt sind.</span></li>
        <li><span><b>Fertigstellen und senden</b> – als vollständige E-Rechnung an REWE oder EDEKA.</span></li>
      </ol>
    </div>

    <div class="karte">
      <div class=${'ablage' + (ueber ? ' ueber' : '')}
        onDragOver=${(e) => { e.preventDefault(); setUeber(true); }} onDragLeave=${() => setUeber(false)}
        onDrop=${(e) => { e.preventDefault(); setUeber(false); nimmDateien(e.dataTransfer.files); }}>
        <p><b>Rechnungsdateien hierher ziehen</b><br /><span class="leise">PDF (ZUGFeRD) oder XML (XRechnung) – eine oder viele</span></p>
        <label class="knopf haupt">Dateien auswählen …<input type="file" multiple accept=".pdf,.xml,application/pdf,text/xml,application/xml" hidden
          onChange=${(e) => { nimmDateien(e.target.files); e.target.value = ''; }} /></label>
        <p class="leise klein" style="margin:.9rem 0 0">Die Dateien bleiben auf diesem Rechner.</p>
      </div>
      <div class="raster" style="margin-top:.9rem">
        <label class="feld b6"><span>Prüfen nach den Vorgaben von</span>
          <${Auswahl} wert=${profil} setze=${(v) => { setProfil(v); pruefeAlleNeu(v); }}
            optionen=${[['auto', 'automatisch – je nach Kunde'], ...stamm.profile.map((p) => [p.id, p.name]), ['keine', 'nur allgemeine Regeln der E-Rechnung']]} /></label>
        <div class="feld b6" style="justify-content:flex-end"><${Haken} wert=${artikelAnlegen} setze=${setArtikelAnlegen}>Neue Artikel aus den Rechnungen in die Stammdaten übernehmen<//></div>
      </div>
    </div>

    <${LexwareAbruf} profil=${profil} nimm=${nimmAbruf} />

    ${ohneFirma && html`<${Kasten} art="warnung">
      <b>Die eigenen Firmendaten sind noch nicht eingetragen.</b> Die Rechnung „${ohneFirma.name}“ stammt von
      <b> ${ohneFirma.erg.rechnung.verkaeufer.name}</b>${ohneFirma.erg.rechnung.verkaeufer.ort ? ', ' + ohneFirma.erg.rechnung.verkaeufer.ort : ''}. Ist das die eigene Firma?
      <p style="margin:.6rem 0 0"><button class="haupt" onClick=${() => uebernimmFirma(ohneFirma)}>Ja – als eigene Firmendaten übernehmen</button></p><//>`}

    ${eintraege.length > 0 && html`<div class="karte">
      <div class="kopf" style="margin-bottom:.5rem">
        <h2 style="margin:0">Eingelesene Rechnungen</h2>
        <div class="knoepfe">
          <button onClick=${() => setEintraege([])}>Liste leeren</button>
          ${uebernommen > 0 && html`<button onClick=${() => gehe('#/')}>Zu den Entwürfen</button>`}
          ${anzahlBereit > 1 && html`<button class="haupt" disabled=${beschaeftigt} onClick=${uebernimmAlle}>Alle ${anzahlBereit} übernehmen</button>`}
        </div>
      </div>
      <table>
        <thead><tr><th>Datei</th><th>Nummer</th><th>Datum</th><th>Kunde</th><th class="zahl">Betrag</th><th>Befund</th><th></th></tr></thead>
        <tbody>${eintraege.map((e) => html`<${Zeile} key=${e.nr} e=${e} umschalten=${() => aendere(e.nr, { offen: !e.offen })}
          uebernimm=${() => uebernimm(e, eintraege.length === 1)} bereit=${bereit(e)} />`)}</tbody>
      </table>
    </div>`}`;
}

function Zeile({ e, umschalten, uebernimm, bereit }) {
  const erg = e.erg;
  if (e.laeuft || !erg) return html`<tr><td>${e.name}</td><td colspan="6" class="leise">wird geprüft …</td></tr>`;
  if (erg.fehler) {
    return html`<tr><td>${e.name}</td><td colspan="5"><span class="ampel fehler"></span> <b>Keine verwertbare E-Rechnung.</b> ${erg.fehler}</td><td></td></tr>`;
  }
  const z = zaehle(erg.meldungen);
  const r = erg.rechnung;
  const urteil = z.fehler ? `${z.fehler} Fehler – so wird die Rechnung voraussichtlich abgelehnt` : z.warnung ? 'Keine Fehler, aber Warnungen' : 'Keine Beanstandung';
  return html`
    <tr style="cursor:pointer" onClick=${umschalten}>
      <td style="overflow-wrap:anywhere">${e.name}</td>
      <td class="nowrap"><b>${r.nummer || '–'}</b></td>
      <td class="nowrap">${datumDE(r.datum)}</td>
      <td>${r.kaeufer.name || '–'}<br /><span class="leise klein">${erg.kunde ? 'bekannt – Stammdaten werden ergänzt' : 'neu – wird als Kunde angelegt'}</span></td>
      <td class="zahl">${r.summen ? geld(r.summen.brutto, r.waehrung) : ''}</td>
      <td class="nowrap"><span class=${'ampel ' + (z.fehler ? 'fehler' : z.warnung ? 'warnung' : 'gut')}></span>
        ${' '}${z.fehler ? `${z.fehler} Fehler` : z.warnung ? `${z.warnung} Warnung${z.warnung > 1 ? 'en' : ''}` : 'in Ordnung'}
        <br /><span class="leise klein">${e.offen ? 'Einzelheiten ausblenden' : 'Einzelheiten zeigen'}</span></td>
      <td class="rechts nowrap" onClick=${(ev) => ev.stopPropagation()}>
        ${e.entwurf ? html`<a class="knopf" href=${'#/rechnung/' + e.entwurf}>Entwurf öffnen</a>`
          : erg.stand === 'vorhanden' ? html`<a class="knopf" href=${'#/rechnung/' + erg.vorhandenId}>Schon fertiggestellt</a>`
          : bereit ? html`<button class="haupt" onClick=${uebernimm}>${erg.stand === 'entwurf' ? 'Erneut übernehmen' : 'Übernehmen'}</button>`
          : html`<span class="leise klein">${erg.firmaLeer ? 'erst Firmendaten eintragen' : 'nicht von der eigenen Firma'}</span>`}
      </td>
    </tr>
    ${e.offen && html`<tr><td colspan="7" style="background:#fafcfa">
      <div class="urteil" style="margin:.3rem 0 .2rem"><span class=${'ampel ' + (z.fehler ? 'fehler' : z.warnung ? 'warnung' : 'gut')}></span>${urteil}</div>
      <p class="leise klein">${erg.dateiart || ''}${erg.stand === 'entwurf' ? ' · wurde schon einmal übernommen; ein erneutes Übernehmen ersetzt den Entwurf' : ''}</p>
      ${erg.auffaellig.length > 0 && html`<${Kasten} art="warnung"><b>Auffälligkeiten an der Datei:</b><ul>${erg.auffaellig.map((a) => html`<li>${a}</li>`)}</ul><//>`}
      <div class="spalten">
        <div>
          ${erg.meldungen.length === 0 ? html`<${Kasten} art="gut">Die Rechnung erfüllt alle geprüften Regeln.<//>` : html`<${Meldungsliste} meldungen=${erg.meldungen} />`}
          ${erg.kunde && z.fehler > 0 && html`<p class="leise klein">Der Kunde ist in den Stammdaten bekannt. Was dort hinterlegt ist (GLN, Markt, Kostenstelle, GTIN der Artikel), wird beim Übernehmen ergänzt – die Liste zeigt, was der Datei allein fehlt.</p>`}
          ${!erg.uebernehmen && !erg.firmaLeer && html`<p class="leise klein">Übernehmen lässt sich nur eine Rechnung der eigenen Firma (Name, Steuernummer oder USt-IdNr. wie unter Einstellungen).</p>`}
        </div>
        <dl class="daten klein">
          <dt>Von</dt><dd>${r.verkaeufer.name || '–'}</dd>
          <dt>An</dt><dd>${r.kaeufer.name || '–'}</dd>
          <dt>GLN Empfänger</dt><dd>${r.kaeufer.gln || 'nicht enthalten'}</dd>
          <dt>GLN Markt</dt><dd>${(r.lieferort && r.lieferort.gln) || 'nicht enthalten'}</dd>
          <dt>Bestellnummer</dt><dd>${r.bestellnummer || 'nicht enthalten'}</dd>
          <dt>Lieferschein</dt><dd>${r.lieferscheinnummer || 'nicht enthalten'}</dd>
          <dt>Kostenstelle</dt><dd>${r.kostenstelle || 'nicht enthalten'}</dd>
          <dt>Lieferdatum</dt><dd>${datumDE(r.leistungsdatum) || 'nicht enthalten'}</dd>
          <dt>Positionen</dt><dd>${(r.positionen || []).length}, davon ${(r.positionen || []).filter((p) => p.gtin).length} mit GTIN</dd>
          <dt>Standard</dt><dd>${r.spezifikation || '–'}</dd>
        </dl>
      </div>
    </td></tr>`}`;
}
