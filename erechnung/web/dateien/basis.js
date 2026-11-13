// Gemeinsame Bausteine der Oberfläche: Zugriff auf das Programm, Formatierung
// von Zahlen und Daten, Eingabefelder, Dialoge und Meldungen.
import { html, render, useState, useEffect, useRef, useMemo, useCallback } from './fremd/preact-htm.js';
export { html, render, useState, useEffect, useRef, useMemo, useCallback };

// ---------------------------------------------------------------- Programm

// api ruft das Programm auf. Fehler kommen als Error mit deutschem Text.
export async function api(methode, pfad, daten, optionen = {}) {
  const kopf = { 'X-Marktrechnung': '1' };
  let koerper;
  if (daten instanceof Blob || daten instanceof ArrayBuffer) {
    koerper = daten;
  } else if (daten !== undefined && daten !== null) {
    koerper = JSON.stringify(daten);
    kopf['Content-Type'] = 'application/json';
  }
  let antwort;
  try {
    antwort = await fetch(pfad, { method: methode, headers: kopf, body: koerper, credentials: 'same-origin' });
  } catch (e) {
    throw new Error('Das Programm Marktrechnung antwortet nicht. Läuft es noch? Bitte über sein Symbol neu starten.');
  }
  if (optionen.blob && antwort.ok) return antwort.blob();
  let inhalt = null;
  try { inhalt = await antwort.json(); } catch (e) { /* keine JSON-Antwort */ }
  if (!antwort.ok) {
    const f = new Error((inhalt && inhalt.fehler) || `Fehler ${antwort.status}`);
    f.status = antwort.status;
    f.meldungen = (inhalt && inhalt.meldungen) || [];
    throw f;
  }
  return inhalt;
}

// Stammdaten des Programms (Einheiten, Profile, Formate), einmal geladen.
export const stamm = { geladen: false };
export async function ladeStamm() {
  Object.assign(stamm, await api('GET', '/api/stamm'), { geladen: true });
  return stamm;
}

// ------------------------------------------------------------ Formatierung

// zahlDE zeigt eine Zahl mit Punkt ("1234.5") in deutscher Schreibweise.
export function zahlDE(wert, min = 0, max = 6) {
  if (wert === undefined || wert === null || wert === '') return '';
  const n = Number(wert);
  if (!isFinite(n)) return String(wert);
  return n.toLocaleString('de-DE', { minimumFractionDigits: min, maximumFractionDigits: max });
}

export function geld(wert, waehrung = 'EUR') {
  if (wert === undefined || wert === null || wert === '') return '';
  return zahlDE(wert, 2, 2) + ' ' + (waehrung === 'EUR' || !waehrung ? '€' : waehrung);
}

// parseZahl liest eine deutsche Eingabe ("1.234,56" oder "12,5" oder "12.5").
// Ergebnis ist die Schreibweise mit Punkt oder null, wenn es keine Zahl ist.
export function parseZahl(text) {
  let s = String(text).trim().replace(/\s|€|%/g, '');
  if (s === '') return '';
  if (s.includes(',')) s = s.replace(/\./g, '').replace(',', '.');
  return /^[-+]?(\d+\.?\d*|\.\d+)$/.test(s) ? s.replace(/^\+/, '') : null;
}

export function datumDE(iso) {
  if (!iso) return '';
  const m = /^(\d{4})-(\d{2})-(\d{2})/.exec(iso);
  return m ? `${m[3]}.${m[2]}.${m[1]}` : iso;
}

export function heute() {
  const d = new Date();
  const z = (n) => String(n).padStart(2, '0');
  return `${d.getFullYear()}-${z(d.getMonth() + 1)}-${z(d.getDate())}`;
}

// Prüfziffer von GLN (13 Ziffern) und GTIN (8, 12, 13 oder 14 Ziffern).
export function pruefzifferOk(s) {
  if (!/^\d+$/.test(s)) return false;
  let summe = 0;
  for (let i = 0; i < s.length; i++) {
    const z = Number(s[s.length - 1 - i]);
    summe += i % 2 === 1 ? z * 3 : z;
  }
  return summe % 10 === 0;
}
export const glnOk = (s) => s.length === 13 && pruefzifferOk(s);
export const gtinOk = (s) => [8, 12, 13, 14].includes(s.length) && pruefzifferOk(s);

