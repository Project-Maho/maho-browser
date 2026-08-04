// Copyright 2026 Maho Browser. All rights reserved.

import React from 'react';
import type { MailAccount, MailOnboardingStringLookup } from './mail_onboarding_api.js';
import {defaultMailOnboardingStringLookup} from './mail_onboarding_api.js';

interface MailOnboardingSidebarProps {
  readonly accounts: readonly MailAccount[];
  readonly t?: MailOnboardingStringLookup;
}

export function MailOnboardingSidebar({ accounts, t = defaultMailOnboardingStringLookup }: MailOnboardingSidebarProps) {
  return (
    <>
      <h1 className="text-2xl font-bold tracking-tight">{t('onboarding.sidebarTitle', 'Bring your inbox to Maho')}</h1>
      <p className="text-sm text-muted-foreground">{t('onboarding.sidebarBody', 'Connect an account now so Mail is ready when you finish setup.')}</p>
      <p className="text-sm text-muted-foreground">
        {accounts.length > 0
          ? t('onboarding.sidebarConnectedCount', '{{count}} account{{plural}} connected.', {
              count: accounts.length,
              plural: accounts.length === 1 ? '' : 's',
            })
          : t('onboarding.sidebarSkipBody', 'You can skip this step and add accounts later.')}
      </p>
    </>
  );
}
