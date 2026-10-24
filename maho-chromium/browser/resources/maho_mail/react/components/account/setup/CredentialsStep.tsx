import { useState } from 'react';
import { useTranslation } from 'react-i18next';
import { ArrowLeft, ChevronDown, ChevronRight, AlertTriangle, CheckCircle, Info } from 'lucide-react';
import * as api from '../../../api';
import type { Encryption, CreateAccountRequest } from '../../../types';
import { Button } from '../../ui';
import { Input } from '../../ui';
import { Select } from '../../ui';
import type { SelectOption } from '../../ui';
import { useToast } from '../../ui/Toast';
import { trackEvent } from '../../../utils/analytics';

import type { ProviderId } from './AccountSetupFlow';

interface ServerConfig {
  imapHost: string;
  imapPort: number;
  imapEncryption: Encryption;
  smtpHost: string;
  smtpPort: number;
  smtpEncryption: Encryption;
}

function getSmartDefaults(email: string): ServerConfig | null {
  const domain = email.split('@')[1]?.toLowerCase();
  if (!domain) return null;

  const defaults: Record<string, ServerConfig> = {
    'gmail.com': { imapHost: 'imap.gmail.com', imapPort: 993, imapEncryption: 'Tls', smtpHost: 'smtp.gmail.com', smtpPort: 587, smtpEncryption: 'StartTls' },
    'outlook.com': { imapHost: 'outlook.office365.com', imapPort: 993, imapEncryption: 'Tls', smtpHost: 'smtp.office365.com', smtpPort: 587, smtpEncryption: 'StartTls' },
    'hotmail.com': { imapHost: 'outlook.office365.com', imapPort: 993, imapEncryption: 'Tls', smtpHost: 'smtp.office365.com', smtpPort: 587, smtpEncryption: 'StartTls' },
    'yahoo.com': { imapHost: 'imap.mail.yahoo.com', imapPort: 993, imapEncryption: 'Tls', smtpHost: 'smtp.mail.yahoo.com', smtpPort: 587, smtpEncryption: 'StartTls' },
    'icloud.com': { imapHost: 'imap.mail.me.com', imapPort: 993, imapEncryption: 'Tls', smtpHost: 'smtp.mail.me.com', smtpPort: 587, smtpEncryption: 'StartTls' },
    'me.com': { imapHost: 'imap.mail.me.com', imapPort: 993, imapEncryption: 'Tls', smtpHost: 'smtp.mail.me.com', smtpPort: 587, smtpEncryption: 'StartTls' },
    'fastmail.com': { imapHost: 'imap.fastmail.com', imapPort: 993, imapEncryption: 'Tls', smtpHost: 'smtp.fastmail.com', smtpPort: 587, smtpEncryption: 'StartTls' },
    'zoho.com': { imapHost: 'imap.zoho.com', imapPort: 993, imapEncryption: 'Tls', smtpHost: 'smtp.zoho.com', smtpPort: 587, smtpEncryption: 'StartTls' },
    'aol.com': { imapHost: 'imap.aol.com', imapPort: 993, imapEncryption: 'Tls', smtpHost: 'smtp.aol.com', smtpPort: 587, smtpEncryption: 'StartTls' },
    'gmx.com': { imapHost: 'imap.gmx.com', imapPort: 993, imapEncryption: 'Tls', smtpHost: 'mail.gmx.com', smtpPort: 587, smtpEncryption: 'StartTls' },
    'yandex.com': { imapHost: 'imap.yandex.com', imapPort: 993, imapEncryption: 'Tls', smtpHost: 'smtp.yandex.com', smtpPort: 587, smtpEncryption: 'StartTls' },
    'yandex.ru': { imapHost: 'imap.yandex.com', imapPort: 993, imapEncryption: 'Tls', smtpHost: 'smtp.yandex.com', smtpPort: 587, smtpEncryption: 'StartTls' },
    'naver.com': { imapHost: 'imap.naver.com', imapPort: 993, imapEncryption: 'Tls', smtpHost: 'smtp.naver.com', smtpPort: 587, smtpEncryption: 'StartTls' },
  };

  if (defaults[domain]) return defaults[domain];

  return {
    imapHost: `imap.${domain}`,
    imapPort: 993,
    imapEncryption: 'Tls',
    smtpHost: `smtp.${domain}`,
    smtpPort: 587,
    smtpEncryption: 'StartTls',
  };
}

const ENCRYPTION_OPTIONS: SelectOption[] = [
  { value: 'Tls', label: 'TLS' },
  { value: 'StartTls', label: 'STARTTLS' },
  { value: 'None', label: 'None' },
];

// Locale keys under onboarding.*
const PROVIDER_SETUP_GUIDES: Partial<Record<ProviderId, { title: string; steps: string[] }>> = {
  naver: {
    title: 'naverGuideTitle',
    steps: ['naverGuideStep1', 'naverGuideStep2', 'naverGuideStep3', 'naverGuideStep4', 'naverGuideStep5'],
  },
  yahoo: {
    title: 'yahooGuideTitle',
    steps: ['yahooGuideStep1', 'yahooGuideStep2', 'yahooGuideStep3'],
  },
  icloud: {
    title: 'icloudGuideTitle',
    steps: ['icloudGuideStep1', 'icloudGuideStep2', 'icloudGuideStep3'],
  },
};

