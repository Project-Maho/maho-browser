// Copyright 2026 Maho Browser. All rights reserved.

/**
 * Destructive/setup UX hardening contract for onboarding password setup:
 * confirmation fields block submit on mismatch, errors are announced via an
 * aria-live region and move focus to the offending field, secrets never
 * survive a submit, and the "None"/external provider paths persist and complete
 * without an unlocked Vault.
 */

import React, {act} from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import {PasswordSetupContent} from '../pages/password-setup.js';
import {
  validateVaultSetup,
  validateVaultUnlock,
  VAULT_SETUP_MESSAGES,
} from '../pages/password-setup-fields.js';
import {createInitialState, PASSWORD_PROVIDER_KIND, VAULT_LOCK_STATE, WelcomePage} from '../types.js';
import type {WelcomeState} from '../types.js';

// `@ui/button`, `@ui/badge`, and `@lib/utils` share one stub module in this
// feature's vitest aliases; the real Button stub renders null, so replace the
// module while keeping its other exports intact.
vi.mock('@ui/button', () => ({
  Badge: () => null,
  Button: ({children, ...props}: React.ComponentProps<'button'>) =>
      <button {...props}>{children}</button>,
  cn: (...classes: Array<string | false | null | undefined>) =>
      classes.filter(Boolean).join(' '),
}));

Object.defineProperty(globalThis, 'IS_REACT_ACT_ENVIRONMENT', {
  configurable: true,
  value: true,
});

const PROVIDER_OPTIONS = [
  {
    provider: PASSWORD_PROVIDER_KIND.MahoNative,
    displayName: 'Maho Passwords',
    description: 'Store passwords in the local Maho Vault.',
    isAvailable: true,
  },
  {
    provider: PASSWORD_PROVIDER_KIND.Bitwarden,
    displayName: 'Bitwarden',
    description: 'Use the Bitwarden extension.',
    isAvailable: true,
  },
] as const;

interface StoreCalls {
  readonly advance: ReturnType<typeof vi.fn>;
  readonly initialize: ReturnType<typeof vi.fn>;
  readonly skip: ReturnType<typeof vi.fn>;
  readonly unlock: ReturnType<typeof vi.fn>;
}

function createSnapshot(lockState: number): WelcomeState {
  const base = createInitialState();
  return {
    ...base,
    currentPage: WelcomePage.PasswordSetup,
    passwordSetup: {
      ...base.passwordSetup,
      status: 'ready',
      errorMessage: '',
      providerOptions: [...PROVIDER_OPTIONS],
      providerStatus: null,
      vaultStatus: {
        lockState,
        selectedProvider: PASSWORD_PROVIDER_KIND.MahoNative,
        effectiveProvider: PASSWORD_PROVIDER_KIND.MahoNative,
        agentPolicyDefault: 0,
        itemCount: 0n,
        autoLockMinutes: 15,
        failedUnlockCount: 0,
        retryAtTimestamp: null,
      },
    },
  } as WelcomeState;
}

function createStore(overrides: Partial<StoreCalls> = {}) {
  const calls: StoreCalls = {
    advance: overrides.advance ?? vi.fn().mockResolvedValue(true),
    initialize: overrides.initialize ?? vi.fn().mockResolvedValue(true),
    skip: overrides.skip ?? vi.fn().mockResolvedValue(true),
    unlock: overrides.unlock ?? vi.fn().mockResolvedValue(true),
  };
  const store = {
    advanceFromPasswordSetup: calls.advance,
    initializeVault: calls.initialize,
    skipPasswordSetup: calls.skip,
    unlockVault: calls.unlock,
    refreshPasswordSetup: vi.fn().mockResolvedValue(true),
  };
  return {calls, store: store as unknown as Parameters<typeof PasswordSetupContent>[0]['store']};
}

let container: HTMLDivElement;
let root: Root;

function render(snapshot: WelcomeState, store: Parameters<typeof PasswordSetupContent>[0]['store']) {
  act(() => {
    root.render(<PasswordSetupContent snapshot={snapshot} store={store} />);
  });
}

