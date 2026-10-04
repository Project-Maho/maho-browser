// Copyright 2026 Maho Browser. All rights reserved.

import React, {useEffect, useRef, useState} from 'react';

import {Input} from '@ui/input';
import {VaultLockState} from '../mojo.js';
import type {VaultOperationResult} from '../mojo.js';
import {
  ActionButton,
  ManagedSettingRow,
  SETTING_ACTIONS_CLASS,
  SETTING_CONTROL_STACK_CLASS,
  SETTING_CONTROL_STACKED_CLASS,
  SETTING_DESCRIPTION_CLASS,
} from './domain_panes.js';
import type {LoadPasswords} from './passwords_model.js';
import type {MahoSettingsStore} from './store.js';

const FIELD_GROUP_CLASS = 'grid gap-1.5';
const FIELD_LABEL_CLASS = 'text-xs font-medium text-foreground';
const FIELD_HELP_CLASS = 'text-xs leading-5 text-muted-foreground';
const ERROR_CLASS = 'text-xs leading-5 text-destructive';
const RECOVERY_KIT_CLASS =
    'grid gap-2 rounded-lg border border-border/60 bg-muted/20 p-3';
const ACKNOWLEDGE_CLASS = 'flex items-start gap-2 text-xs leading-5 text-foreground';

export const MIN_MASTER_PASSPHRASE_LENGTH = 8;

export const VAULT_SETUP_MESSAGES = {
  masterPassphraseRequired: 'Enter a master passphrase.',
  masterPassphraseTooShort:
      `Use at least ${MIN_MASTER_PASSPHRASE_LENGTH} characters for the master passphrase.`,
  confirmPassphraseRequired: 'Re-enter the master passphrase to confirm it.',
  passphraseMismatch: 'The master passphrases do not match.',
  recoverySecretRequired: 'Enter a recovery secret.',
  confirmRecoveryRequired: 'Re-enter the recovery secret to confirm it.',
  recoveryMismatch: 'The recovery secrets do not match.',
  recoveryKitNotAcknowledged:
      'Confirm that you saved the recovery kit before continuing.',
  unlockPassphraseRequired: 'Enter your master passphrase to unlock the Vault.',
} as const;

export type VaultSetupFieldId =
    'masterPassphrase'|'confirmPassphrase'|'recoverySecret'|'confirmRecovery'|
    'recoveryKitAcknowledged';

export interface VaultSetupValidationInput {
  readonly masterPassphrase: string;
  readonly confirmPassphrase: string;
  readonly recoverySecret: string;
  readonly confirmRecovery: string;
  readonly recoveryKitAcknowledged: boolean;
}

export interface VaultSetupValidationFailure {
  readonly field: VaultSetupFieldId;
  readonly message: string;
}

export interface VaultAccessCopy {
  readonly actionLabel: 'Initialize Vault'|'Unlock Vault';
  readonly description: string;
  readonly mode: 'initialize'|'unlock';
  readonly title: 'Set up Maho Vault'|'Unlock Maho Vault';
}

/**
 * Keeps the never-initialized Vault setup path semantically separate from an
 * initialized Vault that is merely locked. Callers can use this same copy for
 * both the gate itself and regression assertions without inspecting secrets.
 */
export function getVaultAccessCopy(lockState: VaultLockState): VaultAccessCopy {
  if (lockState === VaultLockState.kUninitialized) {
    return {
      actionLabel: 'Initialize Vault',
      description:
          'Signing in sets up your Maho Vault automatically - its key follows your account. You can also set one up here with a passphrase.',
      mode: 'initialize',
      title: 'Set up Maho Vault',
    };
  }
  return {
    actionLabel: 'Unlock Vault',
    description:
        'Your Maho Vault unlocks when you sign in. After auto-lock, confirm it is you or enter your passphrase.',
    mode: 'unlock',
    title: 'Unlock Maho Vault',
  };
}

/**
 * Returns the first blocking failure for Vault initialization, or null when the
 * form may be submitted. Field order matches visual order so refocus lands on
 * the first offending control. Pure: no secret is retained here.
 */
