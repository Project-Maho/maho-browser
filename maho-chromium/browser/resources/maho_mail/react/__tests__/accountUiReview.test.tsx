import React from 'react';
import {act, cleanup, fireEvent, render, renderHook, screen} from '@testing-library/react';
import {afterEach, describe, expect, it, vi} from 'vitest';
import {MailAccountsPane} from '../../../maho_settings/react/mail_accounts';
import {useMailOnboardingController} from '../../../maho_common/react/mail/mail_onboarding_controller';
import {MAIL_APP_ONBOARDING_CONFIG, type MailOnboardingApi} from '../../../maho_common/react/mail/mail_onboarding_api';

// Only the unrelated Settings shell is replaced; account UI and controller are real.
vi.mock('../../../maho_settings/react/domain_panes.js', () => ({
  PaneShell: ({children}: React.PropsWithChildren) => <div>{children}</div>,
  SectionCard: ({children, action}: React.PropsWithChildren<{action: React.ReactNode}>) => <section>{action}{children}</section>,
}));
afterEach(() => { cleanup(); vi.restoreAllMocks(); });
const account = {id: 'account-1', email: 'qa@example.invalid', status: 'error'};
function deferred<T>() {
  let resolve!: (value: T) => void;
  let reject!: (error: Error) => void;
  const promise = new Promise<T>((yes, no) => { resolve = yes; reject = no; });
  return {promise, resolve, reject};
}
const ok = {ok: true, resultJson: '[]'};
function wire() {
  let rows = [account];
  const handler = {
    mailListAccounts: vi.fn(async () => ({ok: true, resultJson: JSON.stringify(rows)})),
    mailDeleteAccount: vi.fn(async (id: string) => { rows = rows.filter(row => row.id !== id); return ok; }),
    mailBeginOAuth: vi.fn(async (_provider: string) => ok),
    mailOAuthCancel: vi.fn(async (_state: string) => ({ok: true})),
    mailReconnectAccount: vi.fn(async (_id: string) => ok),
    mailAddAccount: vi.fn(async (_request: string) => ok),
    mailTestConnection: vi.fn(async (_request: string) => ({ok: false, resultJson: 'authentication failed'})),
  };
  const store = {getHandler: () => handler, getCallbackRouter: () => ({
    onMailAccountsChanged: {addListener: () => 1}, removeListener: () => {},
  })};
  return {handler, store: store as never, rows: () => rows};
}
async function settings() {
  const fixture = wire();
  await act(async () => { render(<MailAccountsPane pane={{} as never} store={fixture.store} />); });
  return fixture;
}
async function click(name: string | RegExp) {
  await act(async () => { fireEvent.click(screen.getByRole('button', {name})); });
}
async function imapForm() {
  await click('Add account');
  await click(/IMAP \/ other/);
  fireEvent.change(screen.getByLabelText('Email'), {target: {value: 'qa@example.invalid'}});
  fireEvent.change(screen.getByLabelText('App password'), {target: {value: 'OLD_SECRET_SENTINEL'}});
  fireEvent.change(screen.getByLabelText('IMAP host'), {target: {value: 'imap.example.invalid'}});
}