function input(id: string): HTMLInputElement {
  const element = container.querySelector<HTMLInputElement>(`#${id}`);
  if (!element) {
    throw new Error(`missing input #${id}`);
  }
  return element;
}

function setValue(element: HTMLInputElement, value: string): void {
  const setter = Object.getOwnPropertyDescriptor(
      window.HTMLInputElement.prototype, 'value')!.set!;
  act(() => {
    setter.call(element, value);
    element.dispatchEvent(new Event('input', {bubbles: true}));
  });
}

function toggle(element: HTMLInputElement): void {
  act(() => {
    element.click();
  });
}

function submit(): void {
  const form = container.querySelector('form');
  if (!form) {
    throw new Error('missing form');
  }
  act(() => {
    form.dispatchEvent(new Event('submit', {bubbles: true, cancelable: true}));
  });
}

function clickChoice(label: string): void {
  const card = Array.from(container.querySelectorAll('[role="radio"]'))
      .find(node => node.textContent?.includes(label));
  if (!card) {
    throw new Error(`missing provider choice ${label}`);
  }
  act(() => {
    (card as HTMLElement).click();
  });
}

function errorRegion(): HTMLElement {
  const region = container.querySelector<HTMLElement>('[data-testid="password-setup-error"]');
  if (!region) {
    throw new Error('missing aria-live error region');
  }
  return region;
}

