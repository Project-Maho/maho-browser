// Copyright 2026 Maho Browser. All rights reserved.

/**
 * Destructive/setup UX hardening contract for the settings password surfaces:
 * the Vault gate blocks submit on mismatch, announces failures through an
 * aria-live region while moving focus to the offending field, clears secrets
 * after submit and on unmount, and deleting a saved password requires an
 * explicit confirmation that matches the credential's origin.
 */

import React, {act} from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import {
  validateVaultSetup,
  validateVaultUnlock,
  VaultAccessGate,
  VAULT_SETUP_MESSAGES,
} from '../passwords_vault_gate.js';
import {
  describeDeleteTarget,
  SavedPasswordsContent,
} from '../saved_passwords_content.js';
import {
  PasswordProviderKind,
  VaultItemKind,
  VaultLockState,
  VaultPreflightState,
  type VaultItem,
  type VaultOperationResult,
} from '../../mojo.js';

Object.defineProperty(globalThis, 'IS_REACT_ACT_ENVIRONMENT', {
  configurable: true,
  value: true,
});

const VALID_SETUP = {
  masterPassphrase: 'correct horse battery',
  confirmPassphrase: 'correct horse battery',
  recoverySecret: 'recovery-kit-value',
  confirmRecovery: 'recovery-kit-value',
  recoveryKitAcknowledged: true,
};

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

function createItem(overrides: Partial<VaultItem> = {}): VaultItem {
  return {
    id: 'item-1',
    revision: 1n,
    provider: PasswordProviderKind.kMahoNative,
    itemKind: VaultItemKind.kLogin,
    title: 'Example',
    origins: ['https://example.com'],
    usernameHint: 'user@example.com',
    createdAt: '2026-01-01',
    updatedAt: '2026-01-02',
    lastUsedAt: null,
    hasTotp: false,
    hasPasskey: false,
    favorite: false,
    trashedAt: null,
    hasNotes: false,
    ...overrides,
  };
}

function createStore(handler: Record<string, unknown>) {
  return {getHandler: () => handler} as unknown as
      Parameters<typeof VaultAccessGate>[0]['store'];
}

let container: HTMLDivElement;
let root: Root;

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

