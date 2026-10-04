// Copyright 2026 Maho Browser. All rights reserved.

import React from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {act} from 'react';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import {
  PasswordProviderKind,
  SecretAction,
  VaultItemKind,
  VaultLockState,
  VaultPreflightState,
} from './mojo_stub.js';
import type {
  GeneratedPasswordResult,
  PasswordGeneratorOptions,
  VaultHealthReport,
  VaultItem,
  VaultOperationResult,
  VaultTotpCodeResult,
} from './mojo_stub.js';
import {SavedPasswordsContent} from '../saved_passwords_content.js';
import {
  AddSavedPasswordDrawer,
  EditSavedPasswordDrawer,
  PasswordGenerator,
  StandalonePasswordGeneratorDialog,
} from '../saved_passwords_drawer.js';
import {
  filterByCategory,
  filterSavedPasswordItems,
  operationResetsTransientState,
  sortSavedPasswordItems,
  vaultOperationMessage,
} from '../saved_passwords_model.js';
import type {MahoSettingsStore} from '../store.js';

let container: HTMLDivElement;
let root: Root;

beforeEach(() => {
  if (typeof (global as unknown as {ResizeObserver?: unknown}).ResizeObserver === 'undefined') {
    (global as unknown as {ResizeObserver: unknown}).ResizeObserver = class {
      observe() {}
      unobserve() {}
      disconnect() {}
    };
  }
  container = document.createElement('div');
  document.body.appendChild(container);
  root = createRoot(container);
});

function setInputValue(element: HTMLInputElement | HTMLTextAreaElement, value: string): void {
  const proto = element instanceof HTMLTextAreaElement ?
      window.HTMLTextAreaElement.prototype :
      window.HTMLInputElement.prototype;
  const setter = Object.getOwnPropertyDescriptor(proto, 'value')!.set!;
  setter.call(element, value);
  element.dispatchEvent(new Event('input', {bubbles: true}));
}

afterEach(() => {
  act(() => {
    root.unmount();
  });
  container.remove();
  vi.clearAllMocks();
});

function createMockItem(overrides: Partial<VaultItem> = {}): VaultItem {
  return {
    id: 'item-1',
    revision: 1n,
    provider: PasswordProviderKind.kMahoNative,
    itemKind: VaultItemKind.kLogin,
    title: 'GitHub',
    origins: ['https://github.com'],
    usernameHint: 'octocat',
    createdAt: '2026-01-01T00:00:00Z',
    updatedAt: '2026-01-02T00:00:00Z',
    lastUsedAt: '2026-01-03T00:00:00Z',
    hasTotp: false,
    hasPasskey: false,
    favorite: false,
    trashedAt: null,
    hasNotes: false,
    ...overrides,
  };
}

function createVaultStatus(lockState: VaultLockState = VaultLockState.kUnlocked): VaultOperationResult {
  return {
    success: true,
    errorCode: null,
    errorMessage: null,
    status: {
      lockState,
      selectedProvider: PasswordProviderKind.kMahoNative,
      effectiveProvider: PasswordProviderKind.kMahoNative,
      agentPolicyDefault: 0,
      itemCount: 1n,
      autoLockMinutes: 15,
      failedUnlockCount: 0,
      retryAtTimestamp: null,
    },
    item: null,
  };
}

