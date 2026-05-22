export function PendingThought({label}: {label: string}) {
  return (
    <article className="compact-thread__row compact-thread__row--system compact-thread__row--thinking">
      <div className="compact-thread__pending">{label}</div>
    </article>
  );
}