export function validateVaultSetup(
    input: VaultSetupValidationInput): VaultSetupValidationFailure|null {
  if (input.masterPassphrase.length === 0) {
    return {field: 'masterPassphrase', message: VAULT_SETUP_MESSAGES.masterPassphraseRequired};
  }
  if (input.masterPassphrase.length < MIN_MASTER_PASSPHRASE_LENGTH) {
    return {field: 'masterPassphrase', message: VAULT_SETUP_MESSAGES.masterPassphraseTooShort};
  }
  if (input.confirmPassphrase.length === 0) {
    return {field: 'confirmPassphrase', message: VAULT_SETUP_MESSAGES.confirmPassphraseRequired};
  }
  if (input.masterPassphrase !== input.confirmPassphrase) {
    return {field: 'confirmPassphrase', message: VAULT_SETUP_MESSAGES.passphraseMismatch};
  }
  if (input.recoverySecret.length === 0) {
    return {field: 'recoverySecret', message: VAULT_SETUP_MESSAGES.recoverySecretRequired};
  }
  if (input.confirmRecovery.length === 0) {
    return {field: 'confirmRecovery', message: VAULT_SETUP_MESSAGES.confirmRecoveryRequired};
  }
  if (input.recoverySecret !== input.confirmRecovery) {
    return {field: 'confirmRecovery', message: VAULT_SETUP_MESSAGES.recoveryMismatch};
  }
  if (!input.recoveryKitAcknowledged) {
    return {
      field: 'recoveryKitAcknowledged',
      message: VAULT_SETUP_MESSAGES.recoveryKitNotAcknowledged,
    };
  }
  return null;
}

/** Returns the blocking failure for the unlock form, or null. */
export function validateVaultUnlock(masterPassphrase: string):
    VaultSetupValidationFailure|null {
  if (masterPassphrase.length === 0) {
    return {field: 'masterPassphrase', message: VAULT_SETUP_MESSAGES.unlockPassphraseRequired};
  }
  return null;
}

type VaultAccessGateProps = {
  readonly loadPasswords: LoadPasswords;
  readonly searchQuery: string;
  readonly store: MahoSettingsStore;
  readonly vaultResult: VaultOperationResult;
};

