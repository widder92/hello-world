import type { NextConfig } from "next";

const nextConfig: NextConfig = {
  output: "standalone",
  experimental: {
    serverActions: {
      // Fotos vom Handy können groß sein – Anhänge bis 25 MB zulassen.
      bodySizeLimit: "25mb",
    },
  },
  eslint: {
    ignoreDuringBuilds: true,
  },
  async redirects() {
    return [
      {
        // Die frühere Geräteliste lag in der Verwaltung und war nur lesbar.
        // Ihr Inhalt steckt jetzt unter /inventar.
        //
        // Die Weiterleitung gehört hierher und nicht in eine Seite: Der
        // Rahmen unter /verwaltung lässt nur die Administration durch und
        // würde alle anderen vorher auf die Startseite schicken – ein
        // Lesezeichen aus der IT liefe damit ins Leere.
        source: "/verwaltung/geraete",
        destination: "/inventar",
        permanent: true,
      },
    ];
  },
};

export default nextConfig;
