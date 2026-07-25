// Copyright 2026 Maho Browser. All rights reserved.

import React, {useState} from 'react';

import {VaultLockState} from '../mojo.js';
import type {VaultOperationResult} from '../mojo.js';
import {
  ActionButton,
  ManagedSettingRow,
  SETTING_ACTIONS_CLASS,
  SETTING_CONTROL_STACK_CLASS,
  SETTING_CONTROL_STACKED_CLASS,
  SETTING_DESCRIPTION_CLASS,
  TextFieldShell,
} from './domain_panes.js';
import type {LoadPasswords} from './passwords_model.js';
import type {MahoSettingsStore} from './store.js';

type VaultAccessGateProps = {
  readonly loadPasswords: LoadPasswords;
  readonly searchQuery: string;
  readonly store: MahoSettingsStore;
  readonly vaultResult: VaultOperationResult;
};

export function VaultAccessGate(
    {loadPasswords, searchQuery, store, vaultResult}: VaultAccessGateProps) {
  const handler = store.getHandler();
  const [masterPassphrase, setMasterPassphrase] = useState('');
  const [recoverySecret, setRecoverySecret] = useState('');
  const [busy, setBusy] = useState(false);
  const [errorMessage, setErrorMessage] = useState<string | null>(null);
  const lockState = vaultResult.status?.lockState ?? VaultLockState.kUninitialized;
  const needsInitialize = lockState === VaultLockState.kUninitialized;
  const title = needsInitialize ? 'Set up Maho Vault' : 'Unlock Maho Vault';
  const description = needsInitialize ?
      'Create a local Vault before storing native passwords. The recovery secret is used only to recover this Vault.' :
      'Unlock your local Vault before viewing or changing saved native passwords.';
  const canSubmit = needsInitialize ?
      masterPassphrase.trim().length > 0 && recoverySecret.trim().length > 0 :
      masterPassphrase.trim().length > 0;

  return (
    <ManagedSettingRow
      controlClassName={SETTING_CONTROL_STACKED_CLASS}
      description={description}
      title={title}>
      <div className={SETTING_CONTROL_STACK_CLASS}>
        <TextFieldShell
          ariaLabel="Vault master passphrase"
          placeholder="Master passphrase"
          type="password"
          value={masterPassphrase}
          onChange={setMasterPassphrase}
        />
        {needsInitialize ? (
          <TextFieldShell
            ariaLabel="Vault recovery secret"
            placeholder="Recovery secret"
            type="password"
            value={recoverySecret}
            onChange={setRecoverySecret}
          />
        ) : null}
        {errorMessage ? <p className={SETTING_DESCRIPTION_CLASS}>{errorMessage}</p> : null}
      </div>
      <div className={SETTING_ACTIONS_CLASS}>
        <ActionButton
          label={busy ? 'Working…' : needsInitialize ? 'Initialize Vault' : 'Unlock Vault'}
          onClick={async () => {
            if (!canSubmit || busy) {
              return;
            }
            setBusy(true);
            setErrorMessage(null);
            const result = needsInitialize ?
                await handler.initializeVault(masterPassphrase, recoverySecret) :
                await handler.unlockVault(masterPassphrase);
            if (!result.result.success) {
              setErrorMessage(result.result.errorMessage || 'Vault operation failed.');
              setBusy(false);
              return;
            }
            setMasterPassphrase('');
            setRecoverySecret('');
            setBusy(false);
            await loadPasswords(searchQuery, true);
          }}
        />
      </div>
    </ManagedSettingRow>
  );
}
