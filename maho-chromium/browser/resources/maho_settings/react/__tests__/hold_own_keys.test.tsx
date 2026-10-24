// Copyright 2026 Maho Browser. All rights reserved.

import React, {act} from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import {
  HOLD_OWN_KEYS_WARNING,
  HoldOwnKeysSection,
} from '../domain_panes.js';
import type {ClientHeldKeysResult} from './mojo_stub.js';

Object.defineProperty(globalThis, 'IS_REACT_ACT_ENVIRONMENT', {
  configurable: true,
  value: true,
});

const PHRASE = Array.from({length: 24}, (_, i) => `word${i + 1}`).join(' ');

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
});

function render(result: ClientHeldKeysResult, relaySwitchPending = false) {
  const handler = {enableClientHeldKeys: vi.fn().mockResolvedValue({result})};
  const onSwitched = vi.fn();
  act(() => {
    root.render(<HoldOwnKeysSection
      handler={handler as never}
      onSwitched={onSwitched}
      relaySwitchPending={relaySwitchPending} />);
  });
  return {handler, onSwitched};
}

function button(label: string): HTMLButtonElement {
  const match = [...container.querySelectorAll('button')]
      .find(candidate => candidate.textContent === label);
  if (!match) throw new Error(`missing button ${label}`);
  return match;
}

async function click(element: HTMLElement): Promise<void> {
  await act(async () => {
    element.dispatchEvent(new MouseEvent('click', {bubbles: true}));
    await Promise.resolve();
  });
}

describe('Settings > Sync "Hold my own keys"', () => {
  it('warns before switching and calls core only after confirmation', async () => {
    const {handler} = render(
        {success: true, recoveryPhrase: PHRASE, roomId: 'room-2', errorMessage: ''});

    await click(button('Hold my own keys'));
    expect(handler.enableClientHeldKeys).not.toHaveBeenCalled();
    expect(container.textContent).toContain(HOLD_OWN_KEYS_WARNING);

    await click(button('Cancel'));
    expect(handler.enableClientHeldKeys).not.toHaveBeenCalled();
    expect(container.textContent).not.toContain(HOLD_OWN_KEYS_WARNING);
  });

  it('shows the 24-word recovery phrase after a successful switch', async () => {
    const {handler, onSwitched} = render(
        {success: true, recoveryPhrase: PHRASE, roomId: 'room-2', errorMessage: ''});

    await click(button('Hold my own keys'));
    await click(button('Switch to my own keys'));

    expect(handler.enableClientHeldKeys).toHaveBeenCalledTimes(1);
    const phrase = container.querySelector('[data-testid="hold-own-keys-phrase"]');
    expect(phrase?.textContent?.split(' ')).toHaveLength(24);
    expect(container.querySelector('[role="alert"]')).toBeNull();
    // The phrase stays on screen; no refresh that would unmount it.
    expect(onSwitched).not.toHaveBeenCalled();
  });

  it('keeps escrow mode and shows the error when core refuses', async () => {
    const {onSwitched} = render({
      success: false,
      recoveryPhrase: '',
      roomId: '',
      errorMessage: 'Could not switch to your own keys. Sync keeps working as before.',
    });

    await click(button('Hold my own keys'));
    await click(button('Switch to my own keys'));

    expect(container.querySelector('[data-testid="hold-own-keys-phrase"]')).toBeNull();
    expect(container.querySelector('[role="alert"]')?.textContent)
        .toContain('Sync keeps working as before');
    expect(onSwitched).not.toHaveBeenCalled();
  });

  it('still shows the phrase when only the relay step failed', async () => {
    render({
      success: false,
      recoveryPhrase: PHRASE,
      roomId: 'room-2',
      errorMessage: 'This device now uses your own keys, but the Sync relay was not updated (room_retired).',
    });

    await click(button('Hold my own keys'));
    await click(button('Switch to my own keys'));

    expect(container.querySelector('[data-testid="hold-own-keys-phrase"]')?.textContent)
        .toBe(PHRASE);
    expect(container.querySelector('[role="alert"]')?.textContent)
        .toContain('relay was not updated');
    // The retry stays available after a relay failure.
    expect(button('Retry relay update')).toBeTruthy();
  });

  it('retries only the relay update when the local switch already happened', async () => {
    const {handler, onSwitched} = render(
        {success: false, recoveryPhrase: '', roomId: 'room-2',
         errorMessage: 'This device now uses your own keys, but the Sync relay was not updated (HTTP 404).'},
        /*relaySwitchPending=*/ true);

    // No "Hold my own keys" re-switch is offered, only the retry.
    expect(container.textContent).not.toContain(HOLD_OWN_KEYS_WARNING);
    await click(button('Retry relay update'));
    expect(handler.enableClientHeldKeys).toHaveBeenCalledTimes(1);
    expect(button('Retry relay update')).toBeTruthy();
    expect(container.querySelector('[role="alert"]')?.textContent).toContain('HTTP 404');
    expect(onSwitched).not.toHaveBeenCalled();

    handler.enableClientHeldKeys.mockResolvedValueOnce({result: {
      success: true, recoveryPhrase: '', roomId: 'room-2', errorMessage: ''}});
    await click(button('Retry relay update'));
    expect(handler.enableClientHeldKeys).toHaveBeenCalledTimes(2);
    expect(container.querySelector('[role="alert"]')).toBeNull();
    expect([...container.querySelectorAll('button')]
        .some(candidate => candidate.textContent === 'Retry relay update')).toBe(false);
    expect(onSwitched).toHaveBeenCalledTimes(1);
  });
});
