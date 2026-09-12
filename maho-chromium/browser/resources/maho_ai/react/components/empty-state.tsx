import * as React from "react";
import { cn } from "@lib/utils";

export function EmptyState(
    {className, description, title}: {className?: string; description: string; title: string}) {
  return (
    <section className={cn('flex min-w-0 flex-col items-center gap-1.5 px-2 text-center', className)}>
      <h2 className="text-balance text-[15px] font-semibold leading-[1.35] tracking-[-0.014em] text-foreground">
        {title}
      </h2>
      <p className="text-balance text-[12px] leading-[1.5] tracking-[-0.003em] text-muted-foreground">
        {description}
      </p>
    </section>
  );
}
