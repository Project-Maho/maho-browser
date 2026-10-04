// Copyright 2026 Maho Browser. All rights reserved.

import type {MailOnboardingStringKey} from './mail_onboarding_api.js';

export type SetupStep = 'provider' | 'oauth' | 'credentials' | 'translation' | 'success';
export type OAuthProviderId = 'gmail' | 'outlook';
export type ProviderId =
  | OAuthProviderId
  | 'yahoo'
  | 'icloud'
  | 'fastmail'
  | 'zoho'
  | 'aol'
  | 'gmx'
  | 'yandex'
  | 'naver'
  | 'other';
export type MailServerEncryption = 'Tls' | 'StartTls' | 'None';

export type ServerConfig = {
  readonly imapHost: string;
  readonly imapPort: number;
  readonly imapEncryption: MailServerEncryption;
  readonly smtpHost: string;
  readonly smtpPort: number;
  readonly smtpEncryption: MailServerEncryption;
};

export type ProviderOption = {
  readonly id: ProviderId;
  readonly nameKey: MailOnboardingStringKey;
  readonly nameFallback: string;
  readonly descriptionKey: MailOnboardingStringKey;
  readonly descriptionFallback: string;
};

export type ProviderSetupGuide = {
  readonly titleKey: MailOnboardingStringKey;
  readonly titleFallback: string;
  readonly steps: readonly (readonly [MailOnboardingStringKey, string])[];
};

const SMART_DEFAULTS: Record<string, ServerConfig> = {
  'gmail.com': {imapHost: 'imap.gmail.com', imapPort: 993, imapEncryption: 'Tls', smtpHost: 'smtp.gmail.com', smtpPort: 587, smtpEncryption: 'StartTls'},
  'outlook.com': {imapHost: 'outlook.office365.com', imapPort: 993, imapEncryption: 'Tls', smtpHost: 'smtp.office365.com', smtpPort: 587, smtpEncryption: 'StartTls'},
  'hotmail.com': {imapHost: 'outlook.office365.com', imapPort: 993, imapEncryption: 'Tls', smtpHost: 'smtp.office365.com', smtpPort: 587, smtpEncryption: 'StartTls'},
  'yahoo.com': {imapHost: 'imap.mail.yahoo.com', imapPort: 993, imapEncryption: 'Tls', smtpHost: 'smtp.mail.yahoo.com', smtpPort: 587, smtpEncryption: 'StartTls'},
  'icloud.com': {imapHost: 'imap.mail.me.com', imapPort: 993, imapEncryption: 'Tls', smtpHost: 'smtp.mail.me.com', smtpPort: 587, smtpEncryption: 'StartTls'},
  'me.com': {imapHost: 'imap.mail.me.com', imapPort: 993, imapEncryption: 'Tls', smtpHost: 'smtp.mail.me.com', smtpPort: 587, smtpEncryption: 'StartTls'},
  'fastmail.com': {imapHost: 'imap.fastmail.com', imapPort: 993, imapEncryption: 'Tls', smtpHost: 'smtp.fastmail.com', smtpPort: 587, smtpEncryption: 'StartTls'},
  'zoho.com': {imapHost: 'imap.zoho.com', imapPort: 993, imapEncryption: 'Tls', smtpHost: 'smtp.zoho.com', smtpPort: 587, smtpEncryption: 'StartTls'},
  'aol.com': {imapHost: 'imap.aol.com', imapPort: 993, imapEncryption: 'Tls', smtpHost: 'smtp.aol.com', smtpPort: 587, smtpEncryption: 'StartTls'},
  'gmx.com': {imapHost: 'imap.gmx.com', imapPort: 993, imapEncryption: 'Tls', smtpHost: 'mail.gmx.com', smtpPort: 587, smtpEncryption: 'StartTls'},
  'yandex.com': {imapHost: 'imap.yandex.com', imapPort: 993, imapEncryption: 'Tls', smtpHost: 'smtp.yandex.com', smtpPort: 587, smtpEncryption: 'StartTls'},
  'yandex.ru': {imapHost: 'imap.yandex.com', imapPort: 993, imapEncryption: 'Tls', smtpHost: 'smtp.yandex.com', smtpPort: 587, smtpEncryption: 'StartTls'},
  'naver.com': {imapHost: 'imap.naver.com', imapPort: 993, imapEncryption: 'Tls', smtpHost: 'smtp.naver.com', smtpPort: 587, smtpEncryption: 'StartTls'},
};

