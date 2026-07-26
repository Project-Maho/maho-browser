// Copyright 2026 Maho Browser. All rights reserved.

import React, {useCallback, useEffect, useMemo, useRef, useState} from 'react';
import {ShieldCheck} from 'lucide-react';

import {Alert, AlertDescription, AlertTitle} from '@ui/alert';
import {Button} from '@ui/button';
import type {PasswordProviderKind} from '../../../maho_settings/maho_settings.mojom-webui.js';
import type {MahoWelcomeStore} from '../store.js';
import {orderProviderOptions, VAULT_LOCK_STATE} from '../types.js';
import type {WelcomeState} from '../types.js';
import {PasswordProviderCard, VaultCard} from './password-setup-fields.js';

interface PasswordSetupProps {
  readonly snapshot: WelcomeState;
  readonly store: MahoWelcomeStore;
}

interface SecretRefs {
  readonly masterPassphrase: React.MutableRefObject<string>;
  readonly recoverySecret: React.MutableRefObject<string>;
  readonly masterInput: React.RefObject<HTMLInputElement | null>;
  readonly recoveryInput: React.RefObject<HTMLInputElement | null>;
}

export function PasswordSetupSidebar({store}: {store: MahoWelcomeStore}) {
  return (
    <>
      <div className="flex size-11 items-center justify-center rounded-xl border border-border bg-background/70 text-foreground shadow-sm">
        <ShieldCheck className="size-5" aria-hidden="true" />
      </div>
      <h1 className="text-balance">
        {store.getString('IDS_MAHO_WELCOME_PASSWORD_SETUP_TITLE')}
      </h1>
      <p className="text-pretty">
        {store.getString('IDS_MAHO_WELCOME_PASSWORD_SETUP_BODY')}
      </p>
    </>
  );
}

