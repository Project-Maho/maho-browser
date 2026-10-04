// Copyright 2026 Maho Browser. All rights reserved.

import React from 'react';
import {AlertTriangle, AtSign, ChevronDown, ChevronUp, Loader2, Mail, Server} from 'lucide-react';

import {cn} from '@lib/utils';
import {Alert, AlertDescription} from '@ui/alert';
import {Button} from '@ui/button';
import type {ProviderStepActions, ProviderStepState} from './mail_onboarding_controller_types.js';
import type {ProviderId} from './mail_onboarding_config.js';
import type {MailOnboardingStringLookup} from './mail_onboarding_api.js';

type MailOnboardingProviderStepProps = {
  readonly state: ProviderStepState;
  readonly actions: ProviderStepActions;
  readonly copy: MailOnboardingStringLookup;
};

export function MailOnboardingProviderStep({state, actions, copy}: MailOnboardingProviderStepProps) {
  return (
    <div className="w-full space-y-4">
      <h2 className="text-base font-semibold text-foreground">{copy('onboarding.chooseProvider', 'Choose your email provider')}</h2>
      {state.mailServiceStatus === 'preparing' && (
        <Alert variant="warning" className="mt-2.5">
          <Loader2 className="size-3.5 animate-spin" />
          <AlertDescription className="text-[10px]">
            {copy('onboarding.mailPreparing', 'Mail is still getting ready. We will retry automatically.')}
          </AlertDescription>
        </Alert>
      )}
      {state.mailServiceStatus === 'unavailable' && (
        <Alert variant="destructive" className="mt-2.5">
          <AlertTriangle className="size-3.5" />
          <AlertDescription className="text-[10px]">
            {copy('onboarding.mailUnavailable', 'Mail could not start. Restart Maho and try again.')}
          </AlertDescription>
        </Alert>
      )}
      {state.error && (
        <Alert variant="destructive" className="mt-2.5">
          <AlertTriangle className="size-3.5" />
          <AlertDescription className="text-[10px]">{state.error}</AlertDescription>
        </Alert>
      )}
      <div className="grid grid-cols-2 gap-2.5 sm:gap-3">
        {state.visibleProviders.map((provider) => {
          const isOther = provider.id === 'other';
          return (
            <button
              key={provider.id}
              type="button"
              onClick={() => { void actions.onSelectProvider(provider.id); }}
              disabled={state.busy}
              className={cn(
                'group flex items-center gap-3 rounded-lg border border-border bg-background/60 p-3 text-left transition-colors hover:border-border hover:bg-muted/50 focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring disabled:opacity-50',
                isOther ? 'col-span-2' : '',
              )}
            >
              <div className="shrink-0 flex size-9 items-center justify-center rounded-lg border border-border bg-card shadow-sm">
                {getProviderIcon(provider.id)}
              </div>
              <div>
                <div className="text-xs font-semibold text-foreground">{copy(provider.nameKey, provider.nameFallback)}</div>
                <div className="text-[10px] text-muted-foreground">{copy(provider.descriptionKey, provider.descriptionFallback)}</div>
              </div>
            </button>
          );
        })}

        {state.hasHiddenProviders && (
          <Button
            type="button"
            variant="ghost"
            size="sm"
            onClick={() => actions.onShowAllProvidersChange(true)}
            className="col-span-2 h-9 rounded-lg border border-dashed border-border px-4 py-2 text-xs text-muted-foreground hover:bg-muted/50 hover:text-foreground"
            aria-expanded={state.showAllProviders}
          >
            <ChevronDown className="mr-1 size-3.5" />
            {copy('onboarding.moreProviders', 'More providers')}
          </Button>
        )}

        {state.showAllProviders && (
          <Button
            type="button"
            variant="ghost"
            size="sm"
            onClick={() => actions.onShowAllProvidersChange(false)}
            className="col-span-2 h-9 rounded-lg px-4 py-2 text-xs text-muted-foreground hover:bg-muted/50 hover:text-foreground"
            aria-expanded={state.showAllProviders}
          >
            <ChevronUp className="mr-1 size-3.5" />
            {copy('onboarding.showFewer', 'Show fewer')}
          </Button>
        )}
      </div>
    </div>
  );
}

function getProviderIcon(id: ProviderId) {
  switch (id) {
    case 'gmail':
    case 'outlook':
      return <Mail className="size-5 text-muted-foreground" aria-hidden="true" />;
    case 'other':
      return <Server className="size-5 text-muted-foreground" aria-hidden="true" />;
    default:
      return <AtSign className="size-5 text-muted-foreground" aria-hidden="true" />;
  }
}
