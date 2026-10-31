// Copyright 2026 Maho Browser. All rights reserved.

import {VaultItemKind, VaultLockState} from '../mojo.js';
import type {VaultPreflightState} from '../mojo.js';
import type {
  PasswordProviderStatus,
  VaultItem,
  VaultOperationResult,
} from '../mojo.js';

export type VaultFilterCategory =
    | 'all'
    | 'favorites'
    | 'logins'
    | 'authenticator'
    | 'secure-notes'
    | 'trash';

export type SavedPasswordSortField =
    | 'name'
    | 'recently-used'
    | 'date-created'
    | 'date-updated';

export type SavedPasswordSortDirection = 'asc' | 'desc';

export type SavedPasswordsPaneData = {
  readonly items: readonly VaultItem[];
  readonly trashItems?: readonly VaultItem[];
  readonly providerStatus: PasswordProviderStatus;
  readonly vaultPreflightState: VaultPreflightState;
  readonly vaultStatus: VaultOperationResult;
};

export type SavedPasswordLoadState =
    | {readonly status: 'loading'}
    | {readonly status: 'error'; readonly message: string}
    | {readonly data: SavedPasswordsPaneData; readonly status: 'loaded'};

export type LoadSavedPasswords = (showLoading?: boolean) => Promise<void>;

export type SavedPasswordActionStatus = {
  readonly itemId: string;
  readonly message: string;
  readonly status: 'failed' | 'success';
};

export type SavedPasswordDrawerState =
    | {readonly kind: 'add'; readonly initialKind?: VaultItemKind}
    | {readonly item: VaultItem; readonly kind: 'edit'}
    | null;

export type AddSavedPasswordInput = {
  readonly title?: string;
  readonly origin: string;
  readonly origins?: readonly string[];
  readonly password: string;
  readonly username: string;
  readonly notes?: string;
  readonly totpSecret?: string;
};

export type EditSavedPasswordInput = {
  readonly title?: string;
  readonly origin: string;
  readonly origins?: readonly string[];
  /** null means keep the existing secret unchanged. */
  readonly password: string | null;
  readonly username: string;
  readonly notes?: string;
  readonly totpSecret?: string;
};

export type AddSecureNoteInput = {
  readonly title: string;
  readonly notes: string;
};

export type EditSecureNoteInput = {
  readonly title: string;
  readonly notes: string;
};

export function createSavedPasswordLoadingState(): SavedPasswordLoadState {
  return {status: 'loading'};
}

export function createSavedPasswordErrorState(message: string): SavedPasswordLoadState {
  return {message, status: 'error'};
}

export function createSavedPasswordLoadedState(
    data: SavedPasswordsPaneData): SavedPasswordLoadState {
  return {data, status: 'loaded'};
}

export function getPrimaryOrigin(item: VaultItem): string {
  return item.origins[0] ?? '';
}

export function getDisplayName(item: VaultItem): string {
  return item.usernameHint || item.title || getPrimaryOrigin(item) || 'Saved login';
}

export function filterSavedPasswordItems(
    items: readonly VaultItem[], query: string): VaultItem[] {
  const normalized = query.trim().toLowerCase();
  if (!normalized) {
    return [...items];
  }

  return items.filter(item => {
    if (item.title && item.title.toLowerCase().includes(normalized)) {
      return true;
    }
    if (item.usernameHint && item.usernameHint.toLowerCase().includes(normalized)) {
      return true;
    }
    return item.origins.some(origin => origin.toLowerCase().includes(normalized));
  });
}

