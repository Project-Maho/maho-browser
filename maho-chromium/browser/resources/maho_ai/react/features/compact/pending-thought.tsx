export function PendingThought({label}: {label: string}) {
  return (
    <article className="w-full motion-safe:animate-pulse motion-reduce:animate-none">
      <div className="px-1 text-sm leading-6 text-muted-foreground">{label}</div>
    </article>
  );
}
