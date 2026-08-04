// Copyright 2026 Maho Browser. All rights reserved.

import React from 'react';
import {renderToStaticMarkup} from 'react-dom/server';
import { describe, it, expect, vi, beforeEach } from 'vitest';
import type {Mock} from 'vitest';
import type {
  MailAccount,
  MailHandlerResult,
  MailOnboardingApi,
  MailOnboardingConfig,
  MailOnboardingStepKey,
  MailOnboardingStringLookup,
  MailOnboardingVisualFixture,
} from '../../../maho_common/react/mail/mail_onboarding_api';
import {
  MAIL_APP_ONBOARDING_CONFIG,
  WELCOME_MAIL_ONBOARDING_CONFIG,
  deleteMailAccountWithConfirmation,
  getMailOnboardingActionFlags,
  isMailHelperUnavailableError,
  parseMailAccounts,
  parseMailServerPort,
  toSafeMailErrorMessage,
} from '../../../maho_common/react/mail/mail_onboarding_api';
import type {
  CredentialsStepActions,
  CredentialsStepState,
  ProviderStepActions,
  ProviderStepState,
  TranslationProviderChoice,
  TranslationStepState,
} from '../../../maho_common/react/mail/mail_onboarding_controller_types';
import {
  MailOnboardingLifecycle,
  type LifecycleDelegate,
} from '../../../maho_common/react/mail/mail_onboarding_lifecycle';
import type {ProviderId, SetupStep} from '../../../maho_common/react/mail/mail_onboarding_config';
import {MailOnboardingContent} from '../../../maho_common/react/mail/mail_onboarding_content';
import {
  applyFooterPrimaryNavigation,
  applyGlobalKeyNavigation,
} from '../navigation.js';
import {WelcomePage} from '../types.js';
import {createWelcomeMailStringLookup} from '../localized-strings.js';

type AccountsChangedCallback = () => void;
type UnsubscribeCallback = () => void;
type BeginOAuth = MailOnboardingApi['beginOAuth'];
type MockAccountsListProps = {
  readonly accounts: readonly MailAccount[];
};
type MockStepperProps = {
  readonly steps: readonly MailOnboardingStepKey[];
  readonly step: SetupStep;
  readonly copy: MailOnboardingStringLookup;
};
type MockProviderStepProps = {
  readonly state: ProviderStepState;
  readonly actions: ProviderStepActions;
  readonly copy: MailOnboardingStringLookup;
};
type MockCredentialsStepProps = {
  readonly state: CredentialsStepState;
  readonly actions: CredentialsStepActions;
  readonly copy: MailOnboardingStringLookup;
};
type MockOAuthWaitingProps = {
  readonly selectedProvider: ProviderId | null;
};
type MockTranslationStepProps = {
  readonly state: TranslationStepState;
  readonly onSelectProvider: (provider: TranslationProviderChoice) => Promise<void>;
};

vi.mock('../../../maho_common/react/mail/mail_onboarding_accounts_list.js', async () => {
  const ReactModule = await import('react');
  return {
    MailOnboardingAccountsList: ({accounts}: MockAccountsListProps) => ReactModule.createElement(
      'section',
      {'data-mail-accounts-count': String(accounts.length)},
    ),
  };
});

vi.mock('../../../maho_common/react/mail/mail_onboarding_stepper.js', async () => {
  const ReactModule = await import('react');
  return {
    MailOnboardingStepper: ({steps, step}: MockStepperProps) => ReactModule.createElement(
      'nav',
      {'data-current-step': step},
      steps.map(stepKey => ReactModule.createElement(
        'span',
        {key: stepKey, 'data-step-key': stepKey},
      )),
    ),
  };
});

vi.mock('../../../maho_common/react/mail/mail_onboarding_provider_step.js', async () => {
  const ReactModule = await import('react');
  return {
    MailOnboardingProviderStep: ({state}: MockProviderStepProps) => ReactModule.createElement(
      'section',
      {
        'data-mail-step': 'provider',
        'data-mail-service-status': state.mailServiceStatus,
      },
    ),
  };
});