export function filterByCategory(
    items: readonly VaultItem[],
    trashItems: readonly VaultItem[],
    category: VaultFilterCategory): VaultItem[] {
  switch (category) {
    case 'favorites':
      return items.filter(item => item.favorite && !item.trashedAt);
    case 'logins':
      return items.filter(item => item.itemKind === VaultItemKind.kLogin && !item.trashedAt);
    case 'authenticator':
      return items.filter(item => item.hasTotp && !item.trashedAt);
    case 'secure-notes':
      return items.filter(item => item.itemKind === VaultItemKind.kSecureItem && !item.trashedAt);
    case 'trash':
      return [...trashItems];
    case 'all':
    default:
      return items.filter(item => !item.trashedAt);
  }
}

export function sortSavedPasswordItems(
    items: readonly VaultItem[],
    field: SavedPasswordSortField,
    direction: SavedPasswordSortDirection = 'asc'): VaultItem[] {
  const sorted = [...items];
  const mult = direction === 'asc' ? 1 : -1;
  sorted.sort((a, b) => {
    if (field === 'name') {
      const nameA = (a.title || getPrimaryOrigin(a) || a.usernameHint || '').toLowerCase();
      const nameB = (b.title || getPrimaryOrigin(b) || b.usernameHint || '').toLowerCase();
      return nameA.localeCompare(nameB) * mult;
    }
    if (field === 'recently-used') {
      const timeA = a.lastUsedAt ? new Date(a.lastUsedAt).getTime() : 0;
      const timeB = b.lastUsedAt ? new Date(b.lastUsedAt).getTime() : 0;
      return (timeB - timeA) * mult;
    }
    if (field === 'date-created') {
      const timeA = a.createdAt ? new Date(a.createdAt).getTime() : 0;
      const timeB = b.createdAt ? new Date(b.createdAt).getTime() : 0;
      return (timeB - timeA) * mult;
    }
    if (field === 'date-updated') {
      const timeA = a.updatedAt ? new Date(a.updatedAt).getTime() : 0;
      const timeB = b.updatedAt ? new Date(b.updatedAt).getTime() : 0;
      return (timeB - timeA) * mult;
    }
    return 0;
  });
  return sorted;
}

export function normalizeCredentialOrigin(input: string): string | null {
  const trimmed = input.trim();
  if (!trimmed) {
    return null;
  }
  const candidate = trimmed.includes('://') ? trimmed : `https://${trimmed}`;

  try {
    const url = new URL(candidate);
    if (!url.hostname) {
      return null;
    }
    return url.origin;
  } catch (error) {
    if (error instanceof TypeError) {
      return null;
    }
    throw error;
  }
}

export function operationResetsTransientState(result: VaultOperationResult): boolean {
  const lockState = result.status?.lockState;
  if (lockState !== undefined && lockState !== VaultLockState.kUnlocked) {
    return true;
  }
  // A declined/failed device reauth leaves the Vault unlocked: keep the open
  // flow so the caller can surface the error instead of silently closing it.
  return result.errorCode === 'locked';
}

const VAULT_ERROR_CODE_MESSAGES: Readonly<Record<string, string>> = {
  locked: 'Vault is locked. Unlock it and try again.',
  vault_locked: 'Vault is locked. Unlock it and try again.',
  revision_conflict: 'This item changed since it was loaded. Refresh and try again.',
  not_found: 'This item no longer exists.',
  reauth_failed: 'Device verification failed or was cancelled.',
  reauth_required: 'Device verification is required for this action.',
  reauth_in_progress: 'Another verification is already in progress.',
  storage_failure: 'Vault storage could not be updated.',
  core_unavailable: 'Vault service is unavailable.',
  profile_not_allowed: 'Vault is not available for this profile.',
  secret_unavailable: 'This secret is not available.',
  invalid_response: 'Vault returned an unexpected response.',
};

// The native layer sends a generic errorMessage for every failure, so the
// errorCode carries the actual cause and wins when it is a known code.
export function vaultOperationMessage(
    result: VaultOperationResult, fallback: string): string {
  const known = result.errorCode ? VAULT_ERROR_CODE_MESSAGES[result.errorCode] : undefined;
  return known || result.errorMessage || result.errorCode || fallback;
}
