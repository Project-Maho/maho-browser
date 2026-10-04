// Copyright 2026 Maho Browser. All rights reserved.

import React from 'react';
import {CheckCircle, Trash2} from 'lucide-react';

import {Button} from '@ui/button';
import {Card} from '@ui/card';
import type {MailAccount, MailOnboardingStringLookup} from './mail_onboarding_api.js';

type MailOnboardingAccountsListProps = {
  readonly accounts: readonly MailAccount[];
  readonly busy: boolean;
  readonly copy: MailOnboardingStringLookup;
  readonly onDeleteAccount: (account: MailAccount) => Promise<void>;
};

export function MailOnboardingAccountsList({
  accounts,
  busy,
  copy,
  onDeleteAccount,
}: MailOnboardingAccountsListProps) {
  if (accounts.length === 0) return null;

  return (
    <Card className="flex flex-col gap-2 bg-background/60 p-3 shadow-sm border border-border">
      {accounts.map(account => (
        <div key={account.id} className="flex items-center gap-3 rounded-lg bg-muted/40 px-3 py-2">
          <CheckCircle className="size-4 text-success" aria-hidden="true" />
          <span className="min-w-0 flex-1 truncate text-sm font-medium">{account.email}</span>
          <Button
            type="button"
            variant="ghost"
            size="icon"
            className="size-8 text-muted-foreground hover:text-destructive"
            aria-label={copy('onboarding.removeAccountLabel', 'Remove {{email}}', {email: account.email})}
            onClick={() => { void onDeleteAccount(account); }}
            disabled={busy}
          >
            <Trash2 className="size-4" aria-hidden="true" />
          </Button>
        </div>
      ))}
    </Card>
  );
}
