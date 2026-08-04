import React, { useMemo, useCallback } from 'react';
import { MailOnboardingContent } from '../../../maho_common/react/mail/mail_onboarding_content.js';
import { MailOnboardingSidebar } from '../../../maho_common/react/mail/mail_onboarding_sidebar.js';
import type {
  MailAccount,
  MailOnboardingApi,
} from '../../../maho_common/react/mail/mail_onboarding_api.js';
import {WELCOME_MAIL_ONBOARDING_CONFIG} from '../../../maho_common/react/mail/mail_onboarding_api.js';
import {createWelcomeMailStringLookup} from '../localized-strings.js';
import type { MahoWelcomeStore } from '../store.js';
import type { WelcomeState } from '../types.js';

interface MailSetupContentProps {
  readonly store: MahoWelcomeStore;
  readonly snapshot: WelcomeState;
}

interface MailSetupSidebarProps {
  readonly snapshot: WelcomeState;
}

export function MailSetupSidebar({ snapshot }: MailSetupSidebarProps) {
  const stringLookup = useMemo(
    () => createWelcomeMailStringLookup(snapshot.localizedStrings),
    [snapshot.localizedStrings],
  );
  return <MailOnboardingSidebar accounts={snapshot.mailAccounts} t={stringLookup} />;
}

export function MailSetupContent({ store, snapshot }: MailSetupContentProps) {
  const api: MailOnboardingApi = useMemo(() => ({
    listAccounts: () => store.mailListAccounts(),
    addAccount: (requestJson: string) => store.mailAddAccount(requestJson),
    testConnection: (paramsJson: string) => store.mailTestConnection(paramsJson),
    deleteAccount: (accountId: string) => store.mailDeleteAccount(accountId),
    beginOAuth: (provider: string) => store.mailBeginOAuth(provider),
    getAiProviderConfigured: () => store.getAiProviderConfigured(),
    setTranslationProvider: (provider: string) => store.setTranslationProvider(provider),
    onComplete: () => store.nextPage(),
    subscribeAccountsChanged: (cb: () => void) => store.subscribeMailAccountsChanged(cb),
    cancelOAuth: (state: string) => store.mailOAuthCancel(state),
  }), [store]);

  const handleAccountsChange = useCallback((accounts: readonly MailAccount[]) => {
    store.patchMailAccounts(accounts);
  }, [store]);

  const stringLookup = useMemo(
    () => createWelcomeMailStringLookup(snapshot.localizedStrings),
    [snapshot.localizedStrings],
  );

  return (
    <MailOnboardingContent
      api={api}
      accounts={snapshot.mailAccounts}
      onAccountsChange={handleAccountsChange}
      config={WELCOME_MAIL_ONBOARDING_CONFIG}
      t={stringLookup}
      visualFixture={snapshot.visualMailStage ?? undefined}
    />
  );
}
