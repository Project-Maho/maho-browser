// Copyright 2026 Maho Browser. All rights reserved.

// allow: SIZE_OK — one cohesive settings section (the Mail accounts pane: list,
// add via OAuth/IMAP, remove, reconnect). Sibling panes live as 200-500 LOC
// blocks inside domain_panes.tsx; this standalone file is already the more
// modular split and maps 1:1 to a single contentKind='mail' responsibility.

import React, {useCallback, useEffect, useRef, useState} from 'react';
import {
  ArrowLeft,
  Check,
  Cloud,
  Loader2,
  Mail,
  Plus,
  RefreshCw,
  Server,
  Trash2,
} from 'lucide-react';

import {Alert, AlertDescription} from '@ui/alert';
import {Button} from '@ui/button';
import {Input} from '@ui/input';
import {cn} from '@lib/utils';
import type {PaneDefinition} from '../models.js';
import type {PageHandlerRemote} from '../maho_settings.mojom-webui.js';
import {PaneShell, SectionCard} from './domain_panes.js';
import {classifyMailSettingsFailure, createMailSettingsContentState, type MailSettingsState} from './mail_settings_state.js';
import type {MahoSettingsStore} from './store.js';

// The connected-account rows Settings manages. `id` and `email` are the only
// fields the mail helper guarantees; the rest enrich the management view when
// present and are parsed defensively.
interface MailAccount {
  readonly id: string;
  readonly email: string;
  readonly displayName: string;
  readonly provider: string;
  readonly status: string;
}

type View = 'list' | 'picker' | 'oauth' | 'imap';
type OAuthProvider = 'gmail' | 'outlook';
type Feedback = {readonly kind: 'error' | 'success'; readonly message: string} | null;


const ADD_OPTIONS = [
  {view: 'oauth', provider: 'gmail', title: 'Gmail', body: 'Connect securely with Google', icon: Mail},
  {view: 'oauth', provider: 'outlook', title: 'Outlook', body: 'Connect Microsoft mail', icon: Cloud},
  {view: 'imap', title: 'IMAP / other', body: 'Use an email and app password', icon: Server},
] as const;

function parseMailAccounts(resultJson: string): MailAccount[] | null {
  let parsed: unknown;
  try {
    parsed = JSON.parse(resultJson);
  } catch {
    return null;
  }
  if (!Array.isArray(parsed)) {
    return null;
  }
  const accounts: MailAccount[] = [];
  for (const value of parsed) {
    if (typeof value !== 'object' || value === null ||
        !('id' in value) || typeof value.id !== 'string' ||
        !('email' in value) || typeof value.email !== 'string') {
      return null;
    }
    const record = value as Record<string, unknown>;
    accounts.push({
      id: record.id as string,
      email: record.email as string,
      displayName: typeof record.display_name === 'string' ? record.display_name : '',
      provider: typeof record.provider === 'string' ? record.provider : '',
      status: typeof record.status === 'string' ? record.status : '',
    });
  }
  return accounts;
}

interface OAuthStartResponse {
  readonly authUrl: string;
  readonly state: string | null;
}

function parseOAuthStartResponse(resultJson: string): OAuthStartResponse | null {
  let parsed: unknown;
  try {
    parsed = JSON.parse(resultJson);
  } catch (error) {
    if (error instanceof SyntaxError) {
      return null;
    }
    throw error;
  }
  if (typeof parsed !== 'object' || parsed === null ||
      !('auth_url' in parsed) || typeof parsed.auth_url !== 'string') {
    return null;
  }

  const authUrl = parseHttpsUrl(parsed.auth_url);
  if (authUrl === null) {
    return null;
  }

  const state = 'state' in parsed && typeof parsed.state === 'string'
    ? parsed.state
    : null;
  return {authUrl, state};
}

function parseHttpsUrl(candidate: string): string | null {
  try {
    const url = new URL(candidate);
    return url.protocol === 'https:' ? url.href : null;
  } catch (error) {
    if (error instanceof TypeError) {
      return null;
    }
    throw error;
  }
}