vi.mock('../../../maho_common/react/mail/mail_onboarding_credentials_step.js', async () => {
  const ReactModule = await import('react');
  return {
    MailOnboardingCredentialsStep: ({state}: MockCredentialsStepProps) => ReactModule.createElement(
      'section',
      {
        'data-mail-step': 'credentials',
        'data-mail-error': state.error ?? '',
      },
    ),
  };
});

vi.mock('../../../maho_common/react/mail/mail_onboarding_oauth_waiting.js', async () => {
  const ReactModule = await import('react');
  return {
    MailOnboardingOAuthWaiting: ({selectedProvider}: MockOAuthWaitingProps) => ReactModule.createElement(
      'section',
      {
        'data-mail-step': 'oauth',
        'data-provider': selectedProvider ?? 'none',
      },
    ),
  };
});

vi.mock('../../../maho_common/react/mail/mail_onboarding_translation_step.js', async () => {
  const ReactModule = await import('react');
  return {
    MailOnboardingTranslationStep: ({state}: MockTranslationStepProps) => ReactModule.createElement(
      'section',
      {
        'data-mail-step': 'translation',
        'data-ai-configured': String(state.isAiConfigured),
        'data-translation-choices': 'byok skip',
      },
    ),
  };
});

vi.mock('../../../maho_common/react/mail/mail_onboarding_success_step.js', async () => {
  const ReactModule = await import('react');
  return {
    MailOnboardingSuccessStep: () => ReactModule.createElement('section', {'data-mail-step': 'success'}),
  };
});

function nextTick(): Promise<void> {
  return new Promise(resolve => process.nextTick(resolve));
}

function createMailApi(overrides: Partial<MailOnboardingApi> = {}): MailOnboardingApi {
  return {
    listAccounts: vi.fn<MailOnboardingApi['listAccounts']>()
      .mockResolvedValue({ok: true, resultJson: '[]'}),
    addAccount: vi.fn<MailOnboardingApi['addAccount']>()
      .mockResolvedValue({ok: true, resultJson: '{}'}),
    testConnection: vi.fn<MailOnboardingApi['testConnection']>()
      .mockResolvedValue({ok: true, resultJson: '{}'}),
    deleteAccount: vi.fn<MailOnboardingApi['deleteAccount']>()
      .mockResolvedValue({ok: true, resultJson: '{}'}),
    beginOAuth: vi.fn<MailOnboardingApi['beginOAuth']>()
      .mockResolvedValue({ok: true, state: 'state_render', errorJson: ''}),
    getAiProviderConfigured: vi.fn<NonNullable<MailOnboardingApi['getAiProviderConfigured']>>()
      .mockResolvedValue(true),
    setTranslationProvider: vi.fn<NonNullable<MailOnboardingApi['setTranslationProvider']>>()
      .mockResolvedValue(true),
    subscribeAccountsChanged: vi.fn<MailOnboardingApi['subscribeAccountsChanged']>(() => vi.fn()),
    cancelOAuth: vi.fn<MailOnboardingApi['cancelOAuth']>().mockResolvedValue(true),
    onComplete: vi.fn<MailOnboardingApi['onComplete']>(),
    ...overrides,
  };
}

function renderMailOnboarding(input: {
  readonly config: MailOnboardingConfig;
  readonly accounts?: readonly MailAccount[];
  readonly visualFixture?: MailOnboardingVisualFixture;
}): string {
  return renderToStaticMarkup(React.createElement(MailOnboardingContent, {
    api: createMailApi(),
    accounts: input.accounts ?? [],
    onAccountsChange: vi.fn<(accounts: readonly MailAccount[]) => void>(),
    config: input.config,
    visualFixture: input.visualFixture,
  }));
}