function createMockStore(handlerOverrides: Record<string, unknown> = {}): MahoSettingsStore {
  const handler = {
    getVaultItemNotes: vi.fn().mockResolvedValue({success: true, notes: 'Secret note text', errorCode: null, username: 'octocat@example.com'}),
    getVaultTotpCode: vi.fn().mockResolvedValue({
      result: {
        success: true,
        code: '123456',
        secondsRemaining: 24,
        period: 30,
        errorCode: null,
      },
    }),
    getVaultHealthReport: vi.fn().mockResolvedValue({
      report: {
        success: true,
        weakItemIds: ['item-weak'],
        reusedGroups: [['item-reused-1', 'item-reused-2']],
        totalLogins: 5,
        errorCode: null,
      },
    }),
    generatePassword: vi.fn().mockResolvedValue({
      result: {
        success: true,
        password: 'generated-mock-password-123',
        strengthScore: 4,
        entropyBits: 85,
      },
    }),
    estimatePasswordStrength: vi.fn().mockResolvedValue({score: 4, entropyBits: 85}),
    setVaultItemFavorite: vi.fn().mockResolvedValue({result: {success: true}}),
    trashVaultItem: vi.fn().mockResolvedValue({result: {success: true}}),
    restoreVaultItem: vi.fn().mockResolvedValue({result: {success: true}}),
    emptyVaultTrash: vi.fn().mockResolvedValue({result: {success: true}}),
    lockVault: vi.fn().mockResolvedValue({result: {success: true}}),
    addVaultLogin: vi.fn().mockResolvedValue({result: {success: true, item: createMockItem()}}),
    updateVaultLogin: vi.fn().mockResolvedValue({result: {success: true, item: createMockItem()}}),
    addVaultSecureNote: vi.fn().mockResolvedValue({result: {success: true, item: createMockItem()}}),
    updateVaultSecureNote: vi.fn().mockResolvedValue({result: {success: true, item: createMockItem()}}),
    setVaultLoginTotp: vi.fn().mockResolvedValue({result: {success: true}}),
    deleteVaultItem: vi.fn().mockResolvedValue({result: {success: true}}),
    useVaultSecret: vi.fn().mockResolvedValue({result: {success: true}}),
    ...handlerOverrides,
  };

  return {
    getHandler: () => handler,
    getCallbackRouter: () => ({
      onVaultLockStateChanged: {addListener: vi.fn(), removeListener: vi.fn()},
    }),
    selectPane: vi.fn(),
  } as unknown as MahoSettingsStore;
}

function findButton(predicate: (btn: HTMLButtonElement) => boolean): HTMLButtonElement | null {
  return Array.from(document.querySelectorAll<HTMLButtonElement>('button')).find(predicate) ?? null;
}

function buttonWithLabel(label: string): HTMLButtonElement {
  const btn = findButton(b =>
      b.textContent?.trim() === label ||
      b.getAttribute('aria-label') === label ||
      b.getAttribute('title') === label);
  if (!btn) {
    throw new Error(`Button not found with label "${label}"`);
  }
  return btn;
}

describe('Maho Native Vault Parity - Filters & Rail', () => {
  it('correctly filters items across All, Favorites, Logins, Authenticator, Secure notes, and Trash', () => {
    const login = createMockItem({id: '1', itemKind: VaultItemKind.kLogin, favorite: true, hasTotp: false});
    const totpLogin = createMockItem({id: '2', itemKind: VaultItemKind.kLogin, favorite: false, hasTotp: true});
    const note = createMockItem({id: '3', itemKind: VaultItemKind.kSecureItem, favorite: true});
    const trashed = createMockItem({id: '4', trashedAt: '2026-01-04T00:00:00Z'});

    const activeItems = [login, totpLogin, note];
    const trashItems = [trashed];

    expect(filterByCategory(activeItems, trashItems, 'all')).toHaveLength(3);
    expect(filterByCategory(activeItems, trashItems, 'favorites')).toHaveLength(2);
    expect(filterByCategory(activeItems, trashItems, 'logins')).toHaveLength(2);
    expect(filterByCategory(activeItems, trashItems, 'authenticator')).toHaveLength(1);
    expect(filterByCategory(activeItems, trashItems, 'secure-notes')).toHaveLength(1);
    expect(filterByCategory(activeItems, trashItems, 'trash')).toHaveLength(1);
  });

  it('renders categories in rail with accurate counts and switches active filter on click', async () => {
    const login = createMockItem({id: '1', itemKind: VaultItemKind.kLogin, favorite: true, hasTotp: false});
    const note = createMockItem({id: '2', title: 'Private Keys', itemKind: VaultItemKind.kSecureItem, favorite: false});
    const trashed = createMockItem({id: '3', title: 'Old Site', trashedAt: '2026-01-04T00:00:00Z'});

    const store = createMockStore();

    await act(async () => {
      root.render(
        <SavedPasswordsContent
          actionStatus={null}
          data={{
            items: [login, note],
            trashItems: [trashed],
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
            vaultStatus: createVaultStatus(),
          } as never}
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
          store={store}
          transientResetToken={0}
        />
      );
    });

    const categoriesRail = document.querySelector('[aria-label="Vault categories"]');
    expect(categoriesRail).not.toBeNull();
    expect(categoriesRail?.textContent).toContain('All items');
    expect(categoriesRail?.textContent).toContain('Favorites');
    expect(categoriesRail?.textContent).toContain('Logins');
    expect(categoriesRail?.textContent).toContain('Authenticator');
    expect(categoriesRail?.textContent).toContain('Secure notes');
    expect(categoriesRail?.textContent).toContain('Trash');

    expect(categoriesRail?.textContent).not.toContain('Card');
    expect(categoriesRail?.textContent).not.toContain('Identity');

    expect(container.textContent).toContain('GitHub');
    expect(container.textContent).toContain('Private Keys');

    const favoritesButton = Array.from(categoriesRail!.querySelectorAll('button')).find(b => b.textContent?.includes('Favorites'));
    expect(favoritesButton).toBeDefined();

    await act(async () => {
      favoritesButton!.click();
    });

    expect(container.textContent).toContain('GitHub');
    expect(container.textContent).not.toContain('Private Keys');
  });
});

