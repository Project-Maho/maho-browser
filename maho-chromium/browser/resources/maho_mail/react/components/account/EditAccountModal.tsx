import { useState, useEffect, useRef, useCallback } from 'react';
import { X, AlertTriangle, CheckCircle } from 'lucide-react';
import { useTranslation } from 'react-i18next';
import * as api from '../../api';
import type { Encryption, CreateAccountRequest, AccountResponse, UpdateAccountRequest } from '../../types';
import { Button, Input, Select, Skeleton } from '../ui';
import type { SelectOption } from '../ui';
import { useToast } from '../ui/Toast';

const ENCRYPTION_OPTIONS: SelectOption[] = [
  { value: 'Tls', label: 'TLS' },
  { value: 'StartTls', label: 'STARTTLS' },
  { value: 'None', label: 'None' },
];

interface EditAccountModalProps {
  accountId: string;
  isOpen: boolean;
  onClose: () => void;
  onAccountUpdated: () => void;
}

export function EditAccountModal({ accountId, isOpen, onClose, onAccountUpdated }: EditAccountModalProps) {
  const modalRef = useRef<HTMLDivElement>(null);
  const { toast } = useToast();
  const { t } = useTranslation();

  const [loading, setLoading] = useState(true);
  const [original, setOriginal] = useState<AccountResponse | null>(null);

  const [email, setEmail] = useState('');
  const [displayName, setDisplayName] = useState('');
  const [imapHost, setImapHost] = useState('');
  const [imapPort, setImapPort] = useState(993);
  const [imapEncryption, setImapEncryption] = useState<Encryption>('Tls');
  const [smtpHost, setSmtpHost] = useState('');
  const [smtpPort, setSmtpPort] = useState(587);
  const [smtpEncryption, setSmtpEncryption] = useState<Encryption>('StartTls');
  const [username, setUsername] = useState('');
  const [password, setPassword] = useState('');

  const [testing, setTesting] = useState(false);
  const [testResult, setTestResult] = useState<boolean | null>(null);
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const isOAuth = original?.auth_type.startsWith('oauth2') ?? false;
  const isDisabled = saving || testing || loading;

  const loadAccount = useCallback(async () => {
    try {
      setLoading(true);
      setError(null);
      const account = await api.getAccount(accountId);
      setOriginal(account);
      setEmail(account.email);
      setDisplayName(account.display_name);
      setImapHost(account.imap_host);
      setImapPort(account.imap_port);
      setImapEncryption(account.imap_encryption);
      setSmtpHost(account.smtp_host);
      setSmtpPort(account.smtp_port);
      setSmtpEncryption(account.smtp_encryption);
      setUsername(account.username);
      setPassword('');
    } catch (err) {
      const message = err instanceof Error ? err.message : t('account.loadFailed');
      setError(message);
      toast('error', message);
    } finally {
      setLoading(false);
    }
  }, [accountId, toast]);

  useEffect(() => {
    if (isOpen) {
      loadAccount();
    }
  }, [isOpen, loadAccount]);

  useEffect(() => {
    if (isOpen) {
      requestAnimationFrame(() => {
        const firstEl = modalRef.current?.querySelector<HTMLElement>('button, input, select');
        firstEl?.focus();
      });
    }
  }, [isOpen, loading]);

  if (!isOpen) return null;

  function buildTestRequest(): CreateAccountRequest {
    return {
      email,
      display_name: displayName,
      auth_type: (original?.auth_type ?? 'password') as CreateAccountRequest['auth_type'],
      imap_host: imapHost,
      imap_port: imapPort,
      imap_encryption: imapEncryption,
      smtp_host: smtpHost,
      smtp_port: smtpPort,
      smtp_encryption: smtpEncryption,
      username,
      password: password || undefined,
    };
  }

  function buildUpdateRequest(): UpdateAccountRequest {
    if (!original) return { id: accountId };

    const req: UpdateAccountRequest = { id: accountId };
    if (email !== original.email) req.email = email;
    if (displayName !== original.display_name) req.display_name = displayName;
    if (imapHost !== original.imap_host) req.imap_host = imapHost;
    if (imapPort !== original.imap_port) req.imap_port = imapPort;
    if (imapEncryption !== original.imap_encryption) req.imap_encryption = imapEncryption;
    if (smtpHost !== original.smtp_host) req.smtp_host = smtpHost;
    if (smtpPort !== original.smtp_port) req.smtp_port = smtpPort;
    if (smtpEncryption !== original.smtp_encryption) req.smtp_encryption = smtpEncryption;
    if (username !== original.username) req.username = username;
    if (password) req.password = password;
    return req;
  }

  async function handleTestConnection() {
    try {
      setTesting(true);
      setTestResult(null);
      setError(null);
      const result = await api.testConnection(buildTestRequest());
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

  async function handleSave() {
    if (!email || !imapHost || !smtpHost) {
      setError(t('account.requiredFields'));
      return;
    }
    if (imapPort < 1 || imapPort > 65535 || smtpPort < 1 || smtpPort > 65535) {
      setError(t('account.portRange'));
      return;
    }

    const updateReq = buildUpdateRequest();
    const hasChanges = Object.keys(updateReq).length > 1;
    if (!hasChanges) {
      onClose();
      return;
    }

    try {
      setSaving(true);
      setError(null);
      await api.updateAccount(updateReq);
      toast('success', t('account.updated'));
      onAccountUpdated();
      onClose();
    } catch (err) {
      const message = err instanceof Error ? err.message : t('account.updateFailed');
      setError(message);
      toast('error', message);
    } finally {
      setSaving(false);
    }
  }

  function handleKeyDown(e: React.KeyboardEvent) {
    if (e.key === 'Escape') {
      onClose();
      return;
    }
    if (e.key !== 'Tab') return;
    const focusable = modalRef.current?.querySelectorAll<HTMLElement>(
      'input:not([disabled]), select:not([disabled]), button:not([disabled]), [tabindex]:not([tabindex="-1"])'
    );
    if (!focusable || focusable.length === 0) return;
    const first = focusable[0];
    const last = focusable[focusable.length - 1];
    if (e.shiftKey && document.activeElement === first) {
      e.preventDefault();
      last.focus();
    } else if (!e.shiftKey && document.activeElement === last) {
      e.preventDefault();
      first.focus();
    }
  }

  return (
    <div
      className="fixed inset-0 z-50 flex items-center justify-center bg-black/60 backdrop-blur-sm animate-fade-in"
      role="dialog"
      aria-modal="true"
      aria-labelledby="edit-account-title"
      onClick={onClose}
      onKeyDown={handleKeyDown}
    >
      <div
        ref={modalRef}
        onClick={(e) => e.stopPropagation()}
        className="h-full w-full rounded-none bg-background shadow-2xl md:h-auto md:max-h-[90vh] md:max-w-lg md:rounded-xl animate-scale-in"
      >
        <div className="flex items-center justify-between border-b border-border px-6 py-4">
          <h2 id="edit-account-title" className="text-lg font-semibold text-foreground">
            {t('account.editAccount')}
          </h2>
          <button
            onClick={onClose}
            className="mail-pressable flex h-8 w-8 items-center justify-center rounded-lg text-muted-foreground hover:bg-surface-hover hover:text-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring"
          >
            <X size={20} />
          </button>
        </div>

        <div className="max-h-[70vh] overflow-y-auto p-6">
          {loading ? (
            <div className="space-y-4">
              <Skeleton className="h-9 w-full" />
              <Skeleton className="h-9 w-full" />
              <Skeleton className="h-9 w-3/4" />
              <Skeleton className="h-9 w-full" />
              <Skeleton className="h-9 w-full" />
            </div>
          ) : (
            <div className="space-y-4">
              {isOAuth && (
                <div className="flex items-center gap-2 rounded-lg border border-primary/20 bg-primary/5 px-4 py-3">
                  <span className="rounded bg-primary/10 px-2 py-0.5 text-xs font-medium text-primary">
                    OAuth2
                  </span>
                  <p className="text-xs text-muted-foreground">
                    {t('account.oauthManaged')}
                  </p>
                </div>
              )}

              <Input
                label={t('account.emailAddress')}
                type="email"
                value={email}
                onChange={setEmail}
                disabled={isDisabled}
              />

              <Input
                label={t('account.displayName')}
                value={displayName}
                onChange={setDisplayName}
                disabled={isDisabled}
              />

              <Input
                label={t('account.username')}
                value={username}
                onChange={setUsername}
                disabled={isDisabled}
              />

              {!isOAuth && (
                <Input
                  label={t('account.password')}
                  type="password"
                  value={password}
                  onChange={setPassword}
                  placeholder={t('account.passwordPlaceholder')}
                  disabled={isDisabled}
                />
              )}

              <div>
                <h3 className="text-xs font-semibold uppercase tracking-wider text-muted-foreground mb-2">
                  {t('account.imapIncoming')}
                </h3>
                <div className="grid grid-cols-3 gap-3">
                  <div className="col-span-3">
                    <Input
                      label={t('account.host')}
                      value={imapHost}
                      onChange={setImapHost}
                      disabled={isDisabled}
                    />
                  </div>
                  <div>
                    <Input
                      label={t('account.port')}
                      type="number"
                      value={String(imapPort)}
                      onChange={(v) => setImapPort(parseInt(v, 10) || imapPort)}
                      disabled={isDisabled}
                    />
                  </div>
                  <div className="col-span-2">
                    <Select
                      label={t('account.encryption')}
                      value={imapEncryption}
                      onChange={(v) => setImapEncryption(v as Encryption)}
                      options={ENCRYPTION_OPTIONS}
                      disabled={isDisabled}
                    />
                  </div>
                </div>
              </div>

              <div>
                <h3 className="text-xs font-semibold uppercase tracking-wider text-muted-foreground mb-2">
                  {t('account.smtpOutgoing')}
                </h3>
                <div className="grid grid-cols-3 gap-3">
                  <div className="col-span-3">
                    <Input
                      label={t('account.host')}
                      value={smtpHost}
                      onChange={setSmtpHost}
                      disabled={isDisabled}
                    />
                  </div>
                  <div>
                    <Input
                      label={t('account.port')}
                      type="number"
                      value={String(smtpPort)}
                      onChange={(v) => setSmtpPort(parseInt(v, 10) || smtpPort)}
                      disabled={isDisabled}
                    />
                  </div>
                  <div className="col-span-2">
                    <Select
                      label={t('account.encryption')}
                      value={smtpEncryption}
                      onChange={(v) => setSmtpEncryption(v as Encryption)}
                      options={ENCRYPTION_OPTIONS}
                      disabled={isDisabled}
                    />
                  </div>
                </div>
              </div>

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

              <div className="flex items-center gap-3 mt-6">
                <Button
                  variant="secondary"
                  onClick={handleTestConnection}
                  disabled={isDisabled || !email || !imapHost}
                  loading={testing}
                >
                  {t('account.testConnection')}
                </Button>
                <div className="flex-1" />
                <Button
                  variant="primary"
                  onClick={handleSave}
                  disabled={isDisabled}
                  loading={saving}
                >
                  {t('common.save')}
                </Button>
              </div>
            </div>
          )}
        </div>
      </div>
    </div>
  );
}
