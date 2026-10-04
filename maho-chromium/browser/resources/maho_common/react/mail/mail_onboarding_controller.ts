// Copyright 2026 Maho Browser. All rights reserved.

import {useState} from 'react';

import type {
  MailAccount,
  MailOnboardingAction,
  MailOnboardingProps,
} from './mail_onboarding_api.js';
import {
  defaultMailOnboardingStringLookup,
  deleteMailAccountWithConfirmation,
  getMailOnboardingActionFlags,
  isMailHelperUnavailableError,
  parseMailAccounts,
  toSafeMailErrorMessage,
} from './mail_onboarding_api.js';
import type {MailServerEncryption, ProviderId, SetupStep} from './mail_onboarding_config.js';
import {getSmartDefaults, isOAuthProvider, isPrimaryProviderId, PROVIDERS} from './mail_onboarding_config.js';
import type {MailOnboardingViewModel, TranslationProviderChoice} from './mail_onboarding_controller_types.js';
import {
  buildMailCredentialsRequestJson,
  deriveDisplayNameFromEmail,
  parseMailCredentialPorts,
} from './mail_onboarding_helpers.js';
import {useMailOnboardingAiConfig, useMailOnboardingLifecycle} from './mail_onboarding_lifecycle_hooks.js';
import {createMailOnboardingCopy, defaultConfirmDeleteAccount} from './mail_onboarding_strings.js';