export function PasswordSetupContent({snapshot, store}: PasswordSetupProps) {
  const {passwordSetup} = snapshot;
  const providerOptions = useMemo(
      () => orderProviderOptions(passwordSetup.providerOptions),
      [passwordSetup.providerOptions]);
  const [selectedProvider, setSelectedProvider] = useState<PasswordProviderKind | null>(
      passwordSetup.providerStatus?.provider ?? null);
  const [masterPassphrase, setMasterPassphrase] = useState('');
  const [recoverySecret, setRecoverySecret] = useState('');
  const [localError, setLocalError] = useState('');
  const masterPassphraseRef = useRef('');
  const recoverySecretRef = useRef('');
  const masterInputRef = useRef<HTMLInputElement>(null);
  const recoveryInputRef = useRef<HTMLInputElement>(null);

  useEffect(() => {
    void store.refreshPasswordSetup();
  }, [store]);

  useEffect(() => {
    if (passwordSetup.providerStatus) {
      setSelectedProvider(passwordSetup.providerStatus.provider);
    }
  }, [passwordSetup.providerStatus]);

  useEffect(() => () => {
    scrubSecretRefs({
      masterPassphrase: masterPassphraseRef,
      recoverySecret: recoverySecretRef,
      masterInput: masterInputRef,
      recoveryInput: recoveryInputRef,
    });
  }, []);

  const clearSecrets = useCallback(() => {
    scrubSecretRefs({
      masterPassphrase: masterPassphraseRef,
      recoverySecret: recoverySecretRef,
      masterInput: masterInputRef,
      recoveryInput: recoveryInputRef,
    });
    setMasterPassphrase('');
    setRecoverySecret('');
  }, []);

  const lockState = passwordSetup.vaultStatus?.lockState ??
      VAULT_LOCK_STATE.Uninitialized;
  const needsInitialize = lockState === VAULT_LOCK_STATE.Uninitialized;
  const needsUnlock = lockState === VAULT_LOCK_STATE.Locked ||
      lockState === VAULT_LOCK_STATE.AutoLocked;
  const isUnlocked = lockState === VAULT_LOCK_STATE.Unlocked;
  const isBusy = passwordSetup.status === 'loading' ||
      passwordSetup.status === 'submitting';
  const canSubmit = selectedProvider !== null && !isBusy && (
    isUnlocked ||
    (needsInitialize && masterPassphrase.trim().length > 0 &&
      recoverySecret.trim().length > 0) ||
    (needsUnlock && masterPassphrase.trim().length > 0)
  );
  const errorMessage = localError || passwordSetup.errorMessage;

  async function handleSubmit(event: React.FormEvent<HTMLFormElement>): Promise<void> {
    event.preventDefault();
    if (selectedProvider === null || isBusy) {
      return;
    }
    if (needsInitialize &&
        (masterPassphrase.trim().length === 0 ||
          recoverySecret.trim().length === 0)) {
      setLocalError('Master passphrase and recovery secret are required.');
      return;
    }
    if (needsUnlock && masterPassphrase.trim().length === 0) {
      setLocalError('Master passphrase is required.');
      return;
    }

    setLocalError('');
    try {
      let vaultReady = isUnlocked;
      if (needsInitialize) {
        vaultReady = await store.initializeVault(masterPassphrase, recoverySecret);
      } else if (needsUnlock) {
        vaultReady = await store.unlockVault(masterPassphrase);
      }
      if (vaultReady) {
        await store.advanceFromPasswordSetup(selectedProvider);
      }
    } finally {
      clearSecrets();
    }
  }

  if (passwordSetup.status === 'loading' && providerOptions.length === 0) {
    return (
      <div className="flex h-full w-full items-center justify-center rounded-xl border border-border bg-background/50 p-8 text-center">
        <p className="m-0 text-sm text-muted-foreground" aria-live="polite">
          Loading password setup…
        </p>
      </div>
    );
  }

  return (
    <form
      className="flex h-full min-h-0 w-full max-w-lg flex-col"
      onSubmit={event => { void handleSubmit(event); }}
    >
      <div className="min-h-0 flex-1 space-y-4 overflow-y-auto pr-1 pb-4">
        <PasswordProviderCard
          store={store}
          disabled={isBusy}
          options={providerOptions}
          selectedProvider={selectedProvider}
          onSelect={provider => {
            setSelectedProvider(provider);
            setLocalError('');
          }}
        />

        <VaultCard
          store={store}
          disabled={isBusy}
          isUnlocked={isUnlocked}
          masterInputRef={masterInputRef}
          masterPassphrase={masterPassphrase}
          needsInitialize={needsInitialize}
          needsUnlock={needsUnlock}
          recoveryInputRef={recoveryInputRef}
          recoverySecret={recoverySecret}
          onMasterPassphraseChange={value => {
            masterPassphraseRef.current = value;
            setMasterPassphrase(value);
            setLocalError('');
          }}
          onRecoverySecretChange={value => {
            recoverySecretRef.current = value;
            setRecoverySecret(value);
            setLocalError('');
          }}
        />

        {errorMessage ? (
          <Alert variant="destructive" aria-live="assertive">
            <AlertTitle>Password setup failed</AlertTitle>
            <AlertDescription>{errorMessage}</AlertDescription>
            {providerOptions.length === 0 ? (
              <Button
                type="button"
                variant="outline"
                size="sm"
                className="mt-3"
                disabled={isBusy}
                onClick={() => { void store.refreshPasswordSetup(); }}
              >
                Retry
              </Button>
            ) : null}
          </Alert>
        ) : null}
      </div>

      <div className="shrink-0 border-t border-border pt-4">
        <Button
          type="submit"
          className="h-10 w-full rounded-lg font-semibold shadow-lg shadow-primary/10"
          aria-busy={isBusy}
          disabled={!canSubmit}
        >
          {isBusy
            ? 'Working…'
            : needsInitialize
            ? 'Initialize Vault and continue'
            : needsUnlock
            ? 'Unlock Vault and continue'
            : 'Continue'}
        </Button>
      </div>
    </form>
  );
}

function scrubSecretRefs({
  masterPassphrase,
  recoverySecret,
  masterInput,
  recoveryInput,
}: SecretRefs): void {
  masterPassphrase.current = '';
  recoverySecret.current = '';
  if (masterInput.current) {
    masterInput.current.value = '';
  }
  if (recoveryInput.current) {
    recoveryInput.current.value = '';
  }
}