describe('Maho Native Vault Parity - Sorting', () => {
  it('sorts items alphabetically, by recently used, and by date added/modified', () => {
    const itemA = createMockItem({id: '1', title: 'Alpha', createdAt: '2026-01-01', updatedAt: '2026-01-03', lastUsedAt: '2026-01-02'});
    const itemB = createMockItem({id: '2', title: 'Beta', createdAt: '2026-01-02', updatedAt: '2026-01-01', lastUsedAt: '2026-01-05'});

    const sortedByNameAsc = sortSavedPasswordItems([itemB, itemA], 'name', 'asc');
    expect(sortedByNameAsc[0].title).toBe('Alpha');

    const sortedByNameDesc = sortSavedPasswordItems([itemA, itemB], 'name', 'desc');
    expect(sortedByNameDesc[0].title).toBe('Beta');

    const sortedByRecentlyUsed = sortSavedPasswordItems([itemA, itemB], 'recently-used', 'asc');
    expect(sortedByRecentlyUsed[0].title).toBe('Beta');

    const sortedByCreated = sortSavedPasswordItems([itemA, itemB], 'date-created', 'asc');
    expect(sortedByCreated[0].title).toBe('Beta');

    const sortedByUpdated = sortSavedPasswordItems([itemA, itemB], 'date-updated', 'asc');
    expect(sortedByUpdated[0].title).toBe('Alpha');
  });
});

