// Zusatzfunktionen rund um die Rechnung: offene Posten und Mahnungen,
// Serienrechnungen, Auswertung. Jede lässt sich in den Einstellungen
// einschalten.
import {
  html, useState, useEffect, api, geld, datumDE, heute, gehe, zeige, zeigeFehler,
  Feld, Text, Mehrzeilig, Auswahl, Zahl, Dialog, Kasten, Haken, PdfDialog, frage,
} from './basis.js';

const STUFE = (n) => (n < 0 ? '–' : n === 0 ? 'Zahlungserinnerung' : n === 1 ? 'Mahnung' : `${n}. Mahnung`);

// ------------------------------------------------------------- Mahnungen

// MahnDialog legt zu einer Rechnung die nächste Zahlungserinnerung oder
// Mahnung an: Vorschlag ansehen, ändern, als PDF erzeugen.
export function MahnDialog({ rechnung, zu, fertig }) {
  const [v, setV] = useState(null);
  const [laeuft, setLaeuft] = useState(false);
  useEffect(() => { api('GET', `/api/rechnungen/${rechnung.id}/mahnung`).then(setV).catch((e) => { zeigeFehler(e); zu(); }); }, []);
  if (!v) return null;
  const lege = async () => {
    setLaeuft(true);
    try {
      const r = await api('POST', `/api/rechnungen/${rechnung.id}/mahnung`, { stufe: Number(v.stufe), frist: v.frist, gebuehr: v.gebuehr || '0', text: v.text });
      zeige(STUFE(Number(v.stufe)) + ' erstellt.', 'gut');
      fertig(r);
    } catch (e) { zeigeFehler(e); } finally { setLaeuft(false); }
  };
  return html`<${Dialog} titel=${`${STUFE(Number(v.stufe))} zur Rechnung ${rechnung.nummer}`} zu=${zu} breit aktionen=${html`
      <button onClick=${zu}>Abbrechen</button><button class="haupt" onClick=${lege} disabled=${laeuft}>Schreiben erstellen</button>`}>
    ${(v.hinweise || []).map((h, i) => html`<${Kasten} key=${i} art="warnung">${h}<//>`)}
    <div class="raster">
      <${Feld} titel="Art" breite="4"><${Auswahl} wert=${String(v.stufe)} setze=${(x) => setV({ ...v, stufe: Number(x) })}
        optionen=${[0, 1, 2, 3].map((n) => [String(n), STUFE(n)])} /><//>
      <${Feld} titel="Zahlbar bis" breite="4"><${Text} typ="date" wert=${v.frist} setze=${(x) => setV({ ...v, frist: x })} /><//>
      <${Feld} titel="Mahngebühr in €" breite="4" hilfe="leer = keine"><${Zahl} wert=${v.gebuehr} min="2" max="2" leerBeiNull setze=${(x) => setV({ ...v, gebuehr: x })} /><//>
      <${Feld} titel="Text des Schreibens" breite="12" hilfe=${`Darunter stehen Rechnung, offener Betrag (${geld(v.betrag)}), Frist und Bankverbindung.`}>
        <${Mehrzeilig} wert=${v.text} setze=${(x) => setV({ ...v, text: x })} zeilen="7" /><//>
    </div>
  <//>`;
}

