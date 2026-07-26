// Copyright 2026 Maho Browser. All rights reserved.

import React from 'react';
import {KeyRound, LockKeyhole} from 'lucide-react';

import {Card, CardContent, CardDescription, CardHeader} from '@ui/card';
import {Input} from '@ui/input';
import {Label} from '@ui/label';
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from '@ui/select';
import type {
  PasswordProviderKind,
  PasswordProviderOption,
} from '../../../maho_settings/maho_settings.mojom-webui.js';
import type {MahoWelcomeStore} from '../store.js';
import {getProviderHelperText} from '../types.js';

const PROVIDER_DESCRIPTION_ID = 'password-provider-description';

interface PasswordProviderCardProps {
  readonly store: MahoWelcomeStore;
  readonly disabled: boolean;
  readonly options: readonly PasswordProviderOption[];
  readonly selectedProvider: PasswordProviderKind | null;
  readonly onSelect: (provider: PasswordProviderKind) => void;
}

interface VaultCardProps {
  readonly store: MahoWelcomeStore;
  readonly disabled: boolean;
  readonly isUnlocked: boolean;
  readonly masterInputRef: React.RefObject<HTMLInputElement | null>;
  readonly masterPassphrase: string;
  readonly needsInitialize: boolean;
  readonly needsUnlock: boolean;
  readonly recoveryInputRef: React.RefObject<HTMLInputElement | null>;
  readonly recoverySecret: string;
  readonly onMasterPassphraseChange: (value: string) => void;
  readonly onRecoverySecretChange: (value: string) => void;
}

export function PasswordProviderCard(
    {store, disabled, onSelect, options, selectedProvider}: PasswordProviderCardProps) {
  const selectedOption = options.find(
      option => option.provider === selectedProvider) ?? null;

  return (
    <Card className="bg-background/60 shadow-sm">
      <CardHeader className="space-y-1 p-4 pb-3">
        <div className="flex items-center gap-2">
          <KeyRound className="size-4 text-muted-foreground" aria-hidden="true" />
          <h2 className="text-sm font-semibold leading-none tracking-tight">
            {store.getString('IDS_MAHO_WELCOME_PASSWORD_PROVIDER_TITLE')}
          </h2>
        </div>
        <CardDescription className="text-pretty text-xs leading-5">
          {store.getString('IDS_MAHO_WELCOME_PASSWORD_PROVIDER_BODY')}
        </CardDescription>
      </CardHeader>
      <CardContent className="space-y-2 p-4 pt-0">
        <Label htmlFor="password-provider">
          {store.getString('IDS_MAHO_WELCOME_PASSWORD_PROVIDER_LABEL')}
        </Label>
        <Select
          value={selectedProvider === null ? undefined : String(selectedProvider)}
          disabled={disabled}
          onValueChange={value => {
            const option = options.find(candidate => String(candidate.provider) === value);
            if (option) {
              onSelect(option.provider);
            }
          }}
        >
          <SelectTrigger
            id="password-provider"
            className="h-10 bg-background"
            aria-label={store.getString('IDS_MAHO_WELCOME_PASSWORD_PROVIDER_ARIA')}
            aria-describedby={selectedOption ? PROVIDER_DESCRIPTION_ID : undefined}
          >
            <SelectValue
              placeholder={store.getString(
                  'IDS_MAHO_WELCOME_PASSWORD_PROVIDER_PLACEHOLDER')}
            />
          </SelectTrigger>
          <SelectContent>
            {options.map(option => (
              <SelectItem key={option.provider} value={String(option.provider)}>
                {option.displayName}
              </SelectItem>
            ))}
          </SelectContent>
        </Select>
        {selectedOption ? (
          <p
            id={PROVIDER_DESCRIPTION_ID}
            className="m-0 text-pretty text-xs leading-5 text-muted-foreground"
            aria-live="polite"
          >
            {getProviderHelperText(
                selectedOption,
                store.getString(
                    'IDS_MAHO_WELCOME_PASSWORD_PROVIDER_UNAVAILABLE_HELPER'))}
          </p>
        ) : null}
      </CardContent>
    </Card>
  );
}

export function VaultCard(props: VaultCardProps) {
  const {
    disabled,
    isUnlocked,
    masterInputRef,
    masterPassphrase,
    needsInitialize,
    needsUnlock,
    onMasterPassphraseChange,
    onRecoverySecretChange,
    recoveryInputRef,
    recoverySecret,
    store,
  } = props;

  return (
    <Card className="bg-background/60 shadow-sm">
      <CardHeader className="space-y-1 p-4 pb-3">
        <div className="flex items-center gap-2">
          <LockKeyhole className="size-4 text-muted-foreground" aria-hidden="true" />
          <h2 className="text-sm font-semibold leading-none tracking-tight">
            {store.getString('IDS_MAHO_WELCOME_VAULT_CARD_TITLE')}
          </h2>
        </div>
        <CardDescription className="text-pretty text-xs leading-5">
          {needsInitialize
            ? store.getString('IDS_MAHO_WELCOME_VAULT_INITIALIZE_BODY')
            : needsUnlock
            ? store.getString('IDS_MAHO_WELCOME_VAULT_UNLOCK_BODY')
            : store.getString('IDS_MAHO_WELCOME_VAULT_READY_BODY')}
        </CardDescription>
      </CardHeader>
      <CardContent className="space-y-3 p-4 pt-0">
        {!isUnlocked ? (
          <div className="space-y-3">
            <div className="space-y-2">
              <Label htmlFor="vault-master-passphrase">
                {store.getString('IDS_MAHO_WELCOME_VAULT_MASTER_LABEL')}
              </Label>
              <Input
                ref={masterInputRef}
                id="vault-master-passphrase"
                type="password"
                autoComplete={needsInitialize ? 'new-password' : 'current-password'}
                disabled={disabled}
                required
                value={masterPassphrase}
                onChange={event => onMasterPassphraseChange(event.target.value)}
              />
            </div>
            {needsInitialize ? (
              <div className="space-y-2">
                <Label htmlFor="vault-recovery-secret">
                  {store.getString('IDS_MAHO_WELCOME_VAULT_RECOVERY_LABEL')}
                </Label>
                <Input
                  ref={recoveryInputRef}
                  id="vault-recovery-secret"
                  type="password"
                  autoComplete="new-password"
                  disabled={disabled}
                  required
                  value={recoverySecret}
                  onChange={event => onRecoverySecretChange(event.target.value)}
                />
              </div>
            ) : null}
          </div>
        ) : null}
      </CardContent>
    </Card>
  );
}