export function MailAccountsPane(
    {pane, store}: {pane: PaneDefinition; store: MahoSettingsStore}) {
  const handler: PageHandlerRemote = store.getHandler();
  const mountedRef = useRef(true);
  const [accounts, setAccounts] = useState<MailAccount[]>([]);
  const [modelState, setModelState] = useState<MailSettingsState<MailAccount[]>>({status: 'loading'});
  const [view, setView] = useState<View>('list');
  const [oauthProvider, setOAuthProvider] = useState<OAuthProvider>('gmail');
  const [busy, setBusy] = useState(false);
  const [feedback, setFeedback] = useState<Feedback>(null);
  const [email, setEmail] = useState('');
  const [password, setPassword] = useState('');
  const [imapHost, setImapHost] = useState('');
  const preOAuthIds = useRef<ReadonlySet<string>>(new Set());
  const activeOAuthState = useRef<string | null>(null);
  const oauthAttempt = useRef(0);

  useEffect(() => {
    return () => {
      mountedRef.current = false;
    };
  }, []);

  const loadAccounts = useCallback(async (): Promise<MailAccount[] | null> => {
    setModelState({status: 'loading'});
    try {
      const {ok, resultJson} = await handler.mailListAccounts();
      if (!mountedRef.current) {
        return null;
      }
      if (!ok) {
        setModelState(classifyMailSettingsFailure(resultJson, 'Could not load mail accounts.'));
        return null;
      }
      const parsed = parseMailAccounts(resultJson);
      if (parsed === null) {
        setModelState({status: 'error', message: 'The mail service returned invalid account data.'});
        return null;
      }
      setAccounts(parsed);
      setModelState(createMailSettingsContentState(parsed));

      if (view === 'oauth' && activeOAuthState.current) {
        const connected = parsed.some(account => !preOAuthIds.current.has(account.id));
        if (connected) {
          activeOAuthState.current = null;
          setFeedback({kind: 'success', message: 'Mail account connected.'});
          setView('list');
        }
      }
      return parsed;
    } catch {
      if (mountedRef.current) {
        setModelState({status: 'error', message: 'Could not load mail accounts.'});
      }
      return null;
    }
  }, [handler, view]);

  useEffect(() => {
    void loadAccounts();
  }, [loadAccounts]);

  // Settings CallbackRouter for accounts mutated event
  useEffect(() => {
    const router = store.getCallbackRouter();
    const listenerId = router.onMailAccountsChanged.addListener(() => {
      void loadAccounts();
    });
    return () => {
      router.removeListener(listenerId);
    };
  }, [store, loadAccounts]);

  // Focus/visibility reconciliation
  useEffect(() => {
    const handleVisibilityOrFocus = () => {
      if (document.visibilityState === 'visible' || document.hasFocus()) {
        void loadAccounts();
      }
    };
    window.addEventListener('visibilitychange', handleVisibilityOrFocus);
    window.addEventListener('focus', handleVisibilityOrFocus);
    return () => {
      window.removeEventListener('visibilitychange', handleVisibilityOrFocus);
      window.removeEventListener('focus', handleVisibilityOrFocus);
    };
  }, [loadAccounts]);

  // Clean up OAuth on unmount
  useEffect(() => {
    return () => {
      if (activeOAuthState.current) {
        void handler.mailOAuthCancel(activeOAuthState.current);
      }
    };
  }, [handler]);

  const cancelOAuth = useCallback(async () => {
    if (activeOAuthState.current) {
      const state = activeOAuthState.current;
      activeOAuthState.current = null;
      await handler.mailOAuthCancel(state);
    }
    setView('list');
  }, [handler]);

  const removeAccount = async (account: MailAccount) => {
    if (busy || !window.confirm(`Remove ${account.email} from Maho? This deletes its local mail data and saved credentials, including any unsynced local changes.`)) return;
    setBusy(true);
    try {
      const {ok, resultJson} = await handler.mailDeleteAccount(account.id);
      if (!ok) {
        setFeedback({kind: 'error', message: resultJson || 'Could not remove this account.'});
        return;
      }
      setFeedback({kind: 'success', message: `Removed ${account.email}.`});
      await loadAccounts();
    } catch {
      setFeedback({kind: 'error', message: 'Could not remove this account.'});
    } finally {
      setBusy(false);
    }
  };

  const reconnectAccount = async (account: MailAccount) => {
    try {
      const {ok, resultJson} = await handler.mailReconnectAccount(account.id);
      if (!ok) {
        setFeedback({kind: 'error', message: resultJson || 'Could not reconnect this account.'});
        return;
      }
      setFeedback({kind: 'success', message: `Reconnecting ${account.email}…`});
      await loadAccounts();
    } catch {
      setFeedback({kind: 'error', message: 'Could not reconnect this account.'});
    }
  };

  const startOAuth = async (provider: OAuthProvider) => {
    const attempt = ++oauthAttempt.current;
    setBusy(true);
    setFeedback(null);
    setOAuthProvider(provider);
    preOAuthIds.current = new Set(accounts.map(account => account.id));
    try {
      const {ok, resultJson} = await handler.mailBeginOAuth(provider);
      if (!ok) {
        setFeedback({kind: 'error', message: resultJson || 'OAuth is unavailable. Try IMAP instead.'});
        return;
      }
      const oauthStart = parseOAuthStartResponse(resultJson);
      if (oauthStart === null) {
        setFeedback({kind: 'error', message: 'The mail service returned an invalid authorization link.'});
        return;
      }
      if (attempt !== oauthAttempt.current || !mountedRef.current) {
        if (oauthStart.state) await handler.mailOAuthCancel(oauthStart.state);
        return;
      }
      activeOAuthState.current = oauthStart.state;
      window.open(oauthStart.authUrl, '_blank', 'noopener,noreferrer');
      setView('oauth');
    } catch {
      setFeedback({kind: 'error', message: 'OAuth is unavailable. Try IMAP instead.'});
    } finally {
      setBusy(false);
    }
  };

  const connectImap = async () => {
    const username = email.trim();
    const host = imapHost.trim();
    if (!username || !password || !host) {
      setFeedback({kind: 'error', message: 'Email, app password, and IMAP host are required.'});
      return;
    }
    setBusy(true);
    setFeedback(null);
    const smtpHost = host.startsWith('imap.') ? `smtp.${host.slice(5)}` : host;
    const requestJson = JSON.stringify({email: username, display_name: username, auth_type: 'password', imap_host: host, imap_port: 993, imap_encryption: 'Tls', smtp_host: smtpHost, smtp_port: 587, smtp_encryption: 'StartTls', username, password});
    try {
      const probe = await handler.mailTestConnection(JSON.stringify({
        imap_host: host, imap_port: 993, imap_encryption: 'Tls',
        username, auth_type: 'password', password,
      }));
      if (!probe.ok) {
        setFeedback({kind: 'error', message: probe.resultJson || 'Could not connect to the IMAP server.'});
        return;
      }
      const {ok, resultJson} = await handler.mailAddAccount(requestJson);
      if (!ok) {
        setFeedback({kind: 'error', message: resultJson || 'Could not add this mail account.'});
        return;
      }
      setEmail('');
      setPassword('');
      setImapHost('');
      setFeedback({kind: 'success', message: 'Mail account added. IMAP connection verified; outgoing mail has not been tested.'});
      setView('list');
      await loadAccounts();
    } catch {
      setFeedback({kind: 'error', message: 'Could not connect or add this mail account. Try again.'});
    } finally {
      setBusy(false);
    }
  };

  return (
    <PaneShell pane={pane}>
      <div data-mail-settings-state={modelState.status}>
      {feedback ? (
        <Alert className="mb-6" variant={feedback.kind === 'error' ? 'destructive' : 'success'}>
          <AlertDescription>{feedback.message}</AlertDescription>
        </Alert>
      ) : null}

      <SectionCard
        title="Connected accounts"
        description="Manage the mail accounts connected to Maho. Add new accounts or remove ones you no longer use."
        action={view === 'list' ? (
          <Button type="button" size="sm" variant="outline" onClick={() => { setFeedback(null); setView('picker'); }}>
            <Plus aria-hidden="true" />
            Add account
          </Button>
        ) : null}>
        {modelState.status === 'loading' ? (
          <div className="flex items-center gap-3 border-b border-border px-6 py-6 text-sm text-muted-foreground">
            <Loader2 className="size-4 animate-spin" aria-hidden="true" />
            Loading mail accounts…
          </div>
        ) : null}

        {modelState.status === 'unavailable' || modelState.status === 'error' ? (
          <div className="flex flex-wrap items-center justify-between gap-3 border-b border-border px-6 py-4 text-sm text-muted-foreground">
            <span>{modelState.message}</span>
            <Button type="button" size="sm" variant="outline" onClick={() => { void loadAccounts(); }}>Retry</Button>
          </div>
        ) : null}

        {modelState.status === 'empty' ? (
          <div className="border-b border-border px-6 py-4 text-sm leading-6 text-muted-foreground">
            No mail accounts connected yet.
          </div>
        ) : null}

        {accounts.map(account => {
          const needsAttention = account.status !== '' && account.status !== 'connected' && account.status !== 'ok';
          const secondary = [account.displayName, account.provider].filter(Boolean).join(' · ');
          return (
            <div key={account.id} className="flex items-center gap-3 border-b border-border px-6 py-4">
              <span className={cn(
                'flex size-8 shrink-0 items-center justify-center rounded-full',
                needsAttention ? 'bg-destructive/10 text-destructive' : 'bg-success/10 text-success')}>
                {needsAttention ? <RefreshCw className="size-4" aria-hidden="true" /> : <Check className="size-4" aria-hidden="true" />}
              </span>
              <div className="min-w-0 flex-1">
                <p className="truncate text-sm font-medium text-foreground">{account.email}</p>
                {secondary ? <p className="mt-0.5 truncate text-xs text-muted-foreground">{secondary}</p> : null}
              </div>
              {needsAttention ? (
                <Button type="button" size="sm" variant="outline" aria-label={`Reconnect ${account.email}`} onClick={() => { void reconnectAccount(account); }}>
                  <RefreshCw aria-hidden="true" />
                  Reconnect
                </Button>
              ) : null}
              <Button type="button" size="icon" variant="ghost" className="size-8 text-muted-foreground hover:text-destructive" disabled={busy} aria-label={`Remove ${account.email}`} onClick={() => { void removeAccount(account); }}>
                <Trash2 aria-hidden="true" />
              </Button>
            </div>
          );
        })}

        {view === 'picker' ? (
          <div className="border-b border-border px-6 py-5">
            <div className="mb-4 grid gap-3 sm:grid-cols-3">
              {ADD_OPTIONS.map(option => {
                const Icon = option.icon;
                return (
                  <button
                    key={option.title}
                    type="button"
                    disabled={busy}
                    className="group flex min-h-28 flex-col items-start justify-between rounded-xl border border-border bg-background/60 p-4 text-left shadow-sm transition duration-150 hover:-translate-y-0.5 hover:bg-muted/50 hover:shadow-md focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring disabled:opacity-50"
                    onClick={() => {
                      if (option.view === 'oauth') {
                        void startOAuth(option.provider);
                      } else {
                        setFeedback(null);
                        setView('imap');
                      }
                    }}>
                    <span className="flex size-9 items-center justify-center rounded-lg border border-border bg-card shadow-sm">
                      <Icon className="size-4 text-muted-foreground transition-colors group-hover:text-foreground" aria-hidden="true" />
                    </span>
                    <span>
                      <span className="block text-sm font-semibold text-foreground">{option.title}</span>
                      <span className="mt-1 block text-xs text-muted-foreground">{option.body}</span>
                    </span>
                  </button>
                );
              })}
            </div>
            <Button type="button" size="sm" variant="ghost" className="px-0 text-muted-foreground hover:bg-transparent hover:text-foreground" onClick={() => { ++oauthAttempt.current; setView('list'); }}>
              <ArrowLeft aria-hidden="true" />
              Cancel
            </Button>
          </div>
        ) : null}

        {view === 'oauth' ? (
          <div className="flex flex-col items-center gap-4 border-b border-border px-6 py-8 text-center">
            <Loader2 className="size-8 animate-spin text-muted-foreground" aria-hidden="true" />
            <div>
              <p className="text-sm font-semibold text-foreground">Waiting for authorization…</p>
              <p className="mt-1 text-xs text-muted-foreground">
                Finish signing in to {oauthProvider === 'gmail' ? 'Google' : 'Microsoft'} in the new tab.
              </p>
            </div>
            <Button type="button" size="sm" variant="outline" onClick={cancelOAuth}>Cancel</Button>
          </div>
        ) : null}

        {view === 'imap' ? (
          <div className="flex flex-col gap-4 border-b border-border px-6 py-5">
            <MailField label="Email" type="email" value={email} placeholder="you@example.com" onChange={setEmail} />
            <MailField label="App password" type="password" value={password} placeholder="App-specific password" onChange={setPassword} />
            <MailField label="IMAP host" value={imapHost} placeholder="imap.example.com" onChange={setImapHost} />
            <div className="flex items-center gap-2">
              <Button type="button" disabled={busy} onClick={() => { void connectImap(); }}>
                {busy ? 'Testing connection…' : 'Connect account'}
              </Button>
              <Button type="button" variant="ghost" disabled={busy} onClick={() => { setPassword(''); setView('picker'); }}>
                <ArrowLeft aria-hidden="true" />
                Back
              </Button>
            </div>
          </div>
        ) : null}
      </SectionCard>
      </div>
    </PaneShell>
  );
}

function MailField(
    {label, type = 'text', value, placeholder, onChange}: {
      readonly label: string;
      readonly type?: string;
      readonly value: string;
      readonly placeholder: string;
      readonly onChange: (value: string) => void;
    }) {
  const id = `mail-account-${label.toLowerCase().replaceAll(' ', '-')}`;
  return (
    <div className="space-y-1.5">
      <label htmlFor={id} className="text-sm font-medium text-foreground">{label}</label>
      <Input
        id={id}
        type={type}
        value={value}
        placeholder={placeholder}
        className={cn('h-10 bg-background/70', type === 'password' && 'tracking-wide')}
        onChange={event => onChange(event.target.value)}
      />
    </div>
  );
}
