// Der Assistent: Er zeigt zur geöffneten Seite, was dort zu tun ist, führt
// durch die Einrichtung und sucht Antworten in der eingebauten Anleitung -
// ohne Internet. Ist unter Einstellungen ein Zugangsschlüssel hinterlegt,
// beantwortet zusätzlich eine KI frei gestellte Fragen.
import { html, useState, useEffect, useRef, api, gehe, zeige, zeigeFehler, Kasten, frage, stamm } from './basis.js';

// ---------------------------------------------------------------- Anleitung

let anleitung = null;
export async function ladeAnleitung() {
  if (!anleitung) {
    const antwort = await fetch('anleitung.json', { credentials: 'same-origin' });
    if (!antwort.ok) throw new Error('Die Anleitung lässt sich nicht laden.');
    anleitung = await antwort.json();
  }
  return anleitung;
}

// useAnleitung liefert die Anleitung, sobald sie geladen ist.
export function useAnleitung() {
  const [a, setA] = useState(anleitung);
  useEffect(() => { if (!a) ladeAnleitung().then(setA).catch(zeigeFehler); }, []);
  return a;
}

// -------------------------------------------------------------------- Suche

// einfach macht Text vergleichbar: klein, Umlaute ausgeschrieben, ohne Satzzeichen.
export function einfach(text) {
  return String(text || '').toLowerCase()
    .replace(/ä/g, 'ae').replace(/ö/g, 'oe').replace(/ü/g, 'ue').replace(/ß/g, 'ss')
    .replace(/[^a-z0-9]+/g, ' ').trim();
}

const FUELLWORTE = new Set(('wie was wo wer wann warum wieso weshalb kann koennen man ich mir mich meine meinen meinem mein eine einen einem einer eines ein '
  + 'der die das den dem des und oder aber auch nicht ist sind war wird werden hat habe haben muss soll sollte will moechte bitte fuer mit von zu zum zur im '
  + 'in an am auf aus bei nach ueber unter es sie er wir ihr du dann denn da dass ob noch schon mal nur sehr mehr mache machen macht tun tue gibt geben '
  + 'bedeutet bedeuten heisst heissen genau eigentlich jetzt hier dort dieser diese dieses welche welcher welches etwas alles').split(' '));

// passt: gleiches Wort oder dasselbe Wort in anderer Form ("rechnungen"
// findet "rechnung", "sichere" findet "sichern") - aber nicht ein längeres
// Wort, das nur gleich anfängt ("rechnung" und "rechnungsempfänger").
function passt(a, b) {
  if (a === b) return true;
  const [kurz, lang] = a.length <= b.length ? [a, b] : [b, a];
  if (kurz.length < 4 || lang.length - kurz.length > 3) return false;
  if (lang.startsWith(kurz)) return true;
  if (kurz.length < 6) return false;
  let gleich = 0;
  while (gleich < kurz.length && kurz[gleich] === lang[gleich]) gleich++;
  return gleich >= Math.max(5, kurz.length - 2);
}