describe('Maho Native Vault Parity - Item Detail View & Zero-Plaintext Rule', () => {
  it('opens item detail view and provides username copy, password copy, and native reveal without leaking secrets', async () => {
    const item = createMockItem({
      id: 'item-octo',
      title: 'GitHub',
      usernameHint: 'octocat',
      origins: ['https://github.com'],
      hasPasskey: true,
      hasTotp: true,
    });
    const store = createMockStore();
    const onSecretAction = vi.fn().mockResolvedValue(undefined);

    await act(async () => {
      root.render(
        <SavedPasswordsContent
          actionStatus={null}
          data={{
            items: [item],
            trashItems: [],
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
            vaultStatus: createVaultStatus(),
          } as never}
          drawer={null}
          loadSavedPasswords={vi.fn() as never}
          onAdd={vi.fn() as never}
          onCloseDrawer={vi.fn()}
          onDelete={vi.fn() as never}
          onEdit={vi.fn() as never}
          onOpenAdd={vi.fn()}
          onOpenEdit={vi.fn()}
          onSearchChange={vi.fn()}
          onSecretAction={onSecretAction}
          passwordsEnabled
          searchInputValue=""
          searchQuery=""
          store={store}
          transientResetToken={0}
        />
      );
    });

    const itemTitleButton = Array.from(document.querySelectorAll('button')).find(b => b.textContent?.includes('GitHub'));
    expect(itemTitleButton).toBeDefined();

    await act(async () => {
      itemTitleButton!.click();
    });

    const detailDialog = document.querySelector('[role="dialog"]');
    expect(detailDialog).not.toBeNull();
    expect(detailDialog?.textContent).toContain('GitHub');
    expect(detailDialog?.textContent).toContain('octocat');
    expect(detailDialog?.textContent).toContain('Passkey');
    expect(detailDialog?.textContent).toContain('••••••••');

    const rawDomText = detailDialog?.textContent || '';
    expect(rawDomText).not.toContain('super-secret-password');
    expect(rawDomText).not.toContain('stored-password-secret');

    // The detail view shows the stored username, not the masked list hint.
    expect(detailDialog?.textContent).toContain('octocat@example.com');
    const writeText = vi.fn().mockResolvedValue(undefined);
    Object.defineProperty(navigator, 'clipboard', {configurable: true, value: {writeText}});
    const copyUsernameBtn = buttonWithLabel('Copy username');
    await act(async () => {
      copyUsernameBtn.click();
    });
    expect(writeText).toHaveBeenCalledWith('octocat@example.com');

    const copyPasswordBtn = buttonWithLabel('Copy password');
    await act(async () => {
      copyPasswordBtn.click();
    });
    expect(onSecretAction).toHaveBeenCalledWith(item, SecretAction.kCopy);

    const revealBtn = buttonWithLabel('Reveal password');
    await act(async () => {
      revealBtn.click();
    });
    expect(onSecretAction).toHaveBeenCalledWith(item, SecretAction.kReveal);

    expect(store.getHandler().getVaultTotpCode).toHaveBeenCalledWith('item-octo');
    expect(document.querySelector('[data-testid="detail-totp-code"]')?.textContent).toContain('123456');

    expect(store.getHandler().getVaultItemNotes).toHaveBeenCalledWith('item-octo');
    expect(document.querySelector('[data-testid="detail-notes"]')?.textContent).toContain('Secret note text');
  });
});

describe('Maho Native Vault Parity - Password Generator', () => {
  it('generates secure passwords and estimates strength', async () => {
    const store = createMockStore();
    const onUsePassword = vi.fn();

    await act(async () => {
      root.render(
        <PasswordGenerator onUsePassword={onUsePassword} store={store} />
      );
    });

    expect(store.getHandler().generatePassword).toHaveBeenCalled();
    const preview = document.querySelector('[data-testid="generated-password-preview"]');
    expect(preview?.textContent).toBe('generated-mock-password-123');

    const useButton = Array.from(document.querySelectorAll('button')).find(b => b.textContent?.includes('Use this password'));
    expect(useButton).toBeDefined();

    await act(async () => {
      useButton!.click();
    });
    expect(onUsePassword).toHaveBeenCalledWith('generated-mock-password-123');
  });

  it('surfaces generator failure without producing a renderer-side password', async () => {
    const store = createMockStore();
    vi.mocked(store.getHandler().generatePassword).mockRejectedValue(new Error('offline'));

    await act(async () => {
      root.render(<PasswordGenerator store={store} />);
    });

    const preview = document.querySelector('[data-testid="generated-password-preview"]');
    expect(preview?.textContent).toBe('');
    expect(document.querySelector('[role="status"]')?.textContent)
        .toBe('Password generation is unavailable. Try again.');
  });

  it('renders StandalonePasswordGeneratorDialog and closes on request', async () => {
    const store = createMockStore();
    const onClose = vi.fn();

    await act(async () => {
      root.render(
        <StandalonePasswordGeneratorDialog onClose={onClose} store={store} />
      );
    });

    expect(document.querySelector('[role="dialog"]')?.textContent).toContain('Password Generator');
    const closeBtn = Array.from(document.querySelectorAll('button')).find(b => b.textContent?.trim() === 'Close');
    expect(closeBtn).toBeDefined();

    await act(async () => {
      closeBtn!.click();
    });
    expect(onClose).toHaveBeenCalled();
  });
});

