import React, {act, useSyncExternalStore} from 'react';
import {createRoot} from 'react-dom/client';
import {expect, it, vi} from 'vitest';
import {MahoWelcomeStore} from '../store.js';
import {MailSetupContent, MailSetupSidebar} from '../pages/mail-setup.js';

const transport = vi.hoisted(() => {
  const stringCall = <T,>(result: T) => vi.fn(async (value: string) => {
    if (typeof value !== 'string') throw new TypeError('Non-string Mojo string field');
    return result;
  });
  const listeners = new Map<number, () => void>();
  const handler = {
    $: {bindNewPipeAndPassReceiver: vi.fn()},
    mailListAccounts: vi.fn(async () => ({ok: true, resultJson: '[]'})),
    mailBeginOAuth: stringCall({ok: true, state: 'gmail-state', errorJson: ''}),
    mailAddAccount: stringCall({ok: true, resultJson: '{}'}),
    mailTestConnection: stringCall({ok: true, resultJson: '{}'}),
    mailDeleteAccount: stringCall({ok: true, resultJson: '{}'}),
    mailOAuthCancel: stringCall({accepted: true}),
    getAiProviderConfigured: vi.fn(async () => ({configured: true})),
    setTranslationProvider: stringCall({ok: true}),
  };
  return {handler, listeners, router: {
    $: {bindNewPipeAndPassRemote: vi.fn()},
    onImportProgress: {addListener: vi.fn()},
    onMailAccountsChanged: {addListener: vi.fn((cb: () => void) => {
      listeners.set(1, cb);
      return 1;
    })},
    removeListener: vi.fn((id: number) => listeners.delete(id)),
  }};
});
vi.mock('../../maho_welcome.mojom-webui.js', async importOriginal => ({
  ...await importOriginal<object>(),
  PageHandlerRemote: vi.fn(() => transport.handler),
  PageCallbackRouter: vi.fn(() => transport.router),
  PageHandlerFactory: {getRemote: () => ({createPageHandler: vi.fn()})},
}));
Object.defineProperty(globalThis, 'IS_REACT_ACT_ENVIRONMENT', {configurable: true, value: true});

it('renders the real Welcome mail adapter, starts Gmail OAuth, and publishes account events', async () => {
  const store = new MahoWelcomeStore();
  const container = document.createElement('div');
  document.body.append(container);
  const root = createRoot(container);
  function Step() {
    const snapshot = useSyncExternalStore(cb => store.subscribe(cb), () => store.getSnapshot());
    return <><MailSetupContent store={store} snapshot={snapshot} /><MailSetupSidebar snapshot={snapshot} /></>;
  }
  try {
    await act(async () => { root.render(<Step />); });
    expect(store.getSnapshot().mailAccounts).toEqual([]);
    expect(transport.handler.mailListAccounts).toHaveBeenCalledWith();
    const gmail = Array.from(container.querySelectorAll('button')).find(button => button.textContent?.includes('Gmail'));
    expect(gmail).toBeDefined();
    await act(async () => { gmail!.click(); });
    expect(transport.handler.mailBeginOAuth).toHaveBeenCalledExactlyOnceWith('gmail');
    expect(container.querySelector('[role="alert"]')).toBeNull();

    const accounts = [{id: 'gmail-account', email: 'welcome@example.test'}];
    transport.handler.mailListAccounts.mockResolvedValue({ok: true, resultJson: JSON.stringify(accounts)});
    expect(transport.listeners.size).toBe(1);
    await act(async () => { await transport.listeners.get(1)!(); });
    expect(store.getSnapshot().mailAccounts).toEqual(accounts);
    expect(container.textContent).toContain(accounts[0].email);
    expect(transport.handler.getAiProviderConfigured).toHaveBeenCalledOnce();
    act(() => store.setAuthEmail('unrelated-state-change@example.test'));
    expect(store.getSnapshot().mailAccounts).toEqual(accounts);

    await expect(store.mailAddAccount('{"email":"imap@example.test"}')).resolves.toEqual({ok: true, resultJson: '{}'});
    expect(transport.handler.mailAddAccount).toHaveBeenCalledExactlyOnceWith('{"email":"imap@example.test"}');
    await store.mailTestConnection('{}');
    expect(transport.handler.mailTestConnection).toHaveBeenCalledExactlyOnceWith('{}');
    await store.mailDeleteAccount('gmail-account');
    expect(transport.handler.mailDeleteAccount).toHaveBeenCalledExactlyOnceWith('gmail-account');
    await expect(store.mailOAuthCancel('gmail-state')).resolves.toBe(true);
    expect(transport.handler.mailOAuthCancel).toHaveBeenCalledExactlyOnceWith('gmail-state');
    await expect(store.setTranslationProvider('byok')).resolves.toBe(true);
    expect(transport.handler.setTranslationProvider).toHaveBeenCalledExactlyOnceWith('byok');
  } finally {
    await act(async () => root.unmount());
    container.remove();
    store.dispose();
  }
  expect(transport.listeners.size).toBe(0);
  expect(transport.router.removeListener).toHaveBeenCalledWith(1);
});
