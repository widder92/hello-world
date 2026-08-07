import { Priority, TicketStatus } from "@prisma/client";

import { Badge } from "@/components/ui/badge";
import {
  PRIORITY,
  SLA,
  STATUS,
  STATUS_EINFACH,
  slaState,
  type SlaState,
} from "@/lib/domain";
import { formatRelative } from "@/lib/format";
import { cn } from "@/lib/utils";

/**
 * Anzeigen für Status, Priorität und Frist.
 *
 * Alle drei tragen Symbol und Text – Farbe kommt hinzu, ist aber nie der
 * einzige Träger der Bedeutung. Wer die Oberfläche mit hohem Kontrast oder
 * einer Farbsehschwäche nutzt, erfasst sie genauso schnell.
 */

export function StatusBadge({
  status,
  einfach = false,
  size = "md",
  className,
}: {
  status: TicketStatus;
  /** In der schlanken Ausbaustufe stehen weniger Fachbegriffe. */
  einfach?: boolean;
  size?: "sm" | "md" | "lg";
  className?: string;
}) {
  const d = STATUS[status];
  const Icon = d.icon;
  const label = einfach ? (STATUS_EINFACH[status] ?? d.label) : d.label;

  return (
    <Badge tone={d.tone} size={size} className={className}>
      <Icon aria-hidden="true" />
      {label}
    </Badge>
  );
}

export function PriorityBadge({
  priority,
  size = "md",
  className,
}: {
  priority: Priority;
  size?: "sm" | "md" | "lg";
  className?: string;
}) {
  const d = PRIORITY[priority];
  const Icon = d.icon;

  return (
    <Badge tone={d.tone} size={size} className={className}>
      <Icon aria-hidden="true" />
      {d.label}
    </Badge>
  );
}

/**
 * Fristenampel.
 *
 * Zeigt nicht nur den Zustand, sondern auch, wann die Frist abläuft – „in
 * 2 Stunden" ist beim Verteilen der Arbeit brauchbarer als ein gelber Punkt.
 */
export function SlaBadge({
  due,
  start,
  erledigt,
  warningThreshold,
  size = "md",
  showTime = true,
  className,
}: {
  due: Date | string | null | undefined;
  start: Date | string;
  erledigt?: boolean;
  warningThreshold?: number;
  size?: "sm" | "md" | "lg";
  showTime?: boolean;
  className?: string;
}) {
  const dueDate = due ? new Date(due) : null;
  const state: SlaState = slaState({
    due: dueDate,
    start: new Date(start),
    warningThreshold,
    erledigt,
  });

  if (state === "keine") return null;

  const d = SLA[state];
  const Icon = d.icon;

  return (
    <Badge tone={d.tone} size={size} className={className}>
      <Icon aria-hidden="true" />
      {showTime && dueDate ? (
        <>
          {state === "ueberschritten" ? "Überfällig " : "Fällig "}
          <time dateTime={dueDate.toISOString()}>{formatRelative(dueDate)}</time>
        </>
      ) : (
        d.short ?? d.label
      )}
    </Badge>
  );
}

/**
 * Farbiger Punkt für den Bereich – für enge Listen, in denen der Name nicht
 * mehr hinpasst. Der Bereichsname steht dann im `title` und als versteckter
 * Text für Screenreader.
 */
export function TeamDot({
  color,
  name,
  className,
}: {
  color: string;
  name: string;
  className?: string;
}) {
  return (
    <span className={cn("inline-flex items-center gap-1.5", className)}>
      <span
        className="size-2 shrink-0 rounded-full"
        style={{ backgroundColor: color }}
        aria-hidden="true"
      />
      <span className="sr-only">Bereich: </span>
      {name}
    </span>
  );
}