// suche findet zu einer Frage die passenden Kapitel, Begriffe und Meldungen.
export function suche(a, text) {
  const satz = ' ' + einfach(text) + ' ';
  const worte = [...new Set(satz.trim().split(' ').filter((w) => w.length > 1 && !FUELLWORTE.has(w)))];
  const leer = { kapitel: [], begriffe: [], meldungen: [] };
  if (!a || worte.length === 0) return leer;

  // Wie gut passt ein Wort zu einem Kapitel? Stichwort 6, Titel 4, Text 1.
  const staerke = a.kapitel.map((k) => {
    const stichworte = (k.stichworte || []).map(einfach).filter((e) => !e.includes(' '));
    const titel = einfach(k.titel).split(' ');
    const rest = ' ' + einfach([k.kurz, ...(k.schritte || []), ...(k.hinweise || [])].join(' ')) + ' ';
    return worte.map((w) => (stichworte.includes(w) ? 6 : stichworte.some((e) => passt(w, e)) ? 4 : titel.some((t) => passt(w, t)) ? 4 : w.length >= 4 && rest.includes(' ' + w) ? 1 : 0));
  });
  // Ein Wort, das fast überall vorkommt ("Rechnung"), sagt wenig; ein
  // seltenes ("Sicherung") entscheidet.
  const gewicht = worte.map((_, i) => {
    const wieOft = staerke.filter((z) => z[i] >= 4).length;
    return wieOft === 0 ? 1 : 1 + Math.log(a.kapitel.length / wieOft);
  });
  const kapitel = a.kapitel.map((k, nr) => {
    let punkte = staerke[nr].reduce((summe, wert, i) => summe + wert * gewicht[i], 0);
    // Stichworte aus mehreren Wörtern ("offene posten") zählen als Ganzes.
    for (const st of k.stichworte || []) { const e = einfach(st); if (e.includes(' ') && satz.includes(' ' + e + ' ')) punkte += 14; }
    return { k, punkte };
  }).filter((t) => t.punkte >= 4).sort((x, y) => y.punkte - x.punkte);
  // Nur was an den besten Treffer heranreicht - sonst wird die Liste beliebig.
  const grenze = kapitel.length ? Math.max(4, kapitel[0].punkte * 0.45) : 0;

  const begriffe = (a.begriffe || []).filter((b) =>
    [b.wort, ...(b.auch || [])].some((n) => { const e = einfach(n); return e.includes(' ') ? satz.includes(' ' + e + ' ') : worte.includes(e); }));
  // "BR-DE-15" in der Frage steht nach dem Vereinfachen als "br de 15" da.
  const meldungen = (a.meldungen || []).filter((m) =>
    String(m.regel).split(/,|\/| bis /).map((r) => einfach(r)).filter((r) => r.length > 3).some((r) => satz.includes(' ' + r + ' ')));
  return { kapitel: kapitel.filter((t) => t.punkte >= grenze).slice(0, 3).map((t) => t.k), begriffe: begriffe.slice(0, 3), meldungen: meldungen.slice(0, 3) };
}

// kapitelZurSeite: die Kapitel, die zur geöffneten Seite gehören.
function kapitelZurSeite(a, seite) {
  return a ? a.kapitel.filter((k) => (k.seiten || []).includes(seite)) : [];
}

// ---------------------------------------------------------------- Bausteine

