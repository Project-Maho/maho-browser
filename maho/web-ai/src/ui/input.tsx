import type { JSX } from 'preact';
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
  onInput?: (event: JSX.TargetedInputEvent<HTMLInputElement>) => void;
  onChange?: (event: JSX.TargetedEvent<HTMLInputElement, Event>) => void;
  onBlur?: (event: JSX.TargetedFocusEvent<HTMLInputElement>) => void;
}

export function Input({ label, hint, error, id, class: className, ...rest }: InputProps) {
  const inputId = id ?? label?.toLowerCase().replace(/\s+/g, '-');

  return (
    <div class="ui-field">
      {label && (
        <label for={inputId} class="ui-field__label">
          {label}
        </label>
      )}
      <input
        id={inputId}
        class={clsx('ui-field__input', error && 'ui-field__input--error', className)}
        {...rest}
      />
      {error && <p class="ui-field__error">{error}</p>}
      {!error && hint && <p class="ui-field__hint">{hint}</p>}
    </div>
  );
}
