// Copyright 2026 Maho Browser. All rights reserved.

import React from 'react';
import {
  Check,
  ChevronRight,
  ExternalLink,
  LockKeyhole,
  ShieldAlert,
  ShieldCheck,
  ShieldOff,
} from 'lucide-react';

import {Card, CardContent, CardDescription, CardHeader} from '@ui/card';
import {Input} from '@ui/input';
import {Label} from '@ui/label';
import type {
  PasswordProviderKind,
  PasswordProviderOption,
} from '../../../maho_settings/maho_settings.mojom-webui.js';
import type {MahoWelcomeStore} from '../store.js';
import {PASSWORD_PROVIDER_KIND} from '../types.js';

const ASSET_ORIGIN = 'chrome://maho-' + 'welcome';
const PASSWORD_PROVIDER_ICONS = {
  onePassword: `${ASSET_ORIGIN}/icons/password-providers/onepassword.png`,
  bitwarden: `${ASSET_ORIGIN}/icons/password-providers/bitwarden.png`,
} as const;

export type ProviderChoice = PasswordProviderKind | 'none';

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

interface ProviderSelectionGridProps {
  readonly store: MahoWelcomeStore;
  readonly disabled: boolean;
  readonly options: readonly PasswordProviderOption[];
  readonly selectedProvider: ProviderChoice | null;
  readonly onSelect: (provider: ProviderChoice) => void;
}

interface ExternalProviderGuideCardProps {
  readonly store: MahoWelcomeStore;
  readonly provider: PasswordProviderKind;
  readonly option: PasswordProviderOption | null;
}

interface NoPasswordManagerConfirmCardProps {
  readonly store: MahoWelcomeStore;
}

interface VaultCardProps {
  readonly store: MahoWelcomeStore;
  readonly acknowledgeInputRef: React.Ref<HTMLInputElement>;
  readonly confirmPassphrase: string;
  readonly confirmPassphraseInputRef: React.Ref<HTMLInputElement>;
  readonly confirmRecovery: string;
  readonly confirmRecoveryInputRef: React.Ref<HTMLInputElement>;
  readonly disabled: boolean;
  readonly isUnlocked: boolean;
  readonly masterInputRef: React.Ref<HTMLInputElement>;
  readonly masterPassphrase: string;
  readonly needsInitialize: boolean;
  readonly needsUnlock: boolean;
  readonly recoveryInputRef: React.Ref<HTMLInputElement>;
  readonly recoveryKitAcknowledged: boolean;
  readonly recoverySecret: string;
  readonly onConfirmPassphraseChange: (value: string) => void;
  readonly onConfirmRecoveryChange: (value: string) => void;
  readonly onMasterPassphraseChange: (value: string) => void;
  readonly onRecoveryKitAcknowledgedChange: (value: boolean) => void;
  readonly onRecoverySecretChange: (value: string) => void;
}

export function ProviderSelectionGrid({
  store,
  disabled,
  options,
  selectedProvider,
  onSelect,
}: ProviderSelectionGridProps) {
  const nativeOption = options.find(
    o => o.provider === PASSWORD_PROVIDER_KIND.MahoNative,
  );
  const bitwardenOption = options.find(
    o => o.provider === PASSWORD_PROVIDER_KIND.Bitwarden,
  );
  const onePasswordOption = options.find(
    o => o.provider === PASSWORD_PROVIDER_KIND.OnePassword,
  );

  const choices: Array<{
    id: ProviderChoice;
    title: string;
    description: string;
    icon: React.ReactNode;
    badge?: string;
  }> = [
    {
      id: PASSWORD_PROVIDER_KIND.MahoNative,
      title: nativeOption?.displayName || 'Maho Passwords',
      description:
        'Built-in encrypted Local Vault stored locally on this device.',
      icon: <ShieldCheck className="size-5 text-primary" aria-hidden="true" />,
      badge: 'Recommended',
    },
    {
      id: PASSWORD_PROVIDER_KIND.OnePassword,
      title: onePasswordOption?.displayName || '1Password',
      description:
        'Use 1Password browser extension to manage passwords.',
      icon: <img src={PASSWORD_PROVIDER_ICONS.onePassword} alt="" width={20} height={20} className="size-5 rounded-md object-contain" />,
    },
    {
      id: PASSWORD_PROVIDER_KIND.Bitwarden,
      title: bitwardenOption?.displayName || 'Bitwarden',
      description:
        'Use Bitwarden open-source extension for password sync.',
      icon: <img src={PASSWORD_PROVIDER_ICONS.bitwarden} alt="" width={20} height={20} className="size-5 rounded-md object-contain" />,
    },
    {
      id: 'none',
      title: 'No password manager',
      description:
        'Skip setting up password management for now. You can enable it anytime in Settings.',
      icon: <ShieldOff className="size-5 text-muted-foreground" aria-hidden="true" />,
    },
  ];

  return (
    <div className="space-y-3" role="radiogroup" aria-label="Select password manager provider">
      {choices.map(choice => {
        const isSelected = selectedProvider === choice.id;
        return (
          <Card
            key={String(choice.id)}
            className={`cursor-pointer transition-all duration-200 hover:border-primary/50 bg-background/60 shadow-sm ${
              isSelected
                ? 'border-primary bg-primary/5 ring-1 ring-primary'
                : 'border-border'
            } ${disabled ? 'pointer-events-none opacity-60' : ''}`}
            onClick={() => {
              if (!disabled) {
                onSelect(choice.id);
              }
            }}
            role="radio"
            aria-checked={isSelected}
            tabIndex={disabled ? -1 : 0}
            onKeyDown={e => {
              if ((e.key === ' ' || e.key === 'Enter') && !disabled) {
                e.preventDefault();
                onSelect(choice.id);
              }
            }}
          >
            <CardContent className="flex items-center justify-between p-4">
              <div className="flex items-start gap-3.5 pr-2">
                <div className="mt-0.5 shrink-0">{choice.icon}</div>
                <div className="space-y-1">
                  <div className="flex items-center gap-2">
                    <span className="text-sm font-semibold leading-none">
                      {choice.title}
                    </span>
                    {choice.badge ? (
                      <span className="rounded-full bg-primary/10 px-2 py-0.5 text-[10px] font-medium text-primary">
                        {choice.badge}
                      </span>
                    ) : null}
                  </div>
                  <p className="text-xs text-muted-foreground leading-normal">
                    {choice.description}
                  </p>
                </div>
              </div>
              <div
                className={`flex size-5 shrink-0 items-center justify-center rounded-full border ${
                  isSelected
                    ? 'border-primary bg-primary text-primary-foreground'
                    : 'border-muted-foreground/30'
                }`}
              >
                {isSelected ? <Check className="size-3.5 stroke-[3]" /> : null}
              </div>
            </CardContent>
          </Card>
        );
      })}
    </div>
  );
}