// Mahnliste zeigt die Schreiben zu einer Rechnung mit Ansehen und Versand.
export function Mahnliste({ rechnung, setRechnung, firma }) {
  const [pdf, setPdf] = useState(null);
  const [mail, setMail] = useState(null);
  const [laeuft, setLaeuft] = useState(false);
  const l = rechnung.mahnungen || [];
  if (!l.length) return null;
  const sende = async () => {
    setLaeuft(true);
    try {
      const r = await api('POST', `/api/rechnungen/${rechnung.id}/mahnung/${mail.nr}/senden`, mail);
      setRechnung(r); setMail(null); zeige('Verschickt.', 'gut');
    } catch (e) { zeigeFehler(e); } finally { setLaeuft(false); }
  };
  return html`<div class="karte">
    <h2>Zahlungserinnerungen und Mahnungen</h2>
    ${l.map((m, i) => html`<p class="klein" key=${i}><b>${STUFE(m.stufe)}</b> vom ${datumDE(m.datum)} · zahlbar bis ${datumDE(m.frist)}${Number(m.gebuehr) ? ` · Gebühr ${geld(m.gebuehr)}` : ''}
      <br /><span class="leise">${m.versendetAn ? `verschickt an ${m.versendetAn}` : 'noch nicht verschickt'}</span><br />
      <button class="schlicht" onClick=${() => setPdf(i)}>Ansehen</button>
      <button class="schlicht" onClick=${() => api('POST', `/api/rechnungen/${rechnung.id}/mahnung/${i}/oeffnen`).catch(zeigeFehler)}>Im PDF-Programm öffnen</button>
      <button class="schlicht" onClick=${() => setMail({ nr: i, an: '', betreff: `${STUFE(m.stufe)} zur Rechnung ${rechnung.nummer}`,
        text: `Guten Tag,\n\nim Anhang erhalten Sie unsere ${STUFE(m.stufe)} zur Rechnung ${rechnung.nummer}.\n\nMit freundlichen Grüßen\n${firma || ''}` })}>Per E-Mail senden …</button></p>`)}
    ${pdf !== null && html`<${PdfDialog} titel=${STUFE(l[pdf].stufe) + ' ' + rechnung.nummer} adresse=${`/api/rechnungen/${rechnung.id}/mahnung/${pdf}`} zu=${() => setPdf(null)}
      oeffne=${() => api('POST', `/api/rechnungen/${rechnung.id}/mahnung/${pdf}/oeffnen`).catch(zeigeFehler)} />`}
    ${mail && html`<${Dialog} titel="Schreiben per E-Mail senden" zu=${() => setMail(null)} aktionen=${html`
        <button onClick=${() => setMail(null)}>Abbrechen</button><button class="haupt" onClick=${sende} disabled=${laeuft}>Senden</button>`}>
      ${(rechnung.profil === 'rewe' || rechnung.profil === 'edeka') && html`<${Kasten} art="warnung">Nicht an das Rechnungspostfach schicken – dort werden Mahnungen nicht gelesen. Bitte die Adresse des Ansprechpartners eintragen.<//>`}
      <div class="raster">
        <${Feld} titel="An" breite="12"><${Text} typ="email" wert=${mail.an} setze=${(x) => setMail({ ...mail, an: x })} /><//>
        <${Feld} titel="Betreff" breite="12"><${Text} wert=${mail.betreff} setze=${(x) => setMail({ ...mail, betreff: x })} /><//>
        <${Feld} titel="Text" breite="12"><${Mehrzeilig} wert=${mail.text} setze=${(x) => setMail({ ...mail, text: x })} zeilen="6" /><//>
      </div>
    <//>`}
  </div>`;
}

// OffenePosten: alle fertigen, noch nicht bezahlten Rechnungen.
export function OffenePosten() {
  const [l, setL] = useState(null);
  const [mahnen, setMahnen] = useState(null);
  const lade = () => api('GET', '/api/offene-posten').then(setL).catch(zeigeFehler);
  useEffect(() => { lade(); }, []);
  if (!l) return html`<p class="laden">Offene Posten werden geladen …</p>`;
  const ueberfaellig = l.filter((p) => p.ueberfaellig);
  const summe = (x) => x.reduce((s, p) => s + Number(p.betrag), 0);
  const bezahlt = async (p) => {
    if (!await frage(`Rechnung ${p.nummer} über ${geld(p.betrag)} als bezahlt vermerken?`, { ja: 'Bezahlt', titel: 'Zahlung eingegangen' })) return;
    try { await api('POST', `/api/rechnungen/${p.id}/status`, { status: 'bezahlt' }); zeige('Als bezahlt vermerkt.', 'gut'); lade(); } catch (e) { zeigeFehler(e); }
  };
  return html`
    <div class="kopf"><div><h1>Offene Posten</h1>
      <div class="leise">${l.length} offen · ${geld(summe(l))} · davon ${ueberfaellig.length} überfällig (${geld(summe(ueberfaellig))})</div></div></div>
    <div class="karte">
      ${l.length === 0 ? html`<div class="leer">Alle Rechnungen sind bezahlt.</div>` : html`<table>
        <thead><tr><th>Rechnung</th><th>Kunde</th><th class="zahl">Betrag</th><th>Fällig</th><th>Stand</th><th></th></tr></thead>
        <tbody>${l.map((p) => html`<tr key=${p.id}>
          <td class="nowrap"><a href=${'#/rechnung/' + p.id}><b>${p.nummer}</b></a><br /><span class="leise klein">vom ${datumDE(p.datum)}</span></td>
          <td>${p.kunde}</td><td class="zahl">${geld(p.betrag)}</td>
          <td class="nowrap">${datumDE(p.faellig) || '–'}<br /><span class="klein" style=${p.ueberfaellig ? 'color:var(--rot)' : ''}>${p.ueberfaellig ? `seit ${p.tage} ${p.tage === 1 ? 'Tag' : 'Tagen'}` : p.faellig ? 'noch nicht fällig' : ''}</span></td>
          <td>${p.stufe < 0 ? html`<span class="leise">${p.versendet ? 'verschickt' : 'noch nicht verschickt'}</span>` : html`${STUFE(p.stufe)}<br /><span class="leise klein">vom ${datumDE(p.letzteVom)}, Frist ${datumDE(p.letzteBis)}</span>`}</td>
          <td class="rechts nowrap"><button class="schlicht" onClick=${() => bezahlt(p)}>Bezahlt</button>
            ${p.ueberfaellig ? html`<button onClick=${() => setMahnen(p)}>${p.stufe < 0 ? 'Erinnern …' : 'Mahnen …'}</button>` : null}</td></tr>`)}</tbody>
      </table>`}
    </div>
    ${mahnen && html`<${MahnDialog} rechnung=${mahnen} zu=${() => setMahnen(null)} fertig=${(r) => { setMahnen(null); gehe('#/rechnung/' + r.id); }} />`}`;
}

// ------------------------------------------------------- Serienrechnungen

const ABSTAND = [['woche', 'jede Woche'], ['monat', 'jeden Monat'], ['quartal', 'jedes Vierteljahr'], ['halbjahr', 'jedes halbe Jahr'], ['jahr', 'jedes Jahr']];

// SerieDialog legt aus einer Rechnung eine Serie an oder ändert Name,
// Abstand und Datum einer vorhandenen.
export function SerieDialog({ serie, ausRechnung, zu, fertig }) {
  const [s, setS] = useState(serie || { name: '', abstand: 'monat', naechste: heute(), aktiv: true });
  const speichere = async () => {
    try {
      const g = await api('POST', '/api/serien', { ...s, ausRechnung: ausRechnung || '' });
      zeige('Serienrechnung gespeichert.', 'gut'); fertig(g);
    } catch (e) { zeigeFehler(e); }
  };
  return html`<${Dialog} titel=${serie ? 'Serienrechnung ändern' : 'Als Serienrechnung speichern'} zu=${zu} aktionen=${html`
      <button onClick=${zu}>Abbrechen</button><button class="haupt" onClick=${speichere}>Speichern</button>`}>
    <p class="leise" style="margin-top:-.4rem">${serie ? 'Kunde und Positionen stammen aus der Rechnung, aus der die Serie entstand.'
      : 'Diese Rechnung wird zur Vorlage: Kunde, Markt, Positionen und Texte. Zum genannten Datum schlägt das Programm die nächste Rechnung als Entwurf vor – verschickt wird nichts von allein.'}</p>
    <div class="raster">
      <${Feld} titel="Name der Serie" breite="12"><${Text} wert=${s.name} setze=${(x) => setS({ ...s, name: x })} placeholder="z. B. Monatslieferung Markt Korbach" /><//>
      <${Feld} titel="Wiederholung" breite="6"><${Auswahl} wert=${s.abstand} setze=${(x) => setS({ ...s, abstand: x })} optionen=${ABSTAND} /><//>
      <${Feld} titel="Nächste Rechnung am" breite="6"><${Text} typ="date" wert=${s.naechste} setze=${(x) => setS({ ...s, naechste: x })} /><//>
    </div>
    <${Haken} wert=${s.aktiv} setze=${(x) => setS({ ...s, aktiv: x })}>Serie läuft (ohne Haken ruht sie)<//>
  <//>`;
}