describe('Maho Native Vault Parity - Health Report View', () => {
  it('opens Password Health Report, displays weak and reused groups, and allows selecting items', async () => {
    const weakItem = createMockItem({id: 'item-weak', title: 'Insecure Portal'});
    const reused1 = createMockItem({id: 'item-reused-1', title: 'Shared Service 1'});
    const reused2 = createMockItem({id: 'item-reused-2', title: 'Shared Service 2'});
    const store = createMockStore();

    await act(async () => {
      root.render(
        <SavedPasswordsContent
          actionStatus={null}
          data={{
            items: [weakItem, reused1, reused2],
            trashItems: [],
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
            vaultStatus: createVaultStatus(),
          } as never}
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
          store={store}
          transientResetToken={0}
        />
      );
    });

    const healthBtn = buttonWithLabel('Password health');
    await act(async () => {
      healthBtn.click();
    });

    const healthView = document.querySelector('[data-testid="password-health-view"]');
    expect(healthView).not.toBeNull();
    expect(healthView?.textContent).toContain('Password Health Report');
    expect(healthView?.textContent).toContain('Weak Passwords (1)');
    expect(healthView?.textContent).toContain('Insecure Portal');
    expect(healthView?.textContent).toContain('Reused Passwords (1 groups)');
    expect(healthView?.textContent).toContain('Shared Service 1');
    expect(healthView?.textContent).toContain('Shared Service 2');

    const selectWeakItemBtn = Array.from(healthView!.querySelectorAll('button')).find(b => b.textContent?.includes('Insecure Portal'));
    expect(selectWeakItemBtn).toBeDefined();

    await act(async () => {
      selectWeakItemBtn!.click();
    });

    expect(document.querySelector('[role="dialog"]')?.textContent).toContain('Insecure Portal');
  });
});

describe('Maho Native Vault Parity - Secure Notes & Multiple URLs', () => {
  it('supports adding and editing secure notes with custom title and notes', async () => {
    const onAddSecureNote = vi.fn().mockResolvedValue(null);
    const store = createMockStore();

    await act(async () => {
      root.render(
        <AddSavedPasswordDrawer
          initialKind={VaultItemKind.kSecureItem}
          onAdd={vi.fn() as never}
          onAddSecureNote={onAddSecureNote}
          onClose={vi.fn()}
          store={store}
        />
      );
    });

    const titleInput = document.querySelector<HTMLInputElement>('#secure-note-title');
    const notesInput = document.querySelector<HTMLTextAreaElement>('#secure-note-notes');
    expect(titleInput).not.toBeNull();
    expect(notesInput).not.toBeNull();

    await act(async () => {
      setInputValue(titleInput!, 'Office WiFi');
      setInputValue(notesInput!, 'Password is Guest2026');
    });

    const saveNoteBtn = Array.from(document.querySelectorAll('button')).find(b => b.textContent?.includes('Save note'));
    expect(saveNoteBtn).toBeDefined();

    await act(async () => {
      saveNoteBtn!.click();
    });

    expect(onAddSecureNote).toHaveBeenCalledWith({
      title: 'Office WiFi',
      notes: 'Password is Guest2026',
    });
  });

  it('supports multiple URLs in the login drawer and adds secondary URLs', async () => {
    const onAdd = vi.fn().mockResolvedValue(null);
    const store = createMockStore();

    await act(async () => {
      root.render(
        <AddSavedPasswordDrawer
          onAdd={onAdd}
          onClose={vi.fn()}
          store={store}
        />
      );
    });

    const primaryOriginInput = document.querySelector<HTMLInputElement>('#saved-password-origin');
    expect(primaryOriginInput).not.toBeNull();

    const addUrlBtn = Array.from(document.querySelectorAll('button')).find(b => b.textContent?.includes('Add another URL'));
    expect(addUrlBtn).toBeDefined();

    await act(async () => {
      addUrlBtn!.click();
    });

    const secondaryOriginInput = document.querySelector<HTMLInputElement>('#saved-password-origin-1');
    expect(secondaryOriginInput).not.toBeNull();

    await act(async () => {
      setInputValue(primaryOriginInput!, 'https://app.example.com');
      setInputValue(secondaryOriginInput!, 'https://auth.example.com');

      const usernameInput = document.querySelector<HTMLInputElement>('#saved-password-username')!;
      setInputValue(usernameInput, 'john');

      const passwordInput = document.querySelector<HTMLInputElement>('#saved-password-value')!;
      setInputValue(passwordInput, 'secret123');
    });

    const saveBtn = Array.from(document.querySelectorAll('button')).find(b => b.textContent?.includes('Save password'));
    await act(async () => {
      saveBtn!.click();
    });

    expect(onAdd).toHaveBeenCalledWith(expect.objectContaining({
      origin: 'https://app.example.com',
      origins: ['https://app.example.com', 'https://auth.example.com'],
      username: 'john',
      password: 'secret123',
    }));
  });
});