// fuehrtWeg: Liegt das Ziel des Kapitels auf einer anderen als der geöffneten Seite?
const fuehrtWeg = (k, seite) => !!k.ziel && k.ziel.replace(/^#\//, '').split('/')[0] !== seite;

// KapitelKarte zeigt ein Kapitel der Anleitung zum Auf- und Zuklappen.
function KapitelKarte({ k, nummer, offen: zuBeginnOffen, seite }) {
  const [offen, setOffen] = useState(!!zuBeginnOffen);
  return html`<div class=${'hilfskarte' + (offen ? ' offen' : '')} data-kapitel=${k.id}>
    <button class="titel" aria-expanded=${offen} onClick=${() => setOffen(!offen)}>
      <span class="pfeil">${offen ? '▾' : '▸'}</span>
      <span><b>${nummer ? nummer + '. ' : ''}${k.titel}</b><br /><span class="leise">${k.kurz}</span></span>
    </button>
    ${offen && html`<div class="inhalt">
      <ol>${k.schritte.map((s, i) => html`<li key=${i}>${s}</li>`)}</ol>
      ${(k.hinweise || []).map((h, i) => html`<p class="tipp" key=${i}><b>Gut zu wissen:</b> ${h}</p>`)}
      <div class="knoepfe">
        ${fuehrtWeg(k, seite) ? html`<button class="haupt" onClick=${() => gehe(k.ziel)}>Dorthin gehen</button>` : null}
        <button class="schlicht" onClick=${() => gehe('#/anleitung?k=' + k.id)}>In der Anleitung zeigen</button>
      </div>
    </div>`}
  </div>`;
}

// Antworttext zeigt eine Antwort der KI: gewöhnlicher Text, in dem nur
// Verweise der Form [[#/seite|Beschriftung]] zu Knöpfen werden.
function Antworttext({ text }) {
  const teile = [];
  const muster = /\[\[(#\/[A-Za-z0-9/_-]*)\|([^\]\n]{1,80})\]\]/g;
  const sauber = String(text).replace(/\*\*/g, '');
  let ab = 0;
  let m;
  while ((m = muster.exec(sauber))) {
    if (m.index > ab) teile.push(sauber.slice(ab, m.index));
    teile.push({ ziel: m[1], name: m[2] });
    ab = muster.lastIndex;
  }
  if (ab < sauber.length) teile.push(sauber.slice(ab));
  return html`<div class="antworttext">${teile.map((t, i) => typeof t === 'string' ? t
    : html`<a key=${i} class="knopf verweis" href=${t.ziel}>${t.name} ›</a>`)}</div>`;
}

function Treffer({ t, seite }) {
  const nichts = !t.kapitel.length && !t.begriffe.length && !t.meldungen.length;
  if (nichts) return null;
  return html`
    ${t.begriffe.map((b) => html`<div class="hilfskarte erklaerung" key=${b.wort}><b>${b.wort}</b><br />${b.text}</div>`)}
    ${t.meldungen.map((m) => html`<div class="hilfskarte erklaerung" key=${m.regel}><b>Meldung ${m.regel}</b><br />${m.text}</div>`)}
    ${t.kapitel.map((k, i) => html`<${KapitelKarte} key=${k.id} k=${k} offen=${i === 0 && !t.zu} seite=${seite} />`)}`;
}

// ------------------------------------------------------------- Der Assistent

const BEISPIELE = ['Wie schreibe ich eine Rechnung?', 'Was ist eine GLN?', 'Die Rechnung kam zurück', 'Wie sichere ich meine Daten?'];

// Assistent ist die Leiste am rechten Rand. Sie bleibt im Hintergrund
// bestehen, damit das Gespräch beim Zuklappen nicht verloren geht.
export function Assistent({ offen, zu, seite }) {
  const a = useAnleitung();
  const [stand, setStand] = useState({ ki: false, einrichtung: [] });
  const [text, setText] = useState('');
  const [verlauf, setVerlauf] = useState([]); // [{frage, treffer, antwort, fehler, laeuft}]
  const [laeuft, setLaeuft] = useState(false);
  const [alleSchritte, setAlleSchritte] = useState(false);
  const eingabe = useRef(null);
  const ende = useRef(null);

  // Der Stand wird neu geholt, wenn die Leiste aufgeht, die Seite wechselt
  // oder Einstellungen gespeichert wurden.
  const ladeStand = () => api('GET', '/api/assistent').then(setStand).catch(() => {});
  useEffect(() => {
    if (!offen) return undefined;
    ladeStand();
    window.addEventListener('assistentneu', ladeStand);
    window.addEventListener('stammneu', ladeStand);
    return () => { window.removeEventListener('assistentneu', ladeStand); window.removeEventListener('stammneu', ladeStand); };
  }, [offen, seite]);
  useEffect(() => { if (offen) setTimeout(() => eingabe.current && eingabe.current.focus(), 60); }, [offen]);
  useEffect(() => { if (offen && verlauf.length && ende.current) ende.current.scrollIntoView({ behavior: 'smooth', block: 'end' }); }, [verlauf, offen]);

  const stelle = async (frageText) => {
    const f = String(frageText || '').trim();
    if (!f || laeuft) return;
    setText('');
    const treffer = suche(a, f);
    if (!stand.ki) {
      setVerlauf((v) => [...v, { frage: f, treffer }]);
      return;
    }
    // Mit KI: die Antwort kommt von dort, die Treffer aus der Anleitung stehen zugeklappt darunter.
    const bisher = verlauf.filter((e) => e.antwort).flatMap((e) => [{ rolle: 'frage', text: e.frage }, { rolle: 'antwort', text: e.antwort }]);
    setVerlauf((v) => [...v, { frage: f, treffer: { ...treffer, zu: true }, laeuft: true }]);
    setLaeuft(true);
    let ergebnis;
    try {
      const antwort = await api('POST', '/api/assistent/frage', { frage: f, seite, verlauf: bisher });
      ergebnis = { antwort: antwort.antwort };
    } catch (e) {
      // Ohne Antwort der KI hilft die Anleitung weiter.
      ergebnis = { fehler: e.message, treffer };
    }
    setVerlauf((v) => v.map((e, i) => (i === v.length - 1 ? { ...e, ...ergebnis, laeuft: false } : e)));
    setLaeuft(false);
  };

  const schritte = stand.einrichtung || [];
  const erledigt = schritte.filter((s) => s.erledigt).length;
  const naechster = schritte.find((s) => !s.erledigt);
  const zurSeite = kapitelZurSeite(a, seite);
  const kapitelVon = (id) => a && a.kapitel.find((k) => k.id === id);

  // Zugeklappt steht nichts im Seiteninhalt; das Gespräch bleibt gemerkt.
  if (!offen) return null;
  return html`<aside class="assistent offen" aria-label="Assistent">
    <div class="assistent-kopf">
      <div><b>Assistent</b><br /><span class="klein">${stand.ki ? 'mit KI – fragen Sie, wie Sie einen Menschen fragen würden' : 'Hilfe zu jeder Seite und Antworten aus der Anleitung'}</span></div>
      <button class="schliessen" title="Assistent schließen (F1)" aria-label="Assistent schließen" onClick=${zu}>×</button>
    </div>

    <div class="assistent-inhalt">
      ${verlauf.length === 0 && html`
        ${naechster && html`<div class="abschnitt">
          <h3>Einrichtung: ${erledigt} von ${schritte.length} Schritten erledigt</h3>
          <div class="fortschritt"><div style=${`width:${Math.round(100 * erledigt / schritte.length)}%`}></div></div>
          <div class="naechster">
            <span class="leise klein">Als Nächstes</span>
            <b>${naechster.titel}</b>
            <span class="klein">${naechster.text}</span>
            <div class="knoepfe">
              <button class="haupt" onClick=${() => gehe(naechster.ziel)}>Dorthin gehen</button>
              ${kapitelVon(naechster.kapitel) && html`<button onClick=${() => gehe('#/anleitung?k=' + naechster.kapitel)}>So geht es</button>`}
            </div>
          </div>
          <button class="schlicht klein" onClick=${() => setAlleSchritte(!alleSchritte)}>${alleSchritte ? 'Schritte ausblenden' : 'Alle Schritte zeigen'}</button>
          ${alleSchritte && html`<ul class="haken-liste">${schritte.map((s) => html`<li key=${s.id} class=${s.erledigt ? 'erledigt' : ''}>
            <a href=${s.ziel}>${s.titel}</a></li>`)}</ul>`}
        </div>`}

        ${zurSeite.length > 0 && html`<div class="abschnitt">
          <h3>Zu dieser Seite</h3>
          ${zurSeite.map((k) => html`<${KapitelKarte} key=${seite + k.id} k=${k} offen=${zurSeite.length === 1} seite=${seite} />`)}
        </div>`}

        <div class="abschnitt">
          <h3>Häufige Fragen</h3>
          <div class="beispiele">${BEISPIELE.map((b) => html`<button key=${b} onClick=${() => stelle(b)}>${b}</button>`)}</div>
        </div>`}

      ${verlauf.map((e, i) => html`<div class="wechsel" key=${i}>
        <div class="gefragt">${e.frage}</div>
        ${e.laeuft && html`<div class="geantwortet leise">Der Assistent überlegt …</div>`}
        ${e.antwort && html`<div class="geantwortet"><${Antworttext} text=${e.antwort} />
          <div class="herkunft">Antwort der KI – sie kann sich irren. Maßgeblich ist, was das Programm bei der Prüfung meldet.</div></div>`}
        ${e.fehler && html`<${Kasten} art="warnung">${e.fehler}<//>`}
        ${!e.laeuft && (() => {
          const t = e.treffer;
          const nichts = !t.kapitel.length && !t.begriffe.length && !t.meldungen.length;
          if (e.antwort) return nichts ? null : html`<details class="dazu"><summary>Dazu in der Anleitung</summary><${Treffer} t=${t} seite=${seite} /></details>`;
          if (nichts) return html`<div class="geantwortet">Dazu habe ich in der Anleitung nichts gefunden. Versuchen Sie es mit einem anderen Wort – zum Beispiel „Kunde“, „Markt“, „senden“ oder „DATEV“ – oder sehen Sie die
            ${' '}<a href="#/anleitung">ganze Anleitung</a> durch.${!stand.ki ? html` Frei gestellte Fragen beantwortet der Assistent, wenn unter <a href="#/einstellungen">Einstellungen</a> die KI eingeschaltet ist.` : null}</div>`;
          return html`<div class="geantwortet schlicht">Das habe ich in der Anleitung gefunden:</div><${Treffer} t=${t} seite=${seite} />`;
        })()}
      </div>`)}
      ${verlauf.length > 0 && html`<p class="rechts"><button class="schlicht klein" onClick=${() => setVerlauf([])}>Neu beginnen</button></p>`}
      <div ref=${ende}></div>
    </div>

    <form class="assistent-frage" onSubmit=${(e) => { e.preventDefault(); stelle(text); }}>
      <input ref=${eingabe} value=${text} onInput=${(e) => setText(e.target.value)} maxlength="2000" aria-label="Frage an den Assistenten"
        placeholder=${stand.ki ? 'Ihre Frage …' : 'Wonach suchen Sie? Zum Beispiel: Markt anlegen'} />
      <button class="haupt" disabled=${laeuft || !text.trim()}>${stand.ki ? 'Fragen' : 'Suchen'}</button>
    </form>
    <div class="assistent-fuss"><a href="#/anleitung">Ganze Anleitung öffnen</a>${!stand.ki ? html` · <a href="#/einstellungen?zu=assistent">KI einschalten</a>` : null}</div>
  </aside>`;
}

// ---------------------------------------------------------------- Anleitung

// Anleitungsseite zeigt die ganze Anleitung zum Lesen und Drucken.
export function Anleitungsseite({ kapitel }) {
  const a = useAnleitung();
  useEffect(() => {
    if (!a || !kapitel) return;
    const el = document.getElementById('kapitel-' + kapitel);
    if (el) { el.scrollIntoView({ block: 'start' }); el.classList.add('blink'); }
  }, [a, kapitel]);
  if (!a) return html`<p class="laden">Anleitung wird geladen …</p>`;
  const zeige_ = (id) => { const el = document.getElementById('kapitel-' + id); if (el) el.scrollIntoView({ behavior: 'smooth', block: 'start' }); };
  return html`
    <div class="kopf"><div><h1>${a.titel}</h1></div>
      <div class="knoepfe">
        ${stamm.anleitungPdf ? html`<button onClick=${() => api('POST', '/api/anleitung/oeffnen').catch(zeigeFehler)}>Anleitung mit Bildern (PDF)</button>` : null}
        <button onClick=${() => window.print()}>Drucken</button>
      </div></div>
    <div class="karte"><p>${a.vorwort}</p>
      <ol class="inhaltsliste">${a.kapitel.map((k) => html`<li key=${k.id}><a href="#/anleitung?k=${k.id}" onClick=${(e) => { e.preventDefault(); zeige_(k.id); }}>${k.titel}</a></li>`)}</ol>
    </div>
    ${a.kapitel.map((k, i) => html`<div class="karte kapitel" id=${'kapitel-' + k.id} key=${k.id}>
      <h2>${i + 1}. ${k.titel}</h2>
      <p class="leise">${k.kurz}</p>
      <ol class="anleitung">${k.schritte.map((s, j) => html`<li key=${j}>${s}</li>`)}</ol>
      ${(k.hinweise || []).map((h, j) => html`<${Kasten} key=${j} art="hinweis"><b>Gut zu wissen:</b> ${h}<//>`)}
      ${k.ziel ? html`<p class="knoepfe" style="margin-bottom:0"><button onClick=${() => gehe(k.ziel)}>Dorthin gehen</button></p>` : null}
    </div>`)}
    ${(a.begriffe || []).length > 0 && html`<div class="karte kapitel" id="kapitel-begriffe">
      <h2>Begriffe kurz erklärt</h2>
      <dl class="begriffe">${a.begriffe.map((b) => html`<dt key=${b.wort}>${b.wort}</dt><dd>${b.text}</dd>`)}</dl>
    </div>`}`;
}

// --------------------------------------------------------- Einstellungen

// KiKarte: den KI-Teil des Assistenten ein- und ausschalten.
export function KiKarte() {
  const [stand, setStand] = useState(null);
  const [schluessel, setSchluessel] = useState('');
  const [laeuft, setLaeuft] = useState(false);
  useEffect(() => { api('GET', '/api/assistent').then(setStand).catch(() => setStand({ ki: false })); }, []);
  // Der Verweis #/einstellungen?zu=assistent führt gleich zu diesem Abschnitt.
  const da = !!stand;
  useEffect(() => {
    if (!da || !/[?&]zu=assistent\b/.test(location.hash)) return;
    setTimeout(() => { const el = document.getElementById('assistent'); if (el) el.scrollIntoView({ block: 'start' }); }, 60);
  }, [da]);
  const melde = () => window.dispatchEvent(new Event('assistentneu'));
  const schalteEin = async () => {
    setLaeuft(true);
    try {
      await api('POST', '/api/assistent/schluessel', { schluessel });
      setStand((s) => ({ ...s, ki: true })); setSchluessel(''); melde();
      zeige('Der KI-Assistent ist eingeschaltet. Er öffnet sich über „Assistent“ links unten oder die Taste F1.', 'gut');
    } catch (e) { zeigeFehler(e); } finally { setLaeuft(false); }
  };
  const schalteAus = async () => {
    if (!await frage('Die KI ausschalten? Der gespeicherte Zugangsschlüssel wird gelöscht. Der Assistent hilft danach weiter mit der eingebauten Anleitung.', { ja: 'Ausschalten', titel: 'KI ausschalten' })) return;
    try { await api('POST', '/api/assistent/schluessel', { schluessel: '' }); setStand((s) => ({ ...s, ki: false })); melde(); } catch (e) { zeigeFehler(e); }
  };
  if (!stand) return null;
  return html`<div class="karte" id="assistent">
    <h2>Assistent mit KI (freiwillig)</h2>
    <p>Der Assistent hilft auch ohne Internet: Er kennt die Anleitung, zeigt zu jeder Seite die passenden Schritte und führt durch die Einrichtung.
      Wer Fragen frei stellen möchte – so, wie man einen Menschen fragt –, kann zusätzlich eine KI einschalten (${stand.modell || 'Claude'} von Anthropic).</p>
    ${stand.ki ? html`
      <${Kasten} art="gut">Die KI ist eingeschaltet. Der Zugangsschlüssel liegt geschützt auf diesem Rechner.<//>
      <button onClick=${schalteAus}>KI ausschalten</button>` : html`
      <div class="raster">
        <label class="feld b8"><span>Zugangsschlüssel (API-Key) von Anthropic</span>
          <input type="password" autocomplete="off" value=${schluessel} onInput=${(e) => setSchluessel(e.target.value)} placeholder="sk-ant-…" data-feld="ki.schluessel" />
          <small>Der Schlüssel wird beim Einschalten geprüft und geschützt auf diesem Rechner abgelegt.</small></label>
        <div class="feld b4" style="justify-content:center"><button class="haupt" disabled=${laeuft || !schluessel.trim()} onClick=${schalteEin}>${laeuft ? 'Wird geprüft …' : 'KI einschalten'}</button></div>
      </div>
      <details><summary>So bekommt man einen Zugangsschlüssel</summary>
        <ol>
          <li>Im Browser platform.claude.com aufrufen und ein Konto anlegen.</li>
          <li>Dort unter „Billing“ ein Guthaben aufladen – abgerechnet wird nach Verbrauch.</li>
          <li>Unter „API Keys“ einen neuen Schlüssel erzeugen und kopieren.</li>
          <li>Den Schlüssel hier einfügen und auf „KI einschalten“ klicken.</li>
        </ol>
      </details>`}
    <details><summary>Was dabei übertragen wird und was es kostet</summary>
      <ul>
        <li><b>Übertragen werden</b> an Anthropic: die Frage, so wie sie eingetippt wird, das laufende Gespräch im Assistenten, die Anleitung des Programms und der Stand der Einrichtung (welche Seite geöffnet ist, welche Schritte erledigt und welche Zusatzfunktionen eingeschaltet sind).</li>
        <li><b>Nicht übertragen werden</b> Kunden, Märkte, Artikel, Rechnungen, die eigenen Firmendaten und Kennwörter – außer man schreibt so etwas selbst in die Frage.</li>
        <li><b>Kosten:</b> Anthropic rechnet nach Verbrauch ab, je Frage sind es wenige Cent. Das Programm selbst verlangt dafür nichts.</li>
        <li>Die KI kann nichts im Programm anklicken oder ändern, und sie kann sich irren. Ob eine Rechnung richtig ist, entscheidet weiterhin die Prüfung im Programm.</li>
        <li>Lehnt das Modell eine Frage ab, darf ein Ersatzmodell von Anthropic antworten.</li>
      </ul>
    </details>
  </div>`;
}
