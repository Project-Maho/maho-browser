import type {RuntimeEvent} from '../maho_ai.mojom-webui.js';
import {RuntimeEventKind} from '../maho_ai.mojom-webui.js';

export const CredentialErrorCode = {
  kGeneric: 0,
  kProviderNotConfigured: 1,
  kCredentialUnusable: 2,
  kSecureStoreUnavailable: 3,
  kCredentialDecryptFailed: 4,
  kManagedAuthUnavailable: 5,
  kUnsupportedProvider: 6,
} as const;

export type CredentialSettingsPane = 'account-sync'|'maho-ai';

export interface CredentialErrorPresentation {
  readonly actionLabel: string;
  readonly description: string;
  readonly settingsPane: CredentialSettingsPane;
  readonly title: string;
}

export interface CredentialRuntimeEvent extends RuntimeEvent {
  readonly credentialErrorCode?: number|null;
}

const genericCredentialPresentation: CredentialErrorPresentation = {
  actionLabel: 'Open AI settings',
  description: 'Maho could not use your AI credential. Review Settings, then try again.',
  settingsPane: 'maho-ai',
  title: 'Review AI settings',
};

export function getCredentialErrorPresentation(
    event: RuntimeEvent): CredentialErrorPresentation|null {
  if (event.kind !== RuntimeEventKind.kError || !('credentialErrorCode' in event) ||
      typeof event.credentialErrorCode !== 'number') {
    return null;
  }

  switch (event.credentialErrorCode) {
    case CredentialErrorCode.kProviderNotConfigured:
      return {
        actionLabel: 'Open AI settings',
        description: 'Select an AI provider in Settings, then try again.',
        settingsPane: 'maho-ai',
        title: 'Choose an AI provider',
      };
    case CredentialErrorCode.kCredentialUnusable:
      return {
        actionLabel: 'Open AI settings',
        description: 'Enter a valid AI credential in Settings, then try again.',
        settingsPane: 'maho-ai',
        title: 'Update your AI credential',
      };
    case CredentialErrorCode.kSecureStoreUnavailable:
      return {
        actionLabel: 'Open AI settings',
        description: 'AI credentials are temporarily unavailable. Review Settings, then try again.',
        settingsPane: 'maho-ai',
        title: 'AI credentials are unavailable',
      };
    case CredentialErrorCode.kCredentialDecryptFailed:
      return {
        actionLabel: 'Open AI settings',
        description: 'Re-enter your AI credential in Settings, then try again.',
        settingsPane: 'maho-ai',
        title: 'Re-enter your AI credential',
      };
    case CredentialErrorCode.kManagedAuthUnavailable:
      return {
        actionLabel: 'Open account settings',
        description: 'Sign in again in Settings, then try again.',
        settingsPane: 'account-sync',
        title: 'Reconnect your Maho account',
      };
    case CredentialErrorCode.kUnsupportedProvider:
      return {
        actionLabel: 'Open AI settings',
        description: 'Select a supported AI provider in Settings, then try again.',
        settingsPane: 'maho-ai',
        title: 'Choose a supported AI provider',
      };
    case CredentialErrorCode.kGeneric:
    default:
      return genericCredentialPresentation;
  }
}
