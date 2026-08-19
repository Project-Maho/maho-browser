// Copyright 2026 Maho Browser. All rights reserved.

import {PasswordImportSourceFormat} from '../mojo.js';
import {
  PASSWORD_IMPORT_SOURCE_DESCRIPTORS,
  type PasswordImportFormatDescriptor,
  type PasswordImportFormatId,
  type PasswordImportSourceDescriptor,
  type PasswordImportSourceId,
} from './passwords_model.js';

export type PasswordImportProviderId =
    | 'onepassword'
    | 'bitwarden'
    | 'apple_passwords'
    | 'keepass';

export type PasswordImportFormatOption = {
  readonly descriptor: PasswordImportFormatDescriptor;
  readonly pickerAccept: string;
  readonly sourceFormat: PasswordImportSourceFormat;
};

export type PasswordImportProvider = {
  readonly acceptedFormats: string;
  readonly description: string;
  readonly displayName: string;
  readonly formats: readonly PasswordImportFormatOption[];
  readonly instructions: readonly string[];
  readonly providerId: PasswordImportProviderId;
  readonly warning: string;
};

class PasswordImportDescriptorError extends Error {
  readonly name = 'PasswordImportDescriptorError';
}

function findSourceDescriptor(sourceId: PasswordImportSourceId): PasswordImportSourceDescriptor {
  const descriptor = PASSWORD_IMPORT_SOURCE_DESCRIPTORS.find(source => source.sourceId === sourceId);
  if (!descriptor) {
    throw new PasswordImportDescriptorError(`Missing password import source descriptor: ${sourceId}`);
  }
  return descriptor;
}

function findFormatDescriptor(
    sourceId: PasswordImportSourceId,
    formatId: PasswordImportFormatId): PasswordImportFormatDescriptor {
  const source = findSourceDescriptor(sourceId);
  const descriptor = source.formats.find(format => format.formatId === formatId);
  if (!descriptor) {
    throw new PasswordImportDescriptorError(`Missing password import format descriptor: ${formatId}`);
  }
  return descriptor;
}

function sourceFormatForFormatId(formatId: PasswordImportFormatId): PasswordImportSourceFormat {
  switch (formatId) {
    case 'onepassword_csv':
      return PasswordImportSourceFormat.kOnePasswordCsv;
    case 'onepassword_1pux':
      return PasswordImportSourceFormat.kOnePasswordPux;
    case 'bitwarden_individual_csv':
      return PasswordImportSourceFormat.kBitwardenIndividualCsv;
    case 'bitwarden_organization_csv':
      return PasswordImportSourceFormat.kBitwardenOrganizationCsv;
    case 'bitwarden_json':
      return PasswordImportSourceFormat.kBitwardenJson;
    case 'apple_passwords_csv':
      return PasswordImportSourceFormat.kApplePasswordsCsv;
    case 'keepassxc_csv':
      return PasswordImportSourceFormat.kKeePassXcCsv;
    case 'keepass_classic_csv':
      return PasswordImportSourceFormat.kKeePassClassicCsv;
  }
}

function createFormatOption(
    sourceId: PasswordImportSourceId,
    formatId: PasswordImportFormatId): PasswordImportFormatOption {
  const descriptor = findFormatDescriptor(sourceId, formatId);
  return {
    descriptor,
    pickerAccept: descriptor.fileExtensions.join(','),
    sourceFormat: sourceFormatForFormatId(formatId),
  };
}

const PLAINTEXT_WARNING =
    'Exported password files are plaintext. Keep the file local, import it, then delete the export when you are done.';

