// Rechnungen: Übersicht, Bearbeiten eines Entwurfs, Ansicht einer fertigen
// Rechnung und der Versand per E-Mail.
import {
  html, useState, useEffect, useRef, api, stamm, zahlDE, geld, datumDE, heute, gehe, zeige, zeigeFehler,
  Feld, Text, Mehrzeilig, Auswahl, Zahl, Dialog, Kasten, Etikett, PdfDialog, Meldungsliste, zaehle, springeZu,
  setzeIn, STATUS, belegName, ladeDatei, Haken,
} from './basis.js';

// ------------------------------------------------------------- Übersicht

export function Uebersicht() {
  const [liste, setListe] = useState(null);
  const [einst, setEinst] = useState(null);
  const [kunden, setKunden] = useState([]);
  const [filter, setFilter] = useState('alle');
  const [suche, setSuche] = useState('');
  const [datev, setDatev] = useState(false);
  useEffect(() => {
    Promise.all([api('GET', '/api/rechnungen'), api('GET', '/api/einstellungen'), api('GET', '/api/kunden')])
      .then(([l, e, k]) => { setListe(l); setEinst(e); setKunden(k); }).catch(zeigeFehler);
  }, []);
  if (!liste || !einst) return html`<p class="laden">Rechnungen werden geladen …</p>`;

  const offen = (r) => r.status === 'fertig' || r.status === 'versendet';
  const passt = { alle: () => true, entwurf: (r) => r.status === 'entwurf', offen, bezahlt: (r) => r.status === 'bezahlt' };
  const wort = suche.trim().toLowerCase();
  const sichtbar = liste.filter(passt[filter]).filter((r) => !wort ||
    [r.nummer, r.kunde, r.markt, datumDE(r.datum)].some((t) => (t || '').toLowerCase().includes(wort)));
  const summeOffen = liste.filter(offen).reduce((s, r) => s + (r.art === '381' ? -1 : 1) * Number(r.brutto || 0), 0);
  const anzahl = (f) => liste.filter(passt[f]).length;

  const firmaDa = !!(einst.firma.name && einst.firma.strasse && (einst.firma.ustId || einst.firma.steuernummer) && einst.iban);
  const postDa = !!(einst.post.server && einst.post.passwortDa);
  const fertigEingerichtet = firmaDa && kunden.length > 0 && postDa && liste.length > 0;

  return html`
    <div class="kopf">
      <div><h1>Rechnungen</h1>
        <div class="leise">${liste.filter(offen).length} offen · ${geld(summeOffen)} ausstehend</div></div>
      <div class="knoepfe">
        <button onClick=${() => setDatev(true)} title="Rechnungen an die Steuerkanzlei oder nach DATEV Unternehmen online übergeben">DATEV-Export</button>
        <button onClick=${() => gehe('#/pruefen')}>Aus Lexoffice importieren</button>
        <button class="haupt gross" onClick=${() => gehe('#/rechnung/neu')}>+ Neue Rechnung</button>
      </div>
    </div>
    ${datev && html`<${DatevDialog} zu=${() => setDatev(false)} />`}

    ${!fertigEingerichtet && html`<div class="karte">
      <h2>Erste Schritte</h2>
      <ol class="schritte">
        <li class=${firmaDa ? 'erledigt' : ''}><span><b>Eigene Firmendaten und Bankverbindung eintragen</b><br /><span class="leise klein">Name, Anschrift, Steuernummer oder USt-IdNr., GLN, IBAN</span></span>
          <button onClick=${() => gehe('#/einstellungen')}>${firmaDa ? 'Ansehen' : 'Eintragen'}</button></li>
        <li class=${kunden.length ? 'erledigt' : ''}><span><b>Kunden anlegen</b><br /><span class="leise klein">Rechnungsempfänger mit GLN, die belieferten Märkte und die Rechnungsadresse</span></span>
          <button onClick=${() => gehe('#/kunden/neu')}>Kunde anlegen</button></li>
        <li class=${postDa ? 'erledigt' : ''}><span><b>E-Mail-Versand einrichten</b><br /><span class="leise klein">Damit Rechnungen direkt aus dem Programm verschickt werden</span></span>
          <button onClick=${() => gehe('#/einstellungen')}>${postDa ? 'Ansehen' : 'Einrichten'}</button></li>
        <li class=${liste.length ? 'erledigt' : ''}><span><b>Erste Rechnung schreiben</b><br /><span class="leise klein">Das Programm prüft sie vor dem Versand und sagt, was fehlt</span></span>
          <button class="haupt" onClick=${() => gehe('#/rechnung/neu')}>Neue Rechnung</button></li>
      </ol>
    </div>`}

    <div class="karte">
      <div class="kopf" style="margin-bottom:.4rem">
        <div class="reiter" style="margin:0">
          ${[['alle', 'Alle'], ['entwurf', 'Entwürfe'], ['offen', 'Offen'], ['bezahlt', 'Bezahlt']].map(([f, name]) => html`
            <button class=${filter === f ? 'aktiv' : ''} onClick=${() => setFilter(f)}>${name} (${anzahl(f)})</button>`)}
        </div>
        <input style="width:260px" placeholder="Suchen: Nummer, Kunde, Markt" value=${suche} onInput=${(e) => setSuche(e.target.value)} />
      </div>
      ${sichtbar.length === 0 ? html`<div class="leer">${liste.length === 0 ? 'Noch keine Rechnung vorhanden.' : 'Keine Rechnung passt zur Auswahl.'}</div>` : html`
      <table class="wahl">
        <thead><tr><th>Nummer</th><th>Datum</th><th>Kunde</th><th class="zahl">Betrag</th><th>Fällig</th><th>Stand</th></tr></thead>
        <tbody>${sichtbar.map((r) => {
          const ueberfaellig = offen(r) && r.faellig && r.faellig < heute() && r.art !== '381';
          return html`<tr key=${r.id} onClick=${() => gehe('#/rechnung/' + r.id)}>
            <td class="nowrap"><b>${r.nummer || '–'}</b>${r.art === '381' ? html`<br /><span class="leise klein">Korrektur</span>` : null}</td>
            <td class="nowrap">${datumDE(r.datum)}</td>
            <td>${r.kunde || html`<span class="leise">noch kein Kunde</span>`}${r.markt ? html`<br /><span class="leise klein">${r.markt}</span>` : null}</td>
            <td class="zahl">${r.art === '381' ? '-' : ''}${geld(r.brutto)}</td>
            <td class="nowrap" style=${ueberfaellig ? 'color:var(--rot);font-weight:600' : ''}>${r.status === 'entwurf' ? '' : datumDE(r.faellig)}</td>
            <td><${Etikett} status=${r.status} />
              ${r.versendetAm && r.status !== 'bezahlt' ? html`<br /><span class="leise klein">am ${datumDE(r.versendetAm)}</span>` : null}</td>
          </tr>`;
        })}</tbody>
      </table>`}
    </div>`;
}

// ------------------------------------------------------- Rechnung öffnen