describe('Mail onboarding Welcome presentation contract', () => {
  it('renders the typed Welcome surface marker and translation visual step', () => {
    const html = renderMailOnboarding({
      config: WELCOME_MAIL_ONBOARDING_CONFIG,
      visualFixture: 'translation',
    });

    expect(html).toContain('data-mail-onboarding-surface="welcome"');
    expect(html).toContain('data-step-key="translation"');
    expect(html).toContain('data-mail-step="translation"');
    expect(html).toContain('data-translation-choices="byok skip"');
  });

  it('keeps standalone Mail output on the mail surface without Welcome marker or translation step', () => {
    const html = renderMailOnboarding({config: MAIL_APP_ONBOARDING_CONFIG});

    expect(html).not.toContain('data-mail-onboarding-surface');
    expect(html).not.toContain('data-step-key="translation"');
    expect(html).toContain('data-step-key="provider"');
    expect(html).toContain('data-step-key="connect"');
    expect(html).toContain('data-step-key="done"');
    expect(html).toContain('data-current-step="provider"');
  });

  it('covers mocked zero-account, one-account, helper, OAuth, and translation Welcome states', () => {
    const zeroAccount = renderMailOnboarding({
      config: WELCOME_MAIL_ONBOARDING_CONFIG,
      visualFixture: 'zero-account',
    });
    const oneAccount = renderMailOnboarding({
      config: WELCOME_MAIL_ONBOARDING_CONFIG,
      visualFixture: 'one-account',
    });
    const helperUnavailable = renderMailOnboarding({
      config: WELCOME_MAIL_ONBOARDING_CONFIG,
      visualFixture: 'helper',
    });
    const oauthWaiting = renderMailOnboarding({
      config: WELCOME_MAIL_ONBOARDING_CONFIG,
      visualFixture: 'oauth',
    });
    const translation = renderMailOnboarding({
      config: WELCOME_MAIL_ONBOARDING_CONFIG,
      visualFixture: 'translation',
    });

    expect(zeroAccount).toContain('data-mail-accounts-count="0"');
    expect(oneAccount).toContain('data-mail-accounts-count="1"');
    expect(helperUnavailable).toContain('data-mail-step="provider"');
    expect(helperUnavailable).toContain('data-mail-service-status="preparing"');
    expect(oauthWaiting).toContain('data-mail-step="oauth"');
    expect(oauthWaiting).toContain('data-provider="gmail"');
    expect(translation).toContain('data-mail-step="translation"');
  });

  it('routes explicit Welcome MailSetup skip without generic advance', async () => {
    const globalActions = {
      submitAuth: vi.fn<() => void>(),
      nextPage: vi.fn<() => void>(),
      complete: vi.fn<() => void>(),
      openMigrationDialog: vi.fn<() => void>(),
      skipImport: vi.fn<() => void>(),
      skipMailSetup: vi.fn<() => void>(),
    };

    const enterHandled = applyGlobalKeyNavigation({
      key: 'Enter',
      defaultPrevented: false,
      repeat: false,
      currentPage: WelcomePage.MailSetup,
      importStage: 'A',
      targetKind: 'body',
    }, globalActions);

    expect(enterHandled).toBe(false);
    expect(globalActions.nextPage).not.toHaveBeenCalled();
    expect(globalActions.skipMailSetup).not.toHaveBeenCalled();

    const escapeHandled = applyGlobalKeyNavigation({
      key: 'Escape',
      defaultPrevented: false,
      repeat: false,
      currentPage: WelcomePage.MailSetup,
      importStage: 'A',
      targetKind: 'body',
    }, globalActions);

    expect(escapeHandled).toBe(true);
    expect(globalActions.skipMailSetup).toHaveBeenCalledTimes(1);
    expect(globalActions.nextPage).not.toHaveBeenCalled();

    const footerActions = {
      submitAuth: vi.fn<() => Promise<void>>().mockResolvedValue(undefined),
      startImport: vi.fn<(browserIndex: number) => void>(),
      openMigrationDialog: vi.fn<() => void>(),
      setSetAsDefault: vi.fn<(value: boolean) => void>(),
      nextPage: vi.fn<() => void>(),
      complete: vi.fn<() => void>(),
      openMainBrowser: vi.fn<() => void>(),
      skipMailSetup: vi.fn<() => void>(),
    };

    await applyFooterPrimaryNavigation({
      currentPage: WelcomePage.MailSetup,
      importStage: 'A',
      hasAvailableBrowsers: false,
      selectedBrowserIndex: null,
    }, footerActions);

    expect(footerActions.skipMailSetup).toHaveBeenCalledTimes(1);
    expect(footerActions.nextPage).not.toHaveBeenCalled();
    expect(footerActions.complete).not.toHaveBeenCalled();
  });
});

