import * as React from "react";
import { cva, type VariantProps } from "class-variance-authority";

import { cn } from "@/lib/utils";

/**
 * Kennzeichnung für Status, Priorität und Ähnliches.
 *
 * Farbe ist hier nie der alleinige Informationsträger (WCAG 1.4.1) – jede
 * Verwendung im System setzt zusätzlich ein Symbol und einen lesbaren Text.
 */
const badgeVariants = cva(
  "inline-flex items-center gap-1.5 rounded-md border px-2 py-0.5 " +
    "text-xs font-medium whitespace-nowrap [&_svg]:size-3.5 [&_svg]:shrink-0",
  {
    variants: {
      tone: {
        neutral: "border-line bg-surface-2 text-fg-muted",
        primary: "border-transparent bg-primary-soft text-primary-soft-fg",
        success: "border-transparent bg-success-soft text-success-soft-fg",
        warning: "border-transparent bg-warning-soft text-warning-soft-fg",
        danger: "border-transparent bg-danger-soft text-danger-soft-fg",
        info: "border-transparent bg-info-soft text-info-soft-fg",
        outline: "border-line-strong bg-transparent text-fg-muted",
      },
      size: {
        sm: "px-1.5 py-0 text-[0.6875rem]",
        md: "",
        lg: "px-2.5 py-1 text-sm [&_svg]:size-4",
      },
    },
    defaultVariants: { tone: "neutral", size: "md" },
  },
);

export interface BadgeProps
  extends React.HTMLAttributes<HTMLSpanElement>,
    VariantProps<typeof badgeVariants> {}

export function Badge({ className, tone, size, ...props }: BadgeProps) {
  return (
    <span className={cn(badgeVariants({ tone, size }), className)} {...props} />
  );
}
