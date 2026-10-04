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
  primary: 'ui-btn--primary',
  secondary: 'ui-btn--secondary',
  ghost: 'ui-btn--ghost',
  destructive: 'ui-btn--destructive',
};

const sizeClasses: Record<ButtonSize, string> = {
  sm: 'ui-btn--sm',
  md: 'ui-btn--md',
  lg: 'ui-btn--lg',
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
        'ui-btn',
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
      class="animate-spin ui-btn__spinner"
      aria-hidden="true"
    >
      <path d="M12 2v4M12 18v4M4.93 4.93l2.83 2.83M16.24 16.24l2.83 2.83M2 12h4M18 12h4M4.93 19.07l2.83-2.83M16.24 7.76l2.83-2.83" />
    </svg>
  );
}
