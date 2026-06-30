import * as React from "react";
import { cn } from "@lib/utils";

export function EmptyState(
    {className, description, title}: {className?: string; description: string; title: string}) {
  return (
    <section className={cn('flex flex-col items-center justify-center text-center p-8 max-w-sm mx-auto my-auto gap-2', className)}>
      <h2 className="text-lg font-semibold tracking-tight">{title}</h2>
      <p className="text-sm text-muted-foreground">{description}</p>
    </section>
  );
}
