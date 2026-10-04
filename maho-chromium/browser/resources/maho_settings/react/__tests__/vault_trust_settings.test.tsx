// Copyright 2026 Maho Browser. All rights reserved.

import React, {act} from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import {PasswordsPane} from '../passwords_settings.js';
import {PasswordProviderKind, VaultAgentPolicy} from '../../mojo.js';

Object.defineProperty(globalThis, 'IS_REACT_ACT_ENVIRONMENT', {
  configurable: true,
  value: true,
});

const PASSWORDS_PANE = {
  key: 'passwords',
  scope: 'core-global',
  selectedProfileSupport: 'host-only',
  symbol: 'PW',
  navTitle: 'Passwords',
  title: 'Passwords',
  kind: 'live',
  domain: 'identity',
  contentKind: 'passwords',
} as const;

function createHandler() {
  return {
    getPasswordProviderStatus: vi.fn().mockResolvedValue({
      status: {
        provider: PasswordProviderKind.kMahoNative,
        displayName: 'Maho Native',
        description: '',
        isAvailable: true,
        isEnabled: true,
        capabilities: {
          canListSavedPasswords: true,
          canSearchSavedPasswords: true,
          canAddSavedPasswords: true,
          canDeleteSavedPasswords: true,
          canEditSavedPasswords: true,
        },
      },
    }),
    getPasswordProviderOptions: vi.fn().mockResolvedValue({options: []}),
    getVaultPolicyStatus: vi.fn().mockResolvedValue({
      status: {
        isAvailable: true,
        policy: VaultAgentPolicy.kDeny,
        unavailableReason: '',
      },
    }),
  };
}

function createStore(handler: ReturnType<typeof createHandler>) {
  return {
    getHandler: () => handler,
    commitSettingValue: vi.fn().mockResolvedValue(true),
    selectPane: vi.fn(),
  };
}

function deviceAuthSwitch(): HTMLElement | null {
  return document.querySelector<HTMLElement>(
      '[role="switch"][aria-label="Require device authentication"]');
}

async function openSelectAndChoose(ariaLabel: string, optionLabel: string): Promise<void> {
  const trigger = document.querySelector<HTMLButtonElement>(
      `button[aria-label="${ariaLabel}"]`);
  if (!trigger) {
    throw new Error(`missing select trigger ${ariaLabel}`);
  }
  await act(async () => {
    trigger.dispatchEvent(new KeyboardEvent('keydown', {key: 'ArrowDown', bubbles: true}));
  });
  const option = [...document.querySelectorAll<HTMLElement>('[role="option"]')]
      .find(el => el.textContent?.includes(optionLabel));
  if (!option) {
    throw new Error(`missing option ${optionLabel}`);
  }
  await act(async () => {
    option.click();
  });
}

let container: HTMLDivElement;
let root: Root;

beforeEach(() => {
  Element.prototype.scrollIntoView = vi.fn();
  Element.prototype.hasPointerCapture = () => false;
  container = document.createElement('div');
  document.body.appendChild(container);
  root = createRoot(container);
});

afterEach(() => {
  act(() => {
    root.unmount();
  });
  container.remove();
  vi.clearAllMocks();
});

async function renderPane(
    store: ReturnType<typeof createStore>,
    settings: readonly {key: string; value: string}[]): Promise<void> {
  await act(async () => {
    root.render(
        <PasswordsPane
          pane={PASSWORDS_PANE as never}
          settings={settings as never}
          store={store as never}
        />);
  });
}

describe('Vault trust settings', () => {
  it('renders both controls with Bitwarden-like defaults', async () => {
    const handler = createHandler();
    const store = createStore(handler);
    await renderPane(store, [
      {key: 'autofill.passwords_enabled', value: 'true'},
    ]);

    expect(document.body.textContent).toContain('Lock Maho Vault');
    expect(document.body.textContent).toContain('Require device authentication');
    expect(deviceAuthSwitch()?.getAttribute('aria-checked')).toBe('true');
    expect(document.querySelector('button[aria-label="Lock Maho Vault after"]'))
        .not.toBeNull();
  });

  it('commits the auto-lock selection through the settings snapshot', async () => {
    const handler = createHandler();
    const store = createStore(handler);
    await renderPane(store, [
      {key: 'autofill.vault_auto_lock_minutes', value: '15'},
    ]);

    await openSelectAndChoose('Lock Maho Vault after', 'After 1 hour');

    expect(store.commitSettingValue).toHaveBeenCalledWith(
        'autofill.vault_auto_lock_minutes', '60');
  });

  it('commits the device authentication toggle', async () => {
    const handler = createHandler();
    const store = createStore(handler);
    await renderPane(store, [
      {key: 'autofill.vault_require_device_auth', value: 'true'},
    ]);

    await act(async () => {
      deviceAuthSwitch()!.click();
    });

    expect(store.commitSettingValue).toHaveBeenCalledWith(
        'autofill.vault_require_device_auth', 'false');
  });

  it('reflects persisted values from the settings snapshot', async () => {
    const handler = createHandler();
    const store = createStore(handler);
    await renderPane(store, [
      {key: 'autofill.vault_auto_lock_minutes', value: '240'},
      {key: 'autofill.vault_require_device_auth', value: 'false'},
    ]);

    expect(deviceAuthSwitch()?.getAttribute('aria-checked')).toBe('false');
    expect(document.querySelector('button[aria-label="Lock Maho Vault after"]')
        ?.textContent).toContain('After 4 hours');
  });
});
