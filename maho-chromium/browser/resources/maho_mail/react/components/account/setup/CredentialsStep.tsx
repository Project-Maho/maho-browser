import { useState } from 'react';
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

const PROVIDER_SETUP_GUIDES: Partial<Record<ProviderId, { title: string; steps: string[] }>> = {
  naver: {
    title: 'Naver Mail Setup',
    steps: [
      'Log in to Naver Mail (mail.naver.com)',
      'Go to Settings → POP3/IMAP',
      'Enable IMAP/SMTP access',
      'Set up 2-step verification in Naver account settings',
      'Generate an app password and use it below',
    ],
  },
  yahoo: {
    title: 'Yahoo Mail Setup',
    steps: [
      'Enable 2-step verification in Yahoo account security',
      'Generate an app password (Account Info → Security → App passwords)',
      'Use the app password below instead of your regular password',
    ],
  },
  icloud: {
    title: 'iCloud Mail Setup',
    steps: [
      'Enable 2-factor authentication for your Apple ID',
      'Go to appleid.apple.com → Sign-In and Security → App-Specific Passwords',
      'Generate an app password and use it below',
    ],
  },
};

interface CredentialsStepProps {
  providerHint: ProviderId | null;
  onSuccess: (accountId: string) => void;
  onBack: () => void;
}

