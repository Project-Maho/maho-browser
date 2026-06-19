import { type ComponentChildren } from 'preact';
import { clsx } from '../utils/clsx';

export type ButtonVariant = 'primary' | 'secondary' | 'ghost' | 'destructive';
export type ButtonSize = 'sm' | 'md' | 'lg';

// Explicit props rather than extending JSX.HTMLAttributes, which in Preact
// types `class` as Signalish<string | undefined> — incompatible with clsx.
export interface ButtonProps {
  variant?: ButtonVariant;
  size?: ButtonSize;
  loading?: boolean;
  disabled?: boolean;
  class?: string;
  type?: 'button' | 'submit' | 'reset';
  onClick?: (e: MouseEvent) => void;
  'aria-label'?: string;
  'aria-hidden'?: boolean | 'true' | 'false';
  children: ComponentChildren;
}

const variantClasses: Record<ButtonVariant, string> = {
  primary:
    'bg-[var(--color-accent)] text-[var(--color-on-accent)] hover:opacity-90 active:opacity-80',
  secondary:
    'bg-[var(--color-surface-raised)] text-[var(--color-on-surface)] border border-[var(--color-border)] hover:bg-[var(--color-surface-hover)]',
  ghost:
    'bg-transparent text-[var(--color-on-surface-secondary)] hover:bg-[var(--color-surface-hover)]',
  destructive:
    'bg-transparent text-[var(--color-error)] hover:bg-[var(--color-error-subtle)] border border-[var(--color-error)]',
};

const sizeClasses: Record<ButtonSize, string> = {
  sm: 'px-3 py-1.5 text-sm rounded-lg',
  md: 'px-4 py-2 text-sm rounded-xl',
  lg: 'px-5 py-2.5 text-base rounded-xl',
};

export function Button({
  variant = 'secondary',
  size = 'md',
  loading = false,
  disabled = false,
  type = 'button',
  class: className,
  onClick,
  children,
  ...rest
}: ButtonProps) {
  return (
    <button
      type={type}
      class={clsx(
        'inline-flex items-center justify-center gap-2 font-medium transition-all',
        'disabled:opacity-40 disabled:pointer-events-none',
        variantClasses[variant],
        sizeClasses[size],
        className,
      )}
      disabled={disabled || loading}
      onClick={onClick}
      {...rest}
    >
      {loading && <Spinner size={size === 'lg' ? 18 : 14} />}
      {children}
    </button>
  );
}

function Spinner({ size }: { size: number }) {
  return (
    <svg
      width={size}
      height={size}
      viewBox="0 0 24 24"
      fill="none"
      stroke="currentColor"
      stroke-width="2"
      class="animate-spin"
      aria-hidden="true"
    >
      <path d="M12 2v4M12 18v4M4.93 4.93l2.83 2.83M16.24 16.24l2.83 2.83M2 12h4M18 12h4M4.93 19.07l2.83-2.83M16.24 7.76l2.83-2.83" />
    </svg>
  );
}
