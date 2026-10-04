// Copyright 2026 Maho Browser. All rights reserved.

import {PasswordProviderKind} from '../mojo.js';
import type {
  PasswordProviderOption,
  PasswordProviderStatus,
  SavedPassword,
  SettingValue,
  VaultOperationResult,
  VaultPolicyStatus,
} from '../mojo.js';

export type PasswordsPaneData = {
  readonly passwords: readonly SavedPassword[];
  readonly providerStatus: PasswordProviderStatus;
  readonly providerOptions: readonly PasswordProviderOption[];
  readonly vaultStatus: VaultOperationResult;
};

export type PasswordSettingsPaneData = {
  readonly providerStatus: PasswordProviderStatus;
  readonly providerOptions: readonly PasswordProviderOption[];
  readonly policyStatus: VaultPolicyStatus;
};

export type PasswordsLoadState<T> =
    | {readonly status: 'loading'}
    | {readonly status: 'error'; readonly message: string}
    | {readonly data: T; readonly status: 'loaded'};

export type LoadPasswords = (searchQuery: string, showLoading?: boolean) => Promise<void>;

export type PasswordProviderBackendValue =
    'maho_native' | 'bitwarden' | 'onepassword' | 'disabled';

export type PasswordImportFileFormat = 'csv' | 'one_pux' | 'json';

export type PasswordImportSourceId =
    | 'onepassword'
    | 'bitwarden'
    | 'apple_passwords'
    | 'keepassxc'
    | 'keepass_classic';

export type PasswordImportFormatId =
    | 'onepassword_csv'
    | 'onepassword_1pux'
    | 'bitwarden_individual_csv'
    | 'bitwarden_organization_csv'
    | 'bitwarden_json'
    | 'apple_passwords_csv'
    | 'keepassxc_csv'
    | 'keepass_classic_csv';

export type PasswordImportFormatDescriptor = {
  readonly formatId: PasswordImportFormatId;
  readonly label: string;
  readonly fileFormat: PasswordImportFileFormat;
  readonly fileExtensions: readonly string[];
};

export type PasswordImportSourceDescriptor = {
  readonly sourceId: PasswordImportSourceId;
  readonly displayName: string;
  readonly description: string;
  readonly fileImport: true;
  readonly platformIndependent: true;
  readonly formats: readonly PasswordImportFormatDescriptor[];
};

const PASSWORD_PROVIDER_BACKEND_VALUES = {
  [PasswordProviderKind.kMahoNative]: 'maho_native',
  [PasswordProviderKind.kBitwarden]: 'bitwarden',
  [PasswordProviderKind.kOnePassword]: 'onepassword',
  [PasswordProviderKind.kDisabled]: 'disabled',
} as const satisfies Record<PasswordProviderKind, PasswordProviderBackendValue>;

export const PASSWORD_IMPORT_SOURCE_DESCRIPTORS = [
  {
    sourceId: 'onepassword',
    displayName: '1Password',
    description: 'Import a user-exported 1Password CSV or 1PUX file.',
    fileImport: true,
    platformIndependent: true,
    formats: [
      {
        formatId: 'onepassword_csv',
        label: '1Password CSV',
        fileFormat: 'csv',
        fileExtensions: ['.csv'],
      },
      {
        formatId: 'onepassword_1pux',
        label: '1PUX',
        fileFormat: 'one_pux',
        fileExtensions: ['.1pux'],
      },
    ],
  },
  {
    sourceId: 'bitwarden',
    displayName: 'Bitwarden',
    description: 'Import a user-exported Bitwarden CSV or JSON file.',
    fileImport: true,
    platformIndependent: true,
    formats: [
      {
        formatId: 'bitwarden_individual_csv',
        label: 'Bitwarden individual CSV',
        fileFormat: 'csv',
        fileExtensions: ['.csv'],
      },
      {
        formatId: 'bitwarden_organization_csv',
        label: 'Bitwarden organization CSV',
        fileFormat: 'csv',
        fileExtensions: ['.csv'],
      },
      {
        formatId: 'bitwarden_json',
        label: 'Bitwarden JSON',
        fileFormat: 'json',
        fileExtensions: ['.json'],
      },
    ],
  },
  {
    sourceId: 'apple_passwords',
    displayName: 'Apple Passwords',
    description: 'Import a user-exported Apple Passwords CSV file.',
    fileImport: true,
    platformIndependent: true,
    formats: [
      {
        formatId: 'apple_passwords_csv',
        label: 'Apple Passwords CSV',
        fileFormat: 'csv',
        fileExtensions: ['.csv'],
      },
    ],
  },
  {
    sourceId: 'keepassxc',
    displayName: 'KeePassXC',
    description: 'Import a user-exported KeePassXC CSV file.',
    fileImport: true,
    platformIndependent: true,
    formats: [
      {
        formatId: 'keepassxc_csv',
        label: 'KeePassXC CSV',
        fileFormat: 'csv',
        fileExtensions: ['.csv'],
      },
    ],
  },
  {
    sourceId: 'keepass_classic',
    displayName: 'KeePass classic',
    description: 'Import a user-exported KeePass classic CSV file.',
    fileImport: true,
    platformIndependent: true,
    formats: [
      {
        formatId: 'keepass_classic_csv',
        label: 'KeePass classic CSV',
        fileFormat: 'csv',
        fileExtensions: ['.csv'],
      },
    ],
  },
] as const satisfies readonly PasswordImportSourceDescriptor[];

export function createPasswordLoadingState<T>(): PasswordsLoadState<T> {
  return {status: 'loading'};
}

export function createPasswordErrorState<T>(message: string): PasswordsLoadState<T> {
  return {message, status: 'error'};
}

export function createPasswordLoadedState<T>(data: T): PasswordsLoadState<T> {
  return {data, status: 'loaded'};
}

export function getPasswordsEnabled(settings: readonly SettingValue[]): boolean {
  const item = settings.find(setting => setting.key === 'autofill.passwords_enabled');
  return item?.value !== 'false';
}

export function getVaultAutoLockMinutes(settings: readonly SettingValue[]): string {
  const item = settings.find(setting => setting.key === 'autofill.vault_auto_lock_minutes');
  return item?.value || '15';
}

export function getVaultDeviceAuthRequired(settings: readonly SettingValue[]): boolean {
  const item = settings.find(setting => setting.key === 'autofill.vault_require_device_auth');
  return item?.value !== 'false';
}

export function getPasswordProviderBackendValue(
    provider: PasswordProviderKind): PasswordProviderBackendValue {
  return PASSWORD_PROVIDER_BACKEND_VALUES[provider];
}

export function isMahoNativeProvider(provider: PasswordProviderStatus['provider']): boolean {
  return provider === PasswordProviderKind.kMahoNative;
}

export function getPasswordImportSourceDescriptors(): readonly PasswordImportSourceDescriptor[] {
  return PASSWORD_IMPORT_SOURCE_DESCRIPTORS;
}

export function normalizeSavedPasswords(passwords: readonly SavedPassword[]): SavedPassword[] {
  return passwords.map(password => ({
    ...password,
    lastUsed: password.lastUsed || null,
  }));
}