export const PROVIDERS = [
  {id: 'gmail', nameKey: 'onboarding.providerGmail', nameFallback: 'Gmail', descriptionKey: 'onboarding.providerGmailDesc', descriptionFallback: 'Sign in with Google'},
  {id: 'outlook', nameKey: 'onboarding.providerOutlook', nameFallback: 'Outlook', descriptionKey: 'onboarding.providerOutlookDesc', descriptionFallback: 'Sign in with Microsoft'},
  {id: 'yahoo', nameKey: 'onboarding.providerYahoo', nameFallback: 'Yahoo Mail', descriptionKey: 'onboarding.providerAppPasswordDesc', descriptionFallback: 'App password required'},
  {id: 'icloud', nameKey: 'onboarding.providerIcloud', nameFallback: 'iCloud Mail', descriptionKey: 'onboarding.providerAppPasswordDesc', descriptionFallback: 'App password required'},
  {id: 'fastmail', nameKey: 'onboarding.providerFastmail', nameFallback: 'Fastmail', descriptionKey: 'onboarding.providerAppPasswordDesc', descriptionFallback: 'App password required'},
  {id: 'zoho', nameKey: 'onboarding.providerZoho', nameFallback: 'Zoho Mail', descriptionKey: 'onboarding.providerAppPasswordDesc', descriptionFallback: 'App password required'},
  {id: 'aol', nameKey: 'onboarding.providerAol', nameFallback: 'AOL', descriptionKey: 'onboarding.providerAppPasswordDesc', descriptionFallback: 'App password required'},
  {id: 'gmx', nameKey: 'onboarding.providerGmx', nameFallback: 'GMX', descriptionKey: 'onboarding.providerGmxDesc', descriptionFallback: 'Free email service'},
  {id: 'yandex', nameKey: 'onboarding.providerYandex', nameFallback: 'Yandex', descriptionKey: 'onboarding.providerAppPasswordDesc', descriptionFallback: 'App password required'},
  {id: 'naver', nameKey: 'onboarding.providerNaver', nameFallback: 'Naver', descriptionKey: 'onboarding.providerNaverDesc', descriptionFallback: 'Enable IMAP and use an app password'},
  {id: 'other', nameKey: 'onboarding.providerOther', nameFallback: 'Other', descriptionKey: 'onboarding.providerOtherDesc', descriptionFallback: 'IMAP/SMTP manual setup'},
] as const satisfies readonly ProviderOption[];

export const PRIMARY_PROVIDER_IDS = ['gmail', 'outlook', 'yahoo', 'icloud', 'other'] as const satisfies readonly ProviderId[];

export const PROVIDER_SETUP_GUIDES: Partial<Record<ProviderId, ProviderSetupGuide>> = {
  naver: {
    titleKey: 'onboarding.naverGuideTitle',
    titleFallback: 'Naver Mail Setup',
    steps: [
      ['onboarding.naverGuideStep1', 'Log in to Naver Mail (mail.naver.com)'],
      ['onboarding.naverGuideStep2', 'Go to Settings → POP3/IMAP'],
      ['onboarding.naverGuideStep3', 'Enable IMAP/SMTP access'],
      ['onboarding.naverGuideStep4', 'Set up 2-step verification in Naver account settings'],
      ['onboarding.naverGuideStep5', 'Generate an app password and use it below'],
    ],
  },
  yahoo: {
    titleKey: 'onboarding.yahooGuideTitle',
    titleFallback: 'Yahoo Mail Setup',
    steps: [
      ['onboarding.yahooGuideStep1', 'Enable 2-step verification in Yahoo account security'],
      ['onboarding.yahooGuideStep2', 'Generate an app password (Account Info → Security → App passwords)'],
      ['onboarding.yahooGuideStep3', 'Use the app password below instead of your regular password'],
    ],
  },
  icloud: {
    titleKey: 'onboarding.icloudGuideTitle',
    titleFallback: 'iCloud Mail Setup',
    steps: [
      ['onboarding.icloudGuideStep1', 'Enable 2-factor authentication for your Apple ID'],
      ['onboarding.icloudGuideStep2', 'Go to appleid.apple.com → Sign-In and Security → App-Specific Passwords'],
      ['onboarding.icloudGuideStep3', 'Generate an app password and use it below'],
    ],
  },
};

export function getSmartDefaults(email: string): ServerConfig | null {
  const domain = email.split('@')[1]?.toLowerCase();
  if (!domain) return null;

  const defaults = SMART_DEFAULTS[domain];
  if (defaults) return defaults;

  return {
    imapHost: `imap.${domain}`,
    imapPort: 993,
    imapEncryption: 'Tls',
    smtpHost: `smtp.${domain}`,
    smtpPort: 587,
    smtpEncryption: 'StartTls',
  };
}

export function isOAuthProvider(provider: ProviderId): provider is OAuthProviderId {
  return provider === 'gmail' || provider === 'outlook';
}

export function isPrimaryProviderId(provider: ProviderId): boolean {
  return PRIMARY_PROVIDER_IDS.some(primaryProvider => primaryProvider === provider);
}

export function isMailServerEncryption(value: string): value is MailServerEncryption {
  return value === 'Tls' || value === 'StartTls' || value === 'None';
}
