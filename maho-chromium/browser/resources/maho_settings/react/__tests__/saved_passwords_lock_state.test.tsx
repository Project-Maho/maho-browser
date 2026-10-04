// Copyright 2026 Maho Browser. All rights reserved.

/**
 * Saved-password lock-state regression coverage. The callback-router event is
 * the synchronization boundary: a lock closes transient credential UI, then
 * the exact status response transitions the pane to the unlock gate without
 * leaving copy, fill, or save affordances reachable.
 */

import React, {act} from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import {SavedPasswordsPane} from '../passwords_library.js';
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

type Deferred<T> = {
  readonly promise: Promise<T>;
  resolve(value: T): void;
};

function deferred<T>(): Deferred<T> {
  let resolve!: (value: T) => void;
  const promise = new Promise<T>(settle => {
    resolve = settle;
  });
  return {promise, resolve};
}

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
      itemCount: lockState === VaultLockState.kUnlocked ? 1n : 0n,
      autoLockMinutes: 15,
      failedUnlockCount: 0,
      retryAtTimestamp: null,
    },
    item: null,
  };
}

function createItem(): VaultItem {
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
  };
}

function exactButton(label: string): HTMLButtonElement | null {
  return Array.from(document.querySelectorAll<HTMLButtonElement>('button'))
      .find(button => button.textContent?.trim() === label || button.getAttribute('aria-label') === label || button.getAttribute('aria-label')?.startsWith(`${label} for `)) ?? null;
}

async function clickDeleteMenuAction(): Promise<void> {
  const more = exactButton('More options');
  if (!more) {
    throw new Error('missing More options button');
  }
  await act(async () => {
    more.dispatchEvent(new KeyboardEvent("keydown", {key: "Enter", bubbles: true}));
  });
  const deleteItem = Array.from(document.querySelectorAll<HTMLElement>('[role="menuitem"]'))
      .find(item => item.textContent?.trim() === 'Delete');
  if (!deleteItem) {
    throw new Error('missing Delete menu item');
  }
  await act(async () => {
    deleteItem.click();
  });
}

const PROVIDER_STATUS = {
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
};

const SAVED_PASSWORDS_PANE = {
  key: 'saved-passwords',
  scope: 'core-global',
  selectedProfileSupport: 'host-only',
  symbol: 'SP',
  navTitle: 'Saved Passwords',
  title: 'Saved Passwords',
  kind: 'live',
  domain: 'identity',
  contentKind: 'saved-passwords',
} as const;