export function Serienseite() {
  const [l, setL] = useState(null);
  const [offen, setOffen] = useState(null);
  const lade = () => api('GET', '/api/serien').then(setL).catch(zeigeFehler);
  useEffect(() => { lade(); }, []);
  if (!l) return html`<p class="laden">Serienrechnungen werden geladen …</p>`;
  const faellig = l.filter((s) => s.faellig);
  const lege = async (id) => {
    try {
      const neu = await api('POST', '/api/serien/anlegen', id ? { id } : {});
      zeige(neu.length === 1 ? 'Ein Entwurf angelegt.' : `${neu.length} Entwürfe angelegt.`, 'gut');
      if (neu.length === 1) gehe('#/rechnung/' + neu[0].id); else lade();
    } catch (e) { zeigeFehler(e); }
  };
  const loesche = async (s) => {
    if (!await frage(`Serie „${s.name}“ löschen?\n\nSchon angelegte Rechnungen bleiben erhalten.`, { ja: 'Löschen', gefahr: true, titel: 'Serie löschen' })) return;
    try { await api('DELETE', '/api/serien/' + s.id); lade(); } catch (e) { zeigeFehler(e); }
  };
  return html`
    <div class="kopf">
      <div><h1>Serienrechnungen</h1><div class="leise">Rechnungen, die sich regelmäßig wiederholen – das Programm legt zum Termin den Entwurf an</div></div>
      ${faellig.length > 0 && html`<div class="knoepfe"><button class="haupt" onClick=${() => lege('')}>${faellig.length === 1 ? 'Fälligen Entwurf anlegen' : `${faellig.length} fällige Entwürfe anlegen`}</button></div>`}
    </div>
    <div class="karte">
      ${l.length === 0 ? html`<div class="leer">Noch keine Serie angelegt.<br /><span class="klein">Eine fertige Rechnung öffnen und dort „Als Serienrechnung speichern“ wählen.</span></div>` : html`<table>
        <thead><tr><th>Serie</th><th>Kunde</th><th class="zahl">Betrag</th><th>Wiederholung</th><th>Nächste Rechnung</th><th></th></tr></thead>
        <tbody>${l.map((s) => html`<tr key=${s.id}>
          <td><b>${s.name}</b>${!s.aktiv ? html`<br /><span class="leise klein">ruht</span>` : null}</td><td>${s.kunde}</td><td class="zahl">${geld(s.brutto)}</td>
          <td>${(ABSTAND.find(([w]) => w === s.abstand) || ['', s.abstand])[1]}</td>
          <td class="nowrap">${datumDE(s.naechste)}${s.faellig ? html`<br /><span class="klein" style="color:var(--rot)">fällig</span>` : null}
            ${s.letzte ? html`<br /><span class="leise klein">zuletzt ${datumDE(s.letzte)}</span>` : null}</td>
          <td class="rechts nowrap"><button class="schlicht" onClick=${() => setOffen(s)}>Ändern</button>
            <button class="schlicht" onClick=${() => lege(s.id)}>Jetzt anlegen</button>
            <button class="schlicht" onClick=${() => loesche(s)}>Löschen</button></td></tr>`)}</tbody>
      </table>`}
    </div>
    ${offen && html`<${SerieDialog} serie=${offen} zu=${() => setOffen(null)} fertig=${() => { setOffen(null); lade(); }} />`}`;
}

