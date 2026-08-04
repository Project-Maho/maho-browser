import {Button} from '@ui/button';
import {cn} from '@lib/utils';
import type {CredentialErrorPresentation} from '../../views/credential_error.js';

export function CredentialErrorNotice(
    {
      onOpenSettings,
      presentation,
      surface,
    }: {
      readonly onOpenSettings: (paneKey: string) => void;
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
      <div>
        <Button
            aria-label={presentation.actionLabel}
            size="sm"
            variant="outline"
            onClick={() => onOpenSettings(presentation.settingsPane)}>
          {presentation.actionLabel}
        </Button>
      </div>
    </div>
  );
}
