// Einstellungen: eigene Firma, Bank, Nummernkreis, Logo, E-Mail-Versand, Daten.
import {
  html, useState, useEffect, api, stamm, zeige, zeigeFehler, Feld, Text, Mehrzeilig, Auswahl, Haken, Kasten, glnOk, setzeIn, ladeDatei,
} from './basis.js';

// Zugangsdaten gängiger Postfächer; der Rest steht beim jeweiligen Anbieter.
const ANBIETER = [
  ['', '– Anbieter wählen –'],
  ['gmx', 'GMX', 'mail.gmx.net', 587, 'starttls', 'In GMX unter E-Mail → Einstellungen → POP3/IMAP Abruf den Zugriff über „POP3 und IMAP“ erlauben.'],
  ['webde', 'WEB.DE', 'smtp.web.de', 587, 'starttls', 'In WEB.DE unter Einstellungen → POP3/IMAP Abruf den Zugriff erlauben.'],
  ['tonline', 'T-Online / Telekom', 'securesmtp.t-online.de', 465, 'tls', 'Im Kundencenter der Telekom ein „Passwort für E-Mail-Programme“ einrichten und hier verwenden.'],
  ['gmail', 'Gmail (Google)', 'smtp.gmail.com', 465, 'tls', 'Google verlangt ein „App-Passwort“ (Google-Konto → Sicherheit → Bestätigung in zwei Schritten → App-Passwörter). Das normale Passwort funktioniert nicht.'],
  ['ionos', 'IONOS (1&1)', 'smtp.ionos.de', 465, 'tls', ''],
  ['strato', 'Strato', 'smtp.strato.de', 465, 'tls', ''],
  ['icloud', 'iCloud (Apple)', 'smtp.mail.me.com', 587, 'starttls', 'Apple verlangt ein „anwendungsspezifisches Passwort“ (account.apple.com → Anmeldung und Sicherheit).'],
  ['outlook', 'Outlook.com / Hotmail', 'smtp-mail.outlook.com', 587, 'starttls', 'Microsoft lässt die Anmeldung mit Passwort für private Outlook-Postfächer meist nicht mehr zu. Falls der Test scheitert, bitte ein anderes Postfach verwenden.'],
];

