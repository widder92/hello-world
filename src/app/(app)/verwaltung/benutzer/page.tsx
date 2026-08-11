import type { Metadata } from "next";
import { GlobalRole } from "@prisma/client";
import { Mail, Search, ShieldCheck, UserX } from "lucide-react";

import { PageHeader } from "@/components/layout/page-header";
import { BenutzerDialog } from "@/components/verwaltung/benutzer-dialog";
import { MitgliedschaftDialog } from "@/components/verwaltung/mitgliedschaft-dialog";
import { Avatar } from "@/components/ui/avatar";
import { Badge } from "@/components/ui/badge";
import { Card } from "@/components/ui/card";
import { Input } from "@/components/ui/input";
import { db } from "@/server/db";
import { requireAdmin } from "@/server/policy";

export const metadata: Metadata = { title: "Benutzer" };

/**
 * Benutzerverwaltung.
 *
 * Hier wird gesteuert, wer wo arbeitet – die eigentliche Antwort auf die
 * Frage, welche Mitarbeitenden den vollen Arbeitsplatz brauchen und welche
 * nur melden. Wer in keinem Bereich Mitglied ist, sieht ausschließlich das
 * Portal; das ist der Normalfall für die große Mehrheit im Haus.
 */
export default async function BenutzerPage({
  searchParams,
}: {
  searchParams: Promise<{ q?: string; nur?: string }>;
}) {
  await requireAdmin();

  const sp = await searchParams;
  const suche = sp.q?.trim() ?? "";
  const nurMitarbeitende = sp.nur === "bearbeitende";

  const [benutzer, bereiche, standorte, anzahl] = await Promise.all([
    db.user.findMany({
      where: {
        ...(suche
          ? {
              OR: [
                { name: { contains: suche, mode: "insensitive" } },
                { email: { contains: suche, mode: "insensitive" } },
                { department: { contains: suche, mode: "insensitive" } },
              ],
            }
          : {}),
        ...(nurMitarbeitende ? { memberships: { some: {} } } : {}),
      },
      orderBy: [{ isActive: "desc" }, { name: "asc" }],
      take: 200,
      include: {
        site: { select: { name: true, shortName: true } },
        memberships: {
          include: {
            team: { select: { id: true, name: true, color: true, mode: true } },
          },
        },
        queueAccess: {
          include: {
            queue: {
              select: {
                id: true,
                name: true,
                teamId: true,
                site: { select: { name: true, shortName: true } },
              },
            },
          },
        },
      },
    }),
    db.team.findMany({
      where: { isActive: true },
      orderBy: [{ sortOrder: "asc" }, { name: "asc" }],
      select: {
        id: true,
        name: true,
        color: true,
        mode: true,
        queues: {
          where: { isActive: true },
          orderBy: { sortOrder: "asc" },
          select: {
            id: true,
            name: true,
            site: { select: { name: true, shortName: true } },
          },
        },
      },
    }),
    db.site.findMany({
      where: { isActive: true },
      orderBy: [{ sortOrder: "asc" }, { name: "asc" }],
      select: { id: true, name: true },
    }),
    db.user.count(),
  ]);

  return (
    <>
      <PageHeader
        title="Benutzer"
        description={
          <>
            {anzahl} Konten. Die <strong className="font-medium text-fg">Mitgliedschaft</strong> in
            einem Bereich entscheidet, wer dort arbeiten darf; die{" "}
            <strong className="font-medium text-fg">Eingangsfreigabe</strong>,
            welche Standorte diese Person sieht. Ohne Mitgliedschaft bleibt nur
            das Melder-Portal.
          </>
        }
        actions={<BenutzerDialog standorte={standorte} />}
      >
        <form className="flex flex-wrap items-center gap-2">
          <div className="relative min-w-56 flex-1">
            <label htmlFor="benutzer-suche" className="sr-only">
              Benutzer suchen
            </label>
            <Search
              className="pointer-events-none absolute left-2.5 top-1/2 size-4 -translate-y-1/2 text-fg-subtle"
              aria-hidden="true"
            />
            <Input
              id="benutzer-suche"
              name="q"
              defaultValue={suche}
              placeholder="Name, E-Mail oder Abteilung"
              className="h-9 pl-8.5"
            />
          </div>
          <label className="flex items-center gap-2 text-sm text-fg-muted">
            <input
              type="checkbox"
              name="nur"
              value="bearbeitende"
              defaultChecked={nurMitarbeitende}
              className="size-4 rounded border-line-strong"
            />
            Nur Bearbeitende
          </label>
        </form>
      </PageHeader>

      <div className="space-y-2 px-4 py-5 sm:px-6">
        {benutzer.length === 0 && (
          <Card>
            <p className="px-6 py-12 text-center text-sm text-fg-muted">
              Keine Konten gefunden.
            </p>
          </Card>
        )}

        {benutzer.map((u) => (
          <Card key={u.id} className={u.isActive ? undefined : "opacity-60"}>
            <div className="flex flex-wrap items-start gap-3 px-5 py-3.5">
              <Avatar name={u.name} size="md" />

              <div className="min-w-0 flex-1">
                <div className="flex flex-wrap items-center gap-2">
                  <span className="font-medium text-fg">{u.name}</span>

                  {u.globalRole === GlobalRole.ADMIN && (
                    <Badge tone="primary">
                      <ShieldCheck aria-hidden="true" />
                      Administration
                    </Badge>
                  )}
                  {u.isExternal && (
                    <Badge
                      tone="neutral"
                      title="Aus einer eingehenden Mail entstanden – kann sich nicht anmelden."
                    >
                      <Mail aria-hidden="true" />
                      Externer Melder
                    </Badge>
                  )}
                  {!u.isActive && (
                    <Badge tone="danger">
                      <UserX aria-hidden="true" />
                      Gesperrt
                    </Badge>
                  )}
                  {!u.passwordHash && !u.isExternal && (
                    <Badge
                      tone="outline"
                      title="Kein lokales Passwort – Anmeldung nur über Entra ID."
                    >
                      Nur SSO
                    </Badge>
                  )}
                </div>

                <p className="mt-0.5 flex flex-wrap items-center gap-x-2 text-sm text-fg-muted">
                  <span>{u.email}</span>
                  {u.jobTitle && (
                    <>
                      <span aria-hidden="true">·</span>
                      <span>{u.jobTitle}</span>
                    </>
                  )}
                  {u.site && (
                    <>
                      <span aria-hidden="true">·</span>
                      <span>{u.site.shortName ?? u.site.name}</span>
                    </>
                  )}
                </p>

                {u.memberships.length > 0 ? (
                  <ul className="mt-2 flex flex-wrap gap-1.5">
                    {u.memberships.map((m) => {
                      const eingaenge = u.queueAccess.filter(
                        (a) => a.queue.teamId === m.team.id,
                      );
                      return (
                        <li
                          key={m.id}
                          className="rounded-md border border-line bg-surface-2 px-2 py-1 text-xs"
                        >
                          <span className="inline-flex items-center gap-1.5">
                            <span
                              className="size-2 rounded-full"
                              style={{ backgroundColor: m.team.color }}
                              aria-hidden="true"
                            />
                            <span className="font-medium text-fg">
                              {m.team.name}
                            </span>
                            <span className="text-fg-subtle">{m.role}</span>
                          </span>
                          {eingaenge.length > 0 && (
                            <span className="ml-1.5 text-fg-subtle">
                              ·{" "}
                              {eingaenge
                                .map(
                                  (a) =>
                                    a.queue.site?.shortName ??
                                    a.queue.site?.name ??
                                    a.queue.name,
                                )
                                .join(", ")}
                            </span>
                          )}
                          {m.role === "LEAD" && (
                            <span className="ml-1.5 text-fg-subtle">
                              · sieht alle Eingänge
                            </span>
                          )}
                        </li>
                      );
                    })}
                  </ul>
                ) : (
                  <p className="mt-1.5 text-xs text-fg-subtle">
                    In keinem Bereich – sieht nur das Melder-Portal.
                  </p>
                )}
              </div>

              <div className="flex shrink-0 flex-wrap gap-2">
                {!u.isExternal && (
                  <MitgliedschaftDialog
                    benutzer={{
                      id: u.id,
                      name: u.name,
                      memberships: u.memberships.map((m) => ({
                        teamId: m.team.id,
                        role: m.role,
                      })),
                      queueAccess: u.queueAccess.map((a) => ({
                        queueId: a.queue.id,
                        isDefault: a.isDefault,
                      })),
                    }}
                    bereiche={bereiche}
                  />
                )}
                <BenutzerDialog
                  standorte={standorte}
                  benutzer={{
                    id: u.id,
                    email: u.email,
                    name: u.name,
                    globalRole: u.globalRole,
                    siteId: u.siteId,
                    phone: u.phone,
                    jobTitle: u.jobTitle,
                    department: u.department,
                    isActive: u.isActive,
                  }}
                />
              </div>
            </div>
          </Card>
        ))}
      </div>
    </>
  );
}
