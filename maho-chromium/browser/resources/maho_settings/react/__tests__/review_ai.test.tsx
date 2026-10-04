import React, {act} from 'react';
import {createRoot} from 'react-dom/client';
import {expect, it, vi} from 'vitest';
import {DomainPaneContent} from '../domain_panes';
import {ALL_PANE_DEFINITIONS} from '../../schema/panes';

Object.assign(globalThis, {IS_REACT_ACT_ENVIRONMENT: true});
async function setup(provider = 'maho-managed', fetchModels = vi.fn().mockResolvedValue({modelIds: [], fromCache: false})) {
  let settings = {provider, model: '', hasRelaySession: false};
  const handler = {
    getAISettings: vi.fn(async () => ({settings})), fetchProviderModels: fetchModels,
    setAIProvider: vi.fn(async (provider: string) => {settings = {...settings, provider};}),
  };
  const snapshot = {settings: []};
  const store = {getSnapshot: () => snapshot, subscribe: () => () => {}, getHandler: () => handler,
    getCallbackRouter: () => ({providerModelsRefreshed: {addListener: () => 1}, removeListener: vi.fn()}),
    selectPane: vi.fn(), refreshAccountStatus: vi.fn()};
  const container = document.createElement('div'); document.body.append(container);
  const root = createRoot(container);
  await act(async () => root.render(<DomainPaneContent pane={ALL_PANE_DEFINITIONS.find(p => p.contentKind === 'ai')!}
    settings={[]} store={store as any} />));
  return {handler, store, container, cleanup() {act(() => root.unmount()); container.remove();}};
}
it('keeps a failed AI mutation visible after reconciliation', async () => {
  const h = await setup();
  try {
    h.handler.setAIProvider.mockRejectedValueOnce(new Error('failed'));
    await act(async () => (h.container.querySelectorAll('input[type="radio"]')[1] as HTMLInputElement).click());
    expect(h.handler.setAIProvider).toHaveBeenCalled();
    expect(h.container.querySelector('section > p')).not.toBeNull();
  } finally {h.cleanup();}
});
it('routes the managed Sign In action to authentication', async () => {
  const h = await setup();
  try {
    const button = [...h.container.querySelectorAll('button')].find(b => b.textContent === 'Sign In')!;
    await act(async () => button.click());
    expect(h.store.selectPane).toHaveBeenCalledWith('account');
  } finally {h.cleanup();}
});
it('keeps the new provider models when the old request finishes last', async () => {
  const pending = new Map<string, (v: any) => void>();
  const h = await setup('maho-managed', vi.fn((provider: string) => new Promise(resolve => {pending.set(provider, resolve);} )));
  Element.prototype.scrollIntoView = vi.fn();
  Element.prototype.hasPointerCapture = () => false;
  try {
    await act(async () => (h.container.querySelectorAll('input[type="radio"]')[1] as HTMLInputElement).click());
    expect(pending.has('openai')).toBe(true);
    await act(async () => pending.get('openai')!({modelIds: ['B-model'], fromCache: false}));
    await act(async () => pending.get('maho-managed')!({modelIds: ['A-model'], fromCache: false}));
    const model = h.container.querySelector<HTMLButtonElement>('button[aria-label="Model"]');
    expect(model).not.toBeNull();
    await act(async () => model!.dispatchEvent(new KeyboardEvent('keydown', {key: 'ArrowDown', bubbles: true})));
    const options = [...document.querySelectorAll('[role="option"]')].map(el => el.textContent);
    expect(options).toContain('B-model');
    expect(options).not.toContain('A-model');
  } finally {h.cleanup();}
});
