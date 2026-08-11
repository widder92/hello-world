import type { Metadata } from "next";
import { redirect } from "next/navigation";
import { LifeBuoy } from "lucide-react";

import { LoginForm } from "@/app/anmelden/login-form";
import { auth, localLoginEnabled, ssoEnabled } from "@/server/auth";

export const metadata: Metadata = { title: "Anmelden" };

export default async function AnmeldenPage({
  searchParams,
}: {
  searchParams: Promise<{ error?: string; weiter?: string }>;
}) {
  const session = await auth();
  const params = await searchParams;

  if (session?.user) {
    redirect(params.weiter && params.weiter.startsWith("/") ? params.weiter : "/");
  }

  return (
    <main
      id="hauptinhalt"
      className="grid min-h-dvh lg:grid-cols-[1fr_minmax(28rem,36rem)]"
    >
      {/* Begrüßungsspalte – auf schmalen Geräten ausgeblendet, weil dort das
          Formular sofort erreichbar sein soll. */}
      <div className="relative hidden flex-col justify-between bg-primary p-12 text-primary-fg lg:flex">
        <div className="flex items-center gap-3">
          <LifeBuoy className="size-7" aria-hidden="true" />
          <span className="text-lg font-semibold">Ticketsystem</span>
        </div>

        <div className="max-w-md space-y-6">
          <h1 className="text-3xl font-semibold leading-tight text-balance">
            Ihre Meldung kommt direkt bei den richtigen Kolleginnen und Kollegen an.
          </h1>
          <p className="text-primary-fg/85 leading-relaxed">
            Sie wählen den Bereich und den Standort – wir kümmern uns um den Rest.
            Den Stand Ihres Vorgangs können Sie jederzeit nachlesen.
          </p>
        </div>

        <p className="text-sm text-primary-fg/70">
          Bei Fragen zur Anmeldung hilft die IT weiter.
        </p>
      </div>

      <div className="flex items-center justify-center px-4 py-12 sm:px-8">
        <div className="w-full max-w-sm">
          <div className="mb-8 flex items-center gap-3 lg:hidden">
            <LifeBuoy className="size-7 text-primary" aria-hidden="true" />
            <span className="text-lg font-semibold">Ticketsystem</span>
          </div>

          <h2 className="text-2xl font-semibold text-fg">Anmelden</h2>
          <p className="mt-1.5 text-sm text-fg-muted">
            Melden Sie sich mit Ihrem Dienstkonto an.
          </p>

          <LoginForm
            ssoEnabled={ssoEnabled}
            localEnabled={localLoginEnabled}
            error={params.error}
            weiter={params.weiter}
          />
        </div>
      </div>
    </main>
  );
}
