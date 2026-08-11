import type { Metadata } from "next";
import { MailTransport } from "@prisma/client";
import { AlertTriangle, CheckCircle2, Inbox, Mail } from "lucide-react";

import { PageHeader } from "@/components/layout/page-header";
import { MailkontoDialog } from "@/components/verwaltung/mailkonto-dialog";
import { MailkontoAktionen } from "@/components/verwaltung/mailkonto-aktionen";
import { Badge } from "@/components/ui/badge";
import { Card } from "@/components/ui/card";
import { db } from "@/server/db";
import { requireAdmin } from "@/server/policy";
import { formatRelative } from "@/lib/format";

export const metadata: Metadata = { title: "Postfächer" };

/**
 * Postfächer je Eingang.
 *
 * Ein Postfach pro Eingang – so wird aus einer Mail an
 * hausmeister-wohnheim-nord@… unmittelbar ein Vorgang im richtigen
 * Standort-Eingang, genauso direkt wie über die Bereichswahl im Portal.
 */
export default async function MailkontenPage() {
  await requireAdmin();

  const [eingaenge, konten] = await Promise.all([
    db.queue.findMany({
      where: { isActive: true },
      orderBy: [{ team: { sortOrder: "asc" } }, { sortOrder: "asc" }],
      select: {
        id: true,
        name: true,
        inboundEmail: true,
        team: { select: { name: true, color: true } },
        site: { select: { name: true, shortName: true } },
        mailAccount: { select: { id: true } },
      },
    }),
    db.mailAccount.findMany({
      include: {
        queue: {
          select: {
            id: true,
            name: true,
            team: { select: { name: true, color: true } },
            site: { select: { name: true, shortName: true } },
          },
        },
      },
      orderBy: { createdAt: "asc" },
    }),
  ]);

  const ohneKonto = eingaenge.filter((q) => !q.mailAccount);

  return (
    <>
      <PageHeader
        title="Postfächer"
        description={
          <>
            Je Eingang ein Postfach. Der Hintergrunddienst holt neue Nachrichten
            zyklisch ab und macht Vorgänge daraus. Antworten auf
            Benachrichtigungen landen automatisch am selben Vorgang.
          </>
        }
        actions={
          <MailkontoDialog
            eingaenge={ohneKonto.map((q) => ({
              id: q.id,
              name: q.name,
              teamName: q.team.name,
              siteName: q.site?.shortName ?? q.site?.name ?? null,
              inboundEmail: q.inboundEmail,
            }))}
          />
        }
      />

      <div className="space-y-4 px-4 py-5 sm:px-6">
        {konten.length === 0 && (
          <Card>
            <div className="px-6 py-12 text-center">
              <Mail className="mx-auto size-8 text-fg-subtle" aria-hidden="true" />
              <p className="mt-2 text-sm font-medium text-fg">
                Noch kein Postfach eingerichtet
              </p>
              <p className="mx-auto mt-1 max-w-md text-sm text-fg-muted">
                Solange kein Postfach hinterlegt ist, entstehen Vorgänge nur
                über das Portal. Eingehende E-Mails werden nicht abgeholt.
              </p>
            </div>
          </Card>
        )}

        {konten.map((k) => {
          const imap = k.transport === MailTransport.IMAP;
          const hatFehler = Boolean(k.lastSyncError);

          return (
            <Card key={k.id} className={hatFehler ? "border-danger/40" : undefined}>
              <div className="flex flex-wrap items-start gap-4 px-5 py-4">
                <span
                  className="flex size-10 shrink-0 items-center justify-center rounded-lg"
                  style={{
                    backgroundColor: `${k.queue.team.color}1a`,
                    color: k.queue.team.color,
                  }}
                >
                  <Inbox className="size-5" aria-hidden="true" />
                </span>

                <div className="min-w-0 flex-1">
                  <div className="flex flex-wrap items-center gap-2">
                    <h2 className="font-semibold text-fg">
                      {k.queue.site
                        ? `${k.queue.team.name} – ${k.queue.site.shortName ?? k.queue.site.name}`
                        : k.queue.name}
                    </h2>
                    <Badge tone="outline">{imap ? "IMAP" : "Microsoft Graph"}</Badge>
                    {!k.isActive && <Badge tone="neutral">Abgeschaltet</Badge>}
                  </div>

                  <p className="mt-1 font-mono text-sm text-fg-muted">
                    {k.address}
                  </p>

                  <dl className="mt-2.5 grid gap-x-6 gap-y-1 text-xs sm:grid-cols-2">
                    {imap ? (
                      <>
                        <Zeile
                          label="Server"
                          wert={`${k.imapHost ?? "—"}:${k.imapPort ?? 993}${k.imapSecure ? " (TLS)" : ""}`}
                        />
                        <Zeile label="Benutzer" wert={k.imapUser ?? "—"} />
                        <Zeile label="Ordner" wert={k.imapFolder} />
                        <Zeile
                          label="Verarbeitet nach"
                          wert={k.imapProcessedFolder ?? "nur als gelesen markieren"}
                        />
                      </>
                    ) : (
                      <Zeile label="Postfach" wert={k.graphUserId ?? "—"} />
                    )}
                    <Zeile
                      label="Unbekannte Absender"
                      wert={
                        {
                          CREATE_CONTACT: "als externen Melder anlegen",
                          QUARANTINE: "zur Sichtung markieren",
                          REJECT: "verwerfen",
                        }[k.unknownSender]
                      }
                    />
                    <Zeile
                      label="Letzter Abruf"
                      wert={
                        k.lastSyncAt ? formatRelative(k.lastSyncAt) : "noch nie"
                      }
                    />
                  </dl>

                  {hatFehler ? (
                    <p className="mt-2.5 flex items-start gap-2 rounded-md bg-danger-soft px-3 py-2 text-xs text-danger-soft-fg">
                      <AlertTriangle
                        className="mt-0.5 size-3.5 shrink-0"
                        aria-hidden="true"
                      />
                      <span>
                        <strong className="font-semibold">
                          Letzter Abruf fehlgeschlagen:
                        </strong>{" "}
                        {k.lastSyncError}
                      </span>
                    </p>
                  ) : (
                    k.lastSyncAt && (
                      <p className="mt-2.5 flex items-center gap-1.5 text-xs text-success-soft-fg">
                        <CheckCircle2 className="size-3.5" aria-hidden="true" />
                        Abruf läuft.
                      </p>
                    )
                  )}
                </div>

                <div className="flex shrink-0 flex-col gap-2">
                  <MailkontoAktionen mailAccountId={k.id} />
                  <MailkontoDialog
                    eingaenge={[
                      {
                        id: k.queue.id,
                        name: k.queue.name,
                        teamName: k.queue.team.name,
                        siteName:
                          k.queue.site?.shortName ?? k.queue.site?.name ?? null,
                        inboundEmail: k.address,
                      },
                    ]}
                    konto={{
                      id: k.id,
                      queueId: k.queue.id,
                      transport: k.transport,
                      address: k.address,
                      isActive: k.isActive,
                      unknownSender: k.unknownSender,
                      imapHost: k.imapHost,
                      imapPort: k.imapPort,
                      imapSecure: k.imapSecure,
                      imapUser: k.imapUser,
                      imapFolder: k.imapFolder,
                      imapProcessedFolder: k.imapProcessedFolder,
                      graphUserId: k.graphUserId,
                    }}
                  />
                </div>
              </div>
            </Card>
          );
        })}

        {ohneKonto.length > 0 && (
          <Card>
            <div className="px-5 py-4">
              <h2 className="text-sm font-semibold text-fg">
                Eingänge ohne Postfach
              </h2>
              <p className="mt-0.5 text-xs text-fg-muted">
                Diese Eingänge nehmen derzeit nur Meldungen über das Portal
                entgegen.
              </p>
              <ul className="mt-2.5 flex flex-wrap gap-1.5">
                {ohneKonto.map((q) => (
                  <li
                    key={q.id}
                    className="rounded-md border border-line bg-surface-2 px-2 py-1 text-xs"
                  >
                    <span className="inline-flex items-center gap-1.5">
                      <span
                        className="size-2 rounded-full"
                        style={{ backgroundColor: q.team.color }}
                        aria-hidden="true"
                      />
                      <span className="text-fg">
                        {q.site
                          ? `${q.team.name} – ${q.site.shortName ?? q.site.name}`
                          : q.name}
                      </span>
                    </span>
                    {q.inboundEmail && (
                      <span className="ml-1.5 font-mono text-fg-subtle">
                        {q.inboundEmail}
                      </span>
                    )}
                  </li>
                ))}
              </ul>
            </div>
          </Card>
        )}
      </div>
    </>
  );
}

function Zeile({ label, wert }: { label: string; wert: string }) {
  return (
    <div className="flex gap-2">
      <dt className="shrink-0 text-fg-subtle">{label}:</dt>
      <dd className="min-w-0 truncate text-fg-muted">{wert}</dd>
    </div>
  );
}
