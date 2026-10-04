import React from 'react';
import {act, cleanup, fireEvent, render, screen} from '@testing-library/react';
import {afterEach, expect, it, vi} from 'vitest';

const mojo = vi.hoisted(() => {
  let accounts: {id: string; email: string}[] = [];
  const listeners = new Map<number, () => unknown>();
  let nextId = 1;
  const event = {addListener: vi.fn(() => nextId++)};
  return {
    handler: {
      getBrowserUiPrefs: async () => ({prefsJson: '{}'}),
      listAccounts: async () => ({ok: true, resultJson: JSON.stringify(accounts)}),
      beginOAuth: vi.fn(async (provider: string, reauthorizeAccountId: string) => {
        // Preserve Mojo's non-nullable string serialization boundary.
        if (typeof provider !== 'string' || typeof reauthorizeAccountId !== 'string') {
          throw new Error('Unxpected non-string value for string field.');
        }
        return {ok: true, errorJson: '', state: 'gmail-state'};
      }),
      cancelOAuth: async () => ({accepted: true}),
      closeOnboarding: vi.fn(),
    },
    callbackRouter: {
      onLifecycleChanged: event,
      onBrowserUiPrefsChanged: event,
      onAccountsChanged: {addListener: (cb: () => unknown) => {
        const id = nextId++;
        listeners.set(id, cb);
        return id;
      }},
      removeListener: (id: number) => listeners.delete(id),
    },
    async completeAccount() {
      accounts = [{id: 'gmail-account', email: 'user@example.test'}];
      await Promise.all([...listeners.values()].map(cb => cb()));
    },
  };
});
vi.mock('../mojo_client.js', () => ({...mojo, closeMojoClient: vi.fn()}));
vi.mock('../i18n', () => ({}));
vi.mock('../hooks/useTheme', () => ({useTheme: vi.fn()}));
vi.mock('../hooks/useSettings', () => ({useSettings: () => ({sidebarCollapsed: false})}));
vi.mock('../../../maho_common/react/mail/mail_onboarding_sidebar.js', () => ({MailOnboardingSidebar: () => null}));
vi.mock('../components/ui/CommandPalette', () => ({CommandPalette: () => null}));
vi.mock('../../../maho_common/react/mail/mail_onboarding_content.js', async () => {
  const {useMailOnboardingController} = await import('../../../maho_common/react/mail/mail_onboarding_controller.js');
  return {MailOnboardingContent: (props: Parameters<typeof useMailOnboardingController>[0]) => {
    const model = useMailOnboardingController(props);
    return <><button onClick={() => model.providerActions.onSelectProvider('gmail')}>Gmail</button><output>{model.step}</output></>;
  }};
});

import {App} from '../app';

afterEach(cleanup);
it('starts Gmail through the Mail adapter and reconciles the added account', async () => {
  await act(async () => { render(<App renderMailClient={({accounts}) => <div>{accounts[0]?.email}</div>} />); });
  await act(async () => { fireEvent.click(screen.getByRole('button', {name: 'Gmail'})); });
  expect(mojo.handler.beginOAuth).toHaveBeenCalledExactlyOnceWith('gmail', '');
  expect(screen.getByRole('status').textContent).toBe('oauth');
  await act(async () => { await mojo.completeAccount(); });
  expect(screen.getByText('user@example.test')).toBeTruthy();
});