export function CredentialsStep({ providerHint, onSuccess, onBack }: CredentialsStepProps) {
  const { toast } = useToast();

  const [email, setEmail] = useState('');
  const [displayName, setDisplayName] = useState('');
  const [password, setPassword] = useState('');
  const [imapHost, setImapHost] = useState('');
  const [smtpHost, setSmtpHost] = useState('');
  const [imapPort, setImapPort] = useState(993);
  const [smtpPort, setSmtpPort] = useState(587);
  const [imapEncryption, setImapEncryption] = useState<Encryption>('Tls');
  const [smtpEncryption, setSmtpEncryption] = useState<Encryption>('StartTls');
  const [showAdvanced, setShowAdvanced] = useState(providerHint === 'other');
  const [serverTouched, setServerTouched] = useState(false);
  const [testing, setTesting] = useState(false);
  const [testResult, setTestResult] = useState<boolean | null>(null);
  const [submitting, setSubmitting] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const isDisabled = submitting || testing;

  function handleEmailBlur() {
    if (!email.includes('@')) return;
    if (serverTouched) return;
    const config = getSmartDefaults(email);
    if (!config) return;
    setImapHost(config.imapHost);
    setImapPort(config.imapPort);
    setImapEncryption(config.imapEncryption);
    setSmtpHost(config.smtpHost);
    setSmtpPort(config.smtpPort);
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
      imap_port: imapPort,
      imap_encryption: imapEncryption,
      smtp_host: smtpHost,
      smtp_port: smtpPort,
      smtp_encryption: smtpEncryption,
      username: email,
      password: password || undefined,
    };
  }

  async function handleTestConnection() {
    try {
      setTesting(true);
      setTestResult(null);
      setError(null);
      const result = await api.testConnection(buildRequest());
      setTestResult(result);
    } catch (err) {
      setTestResult(false);
      const message = err instanceof Error ? err.message : 'Connection test failed';
      setError(message);
      toast('error', message);
    } finally {
      setTesting(false);
    }
  }

  async function handleSubmit() {
    const emailRegex = /^[^\s@]+@[^\s@]+\.[^\s@]+$/;
    if (!emailRegex.test(email)) { setError('Please enter a valid email address'); return; }
    if (!password || !imapHost || !smtpHost) { setError('Please fill in all required fields'); return; }
    if (imapPort < 1 || imapPort > 65535 || smtpPort < 1 || smtpPort > 65535) { setError('Port numbers must be between 1 and 65535'); return; }
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
      const message = err instanceof Error ? err.message : 'Failed to add account';
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
        Back
      </button>

      <h2 className="text-lg font-semibold text-foreground mb-4">Enter your credentials</h2>

      {providerHint && PROVIDER_SETUP_GUIDES[providerHint] && (
        <div className="mb-4 rounded-lg border border-primary/20 bg-primary/5 px-4 py-3">
          <div className="flex items-center gap-2 mb-2">
            <Info size={16} className="text-primary shrink-0" />
            <p className="text-sm font-medium text-foreground">{PROVIDER_SETUP_GUIDES[providerHint]!.title}</p>
          </div>
          <ol className="ml-6 list-decimal space-y-1">
            {PROVIDER_SETUP_GUIDES[providerHint]!.steps.map((step, i) => (
              <li key={i} className="text-xs text-muted-foreground">{step}</li>
            ))}
          </ol>
        </div>
      )}

      <div className="space-y-4">
        <Input
          label="Email address"
          type="email"
          value={email}
          onChange={setEmail}
          onBlur={handleEmailBlur}
          placeholder="you@example.com"
        />

        <Input label="Display name" value={displayName} onChange={setDisplayName} placeholder="Your Name" />
        <Input label="Password" value={password} onChange={setPassword} placeholder="App password or account password" type="password" />

        <button
          onClick={() => setShowAdvanced(!showAdvanced)}
          className="flex items-center gap-2 text-sm text-muted-foreground hover:text-foreground transition-colors mt-2"
        >
          {showAdvanced ? <ChevronDown size={16} /> : <ChevronRight size={16} />}
          Advanced server settings
        </button>

        {showAdvanced && (
          <div className="space-y-4 mt-4">
            <div>
              <h3 className="text-xs font-semibold uppercase tracking-wider text-muted-foreground mb-2">IMAP (Incoming)</h3>
              <div className="grid grid-cols-3 gap-3">
                <div className="col-span-3">
                  <Input label="Host" value={imapHost} onChange={(v) => { setImapHost(v); setServerTouched(true); }} placeholder="imap.example.com" />
                </div>
                <div>
                  <Input
                    label="Port"
                    type="number"
                    value={String(imapPort)}
                    onChange={(v) => { setImapPort(parseInt(v, 10) || imapPort); setServerTouched(true); }}
                  />
                </div>
                <div className="col-span-2">
                  <Select
                    label="Encryption"
                    value={imapEncryption}
                    onChange={(v) => { setImapEncryption(v as Encryption); setServerTouched(true); }}
                    options={ENCRYPTION_OPTIONS}
                  />
                </div>
              </div>
            </div>

            <div>
              <h3 className="text-xs font-semibold uppercase tracking-wider text-muted-foreground mb-2">SMTP (Outgoing)</h3>
              <div className="grid grid-cols-3 gap-3">
                <div className="col-span-3">
                  <Input label="Host" value={smtpHost} onChange={(v) => { setSmtpHost(v); setServerTouched(true); }} placeholder="smtp.example.com" />
                </div>
                <div>
                  <Input
                    label="Port"
                    type="number"
                    value={String(smtpPort)}
                    onChange={(v) => { setSmtpPort(parseInt(v, 10) || smtpPort); setServerTouched(true); }}
                  />
                </div>
                <div className="col-span-2">
                  <Select
                    label="Encryption"
                    value={smtpEncryption}
                    onChange={(v) => { setSmtpEncryption(v as Encryption); setServerTouched(true); }}
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
            Connection successful!
          </div>
        )}

        <div className="flex items-center gap-3 mt-6">
          <Button
            variant="secondary"
            onClick={handleTestConnection}
            disabled={isDisabled || !email || !password || !imapHost}
            loading={testing}
          >
            Test Connection
          </Button>
          <div className="flex-1" />
          <Button
            variant="primary"
            onClick={handleSubmit}
            disabled={isDisabled || !email || !password || !imapHost || !smtpHost}
            loading={submitting}
          >
            Add Account
          </Button>
        </div>
      </div>
    </div>
  );
}
