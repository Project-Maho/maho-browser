// Copyright 2026 Maho Browser. All rights reserved.

import React, {act} from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import {PasswordsPane, VAULT_POLICY_OPTIONS} from '../passwords_settings.js';
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

function createHandler(policy = VaultAgentPolicy.kDeny) {
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
      status: {isAvailable: true, policy, unavailableReason: ''},
    }),
    setVaultPolicy: vi.fn().mockResolvedValue({
      status: {isAvailable: true, policy, unavailableReason: ''},
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
  act(() => root.unmount());
  container.remove();
  vi.clearAllMocks();
});

async function renderPane(
    handler: ReturnType<typeof createHandler>): Promise<void> {
  await act(async () => {
    root.render(
        <PasswordsPane
          pane={PASSWORDS_PANE as never}
          settings={[]}
          store={createStore(handler) as never}
        />);
  });
}

async function choosePolicy(label: string): Promise<void> {
  const trigger = container.querySelector<HTMLButtonElement>(
      'button[aria-label="Vault agent policy"]');
  if (!trigger) {
    throw new Error('missing Vault agent policy selector');
  }
  await act(async () => {
    trigger.dispatchEvent(new KeyboardEvent('keydown', {key: 'ArrowDown', bubbles: true}));
  });
  const option = [...document.querySelectorAll<HTMLElement>('[role="option"]')]
      .find(element => element.textContent?.includes(label));
  if (!option) {
    throw new Error(`missing Vault agent policy option ${label}`);
  }
  await act(async () => option.click());
}

describe('Vault agent credential access settings', () => {
  it.each([
    [VaultAgentPolicy.kDeny, 'Deny'],
    [VaultAgentPolicy.kAskEveryUse, 'Ask every use'],
    [VaultAgentPolicy.kAllowForTask, 'Allow for task'],
    [VaultAgentPolicy.kWhileUnlocked, 'While unlocked'],
    [VaultAgentPolicy.kAlwaysAllow, 'Always allow'],
  ])('renders the %s policy state', async (policy, label) => {
    const handler = createHandler(policy);
    await renderPane(handler);

    expect(container.querySelector('button[aria-label="Vault agent policy"]')?.textContent)
        .toContain(label);
    expect(VAULT_POLICY_OPTIONS.map(option => option.label)).toEqual([
      'Deny', 'Ask every use', 'Allow for task', 'While unlocked', 'Always allow',
    ]);
    expect(container.textContent).not.toContain('grants are not implemented');
  });

  it('saves the selected policy through the Vault handler and reloads status', async () => {
    const handler = createHandler();
    await renderPane(handler);

    await choosePolicy('Always allow');

    expect(handler.setVaultPolicy).toHaveBeenCalledWith(
        VaultAgentPolicy.kAlwaysAllow, null, null, null);
    expect(handler.getVaultPolicyStatus).toHaveBeenCalledTimes(2);
  });

  it('shows returned save failures without reloading the policy status', async () => {
    const handler = createHandler();
    handler.setVaultPolicy.mockResolvedValue({
      status: {isAvailable: false, policy: VaultAgentPolicy.kDeny, unavailableReason: 'Vault is locked.'},
    });
    await renderPane(handler);

    await choosePolicy('Always allow');

    expect(container.querySelector('[role="alert"]')?.textContent).toBe('Vault is locked.');
    expect(handler.getVaultPolicyStatus).toHaveBeenCalledTimes(1);
  });

  it('shows rejected save failures', async () => {
    const handler = createHandler();
    handler.setVaultPolicy.mockRejectedValue(new Error('Policy storage failed.'));
    await renderPane(handler);

    await choosePolicy('Always allow');

    expect(container.querySelector('[role="alert"]')?.textContent)
        .toBe('Policy storage failed.');
    expect(handler.getVaultPolicyStatus).toHaveBeenCalledTimes(1);
  });
});