function leerePosition(einst) {
  return { bezeichnung: '', artikelnummer: '', gtin: '', kundenArtNr: '', beschreibung: '', menge: '1',
    einheit: einst.standardEinheit || 'H87', einzelpreis: '0', rabattProzent: '0', steuerKat: 'S',
    steuerSatz: einst.standardSteuer || '19' };
}

function neuerEntwurf(einst) {
  return { id: '', status: 'entwurf', art: '380', nummer: '', datum: heute(), leistungsdatum: heute(), kundeId: '',
    lieferort: null, bestellnummer: '', lieferscheinnummer: '', kostenstelle: '', kaeuferreferenz: '',
    positionen: [leerePosition(einst)], zuAbschlaege: [], hinweise: [],
    zahlung: { art: '58', zieltage: einst.zieltage || 0, skontoTage: 0, skontoProzent: '0', vorauszahlung: '0' },
    einleitung: einst.einleitung || '', schluss: einst.schluss || '', format: '' };
}

export function Rechnungsseite({ id }) {
  const [zustand, setZustand] = useState(null);
  useEffect(() => {
    setZustand(null);
    (async () => {
      const [einst, kunden, artikel] = await Promise.all([api('GET', '/api/einstellungen'), api('GET', '/api/kunden'), api('GET', '/api/artikel')]);
      const r = id === 'neu' ? neuerEntwurf(einst) : await api('GET', '/api/rechnungen/' + id);
      setZustand({ r, einst, kunden, artikel });
    })().catch((e) => { zeigeFehler(e); gehe('#/'); });
  }, [id]);
  if (!zustand) return html`<p class="laden">Rechnung wird geladen …</p>`;
  if (zustand.r.status === 'entwurf') return html`<${Editor} ...${zustand} />`;
  return html`<${Ansicht} anfang=${zustand.r} einst=${zustand.einst} />`;
}

// ------------------------------------------------------------ DATEV-Export

// DatevDialog: Zeitraum wählen und die beiden Dateien für DATEV holen.
function DatevDialog({ zu }) {
  const vormonat = (() => { const d = new Date(); d.setDate(1); d.setMonth(d.getMonth() - 1); return d.getFullYear() + '-' + String(d.getMonth() + 1).padStart(2, '0'); })();
  const [von, setVon] = useState(vormonat);
  const [bis, setBis] = useState(vormonat);
  const [link, setLink] = useState(true);
  const [u, setU] = useState(null);
  const frage = `von=${von}&bis=${bis}`;
  useEffect(() => {
    if (!/^\d{4}-\d{2}$/.test(von) || !/^\d{4}-\d{2}$/.test(bis)) return;
    setU(null);
    api('GET', '/api/datev/uebersicht?' + frage).then(setU).catch(zeigeFehler);
  }, [von, bis]);
  const monatDE = (m) => m.slice(5) + '/' + m.slice(0, 4);
  const leer = u && u.anzahl === 0;
  return html`<${Dialog} titel="DATEV-Export" zu=${zu} breit aktionen=${html`<button onClick=${zu}>Schließen</button>`}>
    <p class="leise" style="margin-top:-.4rem">Übergibt fertige Rechnungen an DATEV – als Dateien zum Herunterladen. Eine Verbindung zu DATEV baut das Programm nicht auf.</p>
    <div class="raster">
      <label class="feld b3"><span>Von Monat</span><input type="month" value=${von} onInput=${(e) => { setVon(e.target.value); if (e.target.value > bis) setBis(e.target.value); }} /></label>
      <label class="feld b3"><span>Bis Monat</span><input type="month" value=${bis} min=${von} onInput=${(e) => setBis(e.target.value)} /></label>
      <div class="feld b6" style="justify-content:flex-end">
        ${!u ? html`<span class="leise">wird gezählt …</span>`
          : leer ? html`<span>In diesem Zeitraum gibt es keine fertige Rechnung.</span>`
          : html`<span><b>${u.anzahl} ${u.anzahl === 1 ? 'Rechnung' : 'Rechnungen'}</b> · ${geld(u.brutto)} brutto
              ${u.monate.length > 1 ? html`<br /><span class="leise klein">${u.monate.map((m) => `${monatDE(m.monat)}: ${m.anzahl}`).join(' · ')}</span>` : null}</span>`}
      </div>
    </div>

    <${Kasten} art="warnung"><b>Erst klären, wer bucht.</b> Werden diese Rechnungen schon in Lexware Office gebucht und von dort an die Steuerkanzlei gegeben,
      die Buchungen hier nicht zusätzlich übergeben – sonst stehen sie doppelt in der Buchhaltung. Das Belegpaket allein ist dann meist auch nicht nötig.<//>

    <h3>1 · Belege für DATEV Unternehmen online</h3>
    <p class="klein">Eine ZIP-Datei mit den E-Rechnungen und ihren Rechnungsdaten (DATEV XML-Schnittstelle online).
      Sie wird mit dem kostenlosen Programm <b>DATEV Belegtransfer</b> übertragen; den Zugang dafür richtet die Steuerkanzlei ein.
      ${u && !u.mitKonten ? ' Konten gibt das Programm nicht mit – die ordnet dann die Kanzlei zu.' : ''}</p>
    <p><button class="haupt" disabled=${!u || leer} onClick=${() => ladeDatei('/api/datev/belege.zip?' + frage)}>Belegpaket herunterladen (ZIP)</button></p>

    <h3>2 · Buchungsstapel für die Steuerkanzlei</h3>
    <p class="klein">Eine CSV-Datei im DATEV-Format je Monat (Name beginnt mit EXTF_), die die Kanzlei in DATEV Rechnungswesen einliest:
      je Rechnung und Steuersatz der Bruttobetrag vom Kundenkonto an das Erlöskonto. Die Datei nicht mit Excel öffnen und speichern – das verändert sie.</p>
    ${u && u.angaben.length > 0
      ? html`<${Kasten} art="hinweis"><b>Dafür fehlen noch Angaben:</b><ul>${u.angaben.map((a) => html`<li>${a}</li>`)}</ul>
          <a href="#/einstellungen" onClick=${zu}>Zu den Einstellungen, Abschnitt DATEV</a><//>`
      : html`<${Haken} wert=${link} setze=${setLink}>Buchungen mit den Belegen verknüpfen (Beleglink) – nur sinnvoll, wenn auch das Belegpaket übertragen wird<//>
          <p style="margin-top:.6rem"><button class="haupt" disabled=${!u || leer} onClick=${() => ladeDatei('/api/datev/buchungen?' + frage + (link ? '&beleglink=1' : ''))}>
            Buchungsstapel herunterladen${u && u.monate.length > 1 ? ' (ZIP, je Monat eine Datei)' : ' (CSV)'}</button></p>`}

    ${u && u.hinweise.length > 0 && html`<${Kasten} art="warnung"><b>Bitte beachten:</b><ul>${u.hinweise.map((h) => html`<li>${h}</li>`)}</ul><//>`}
  <//>`;
}

// ------------------------------------------------------------- Bearbeiten

