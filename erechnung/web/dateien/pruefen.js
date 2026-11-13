// Fremde Rechnung prüfen: eine E-Rechnung aus einem anderen Programm einlesen
// und sagen, was daran fehlt.
import { html, useState, api, stamm, geld, datumDE, gehe, zeige, zeigeFehler, Auswahl, Kasten, Meldungsliste, zaehle } from './basis.js';

export function Pruefseite() {
  const [profil, setProfil] = useState('rewe');
  const [erg, setErg] = useState(null);
  const [datei, setDatei] = useState(null);
  const [ueber, setUeber] = useState(false);
  const [laeuft, setLaeuft] = useState(false);

  const pruefe = async (f, p = profil) => {
    if (!f) return;
    setDatei(f); setLaeuft(true); setErg(null);
    try {
      setErg(await api('POST', `/api/import?profil=${encodeURIComponent(p)}&name=${encodeURIComponent(f.name)}`, f));
    } catch (e) { zeigeFehler(e); } finally { setLaeuft(false); }
  };
  const uebernimm = async () => {
    try {
      const r = await api('POST', '/api/import/uebernehmen', erg.rechnung);
      zeige('Als Entwurf übernommen. Bitte die fehlenden Angaben ergänzen.', 'gut');
      gehe('#/rechnung/' + r.id);
    } catch (e) { zeigeFehler(e); }
  };
  const z = zaehle(erg && erg.meldungen);
  const r = erg && erg.rechnung;

  return html`
    <div class="kopf"><div><h1>Fremde Rechnung prüfen</h1>
      <div class="leise">Eine E-Rechnung aus Lexware Office (lexoffice) oder einem anderen Programm einlesen und sehen, warum sie abgelehnt wird.</div></div></div>

    <div class="karte">
      <div class="raster" style="margin-bottom:.9rem">
        <label class="feld b6"><span>Prüfen nach den Vorgaben von</span>
          <${Auswahl} wert=${profil} setze=${(v) => { setProfil(v); if (datei) pruefe(datei, v); }}
            optionen=${[...stamm.profile.map((p) => [p.id, p.name]), ['', 'nur allgemeine Regeln der E-Rechnung']]} /></label>
      </div>
      <div class=${'ablage' + (ueber ? ' ueber' : '')}
        onDragOver=${(e) => { e.preventDefault(); setUeber(true); }} onDragLeave=${() => setUeber(false)}
        onDrop=${(e) => { e.preventDefault(); setUeber(false); pruefe(e.dataTransfer.files[0]); }}>
        <p><b>Rechnungsdatei hierher ziehen</b><br /><span class="leise">PDF (ZUGFeRD) oder XML (XRechnung)</span></p>
        <label class="knopf haupt">Datei auswählen …<input type="file" accept=".pdf,.xml,application/pdf,text/xml,application/xml" hidden
          onChange=${(e) => { pruefe(e.target.files[0]); e.target.value = ''; }} /></label>
        <p class="leise klein" style="margin:.9rem 0 0">Die Datei bleibt auf diesem Rechner. In Lexware Office: Rechnung öffnen → Herunterladen.</p>
      </div>
    </div>

    ${laeuft && html`<p class="laden">Die Datei wird geprüft …</p>`}

    ${erg && html`<div class="karte">
      <h2>${datei ? datei.name : 'Ergebnis'}</h2>
      ${erg.fehler ? html`
        <div class="urteil"><span class="ampel fehler"></span>Keine verwertbare E-Rechnung</div>
        <${Kasten} art="fehler">${erg.fehler}<//>
      ` : html`
        <div class="urteil"><span class=${'ampel ' + (z.fehler ? 'fehler' : z.warnung ? 'warnung' : 'gut')}></span>
          ${z.fehler ? `${z.fehler} Fehler – so wird die Rechnung voraussichtlich abgelehnt` : z.warnung ? 'Keine Fehler, aber Warnungen' : 'Keine Beanstandung'}</div>
        <p class="leise">${erg.dateiart}</p>
        ${erg.auffaellig.length > 0 && html`<${Kasten} art="warnung"><b>Auffälligkeiten an der Datei:</b><ul>${erg.auffaellig.map((a) => html`<li>${a}</li>`)}</ul><//>`}
        <div class="spalten" style="margin-top:.8rem">
          <div>
            ${erg.meldungen.length === 0 ? html`<${Kasten} art="gut">Die Rechnung erfüllt alle geprüften Regeln.<//>` : html`<${Meldungsliste} meldungen=${erg.meldungen} />`}
            ${erg.uebernehmen && z.fehler + z.warnung > 0 && html`<${Kasten} art="hinweis">
              <b>Diese Rechnung stammt von der eigenen Firma.</b> Sie kann als Entwurf übernommen werden: Die Rechnungsnummer bleibt, die fehlenden Angaben (GLN, Kostenstelle, Markt …) werden ergänzt, und das Programm erzeugt daraus eine vollständige E-Rechnung zum erneuten Versand.
              <p style="margin:.6rem 0 0"><button class="haupt" onClick=${uebernimm}>Als Entwurf übernehmen und ergänzen</button></p><//>`}
            ${!erg.uebernehmen && html`<p class="leise klein">Übernehmen lässt sich nur eine Rechnung der eigenen Firma (Name, Steuernummer oder USt-IdNr. wie unter Einstellungen).</p>`}
          </div>
          <div>
            <dl class="daten klein">
              <dt>Nummer</dt><dd>${r.nummer || '–'}</dd>
              <dt>Datum</dt><dd>${datumDE(r.datum) || '–'}</dd>
              <dt>Von</dt><dd>${r.verkaeufer.name || '–'}</dd>
              <dt>An</dt><dd>${r.kaeufer.name || '–'}</dd>
              <dt>GLN Empfänger</dt><dd>${r.kaeufer.gln || 'nicht enthalten'}</dd>
              <dt>GLN Markt</dt><dd>${(r.lieferort && r.lieferort.gln) || 'nicht enthalten'}</dd>
              <dt>Bestellnummer</dt><dd>${r.bestellnummer || 'nicht enthalten'}</dd>
              <dt>Lieferschein</dt><dd>${r.lieferscheinnummer || 'nicht enthalten'}</dd>
              <dt>Kostenstelle</dt><dd>${r.kostenstelle || 'nicht enthalten'}</dd>
              <dt>Lieferdatum</dt><dd>${datumDE(r.leistungsdatum) || 'nicht enthalten'}</dd>
              <dt>Positionen</dt><dd>${(r.positionen || []).length}, davon ${(r.positionen || []).filter((p) => p.gtin).length} mit GTIN</dd>
              <dt>Betrag</dt><dd>${r.summen ? geld(r.summen.brutto, r.waehrung) : '–'}</dd>
              <dt>Standard</dt><dd>${r.spezifikation || '–'}</dd>
            </dl>
          </div>
        </div>`}
    </div>`}`;
}
