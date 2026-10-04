// Copyright 2026 Maho Browser. All rights reserved.

import React from 'react';
import {Loader2} from 'lucide-react';

import {Button} from '@ui/button';
import {Card} from '@ui/card';
import type {MailOnboardingStringLookup} from './mail_onboarding_api.js';
import type {ProviderId} from './mail_onboarding_config.js';

type MailOnboardingOAuthWaitingProps = {
  readonly copy: MailOnboardingStringLookup;
  readonly selectedProvider: ProviderId | null;
  readonly onCancel: () => void;
};

export function MailOnboardingOAuthWaiting({copy, selectedProvider, onCancel}: MailOnboardingOAuthWaitingProps) {
  return (
    <Card className="flex flex-col items-center gap-5 bg-background/60 p-8 text-center shadow-sm border border-border rounded-xl">
      <Loader2 className="size-10 animate-spin text-muted-foreground" aria-hidden="true" />
      <div>
        <h2 className="text-base font-semibold text-foreground">{copy('onboarding.oauthWaitingTitle', 'Waiting for authorization…')}</h2>
        <p className="mt-2 text-xs text-muted-foreground">
          {copy('onboarding.oauthWaitingBody', 'Complete sign-in to {{provider}} in the new tab.', {
            provider: selectedProvider === 'gmail' ? 'Google' : 'Microsoft',
          })}
        </p>
      </div>
      <Button type="button" variant="outline" size="sm" onClick={onCancel}>
        {copy('common.cancel', 'Cancel')}
      </Button>
    </Card>
  );
}