function input(id: string): HTMLInputElement {
  const element = container.querySelector<HTMLInputElement>(`#${id}`) ??
      document.querySelector<HTMLInputElement>(`#${id}`);
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

async function clickButton(label: string): Promise<void> {
  const button = Array.from(document.querySelectorAll('button'))
      .find(node => node.textContent?.includes(label) || node.getAttribute('aria-label') === label);
  if (button) {
    await act(async () => {
      button.click();
    });
    return;
  }
  if (label === 'Delete') {
    const more = Array.from(document.querySelectorAll<HTMLButtonElement>('button'))
        .find(node => node.getAttribute('aria-label')?.startsWith('More options for '));
    if (!more) {
      throw new Error('missing More options button');
    }
    await act(async () => {
      more.dispatchEvent(new KeyboardEvent("keydown", {key: "Enter", bubbles: true}));
    });
    const deleteItem = Array.from(document.querySelectorAll<HTMLElement>('[role="menuitem"]'))
        .find(node => node.textContent?.trim() === 'Delete');
    if (!deleteItem) {
      throw new Error('missing Delete menu item');
    }
    await act(async () => {
      deleteItem.click();
    });
    return;
  }
  throw new Error(`missing button ${label}`);
}

function errorRegion(testId: string): HTMLElement {
  const region = document.querySelector<HTMLElement>(`[data-testid="${testId}"]`);
  if (!region) {
    throw new Error(`missing aria-live region ${testId}`);
  }
  return region;
}

describe('Vault setup validation', () => {
  it('accepts a fully confirmed and acknowledged setup', () => {
    expect(validateVaultSetup(VALID_SETUP)).toBeNull();
  });

  it('blocks and targets the confirmation field on a passphrase mismatch', () => {
    expect(validateVaultSetup({...VALID_SETUP, confirmPassphrase: 'other'})).toEqual({
      field: 'confirmPassphrase',
      message: VAULT_SETUP_MESSAGES.passphraseMismatch,
    });
  });

  it('blocks on a recovery mismatch and on an unacknowledged recovery kit', () => {
    expect(validateVaultSetup({...VALID_SETUP, confirmRecovery: 'other'})).toEqual({
      field: 'confirmRecovery',
      message: VAULT_SETUP_MESSAGES.recoveryMismatch,
    });
    expect(validateVaultSetup({...VALID_SETUP, recoveryKitAcknowledged: false})).toEqual({
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

describe('Settings Vault access gate', () => {
  function renderGate(handler: Record<string, unknown>, loadPasswords = vi.fn()) {
    act(() => {
      root.render(
          <VaultAccessGate
            loadPasswords={loadPasswords as never}
            searchQuery=""
            store={createStore(handler)}
            vaultResult={createVaultResult(VaultLockState.kUninitialized)}
          />);
    });
  }

  it('blocks initialization on passphrase mismatch, announces it, and refocuses', async () => {
    const initializeVault = vi.fn();
    renderGate({initializeVault});

    setValue(input('vault-master-passphrase'), 'correct horse battery');
    setValue(input('vault-confirm-passphrase'), 'different passphrase');
    setValue(input('vault-recovery-secret'), 'recovery-kit-value');
    setValue(input('vault-confirm-recovery-secret'), 'recovery-kit-value');
    act(() => {
      input('vault-recovery-kit-acknowledged').click();
    });
    await clickButton('Initialize Vault');

    expect(initializeVault).not.toHaveBeenCalled();
    const region = errorRegion('vault-gate-error');
    expect(region.getAttribute('aria-live')).toBe('assertive');
    expect(region.getAttribute('role')).toBe('alert');
    expect(region.textContent).toBe(VAULT_SETUP_MESSAGES.passphraseMismatch);
    expect(document.activeElement).toBe(input('vault-confirm-passphrase'));
  });

  it('blocks until the recovery kit is acknowledged and refocuses the checkbox', async () => {
    const initializeVault = vi.fn();
    renderGate({initializeVault});

    setValue(input('vault-master-passphrase'), 'correct horse battery');
    setValue(input('vault-confirm-passphrase'), 'correct horse battery');
    setValue(input('vault-recovery-secret'), 'recovery-kit-value');
    setValue(input('vault-confirm-recovery-secret'), 'recovery-kit-value');
    await clickButton('Initialize Vault');

    expect(initializeVault).not.toHaveBeenCalled();
    expect(errorRegion('vault-gate-error').textContent)
        .toBe(VAULT_SETUP_MESSAGES.recoveryKitNotAcknowledged);
    expect(document.activeElement).toBe(input('vault-recovery-kit-acknowledged'));
  });

  it('initializes once confirmed and renders no secret afterwards', async () => {
    const initializeVault = vi.fn().mockResolvedValue({
      result: createVaultResult(VaultLockState.kUnlocked),
    });
    const loadPasswords = vi.fn().mockResolvedValue(undefined);
    renderGate({initializeVault}, loadPasswords);

    setValue(input('vault-master-passphrase'), 'correct horse battery');
    setValue(input('vault-confirm-passphrase'), 'correct horse battery');
    setValue(input('vault-recovery-secret'), 'recovery-kit-value');
    setValue(input('vault-confirm-recovery-secret'), 'recovery-kit-value');
    act(() => {
      input('vault-recovery-kit-acknowledged').click();
    });

    await clickButton('Initialize Vault');

    expect(initializeVault)
        .toHaveBeenCalledWith('correct horse battery', 'recovery-kit-value');
    expect(loadPasswords).toHaveBeenCalledWith('', true);
    expect(input('vault-master-passphrase').value).toBe('');
    expect(input('vault-confirm-passphrase').value).toBe('');
    expect(input('vault-recovery-secret').value).toBe('');
    expect(input('vault-confirm-recovery-secret').value).toBe('');
    expect(input('vault-recovery-kit-acknowledged').checked).toBe(false);
    expect(document.body.innerHTML).not.toContain('correct horse battery');
    expect(document.body.innerHTML).not.toContain('recovery-kit-value');
  });

  it('clears secrets even when the operation fails', async () => {
    const initializeVault = vi.fn().mockResolvedValue({
      result: {
        success: false,
        errorCode: 'kBadPassphrase',
        errorMessage: 'Vault initialization failed.',
        status: null,
        item: null,
      },
    });
    renderGate({initializeVault});

    setValue(input('vault-master-passphrase'), 'correct horse battery');
    setValue(input('vault-confirm-passphrase'), 'correct horse battery');
    setValue(input('vault-recovery-secret'), 'recovery-kit-value');
    setValue(input('vault-confirm-recovery-secret'), 'recovery-kit-value');
    act(() => {
      input('vault-recovery-kit-acknowledged').click();
    });

    await clickButton('Initialize Vault');

    expect(input('vault-master-passphrase').value).toBe('');
    expect(input('vault-recovery-secret').value).toBe('');
    expect(errorRegion('vault-gate-error').textContent)
        .toBe('Vault initialization failed.');
    expect(document.activeElement).toBe(input('vault-master-passphrase'));
  });

  it('clears secret inputs on unmount', () => {
    renderGate({initializeVault: vi.fn()});

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
});

describe('Saved password delete confirmation target', () => {
  it('describes the exact origin and username being deleted', () => {
    expect(describeDeleteTarget(createItem())).toEqual({
      origin: 'https://example.com',
      username: 'user@example.com',
    });
  });

  it('falls back to explicit placeholders when the item has neither', () => {
    expect(describeDeleteTarget(createItem({origins: [], usernameHint: ''}))).toEqual({
      origin: 'No origin',
      username: 'No username saved',
    });
  });
});

describe('Saved password delete confirmation dialog', () => {
  function renderPane(
      onDelete: ReturnType<typeof vi.fn>, transientResetToken = 0) {
    const item = createItem();
    act(() => {
      root.render(
          <SavedPasswordsContent
            actionStatus={null}
            data={{
              items: [item],
              providerStatus: {
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
              vaultPreflightState: VaultPreflightState.kHealthy,
              vaultStatus: createVaultResult(VaultLockState.kUnlocked),
            } as never}
            drawer={null}
            loadSavedPasswords={vi.fn() as never}
            onAdd={vi.fn() as never}
            onCloseDrawer={vi.fn()}
            onDelete={onDelete as never}
            onEdit={vi.fn() as never}
            onOpenAdd={vi.fn()}
            onOpenEdit={vi.fn()}
            onSearchChange={vi.fn()}
            onSecretAction={vi.fn() as never}
            passwordsEnabled
            searchInputValue=""
            searchQuery=""
            store={createStore({}) as never}
            transientResetToken={transientResetToken}
          />);
    });
    return item;
  }

  it('never deletes on the row click alone; it opens a confirmation naming the credential', async () => {
    const onDelete = vi.fn().mockResolvedValue(undefined);
    renderPane(onDelete);

    await clickButton('Delete');

    expect(onDelete).not.toHaveBeenCalled();
    expect(document.querySelector('[data-testid="delete-confirm-origin"]')?.textContent)
        .toBe('Origin: https://example.com');
    expect(document.querySelector('[data-testid="delete-confirm-username"]')?.textContent)
        .toBe('Username: user@example.com');
  });

  it('blocks the delete until the typed confirmation matches the origin', async () => {
    const onDelete = vi.fn().mockResolvedValue(undefined);
    renderPane(onDelete);

    await clickButton('Delete');
    setValue(input('delete-confirm-input'), 'https://example.org');
    await clickButton('Delete password');

    expect(onDelete).not.toHaveBeenCalled();
    const error = document.querySelector<HTMLElement>('[data-testid="delete-confirm-error"]')!;
    expect(error.getAttribute('aria-live')).toBe('assertive');
    expect(error.getAttribute('role')).toBe('alert');
    expect(error.textContent)
        .toBe('Type https://example.com exactly to confirm this deletion.');
    expect(document.activeElement).toBe(input('delete-confirm-input'));
  });

  it('deletes the exact item once the origin confirmation matches', async () => {
    const onDelete = vi.fn().mockResolvedValue(undefined);
    const item = renderPane(onDelete);

    await clickButton('Delete');
    setValue(input('delete-confirm-input'), 'https://example.com');
    await clickButton('Delete password');

    expect(onDelete).toHaveBeenCalledTimes(1);
    expect(onDelete).toHaveBeenCalledWith(item);
    expect(document.querySelector('[data-testid="delete-confirm-origin"]')).toBeNull();
  });

  it('discards the open confirmation when the pane resets transient state', async () => {
    const onDelete = vi.fn().mockResolvedValue(undefined);
    renderPane(onDelete, 0);

    await clickButton('Delete');
    setValue(input('delete-confirm-input'), 'https://example.com');
    expect(document.querySelector('[data-testid="delete-confirm-origin"]')).not.toBeNull();

    renderPane(onDelete, 1);

    expect(document.querySelector('[data-testid="delete-confirm-origin"]')).toBeNull();
    expect(onDelete).not.toHaveBeenCalled();
  });

  it('does not reopen the discarded confirmation on later re-renders', async () => {
    const onDelete = vi.fn().mockResolvedValue(undefined);
    renderPane(onDelete, 0);

    await clickButton('Delete');
    renderPane(onDelete, 1);
    renderPane(onDelete, 1);

    expect(document.querySelector('[data-testid="delete-confirm-origin"]')).toBeNull();
    expect(onDelete).not.toHaveBeenCalled();
  });
});