function setInputValue(element: HTMLInputElement, value: string): void {
  const setter = Object.getOwnPropertyDescriptor(
      window.HTMLInputElement.prototype, 'value')!.set!;
  act(() => {
    setter.call(element, value);
    element.dispatchEvent(new Event('input', {bubbles: true}));
  });
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

describe('Saved Passwords lock-state callback', () => {
  it('clears an open save flow and replaces credential actions with the unlock gate', async () => {
    const initialStatus = deferred<{result: VaultOperationResult}>();
    const lockedStatus = deferred<{result: VaultOperationResult}>();
    const statusResponses = [initialStatus.promise, lockedStatus.promise];
    const item = createItem();
    const useVaultSecret = vi.fn();
    const addVaultLogin = vi.fn();
    const updateVaultLogin = vi.fn();
    const listVaultItems = vi.fn().mockResolvedValue({
      result: {
        success: true,
        items: [item],
        nextCursor: null,
        errorCode: null,
        errorMessage: null,
      },
    });
    const handler = {
      addVaultLogin,
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
      getVaultPreflightState: vi.fn().mockResolvedValue({
        state: VaultPreflightState.kHealthy,
      }),
      getVaultStatus: vi.fn(() => {
        const response = statusResponses.shift();
        if (!response) {
          throw new Error('unexpected Vault status request');
        }
        return response;
      }),
      listVaultItems,
      updateVaultLogin,
      useVaultSecret,
    };

    let lockStateListener: (() => void) | null = null;
    const removeListener = vi.fn();
    const callbackRouter = {
      onVaultLockStateChanged: {
        addListener: vi.fn((listener: () => void) => {
          lockStateListener = listener;
          return 41;
        }),
      },
      removeListener,
    };
    const store = {
      getCallbackRouter: () => callbackRouter,
      getHandler: () => handler,
      selectPane: vi.fn(),
    };

    await act(async () => {
      root.render(
          <SavedPasswordsPane
            pane={{
              key: 'saved-passwords',
              scope: 'core-global',
              selectedProfileSupport: 'host-only',
              symbol: 'SP',
              navTitle: 'Saved Passwords',
              title: 'Saved Passwords',
              kind: 'live',
              domain: 'identity',
              contentKind: 'saved-passwords',
            }}
            settings={[{key: 'autofill.passwords_enabled', value: 'true'}]}
            store={store as never}
          />);
      initialStatus.resolve({result: createVaultResult(VaultLockState.kUnlocked)});
      await initialStatus.promise;
    });

    expect(container.textContent).toContain('example.com');
    expect(exactButton('Copy')).not.toBeNull();
    expect(exactButton('Add password')).not.toBeNull();
    expect(exactButton('Fill')).toBeNull();
    expect(listVaultItems).toHaveBeenCalledTimes(2);

    await act(async () => {
      exactButton('Add password')!.click();
    });
    expect(document.querySelector('#saved-password-value')).not.toBeNull();
    expect(exactButton('Save password')).not.toBeNull();

    act(() => {
      lockStateListener?.();
    });

    // The event itself synchronously clears transient save/edit state, before
    // the asynchronous status refresh is allowed to complete.
    expect(document.querySelector('#saved-password-value')).toBeNull();
    expect(exactButton('Save password')).toBeNull();

    await act(async () => {
      lockedStatus.resolve({result: createVaultResult(VaultLockState.kAutoLocked)});
      await lockedStatus.promise;
    });

    expect(container.textContent).toContain('Maho Vault is locked');
    expect(container.textContent).not.toContain('https://example.com');
    expect(exactButton('Unlock Vault')).not.toBeNull();
    expect(exactButton('Copy')).toBeNull();
    expect(exactButton('Fill')).toBeNull();
    expect(exactButton('Add password')).toBeNull();
    expect(exactButton('Save password')).toBeNull();
    expect(exactButton('Save changes')).toBeNull();
    expect(listVaultItems).toHaveBeenCalledTimes(2);
    expect(useVaultSecret).not.toHaveBeenCalled();
    expect(addVaultLogin).not.toHaveBeenCalled();
    expect(updateVaultLogin).not.toHaveBeenCalled();

    act(() => {
      root.unmount();
    });
    expect(removeListener).toHaveBeenCalledWith(41);
    root = createRoot(container);
  });

  it('closes the delete confirmation on lock and keeps it closed after unlock',
     async () => {
       const initialStatus = deferred<{result: VaultOperationResult}>();
       const lockedStatus = deferred<{result: VaultOperationResult}>();
       const unlockedStatus = deferred<{result: VaultOperationResult}>();
       const statusResponses = [
         initialStatus.promise,
         lockedStatus.promise,
         unlockedStatus.promise,
       ];
       const item = createItem();
       const deleteVaultItem = vi.fn();
       const listResult = {
         result: {
           success: true,
           items: [item],
           nextCursor: null,
           errorCode: null,
           errorMessage: null,
         },
       };
       const handler = {
         deleteVaultItem,
         getPasswordProviderStatus:
             vi.fn().mockResolvedValue({status: PROVIDER_STATUS}),
         getVaultPreflightState: vi.fn().mockResolvedValue({
           state: VaultPreflightState.kHealthy,
         }),
         getVaultStatus: vi.fn(() => {
           const response = statusResponses.shift();
           if (!response) {
             throw new Error('unexpected Vault status request');
           }
           return response;
         }),
         listVaultItems: vi.fn().mockResolvedValue(listResult),
       };

       let lockStateListener: (() => void) | null = null;
       const callbackRouter = {
         onVaultLockStateChanged: {
           addListener: vi.fn((listener: () => void) => {
             lockStateListener = listener;
             return 42;
           }),
         },
         removeListener: vi.fn(),
       };
       const store = {
         getCallbackRouter: () => callbackRouter,
         getHandler: () => handler,
         selectPane: vi.fn(),
       };

       await act(async () => {
         root.render(
             <SavedPasswordsPane
               pane={SAVED_PASSWORDS_PANE as never}
               settings={[{key: 'autofill.passwords_enabled', value: 'true'}]}
               store={store as never}
             />);
         initialStatus.resolve(
             {result: createVaultResult(VaultLockState.kUnlocked)});
         await initialStatus.promise;
       });

       await clickDeleteMenuAction();
       const confirmInput = document.querySelector<HTMLInputElement>(
           '#delete-confirm-input')!;
       expect(confirmInput).not.toBeNull();
       setInputValue(confirmInput, 'https://example.com');

       act(() => {
         lockStateListener?.();
       });

       // The lock event alone must retire the confirmation, before the async
       // status refresh resolves.
       expect(document.querySelector('#delete-confirm-input')).toBeNull();
       expect(document.querySelector('[data-testid="delete-confirm-origin"]'))
           .toBeNull();
       expect(exactButton('Delete password')).toBeNull();
       expect(deleteVaultItem).not.toHaveBeenCalled();

       await act(async () => {
         lockedStatus.resolve(
             {result: createVaultResult(VaultLockState.kAutoLocked)});
         await lockedStatus.promise;
       });
       expect(container.textContent).toContain('Maho Vault is locked');
       expect(document.querySelector('#delete-confirm-input')).toBeNull();

       // Unlocking restores the management UI without resurrecting the stale
       // confirmation.
       act(() => {
         lockStateListener?.();
       });
       await act(async () => {
         unlockedStatus.resolve(
             {result: createVaultResult(VaultLockState.kUnlocked)});
         await unlockedStatus.promise;
       });

       expect(container.textContent).toContain('example.com');
       expect(exactButton('More options')).not.toBeNull();
       expect(document.querySelector('#delete-confirm-input')).toBeNull();
       expect(document.querySelector('[data-testid="delete-confirm-origin"]'))
           .toBeNull();
       expect(deleteVaultItem).not.toHaveBeenCalled();
     });
});

describe('Saved Passwords trash listing', () => {
  it('requests trashed items separately so the Trash view shows them', async () => {
    const active = createItem();
    const trashed = {...createItem(), id: 'trashed-item', title: 'Trashed', trashedAt: '2026-09-29T00:00:00Z'};
    // Mirrors the core contract: trashed items are returned only when trashOnly is set.
    const listVaultItems = vi.fn((_p: unknown, _k: unknown, _c: unknown, _l: unknown, trashOnly?: boolean) =>
      Promise.resolve({
        result: {success: true, items: trashOnly ? [trashed] : [active], nextCursor: null, errorCode: null, errorMessage: null},
      }));
    const handler = {
      getPasswordProviderStatus: vi.fn().mockResolvedValue({status: PROVIDER_STATUS}),
      getVaultPreflightState: vi.fn().mockResolvedValue({state: VaultPreflightState.kHealthy}),
      getVaultStatus: vi.fn().mockResolvedValue({result: createVaultResult(VaultLockState.kUnlocked)}),
      listVaultItems,
    };
    const store = {
      getCallbackRouter: () => ({onVaultLockStateChanged: {addListener: vi.fn(() => 43)}, removeListener: vi.fn()}),
      getHandler: () => handler,
      selectPane: vi.fn(),
    };

    await act(async () => {
      root.render(
          <SavedPasswordsPane
            pane={SAVED_PASSWORDS_PANE as never}
            settings={[{key: 'autofill.passwords_enabled', value: 'true'}]}
            store={store as never}
          />);
    });

    expect(listVaultItems.mock.calls.some(call => call[4] === true)).toBe(true);
    const trashButton = document.querySelector<HTMLElement>('[data-category="trash"]')!;
    expect(trashButton.textContent).toContain('1');
    await act(async () => {
      trashButton.click();
    });
    expect(container.textContent).toContain('Trashed');
    expect(container.textContent).not.toContain('Trash is empty');
  });
});

describe('Saved Passwords stale-revision retry', () => {
  it('re-resolves the current revision and retries trash once on revision_conflict', async () => {
    const stale = createItem();
    const fresh = {...stale, revision: 2n};
    let listed = stale;
    const listVaultItems = vi.fn((_p, _k, _c, _l, trashOnly: boolean) => Promise.resolve({
      result: {success: true, items: trashOnly ? [] : [listed], nextCursor: null, errorCode: null, errorMessage: null},
    }));
    const conflict = {success: false, errorCode: 'revision_conflict', errorMessage: null, status: null, item: null};
    const trashVaultItem = vi.fn((_id: string, revision: bigint) => Promise.resolve({
      result: revision === 2n ? {...conflict, success: true, errorCode: null} : conflict,
    }));
    const handler = {
      getPasswordProviderStatus: vi.fn().mockResolvedValue({status: PROVIDER_STATUS}),
      getVaultPreflightState: vi.fn().mockResolvedValue({state: VaultPreflightState.kHealthy}),
      getVaultStatus: vi.fn().mockResolvedValue({result: createVaultResult(VaultLockState.kUnlocked)}),
      listVaultItems,
      trashVaultItem,
    };
    const store = {
      getCallbackRouter: () => ({onVaultLockStateChanged: {addListener: vi.fn(() => 1)}, removeListener: vi.fn()}),
      getHandler: () => handler,
      selectPane: vi.fn(),
    };
    await act(async () => {
      root.render(
          <SavedPasswordsPane
            pane={SAVED_PASSWORDS_PANE as never}
            settings={[{key: 'autofill.passwords_enabled', value: 'true'}]}
            store={store as never}
          />);
    });
    // The row still holds revision 1; the core has moved on to revision 2.
    listed = fresh;
    const more = exactButton('More options');
    expect(more).not.toBeNull();
    await act(async () => {
      more!.dispatchEvent(new KeyboardEvent('keydown', {key: 'Enter', bubbles: true}));
    });
    const trash = document.querySelector<HTMLElement>('[data-action="move-to-trash"]');
    expect(trash).not.toBeNull();
    await act(async () => {
      trash!.click();
    });
    expect(trashVaultItem.mock.calls.map(call => call[1])).toEqual([1n, 2n]);
    expect(container.textContent).not.toContain('This item changed since it was loaded');
  });
});
