"use client";

import * as React from "react";
import * as AvatarPrimitive from "@radix-ui/react-avatar";

import { cn, initials } from "@/lib/utils";

/**
 * Personenkürzel. Die Hintergrundfarbe wird aus dem Namen abgeleitet, damit
 * dieselbe Person immer gleich aussieht – hilft beim schnellen Wiedererkennen
 * in langen Ticketlisten.
 */
const TONES = [
  "bg-[oklch(0.90_0.05_20)] text-[oklch(0.35_0.12_20)]",
  "bg-[oklch(0.90_0.05_70)] text-[oklch(0.35_0.12_70)]",
  "bg-[oklch(0.90_0.05_150)] text-[oklch(0.34_0.12_150)]",
  "bg-[oklch(0.90_0.05_210)] text-[oklch(0.34_0.12_210)]",
  "bg-[oklch(0.90_0.05_270)] text-[oklch(0.35_0.12_270)]",
  "bg-[oklch(0.90_0.05_320)] text-[oklch(0.35_0.12_320)]",
];

function toneFor(name: string): string {
  let sum = 0;
  for (let i = 0; i < name.length; i++) sum = (sum + name.charCodeAt(i)) % 997;
  return TONES[sum % TONES.length];
}

export function Avatar({
  name,
  src,
  size = "md",
  className,
}: {
  name: string;
  src?: string | null;
  size?: "xs" | "sm" | "md" | "lg";
  className?: string;
}) {
  const sizes = {
    xs: "size-5 text-[0.625rem]",
    sm: "size-7 text-xs",
    md: "size-9 text-sm",
    lg: "size-12 text-base",
  };

  return (
    <AvatarPrimitive.Root
      className={cn(
        "relative flex shrink-0 overflow-hidden rounded-full",
        sizes[size],
        className,
      )}
    >
      {src && (
        <AvatarPrimitive.Image
          src={src}
          alt=""
          className="aspect-square size-full object-cover"
        />
      )}
      <AvatarPrimitive.Fallback
        // Der Name steht im Umfeld immer im Klartext daneben; das Kürzel
        // wäre für Screenreader nur Lärm.
        aria-hidden="true"
        className={cn(
          "flex size-full items-center justify-center font-semibold",
          toneFor(name),
        )}
      >
        {initials(name)}
      </AvatarPrimitive.Fallback>
    </AvatarPrimitive.Root>
  );
}