const marktText = (l) => [l.name, [l.plz, l.ort].filter(Boolean).join(' ')].filter(Boolean).join(', ') + (l.gln ? ' · GLN ' + l.gln : '');

// Marktsuche ist die Auswahl des Markts bei Kunden mit vielen Märkten: ein
// Eingabefeld, das beim Tippen die passenden Märkte vorschlägt.
function Marktsuche({ maerkte, gewaehlt, waehle }) {
  const [text, setText] = useState(gewaehlt ? marktText(gewaehlt) : '');
  useEffect(() => { setText(gewaehlt ? marktText(gewaehlt) : ''); }, [gewaehlt && gewaehlt.id]);
  const eingabe = (wert) => {
    setText(wert);
    const treffer = maerkte.find((l) => marktText(l) === wert);
    if (treffer) waehle(treffer.id);
    else if (wert.trim() === '' && gewaehlt) waehle('');
  };
  return html`
    <input list="marktliste" data-feld="lieferort.gln" value=${text} placeholder="Markt suchen …" autocomplete="off"
      style=${gewaehlt || !text ? '' : 'border-color:var(--gelb)'}
      onInput=${(e) => eingabe(e.target.value)} onBlur=${() => setText(gewaehlt ? marktText(gewaehlt) : '')} />
    <datalist id="marktliste">${maerkte.map((l) => html`<option value=${marktText(l)}></option>`)}</datalist>`;
}

