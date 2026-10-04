import type { ComponentChildren } from 'preact';
import { clsx } from '../utils/clsx';

interface IconButtonProps {
  label: string;
  testId?: string;
  disabled?: boolean;
  class?: string;
  onClick: () => void;
  children: ComponentChildren;
}

export function IconButton({
  label,
  testId,
  disabled = false,
  class: className,
  onClick,
  children,
}: IconButtonProps) {
  return (
    <button
      type="button"
      class={clsx('agent-icon-button', className)}
      aria-label={label}
      data-testid={testId}
      disabled={disabled}
      onClick={onClick}
    >
      {children}
    </button>
  );
}
