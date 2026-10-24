// Copyright 2026 Maho Browser. All rights reserved.

import {describe, expect, it} from 'vitest';

import {VaultLockState} from '../mojo.js';
import {getVaultAccessCopy} from './passwords_vault_gate.js';

describe('Vault access copy', () => {
  it('uses setup language only for an uninitialized Vault', () => {
    expect(getVaultAccessCopy(VaultLockState.kUninitialized)).toEqual({
      actionLabel: 'Initialize Vault',
      description:
          'Signing in sets up your Maho Vault automatically - its key follows your account. You can also set one up here with a passphrase.',
      mode: 'initialize',
      title: 'Set up Maho Vault',
    });
  });

  it.each([
    VaultLockState.kLocked,
    VaultLockState.kAutoLocked,
  ])('uses unlock language for initialized locked state %s', lockState => {
    const copy = getVaultAccessCopy(lockState);
    expect(copy.mode).toBe('unlock');
    expect(copy.title).toBe('Unlock Maho Vault');
    expect(copy.actionLabel).toBe('Unlock Vault');
    expect(copy.description).toContain('unlocks when you sign in');
  });
});

describe('Vault access copy while signed out', () => {
  it('offers sign-in for an account-escrowed Vault with no seed in memory', () => {
    const copy = getVaultAccessCopy(VaultLockState.kLocked, true);
    expect(copy.mode).toBe('sign-in');
    expect(copy.signInActionLabel).toBe('Sign in to unlock your vault');
    expect(copy.title).toBe('Unlock Maho Vault');
  });

  it('keeps setup copy for an uninitialized Vault even when signed out', () => {
    expect(getVaultAccessCopy(VaultLockState.kUninitialized, true).mode)
        .toBe('initialize');
  });

  it('omits the sign-in action while the account seed is present', () => {
    expect(getVaultAccessCopy(VaultLockState.kLocked, false).signInActionLabel)
        .toBeUndefined();
  });
});
