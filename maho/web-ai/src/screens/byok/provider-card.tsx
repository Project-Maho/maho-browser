import { Button } from '../../ui/button';
import { Icon } from '../../ui/icon';
import { clsx } from '../../utils/clsx';

export type ValidationPhase = 'idle' | 'checking' | 'valid' | 'invalid';

export interface ValidationStatus {
  phase: ValidationPhase;
  message?: string;
}

interface ProviderCardProps {
  displayName: string;
  maskedKey: string;
  validationStatus: ValidationStatus;
  onValidate: () => void;
  onDelete: () => void;
}

export function ProviderCard({
  displayName,
  maskedKey,
  validationStatus,
  onValidate,
  onDelete,
}: ProviderCardProps) {
  const isChecking = validationStatus.phase === 'checking';

  return (
    <div class="flex items-center gap-3 px-4 py-3">
      {/* Leading validation icon */}
      <span class="shrink-0 flex items-center justify-center w-6 h-6">
        <ValidationIcon status={validationStatus} />
      </span>

      {/* Text block */}
      <span class="flex-1 min-w-0">
        <span class="block text-sm font-medium text-[var(--color-on-surface)]">{displayName}</span>
        <span class="block font-mono text-xs text-[var(--color-on-surface-secondary)] mt-0.5 truncate">
          {maskedKey}
        </span>
        {validationStatus.message && (
          <span
            class={clsx(
              'block text-xs mt-1',
              validationStatus.phase === 'valid' && 'text-[var(--color-success)]',
              validationStatus.phase === 'invalid' && 'text-[var(--color-error)]',
              (validationStatus.phase === 'idle' || validationStatus.phase === 'checking') &&
                'text-[var(--color-on-surface-secondary)]',
            )}
          >
            {validationStatus.message}
          </span>
        )}
      </span>

      {/* Actions */}
      <span class="shrink-0 flex items-center gap-1">
        <Button
          variant="ghost"
          size="sm"
          onClick={onValidate}
          disabled={isChecking}
          aria-label={`Validate ${displayName} key`}
        >
          Validate
        </Button>
        <button
          class="p-2 rounded-lg text-[var(--color-error)] hover:bg-[var(--color-error-subtle)] disabled:opacity-40 disabled:pointer-events-none transition-colors"
          onClick={onDelete}
          disabled={isChecking}
          aria-label={`Delete ${displayName} key`}
        >
          <Icon name="trash-2" size={16} aria-hidden />
        </button>
      </span>
    </div>
  );
}

function ValidationIcon({ status }: { status: ValidationStatus }) {
  switch (status.phase) {
    case 'checking':
      return (
        <svg
          width={16}
          height={16}
          viewBox="0 0 24 24"
          fill="none"
          stroke="currentColor"
          stroke-width="2"
          stroke-linecap="round"
          stroke-linejoin="round"
          class="animate-spin text-[var(--color-on-surface-secondary)]"
          aria-label="Checking"
        >
          <path d="M12 2v4M12 18v4M4.93 4.93l2.83 2.83M16.24 16.24l2.83 2.83M2 12h4M18 12h4M4.93 19.07l2.83-2.83M16.24 7.76l2.83-2.83" />
        </svg>
      );
    case 'valid':
      return (
        <Icon
          name="circle-check"
          size={20}
          class="text-[var(--color-success)]"
          aria-label="Valid"
        />
      );
    case 'invalid':
      return (
        <Icon
          name="triangle-alert"
          size={20}
          class="text-[var(--color-error)]"
          aria-label="Invalid"
        />
      );
    default:
      return (
        <Icon
          name="key-round"
          size={20}
          class="text-[var(--color-on-surface-secondary)]"
          aria-hidden
        />
      );
  }
}
