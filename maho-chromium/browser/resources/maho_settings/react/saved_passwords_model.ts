// Copyright 2026 Maho Browser. All rights reserved.

import {VaultLockState} from '../mojo.js';
import type {VaultPreflightState} from '../mojo.js';
import type {
  PasswordProviderStatus,
  VaultItem,
  VaultOperationResult,
} from '../mojo.js';

export type SavedPasswordsPaneData = {
  readonly items: readonly VaultItem[];
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
    | {readonly kind: 'add'}
    | {readonly item: VaultItem; readonly kind: 'edit'}
    | null;

export type AddSavedPasswordInput = {
  readonly origin: string;
  readonly password: string;
  readonly username: string;
};

export type EditSavedPasswordInput = {
  readonly origin: string;
  /** null means keep the existing secret unchanged. */
  readonly password: string | null;
  readonly username: string;
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
    if (item.title.toLowerCase().includes(normalized) ||
        item.usernameHint.toLowerCase().includes(normalized)) {
      return true;
    }
    return item.origins.some(origin => origin.toLowerCase().includes(normalized));
  });
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
  return result.errorCode === 'locked' || result.errorCode === 'reauth_failed';
}

export function vaultOperationMessage(
    result: VaultOperationResult, fallback: string): string {
  return result.errorMessage || result.errorCode || fallback;
}