describe('account UI review', () => {
  it('ACC-04 cancel preserves account, acceptance removes the exact account once', async () => {
    const fixture = await settings();
    const confirm = vi.spyOn(window, 'confirm').mockImplementation(() => {
      expect(fixture.handler.mailDeleteAccount).not.toHaveBeenCalled();
      return false;
    });
    await click(`Remove ${account.email}`);
    expect(fixture.rows()).toEqual([account]);
    expect(fixture.handler.mailDeleteAccount).not.toHaveBeenCalled();
    confirm.mockReturnValue(true);
    await click(`Remove ${account.email}`);
    expect(fixture.handler.mailDeleteAccount).toHaveBeenCalledExactlyOnceWith(account.id);
    expect(fixture.rows()).toEqual([]);
  });

  it('ACC-04 pending removal disables repeats and failure retains the account', async () => {
    const fixture = await settings();
    vi.spyOn(window, 'confirm').mockReturnValue(true);
    let finish!: (value: typeof ok) => void;
    const pending = new Promise<typeof ok>(resolve => { finish = resolve; });
    fixture.handler.mailDeleteAccount.mockImplementation(() => pending);
    await click(`Remove ${account.email}`);
    expect(screen.getByRole('button', {name: `Remove ${account.email}`})).toBeDisabled();
    await click(`Remove ${account.email}`);
    expect(fixture.handler.mailDeleteAccount).toHaveBeenCalledTimes(1);
    await act(async () => { finish({ok: false, resultJson: 'failed'}); await pending; });
    expect(fixture.rows()).toEqual([account]);
    expect(screen.getByRole('button', {name: `Remove ${account.email}`})).toBeEnabled();
    expect(screen.getByRole('alert')).toBeInTheDocument();
  });

  it('ACC-07 Settings Back clears the password before returning to IMAP', async () => {
    await settings();
    await imapForm();
    await click('Back');
    await click(/IMAP \/ other/);
    expect(screen.getByLabelText('App password')).toHaveValue('');
  });

  it('ACC-08 failed Settings probe prevents account persistence', async () => {
    const fixture = await settings();
    await imapForm();
    await click('Connect account');
    expect(fixture.handler.mailAddAccount).not.toHaveBeenCalled();
    expect(fixture.handler.mailTestConnection).toHaveBeenCalledTimes(1);
    expect(JSON.parse(fixture.handler.mailTestConnection.mock.calls[0][0])).toEqual({
      imap_host: 'imap.example.invalid', imap_port: 993, imap_encryption: 'Tls',
      username: 'qa@example.invalid', auth_type: 'password', password: 'OLD_SECRET_SENTINEL',
    });
    expect(screen.getByLabelText('App password')).toBeInTheDocument();
    expect(screen.getByRole('button', {name: 'Connect account'})).toBeEnabled();
    expect(screen.getByRole('button', {name: 'Back'})).toBeEnabled();
    expect(screen.getByRole('alert')).toBeInTheDocument();
  });

  it('ACC-08 waits for probe success before persisting and clears successful credentials', async () => {
    const fixture = await settings();
    let finish!: (value: typeof ok) => void;
    const pending = new Promise<typeof ok>(resolve => { finish = resolve; });
    fixture.handler.mailTestConnection.mockImplementation(() => pending);
    await imapForm();
    await click('Connect account');
    expect(fixture.handler.mailTestConnection).toHaveBeenCalledTimes(1);
    expect(fixture.handler.mailAddAccount).not.toHaveBeenCalled();
    expect(screen.getByRole('button', {name: 'Back'})).toBeDisabled();
    await act(async () => { finish(ok); await pending; });
    expect(fixture.handler.mailAddAccount).toHaveBeenCalledTimes(1);
    expect(JSON.parse(fixture.handler.mailAddAccount.mock.calls[0][0])).toMatchObject({
      email: account.email, password: 'OLD_SECRET_SENTINEL', smtp_host: 'smtp.example.invalid',
    });
    await click('Add account');
    await click(/IMAP \/ other/);
    expect(screen.getByLabelText('App password')).toHaveValue('');
  });

  it.each(['probe', 'add'] as const)('ACC-08 %s transport rejection releases submission state', async stage => {
    const fixture = await settings();
    fixture.handler.mailTestConnection.mockResolvedValue(ok);
    const failing = stage === 'probe' ? fixture.handler.mailTestConnection : fixture.handler.mailAddAccount;
    failing.mockRejectedValue(new Error('transport disconnected'));
    await imapForm();
    await click('Connect account');
    expect(screen.getByRole('button', {name: 'Connect account'})).toBeEnabled();
    expect(screen.getByRole('button', {name: 'Back'})).toBeEnabled();
    expect(screen.getByRole('alert')).toBeInTheDocument();
    if (stage === 'probe') expect(fixture.handler.mailAddAccount).not.toHaveBeenCalled();
  });

  it.each(['oauth', 'reconnect'] as const)('ASYNC-S-%s deferred rejection shows error and releases controls', async stage => {
    const fixture = await settings();
    const pending = deferred<typeof ok>();
    const method = stage === 'oauth' ? fixture.handler.mailBeginOAuth : fixture.handler.mailReconnectAccount;
    method.mockImplementation(() => pending.promise);
    if (stage === 'oauth') { await click('Add account'); await click(/Gmail/); }
    else await click(`Reconnect ${account.email}`);
    expect(method).toHaveBeenCalledTimes(1);
    await act(async () => { pending.reject(new Error('transport disconnected')); await pending.promise.catch(() => {}); });
    expect(screen.getByRole('alert')).toBeInTheDocument();
    expect(screen.getByRole('button', {name: stage === 'oauth' ? /Gmail/ : `Reconnect ${account.email}`})).toBeEnabled();
  });

  it('ASYNC-S-late cancelled pending OAuth does not open authorization', async () => {
    const fixture = await settings();
    const pending = deferred<typeof ok>();
    fixture.handler.mailBeginOAuth.mockImplementation(() => pending.promise);
    const open = vi.spyOn(window, 'open').mockReturnValue(null);
    await click('Add account');
    await click(/Gmail/);
    await click('Cancel');
    await act(async () => { pending.resolve({ok: true, resultJson: JSON.stringify({auth_url: 'https://example.invalid/oauth', state: 'late-state'})}); await pending.promise; });
    expect(open).not.toHaveBeenCalled();
    expect(fixture.handler.mailOAuthCancel).toHaveBeenCalledExactlyOnceWith('late-state');
    expect(screen.getByRole('button', {name: 'Add account'})).toBeEnabled();
  });

  it.each(['testConnection', 'addAccount', 'beginOAuth'] as const)('ASYNC-C-%s deferred rejection resets action', async method => {
    const pending = deferred<never>();
    const transport = vi.fn(() => pending.promise);
    const hook = controller({[method]: transport});
    await act(async () => { await hook.result.current.providerActions.onSelectProvider('other'); });
    act(() => {
      const actions = hook.result.current.credentialsActions;
      actions.onEmailChange(account.email);
      actions.onPasswordChange('OLD_SECRET_SENTINEL');
      actions.onImapHostChange('imap.example.invalid');
      actions.onSmtpHostChange('smtp.example.invalid');
    });
    let outcome!: Promise<unknown>;
    act(() => {
      const model = hook.result.current;
      const operation = method === 'beginOAuth' ? model.providerActions.onSelectProvider('gmail')
        : method === 'testConnection' ? model.credentialsActions.onTestConnection() : model.credentialsActions.onAddAccount();
      outcome = operation.then(() => undefined, error => error);
    });
    expect(hook.result.current.busy).toBe(true);
    expect(transport).toHaveBeenCalledTimes(1);
    let rejection: unknown;
    await act(async () => { pending.reject(new Error('transport disconnected')); rejection = await outcome; });
    expect(hook.result.current.busy).toBe(false);
    expect(rejection).toBeUndefined();
    expect(hook.result.current.credentialsState.error).toBeTruthy();
    expect(hook.result.current.step).not.toBe('success');
  });

  function controller(overrides: Partial<MailOnboardingApi> = {}) {
    const api: MailOnboardingApi = {
      listAccounts: async () => ok, addAccount: vi.fn(async () => ok), testConnection: async () => ok,
      deleteAccount: async () => ok, beginOAuth: async () => ({ok: true, state: 'state', errorJson: ''}),
      cancelOAuth: async () => true, subscribeAccountsChanged: () => () => {}, onComplete: () => {}, ...overrides,
    };
    return renderHook(() => useMailOnboardingController({api, accounts: [], onAccountsChange: () => {}, config: MAIL_APP_ONBOARDING_CONFIG}));
  }
  it.each(['back', 'provider', 'success'] as const)('ACC-07 shared controller clears credentials on %s', async reset => {
    const hook = controller();
    await act(async () => { await hook.result.current.providerActions.onSelectProvider('other'); });
    act(() => {
      const actions = hook.result.current.credentialsActions;
      actions.onEmailChange('qa@example.invalid');
      actions.onPasswordChange('OLD_SECRET_SENTINEL');
      actions.onImapHostChange('imap.example.invalid');
      actions.onSmtpHostChange('smtp.example.invalid');
    });
    await act(async () => {
      if (reset === 'back') hook.result.current.credentialsActions.onBack();
      if (reset === 'provider') await hook.result.current.providerActions.onSelectProvider('yahoo');
      if (reset === 'success') await hook.result.current.credentialsActions.onAddAccount();
    });
    expect(hook.result.current.credentialsState.password).toBe('');
  });
});
