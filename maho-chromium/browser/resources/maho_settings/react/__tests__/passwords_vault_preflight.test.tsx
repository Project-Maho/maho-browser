// Copyright 2026 Maho Browser. All rights reserved.

/**
 * Typed Vault preflight render contract for the Saved Passwords pane. Fatal
 * startup states must remain distinct and actionable without exposing a
 * backend action that does not exist; locked retains the real unlock gate and
 * healthy renders the ordinary password library with no warning banner.
 */

import React, {act} from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import {SavedPasswordsContent} from '../saved_passwords_content.js';
import {
  PasswordProviderKind,
  VaultLockState,
  VaultPreflightState,
  type VaultOperationResult,
} from '../../mojo.js';

Object.defineProperty(globalThis, 'IS_REACT_ACT_ENVIRONMENT', {
  configurable: true,
  value: true,
});

function createVaultResult(lockState: VaultLockState): VaultOperationResult {
  return {
    success: true,
    errorCode: null,
    errorMessage: null,
    status: {
      lockState,
      selectedProvider: PasswordProviderKind.kMahoNative,
      effectiveProvider: PasswordProviderKind.kMahoNative,
      agentPolicyDefault: 0,
      itemCount: 0n,
      autoLockMinutes: 15,
      failedUnlockCount: 0,
      retryAtTimestamp: null,
    },
    item: null,
  };
}

function createStore(handler: Record<string, unknown>) {
  return {getHandler: () => handler} as unknown as
      Parameters<typeof SavedPasswordsContent>[0]['store'];
}

let container: HTMLDivElement;
let root: Root;

beforeEach(() => {
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

function renderState(
    state: VaultPreflightState,
    lockState: VaultLockState = VaultLockState.kUnlocked,
    provider: PasswordProviderKind = PasswordProviderKind.kMahoNative): void {
  act(() => {
    root.render(
        <SavedPasswordsContent
          actionStatus={null}
          data={{
            items: [],
            providerStatus: {
              provider,
              displayName: provider === PasswordProviderKind.kMahoNative ?
                  'Maho Native' : 'Passwords disabled',
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
            vaultPreflightState: state,
            vaultStatus: createVaultResult(lockState),
          }}
          drawer={null}
          loadSavedPasswords={vi.fn() as never}
          onAdd={vi.fn() as never}
          onCloseDrawer={vi.fn()}
          onDelete={vi.fn() as never}
          onEdit={vi.fn() as never}
          onOpenAdd={vi.fn()}
          onOpenEdit={vi.fn()}
          onSearchChange={vi.fn()}
          onSecretAction={vi.fn() as never}
          passwordsEnabled
          searchInputValue=""
          searchQuery=""
          store={createStore({unlockVault: vi.fn()})}
          transientResetToken={0}
        />);
  });
}

function notice(): HTMLElement {
  const element = container.querySelector<HTMLElement>(
      '[data-testid="vault-preflight-notice"]');
  if (!element) {
    throw new Error('missing Vault preflight notice');
  }
  return element;
}

function button(label: string): HTMLButtonElement {
  const element = Array.from(container.querySelectorAll('button'))
      .find(candidate => candidate.textContent?.includes(label) || candidate.getAttribute('aria-label') === label);
  if (!element) {
    throw new Error(`missing button ${label}`);
  }
  return element;
}

describe('Saved Passwords typed Vault preflight states', () => {
  it('renders lost-key guidance with the unsupported destructive action disabled', () => {
    // The effective-provider gate disables Maho Native for fatal preflight
    // states; diagnosis must still render ahead of the generic redirect.
    renderState(
        VaultPreflightState.kUnrecoverableKey,
        VaultLockState.kUninitialized,
        PasswordProviderKind.kDisabled);

    expect(notice().dataset.vaultPreflightState).toBe('unrecoverableKey');
    expect(container.textContent).toContain('Vault keys are unavailable');
    expect(container.textContent).toContain(
        'restore the matching Vault database and key together');
    expect(container.textContent).toContain('delete and recreate the Vault');
    expect(container.textContent).toContain(
        'Vault recreation and deletion are not available in Settings yet.');
    expect(button('Recreate or delete Vault').disabled).toBe(true);
  });

  it('renders structural-corruption recovery and restore guidance', () => {
    renderState(VaultPreflightState.kStructuralCorruption);

    expect(notice().dataset.vaultPreflightState).toBe('structuralCorruption');
    expect(container.textContent).toContain('Vault database needs recovery');
    expect(container.textContent).toContain('Keep the existing Vault files intact');
    expect(container.textContent).toContain('Restore a known-good backup');
    expect(container.textContent).toContain(
        'Vault recovery and restore are not available in Settings yet.');
    expect(button('Recover or restore Vault').disabled).toBe(true);
  });

  it('renders a plaintext-residue security warning with scrub unavailable', () => {
    renderState(VaultPreflightState.kPlaintextResidue);

    expect(notice().dataset.vaultPreflightState).toBe('plaintextResidue');
    expect(container.textContent).toContain('Plaintext Vault residue detected');
    expect(container.textContent).toContain(
        'Native password saving is disabled to prevent further exposure');
    expect(container.textContent).toContain(
        'A safe scrub action is not available in Settings yet.');
    expect(button('Scrub plaintext residue').disabled).toBe(true);
  });

  it('renders the real unlock affordance when the Vault is locked', () => {
    renderState(VaultPreflightState.kLocked, VaultLockState.kLocked);

    expect(container.querySelector('[data-testid="vault-preflight-notice"]'))
        .toBeNull();
    expect(container.textContent).toContain('Maho Vault is locked');
    expect(container.textContent).toContain(
        'stores encrypted password data locally on this device');
    expect(container.textContent).toContain(
        'your passphrase is not sent to Maho or any server');
    expect(button('Unlock Vault').disabled).toBe(false);
    expect(container.querySelector('#vault-master-passphrase')).not.toBeNull();
  });

  it('renders the ordinary library with no banner when preflight is healthy', () => {
    renderState(VaultPreflightState.kHealthy);

    expect(container.querySelector('[data-testid="vault-preflight-notice"]'))
        .toBeNull();
    expect(container.textContent).not.toContain('Maho Vault is locked');
    expect(container.textContent).toContain('No logins in your Vault yet.');
    expect(button('Add password').disabled).toBe(false);
  });
});