function parsePort(value: string): number {
  return /^\d+$/.test(value.trim()) ? parseInt(value, 10) : NaN;
}

interface CredentialsStepProps {
  providerHint: ProviderId | null;
  onSuccess: (accountId: string) => void;
  onBack: () => void;
}

export function CredentialsStep({ providerHint, onSuccess, onBack }: CredentialsStepProps) {
  const { toast } = useToast();
  const { t } = useTranslation();

  const [email, setEmail] = useState('');
  const [displayName, setDisplayName] = useState('');
  const [password, setPassword] = useState('');
  const [imapHost, setImapHost] = useState('');
  const [smtpHost, setSmtpHost] = useState('');
  const [imapPort, setImapPort] = useState('993');
  const [smtpPort, setSmtpPort] = useState('587');
  const [imapEncryption, setImapEncryption] = useState<Encryption>('Tls');
  const [smtpEncryption, setSmtpEncryption] = useState<Encryption>('StartTls');
  const [showAdvanced, setShowAdvanced] = useState(providerHint === 'other');
  const [serverTouched, setServerTouched] = useState(false);
  const [testing, setTesting] = useState(false);
  const [testResult, setTestResult] = useState<boolean | null>(null);
  const [submitting, setSubmitting] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const isDisabled = submitting || testing;

  // Any edit invalidates the previous connection test result.
  function edit<T>(setter: (v: T) => void) {
    return (v: T) => { setter(v); setTestResult(null); };
  }

  function handleEmailBlur() {
    if (!email.includes('@')) return;
    if (serverTouched) return;
    const config = getSmartDefaults(email);
    if (!config) return;
    setImapHost(config.imapHost);
    setImapPort(String(config.imapPort));
    setImapEncryption(config.imapEncryption);
    setSmtpHost(config.smtpHost);
    setSmtpPort(String(config.smtpPort));
    setSmtpEncryption(config.smtpEncryption);
    if (!displayName) {
      const name = email.split('@')[0];
      setDisplayName(name.charAt(0).toUpperCase() + name.slice(1));
    }
  }

  function buildRequest(): CreateAccountRequest {
    return {
      email,
      display_name: displayName,
      auth_type: 'password',
      imap_host: imapHost,
      imap_port: parsePort(imapPort),
      imap_encryption: imapEncryption,
      smtp_host: smtpHost,
      smtp_port: parsePort(smtpPort),
      smtp_encryption: smtpEncryption,
      username: email,
      password: password || undefined,
    };
  }

  function validateServerSettings(): string | null {
    if (!imapHost || !smtpHost) return t('account.requiredFields');
    const ports = [parsePort(imapPort), parsePort(smtpPort)];
    if (ports.some((p) => !(p >= 1 && p <= 65535))) return t('account.portRange');
    return null;
  }

  async function handleTestConnection() {
    const validationError = validateServerSettings();
    if (validationError) {
      setTestResult(null);
      setError(validationError);
      return;
    }
    try {
      setTesting(true);
      setTestResult(null);
      setError(null);
      const result = await api.testConnection(buildRequest());
      setTestResult(result);
    } catch (err) {
      setTestResult(false);
      const message = err instanceof Error ? err.message : t('account.testFailed');
      setError(message);
      toast('error', message);
    } finally {
      setTesting(false);
    }
  }

  async function handleSubmit() {
    const emailRegex = /^[^\s@]+@[^\s@]+\.[^\s@]+$/;
    if (!emailRegex.test(email)) { setError(t('onboarding.invalidEmail')); return; }
    if (!password) { setError(t('account.requiredFields')); return; }
    const validationError = validateServerSettings();
    if (validationError) { setError(validationError); return; }
    try {
      setSubmitting(true);
      setError(null);
      const account = await api.createAccount(buildRequest());
      trackEvent('Account Added', { provider: providerHint ?? 'manual' });
      try {
        await api.syncFolders(account.id);
      } catch (syncErr) {
        toast('error', syncErr instanceof Error ? syncErr.message : 'Account created, but folder sync failed');
      }
      onSuccess(account.id);
    } catch (err) {
      const message = err instanceof Error ? err.message : t('onboarding.addFailed');
      setError(message);
      toast('error', message);
    } finally {
      setSubmitting(false);
    }
  }

  return (
    <div>
      <button onClick={onBack} className="-ml-2 mb-6 flex h-9 items-center gap-1.5 rounded-lg px-2 text-sm text-muted-foreground transition-colors hover:bg-surface-hover hover:text-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring">
        <ArrowLeft size={16} />
        {t('onboarding.back')}
      </button>

      <h2 className="text-lg font-semibold text-foreground mb-4">{t('onboarding.credentialsTitle')}</h2>

      {providerHint && PROVIDER_SETUP_GUIDES[providerHint] && (
        <div className="mb-4 rounded-lg border border-primary/20 bg-primary/5 px-4 py-3">
          <div className="flex items-center gap-2 mb-2">
            <Info size={16} className="text-primary shrink-0" />
            <p className="text-sm font-medium text-foreground">{t(`onboarding.${PROVIDER_SETUP_GUIDES[providerHint]!.title}`)}</p>
          </div>
          <ol className="ml-6 list-decimal space-y-1">
            {PROVIDER_SETUP_GUIDES[providerHint]!.steps.map((step, i) => (
              <li key={i} className="text-xs text-muted-foreground">{t(`onboarding.${step}`)}</li>
            ))}
          </ol>
        </div>
      )}

      <div className="space-y-4">
        <Input
          label={t('account.emailAddress')}
          type="email"
          value={email}
          onChange={edit(setEmail)}
          onBlur={handleEmailBlur}
          placeholder={t('onboarding.emailPlaceholder')}
        />

        <Input label={t('account.displayName')} value={displayName} onChange={setDisplayName} placeholder={t('onboarding.displayNamePlaceholder')} />
        <Input label={t('account.password')} value={password} onChange={edit(setPassword)} placeholder={t('onboarding.passwordPlaceholder')} type="password" />

        <button
          onClick={() => setShowAdvanced(!showAdvanced)}
          className="flex items-center gap-2 text-sm text-muted-foreground hover:text-foreground transition-colors mt-2"
        >
          {showAdvanced ? <ChevronDown size={16} /> : <ChevronRight size={16} />}
          {t('onboarding.advancedSettings')}
        </button>

        {showAdvanced && (
          <div className="space-y-4 mt-4">
            <div>
              <h3 className="text-xs font-semibold uppercase tracking-wider text-muted-foreground mb-2">{t('account.imapIncoming')}</h3>
              <div className="grid grid-cols-3 gap-3">
                <div className="col-span-3">
                  <Input label={t('account.host')} value={imapHost} onChange={(v) => { setImapHost(v); setServerTouched(true); setTestResult(null); }} placeholder={t('onboarding.imapHostPlaceholder')} />
                </div>
                <div>
                  <Input
                    label={t('account.port')}
                    type="number"
                    value={imapPort}
                    onChange={(v) => { setImapPort(v); setServerTouched(true); setTestResult(null); }}
                  />
                </div>
                <div className="col-span-2">
                  <Select
                    label={t('account.encryption')}
                    value={imapEncryption}
                    onChange={(v) => { setImapEncryption(v as Encryption); setServerTouched(true); setTestResult(null); }}
                    options={ENCRYPTION_OPTIONS}
                  />
                </div>
              </div>
            </div>

            <div>
              <h3 className="text-xs font-semibold uppercase tracking-wider text-muted-foreground mb-2">{t('account.smtpOutgoing')}</h3>
              <div className="grid grid-cols-3 gap-3">
                <div className="col-span-3">
                  <Input label={t('account.host')} value={smtpHost} onChange={(v) => { setSmtpHost(v); setServerTouched(true); setTestResult(null); }} placeholder={t('onboarding.smtpHostPlaceholder')} />
                </div>
                <div>
                  <Input
                    label={t('account.port')}
                    type="number"
                    value={smtpPort}
                    onChange={(v) => { setSmtpPort(v); setServerTouched(true); setTestResult(null); }}
                  />
                </div>
                <div className="col-span-2">
                  <Select
                    label={t('account.encryption')}
                    value={smtpEncryption}
                    onChange={(v) => { setSmtpEncryption(v as Encryption); setServerTouched(true); setTestResult(null); }}
                    options={ENCRYPTION_OPTIONS}
                  />
                </div>
              </div>
            </div>
          </div>
        )}

        {error && (
          <div className="flex items-center gap-2 text-sm text-destructive bg-destructive/10 border border-destructive/30 rounded-lg px-3 py-2">
            <AlertTriangle size={16} />
            {error}
          </div>
        )}

        {testResult === true && (
          <div className="flex items-center gap-2 text-sm text-success bg-success/10 border border-success/30 rounded-lg px-3 py-2">
            <CheckCircle size={16} />
            {t('account.connectionSuccess')}
          </div>
        )}

        {testResult === false && !error && (
          <div role="alert" className="flex items-center gap-2 text-sm text-destructive bg-destructive/10 border border-destructive/30 rounded-lg px-3 py-2">
            <AlertTriangle size={16} />
            {t('onboarding.testFailed')}
          </div>
        )}

        <div className="flex items-center gap-3 mt-6">
          <Button
            variant="secondary"
            onClick={handleTestConnection}
            disabled={isDisabled || !email || !password || !imapHost}
            loading={testing}
          >
            {t('account.testConnection')}
          </Button>
          <div className="flex-1" />
          <Button
            variant="primary"
            onClick={handleSubmit}
            disabled={isDisabled || !email || !password || !imapHost || !smtpHost}
            loading={submitting}
          >
            {t('account.addAccount')}
          </Button>
        </div>
      </div>
    </div>
  );
}
