import '@testing-library/jest-dom/vitest';

import {act, fireEvent, render, screen} from '@testing-library/react';
import {beforeEach, describe, expect, it, vi} from 'vitest';

import type {MailAccount} from '../../maho_common/react/mail/mail_onboarding_api.js';

type LifecycleListener = (state: string, generation: bigint) => void;

const mojo = vi.hoisted(() => {
  let lifecycleListener: LifecycleListener | null = null;
  let nextListenerId = 1;
  return {
    handler: {
      getBrowserUiPrefs: vi.fn(async () => ({prefsJson: '{}'})),
      listAccounts: vi.fn(),
      closeOnboarding: vi.fn(),
      addAccount: vi.fn(),
      testConnection: vi.fn(),
      deleteAccount: vi.fn(),
      beginOAuth: vi.fn(),
      cancelOAuth: vi.fn(),
    },
    callbackRouter: {
      onLifecycleChanged: {
        addListener: vi.fn((listener: LifecycleListener) => {
          lifecycleListener = listener;
          return nextListenerId++;
        }),
      },
      onBrowserUiPrefsChanged: {addListener: vi.fn(() => nextListenerId++)},
      onAccountsChanged: {addListener: vi.fn(() => nextListenerId++)},
      removeListener: vi.fn(),
    },
    emitLifecycle(state: string, generation: bigint) {
      if (!lifecycleListener) throw new Error('Lifecycle listener is not bound.');
      lifecycleListener(state, generation);
    },
    closeMojoClient: vi.fn(),
    reset() {
      lifecycleListener = null;
      nextListenerId = 1;
      mojo.closeMojoClient.mockClear();
    },
  };
});

vi.mock('./mojo_client.js', () => ({
  handler: mojo.handler,
  callbackRouter: mojo.callbackRouter,
  closeMojoClient: mojo.closeMojoClient,
}));
vi.mock('./i18n', () => ({}));
vi.mock('./hooks/useTheme', () => ({useTheme: vi.fn()}));
vi.mock('./hooks/useSettings', () => ({
  useSettings: () => ({sidebarCollapsed: false}),
}));
vi.mock('../../maho_common/react/mail/mail_onboarding_sidebar.js', () => ({
  MailOnboardingSidebar: () => <div>Account setup</div>,
}));
vi.mock('../../maho_common/react/mail/mail_onboarding_content.js', () => ({
  MailOnboardingContent: () => <div>Choose your email provider</div>,
}));
vi.mock('./components/ui/CommandPalette', () => ({
  CommandPalette: ({open}: {open: boolean}) => open ? <div>Mail commands</div> : null,
}));

import {App} from './app';

const OLD_ACCOUNT: MailAccount = {id: 'old-account', email: 'secret@example.test'};
const NEW_ACCOUNT: MailAccount = {id: 'new-account', email: 'fresh@example.test'};

function accountsResult(accounts: readonly MailAccount[]) {
  return {ok: true, resultJson: JSON.stringify(accounts)};
}

function deferred<T>() {
  let resolve!: (value: T) => void;
  const promise = new Promise<T>((resolver) => {
    resolve = resolver;
  });
  return {promise, resolve};
}

function renderLifecycleApp() {
  return render(
    <App
      renderMailClient={({accounts}) => (
        <article>
          <h1>Sensitive message</h1>
          <p>{accounts[0]?.email}</p>
        </article>
      )}
    />,
  );
}

