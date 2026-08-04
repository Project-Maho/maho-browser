import {afterEach, describe, expect, it, vi} from 'vitest';

import type {MailOnboardingApi} from '../../../maho_common/react/mail/mail_onboarding_api.js';
import {
  MailOnboardingLifecycle,
  type LifecycleDelegate,
} from '../../../maho_common/react/mail/mail_onboarding_lifecycle.js';

describe('MailOnboardingLifecycle readiness', () => {
  afterEach(() => {
    vi.useRealTimers();
  });

  it('stops claiming automatic retries after the retry budget is exhausted', async () => {
    vi.useFakeTimers();
    const listAccounts = vi.fn<MailOnboardingApi['listAccounts']>()
      .mockResolvedValue({ok: false, resultJson: 'mail helper unavailable'});
    const api: MailOnboardingApi = {
      listAccounts,
      addAccount: vi.fn(),
      testConnection: vi.fn(),
      deleteAccount: vi.fn(),
      beginOAuth: vi.fn(),
      subscribeAccountsChanged: vi.fn(() => () => {}),
      cancelOAuth: vi.fn(),
      onComplete: vi.fn(),
    };
    const onReadinessChanged = vi.fn<LifecycleDelegate['onReadinessChanged']>();
    const delegate: LifecycleDelegate = {
      onAccountsLoaded: vi.fn(),
      onAccountsChanged: vi.fn(),
      onOAuthComplete: vi.fn(),
      onOAuthFailed: vi.fn(),
      onReadinessChanged,
    };
    const lifecycle = new MailOnboardingLifecycle(api, delegate);

    lifecycle.start();
    await vi.advanceTimersByTimeAsync(0);
    await vi.advanceTimersByTimeAsync(250 + 500 + 1000);

    expect(listAccounts).toHaveBeenCalledTimes(4);
    expect(onReadinessChanged).toHaveBeenLastCalledWith('unavailable');
    expect(vi.getTimerCount()).toBe(0);

    lifecycle.destroy();
  });
});
