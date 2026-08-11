/**
 * Gemeinsame Schnittstelle für Postfach-Anbindungen.
 *
 * Zwei Umsetzungen: IMAP (funktioniert überall) und Microsoft Graph (für
 * Microsoft 365, ohne SMTP-Freischaltung). Der Abrufdienst kennt nur diese
 * Schnittstelle – ein späterer Wechsel des Postfachanbieters berührt die
 * Ticketlogik nicht.
 */

export interface EingehenderAnhang {
  filename: string;
  mimeType: string;
  inhalt: Buffer;
  /** Eingebettete Bilder aus HTML-Mails (Signaturlogos u. ä.). */
  inline: boolean;
}

export interface EingehendeNachricht {
  /** Kennung im Quellsystem: IMAP-UID oder Graph-Message-Id. */
  quellId: string;
  /** Message-ID aus den Kopfzeilen – der stabilste Anker für die Zuordnung. */
  messageId: string | null;
  inReplyTo: string | null;
  references: string[];

  betreff: string;
  vonAdresse: string;
  vonName: string | null;
  anAdressen: string[];
  ccAdressen: string[];

  text: string | null;
  html: string | null;

  empfangenAm: Date;

  /** Kopfzeilen zur Erkennung automatischer Antworten. */
  autoSubmitted: string | null;
  precedence: string | null;

  anhaenge: EingehenderAnhang[];
}

export interface MailSource {
  /** Holt unverarbeitete Nachrichten aus dem Postfach. */
  holeNeue(limit?: number): Promise<EingehendeNachricht[]>;

  /**
   * Markiert eine Nachricht als erledigt – als gelesen und, falls ein
   * Zielordner angegeben ist, dorthin verschoben.
   */
  markiereVerarbeitet(quellId: string, zielOrdner?: string): Promise<void>;

  /** Prüft die Verbindung, für die Schaltfläche „Verbindung testen". */
  pruefeVerbindung(): Promise<void>;
}