export function ExternalProviderGuideCard({
  provider,
  option,
}: ExternalProviderGuideCardProps) {
  const is1Password = provider === PASSWORD_PROVIDER_KIND.OnePassword;
  const name = is1Password ? '1Password' : 'Bitwarden';

  return (
    <div className="rounded-xl border border-border/50 bg-background/40 p-5 backdrop-blur-md space-y-4">
      <div className="flex items-center gap-2.5">
        <img src={is1Password ? PASSWORD_PROVIDER_ICONS.onePassword : PASSWORD_PROVIDER_ICONS.bitwarden} alt="" width={20} height={20} className="size-5 rounded object-contain shadow-xs" />
        <div>
          <h2 className="text-sm font-semibold leading-none tracking-tight text-foreground">
            Using {name} with Maho
          </h2>
          <p className="text-xs text-muted-foreground mt-1">
            Extension setup is completed in the browser after onboarding.
          </p>
        </div>
      </div>
      <div className="rounded-lg border border-border/40 bg-muted/20 p-3.5 text-xs text-muted-foreground leading-relaxed">
        You can install and configure the {name} extension from Chrome Web Store or Extension Settings at any time. Maho will seamlessly integrate with your extension once installed.
      </div>
      {option && !option.isAvailable ? (
        <div className="flex items-center gap-2 text-xs text-amber-500/90 bg-amber-500/10 p-2.5 rounded-md border border-amber-500/20">
          <ShieldAlert className="size-4 shrink-0" />
          <span>Extension not currently detected. You can proceed and configure it later.</span>
        </div>
      ) : null}
    </div>
  );
}

export function NoPasswordManagerConfirmCard() {
  return (
    <div className="rounded-xl border border-border/50 bg-background/40 p-5 backdrop-blur-md space-y-3">
      <div className="flex items-center gap-2.5">
        <div className="flex size-8 items-center justify-center rounded-lg border border-border/40 bg-muted/30 text-muted-foreground">
          <ShieldOff className="size-4" aria-hidden="true" />
        </div>
        <div>
          <h2 className="text-sm font-semibold leading-none tracking-tight text-foreground">
            No Password Manager Selected
          </h2>
          <p className="text-xs text-muted-foreground mt-1">
            Local Vault initialization will be skipped.
          </p>
        </div>
      </div>
      <p className="text-xs text-muted-foreground leading-relaxed pl-0.5">
        Maho will not create a Local Vault or prompt for a master passphrase during onboarding. You can set up Maho Passwords or connect external extensions anytime in Settings.
      </p>
    </div>
  );
}