beforeEach(() => {
  container = document.createElement('div');
  container.id = 'app';
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

describe('Vault setup validation', () => {
  const valid = {
    masterPassphrase: 'correct horse battery',
    confirmPassphrase: 'correct horse battery',
    recoverySecret: 'recovery-kit-value',
    confirmRecovery: 'recovery-kit-value',
    recoveryKitAcknowledged: true,
  };

  it('accepts a fully confirmed and acknowledged setup', () => {
    expect(validateVaultSetup(valid)).toBeNull();
  });

  it('blocks on passphrase mismatch before anything else downstream', () => {
    expect(validateVaultSetup({...valid, confirmPassphrase: 'other passphrase'})).toEqual({
      field: 'confirmPassphrase',
      message: VAULT_SETUP_MESSAGES.passphraseMismatch,
    });
  });

  it('blocks on recovery mismatch and on a missing recovery-kit acknowledgement', () => {
    expect(validateVaultSetup({...valid, confirmRecovery: 'nope'})).toEqual({
      field: 'confirmRecovery',
      message: VAULT_SETUP_MESSAGES.recoveryMismatch,
    });
    expect(validateVaultSetup({...valid, recoveryKitAcknowledged: false})).toEqual({
      field: 'recoveryKitAcknowledged',
      message: VAULT_SETUP_MESSAGES.recoveryKitNotAcknowledged,
    });
  });

  it('requires a passphrase to unlock', () => {
    expect(validateVaultUnlock('')).toEqual({
      field: 'masterPassphrase',
      message: VAULT_SETUP_MESSAGES.unlockPassphraseRequired,
    });
    expect(validateVaultUnlock('passphrase')).toBeNull();
  });
});

describe('Onboarding password setup hardening', () => {
  it('blocks submit on passphrase mismatch, announces it, and refocuses the field', () => {
    const {calls, store} = createStore();
    render(createSnapshot(VAULT_LOCK_STATE.Uninitialized), store);

    clickChoice('Maho Passwords');
    submit();

    setValue(input('vault-master-passphrase'), 'correct horse battery');
    setValue(input('vault-confirm-passphrase'), 'different passphrase');
    setValue(input('vault-recovery-secret'), 'recovery-kit-value');
    setValue(input('vault-confirm-recovery-secret'), 'recovery-kit-value');
    toggle(input('vault-recovery-kit-acknowledged'));
    submit();

    expect(calls.initialize).not.toHaveBeenCalled();
    expect(calls.advance).not.toHaveBeenCalled();
    const region = errorRegion();
    expect(region.getAttribute('aria-live')).toBe('assertive');
    expect(region.getAttribute('role')).toBe('alert');
    expect(region.textContent).toContain(VAULT_SETUP_MESSAGES.passphraseMismatch);
    expect(document.activeElement).toBe(input('vault-confirm-passphrase'));
  });

  it('blocks submit until the recovery kit is acknowledged and refocuses the checkbox', () => {
    const {calls, store} = createStore();
    render(createSnapshot(VAULT_LOCK_STATE.Uninitialized), store);

    clickChoice('Maho Passwords');
    submit();

    setValue(input('vault-master-passphrase'), 'correct horse battery');
    setValue(input('vault-confirm-passphrase'), 'correct horse battery');
    setValue(input('vault-recovery-secret'), 'recovery-kit-value');
    setValue(input('vault-confirm-recovery-secret'), 'recovery-kit-value');
    submit();

    expect(calls.initialize).not.toHaveBeenCalled();
    expect(errorRegion().textContent)
        .toContain(VAULT_SETUP_MESSAGES.recoveryKitNotAcknowledged);
    expect(document.activeElement).toBe(input('vault-recovery-kit-acknowledged'));
  });

  it('initializes the Vault once confirmed and clears every secret field after submit', async () => {
    const {calls, store} = createStore();
    render(createSnapshot(VAULT_LOCK_STATE.Uninitialized), store);

    clickChoice('Maho Passwords');
    submit();

    setValue(input('vault-master-passphrase'), 'correct horse battery');
    setValue(input('vault-confirm-passphrase'), 'correct horse battery');
    setValue(input('vault-recovery-secret'), 'recovery-kit-value');
    setValue(input('vault-confirm-recovery-secret'), 'recovery-kit-value');
    toggle(input('vault-recovery-kit-acknowledged'));

    await act(async () => {
      container.querySelector('form')!.dispatchEvent(
          new Event('submit', {bubbles: true, cancelable: true}));
    });

    expect(calls.initialize)
        .toHaveBeenCalledWith('correct horse battery', 'recovery-kit-value');
    expect(calls.advance).toHaveBeenCalledWith(PASSWORD_PROVIDER_KIND.MahoNative);
    expect(input('vault-master-passphrase').value).toBe('');
    expect(input('vault-confirm-passphrase').value).toBe('');
    expect(input('vault-recovery-secret').value).toBe('');
    expect(input('vault-confirm-recovery-secret').value).toBe('');
    expect(input('vault-recovery-kit-acknowledged').checked).toBe(false);
    expect(container.innerHTML).not.toContain('correct horse battery');
    expect(container.innerHTML).not.toContain('recovery-kit-value');
  });

  it('clears secret inputs when the page unmounts', () => {
    const {store} = createStore();
    render(createSnapshot(VAULT_LOCK_STATE.Uninitialized), store);

    clickChoice('Maho Passwords');
    submit();

    const master = input('vault-master-passphrase');
    const recovery = input('vault-recovery-secret');
    setValue(master, 'correct horse battery');
    setValue(recovery, 'recovery-kit-value');

    act(() => {
      root.unmount();
    });

    expect(master.value).toBe('');
    expect(recovery.value).toBe('');

    root = createRoot(container);
  });

  it('persists and completes the "None" choice without an unlocked Vault', async () => {
    const {calls, store} = createStore();
    render(createSnapshot(VAULT_LOCK_STATE.Uninitialized), store);

    clickChoice('No password manager');
    submit();

    await act(async () => {
      container.querySelector('form')!.dispatchEvent(
          new Event('submit', {bubbles: true, cancelable: true}));
    });

    expect(calls.skip).toHaveBeenCalledTimes(1);
    expect(calls.initialize).not.toHaveBeenCalled();
    expect(calls.unlock).not.toHaveBeenCalled();
  });

  it('persists and completes an external provider without an unlocked Vault', async () => {
    const {calls, store} = createStore();
    render(createSnapshot(VAULT_LOCK_STATE.Uninitialized), store);

    clickChoice('Bitwarden');
    submit();

    await act(async () => {
      container.querySelector('form')!.dispatchEvent(
          new Event('submit', {bubbles: true, cancelable: true}));
    });

    expect(calls.advance).toHaveBeenCalledWith(PASSWORD_PROVIDER_KIND.Bitwarden);
    expect(calls.initialize).not.toHaveBeenCalled();
    expect(calls.unlock).not.toHaveBeenCalled();
  });
});
