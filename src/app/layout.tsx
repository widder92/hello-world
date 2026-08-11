import type { Metadata, Viewport } from "next";
import { Inter } from "next/font/google";

import { getActor } from "@/server/policy";
import { cn } from "@/lib/utils";

import "./globals.css";

/**
 * Die Schrift wird von Next.js beim Bauen heruntergeladen und selbst
 * ausgeliefert. Zur Laufzeit geht damit keine Anfrage an einen fremden Server –
 * das wäre bei einem internen System weder datenschutzrechtlich sauber noch
 * zuverlässig, wenn der Internetzugang einmal klemmt.
 */
const inter = Inter({
  subsets: ["latin", "latin-ext"],
  display: "swap",
  variable: "--font-inter",
});

export const metadata: Metadata = {
  title: {
    default: "Ticketsystem",
    template: "%s · Ticketsystem",
  },
  description:
    "Meldungen und Anfragen an IT, Hausmeister und weitere Bereiche – " +
    "an allen Standorten.",
  robots: { index: false, follow: false },
};

export const viewport: Viewport = {
  width: "device-width",
  initialScale: 1,
  // Kein maximumScale: Das Zoomen zu unterbinden verstößt gegen WCAG 1.4.4
  // und trifft genau die Personen, die es am nötigsten haben.
  themeColor: [
    { media: "(prefers-color-scheme: light)", color: "#ffffff" },
    { media: "(prefers-color-scheme: dark)", color: "#1c1f24" },
  ],
};

/**
 * Setzt das dunkle Thema, bevor der Browser zeichnet.
 *
 * Ohne dieses Skript blitzt bei „Systemeinstellung folgen" für einen
 * Sekundenbruchteil die helle Fassung auf. Für Menschen mit Lichtempfindlichkeit
 * ist das kein Schönheitsfehler, sondern unangenehm.
 */
const themeScript = `
(function () {
  try {
    var el = document.documentElement;
    if (el.dataset.themeMode === "system") {
      var dark = window.matchMedia("(prefers-color-scheme: dark)").matches;
      el.classList.toggle("dark", dark);
    }
  } catch (e) {}
})();
`;

export default async function RootLayout({
  children,
}: {
  children: React.ReactNode;
}) {
  const actor = await getActor();
  const prefs = actor?.a11yPrefs ?? {};
  const theme = actor?.theme ?? "system";

  return (
    <html
      lang="de"
      data-theme-mode={theme}
      className={cn(
        inter.variable,
        theme === "dark" && "dark",
        prefs.largeText && "a11y-large-text",
        prefs.highContrast && "a11y-high-contrast",
        prefs.reduceMotion && "a11y-reduce-motion",
      )}
      suppressHydrationWarning
    >
      <head>
        <script dangerouslySetInnerHTML={{ __html: themeScript }} />
      </head>
      <body className="min-h-dvh antialiased">
        <a href="#hauptinhalt" className="skip-link">
          Zum Hauptinhalt springen
        </a>
        {children}
      </body>
    </html>
  );
}
