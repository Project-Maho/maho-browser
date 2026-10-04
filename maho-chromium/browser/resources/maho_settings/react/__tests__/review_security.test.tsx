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
