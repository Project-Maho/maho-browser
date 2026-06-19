import { clsx } from '../utils/clsx';

// Explicit props — avoids Signalish<...> conflicts from JSX.HTMLAttributes.
// autocapitalize is narrowed to Preact's expected union to satisfy the spread onto <input>.
export interface InputProps {
  label?: string;
  hint?: string;
  error?: string;
  id?: string;
  type?: string;
  value?: string;
  placeholder?: string;
  autocomplete?: string;
  autocorrect?: string;
  autocapitalize?: 'none' | 'off' | 'on' | 'sentences' | 'words' | 'characters';
  spellcheck?: boolean;
  disabled?: boolean;
  class?: string;
  onInput?: (e: Event) => void;
  onChange?: (e: Event) => void;
}

export function Input({ label, hint, error, id, class: className, ...rest }: InputProps) {
  const inputId = id ?? label?.toLowerCase().replace(/\s+/g, '-');

  return (
    <div class="flex flex-col gap-1">
      {label && (
        <label for={inputId} class="text-sm font-medium text-[var(--color-on-surface)]">
          {label}
        </label>
      )}
      <input
        id={inputId}
        class={clsx(
          'w-full rounded-xl border border-[var(--color-border)] bg-[var(--color-surface-raised)]',
          'px-3 py-2 text-sm text-[var(--color-on-surface)] placeholder:text-[var(--color-on-surface-tertiary)]',
          'focus:outline-none focus:ring-2 focus:ring-[var(--color-accent)] focus:border-transparent',
          'disabled:opacity-40',
          error && 'border-[var(--color-error)] focus:ring-[var(--color-error)]',
          className,
        )}
        {...rest}
      />
      {error && <p class="text-xs text-[var(--color-error)]">{error}</p>}
      {!error && hint && <p class="text-xs text-[var(--color-on-surface-secondary)]">{hint}</p>}
    </div>
  );
}