export const STATUS = { entwurf: 'Entwurf', fertig: 'Fertig, noch nicht versendet', versendet: 'Versendet', bezahlt: 'Bezahlt', storniert: 'Storniert' };
export const STATUS_KURZ = { entwurf: 'Entwurf', fertig: 'Nicht versendet', versendet: 'Versendet', bezahlt: 'Bezahlt', storniert: 'Storniert' };

export function belegName(art) {
  return art === '381' ? 'Rechnungskorrektur' : art === '384' ? 'Korrigierte Rechnung' : art === '326' ? 'Abschlagsrechnung' : 'Rechnung';
}

// ------------------------------------------------------------- Navigation

export function gehe(ziel) { location.hash = ziel; }

// ------------------------------------------------------------- Meldungen

let toastSetzer = null;
let toastNr = 0;
export function zeige(text, art = '') {
  if (!toastSetzer) return;
  const nr = ++toastNr;
  toastSetzer((l) => [...l, { nr, text, art }]);
  setTimeout(() => toastSetzer((l) => l.filter((t) => t.nr !== nr)), art === 'fehler' ? 9000 : 3500);
}
export const zeigeFehler = (e) => zeige(e && e.message ? e.message : String(e), 'fehler');

export function Toasts() {
  const [liste, setze] = useState([]);
  toastSetzer = setze;
  return html`<div class="toasts" role="status">${liste.map((t) => html`<div key=${t.nr} class=${'toast ' + t.art}>${t.text}</div>`)}</div>`;
}

// -------------------------------------------------------------- Bausteine

export function Feld({ titel, hilfe, falsch, breite, children }) {
  return html`<label class=${'feld ' + (breite ? 'b' + breite : '')}>
    <span>${titel}</span>${children}
    ${falsch ? html`<small class="falsch">${falsch}</small>` : hilfe ? html`<small>${hilfe}</small>` : null}
  </label>`;
}

export function Text({ wert, setze, feld, typ = 'text', ...rest }) {
  return html`<input type=${typ} data-feld=${feld} value=${wert || ''} onInput=${(e) => setze(e.target.value)} ...${rest} />`;
}

export function Mehrzeilig({ wert, setze, feld, zeilen = 3, ...rest }) {
  return html`<textarea data-feld=${feld} rows=${zeilen} value=${wert || ''} onInput=${(e) => setze(e.target.value)} ...${rest}></textarea>`;
}

export function Auswahl({ wert, setze, feld, optionen, ...rest }) {
  return html`<select data-feld=${feld} value=${wert ?? ''} onChange=${(e) => setze(e.target.value)} ...${rest}>
    ${optionen.map(([w, name]) => html`<option value=${w} selected=${String(wert ?? '') === String(w)}>${name}</option>`)}
  </select>`;
}

// Zahl ist ein Eingabefeld für Dezimalzahlen in deutscher Schreibweise. Nach
// außen gibt es die Schreibweise mit Punkt weiter.
export function Zahl({ wert, setze, feld, min = 0, max = 4, ...rest }) {
  const [text, setText] = useState(null); // null = nicht im Feld
  const anzeige = text !== null ? text : (wert === '' || wert === undefined || wert === null || Number(wert) === 0 && rest.leerBeiNull ? '' : zahlDE(wert, min, max));
  const { leerBeiNull, ...attribute } = rest;
  return html`<input class="zahl" inputmode="decimal" data-feld=${feld} value=${anzeige}
    onFocus=${(e) => { setText(anzeige); setTimeout(() => e.target.select(), 0); }}
    onInput=${(e) => { setText(e.target.value); const z = parseZahl(e.target.value); if (z !== null) setze(z === '' ? '0' : z); }}
    onBlur=${() => setText(null)} ...${attribute} />`;
}

export function Haken({ wert, setze, children }) {
  return html`<label class="haken"><input type="checkbox" checked=${!!wert} onChange=${(e) => setze(e.target.checked)} /><span>${children}</span></label>`;
}

export function Dialog({ titel, zu, breit, aktionen, children }) {
  useEffect(() => {
    const taste = (e) => { if (e.key === 'Escape' && zu) zu(); };
    document.addEventListener('keydown', taste);
    return () => document.removeEventListener('keydown', taste);
  }, []);
  return html`<div class="schleier" onMouseDown=${(e) => { if (e.target === e.currentTarget && zu) zu(); }}>
    <div class=${'dialog' + (breit ? ' breit' : '')} role="dialog" aria-label=${titel}>
      <h2>${titel}</h2>
      ${children}
      ${aktionen ? html`<div class="aktionen">${aktionen}</div>` : null}
    </div>
  </div>`;
}