export function VaultAccessGate(
    {loadPasswords, searchQuery, store, vaultResult}: VaultAccessGateProps) {
  const handler = store.getHandler();
  const mountedRef = useRef(true);
  const masterInputRef = useRef<HTMLInputElement | null>(null);
  const confirmPassphraseInputRef = useRef<HTMLInputElement | null>(null);
  const recoveryInputRef = useRef<HTMLInputElement | null>(null);
  const confirmRecoveryInputRef = useRef<HTMLInputElement | null>(null);
  const acknowledgeInputRef = useRef<HTMLInputElement | null>(null);
  // React detaches refs before passive-effect cleanup runs, so unmount
  // scrubbing needs its own registry of the secret inputs that were mounted.
  const secretNodesRef = useRef(new Set<HTMLInputElement>());
  const bindSecretInput =
      (ref: React.MutableRefObject<HTMLInputElement | null>) =>
          (node: HTMLInputElement | null) => {
            ref.current = node;
            if (node) {
              secretNodesRef.current.add(node);
            }
          };

  const [masterPassphrase, setMasterPassphrase] = useState('');
  const [confirmPassphrase, setConfirmPassphrase] = useState('');
  const [recoverySecret, setRecoverySecret] = useState('');
  const [confirmRecovery, setConfirmRecovery] = useState('');
  const [recoveryKitAcknowledged, setRecoveryKitAcknowledged] = useState(false);
  const [busy, setBusy] = useState(false);
  const [errorMessage, setErrorMessage] = useState<string | null>(null);

  const lockState = vaultResult.status?.lockState ?? VaultLockState.kUninitialized;
  const accessCopy = getVaultAccessCopy(lockState);
  const needsInitialize = accessCopy.mode === 'initialize';

  const clearSecrets = () => {
    setMasterPassphrase('');
    setConfirmPassphrase('');
    setRecoverySecret('');
    setConfirmRecovery('');
    setRecoveryKitAcknowledged(false);
    scrubSecretNodes(secretNodesRef.current);
  };

  useEffect(() => {
    const secretNodes = secretNodesRef.current;
    return () => {
      mountedRef.current = false;
      scrubSecretNodes(secretNodes);
      secretNodes.clear();
    };
  }, []);

  const focusField = (field: VaultSetupFieldId) => {
    const target = field === 'masterPassphrase' ? masterInputRef.current :
        field === 'confirmPassphrase' ? confirmPassphraseInputRef.current :
        field === 'recoverySecret' ? recoveryInputRef.current :
        field === 'confirmRecovery' ? confirmRecoveryInputRef.current :
        acknowledgeInputRef.current;
    target?.focus();
  };

  const submit = async () => {
    if (busy) {
      return;
    }
    const failure = needsInitialize ?
        validateVaultSetup({
          masterPassphrase,
          confirmPassphrase,
          recoverySecret,
          confirmRecovery,
          recoveryKitAcknowledged,
        }) :
        validateVaultUnlock(masterPassphrase);
    if (failure) {
      setErrorMessage(failure.message);
      focusField(failure.field);
      return;
    }

    setBusy(true);
    setErrorMessage(null);
    let result: VaultOperationResult|null = null;
    try {
      const response = needsInitialize ?
          await handler.initializeVault(masterPassphrase, recoverySecret) :
          await handler.unlockVault(masterPassphrase);
      result = response.result;
    } catch {
      result = null;
    } finally {
      // Secrets never survive a submit attempt, successful or not.
      if (mountedRef.current) {
        clearSecrets();
      }
    }
    if (!mountedRef.current) {
      return;
    }
    setBusy(false);
    if (!result || !result.success) {
      setErrorMessage(result?.errorMessage || 'Vault operation failed.');
      focusField('masterPassphrase');
      return;
    }
    await loadPasswords(searchQuery, true);
  };

  return (
    <ManagedSettingRow
      controlClassName={SETTING_CONTROL_STACKED_CLASS}
      description={accessCopy.description}
      title={accessCopy.title}>
      <div
        className={SETTING_CONTROL_STACK_CLASS}
        data-vault-access-mode={accessCopy.mode}>
        <div className={FIELD_GROUP_CLASS}>
          <label className={FIELD_LABEL_CLASS} htmlFor="vault-master-passphrase">
            Master passphrase
          </label>
          <Input
            aria-label="Vault master passphrase"
            autoComplete={needsInitialize ? 'new-password' : 'current-password'}
            id="vault-master-passphrase"
            placeholder="Master passphrase"
            ref={bindSecretInput(masterInputRef)}
            type="password"
            value={masterPassphrase}
            onChange={event => {
              setMasterPassphrase(event.currentTarget.value);
              setErrorMessage(null);
            }}
          />
        </div>
        {needsInitialize ? (
          <>
            <div className={FIELD_GROUP_CLASS}>
              <label className={FIELD_LABEL_CLASS} htmlFor="vault-confirm-passphrase">
                Confirm master passphrase
              </label>
              <Input
                aria-label="Confirm Vault master passphrase"
                autoComplete="new-password"
                id="vault-confirm-passphrase"
                placeholder="Confirm master passphrase"
                ref={bindSecretInput(confirmPassphraseInputRef)}
                type="password"
                value={confirmPassphrase}
                onChange={event => {
                  setConfirmPassphrase(event.currentTarget.value);
                  setErrorMessage(null);
                }}
              />
            </div>
            <div className={FIELD_GROUP_CLASS}>
              <label className={FIELD_LABEL_CLASS} htmlFor="vault-recovery-secret">
                Recovery secret
              </label>
              <Input
                aria-label="Vault recovery secret"
                autoComplete="new-password"
                id="vault-recovery-secret"
                placeholder="Recovery secret"
                ref={bindSecretInput(recoveryInputRef)}
                type="password"
                value={recoverySecret}
                onChange={event => {
                  setRecoverySecret(event.currentTarget.value);
                  setErrorMessage(null);
                }}
              />
            </div>
            <div className={FIELD_GROUP_CLASS}>
              <label className={FIELD_LABEL_CLASS} htmlFor="vault-confirm-recovery-secret">
                Confirm recovery secret
              </label>
              <Input
                aria-label="Confirm Vault recovery secret"
                autoComplete="new-password"
                id="vault-confirm-recovery-secret"
                placeholder="Confirm recovery secret"
                ref={bindSecretInput(confirmRecoveryInputRef)}
                type="password"
                value={confirmRecovery}
                onChange={event => {
                  setConfirmRecovery(event.currentTarget.value);
                  setErrorMessage(null);
                }}
              />
            </div>
            <div className={RECOVERY_KIT_CLASS}>
              <p className={FIELD_HELP_CLASS}>
                Save your recovery kit now: store the master passphrase and recovery
                secret in a safe place. Maho cannot recover this Vault for you, and
                these values are never shown again after setup.
              </p>
              <label className={ACKNOWLEDGE_CLASS} htmlFor="vault-recovery-kit-acknowledged">
                <input
                  checked={recoveryKitAcknowledged}
                  id="vault-recovery-kit-acknowledged"
                  ref={bindSecretInput(acknowledgeInputRef)}
                  type="checkbox"
                  onChange={event => {
                    setRecoveryKitAcknowledged(event.currentTarget.checked);
                    setErrorMessage(null);
                  }}
                />
                <span>I saved my recovery kit outside this browser.</span>
              </label>
            </div>
          </>
        ) : null}
        <p
          aria-live="assertive"
          className={errorMessage ? ERROR_CLASS : SETTING_DESCRIPTION_CLASS}
          data-testid="vault-gate-error"
          role="alert">
          {errorMessage ?? ''}
        </p>
      </div>
      <div className={SETTING_ACTIONS_CLASS}>
        <ActionButton
          label={busy ? 'Working…' : accessCopy.actionLabel}
          onClick={submit}
        />
      </div>
    </ManagedSettingRow>
  );
}

function scrubSecretNodes(nodes: ReadonlySet<HTMLInputElement>): void {
  for (const node of nodes) {
    if (node.type === 'checkbox') {
      node.checked = false;
    } else {
      node.value = '';
    }
  }
}
