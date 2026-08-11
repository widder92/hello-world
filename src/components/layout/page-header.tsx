import { cn } from "@/lib/utils";

/**
 * Kopfbereich einer Seite: Überschrift, erklärender Satz und Aktionen.
 *
 * Die Überschrift ist immer ein <h1> – auf jeder Seite genau eine, damit
 * Screenreader-Nutzende beim Sprung zur Überschrift verlässlich dort landen,
 * wo die Seite anfängt.
 */
export function PageHeader({
  title,
  description,
  actions,
  children,
  className,
}: {
  title: string;
  description?: React.ReactNode;
  actions?: React.ReactNode;
  children?: React.ReactNode;
  className?: string;
}) {
  return (
    <div
      className={cn(
        "border-b border-line bg-surface px-4 py-5 sm:px-6",
        className,
      )}
    >
      <div className="flex flex-wrap items-start justify-between gap-3">
        <div className="min-w-0">
          <h1 className="text-xl font-semibold tracking-tight text-fg">
            {title}
          </h1>
          {description && (
            <div className="mt-1 text-sm text-fg-muted">{description}</div>
          )}
        </div>
        {actions && (
          <div className="flex shrink-0 flex-wrap items-center gap-2">
            {actions}
          </div>
        )}
      </div>
      {children && <div className="mt-4">{children}</div>}
    </div>
  );
}

/** Einheitlicher Innenabstand für den Seiteninhalt. */
export function PageBody({
  className,
  ...props
}: React.HTMLAttributes<HTMLDivElement>) {
  return <div className={cn("px-4 py-5 sm:px-6", className)} {...props} />;
}
