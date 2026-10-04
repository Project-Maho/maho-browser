import {Button} from '@ui/button';
import {RefreshCw} from '@icons/lucide';
import {cn} from '@lib/utils';
import type {CredentialErrorPresentation} from '../../views/credential_error.js';

// A typed credential failure renders mapped UI copy only: its raw diagnostic
// can carry the endpoint, model, and key markers, so it is never displayed.
export function CredentialErrorNotice(
    {
      onOpenSettings,
      onRetry,
      presentation,
      surface,
    }: {
      readonly onOpenSettings: (paneKey: string) => void;
      readonly onRetry?: () => void;
      readonly presentation: CredentialErrorPresentation;
      readonly surface: 'compact'|'developer';
    }) {
  return (
    <div
        className={cn(
            'grid min-w-0 gap-3',
            surface === 'compact' &&
              'border-l border-destructive/60 pl-4')}
        role="alert">
      <div className="grid min-w-0 gap-1">
        <div className="text-sm font-semibold leading-5 text-foreground">
          {presentation.title}
        </div>
        <div className="text-sm leading-6 text-muted-foreground">
          {presentation.description}
        </div>
      </div>
      <div className="flex flex-wrap items-center gap-2">
        {onRetry ? (
          <Button
              aria-label="Retry prompt"
              className="gap-1.5"
              data-action="retry"
              size="sm"
              variant="outline"
              onClick={onRetry}>
            <RefreshCw className="size-3.5" />
            Retry
          </Button>
        ) : null}
        <Button
            aria-label={presentation.actionLabel}
            size="sm"
            variant="ghost"
            className="text-muted-foreground hover:text-foreground"
            onClick={() => onOpenSettings(presentation.settingsPane)}>
          {presentation.actionLabel}
        </Button>
      </div>
    </div>
  );
}