// ------------------------------------------------------------ Auswertung

export function Auswertungsseite() {
  const [jahr, setJahr] = useState(String(new Date().getFullYear()));
  const [a, setA] = useState(null);
  useEffect(() => { setA(null); api('GET', '/api/auswertung?jahr=' + jahr).then(setA).catch(zeigeFehler); }, [jahr]);
  if (!a) return html`<p class="laden">Auswertung wird berechnet …</p>`;
  const monat = ['Januar', 'Februar', 'März', 'April', 'Mai', 'Juni', 'Juli', 'August', 'September', 'Oktober', 'November', 'Dezember'];
  const zeile = (titel, m, fett) => html`<tr style=${fett ? 'font-weight:650' : ''}>
    <td>${titel}</td><td class="zahl">${m.anzahl || ''}</td>
    <td class="zahl">${geld(m.netto19)}</td><td class="zahl">${geld(m.steuer19)}</td>
    <td class="zahl">${geld(m.netto7)}</td><td class="zahl">${geld(m.steuer7)}</td>
    <td class="zahl">${Number(m.nettoOhne) ? geld(m.nettoOhne) : ''}</td><td class="zahl">${geld(m.brutto)}</td></tr>`;
  const quartal = (q) => a.monate.slice(q * 3, q * 3 + 3).reduce((s, m) => {
    for (const f of ['anzahl', 'netto19', 'steuer19', 'netto7', 'steuer7', 'nettoOhne', 'brutto']) s[f] = (Number(s[f]) || 0) + Number(m[f]);
    return s;
  }, {});
  const ueberfaellig = a.offen.filter((p) => p.ueberfaellig);
  return html`
    <div class="kopf">
      <div><h1>Auswertung</h1><div class="leise">Übersicht über die geschriebenen Rechnungen – keine Buchführung</div></div>
      <div class="knoepfe"><${Auswahl} wert=${jahr} setze=${setJahr} optionen=${a.jahre.map((j) => [j, 'Jahr ' + j])} /></div>
    </div>
    <div class="karte">
      <h2>Umsätze ${a.jahr} nach Steuersatz</h2>
      <table>
        <thead><tr><th>Zeitraum</th><th class="zahl">Rechn.</th><th class="zahl">Netto 19 %</th><th class="zahl">USt 19 %</th><th class="zahl">Netto 7 %</th><th class="zahl">USt 7 %</th><th class="zahl">ohne USt</th><th class="zahl">Brutto</th></tr></thead>
        <tbody>
          ${[0, 1, 2, 3].map((q) => html`
            ${a.monate.slice(q * 3, q * 3 + 3).map((m, i) => zeile(monat[q * 3 + i], m, false))}
            ${zeile(`${q + 1}. Vierteljahr`, quartal(q), true)}`)}
          ${zeile('Jahr ' + a.jahr, a.summe, true)}
        </tbody>
      </table>
      <p class="leise klein" style="margin:.7rem 0 0">Gezählt wird nach Rechnungsdatum; Rechnungskorrekturen sind abgezogen. Die Zahlen helfen beim Abgleich mit der Buchhaltung und der Umsatzsteuer-Voranmeldung,
        ersetzen sie aber nicht: Ausgaben, Zahlungen und die Steuer nach vereinnahmten Entgelten kennt das Programm nicht.</p>
    </div>
    <div class="spalten">
      <div class="karte">
        <h2>Umsatz je Kunde</h2>
        ${a.kunden.length === 0 ? html`<div class="leer">Keine Rechnungen in diesem Jahr.</div>` : html`<table>
          <thead><tr><th>Kunde</th><th class="zahl">Rechn.</th><th class="zahl">Netto</th><th class="zahl">Brutto</th></tr></thead>
          <tbody>${a.kunden.map((k) => html`<tr key=${k.kunde}><td>${k.kunde}</td><td class="zahl">${k.anzahl}</td><td class="zahl">${geld(k.netto)}</td><td class="zahl">${geld(k.brutto)}</td></tr>`)}</tbody>
        </table>`}
      </div>
      <div class="karte">
        <h2>Offene Rechnungen</h2>
        <p><b>${a.offen.length}</b> offen über <b>${geld(a.offen.reduce((s, p) => s + Number(p.betrag), 0))}</b><br />
          davon <b style=${ueberfaellig.length ? 'color:var(--rot)' : ''}>${ueberfaellig.length}</b> überfällig über <b>${geld(ueberfaellig.reduce((s, p) => s + Number(p.betrag), 0))}</b></p>
        <a class="knopf" href="#/offen">Offene Posten ansehen</a>
      </div>
    </div>`;
}