describe('Maho Native Vault Parity - Trash Actions & Lock Now', () => {
  it('calls lockVault on clicking Lock now button', async () => {
    const onLock = vi.fn().mockResolvedValue(undefined);
    const store = createMockStore();

    await act(async () => {
      root.render(
        <SavedPasswordsContent
          actionStatus={null}
          data={{
            items: [createMockItem()],
            trashItems: [],
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
            vaultStatus: createVaultStatus(),
          } as never}
          drawer={null}
          loadSavedPasswords={vi.fn() as never}
          onAdd={vi.fn() as never}
          onCloseDrawer={vi.fn()}
          onDelete={vi.fn() as never}
          onEdit={vi.fn() as never}
          onLock={onLock}
          onOpenAdd={vi.fn()}
          onOpenEdit={vi.fn()}
          onSearchChange={vi.fn()}
          onSecretAction={vi.fn() as never}
          passwordsEnabled
          searchInputValue=""
          searchQuery=""
          store={store}
          transientResetToken={0}
        />
      );
    });

    const lockNowBtn = buttonWithLabel('Lock Vault now');
    await act(async () => {
      lockNowBtn.click();
    });

    expect(onLock).toHaveBeenCalled();
  });

  it('supports restoring items and emptying trash in the trash view', async () => {
    const trashedItem = createMockItem({id: 'trashed-1', title: 'Old Service', trashedAt: '2026-01-05T00:00:00Z'});
    const onRestore = vi.fn().mockResolvedValue(undefined);
    const onEmptyTrash = vi.fn().mockResolvedValue(undefined);
    const store = createMockStore();

    await act(async () => {
      root.render(
        <SavedPasswordsContent
          actionStatus={null}
          data={{
            items: [],
            trashItems: [trashedItem],
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
            vaultStatus: createVaultStatus(),
          } as never}
          drawer={null}
          loadSavedPasswords={vi.fn() as never}
          onAdd={vi.fn() as never}
          onCloseDrawer={vi.fn()}
          onDelete={vi.fn() as never}
          onEdit={vi.fn() as never}
          onEmptyTrash={onEmptyTrash}
          onOpenAdd={vi.fn()}
          onOpenEdit={vi.fn()}
          onRestore={onRestore}
          onSearchChange={vi.fn()}
          onSecretAction={vi.fn() as never}
          passwordsEnabled
          searchInputValue=""
          searchQuery=""
          store={store}
          transientResetToken={0}
        />
      );
    });

    const trashCategoryBtn = Array.from(document.querySelectorAll('button')).find(b => b.textContent?.includes('Trash'));
    expect(trashCategoryBtn).toBeDefined();

    await act(async () => {
      trashCategoryBtn!.click();
    });

    expect(container.textContent).toContain('Old Service');

    const restoreBtn = buttonWithLabel('Restore Old Service');
    await act(async () => {
      restoreBtn.click();
    });
    expect(onRestore).toHaveBeenCalledWith(trashedItem);

    const emptyTrashBtn = buttonWithLabel('Empty trash');
    await act(async () => {
      emptyTrashBtn.click();
    });

    const confirmEmptyBtn = Array.from(document.querySelectorAll('button')).find(b => b.textContent?.trim() === 'Empty trash' && b !== emptyTrashBtn);
    expect(confirmEmptyBtn).toBeDefined();

    await act(async () => {
      confirmEmptyBtn!.click();
    });
    expect(onEmptyTrash).toHaveBeenCalled();
  });
});

