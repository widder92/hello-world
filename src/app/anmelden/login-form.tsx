"use client";

import { useState } from "react";
import { signIn } from "next-auth/react";
import { AlertCircle } from "lucide-react";

import { Button } from "@/components/ui/button";
import { Field, Input } from "@/components/ui/input";

/**
 * Anmeldeformular.
 *
 * Zeigt nur die Wege an, die tatsächlich eingerichtet sind: Ohne hinterlegte
 * Entra-Zugangsdaten erscheint die SSO-Schaltfläche gar nicht erst, statt bei
 * einem Klick in einen Fehler zu laufen.
 */
export function LoginForm({
  ssoEnabled,
  localEnabled,
  error,
  weiter,
}: {
  ssoEnabled: boolean;
  localEnabled: boolean;
  error?: string;
  weiter?: string;
}) {
  const ziel = weiter && weiter.startsWith("/") ? weiter : "/";

  const [email, setEmail] = useState("");
  const [passwort, setPasswort] = useState("");
  const [laeuft, setLaeuft] = useState<"lokal" | "sso" | null>(null);
  const [fehler, setFehler] = useState<string | null>(
    error ? uebersetzeFehler(error) : null,
  );

  async function anmelden(e: React.FormEvent) {
    e.preventDefault();
    setFehler(null);
    setLaeuft("lokal");

    const ergebnis = await signIn("credentials", {
      email,
      password: passwort,
      redirect: false,
    });

    if (ergebnis?.error) {
      // Bewusst dieselbe Meldung für „Konto gibt es nicht" und „Passwort
      // falsch": sonst ließe sich über die Anmeldemaske herausfinden, welche
      // Adressen im Haus existieren.
      setFehler("E-Mail-Adresse oder Passwort stimmen nicht.");
      setLaeuft(null);
      return;
    }

    window.location.href = ziel;
  }

  return (
    <div className="mt-8 space-y-6">
      {fehler && (
        <div
          role="alert"
          className="flex items-start gap-2.5 rounded-md border border-danger/30 bg-danger-soft px-3.5 py-3 text-sm text-danger-soft-fg"
        >
          <AlertCircle className="mt-0.5 size-4 shrink-0" aria-hidden="true" />
          <span>{fehler}</span>
        </div>
      )}

      {ssoEnabled && (
        <Button
          type="button"
          variant="secondary"
          size="lg"
          className="w-full"
          loading={laeuft === "sso"}
          onClick={() => {
            setLaeuft("sso");
            void signIn("microsoft-entra-id", { redirectTo: ziel });
          }}
        >
          <MicrosoftLogo />
          Mit dem Dienstkonto anmelden
        </Button>
      )}

      {ssoEnabled && localEnabled && (
        <div className="flex items-center gap-3">
          <span className="h-px flex-1 bg-line" />
          <span className="text-xs font-medium uppercase tracking-wide text-fg-subtle">
            oder
          </span>
          <span className="h-px flex-1 bg-line" />
        </div>
      )}

      {localEnabled && (
        <form onSubmit={anmelden} className="space-y-4">
          <Field label="E-Mail-Adresse" htmlFor="email" required>
            <Input
              type="email"
              value={email}
              onChange={(e) => setEmail(e.target.value)}
              autoComplete="username"
              autoFocus={!ssoEnabled}
              placeholder="vorname.nachname@example.org"
            />
          </Field>

          <Field label="Passwort" htmlFor="passwort" required>
            <Input
              type="password"
              value={passwort}
              onChange={(e) => setPasswort(e.target.value)}
              autoComplete="current-password"
            />
          </Field>

          <Button
            type="submit"
            size="lg"
            className="w-full"
            loading={laeuft === "lokal"}
          >
            Anmelden
          </Button>
        </form>
      )}

      {!ssoEnabled && !localEnabled && (
        <p className="rounded-md border border-line bg-surface-2 p-4 text-sm text-fg-muted">
          Es ist kein Anmeldeverfahren eingerichtet. Bitte wenden Sie sich an die
          Administration.
        </p>
      )}

      <p className="text-center text-xs text-fg-subtle">
        Passwort vergessen? Die IT setzt es für Sie zurück.
      </p>
    </div>
  );
}

function uebersetzeFehler(code: string): string {
  switch (code) {
    case "AccessDenied":
      return (
        "Für diese Adresse ist kein Konto im Ticketsystem hinterlegt. " +
        "Wenden Sie sich bitte an die IT, damit Sie freigeschaltet werden."
      );
    case "CredentialsSignin":
      return "E-Mail-Adresse oder Passwort stimmen nicht.";
    case "OAuthAccountNotLinked":
      return "Diese Adresse ist bereits mit einem anderen Anmeldeweg verknüpft.";
    case "Configuration":
      return "Die Anmeldung ist nicht richtig eingerichtet. Bitte die IT informieren.";
    default:
      return "Die Anmeldung ist fehlgeschlagen. Bitte versuchen Sie es erneut.";
  }
}

function MicrosoftLogo() {
  return (
    <svg viewBox="0 0 21 21" className="size-4" aria-hidden="true">
      <rect x="1" y="1" width="9" height="9" fill="#f25022" />
      <rect x="11" y="1" width="9" height="9" fill="#7fba00" />
      <rect x="1" y="11" width="9" height="9" fill="#00a4ef" />
      <rect x="11" y="11" width="9" height="9" fill="#ffb900" />
    </svg>
  );
}
