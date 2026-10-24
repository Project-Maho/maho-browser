import React, {act} from 'react';
import {createRoot} from 'react-dom/client';
import {expect, it, vi} from 'vitest';
import {MailSecurityPane} from '../mail_security';
import {ALL_PANE_DEFINITIONS} from '../../schema/panes';
Object.assign(globalThis, {IS_REACT_ACT_ENVIRONMENT: true});
it('does not render keys from the previous account after a late response', async () => {
  const pending = new Map<string, (v: any) => void>();
  const handler = {
    mailListAccounts: async () => ({ok: true, resultJson: JSON.stringify([{id: 'A', email: 'a@example.com'}, {id: 'B', email: 'b@example.com'}])}),
    mailListPgpKeys: (id: string) => new Promise(resolve => pending.set(id, resolve)),
    mailListSmimeIdentities: async () => ({ok: true, resultJson: '[]'}),
  };
  const router = {onMailSecurityChanged: {addListener: () => 1}, onMailAccountsChanged: {addListener: () => 2}, removeListener: vi.fn()};
  const store = {getHandler: () => handler, getCallbackRouter: () => router};
  const container = document.createElement('div'); document.body.append(container);
  const root = createRoot(container);
  Element.prototype.scrollIntoView = vi.fn(); Element.prototype.hasPointerCapture = () => false;
  try {
    await act(async () => root.render(<MailSecurityPane pane={ALL_PANE_DEFINITIONS.find(p => p.contentKind === 'mail-security')!} store={store as any} />));
    await act(async () => container.querySelector('button[role="combobox"]')!.dispatchEvent(new KeyboardEvent('keydown', {key: 'ArrowDown', bubbles: true})));
    await act(async () => [...document.querySelectorAll<HTMLElement>('[role="option"]')].find(el => el.textContent === 'b@example.com')!.click());
    const result = (id: string) => ({ok: true, resultJson: JSON.stringify([{key_id: id, email: `${id}-key@example.com`, fingerprint: id}])});
    await act(async () => pending.get('B')!(result('B')));
    await act(async () => pending.get('A')!(result('A')));
    expect(container.textContent).toContain('B-key@example.com');
    expect(container.textContent).not.toContain('A-key@example.com');
  } finally {act(() => root.unmount()); container.remove();}
});

it('clears import secrets on cancel and drops the success banner on a later failure', async () => {
  const handler = {
    mailListAccounts: async () => ({ok: true, resultJson: JSON.stringify([{id: 'A', email: 'a@example.com'}])}),
    mailListPgpKeys: async () => ({ok: true, resultJson: '[]'}),
    mailListSmimeIdentities: async () => ({ok: true, resultJson: '[]'}),
    mailImportPgpKey: vi.fn().mockResolvedValueOnce({ok: true}).mockResolvedValueOnce({ok: false, error: 'bad key'}),
  };
  const router = {onMailSecurityChanged: {addListener: () => 1}, onMailAccountsChanged: {addListener: () => 2}, removeListener: vi.fn()};
  const store = {getHandler: () => handler, getCallbackRouter: () => router};
  const container = document.createElement('div'); document.body.append(container);
  const root = createRoot(container);
  const button = (text: string) => [...container.querySelectorAll<HTMLButtonElement>('button')].find(b => b.textContent === text)!;
  const type = (el: HTMLInputElement, value: string) => {
    Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value')!.set!.call(el, value);
    el.dispatchEvent(new Event('input', {bubbles: true}));
  };
  try {
    await act(async () => root.render(<MailSecurityPane pane={ALL_PANE_DEFINITIONS.find(p => p.contentKind === 'mail-security')!} store={store as any} />));
    await act(async () => button('Import Key').click());
    await act(async () => type(container.querySelector<HTMLInputElement>('#pgp-import-passphrase')!, 'hunter2'));
    await act(async () => button('Cancel').click());
    await act(async () => button('Import Key').click());
    expect(container.querySelector<HTMLInputElement>('#pgp-import-passphrase')!.value).toBe('');

    await act(async () => type(container.querySelector<HTMLInputElement>('#pgp-import-keyblock')!, 'KEY'));
    await act(async () => button('Import').click());
    expect(container.textContent).toContain('PGP key imported.');
    await act(async () => button('Import Key').click());
    await act(async () => type(container.querySelector<HTMLInputElement>('#pgp-import-keyblock')!, 'KEY2'));
    await act(async () => button('Import').click());
    expect(container.textContent).toContain('bad key');
    expect(container.textContent).not.toContain('PGP key imported.');
  } finally {act(() => root.unmount()); container.remove();}
});

it('resets key forms when a reload drops the selected account and ignores the old account result', async () => {
  let accounts = [{id: 'A', email: 'a@example.com'}, {id: 'B', email: 'b@example.com'}];
  let finishGenerate: (v: {ok: boolean}) => void = () => {};
  let accountsChanged: () => void = () => {};
  const handler = {
    mailListAccounts: async () => ({ok: true, resultJson: JSON.stringify(accounts)}),
    mailListPgpKeys: async () => ({ok: true, resultJson: '[]'}),
    mailListSmimeIdentities: async () => ({ok: true, resultJson: '[]'}),
    mailGeneratePgpKey: vi.fn(() => new Promise<{ok: boolean}>(resolve => { finishGenerate = resolve; })),
  };
  const router = {
    onMailSecurityChanged: {addListener: () => 1},
    onMailAccountsChanged: {addListener: (cb: () => void) => { accountsChanged = cb; return 2; }},
    removeListener: vi.fn(),
  };
  const store = {getHandler: () => handler, getCallbackRouter: () => router};
  const container = document.createElement('div'); document.body.append(container);
  const root = createRoot(container);
  const button = (text: string) => [...container.querySelectorAll<HTMLButtonElement>('button')].find(b => b.textContent === text);
  try {
    await act(async () => root.render(<MailSecurityPane pane={ALL_PANE_DEFINITIONS.find(p => p.contentKind === 'mail-security')!} store={store as any} />));
    await act(async () => button('Generate Key')!.click());
    await act(async () => button('Generate')!.click());
    expect(handler.mailGeneratePgpKey).toHaveBeenCalledWith('A', 'a@example.com', '');
    // Account A disappears; the pane falls back to B without the dropdown.
    accounts = [{id: 'B', email: 'b@example.com'}];
    await act(async () => accountsChanged());
    await act(async () => finishGenerate({ok: true}));
    expect(container.textContent).not.toContain('PGP key generated.');
    expect(container.textContent).not.toContain('Generating');
    await act(async () => button('Generate Key')!.click());
    expect([...container.querySelectorAll<HTMLInputElement>('input')].some(i => i.value === 'b@example.com')).toBe(true);
    expect([...container.querySelectorAll<HTMLInputElement>('input')].some(i => i.value === 'a@example.com')).toBe(false);
  } finally {act(() => root.unmount()); container.remove();}
});