describe('MailOnboardingLifecycle', () => {
  let api: MailOnboardingApi;
  let delegate: LifecycleDelegate;
  let listAccounts: Mock<MailOnboardingApi['listAccounts']>;
  let beginOAuth: Mock<BeginOAuth>;
  let subscribeAccountsChanged: Mock<MailOnboardingApi['subscribeAccountsChanged']>;
  let cancelOAuth: Mock<MailOnboardingApi['cancelOAuth']>;
  let onAccountsLoaded: Mock<LifecycleDelegate['onAccountsLoaded']>;
  let onAccountsChanged: Mock<LifecycleDelegate['onAccountsChanged']>;
  let onOAuthComplete: Mock<LifecycleDelegate['onOAuthComplete']>;
  let onOAuthFailed: Mock<LifecycleDelegate['onOAuthFailed']>;
  let onReadinessChanged: Mock<LifecycleDelegate['onReadinessChanged']>;
  let accountsChangedCallback: AccountsChangedCallback | null;

  beforeEach(() => {
    accountsChangedCallback = null;
    listAccounts = vi.fn<MailOnboardingApi['listAccounts']>()
      .mockResolvedValue({ ok: true, resultJson: '[]' });
    beginOAuth = vi.fn<BeginOAuth>()
      .mockResolvedValue({ ok: true, state: 'state_default', errorJson: '' });
    subscribeAccountsChanged = vi.fn<MailOnboardingApi['subscribeAccountsChanged']>(
      (callback) => {
        accountsChangedCallback = callback;
        return vi.fn<UnsubscribeCallback>();
      },
    );
    cancelOAuth = vi.fn<MailOnboardingApi['cancelOAuth']>()
      .mockResolvedValue(true);
    api = {
      listAccounts,
      addAccount: vi.fn<MailOnboardingApi['addAccount']>(),
      testConnection: vi.fn<MailOnboardingApi['testConnection']>(),
      deleteAccount: vi.fn<MailOnboardingApi['deleteAccount']>(),
      beginOAuth,
      getAiProviderConfigured: vi.fn<NonNullable<MailOnboardingApi['getAiProviderConfigured']>>()
        .mockResolvedValue(true),
      setTranslationProvider: vi.fn<NonNullable<MailOnboardingApi['setTranslationProvider']>>()
        .mockResolvedValue(true),
      subscribeAccountsChanged,
      cancelOAuth,
      onComplete: vi.fn<MailOnboardingApi['onComplete']>(),
    };
    onAccountsLoaded = vi.fn<LifecycleDelegate['onAccountsLoaded']>();
    onAccountsChanged = vi.fn<LifecycleDelegate['onAccountsChanged']>();
    onOAuthComplete = vi.fn<LifecycleDelegate['onOAuthComplete']>();
    onOAuthFailed = vi.fn<LifecycleDelegate['onOAuthFailed']>();
    onReadinessChanged = vi.fn<LifecycleDelegate['onReadinessChanged']>();
    delegate = {
      onAccountsLoaded,
      onAccountsChanged,
      onOAuthComplete,
      onOAuthFailed,
      onReadinessChanged,
    };
  });

  it('initial load starts exactly once', async () => {
    const lifecycle = new MailOnboardingLifecycle(api, delegate);
    lifecycle.start();
    lifecycle.start();
    await nextTick();
    expect(listAccounts).toHaveBeenCalledTimes(1);
    expect(onAccountsLoaded).toHaveBeenCalledWith([]);
  });

  it('one account-change event causes exactly one reconciliation', async () => {
    const lifecycle = new MailOnboardingLifecycle(api, delegate);
    lifecycle.start();
    await nextTick();
    expect(listAccounts).toHaveBeenCalledTimes(1);
    expect(onAccountsLoaded).toHaveBeenCalledTimes(1);
    expect(onAccountsChanged).not.toHaveBeenCalled();

    accountsChangedCallback?.();
    await nextTick();
    expect(listAccounts).toHaveBeenCalledTimes(2);
    expect(onAccountsLoaded).toHaveBeenCalledTimes(1);
    expect(onAccountsChanged).toHaveBeenCalledTimes(1);
  });

  it('helper-unavailable listAccounts enters readiness and retries until ready', async () => {
    vi.useFakeTimers();
    try {
      listAccounts
        .mockResolvedValueOnce({ok: false, resultJson: '{"error":"mail helper unavailable"}'})
        .mockResolvedValueOnce({ok: true, resultJson: '[{"id":"ready_1","email":"ready@maho.dev"}]'});
      const lifecycle = new MailOnboardingLifecycle(api, delegate);

      lifecycle.start();
      await nextTick();

      expect(listAccounts).toHaveBeenCalledTimes(1);
      expect(onReadinessChanged).toHaveBeenCalledWith('preparing');
      expect(onOAuthFailed).not.toHaveBeenCalled();

      await vi.advanceTimersByTimeAsync(250);

      expect(listAccounts).toHaveBeenCalledTimes(2);
      expect(onReadinessChanged).toHaveBeenCalledWith('ready');
      expect(onAccountsLoaded).toHaveBeenCalledWith([
        {id: 'ready_1', email: 'ready@maho.dev'},
      ]);
    } finally {
      vi.useRealTimers();
    }
  });

  it('focus/visibility causes at most one reconciliation', async () => {
    const lifecycle = new MailOnboardingLifecycle(api, delegate);
    lifecycle.start();
    await nextTick();
    expect(listAccounts).toHaveBeenCalledTimes(1);

    await lifecycle.onVisibilityFocus();
    expect(listAccounts).toHaveBeenCalledTimes(2);
  });

  it('production startOAuth state reaches oauth wait and reconciles through translation', async () => {
    type SetupStep = 'provider' | 'oauth' | 'translation' | 'success';
    let step: SetupStep = 'provider';
    let busy = true;
    listAccounts
      .mockResolvedValueOnce({ ok: true, resultJson: '[]' })
      .mockResolvedValueOnce({
        ok: true,
        resultJson: '[{"id":"acc_new","email":"oauth@maho.dev"}]',
      });
    beginOAuth.mockResolvedValueOnce({
      ok: true,
      state: 'state_from_cpp',
      errorJson: '',
    });

    const lifecycle = new MailOnboardingLifecycle(api, {
      ...delegate,
      onOAuthComplete: () => {
        busy = false;
        step = api.getAiProviderConfigured && api.setTranslationProvider
          ? 'translation'
          : 'success';
      },
    });
    lifecycle.setOauthAccountIds(new Set());
    lifecycle.start();
    await nextTick();

    const beginResult = await lifecycle.beginOAuth('gmail');
    if (beginResult.ok) {
      step = 'oauth';
    }

    expect(beginOAuth).toHaveBeenCalledWith('gmail');
    expect(beginResult).toEqual({ok: true, state: 'state_from_cpp'});
    expect(lifecycle.getActiveOAuthState()).toBe('state_from_cpp');
    expect(step).toBe('oauth');

    accountsChangedCallback?.();
    await nextTick();

    expect(busy).toBe(false);
    expect(step).toBe('translation');
    expect(lifecycle.getActiveOAuthState()).toBeNull();
    expect(cancelOAuth).not.toHaveBeenCalled();
  });

  it('production startOAuth empty state returns an error without locking providers', async () => {
    let busy = true;
    let errorMessage: string | null = null;
    beginOAuth.mockResolvedValueOnce({
      ok: true,
      state: '',
      errorJson: '',
    });

    const lifecycle = new MailOnboardingLifecycle(api, delegate);
    const beginResult = await lifecycle.beginOAuth('gmail');
    if (!beginResult.ok) {
      busy = false;
      errorMessage = beginResult.errorMessage;
    }

    expect(beginResult).toEqual({
      ok: false,
      errorMessage: 'The mail service returned an invalid OAuth state.',
    });
    expect(busy).toBe(false);
    expect(errorMessage).toBe('The mail service returned an invalid OAuth state.');
    expect(lifecycle.getActiveOAuthState()).toBeNull();
    expect(cancelOAuth).not.toHaveBeenCalled();
  });

  it('production startOAuth active state is cancelled once on unmount', async () => {
    beginOAuth.mockResolvedValueOnce({
      ok: true,
      state: 'state_cancel_once',
      errorJson: '',
    });
    const lifecycle = new MailOnboardingLifecycle(api, delegate);

    const beginResult = await lifecycle.beginOAuth('outlook');
    expect(beginResult).toEqual({ok: true, state: 'state_cancel_once'});

    lifecycle.cancelActiveOAuth();
    lifecycle.destroy();

    expect(cancelOAuth).toHaveBeenCalledWith('state_cancel_once');
    expect(cancelOAuth).toHaveBeenCalledTimes(1);
  });

  it('cancel/unmount while pending calls cancelOAuth exactly once', async () => {
    const lifecycle = new MailOnboardingLifecycle(api, delegate);
    lifecycle.start();
    lifecycle.setOauthStarted('state_xyz');
    lifecycle.destroy();
    expect(cancelOAuth).toHaveBeenCalledWith('state_xyz');
    expect(cancelOAuth).toHaveBeenCalledTimes(1);
  });

  it('successful reconciliation clears active state so no cancelOAuth is issued on destroy', async () => {
    listAccounts.mockResolvedValue({
      ok: true,
      resultJson: '[{"id":"acc_1","email":"test@maho.com"}]',
    });
    const lifecycle = new MailOnboardingLifecycle(api, delegate);
    lifecycle.setOauthAccountIds(new Set());
    lifecycle.setOauthStarted('state_abc');
    lifecycle.start();
    await nextTick();

    expect(onOAuthComplete).toHaveBeenCalledTimes(1);
    expect(lifecycle.getActiveOAuthState()).toBeNull();

    lifecycle.destroy();
    expect(cancelOAuth).not.toHaveBeenCalled();
  });
});