export const PASSWORD_IMPORT_PROVIDERS = [
  {
    acceptedFormats: '1Password CSV or 1PUX',
    description: findSourceDescriptor('onepassword').description,
    displayName: findSourceDescriptor('onepassword').displayName,
    formats: [
      createFormatOption('onepassword', 'onepassword_csv'),
      createFormatOption('onepassword', 'onepassword_1pux'),
    ],
    instructions: [
      '1Password 8 on Mac: open and unlock 1Password, choose File > Export, then choose the account to export.',
      '1Password 8 on Windows or Linux: open and unlock 1Password, select the ellipsis at the top of the sidebar, choose Export, then choose the account to export.',
      'Enter your account password, choose 1PUX or CSV, select Export Data, then choose where to save the file.',
      'CSV imports Login and Password items with Title, Website, Username, Password, One-time password, Favorite status, Archived status, Tags, and Notes. 1PUX is a ZIP archive containing export.attributes and export.data.',
    ],
    providerId: 'onepassword',
    warning: PLAINTEXT_WARNING,
  },
  {
    acceptedFormats: 'Bitwarden CSV or JSON',
    description: findSourceDescriptor('bitwarden').description,
    displayName: findSourceDescriptor('bitwarden').displayName,
    formats: [
      createFormatOption('bitwarden', 'bitwarden_individual_csv'),
      createFormatOption('bitwarden', 'bitwarden_organization_csv'),
      createFormatOption('bitwarden', 'bitwarden_json'),
    ],
    instructions: [
      'Bitwarden web app: select Tools, select Export, choose My vault or an organization in Export from, choose .csv or .json as File Format, select Export, then confirm with your master password or email verification code.',
      'Bitwarden browser extension: open Settings, choose Vault options, choose Export vault, choose My vault or an organization, choose .csv or .json, select Export vault, then confirm.',
      'Maho accepts plaintext individual CSV, organization CSV, and plaintext JSON. Encrypted JSON and ZIP-with-attachments exports are not accepted here.',
    ],
    providerId: 'bitwarden',
    warning: PLAINTEXT_WARNING,
  },
  {
    acceptedFormats: 'Apple Passwords CSV/file export',
    description: findSourceDescriptor('apple_passwords').description,
    displayName: findSourceDescriptor('apple_passwords').displayName,
    formats: [createFormatOption('apple_passwords', 'apple_passwords_csv')],
    instructions: [
      'Apple Passwords on Mac: open the Passwords app, choose File > Export All Passwords to File, click Export Passwords, then choose where to save the CSV file.',
      'For one item, select the account, choose File > Export Selected Password to File, click Export Password, then choose where to save the CSV file.',
      'Maho imports the user-selected CSV/file export only. Choose the file you saved from Passwords on Mac; this flow does not promise a Windows-side Apple export path.',
    ],
    providerId: 'apple_passwords',
    warning: PLAINTEXT_WARNING,
  },
  {
    acceptedFormats: 'KeePass / KeePassXC CSV',
    description: 'Import a user-exported KeePass or KeePassXC CSV file.',
    displayName: 'KeePass / KeePassXC',
    formats: [
      createFormatOption('keepassxc', 'keepassxc_csv'),
      createFormatOption('keepass_classic', 'keepass_classic_csv'),
    ],
    instructions: [
      'KeePassXC CLI: run keepassxc-cli export -f csv <database.kdbx> > keepassxc.csv, then select the CSV file here.',
      'KeePassXC CSV fields are Group, Title, Username, Password, URL, and Notes.',
      'KeePass classic CSV variants with Account, Login Name, Password, Web Site, and Comments are also accepted.',
    ],
    providerId: 'keepass',
    warning: PLAINTEXT_WARNING,
  },
] as const satisfies readonly PasswordImportProvider[];

export function getPasswordImportProviders(): readonly PasswordImportProvider[] {
  return PASSWORD_IMPORT_PROVIDERS;
}

export function getPasswordImportProvider(
    providerId: PasswordImportProviderId): PasswordImportProvider {
  const provider = PASSWORD_IMPORT_PROVIDERS.find(item => item.providerId === providerId);
  if (!provider) {
    throw new PasswordImportDescriptorError(`Missing password import provider: ${providerId}`);
  }
  return provider;
}

export function getPasswordImportFormat(
    provider: PasswordImportProvider,
    formatId: PasswordImportFormatId): PasswordImportFormatOption {
  const option = provider.formats.find(item => item.descriptor.formatId === formatId);
  if (!option) {
    throw new PasswordImportDescriptorError(
        `Missing password import format ${formatId} for provider ${provider.providerId}`);
  }
  return option;
}

export function getDefaultPasswordImportFormat(
    provider: PasswordImportProvider): PasswordImportFormatOption {
  const option = provider.formats[0];
  if (!option) {
    throw new PasswordImportDescriptorError(
        `Password import provider has no formats: ${provider.providerId}`);
  }
  return option;
}

export function summarizeImportFilePath(filePath: string): string {
  const parts = filePath.split(/[\\/]/).filter(part => part.length > 0);
  return parts.at(-1) ?? filePath;
}