export function VaultCard(props: VaultCardProps) {
  const {
    acknowledgeInputRef,
    confirmPassphrase,
    confirmPassphraseInputRef,
    confirmRecovery,
    confirmRecoveryInputRef,
    disabled,
    isUnlocked,
    masterInputRef,
    masterPassphrase,
    needsInitialize,
    needsUnlock,
    onConfirmPassphraseChange,
    onConfirmRecoveryChange,
    onMasterPassphraseChange,
    onRecoveryKitAcknowledgedChange,
    onRecoverySecretChange,
    recoveryInputRef,
    recoveryKitAcknowledged,
    recoverySecret,
    store,
  } = props;

  return (
    <div className="rounded-xl border border-border/50 bg-background/40 p-5 backdrop-blur-md space-y-4">
      <div className="flex items-center gap-2.5">
        <div className="flex size-8 items-center justify-center rounded-lg border border-border/40 bg-primary/10 text-primary">
          <LockKeyhole className="size-4" aria-hidden="true" />
        </div>
        <div>
          <h2 className="text-sm font-semibold leading-none tracking-tight text-foreground">
            Maho Local Vault
          </h2>
          <p className="text-xs text-muted-foreground mt-1">
            {needsInitialize
              ? 'Set up your local encrypted vault to store passwords securely on this device.'
              : needsUnlock
              ? 'Enter your master passphrase to unlock your local vault.'
              : 'Your local vault is unlocked and ready.'}
          </p>
        </div>
      </div>

      <div className="rounded-lg border border-border/40 bg-muted/20 p-3 text-xs text-muted-foreground leading-relaxed">
        A separate master passphrase protects and unlocks your local Vault encryption key. It is not your Maho account password: signing in to Maho does not unlock this Vault. Your master passphrase stays on this device.
      </div>

      {!isUnlocked ? (
        <div className="space-y-3">
          <div className="space-y-1.5">
            <div className="flex items-center justify-between">
              <Label htmlFor="vault-master-passphrase" className="text-xs font-medium">
                Master Passphrase
              </Label>
              {needsInitialize && masterPassphrase ? (
                <span className="text-[10px] text-emerald-500 font-medium">
                  {masterPassphrase.length >= 8 ? 'Strong passphrase' : 'Entered'}
                </span>
              ) : null}
            </div>
            <Input
              ref={masterInputRef}
              id="vault-master-passphrase"
              type="password"
              autoComplete={needsInitialize ? 'new-password' : 'current-password'}
              disabled={disabled}
              required
              value={masterPassphrase}
              onChange={event => onMasterPassphraseChange(event.target.value)}
              className="h-9 text-xs bg-background/60"
            />
          </div>
          {needsInitialize ? (
            <>
              <div className="space-y-1.5">
                <Label htmlFor="vault-confirm-passphrase" className="text-xs font-medium">
                  Confirm Master Passphrase
                </Label>
                <Input
                  ref={confirmPassphraseInputRef}
                  id="vault-confirm-passphrase"
                  type="password"
                  autoComplete="new-password"
                  disabled={disabled}
                  required
                  value={confirmPassphrase}
                  onChange={event => onConfirmPassphraseChange(event.target.value)}
                  className="h-9 text-xs bg-background/60"
                />
              </div>
              <div className="space-y-1.5">
                <div className="flex items-center justify-between">
                  <Label htmlFor="vault-recovery-secret" className="text-xs font-medium">
                    Recovery Secret
                  </Label>
                  {recoverySecret ? (
                    <span className="text-[10px] text-emerald-500 font-medium">Entered</span>
                  ) : null}
                </div>
                <Input
                  ref={recoveryInputRef}
                  id="vault-recovery-secret"
                  type="password"
                  autoComplete="new-password"
                  disabled={disabled}
                  required
                  value={recoverySecret}
                  onChange={event => onRecoverySecretChange(event.target.value)}
                  className="h-9 text-xs bg-background/60"
                />
              </div>
              <div className="space-y-1.5">
                <Label htmlFor="vault-confirm-recovery-secret" className="text-xs font-medium">
                  Confirm Recovery Secret
                </Label>
                <Input
                  ref={confirmRecoveryInputRef}
                  id="vault-confirm-recovery-secret"
                  type="password"
                  autoComplete="new-password"
                  disabled={disabled}
                  required
                  value={confirmRecovery}
                  onChange={event => onConfirmRecoveryChange(event.target.value)}
                  className="h-9 text-xs bg-background/60"
                />
              </div>
              <div className="space-y-2 rounded-lg border border-border/40 bg-muted/20 p-3">
                <p className="text-xs text-muted-foreground leading-relaxed">
                  Keep your master passphrase and Vault recovery secret somewhere safe outside this browser. The recovery secret restores Vault access if you forget your passphrase. It is separate from the sync recovery phrase used on other devices. Maho cannot recover this Vault for you, and neither value is shown again after setup.
                </p>
                <label
                  htmlFor="vault-recovery-kit-acknowledged"
                  className="flex items-start gap-2 text-xs text-foreground leading-relaxed">
                  <input
                    ref={acknowledgeInputRef}
                    id="vault-recovery-kit-acknowledged"
                    type="checkbox"
                    disabled={disabled}
                    checked={recoveryKitAcknowledged}
                    onChange={event => onRecoveryKitAcknowledgedChange(event.target.checked)}
                    className="mt-0.5"
                  />
                  <span>I saved my recovery kit outside this browser.</span>
                </label>
              </div>
            </>
          ) : null}
        </div>
      ) : (
        <div className="flex items-center gap-2 rounded-lg bg-emerald-500/10 p-3 text-xs font-medium text-emerald-600 dark:text-emerald-400 border border-emerald-500/20">
          <Check className="size-4 shrink-0" />
          <span>Vault is unlocked and ready for use.</span>
        </div>
      )}
    </div>
  );
}
