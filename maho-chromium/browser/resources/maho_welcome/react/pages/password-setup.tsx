// Copyright 2026 Maho Browser. All rights reserved.

import React, {useCallback, useEffect, useMemo, useRef, useState} from 'react';
import {ArrowLeft, ShieldCheck} from 'lucide-react';

import {AlertDescription, AlertTitle} from '@ui/alert';
import {Button} from '@ui/button';
import type {PasswordProviderKind} from '../../../maho_settings/maho_settings.mojom-webui.js';
import type {MahoWelcomeStore} from '../store.js';
import {orderProviderOptions, PASSWORD_PROVIDER_KIND, VAULT_LOCK_STATE} from '../types.js';
import type {WelcomeState} from '../types.js';
import {
  ExternalProviderGuideCard,
  NoPasswordManagerConfirmCard,
  ProviderChoice,
  ProviderSelectionGrid,
  VaultCard,
} from './password-setup-fields.js';

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
        Password Management
      </h1>
      <p className="text-pretty">
        Choose how you want Maho to store and manage your passwords.
      </p>
    </>
  );
}

export function PasswordSetupContent({snapshot, store}: PasswordSetupProps) {
  const {passwordSetup} = snapshot;
  const providerOptions = useMemo(
      () => orderProviderOptions(passwordSetup.providerOptions),
      [passwordSetup.providerOptions]);

  const [step, setStep] = useState<'select' | 'configure'>('select');
  const [selectedChoice, setSelectedChoice] = useState<ProviderChoice | null>(
      passwordSetup.passwordManagerSkipped
        ? 'none'
        : passwordSetup.providerStatus?.provider ?? PASSWORD_PROVIDER_KIND.MahoNative
  );

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
    if (passwordSetup.passwordManagerSkipped) {
      setSelectedChoice('none');
    } else if (passwordSetup.providerStatus) {
      setSelectedChoice(passwordSetup.providerStatus.provider);
    }
  }, [passwordSetup.providerStatus, passwordSetup.passwordManagerSkipped]);

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

  const isNativeVault = selectedChoice === PASSWORD_PROVIDER_KIND.MahoNative;
  const isNoManager = selectedChoice === 'none';

  const canAdvanceStepA = selectedChoice !== null && !isBusy;

  const canSubmitStepB = !isBusy && (
    isNoManager ||
    !isNativeVault ||
    isUnlocked ||
    (needsInitialize && masterPassphrase.trim().length > 0 &&
      recoverySecret.trim().length > 0) ||
    (needsUnlock && masterPassphrase.trim().length > 0)
  );

  const errorMessage = localError || passwordSetup.errorMessage;

  async function handleSubmit(event: React.FormEvent<HTMLFormElement>): Promise<void> {
    event.preventDefault();

    if (step === 'select') {
      if (!canAdvanceStepA) return;
      setLocalError('');
      if (selectedChoice === 'none') {
        store.skipPasswordSetup();
        return;
      }
      setStep('configure');
      return;
    }

    if (selectedChoice === null || isBusy) {
      return;
    }

    setLocalError('');

    if (isNoManager) {
      store.skipPasswordSetup();
      return;
    }

    const providerKind = selectedChoice as PasswordProviderKind;

    if (isNativeVault) {
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

      try {
        let vaultReady = isUnlocked;
        if (needsInitialize) {
          vaultReady = await store.initializeVault(masterPassphrase, recoverySecret);
        } else if (needsUnlock) {
          vaultReady = await store.unlockVault(masterPassphrase);
        }
        if (vaultReady) {
          await store.advanceFromPasswordSetup(providerKind);
        }
      } finally {
        clearSecrets();
      }
    } else {
      // 1Password or Bitwarden
      await store.advanceFromPasswordSetup(providerKind);
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
        {step === 'configure' ? (
          <button
            type="button"
            className="inline-flex items-center gap-1.5 text-xs text-muted-foreground hover:text-foreground transition-colors mb-1"
            onClick={() => {
              setStep('select');
              setLocalError('');
            }}
          >
            <ArrowLeft className="size-3.5" />
            <span>Change provider choice</span>
          </button>
        ) : null}

        {step === 'select' ? (
          <ProviderSelectionGrid
            store={store}
            disabled={isBusy}
            options={providerOptions}
            selectedProvider={selectedChoice}
            onSelect={choice => {
              setSelectedChoice(choice);
              setLocalError('');
            }}
          />
        ) : (
          <>
            {isNativeVault ? (
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
            ) : isNoManager ? (
              <NoPasswordManagerConfirmCard />
            ) : (
              <ExternalProviderGuideCard
                store={store}
                provider={selectedChoice as PasswordProviderKind}
                option={providerOptions.find(o => o.provider === selectedChoice) ?? null}
              />
            )}
          </>
        )}

        {errorMessage ? (
          <div className="rounded-lg border border-destructive/50 bg-destructive/10 p-3 text-xs text-destructive shrink-0" aria-live="assertive">
            <AlertTitle className="font-semibold text-sm mb-1">Password setup notice</AlertTitle>
            <AlertDescription className="text-pretty">{errorMessage}</AlertDescription>
          </div>
        ) : null}
      </div>

      <div className="shrink-0 border-t border-border pt-4">
        {step === 'select' ? (
          <Button
            type="submit"
            className="h-10 w-full rounded-lg font-semibold shadow-lg shadow-primary/10"
            disabled={!canAdvanceStepA}
          >
            Continue
          </Button>
        ) : (
          <Button
            type="submit"
            className="h-10 w-full rounded-lg font-semibold shadow-lg shadow-primary/10"
            aria-busy={isBusy}
            disabled={!canSubmitStepB}
          >
            {isBusy
              ? 'Working…'
              : isNoManager
              ? 'Skip password manager and continue'
              : isNativeVault
              ? needsInitialize
                ? 'Initialize Vault and continue'
                : needsUnlock
                ? 'Unlock Vault and continue'
                : 'Continue with Maho Local Vault'
              : 'Continue'}
          </Button>
        )}
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