function Editor({ r: anfang, einst, kunden, artikel }) {
  const [r, setR] = useState(anfang);
  const [erg, setErg] = useState(null);
  const [gespeichert, setGespeichert] = useState(!!anfang.id);
  const [vorschau, setVorschau] = useState(null);
  const [fertigDialog, setFertigDialog] = useState(false);
  const [beschaeftigt, setBeschaeftigt] = useState(false);
  const aktuell = useRef(r);
  aktuell.current = r;
  const lauf = useRef({ pruefen: 0, speicherUhr: null, pruefUhr: null, speichert: null });

  const kunde = kunden.find((k) => k.id === r.kundeId);
  const klein = !!einst.kleinunternehmer;

  // Prüfen und speichern laufen mit kurzer Verzögerung nach jeder Änderung.
  const pruefe = async () => {
    const nr = ++lauf.current.pruefen;
    try {
      const e = await api('POST', '/api/pruefen', aktuell.current);
      if (nr === lauf.current.pruefen) setErg(e);
    } catch (e) { /* beim nächsten Tastendruck erneut */ }
  };
  const speichere = async () => {
    clearTimeout(lauf.current.speicherUhr);
    if (lauf.current.speichert) await lauf.current.speichert;
    const stand = aktuell.current;
    lauf.current.speichert = (async () => {
      const neu = await api('POST', '/api/rechnungen', stand);
      if (!stand.id) {
        setR((x) => ({ ...x, id: neu.id }));
        aktuell.current = { ...aktuell.current, id: neu.id };
        history.replaceState(null, '', '#/rechnung/' + neu.id);
      }
      if (aktuell.current === stand || aktuell.current.id === neu.id && JSON.stringify({ ...aktuell.current, id: neu.id }) === JSON.stringify({ ...stand, id: neu.id })) setGespeichert(true);
      return neu.id;
    })();
    try { return await lauf.current.speichert; } finally { lauf.current.speichert = null; }
  };
  const aendere = (pfad, wert) => {
    setR((x) => setzeIn(x, pfad, wert));
    setGespeichert(false);
  };
  useEffect(() => {
    clearTimeout(lauf.current.pruefUhr);
    lauf.current.pruefUhr = setTimeout(pruefe, 300);
    if (!gespeichert && r.kundeId) {
      clearTimeout(lauf.current.speicherUhr);
      lauf.current.speicherUhr = setTimeout(() => speichere().catch(zeigeFehler), 1500);
    }
  }, [r]);
  useEffect(() => () => { clearTimeout(lauf.current.pruefUhr); clearTimeout(lauf.current.speicherUhr); }, []);

  const waehleKunde = (kid) => {
    const k = kunden.find((x) => x.id === kid);
    setR((x) => {
      const neu = { ...x, kundeId: kid, lieferort: null };
      if (k) {
        neu.zahlung = { ...x.zahlung, zieltage: k.zieltage || x.zahlung.zieltage, skontoTage: k.skontoTage || 0, skontoProzent: k.skontoProzent || '0' };
        if (k.lieferorte.length === 1) {
          neu.lieferort = { ...k.lieferorte[0] };
          if (k.lieferorte[0].kostenstelle) neu.kostenstelle = k.lieferorte[0].kostenstelle;
        }
      }
      return neu;
    });
    setGespeichert(false);
  };
  const waehleMarkt = (lid) => {
    const l = kunde && kunde.lieferorte.find((x) => x.id === lid);
    setR((x) => ({ ...x, lieferort: l ? { ...l } : null, kostenstelle: l && l.kostenstelle ? l.kostenstelle : x.kostenstelle }));
    setGespeichert(false);
  };
  const waehleArtikel = (i, name) => {
    const a = artikel.find((x) => x.bezeichnung === name);
    setR((x) => {
      const pos = [...x.positionen];
      pos[i] = { ...pos[i], bezeichnung: name };
      if (a) {
        pos[i] = { ...pos[i], artikelnummer: a.artikelnummer || '', gtin: a.gtin || '', beschreibung: a.beschreibung || '',
          einheit: a.einheit || pos[i].einheit, einzelpreis: a.preis || '0', steuerSatz: a.steuerSatz || pos[i].steuerSatz,
          herkunft: a.herkunft || '', kundenArtNr: (a.kundenArtNr && a.kundenArtNr[x.kundeId]) || '' };
      }
      return { ...x, positionen: pos };
    });
    setGespeichert(false);
  };
  const neuePosition = () => { aendere('positionen', [...r.positionen, leerePosition(einst)]); };
  const entfernePosition = (i) => aendere('positionen', r.positionen.filter((_, j) => j !== i));

  const zeigeVorschau = async () => {
    try {
      const blob = await api('POST', '/api/vorschau', aktuell.current, { blob: true });
      setVorschau(URL.createObjectURL(blob));
    } catch (e) { zeigeFehler(e); }
  };
  const loesche = async () => {
    if (!confirm('Diesen Entwurf wirklich löschen?')) return;
    try {
      clearTimeout(lauf.current.speicherUhr);
      if (r.id) await api('DELETE', '/api/rechnungen/' + r.id);
      gehe('#/');
    } catch (e) { zeigeFehler(e); }
  };
  const stelleFertig = async () => {
    setBeschaeftigt(true);
    try {
      const rid = await speichere();
      await api('POST', `/api/rechnungen/${rid}/fertigstellen`, { trotzWarnungen: true });
      zeige('Die Rechnung ist fertiggestellt.', 'gut');
      setFertigDialog(false);
      sessionStorage.setItem('senden', rid);
      gehe('#/rechnung/' + rid + '?fertig');
    } catch (e) {
      zeigeFehler(e);
      if (e.meldungen && e.meldungen.length) pruefe();
      setFertigDialog(false);
    } finally { setBeschaeftigt(false); }
  };

  const klick = (m) => {
    if (springeZu(m.feld)) return;
    if (m.feld.startsWith('verkaeufer.') || m.feld.startsWith('zahlung.iban') || m.feld.startsWith('zahlung.bic')) {
      if (confirm('Diese Angabe steht in den Einstellungen (Firmendaten). Jetzt dorthin wechseln? Der Entwurf wird gespeichert.')) speichere().then(() => gehe('#/einstellungen')).catch(zeigeFehler);
    } else if ((m.feld.startsWith('kaeufer.') || m.feld.startsWith('lieferort.')) && kunde) {
      if (confirm('Diese Angabe steht beim Kunden. Jetzt den Kunden bearbeiten? Der Entwurf wird gespeichert.')) speichere().then(() => gehe('#/kunden/' + kunde.id + '?zurueck=' + (aktuell.current.id || ''))).catch(zeigeFehler);
    } else if (m.feld === 'kundeId') springeZu('kundeId');
  };

  const z = zaehle(erg && erg.meldungen);
  const aus = erg && erg.rechnung;
  const summen = aus && aus.summen;
  const xr = (r.format || (kunde && kunde.format) || einst.format || '').includes('xr');
  const profil = kunde && stamm.profile.find((p) => p.id === kunde.profil);
  const steuerOptionen = [['19', '19 %'], ['7', '7 %'], ['0', '0 %']];
  const gutschrift = r.art === '381';

  return html`
    <div class="kopf">
      <div>
        <h1>${gutschrift ? 'Rechnungskorrektur' : 'Rechnung'} <span class="leise">· Entwurf</span></h1>
        <div class="leise">${r.nummer ? 'Nummer ' + r.nummer : erg ? 'Erhält beim Fertigstellen die Nummer ' + erg.nummerVorschau : ''}
          ${' · '}${!r.kundeId ? 'wird gespeichert, sobald ein Kunde gewählt ist' : gespeichert ? 'gespeichert' : 'wird gespeichert …'}</div>
      </div>
      <div class="knoepfe">
        <button onClick=${() => gehe('#/')}>Zur Übersicht</button>
        <button class="gefahr" onClick=${loesche}>Entwurf löschen</button>
        <button onClick=${zeigeVorschau}>Vorschau</button>
        <button class="haupt" disabled=${!erg || z.fehler > 0} title=${z.fehler > 0 ? 'Bitte erst die Fehler beheben' : ''}
          onClick=${() => setFertigDialog(true)}>Fertigstellen …</button>
      </div>
    </div>

    ${r.herkunft && html`<${Kasten} art="hinweis">Dieser Entwurf wurde aus der Datei <b>${r.herkunft}</b> übernommen. Die Rechnungsnummer ${r.nummer} bleibt erhalten. Bitte die fehlenden Angaben ergänzen und die Rechnung dann als korrigierte E-Rechnung erneut versenden.<//>`}

    ${Number(r.herkunftsbetrag) && summen && Number(summen.brutto) !== Number(r.herkunftsbetrag) ? html`<${Kasten} art="warnung">
      <b>Die Summe weicht ab:</b> In der eingelesenen Rechnung beträgt der Rechnungsbetrag ${geld(r.herkunftsbetrag)}, hier ergeben sich ${geld(summen.brutto)}.
      Beide Fassungen müssen denselben Betrag ausweisen. Bitte Positionen, Rabatte und Steuersätze vergleichen; eine Abweichung um einen Cent entsteht meist durch andere Rundung.<//>` : null}

    <div class="spalten">
      <div>
        <div class="karte">
          <h2>Empfänger</h2>
          <div class="raster">
            <${Feld} titel="Kunde (Rechnungsempfänger)" breite="6">
              <${Auswahl} feld="kundeId" wert=${r.kundeId} setze=${waehleKunde}
                optionen=${[['', '– bitte wählen –'], ...kunden.map((k) => [k.id, k.name])]} />
            <//>
            <${Feld} titel="Belieferter Markt (Warenempfänger)" breite="6"
              hilfe=${kunde && kunde.lieferorte.length === 0 ? 'Für diesen Kunden ist noch kein Markt hinterlegt.' : kunde && kunde.lieferorte.length > 8 ? 'Name, Ort oder GLN eintippen und aus der Liste wählen' : ''}>
              ${kunde && kunde.lieferorte.length > 8
                ? html`<${Marktsuche} maerkte=${kunde.lieferorte} gewaehlt=${r.lieferort} waehle=${waehleMarkt} />`
                : html`<${Auswahl} feld="lieferort.gln" wert=${r.lieferort ? r.lieferort.id : ''} setze=${waehleMarkt} disabled=${!kunde}
                    optionen=${[['', kunde && kunde.lieferorte.length ? '– bitte wählen –' : '– keiner –'],
                      ...(kunde ? kunde.lieferorte.map((l) => [l.id, marktText(l)]) : [])]} />`}
            <//>
          </div>
          ${kunde && html`<p class="klein leise" style="margin:.7rem 0 0">
            ${[kunde.strasse, (kunde.plz + ' ' + kunde.ort).trim(), kunde.gln && 'GLN ' + kunde.gln, kunde.ustId, kunde.lieferantennummer && 'unsere Lieferantennr. ' + kunde.lieferantennummer].filter(Boolean).join(' · ')}
            ${' '}<a href=${'#/kunden/' + kunde.id + '?zurueck=' + (r.id || '')} onClick=${(e) => { e.preventDefault(); speichere().then((rid) => gehe('#/kunden/' + kunde.id + '?zurueck=' + rid)).catch(zeigeFehler); }}>Kunde und Märkte bearbeiten</a></p>`}
          ${r.lieferort && html`<p class="klein leise" style="margin:.3rem 0 0">Markt:${' '}
            ${[r.lieferort.name, r.lieferort.strasse, ((r.lieferort.plz || '') + ' ' + (r.lieferort.ort || '')).trim(), r.lieferort.gln && 'GLN ' + r.lieferort.gln].filter(Boolean).join(' · ')}</p>`}
          ${!kunden.length && html`<${Kasten} art="warnung">Es ist noch kein Kunde angelegt. <a href="#/kunden/neu">Jetzt einen Kunden anlegen</a><//>`}
        </div>

        <div class="karte">
          <h2>Angaben zur Rechnung</h2>
          <div class="raster">
            <${Feld} titel="Rechnungsdatum" breite="3"><${Text} typ="date" feld="datum" wert=${r.datum} setze=${(v) => aendere('datum', v)} /><//>
            <${Feld} titel="Lieferdatum" breite="3"><${Text} typ="date" feld="leistungsdatum" wert=${r.leistungsdatum} setze=${(v) => aendere('leistungsdatum', v)} /><//>
            <${Feld} titel="Bestellnummer" breite="3"><${Text} feld="bestellnummer" wert=${r.bestellnummer} setze=${(v) => aendere('bestellnummer', v)} /><//>
            <${Feld} titel="Lieferscheinnummer" breite="3"><${Text} feld="lieferscheinnummer" wert=${r.lieferscheinnummer} setze=${(v) => aendere('lieferscheinnummer', v)} /><//>
            <${Feld} titel="Kostenstelle" breite="3" hilfe=${kunde && kunde.profil === 'rewe' ? 'nur bei Kostenrechnungen (10 Ziffern)' : 'falls der Kunde eine nennt'}>
              <${Text} feld="kostenstelle" wert=${r.kostenstelle} setze=${(v) => aendere('kostenstelle', v.trim())} /><//>
            <${Feld} titel="Käuferreferenz" breite="3" hilfe=${xr ? 'Pflicht in der XRechnung; leer = GLN des Kunden' : 'nur falls vom Kunden verlangt'}>
              <${Text} feld="kaeuferreferenz" wert=${r.kaeuferreferenz} setze=${(v) => aendere('kaeuferreferenz', v)} placeholder=${kunde ? kunde.kaeuferreferenz : ''} /><//>
            <${Feld} titel="Rechnungsnummer" breite="3" hilfe="leer lassen = automatisch">
              <${Text} feld="nummer" wert=${r.nummer} setze=${(v) => aendere('nummer', v)} placeholder=${erg ? erg.nummerVorschau : ''} /><//>
            <${Feld} titel="Format" breite="3">
              <${Auswahl} feld="format" wert=${r.format || ''} setze=${(v) => aendere('format', v)}
                optionen=${[['', 'wie beim Kunden hinterlegt'], ...stamm.formate.map((f) => [f.id, f.name])]} /><//>
            ${gutschrift && html`
              <${Feld} titel="Korrigiert die Rechnung Nr." breite="3"><${Text} feld="vorgaenger.0.nummer" wert=${r.vorgaenger && r.vorgaenger[0] ? r.vorgaenger[0].nummer : ''}
                setze=${(v) => aendere('vorgaenger', [{ ...(r.vorgaenger && r.vorgaenger[0]), nummer: v }])} /><//>
              <${Feld} titel="vom" breite="3"><${Text} typ="date" feld="vorgaenger.0.datum" wert=${r.vorgaenger && r.vorgaenger[0] ? r.vorgaenger[0].datum : ''}
                setze=${(v) => aendere('vorgaenger', [{ ...(r.vorgaenger && r.vorgaenger[0]), datum: v }])} /><//>`}
            <${Feld} titel="Einleitungstext" breite="12"><${Mehrzeilig} zeilen="2" wert=${r.einleitung} setze=${(v) => aendere('einleitung', v)} /><//>
          </div>
        </div>

        <div class="karte">
          <h2>Positionen</h2>
          <datalist id="artikelliste">${artikel.map((a) => html`<option value=${a.bezeichnung}>${[a.artikelnummer, a.gtin].filter(Boolean).join(' · ')}</option>`)}</datalist>
          ${r.positionen.map((p, i) => {
            const f = (n) => `positionen.${i}.${n}`;
            const netto = aus && aus.positionen[i] ? aus.positionen[i].netto : '';
            const gtinFalsch = p.gtin && !/^\d+$/.test(p.gtin);
            return html`<div class="position" key=${i}>
              <div class="zeile1">
                <div class="nr">${i + 1}</div>
                <label>Bezeichnung<input list="artikelliste" data-feld=${f('bezeichnung')} value=${p.bezeichnung}
                  placeholder="Artikel eintippen oder aus der Liste wählen" onInput=${(e) => waehleArtikel(i, e.target.value)} /></label>
                <button class="weg" title="Position entfernen" onClick=${() => entfernePosition(i)} disabled=${r.positionen.length <= 1}>✕</button>
              </div>
              <div class="zeileB">
                <div></div>
                <label>Menge<${Zahl} feld=${f('menge')} wert=${p.menge} max="3" setze=${(v) => aendere(f('menge'), v)} /></label>
                <label>Einheit<${Auswahl} feld=${f('einheit')} wert=${p.einheit} setze=${(v) => aendere(f('einheit'), v)}
                  optionen=${[...(stamm.einheiten.some((e) => e.code === p.einheit) || !p.einheit ? [] : [[p.einheit, p.einheit + ' (unbekannt)']]), ...stamm.einheiten.map((e) => [e.code, e.name])]} /></label>
                <label>Einzelpreis netto<${Zahl} feld=${f('einzelpreis')} wert=${p.einzelpreis} min="2" max="4" setze=${(v) => aendere(f('einzelpreis'), v)} /></label>
                <label>Rabatt %<${Zahl} feld=${f('rabattProzent')} wert=${p.rabattProzent} max="2" leerBeiNull setze=${(v) => aendere(f('rabattProzent'), v)} /></label>
                <label>USt${klein
                  ? html`<input value="keine (§ 19)" disabled />`
                  : html`<${Auswahl} feld=${f('steuerSatz')} wert=${String(Number(p.steuerSatz))} setze=${(v) => aendere(f('steuerSatz'), v)}
                      optionen=${steuerOptionen.some(([w]) => w === String(Number(p.steuerSatz))) ? steuerOptionen : [...steuerOptionen, [String(Number(p.steuerSatz)), zahlDE(p.steuerSatz) + ' %']]} />`}</label>
                <div class="netto">${geld(netto)}</div>
              </div>
              <div class="zeile2">
                <div></div>
                <label>Artikelnummer<input data-feld=${f('artikelnummer')} value=${p.artikelnummer || ''} onInput=${(e) => aendere(f('artikelnummer'), e.target.value)} /></label>
                <label>GTIN / EAN<input data-feld=${f('gtin')} value=${p.gtin || ''} inputmode="numeric" style=${gtinFalsch ? 'border-color:var(--rot)' : ''}
                  onInput=${(e) => aendere(f('gtin'), e.target.value.replace(/\s/g, ''))} /></label>
                <label>Kunden-Artikelnr.<input data-feld=${f('kundenArtNr')} value=${p.kundenArtNr || ''} onInput=${(e) => aendere(f('kundenArtNr'), e.target.value)} /></label>
                <label>Zusatztext<input data-feld=${f('beschreibung')} value=${p.beschreibung || ''} onInput=${(e) => aendere(f('beschreibung'), e.target.value)} /></label>
              </div>
            </div>`;
          })}
          <button onClick=${neuePosition} data-feld="positionen">+ Position hinzufügen</button>

          ${summen && html`<div class="summen" style="margin-top:1rem">
            ${(aus.zuAbschlaege || []).length > 0 && html`<div><span>Summe Positionen</span><span>${geld(summen.positionen)}</span></div>`}
            ${(aus.zuAbschlaege || []).map((za) => html`<div><span>${za.zuschlag ? 'zuzüglich ' : 'abzüglich '}${Number(za.prozent) ? zahlDE(za.prozent) + ' % ' : ''}${za.grund}</span><span>${za.zuschlag ? '' : '-'}${geld(za.betrag)}</span></div>`)}
            <div><span>Netto</span><span>${geld(summen.netto)}</span></div>
            ${summen.steuern.filter((s) => s.kat === 'S').map((s) => html`<div><span>USt ${zahlDE(s.satz)} % auf ${zahlDE(s.basis, 2, 2)}</span><span>${geld(s.betrag)}</span></div>`)}
            ${klein && html`<div class="leise klein"><span>ohne Umsatzsteuer (§ 19 UStG)</span></div>`}
            <div class="gesamt"><span>${gutschrift ? 'Gutschrift' : 'Rechnungsbetrag'}</span><span>${geld(summen.brutto)}</span></div>
            ${Number(summen.vorauszahlung) ? html`<div><span>bereits gezahlt</span><span>-${geld(summen.vorauszahlung)}</span></div>
              <div><b>noch zu zahlen</b><b>${geld(summen.zahlbetrag)}</b></div>` : null}
          </div>`}
        </div>

        <div class="karte">
          <h2>Rabatte und Zuschläge auf die ganze Rechnung</h2>
          ${(r.zuAbschlaege || []).length === 0 && html`<p class="leise klein">Zum Beispiel ein Aktionsrabatt oder eine Frachtpauschale. Meist nicht nötig.</p>`}
          ${(r.zuAbschlaege || []).map((za, i) => {
            const f = (n) => `zuAbschlaege.${i}.${n}`;
            return html`<div class="raster" style="margin-bottom:.6rem" key=${i}>
              <${Feld} titel="Art" breite="2"><${Auswahl} wert=${za.zuschlag ? '1' : '0'} setze=${(v) => aendere(f('zuschlag'), v === '1')} optionen=${[['0', 'Rabatt'], ['1', 'Zuschlag']]} /><//>
              <${Feld} titel="Bezeichnung" breite="3"><${Text} feld=${f('grund')} wert=${za.grund} setze=${(v) => aendere(f('grund'), v)} placeholder=${za.zuschlag ? 'z. B. Fracht' : 'z. B. Aktionsrabatt'} /><//>
              <${Feld} titel="Prozent" breite="2" hilfe="oder fester Betrag"><${Zahl} wert=${za.prozent} max="2" leerBeiNull setze=${(v) => setR((x) => { setGespeichert(false); return setzeIn(setzeIn(x, f('prozent'), v), f('betrag'), '0'); })} /><//>
              <${Feld} titel="Betrag netto" breite="2"><${Zahl} feld=${f('betrag')} wert=${Number(za.prozent) ? (aus && aus.zuAbschlaege[i] ? aus.zuAbschlaege[i].betrag : '') : za.betrag} min="2" max="2" disabled=${!!Number(za.prozent)} setze=${(v) => aendere(f('betrag'), v)} /><//>
              <${Feld} titel="gilt für USt-Satz" breite="2">${klein ? html`<input value="–" disabled />` : html`<${Auswahl} feld=${f('steuerSatz')} wert=${String(Number(za.steuerSatz))} setze=${(v) => aendere(f('steuerSatz'), v)} optionen=${steuerOptionen} />`}<//>
              <div class="feld b1" style="grid-column:span 1;justify-content:flex-end"><button class="schlicht" onClick=${() => aendere('zuAbschlaege', r.zuAbschlaege.filter((_, j) => j !== i))}>Entfernen</button></div>
            </div>`;
          })}
          <button onClick=${() => aendere('zuAbschlaege', [...(r.zuAbschlaege || []), { zuschlag: false, grund: '', prozent: '0', betrag: '0', steuerKat: 'S', steuerSatz: (r.positionen[0] && r.positionen[0].steuerSatz) || '19' }])}>+ Rabatt oder Zuschlag</button>
        </div>

        <div class="karte">
          <h2>Zahlung</h2>
          <div class="raster">
            <${Feld} titel="Zahlungsziel in Tagen" breite="3"><${Text} typ="number" min="0" feld="faellig" wert=${String(r.zahlung.zieltage ?? 0)} setze=${(v) => aendere('zahlung.zieltage', Math.max(0, parseInt(v || '0', 10) || 0))} /><//>
            <${Feld} titel="Skonto: Frist in Tagen" breite="3"><${Text} typ="number" min="0" feld="zahlung.skontoTage" wert=${String(r.zahlung.skontoTage ?? 0)} setze=${(v) => aendere('zahlung.skontoTage', Math.max(0, parseInt(v || '0', 10) || 0))} /><//>
            <${Feld} titel="Skonto in Prozent" breite="3"><${Zahl} feld="zahlung.skontoProzent" wert=${r.zahlung.skontoProzent} max="2" leerBeiNull setze=${(v) => aendere('zahlung.skontoProzent', v)} /><//>
            <${Feld} titel="Bereits gezahlt" breite="3"><${Zahl} feld="zahlung.vorauszahlung" wert=${r.zahlung.vorauszahlung} min="2" max="2" leerBeiNull setze=${(v) => aendere('zahlung.vorauszahlung', v)} /><//>
            <${Feld} titel="Verwendungszweck" breite="6" hilfe="leer = Rechnungsnummer"><${Text} feld="zahlung.verwendungszweck" wert=${r.zahlung.verwendungszweck} setze=${(v) => aendere('zahlung.verwendungszweck', v)} /><//>
            <${Feld} titel="Schlusstext" breite="12"><${Mehrzeilig} zeilen="2" wert=${r.schluss} setze=${(v) => aendere('schluss', v)} /><//>
          </div>
          ${erg && html`<p class="klein leise" style="margin:.8rem 0 0">Auf der Rechnung steht: „${erg.zahlungstext}“
            ${aus && aus.zahlung.iban ? ` Bankverbindung: IBAN ${aus.zahlung.iban}.` : ''}</p>`}
        </div>
      </div>

      <div class="haftend">
        <div class="karte pruefung">
          <h2><span class=${'ampel ' + (!erg ? '' : z.fehler ? 'fehler' : z.warnung ? 'warnung' : 'gut')}></span>
            ${!erg ? 'Wird geprüft …' : z.fehler ? `${z.fehler} Fehler` : z.warnung ? 'Bereit, mit Warnungen' : 'Alles in Ordnung'}</h2>
          ${erg && z.fehler === 0 && z.warnung === 0 && html`<p class="klein">Die Rechnung erfüllt die Vorgaben der E-Rechnung${profil ? ' und die bekannten Anforderungen von ' + profil.name : ''}. Sie kann fertiggestellt und versendet werden.</p>`}
          ${erg && z.fehler > 0 && html`<p class="klein leise">Ein Klick auf eine Meldung führt zum Feld.</p>`}
          ${erg && html`<${Meldungsliste} meldungen=${erg.meldungen} klick=${klick} />`}
        </div>
        ${profil && html`<div class="karte schmal klein">
          <b>Vorgaben: ${profil.name}</b>
          <details><summary>Was ${profil.name === 'Handel allgemein' ? 'der Handel' : profil.name} verlangt</summary>
            <ul style="padding-left:1.1rem;margin:.3rem 0">${profil.hinweise.map((h) => html`<li style="margin-bottom:.4rem">${h}</li>`)}</ul>
          </details>
        </div>`}
      </div>
    </div>

    ${vorschau && html`<${PdfDialog} titel="Vorschau (Entwurf)" adresse=${vorschau} zu=${() => { URL.revokeObjectURL(vorschau); setVorschau(null); }} />`}

    ${fertigDialog && html`<${Dialog} titel="Rechnung fertigstellen" zu=${() => setFertigDialog(false)} aktionen=${html`
        <button onClick=${() => setFertigDialog(false)}>Abbrechen</button>
        <button class="haupt" disabled=${beschaeftigt} onClick=${stelleFertig}>${beschaeftigt ? 'Wird erstellt …' : 'Jetzt fertigstellen'}</button>`}>
      <p>Die Rechnung bekommt die Nummer <b>${r.nummer || (erg && erg.nummerVorschau)}</b> und wird als E-Rechnung im Ordner „Rechnungen“ abgelegt.</p>
      <p>Danach lässt sie sich <b>nicht mehr ändern</b> – so verlangt es das Gesetz. Ein Fehler wird später mit einer Rechnungskorrektur berichtigt.</p>
      ${z.warnung > 0 && html`<${Kasten} art="warnung"><b>Es gibt noch ${z.warnung === 1 ? 'eine Warnung' : z.warnung + ' Warnungen'}:</b>
        <ul>${erg.meldungen.filter((m) => m.schwere === 'warnung').map((m) => html`<li>${m.text}</li>`)}</ul>
        Die Rechnung kann trotzdem fertiggestellt werden. Der Empfänger könnte sie aber zurückschicken.<//>`}
    <//>`}
  `;
}

// --------------------------------------------------------------- Ansicht

function Ansicht({ anfang, einst }) {
  const [r, setR] = useState(anfang);
  const [aus, setAus] = useState(null);
  const [pdf, setPdf] = useState(false);
  const [senden, setSenden] = useState(false);
  useEffect(() => {
    api('POST', '/api/pruefen', anfang).then((e) => setAus(e.rechnung)).catch(() => {});
    if (sessionStorage.getItem('senden') === anfang.id) { sessionStorage.removeItem('senden'); setSenden(true); }
  }, []);
  const dateien = r.dateien || [];
  const haupt = Math.max(0, dateien.findIndex((d) => !d.endsWith('_Ansicht.pdf')));
  const ansicht = Math.max(0, dateien.findIndex((d) => d.endsWith('.pdf')));
  const name = (d) => d.split('/').pop();
  const gutschrift = r.art === '381';

  const setzeStatus = async (status, mehr = {}) => {
    try { setR(await api('POST', `/api/rechnungen/${r.id}/status`, { status, ...mehr })); } catch (e) { zeigeFehler(e); }
  };
  const storno = async () => {
    if (!confirm(`Rechnung ${r.nummer} stornieren?\n\nEs wird eine Rechnungskorrektur (Gutschrift) über den vollen Betrag als Entwurf angelegt. Erst wenn diese fertiggestellt ist, gilt die Rechnung als storniert.`)) return;
    try { const n = await api('POST', `/api/rechnungen/${r.id}/storno`); gehe('#/rechnung/' + n.id); } catch (e) { zeigeFehler(e); }
  };
  const kopie = async () => {
    try { const n = await api('POST', `/api/rechnungen/${r.id}/kopie`); zeige('Kopie als neuer Entwurf angelegt.'); gehe('#/rechnung/' + n.id); } catch (e) { zeigeFehler(e); }
  };
  const s = r.summen || {};

  return html`
    <div class="kopf">
      <div><h1>${belegName(r.art)} ${r.nummer}</h1>
        <div><${Etikett} status=${r.status} /> <span class="leise">${STATUS[r.status]} · ${r.kaeufer.name}</span></div></div>
      <div class="knoepfe">
        <button onClick=${() => gehe('#/')}>Zur Übersicht</button>
        <button onClick=${() => setPdf(true)}>Rechnung ansehen</button>
        ${r.status !== 'storniert' && html`<button class="haupt" onClick=${() => setSenden(true)}>${r.versand && r.versand.length ? 'Erneut senden …' : 'Per E-Mail senden …'}</button>`}
      </div>
    </div>

    ${r.status === 'fertig' && html`<${Kasten} art="warnung">Diese Rechnung ist fertiggestellt, aber <b>noch nicht versendet</b>.<//>`}
    ${r.status === 'storniert' && html`<${Kasten} art="fehler">Diese Rechnung wurde storniert.
      ${r.stornoDurch && html` <a href=${'#/rechnung/' + r.stornoDurch}>Zur Rechnungskorrektur</a>`}<//>`}

    <div class="spalten">
      <div>
        <div class="karte">
          <h2>Inhalt</h2>
          <table>
            <thead><tr><th>Pos.</th><th>Bezeichnung</th><th class="zahl">Menge</th><th class="zahl">Einzelpreis</th><th class="zahl">USt</th><th class="zahl">Netto</th></tr></thead>
            <tbody>${((aus && aus.positionen) || r.positionen).map((p, i) => html`<tr key=${i}>
              <td>${p.nr || i + 1}</td>
              <td>${p.bezeichnung}<br /><span class="leise klein">${[p.artikelnummer && 'Art.-Nr. ' + p.artikelnummer, p.gtin && 'GTIN ' + p.gtin].filter(Boolean).join(' · ')}</span></td>
              <td class="zahl">${zahlDE(p.menge, 0, 3)} ${(stamm.einheiten.find((e) => e.code === p.einheit) || {}).kurz || p.einheit}</td>
              <td class="zahl">${zahlDE(p.einzelpreis, 2, 4)}</td>
              <td class="zahl">${p.steuerKat === 'S' ? zahlDE(p.steuerSatz) + ' %' : '–'}</td>
              <td class="zahl">${aus ? geld(p.netto) : ''}</td>
            </tr>`)}</tbody>
          </table>
          <div class="summen" style="margin-top:.8rem">
            <div><span>Netto</span><span>${geld(s.netto)}</span></div>
            ${(s.steuern || []).filter((x) => x.kat === 'S').map((x) => html`<div><span>USt ${zahlDE(x.satz)} %</span><span>${geld(x.betrag)}</span></div>`)}
            <div class="gesamt"><span>${gutschrift ? 'Gutschrift' : 'Rechnungsbetrag'}</span><span>${geld(s.brutto)}</span></div>
          </div>
        </div>

        <div class="karte">
          <h2>Versand</h2>
          ${(r.versand || []).length === 0 ? html`<p class="leise">Noch nicht versendet.</p>` : html`<table>
            <thead><tr><th>Wann</th><th>An</th><th>Weg</th></tr></thead>
            <tbody>${r.versand.map((v, i) => html`<tr key=${i}><td class="nowrap">${datumDE(v.zeit)} ${v.zeit.slice(11, 16)}</td>
              <td>${v.an || '–'}${v.dateien ? html`<br /><span class="leise klein">${v.dateien}</span>` : null}</td>
              <td>${v.weg === 'email' ? 'E-Mail aus dem Programm' : 'von Hand vermerkt'}</td></tr>`)}</tbody></table>`}
          ${r.status === 'fertig' && html`<p style="margin-top:.8rem"><button onClick=${() => {
            const an = prompt('Die Rechnung wurde außerhalb des Programms verschickt oder in ein Portal hochgeladen.\n\nAn wen bzw. wohin? (Angabe für die eigenen Unterlagen)', '');
            if (an !== null) setzeStatus('versendet', { an });
          }}>Als versendet vermerken</button></p>`}
        </div>
      </div>

      <div>
        <div class="karte">
          <h2>Angaben</h2>
          <dl class="daten klein">
            <dt>Rechnungsdatum</dt><dd>${datumDE(r.datum)}</dd>
            ${r.leistungsdatum && html`<dt>Lieferdatum</dt><dd>${datumDE(r.leistungsdatum)}</dd>`}
            <dt>Fällig</dt><dd>${datumDE(r.faellig) || '–'}</dd>
            <dt>Kunde</dt><dd>${r.kaeufer.name}${r.kaeufer.gln ? html`<br />GLN ${r.kaeufer.gln}` : null}</dd>
            ${r.lieferort && html`<dt>Markt</dt><dd>${r.lieferort.name}${r.lieferort.gln ? html`<br />GLN ${r.lieferort.gln}` : null}</dd>`}
            ${r.bestellnummer && html`<dt>Bestellnummer</dt><dd>${r.bestellnummer}</dd>`}
            ${r.lieferscheinnummer && html`<dt>Lieferschein</dt><dd>${r.lieferscheinnummer}</dd>`}
            ${r.kostenstelle && html`<dt>Kostenstelle</dt><dd>${r.kostenstelle}</dd>`}
            ${(r.vorgaenger || []).map((v) => html`<dt>Korrigiert</dt><dd>${v.nummer}${v.datum ? ' vom ' + datumDE(v.datum) : ''}</dd>`)}
            <dt>Format</dt><dd>${(stamm.formate.find((f) => f.id === r.format) || {}).name || r.format}</dd>
            ${r.bezahltAm && html`<dt>Bezahlt am</dt><dd>${datumDE(r.bezahltAm)}</dd>`}
          </dl>
        </div>
        <div class="karte">
          <h2>Dateien</h2>
          ${dateien.map((d, i) => html`<p class="klein" key=${i}><a href=${`/api/rechnungen/${r.id}/datei/${i}`} target="_blank" rel="noopener">${name(d)}</a>
            ${' '}<button class="schlicht" onClick=${() => ladeDatei(`/api/rechnungen/${r.id}/datei/${i}?laden=1`)}>Speichern unter …</button>
            ${i === haupt ? html`<br /><span class="leise">Das ist die E-Rechnung, die verschickt wird.</span>` : html`<br /><span class="leise">Ansicht für die eigenen Unterlagen.</span>`}</p>`)}
          <button onClick=${() => api('POST', '/api/ordner', { ziel: 'rechnungen' }).catch(zeigeFehler)}>Ordner „Rechnungen“ öffnen</button>
        </div>
        <div class="karte">
          <h2>Weitere Schritte</h2>
          <div style="display:flex;flex-direction:column;gap:.5rem;align-items:flex-start">
            ${(r.status === 'fertig' || r.status === 'versendet') && !gutschrift && html`<button onClick=${() => setzeStatus('bezahlt')}>Als bezahlt vermerken</button>`}
            ${r.status === 'bezahlt' && html`<button onClick=${() => setzeStatus('offen')}>Zahlung zurücknehmen</button>`}
            <button onClick=${kopie}>Als neue Rechnung kopieren</button>
            ${r.status !== 'storniert' && !gutschrift && html`<button class="gefahr" onClick=${storno}>Stornieren (Rechnungskorrektur) …</button>`}
          </div>
        </div>
      </div>
    </div>

    ${pdf && dateien.length > 0 && html`<${PdfDialog} titel=${belegName(r.art) + ' ' + r.nummer} adresse=${`/api/rechnungen/${r.id}/datei/${ansicht}`} zu=${() => setPdf(false)} />`}
    ${senden && html`<${SendeDialog} r=${r} zu=${() => setSenden(false)} fertig=${(neu) => { setR(neu); setSenden(false); }} />`}
  `;
}

function SendeDialog({ r, zu, fertig }) {
  const [v, setV] = useState(null);
  const [laeuft, setLaeuft] = useState(false);
  useEffect(() => { api('GET', `/api/rechnungen/${r.id}/versand`).then(setV).catch((e) => { zeigeFehler(e); zu(); }); }, []);
  if (!v) return html`<${Dialog} titel="Rechnung senden" zu=${zu}><p class="laden">Wird vorbereitet …</p><//>`;
  const sende = async () => {
    setLaeuft(true);
    try {
      const neu = await api('POST', `/api/rechnungen/${r.id}/senden`, { an: v.an, kopie: v.kopie || '', betreff: v.betreff, text: v.text });
      zeige('Die Rechnung wurde verschickt.', 'gut');
      fertig(neu);
    } catch (e) { zeigeFehler(e); setLaeuft(false); }
  };
  const feld = (n) => (w) => setV({ ...v, [n]: w });
  return html`<${Dialog} titel=${'Rechnung ' + r.nummer + ' per E-Mail senden'} zu=${laeuft ? null : zu} aktionen=${html`
      <button disabled=${laeuft} onClick=${zu}>Abbrechen</button>
      <button class="haupt" disabled=${laeuft || !v.eingerichtet} onClick=${sende}>${laeuft ? 'Wird gesendet …' : 'Jetzt senden'}</button>`}>
    ${!v.eingerichtet && html`<${Kasten} art="warnung">Der E-Mail-Versand ist noch nicht eingerichtet.${' '}
      <a href="#/einstellungen">Jetzt unter Einstellungen einrichten</a>. Bis dahin lässt sich die Datei über „Speichern unter …“ sichern und von Hand verschicken.<//>`}
    ${(v.hinweise || []).map((h) => html`<${Kasten} art="hinweis">${h}<//>`)}
    <div class="raster">
      <${Feld} titel="An (Rechnungseingang des Kunden)" breite="12" hilfe="Mehrere Adressen mit Komma trennen. Die Adresse wird beim Kunden hinterlegt.">
        <${Text} wert=${v.an} setze=${feld('an')} placeholder="rechnung@kunde.example" /><//>
      <${Feld} titel="Betreff" breite="12"><${Text} wert=${v.betreff} setze=${feld('betreff')} /><//>
      <${Feld} titel="Text" breite="12" hilfe="Der Empfänger verarbeitet nur den Anhang. Rechnungsangaben gehören nicht in den Text.">
        <${Mehrzeilig} zeilen="6" wert=${v.text} setze=${feld('text')} /><//>
    </div>
    <p class="klein" style="margin-top:.8rem"><b>Anhang:</b> ${(v.dateien || []).join(', ') || '–'}<br />
      <span class="leise">Absender: ${v.absender || '–'}${v.kopieAnMich ? ' · eine Blindkopie geht an die eigene Adresse' : ''}</span></p>
  <//>`;
}