describe('Maho Native Vault Parity - Operation failures', () => {
  it('keeps the open flow on reauth_failed so the error is shown, resets only on lock', () => {
    type OperationResult = Parameters<typeof operationResetsTransientState>[0];
    const failure = (errorCode: string): OperationResult => ({
      success: false,
      errorCode,
      errorMessage: 'Vault operation failed.',
    } as unknown as OperationResult);

    expect(operationResetsTransientState(failure('reauth_failed'))).toBe(false);
    expect(operationResetsTransientState(failure('locked'))).toBe(true);
    expect(operationResetsTransientState({
      ...failure('reauth_failed'),
      status: {lockState: VaultLockState.kLocked},
    } as unknown as OperationResult)).toBe(true);
  });
});

describe('Maho Native Vault Parity - Detail revision freshness', () => {
  it('acts on the reloaded item revision after the list refreshes under an open detail view', async () => {
    const store = createMockStore();
    const onToggleFavorite = vi.fn().mockResolvedValue(undefined);
    const render = (item: VaultItem) => (
      <SavedPasswordsContent
        actionStatus={null}
        data={{
          items: [item],
          trashItems: [],
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
          vaultStatus: createVaultStatus(),
        } as never}
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
        onToggleFavorite={onToggleFavorite}
        passwordsEnabled
        searchInputValue=""
        searchQuery=""
        store={store}
        transientResetToken={0}
      />
    );

    await act(async () => {
      root.render(render(createMockItem({revision: 1n})));
    });
    await act(async () => {
      Array.from(document.querySelectorAll('button')).find(b => b.textContent?.includes('GitHub'))!.click();
    });
    // A secret copy bumps the revision and reloads the list.
    await act(async () => {
      root.render(render(createMockItem({revision: 2n})));
    });
    const dialog = document.querySelector('[role="dialog"]')!;
    const favorite = dialog.querySelector<HTMLButtonElement>('button[aria-label="Add to favorites"]')!;
    await act(async () => {
      favorite.click();
    });
    expect(onToggleFavorite).toHaveBeenCalledWith(expect.objectContaining({revision: 2n}));
  });
});

describe('Maho Native Vault Parity - Error messages', () => {
  it('surfaces the specific error code instead of the generic native message', () => {
    type OperationResult = Parameters<typeof vaultOperationMessage>[0];
    const failure = (errorCode: string | null): OperationResult => ({
      success: false,
      errorCode,
      errorMessage: 'Vault operation failed.',
    } as unknown as OperationResult);

    expect(vaultOperationMessage(failure('revision_conflict'), 'x')).toContain('changed since it was loaded');
    expect(vaultOperationMessage(failure('reauth_failed'), 'x')).toContain('verification failed');
    expect(vaultOperationMessage(failure('unmapped_code'), 'x')).toBe('Vault operation failed.');
    expect(vaultOperationMessage({success: false, errorCode: null, errorMessage: null} as unknown as OperationResult, 'fallback')).toBe('fallback');
  });
});

describe('Maho Native Vault Parity - Edit drawer username', () => {
  it('prefills the stored username and never saves the masked hint', async () => {
    const item = createMockItem({usernameHint: 'o••••t'});
    const onSave = vi.fn().mockResolvedValue(null);
    const store = createMockStore();
    await act(async () => {
      root.render(<EditSavedPasswordDrawer item={item} onClose={vi.fn()} onSave={onSave} store={store} />);
    });
    const usernameInput = document.querySelector<HTMLInputElement>('#edit-saved-password-username');
    expect(usernameInput?.value).toBe('octocat@example.com');
    await act(async () => {
      buttonWithLabel('Save changes').click();
    });
    expect(onSave).toHaveBeenCalledWith(item, expect.objectContaining({username: 'octocat@example.com'}));
  });

  it('keeps save disabled when the stored username cannot be loaded', async () => {
    const item = createMockItem({usernameHint: 'o••••t'});
    const onSave = vi.fn().mockResolvedValue(null);
    const store = createMockStore({
      getVaultItemNotes: vi.fn().mockResolvedValue({success: false, notes: null, errorCode: 'vault_locked', username: null}),
    });
    await act(async () => {
      root.render(<EditSavedPasswordDrawer item={item} onClose={vi.fn()} onSave={onSave} store={store} />);
    });
    expect(buttonWithLabel('Save changes').disabled).toBe(true);
    expect(document.querySelector<HTMLInputElement>('#edit-saved-password-username')?.value).toBe('');
  });
});