export function useMailOnboardingController({
  api,
  accounts,
  onAccountsChange,
  config,
  t = defaultMailOnboardingStringLookup,
  confirmDeleteAccount,
}: MailOnboardingProps): MailOnboardingViewModel {
  const copy = createMailOnboardingCopy(t);
  const hasTranslationStep = config.steps.includes('translation');
  const [step, setStep] = useState<SetupStep>('provider');
  const [selectedProvider, setSelectedProvider] = useState<ProviderId | null>(null);
  const [email, setEmail] = useState('');
  const [displayName, setDisplayName] = useState('');
  const [password, setPassword] = useState('');
  const [imapHost, setImapHost] = useState('');
  const [smtpHost, setSmtpHost] = useState('');
  const [imapPortText, setImapPortText] = useState('993');
  const [smtpPortText, setSmtpPortText] = useState('587');
  const [imapEncryption, setImapEncryption] = useState<MailServerEncryption>('Tls');
  const [smtpEncryption, setSmtpEncryption] = useState<MailServerEncryption>('StartTls');
  const [showAdvanced, setShowAdvanced] = useState(false);
  const [serverTouched, setServerTouched] = useState(false);
  const [action, setAction] = useState<MailOnboardingAction | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [testSuccess, setTestSuccess] = useState(false);
  const [showAllProviders, setShowAllProviders] = useState(false);
  const {lifecycleRef, oauthAccountIds, mailServiceStatus, setMailServiceStatus} = useMailOnboardingLifecycle({
    api,
    accounts,
    onAccountsChange,
    config,
    hasTranslationStep,
    setAction,
    setStep,
    setError,
  });
  const {checkingAiConfig, isAiConfigured} = useMailOnboardingAiConfig(step, api);

  const busy = action !== null;
  const actionFlags = getMailOnboardingActionFlags(action);
  const visibleProviders = showAllProviders ? PROVIDERS : PROVIDERS.filter(provider => isPrimaryProviderId(provider.id));

  const resetCredentials = () => {
    setPassword('');
    setTestSuccess(false);
  };

  const selectProvider = async (provider: ProviderId) => {
    resetCredentials();
    setSelectedProvider(provider);
    setError(null);
    setTestSuccess(false);
    if (!isOAuthProvider(provider)) {
      setShowAdvanced(provider === 'other');
      setStep('credentials');
      return;
    }
    setAction('oauth');
    oauthAccountIds.current = new Set(accounts.map(account => account.id));
    const lifecycle = lifecycleRef.current;
    if (!lifecycle) {
      setAction(null);
      setError(copy('onboarding.oauthUnavailable', 'OAuth is unavailable. Try IMAP instead.'));
      return;
    }
    lifecycle.setOauthAccountIds(oauthAccountIds.current);
    try {
      const beginResult = await lifecycle.beginOAuth(provider);
      if (beginResult.ok === false && beginResult.cancelled) return;
      if (beginResult.ok === false) {
        setAction(null);
        setError(beginResult.errorMessage);
        return;
      }
      setStep('oauth');
    } catch {
      setAction(null);
      setError(copy('onboarding.oauthUnavailable', 'OAuth is unavailable. Try IMAP instead.'));
    }
  };

  const applyServerTouched = (setter: (value: string) => void, value: string) => {
    setter(value);
    setServerTouched(true);
  };

  const handleEmailBlur = () => {
    if (!email.includes('@') || serverTouched) return;
    const smartDefaults = getSmartDefaults(email);
    if (!smartDefaults) return;
    setImapHost(smartDefaults.imapHost);
    setImapPortText(String(smartDefaults.imapPort));
    setImapEncryption(smartDefaults.imapEncryption);
    setSmtpHost(smartDefaults.smtpHost);
    setSmtpPortText(String(smartDefaults.smtpPort));
    setSmtpEncryption(smartDefaults.smtpEncryption);
    if (!displayName) setDisplayName(deriveDisplayNameFromEmail(email));
  };

  const parsePortsForSubmit = (includeSmtp: boolean) => {
    const result = parseMailCredentialPorts(imapPortText, smtpPortText, includeSmtp);
    if (!result.ok) setError(copy('account.portRange', 'Port numbers must be between 1 and 65535.'));
    return result.ok ? result.ports : null;
  };

  const buildRequestJson = (mode: 'test' | 'add', ports: {readonly imap: number; readonly smtp: number}) => buildMailCredentialsRequestJson({
    mode,
    email,
    displayName,
    password,
    imapHost,
    imapPort: ports.imap,
    imapEncryption,
    smtpHost,
    smtpPort: ports.smtp,
    smtpEncryption,
  });

  const handleTestConnection = async () => {
    if (!email.trim() || !password || !imapHost.trim()) {
      setError(copy('onboarding.testRequiredFields', 'Email, password, and IMAP host are required.'));
      return;
    }
    const ports = parsePortsForSubmit(false);
    if (!ports) return;
    setAction('testing');
    setError(null);
    setTestSuccess(false);
    try {
      const result = await api.testConnection(buildRequestJson('test', ports));
      if (!result.ok) {
        const isHelperUnavailable = isMailHelperUnavailableError(result.resultJson);
        if (isHelperUnavailable) setMailServiceStatus('preparing');
        const fallback = isHelperUnavailable
          ? copy('onboarding.mailPreparing', 'Mail is still getting ready. We will retry automatically.')
          : copy('onboarding.testFailed', 'Could not connect to the IMAP server.');
        setError(toSafeMailErrorMessage(result.resultJson, fallback));
        return;
      }
      setTestSuccess(true);
    } catch {
      setError(copy('onboarding.testFailed', 'Could not connect to the IMAP server.'));
    } finally {
      setAction(null);
    }
  };

  const handleAddAccount = async () => {
    const emailRegex = /^[^\s@]+@[^\s@]+\.[^\s@]+$/;
    if (!emailRegex.test(email.trim())) {
      setError(copy('onboarding.invalidEmail', 'Please enter a valid email address.'));
      return;
    }
    if (!password || !imapHost.trim() || !smtpHost.trim()) {
      setError(copy('account.requiredFields', 'Please fill in all required fields.'));
      return;
    }
    const ports = parsePortsForSubmit(true);
    if (!ports) return;
    setAction('adding');
    setError(null);
    try {
      const result = await api.addAccount(buildRequestJson('add', ports));
      if (!result.ok) {
        const isHelperUnavailable = isMailHelperUnavailableError(result.resultJson);
        if (isHelperUnavailable) setMailServiceStatus('preparing');
        const fallback = isHelperUnavailable
          ? copy('onboarding.mailPreparing', 'Mail is still getting ready. We will retry automatically.')
          : copy('onboarding.addFailed', 'Could not add this mail account.');
        setError(toSafeMailErrorMessage(result.resultJson, fallback));
        return;
      }
      resetCredentials();
      setStep(hasTranslationStep ? 'translation' : 'success');
      try {
        const accountsResult = await api.listAccounts();
        if (accountsResult.ok) {
          const parsed = parseMailAccounts(accountsResult.resultJson);
          if (parsed) onAccountsChange(parsed);
        }
      } catch (error) {
        // Creation succeeded. Account events or window focus will reconcile the list.
        console.error('Mail account added, but account list refresh failed', error);
      }
    } catch {
      setError(copy('onboarding.addFailed', 'Could not add this mail account.'));
    } finally {
      setAction(null);
    }
  };

  const handleDeleteAccount = async (account: MailAccount) => {
    setAction('deleting');
    setError(null);
    try {
      const outcome = await deleteMailAccountWithConfirmation({
        account,
        accounts,
        confirmDelete: confirmDeleteAccount ?? defaultConfirmDeleteAccount(copy),
        deleteAccount: api.deleteAccount,
        failureMessage: copy(
          'onboarding.deleteFailed',
          'Could not delete this mail account.',
        ),
      });
      switch (outcome.kind) {
        case 'cancelled':
          return;
        case 'deleted':
          onAccountsChange(outcome.accounts);
          return;
        case 'failed':
          setError(outcome.errorMessage);
          return;
      }
    } catch {
      setError(copy('onboarding.deleteFailed', 'Could not delete this mail account.'));
    } finally {
      setAction(null);
    }
  };

  const selectTranslationProvider = async (provider: TranslationProviderChoice) => {
    if (!api.setTranslationProvider) {
      setError(copy('onboarding.translationUnavailable', 'Translation setup is unavailable right now.'));
      return;
    }
    setAction('translation');
    const ok = await api.setTranslationProvider(provider);
    setAction(null);
    if (ok) {
      setStep('success');
    } else {
      setError(copy('onboarding.translationConfigFailed', 'Failed to configure translation setting.'));
    }
  };

  return {
    copy,
    step,
    stepperSteps: config.steps,
    accounts,
    busy,
    selectedProvider,
    providerState: {visibleProviders, hasHiddenProviders: visibleProviders.length < PROVIDERS.length, showAllProviders, mailServiceStatus, error, busy},
    providerActions: {onSelectProvider: selectProvider, onShowAllProvidersChange: setShowAllProviders},
    credentialsState: {selectedProvider, email, displayName, password, imapHost, smtpHost, imapPortText, smtpPortText, imapEncryption, smtpEncryption, showAdvanced, error, testSuccess, busy, actionFlags},
    credentialsActions: {
      onBack: () => { resetCredentials(); setStep('provider'); setSelectedProvider(null); setError(null); },
      onEmailChange: setEmail,
      onDisplayNameChange: setDisplayName,
      onPasswordChange: setPassword,
      onEmailBlur: handleEmailBlur,
      onToggleAdvanced: () => setShowAdvanced(!showAdvanced),
      onImapHostChange: value => applyServerTouched(setImapHost, value),
      onSmtpHostChange: value => applyServerTouched(setSmtpHost, value),
      onImapPortTextChange: value => applyServerTouched(setImapPortText, value),
      onSmtpPortTextChange: value => applyServerTouched(setSmtpPortText, value),
      onImapEncryptionChange: value => { setImapEncryption(value); setServerTouched(true); },
      onSmtpEncryptionChange: value => { setSmtpEncryption(value); setServerTouched(true); },
      onTestConnection: handleTestConnection,
      onAddAccount: handleAddAccount,
    },
    translationState: {busy, checkingAiConfig, isAiConfigured, error},
    onCancelOAuth: () => {
      lifecycleRef.current?.cancelActiveOAuth();
      setStep('provider');
      setSelectedProvider(null);
      setAction(null);
    },
    onDeleteAccount: handleDeleteAccount,
    onSelectTranslationProvider: selectTranslationProvider,
    onComplete: () => api.onComplete(),
  };
}
