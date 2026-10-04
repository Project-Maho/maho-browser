import React, {act} from 'react';
import {createRoot} from 'react-dom/client';
import {expect, it, vi} from 'vitest';
import {MahoSettingsStore} from '../store';
import {SelectShell} from '../domain_panes';

Object.assign(globalThis, {IS_REACT_ACT_ENVIRONMENT: true});

it('discards invoices completed after an account replacement', async () => {
  const store = new MahoSettingsStore();
  const handler = store.getHandler();
  let resolve!: (value: unknown) => void;
  Object.assign(handler, {
    getAccountStatus: vi.fn().mockResolvedValue({status: {signedIn: true, userId: 'A'}}),
    getInvoices: () => new Promise(r => { resolve = r; }),
  });
  await store.refreshAccountStatus();
  const request = store.refreshInvoices();
  Object.assign(handler, {getAccountStatus: async () => ({status: {signedIn: true, userId: 'B'}})});
  await store.refreshAccountStatus();
  resolve({invoices: [{id: 'private-A', pdfUrl: 'https://receipt/A'}]});
  await request;
  expect(store.getSnapshot().invoices).toEqual([]);
  store.dispose();
});

it('opens and selects an explicitly empty option without crashing', async () => {
  const container = document.createElement('div');
  document.body.append(container);
  const root = createRoot(container);
  const change = vi.fn();
  Element.prototype.scrollIntoView = vi.fn();
  Element.prototype.hasPointerCapture = () => false;
  try {
    await act(async () => root.render(<SelectShell ariaLabel="Provider" value="a" onChange={change}
      options={[{value: '', label: 'None'}, {value: 'a', label: 'A'}]} />));
    await act(async () => container.querySelector('button')!.dispatchEvent(
      new KeyboardEvent('keydown', {key: 'ArrowDown', bubbles: true})));
    const empty = [...document.querySelectorAll<HTMLElement>('[role="option"]')].find(el => el.textContent === 'None');
    expect(empty).toBeDefined();
    await act(async () => empty!.click());
    expect(change).toHaveBeenCalledWith('');
  } finally {
    act(() => root.unmount());
    container.remove();
  }
});