describe('rendered Mail lifecycle acceptance', () => {
  beforeEach(() => {
    mojo.reset();
    vi.clearAllMocks();
    mojo.handler.getBrowserUiPrefs.mockResolvedValue({prefsJson: '{}'});
  });

  it('clears a rendered sensitive message immediately when Mail is disabled', async () => {
    mojo.handler.listAccounts.mockResolvedValue(accountsResult([OLD_ACCOUNT]));
    renderLifecycleApp();

    expect(await screen.findByText('Sensitive message')).toBeInTheDocument();
    act(() => mojo.emitLifecycle('disabled', 1n));

    expect(screen.queryByText('Sensitive message')).not.toBeInTheDocument();
    expect(screen.queryByText(OLD_ACCOUNT.email)).not.toBeInTheDocument();
    expect(screen.getByText('Mail is turned off')).toBeInTheDocument();
  });

  it('rebinds account state to a fresh generation after re-enable', async () => {
    const rebound = deferred<ReturnType<typeof accountsResult>>();
    mojo.handler.listAccounts
      .mockResolvedValueOnce(accountsResult([OLD_ACCOUNT]))
      .mockReturnValueOnce(rebound.promise);
    renderLifecycleApp();
    expect(await screen.findByText(OLD_ACCOUNT.email)).toBeInTheDocument();

    act(() => mojo.emitLifecycle('disabled', 1n));
    act(() => mojo.emitLifecycle('starting', 2n));
    act(() => mojo.emitLifecycle('ready', 2n));
    expect(screen.queryByText(OLD_ACCOUNT.email)).not.toBeInTheDocument();

    await act(async () => rebound.resolve(accountsResult([NEW_ACCOUNT])));
    expect(await screen.findByText(NEW_ACCOUNT.email)).toBeInTheDocument();
    expect(mojo.handler.listAccounts).toHaveBeenCalledTimes(2);
  });

  it('recovers from a helper crash through the rendered Retry action', async () => {
    mojo.handler.listAccounts
      .mockResolvedValueOnce(accountsResult([]))
      .mockResolvedValueOnce(accountsResult([NEW_ACCOUNT]));
    renderLifecycleApp();
    await screen.findByText('Choose your email provider');

    act(() => mojo.emitLifecycle('failed', 3n));
    expect(screen.getByRole('alert')).toHaveTextContent('Mail could not start');

    fireEvent.click(screen.getByRole('button', {name: 'Retry'}));
    expect(await screen.findByText(NEW_ACCOUNT.email)).toBeInTheDocument();
  });

  it('renders a visible Skip action that closes onboarding', async () => {
    mojo.handler.listAccounts.mockResolvedValue(accountsResult([]));
    renderLifecycleApp();

    fireEvent.click(await screen.findByRole('button', {name: 'Skip for now'}));
    expect(mojo.handler.closeOnboarding).toHaveBeenCalledTimes(1);
  });

  it('keeps onboarding commands reachable from the keyboard', async () => {
    mojo.handler.listAccounts.mockResolvedValue(accountsResult([]));
    renderLifecycleApp();
    await screen.findByText('Choose your email provider');

    fireEvent.keyDown(window, {key: 'k', ctrlKey: true});
    expect(screen.getByText('Mail commands')).toBeInTheDocument();
  });

  it('renders setup recovery and retries the failed account load', async () => {
    mojo.handler.listAccounts
      .mockResolvedValueOnce({ok: false, resultJson: 'mail helper unavailable'})
      .mockResolvedValueOnce(accountsResult([NEW_ACCOUNT]));
    renderLifecycleApp();

    expect(await screen.findByRole('alert')).toHaveTextContent(
      'Mail setup could not load your accounts.',
    );
    fireEvent.click(screen.getByRole('button', {name: 'Retry setup'}));

    expect(await screen.findByText(NEW_ACCOUNT.email)).toBeInTheDocument();
  });

  it('does not let a stale account completion repopulate cleared state', async () => {
    const staleLoad = deferred<ReturnType<typeof accountsResult>>();
    mojo.handler.listAccounts.mockReturnValue(staleLoad.promise);
    renderLifecycleApp();

    act(() => mojo.emitLifecycle('disabled', 4n));
    await act(async () => staleLoad.resolve(accountsResult([OLD_ACCOUNT])));

    expect(screen.getByText('Mail is turned off')).toBeInTheDocument();
    expect(screen.queryByText('Sensitive message')).not.toBeInTheDocument();
    expect(screen.queryByText(OLD_ACCOUNT.email)).not.toBeInTheDocument();
  });
});
