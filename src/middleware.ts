import { NextResponse, type NextRequest } from "next/server";

/**
 * Erste Schranke vor jeder Seite.
 *
 * Prüft nur, ob überhaupt eine Sitzung besteht, und leitet sonst zur Anmeldung
 * um – mit dem ursprünglichen Ziel im Parameter, damit man nach dem Anmelden
 * dort landet, wo man hinwollte.
 *
 * Die eigentliche Rechteprüfung passiert bewusst nicht hier, sondern in
 * `policy.ts` auf dem Server: Middleware läuft in einer eingeschränkten
 * Laufzeitumgebung ohne Datenbankzugriff und könnte Mitgliedschaften und
 * Eingangs-Freigaben gar nicht auswerten.
 */
const OEFFENTLICH = [
  "/anmelden",
  "/api/auth",
  "/_next",
  "/favicon.ico",
  "/robots.txt",
];

export function middleware(request: NextRequest) {
  const { pathname } = request.nextUrl;

  if (OEFFENTLICH.some((pfad) => pathname.startsWith(pfad))) {
    return NextResponse.next();
  }

  // Auth.js legt das Sitzungs-Cookie je nach Protokoll unter zwei Namen ab.
  const hatSitzung =
    request.cookies.has("authjs.session-token") ||
    request.cookies.has("__Secure-authjs.session-token");

  if (!hatSitzung) {
    const ziel = new URL("/anmelden", request.url);
    if (pathname !== "/") {
      ziel.searchParams.set("weiter", pathname + request.nextUrl.search);
    }
    return NextResponse.redirect(ziel);
  }

  return NextResponse.next();
}

export const config = {
  matcher: [
    /*
     * Alle Pfade außer statischen Dateien. Bilder und Schriften brauchen die
     * Prüfung nicht und würden sie nur verlangsamen.
     */
    "/((?!_next/static|_next/image|favicon.ico|.*\\.(?:svg|png|jpg|jpeg|gif|webp|woff2)$).*)",
  ],
};
