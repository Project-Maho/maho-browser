// Copyright 2026 Maho Browser. All rights reserved.

import type {
  MailAccount,
  MailOnboardingActionFlags,
  MailOnboardingProps,
  MailOnboardingStringLookup,
} from './mail_onboarding_api.js';
import type {MailServerEncryption, ProviderId, ProviderOption, SetupStep} from './mail_onboarding_config.js';

export type MailServiceStatus = 'preparing' | 'ready' | 'unavailable';
export type TranslationProviderChoice = 'byok' | 'skip';

export type ProviderStepState = {
  readonly visibleProviders: readonly ProviderOption[];
  readonly hasHiddenProviders: boolean;
  readonly showAllProviders: boolean;
  readonly mailServiceStatus: MailServiceStatus;
  readonly error: string | null;
  readonly busy: boolean;
};

export type ProviderStepActions = {
  readonly onSelectProvider: (provider: ProviderId) => Promise<void>;
  readonly onShowAllProvidersChange: (showAllProviders: boolean) => void;
};

export type CredentialsStepState = {
  readonly selectedProvider: ProviderId | null;
  readonly email: string;
  readonly displayName: string;
  readonly password: string;
  readonly imapHost: string;
  readonly smtpHost: string;
  readonly imapPortText: string;
  readonly smtpPortText: string;
  readonly imapEncryption: MailServerEncryption;
  readonly smtpEncryption: MailServerEncryption;
  readonly showAdvanced: boolean;
  readonly error: string | null;
  readonly testSuccess: boolean;
  readonly busy: boolean;
  readonly actionFlags: MailOnboardingActionFlags;
};

export type CredentialsStepActions = {
  readonly onBack: () => void;
  readonly onEmailChange: (value: string) => void;
  readonly onDisplayNameChange: (value: string) => void;
  readonly onPasswordChange: (value: string) => void;
  readonly onEmailBlur: () => void;
  readonly onToggleAdvanced: () => void;
  readonly onImapHostChange: (value: string) => void;
  readonly onSmtpHostChange: (value: string) => void;
  readonly onImapPortTextChange: (value: string) => void;
  readonly onSmtpPortTextChange: (value: string) => void;
  readonly onImapEncryptionChange: (value: MailServerEncryption) => void;
  readonly onSmtpEncryptionChange: (value: MailServerEncryption) => void;
  readonly onTestConnection: () => Promise<void>;
  readonly onAddAccount: () => Promise<void>;
};

export type TranslationStepState = {
  readonly busy: boolean;
  readonly checkingAiConfig: boolean;
  readonly isAiConfigured: boolean;
  readonly error: string | null;
};

export type MailOnboardingViewModel = {
  readonly copy: MailOnboardingStringLookup;
  readonly step: SetupStep;
  readonly stepperSteps: MailOnboardingProps['config']['steps'];
  readonly accounts: readonly MailAccount[];
  readonly busy: boolean;
  readonly selectedProvider: ProviderId | null;
  readonly providerState: ProviderStepState;
  readonly providerActions: ProviderStepActions;
  readonly credentialsState: CredentialsStepState;
  readonly credentialsActions: CredentialsStepActions;
  readonly translationState: TranslationStepState;
  readonly onCancelOAuth: () => void;
  readonly onDeleteAccount: (account: MailAccount) => Promise<void>;
  readonly onSelectTranslationProvider: (provider: TranslationProviderChoice) => Promise<void>;
  readonly onComplete: () => void;
};
