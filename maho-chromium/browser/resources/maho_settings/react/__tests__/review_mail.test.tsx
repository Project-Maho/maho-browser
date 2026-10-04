import React, {act} from 'react';
import {createRoot} from 'react-dom/client';
import {expect, it, vi} from 'vitest';
import {MailOnboardingLifecycle} from '../../../maho_common/react/mail/mail_onboarding_lifecycle';
import {useMailOnboardingController} from '../../../maho_common/react/mail/mail_onboarding_controller';
import type {MailOnboardingViewModel} from '../../../maho_common/react/mail/mail_onboarding_controller_types';

Object.assign(globalThis, {IS_REACT_ACT_ENVIRONMENT: true});

it('cancels OAuth returned after destruction', async () => {
  let resolve!: (v: any) => void;
  const cancelOAuth = vi.fn().mockResolvedValue(true);
  const lifecycle = new MailOnboardingLifecycle({beginOAuth: () => new Promise(r => {resolve = r;}), cancelOAuth} as any, {} as any);
  const begin = lifecycle.beginOAuth('gmail');
  lifecycle.destroy();
  resolve({ok: true, state: 'late-state'});
  const result = await begin;
  expect(lifecycle.getActiveOAuthState()).toBeNull();
  expect(cancelOAuth).toHaveBeenCalledWith('late-state');
  expect(result.ok).toBe(false);
});

it.each(['add-refresh', 'delete'] as const)('recovers from rejected %s without repeating a completed mutation', async action => {
  const api = {
    listAccounts: vi.fn().mockResolvedValue({ok: true, resultJson: '[]'}),
    subscribeAccountsChanged: () => () => {},
    addAccount: vi.fn().mockResolvedValue({ok: true, resultJson: '{}'}),
    deleteAccount: vi.fn().mockRejectedValue(new Error('transport')),
  };
  const container = document.createElement('div');
  const root = createRoot(container);
  let vm!: MailOnboardingViewModel;
  function App() {
    vm = useMailOnboardingController({api, accounts: [], onAccountsChange: vi.fn(),
      config: {steps: ['provider', 'credentials', 'success']}, confirmDeleteAccount: async () => true} as any);
    return <div data-step={vm.step} data-busy={vm.busy}>{vm.credentialsState.error}</div>;
  }
  try {
    await act(async () => root.render(<App />));
    if (action === 'add-refresh') {
      await act(async () => {
        await vm.providerActions.onSelectProvider('other');
        vm.credentialsActions.onEmailChange('user@example.com');
        vm.credentialsActions.onPasswordChange('password');
        vm.credentialsActions.onImapHostChange('imap.example.com');
        vm.credentialsActions.onSmtpHostChange('smtp.example.com');
      });
      api.listAccounts.mockRejectedValueOnce(new Error('refresh transport'));
      await act(async () => vm.credentialsActions.onAddAccount());
      expect(vm.step).toBe('success');
      expect(api.addAccount).toHaveBeenCalledTimes(1);
    } else {
      await act(async () => {
        await vm.onDeleteAccount({id: 'A', email: 'user@example.com'} as any);
      });
      expect(vm.busy).toBe(false);
      expect(vm.credentialsState.error).toBeTruthy();
    }
  } finally {act(() => root.unmount());}
});
