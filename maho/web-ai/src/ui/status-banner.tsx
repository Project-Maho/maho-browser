import { type ComponentChildren } from 'preact';
import { clsx } from '../utils/clsx';

export type BannerKind = 'info' | 'success' | 'warning' | 'error';

interface StatusBannerProps {
  kind: BannerKind;
  children: ComponentChildren;
  class?: string;
}

const kindClasses: Record<BannerKind, string> = {
  info: 'bg-[var(--color-info-subtle)] text-[var(--color-info)]',
  success: 'bg-[var(--color-success-subtle)] text-[var(--color-success)]',
  warning: 'bg-[var(--color-warning-subtle)] text-[var(--color-warning)]',
  error: 'bg-[var(--color-error-subtle)] text-[var(--color-error)]',
};

export function StatusBanner({ kind, children, class: className }: StatusBannerProps) {
  return (
    <div
      role="status"
      class={clsx('rounded-xl px-4 py-3 text-sm', kindClasses[kind], className)}
    >
      {children}
    </div>
  );
}
