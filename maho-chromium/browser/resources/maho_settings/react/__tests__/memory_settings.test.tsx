import React, {act} from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import type {PageHandlerRemote, SettingValue} from '../../mojo.js';
import {ALL_PANE_DEFINITIONS} from '../../schema/panes.js';
import {DomainPaneContent} from '../domain_panes.js';
import type {MahoSettingsStore} from '../store.js';

Object.defineProperty(globalThis, 'IS_REACT_ACT_ENVIRONMENT', {
  configurable: true,
  value: true,
});

let container: HTMLDivElement;
let root: Root;

beforeEach(() => {
  container = document.createElement('div');
  document.body.appendChild(container);
  root = createRoot(container);
});

afterEach(() => {
  act(() => root.unmount());
  container.remove();
  vi.restoreAllMocks();
});

async function renderMemoryPane() {
  const settings: SettingValue[] = [];
  const handler = {
    setEpisodicMemory: vi.fn().mockResolvedValue({success: true}),
    forgetEpisodeSite: vi.fn().mockResolvedValue({deletedCount: 2n}),
  };
  const store = {getHandler: () => handler} as unknown as MahoSettingsStore;
  const pane = ALL_PANE_DEFINITIONS.find(item => item.contentKind === 'episodic-memory')!;
  await act(async () => {
    root.render(<DomainPaneContent pane={pane} settings={settings} store={store} />);
  });
  return {handler: handler as unknown as PageHandlerRemote};
}

function switchFor(name: string): HTMLButtonElement {
  const control = container.querySelector<HTMLButtonElement>(
      `[role="switch"][aria-label="${name}"]`);
  if (!control) throw new Error(`Missing switch: ${name}`);
  return control;
}

describe('episodic memory settings', () => {
  it('keeps memory off by default and sends enable update', async () => {
    const {handler} = await renderMemoryPane();
    expect(switchFor('Remember browsing episodes').getAttribute('aria-checked')).toBe('false');
    await act(async () => { switchFor('Remember browsing episodes').click(); });
    expect(handler.setEpisodicMemory).toHaveBeenCalledWith(true, false, []);
  });

  it('sends pause updates and adds and removes excluded domains', async () => {
    const {handler} = await renderMemoryPane();
    await act(async () => { switchFor('Remember browsing episodes').click(); });
    await act(async () => { switchFor('Pause episodic memory').click(); });
    expect(handler.setEpisodicMemory).toHaveBeenLastCalledWith(true, true, []);

    const domainInput = container.querySelector<HTMLInputElement>('[aria-label="Domain to exclude"]')!;
    await act(async () => {
      Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value')!.set!.call(domainInput, '*.Example.com');
      domainInput.dispatchEvent(new Event('input', {bubbles: true}));
    });
    await act(async () => { container.querySelector<HTMLButtonElement>('button:not([role="switch"])')?.click(); });
    expect(handler.setEpisodicMemory).toHaveBeenLastCalledWith(true, true, ['example.com']);
    const remove = container.querySelector<HTMLButtonElement>('[aria-label="Remove example.com"]')!;
    await act(async () => { remove.click(); });
    expect(handler.setEpisodicMemory).toHaveBeenLastCalledWith(true, true, []);
  });

  it('forgets a site and displays the deleted count or backend failure', async () => {
    const {handler} = await renderMemoryPane();
    const input = container.querySelector<HTMLInputElement>('[aria-label="Site to forget"]')!;
    await act(async () => {
      Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value')!.set!.call(input, 'https://example.com');
      input.dispatchEvent(new Event('input', {bubbles: true}));
    });
    const forget = [...container.querySelectorAll<HTMLButtonElement>('button')]
        .find(button => button.textContent?.includes('Forget site'))!;
    await act(async () => { forget.click(); });
    expect(handler.forgetEpisodeSite).toHaveBeenCalledWith('https://example.com');
    expect(container.textContent).toContain('Forgot 2 episodes.');

    vi.mocked(handler.forgetEpisodeSite).mockResolvedValue({deletedCount: -1n});
    await act(async () => { forget.click(); });
    expect(container.textContent).toContain('Could not forget episodes for that site.');
  });
});
