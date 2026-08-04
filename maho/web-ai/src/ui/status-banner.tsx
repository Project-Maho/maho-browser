import { type ComponentChildren } from 'preact';
import { clsx } from '../utils/clsx';

export type BannerKind = 'info' | 'success' | 'warning' | 'error';

interface StatusBannerProps {
  kind: BannerKind;
  children: ComponentChildren;
  class?: string;
}

const kindClasses: Record<BannerKind, string> = {
  info: 'ui-banner--info',
  success: 'ui-banner--success',
  warning: 'ui-banner--warning',
  error: 'ui-banner--error',
};

export function StatusBanner({ kind, children, class: className }: StatusBannerProps) {
  return (
    <div
      role={kind === 'error' ? 'alert' : 'status'}
      class={clsx('ui-banner', kindClasses[kind], className)}
    >
      {children}
    </div>
  );
}