export function Kasten({ art = 'hinweis', children }) {
  return html`<div class=${'kasten ' + art}>${children}</div>`;
}

export function Etikett({ status }) {
  return html`<span class=${'etikett ' + status}>${STATUS_KURZ[status] || status}</span>`;
}

// frage stellt eine Rückfrage in einem Fenster des Programms und liefert,
// ob zugestimmt wurde. Leerzeilen im Text trennen Absätze.
let frageSetzer = null;
export function frage(text, { ja = 'Ja', nein = 'Abbrechen', gefahr = false, titel = 'Bitte bestätigen' } = {}) {
  return new Promise((antwort) => {
    if (!frageSetzer) { antwort(window.confirm(text)); return; }
    frageSetzer({ text, ja, nein, gefahr, titel, antwort });
  });
}
export function Fragen() {
  const [f, setF] = useState(null);
  frageSetzer = setF;
  if (!f) return null;
  const schliesse = (wert) => { setF(null); f.antwort(wert); };
  return html`<${Dialog} titel=${f.titel} zu=${() => schliesse(false)} aktionen=${html`
      <button onClick=${() => schliesse(false)}>${f.nein}</button>
      <button class=${f.gefahr ? 'gefahr' : 'haupt'} data-frage="ja" onClick=${() => schliesse(true)}>${f.ja}</button>`}>
    ${f.text.split('\n\n').map((absatz, i) => html`<p key=${i}>${absatz}</p>`)}
  <//>`;
}

// PdfDialog zeigt ein PDF in einem Fenster über der Seite. Mit "oeffne"
// lässt es sich zusätzlich im PDF-Programm des Rechners öffnen.
export function PdfDialog({ titel, adresse, zu, oeffne }) {
  return html`<${Dialog} titel=${titel} zu=${zu} breit aktionen=${html`
      ${oeffne ? html`<button onClick=${oeffne}>Im PDF-Programm öffnen</button>` : null}
      <button class="haupt" onClick=${zu}>Schließen</button>`}>
    <iframe src=${adresse} title=${titel}></iframe>
  <//>`;
}

// Meldungsliste zeigt das Ergebnis einer Prüfung. klick(meldung) springt zum Feld.
export function Meldungsliste({ meldungen, klick }) {
  const titel = { fehler: 'Fehler', warnung: 'Warnung', hinweis: 'Hinweis' };
  return html`<div>${meldungen.map((m, i) => html`
    <button key=${i} class=${'meldung ' + m.schwere} onClick=${() => klick && klick(m)} title=${klick ? 'Zum Feld springen' : ''}>
      <b>${titel[m.schwere]} · ${m.regel}</b>${m.text}
    </button>`)}</div>`;
}

export function zaehle(meldungen) {
  const z = { fehler: 0, warnung: 0, hinweis: 0 };
  for (const m of meldungen || []) z[m.schwere]++;
  return z;
}

// springeZu setzt den Cursor in das Eingabefeld mit dem Pfad und hebt es hervor.
export function springeZu(feld) {
  const el = document.querySelector(`[data-feld="${feld}"]`);
  if (!el) return false;
  el.scrollIntoView({ behavior: 'smooth', block: 'center' });
  el.classList.remove('blink');
  void el.offsetWidth;
  el.classList.add('blink');
  setTimeout(() => el.focus({ preventScroll: true }), 250);
  return true;
}

// setzeIn ändert einen Wert tief in einem Objekt und gibt eine Kopie zurück.
export function setzeIn(objekt, pfad, wert) {
  const teile = Array.isArray(pfad) ? pfad : String(pfad).split('.');
  const kopie = Array.isArray(objekt) ? [...objekt] : { ...(objekt || {}) };
  if (teile.length === 1) { kopie[teile[0]] = wert; return kopie; }
  kopie[teile[0]] = setzeIn(kopie[teile[0]], teile.slice(1), wert);
  return kopie;
}

export function ladeDatei(adresse) {
  const a = document.createElement('a');
  a.href = adresse;
  a.download = '';
  document.body.appendChild(a);
  a.click();
  a.remove();
}