describe('mail onboarding shared policies', () => {
  it('strictly parses accounts and rejects missing or empty identity fields', () => {
    expect(parseMailAccounts('[{"id":"a1","email":"a@maho.dev"}]')).toEqual([
      {id: 'a1', email: 'a@maho.dev'},
    ]);
    expect(parseMailAccounts('not-json')).toBeNull();
    expect(parseMailAccounts('[{"id":"","email":"a@maho.dev"}]')).toBeNull();
    expect(parseMailAccounts('[{"id":"a1","email":""}]')).toBeNull();
    expect(parseMailAccounts('[{"id":"a1"}]')).toBeNull();
  });

  it('maps helper-unavailable internals to fake locale readiness copy', () => {
    const rawJson = '{"error":"Mail service unavailable","detail":"mail helper unavailable"}';
    expect(isMailHelperUnavailableError(rawJson)).toBe(true);
    const lookup = createWelcomeMailStringLookup(new Map([
      ['IDS_MAHO_WELCOME_MAIL_ONBOARDING_MAILPREPARING', 'Fake Mail readiness'],
    ]));
    const safe = toSafeMailErrorMessage(
      rawJson,
      lookup(
        'onboarding.mailPreparing',
        'Mail is still getting ready. We will retry automatically.',
      ),
    );
    expect(safe).toBe('Fake Mail readiness');
    expect(safe).not.toContain('mail helper unavailable');
    expect(safe).not.toContain('Mail service unavailable');
    expect(safe).not.toContain('{');
  });

  it('maps credentials failures to localized fallback copy without leaking internals', () => {
    const rawJson = '{"error":"IMAP authentication failed","detail":"password rejected"}';
    const safe = toSafeMailErrorMessage(rawJson, 'Fake credentials recovery');

    expect(safe).toBe('Fake credentials recovery');
    expect(safe).not.toContain('IMAP authentication failed');
    expect(safe).not.toContain('password rejected');
    expect(safe).not.toContain('{');
  });

  it('uses explicit surface configs for Welcome and standalone Mail steps', () => {
    expect(WELCOME_MAIL_ONBOARDING_CONFIG.surface).toBe('welcome');
    expect(WELCOME_MAIL_ONBOARDING_CONFIG.steps).toContain('translation');
    expect(WELCOME_MAIL_ONBOARDING_CONFIG.initialExistingAccountsStep).toBe('provider');
    expect(MAIL_APP_ONBOARDING_CONFIG.surface).toBe('mail');
    expect(MAIL_APP_ONBOARDING_CONFIG.steps).not.toContain('translation');
    expect(MAIL_APP_ONBOARDING_CONFIG.initialExistingAccountsStep).toBe('success');
  });

  it('requires account delete confirmation and surfaces failure without local removal', async () => {
    const account = {id: 'delete_1', email: 'delete@maho.dev'};
    const deleteAccount = vi.fn<MailOnboardingApi['deleteAccount']>()
      .mockResolvedValue({ok: true, resultJson: '{}'});

    await expect(deleteMailAccountWithConfirmation({
      account,
      accounts: [account],
      confirmDelete: () => false,
      deleteAccount,
      failureMessage: 'Could not delete this mail account.',
    })).resolves.toEqual({kind: 'cancelled'});
    expect(deleteAccount).not.toHaveBeenCalled();

    deleteAccount.mockResolvedValueOnce({ok: false, resultJson: '{"error":"Mail service unavailable"}'});
    const failed = await deleteMailAccountWithConfirmation({
      account,
      accounts: [account],
      confirmDelete: () => true,
      deleteAccount,
      failureMessage: 'Could not delete this mail account.',
    });
    expect(failed.kind).toBe('failed');
    if (failed.kind === 'failed') {
      expect(failed.accounts).toEqual([account]);
      expect(failed.errorMessage).not.toContain('Mail service unavailable');
    }

    deleteAccount.mockResolvedValueOnce({ok: true, resultJson: '{}'});
    await expect(deleteMailAccountWithConfirmation({
      account,
      accounts: [account],
      confirmDelete: () => true,
      deleteAccount,
      failureMessage: 'Could not delete this mail account.',
    })).resolves.toEqual({kind: 'deleted', accounts: []});
  });

  it('allows blank port text while editing and parses only on submit', () => {
    expect(parseMailServerPort('')).toEqual({ok: false, reason: 'empty'});
    expect(parseMailServerPort('0')).toEqual({ok: false, reason: 'range'});
    expect(parseMailServerPort('65536')).toEqual({ok: false, reason: 'range'});
    expect(parseMailServerPort('993')).toEqual({ok: true, value: 993});
  });

  it('separates test and add spinners through explicit action state', () => {
    expect(getMailOnboardingActionFlags('testing')).toEqual({testing: true, adding: false});
    expect(getMailOnboardingActionFlags('adding')).toEqual({testing: false, adding: true});
    expect(getMailOnboardingActionFlags(null)).toEqual({testing: false, adding: false});
  });
});
