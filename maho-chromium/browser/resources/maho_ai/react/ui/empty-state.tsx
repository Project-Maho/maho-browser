import {cn} from '../lib/cn.js';

export function EmptyState(
    {className, description, title}: {className?: string; description: string; title: string}) {
  return (
    <section className={cn('empty-panel', className)}>
      <h2>{title}</h2>
      <p>{description}</p>
    </section>
  );
}
