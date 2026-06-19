import { type ComponentChildren } from 'preact';
import { clsx } from '../utils/clsx';

interface ListItemProps {
  leading?: ComponentChildren;
  trailing?: ComponentChildren;
  headline: ComponentChildren;
  supporting?: ComponentChildren;
  onClick?: () => void;
  class?: string;
}

export function ListItem({ leading, trailing, headline, supporting, onClick, class: className }: ListItemProps) {
  const Tag = onClick ? 'button' : 'div';
  return (
    <Tag
      class={clsx(
        'flex w-full items-center gap-3 px-4 py-3 text-left',
        onClick && 'hover:bg-[var(--color-surface-hover)] active:bg-[var(--color-surface-active)] transition-colors',
        className,
      )}
      onClick={onClick}
    >
      {leading && (
        <span class="shrink-0 flex items-center justify-center w-6 h-6 text-[var(--color-on-surface-secondary)]">
          {leading}
        </span>
      )}
      <span class="flex-1 min-w-0">
        <span class="block text-sm font-medium text-[var(--color-on-surface)]">{headline}</span>
        {supporting && (
          <span class="block text-xs text-[var(--color-on-surface-secondary)] mt-0.5">
            {supporting}
          </span>
        )}
      </span>
      {trailing && (
        <span class="shrink-0 flex items-center gap-1 text-[var(--color-on-surface-secondary)]">
          {trailing}
        </span>
      )}
    </Tag>
  );
}
