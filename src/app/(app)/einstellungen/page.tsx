import type { Metadata } from "next";
import { redirect } from "next/navigation";

import { PageHeader } from "@/components/layout/page-header";
import { EinstellungenFormular } from "@/components/portal/einstellungen-formular";
import { db } from "@/server/db";
import { getActor } from "@/server/policy";

export const metadata: Metadata = { title: "Einstellungen" };

export default async function EinstellungenPage() {
  const actor = await getActor();
  if (!actor) redirect("/anmelden");

  const [nutzer, standorte] = await Promise.all([
    db.user.findUniqueOrThrow({
      where: { id: actor.id },
      select: {
        name: true,
        email: true,
        phone: true,
        siteId: true,
        jobTitle: true,
        passwordHash: true,
      },
    }),
    db.site.findMany({
      where: { isActive: true },
      orderBy: [{ sortOrder: "asc" }, { name: "asc" }],
      select: { id: true, name: true },
    }),
  ]);

  return (
    <>
      <PageHeader
        title="Einstellungen"
        description="Ihr Profil und die Darstellung. Die Barrierefreiheitseinstellungen gelten an jedem Rechner, an dem Sie sich anmelden – auch am Vertretungsplatz."
      />

      <div className="px-4 py-5 sm:px-6">
        <EinstellungenFormular
          nutzer={{
            name: nutzer.name,
            email: nutzer.email,
            phone: nutzer.phone,
            siteId: nutzer.siteId,
            jobTitle: nutzer.jobTitle,
            hatPasswort: Boolean(nutzer.passwordHash),
          }}
          standorte={standorte}
          theme={actor.theme}
          a11yPrefs={actor.a11yPrefs}
        />
      </div>
    </>
  );
}