export function Einstellungsseite() {
  const [e, setE] = useState(null);
  const [passwort, setPasswort] = useState('');
  const [geaendert, setGeaendert] = useState(false);
  const [logoNr, setLogoNr] = useState(1);
  const [testet, setTestet] = useState('');
  const [anbieter, setAnbieter] = useState('');
  useEffect(() => { api('GET', '/api/einstellungen').then(setE).catch(zeigeFehler); }, []);
  if (!e) return html`<p class="laden">Einstellungen werden geladen …</p>`;

  const setze = (pfad) => (wert) => { setE((x) => setzeIn(x, pfad, wert)); setGeaendert(true); };
  const speichere = async () => {
    const neu = await api('PUT', '/api/einstellungen', { ...e, passwort, naechsteNummer: Number(e.naechsteNummer) || 1, zieltage: Number(e.zieltage) || 0,
      post: { ...e.post, port: Number(e.post.port) || 465 } });
    setE(neu); setPasswort(''); setGeaendert(false);
    await api('GET', '/api/stamm').then((s) => Object.assign(stamm, s));
    return neu;
  };
  const sichere = () => speichere().then(() => zeige('Einstellungen gespeichert.', 'gut')).catch(zeigeFehler);
  const teste = async (probemail) => {
    setTestet(probemail ? 'mail' : 'verbindung');
    try {
      await speichere();
      const a = await api('POST', '/api/post/test', { probemail });
      zeige(probemail ? `Probe-E-Mail an ${a.an} verschickt. Bitte im Postfach nachsehen.` : 'Verbindung und Anmeldung funktionieren.', 'gut');
    } catch (f) { zeigeFehler(f); } finally { setTestet(''); }
  };
  const ladeLogo = async (datei) => {
    if (!datei) return;
    try { await api('POST', '/api/logo', datei); setE((x) => ({ ...x, logoDatei: 'logo' })); setLogoNr((n) => n + 1); zeige('Logo gespeichert.', 'gut'); } catch (f) { zeigeFehler(f); }
  };
  const entferneLogo = async () => {
    try { await api('DELETE', '/api/logo'); setE((x) => ({ ...x, logoDatei: '' })); } catch (f) { zeigeFehler(f); }
  };
  const waehleAnbieter = (id) => {
    setAnbieter(id);
    const a = ANBIETER.find((x) => x[0] === id);
    if (a && a[2]) { setE((x) => ({ ...x, post: { ...x.post, server: a[2], port: a[3], sicherheit: a[4] } })); setGeaendert(true); }
  };
  const f = e.firma;
  const glnFalsch = f.gln && !/^\d{13}$/.test(f.gln) ? 'Eine GLN hat genau 13 Ziffern.' : f.gln && !glnOk(f.gln) ? 'Prüfziffer falsch – bitte auf Tippfehler prüfen.' : '';
  const jahr = new Date().getFullYear();
  const beispiel = (e.nummernmuster || '').replace('{JJJJ}', jahr).replace('{JJ}', String(jahr).slice(2)).replace('{MM}', String(new Date().getMonth() + 1).padStart(2, '0'))
    .replace(/\{(N+)\}/, (_, n) => String(e.naechsteNummer || 1).padStart(n.length, '0'));
  const anbieterHinweis = (ANBIETER.find((x) => x[0] === anbieter) || [])[5];

  return html`
    <div class="kopf">
      <div><h1>Einstellungen</h1></div>
      <div class="knoepfe">${geaendert && html`<span class="leise">ungespeicherte Änderungen</span>`}
        <button class="haupt" onClick=${sichere}>Speichern</button></div>
    </div>

    <div class="karte">
      <h2>Eigene Firma</h2>
      <p class="leise klein">Diese Angaben stehen auf jeder Rechnung und in der E-Rechnung. Das Gesetz verlangt Name, Anschrift und Steuernummer oder USt-IdNr.</p>
      <div class="raster">
        <${Feld} titel="Name der Firma bzw. Vor- und Nachname" breite="8"><${Text} feld="verkaeufer.name" wert=${f.name} setze=${setze('firma.name')} /><//>
        <${Feld} titel="Handelsname (falls abweichend)" breite="4"><${Text} wert=${f.handelsname} setze=${setze('firma.handelsname')} /><//>
        <${Feld} titel="Straße und Hausnummer" breite="6"><${Text} feld="verkaeufer.strasse" wert=${f.strasse} setze=${setze('firma.strasse')} /><//>
        <${Feld} titel="PLZ" breite="2"><${Text} wert=${f.plz} setze=${setze('firma.plz')} /><//>
        <${Feld} titel="Ort" breite="4"><${Text} wert=${f.ort} setze=${setze('firma.ort')} /><//>
        <${Feld} titel="USt-IdNr." breite="4"><${Text} feld="verkaeufer.ustId" wert=${f.ustId} setze=${setze('firma.ustId')} placeholder="DE123456789" /><//>
        <${Feld} titel="Steuernummer" breite="4"><${Text} wert=${f.steuernummer} setze=${setze('firma.steuernummer')} /><//>
        <${Feld} titel="Eigene GLN" breite="4" falsch=${glnFalsch} hilfe="vergibt GS1 Germany; EDEKA erwartet sie"><${Text} feld="verkaeufer.gln" wert=${f.gln} setze=${(v) => setze('firma.gln')(v.replace(/\s/g, ''))} inputmode="numeric" maxlength="13" /><//>
        <${Feld} titel="Ansprechpartner" breite="4"><${Text} feld="verkaeufer.ansprechpartner" wert=${f.ansprechpartner} setze=${setze('firma.ansprechpartner')} /><//>
        <${Feld} titel="Telefon" breite="4"><${Text} feld="verkaeufer.telefon" wert=${f.telefon} setze=${setze('firma.telefon')} /><//>
        <${Feld} titel="E-Mail" breite="4"><${Text} typ="email" feld="verkaeufer.email" wert=${f.email} setze=${(v) => { setze('firma.email')(v); setze('firma.eAdresse')(v); }} /><//>
        <${Feld} titel="Handelsregister (falls eingetragen)" breite="4"><${Text} wert=${f.register} setze=${setze('firma.register')} placeholder="z. B. Amtsgericht Köln, HRA 1234" /><//>
        <${Feld} titel="Inhaber / Geschäftsführung" breite="4" hilfe="für die Fußzeile"><${Text} wert=${e.geschaeftsleitung} setze=${setze('geschaeftsleitung')} /><//>
        <${Feld} titel="Webseite" breite="4"><${Text} wert=${e.webseite} setze=${setze('webseite')} /><//>
        <div class="feld b12"><${Haken} wert=${e.kleinunternehmer} setze=${setze('kleinunternehmer')}>Kleinunternehmer nach § 19 UStG – Rechnungen ohne Umsatzsteuer, mit dem vorgeschriebenen Hinweis<//></div>
      </div>
    </div>

    <div class="karte">
      <h2>Bankverbindung</h2>
      <div class="raster">
        <${Feld} titel="IBAN" breite="5"><${Text} feld="zahlung.iban" wert=${e.iban} setze=${setze('iban')} /><//>
        <${Feld} titel="BIC" breite="3"><${Text} feld="zahlung.bic" wert=${e.bic} setze=${setze('bic')} /><//>
        <${Feld} titel="Name der Bank" breite="4"><${Text} wert=${e.bank} setze=${setze('bank')} /><//>
        <${Feld} titel="Kontoinhaber (falls abweichend vom Firmennamen)" breite="6"><${Text} wert=${e.kontoinhaber} setze=${setze('kontoinhaber')} /><//>
      </div>
    </div>

    <div class="karte">
      <h2>Rechnungen</h2>
      <div class="raster">
        <${Feld} titel="Aufbau der Rechnungsnummer" breite="4" hilfe=${'Nächste Nummer: ' + beispiel}><${Text} wert=${e.nummernmuster} setze=${setze('nummernmuster')} /><//>
        <${Feld} titel="Nächste laufende Nummer" breite="3"><${Text} typ="number" min="1" wert=${String(e.naechsteNummer || 1)} setze=${setze('naechsteNummer')} /><//>
        <div class="feld b5" style="justify-content:center"><${Haken} wert=${e.jaehrlichNeu} setze=${setze('jaehrlichNeu')}>Jedes Jahr wieder bei 1 beginnen<//></div>
        <${Feld} titel="Übliches Zahlungsziel in Tagen" breite="3"><${Text} typ="number" min="0" wert=${String(e.zieltage || 0)} setze=${setze('zieltage')} /><//>
        <${Feld} titel="Üblicher Steuersatz" breite="3"><${Auswahl} wert=${String(Number(e.standardSteuer))} setze=${setze('standardSteuer')} optionen=${[['19', '19 % (Regelsatz, auch Getränke)'], ['7', '7 % (ermäßigt, viele Lebensmittel)'], ['0', '0 %']]} /><//>
        <${Feld} titel="Übliche Einheit" breite="3"><${Auswahl} wert=${e.standardEinheit} setze=${setze('standardEinheit')} optionen=${stamm.einheiten.map((x) => [x.code, x.name])} /><//>
        <${Feld} titel="Übliches Format" breite="3"><${Auswahl} wert=${e.format} setze=${setze('format')} optionen=${stamm.formate.map((x) => [x.id, x.name])} /><//>
        <${Feld} titel="Einleitungstext neuer Rechnungen" breite="6"><${Mehrzeilig} zeilen="2" wert=${e.einleitung} setze=${setze('einleitung')} /><//>
        <${Feld} titel="Schlusstext neuer Rechnungen" breite="6"><${Mehrzeilig} zeilen="2" wert=${e.schluss} setze=${setze('schluss')} /><//>
      </div>
      <p class="leise klein" style="margin-top:.7rem">Im Aufbau der Nummer stehen {JJJJ} für das Jahr, {MM} für den Monat und {NNNN} für die laufende Nummer mit vier Stellen.
        Wer weiter mit Lexware Office bucht, wählt hier am besten einen eigenen Nummernkreis (zum Beispiel „ER-{JJJJ}-{NNNN}“), damit sich die Nummern nicht überschneiden.</p>
      <h3>Logo</h3>
      <div style="display:flex;gap:1rem;align-items:center;flex-wrap:wrap">
        ${e.logoDatei ? html`<img class="logo-vorschau" src=${'/api/logo?n=' + logoNr} alt="Logo" />` : html`<span class="leise">Kein Logo hinterlegt.</span>`}
        <label class="knopf">Logo auswählen (PNG oder JPEG) …<input type="file" accept="image/png,image/jpeg" hidden onChange=${(ev) => { ladeLogo(ev.target.files[0]); ev.target.value = ''; }} /></label>
        ${e.logoDatei && html`<button class="schlicht" onClick=${entferneLogo}>Entfernen</button>`}
      </div>
    </div>

    <div class="karte">
      <h2>E-Mail-Versand</h2>
      <p class="leise klein">Die Rechnungen gehen über das eigene Postfach hinaus – wie aus einem E-Mail-Programm. Das Passwort wird geschützt auf diesem Rechner gespeichert, nicht im Datenordner.</p>
      <div class="raster">
        <${Feld} titel="E-Mail-Anbieter" breite="4" hilfe="füllt Server und Port aus"><${Auswahl} wert=${anbieter} setze=${waehleAnbieter} optionen=${ANBIETER.map((a) => [a[0], a[1]])} /><//>
        <${Feld} titel="Absenderadresse" breite="4" hilfe="bei REWE muss sie angemeldet sein"><${Text} typ="email" wert=${e.post.absender} setze=${setze('post.absender')} /><//>
        <${Feld} titel="Absendername" breite="4"><${Text} wert=${e.post.absenderName} setze=${setze('post.absenderName')} placeholder=${f.name} /><//>
        <${Feld} titel="Postausgangsserver (SMTP)" breite="4"><${Text} wert=${e.post.server} setze=${setze('post.server')} /><//>
        <${Feld} titel="Port" breite="2"><${Text} typ="number" wert=${String(e.post.port || '')} setze=${setze('post.port')} /><//>
        <${Feld} titel="Verschlüsselung" breite="2"><${Auswahl} wert=${e.post.sicherheit} setze=${setze('post.sicherheit')} optionen=${[['tls', 'SSL/TLS'], ['starttls', 'STARTTLS']]} /><//>
        <${Feld} titel="Benutzername" breite="4" hilfe="leer = Absenderadresse"><${Text} wert=${e.post.benutzer} setze=${setze('post.benutzer')} autocomplete="off" /><//>
        <${Feld} titel="Passwort" breite="4" hilfe=${e.post.passwortDa ? 'Ein Passwort ist gespeichert. Nur ausfüllen, um es zu ändern.' : 'noch keines gespeichert'}>
          <${Text} typ="password" wert=${passwort} setze=${(v) => { setPasswort(v); setGeaendert(true); }} autocomplete="new-password" /><//>
        <div class="feld b8" style="justify-content:center"><${Haken} wert=${e.post.kopieAnMich} setze=${setze('post.kopieAnMich')}>Von jeder verschickten Rechnung eine Blindkopie an die eigene Adresse senden (empfohlen als Nachweis)<//></div>
      </div>
      ${anbieterHinweis && html`<${Kasten} art="hinweis">${anbieterHinweis}<//>`}
      <p style="margin-top:.9rem;display:flex;gap:.5rem;flex-wrap:wrap">
        <button disabled=${!!testet} onClick=${() => teste(false)}>${testet === 'verbindung' ? 'Wird geprüft …' : 'Verbindung testen'}</button>
        <button disabled=${!!testet} onClick=${() => teste(true)}>${testet === 'mail' ? 'Wird gesendet …' : 'Probe-E-Mail an mich senden'}</button>
      </p>
    </div>

    <div class="karte">
      <h2>Daten und Sicherung</h2>
      <p>Alle Daten liegen als Dateien auf diesem Rechner:<br /><b>${stamm.datenordner}</b></p>
      <p class="leise klein">Im Unterordner „Rechnungen“ liegen die fertigen E-Rechnungen, nach Jahren sortiert. Rechnungen müssen acht Jahre lang unverändert aufbewahrt werden – diesen Ordner also regelmäßig sichern (externe Festplatte oder Cloud-Ordner).</p>
      <p style="display:flex;gap:.5rem;flex-wrap:wrap">
        <button onClick=${() => api('POST', '/api/ordner', { ziel: 'daten' }).catch(zeigeFehler)}>Datenordner öffnen</button>
        <button onClick=${() => ladeDatei('/api/sicherung.zip')}>Sicherung herunterladen (ZIP)</button>
        <button onClick=${() => ladeDatei('/api/export.csv?jahr=' + jahr)}>Rechnungsliste ${jahr} für die Buchhaltung (CSV)</button>
        <button onClick=${() => ladeDatei('/api/export.csv?jahr=' + (jahr - 1))}>Rechnungsliste ${jahr - 1}</button>
      </p>
    </div>`;
}
