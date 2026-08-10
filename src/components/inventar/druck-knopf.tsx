"use client";

import { Printer } from "lucide-react";

import { Button } from "@/components/ui/button";

/**
 * Druckknopf für das Übergabeprotokoll.
 *
 * Eigene Datei, damit die Protokollseite selbst eine Server-Komponente bleiben
 * kann – sie besteht sonst nur aus Daten und braucht kein JavaScript.
 */
export function DruckKnopf() {
  return (
    <Button size="sm" onClick={() => window.print()}>
      <Printer aria-hidden="true" />
      Drucken
    </Button>
  );
}
