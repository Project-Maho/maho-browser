import React, {useCallback, useEffect, useRef, useState, useSyncExternalStore} from 'react';

import {Alert, AlertDescription, AlertTitle} from '@ui/alert';
import {Badge} from '@ui/badge';
import {Button} from '@ui/button';
import {ShortcutsPane} from './shortcuts/index.js';
import {PasswordsPane} from './passwords_settings.js';
import {SavedPasswordsPane} from './passwords_library.js';
import {MailAccountsPane} from './mail_accounts.js';
import {MailPane} from './mail_overview.js';
import {MailSignaturesPane} from './mail_signatures.js';
import {MailRulesPane} from './mail_rules.js';
import {MailCalendarPane} from './mail_calendar.js';
import {MailBehaviorPane} from './mail_behavior.js';
import {MailSecurityPane} from './mail_security.js';
import {AiDevelopersPane} from './ai_developers.js';
import {McpAgentGuidanceSettingsSection} from './mcp_agent_guidance_settings_section.js';
import {ProfilesEditor} from './profiles_editor.js';
import {openCheckoutOverlay} from '../../maho_common/react/lemonsqueezy.js';
import {PlanPicker} from '../../maho_common/react/ui/plan-picker.js';
import type {PlanTier} from '../../maho_common/react/ui/plan-picker.js';
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
} from '@ui/dialog';
import {Input} from '@ui/input';
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from '@ui/select';
import {Switch} from '@ui/switch';
import {AlertCircle, ArrowUpRight, Sparkles, Download, ExternalLink, Check, Loader2, Zap, Puzzle, ShieldOff, Globe, Database, Settings, Trash2, Key, RefreshCw} from '@icons/lucide';
import {cn} from '@lib/utils';
import {SETTING_METADATA} from '../schema/setting_schema.js';
import {PRIVACY_SETTING_KEYS} from '../schema/panes.js';
import type {AccountStatus, BillingInfo, PageHandlerRemote} from '../maho_settings.mojom-webui.js';
import {scopeApplicabilityLabel} from '../models.js';
import {
  AIConnectionState,
  type AIModelsSettings,
  type AIProviderDescriptor,
  type AITaskModelRoute,
  type AITaskReplacement,
  type AISettings,
  type ATCRule,
  AutofillAddress,
  AutofillPayment,
  ContentBlockerStats,
  FilterListInfo,
  SettingValue,
  SpaceBasicInfo,
  SyncDeviceInfo,
  SyncKeyInfo,
  SyncStatus,
} from '../mojo.js';
import type {PaneDefinition} from '../models.js';
import type {MahoSettingsStore, SettingsState} from './store.js';

type LoadState<T> =
    | {status: 'loading'}
    | {status: 'error'; message: string}
    | {data: T; status: 'loaded'};


interface SyncPaneData {
  devicesError: string | null;
  devicesStatus: 'idle' | 'loaded';
  devices: SyncDeviceInfo[];
  status: SyncStatus;
}

interface AutofillPaneData {
  addresses: AutofillAddress[];
  payments: AutofillPayment[];
}

interface ContentBlockerPaneData {
  lists: FilterListInfo[];
  stats: ContentBlockerStats;
}

type ContentBlockerMutationAction =
    'mode' | 'update' | 'rebuild' | 'add' | 'toggle' | 'remove';

interface ContentBlockerMutationContext {
  readonly action: ContentBlockerMutationAction;
  readonly targetId: string | null;
  readonly targetLabel: string | null;
}

interface ContentBlockerActionError extends ContentBlockerMutationContext {
  readonly message: string;
}

function createContentBlockerActionError(
    context: ContentBlockerMutationContext): ContentBlockerActionError {
  let message: string;
  switch (context.action) {
    case 'mode':
      message = 'Failed to change content blocking mode.';
      break;
    case 'update':
      message = 'Failed to check for filter updates.';
      break;
    case 'rebuild':
      message = 'Failed to rebuild the content blocker engine.';
      break;
    case 'add':
      message = 'Failed to add filter list. The URL might be invalid.';
      break;
    case 'toggle':
      message = `Failed to update ${context.targetLabel || 'filter list'}.`;
      break;
    case 'remove':
      message = `Failed to remove ${context.targetLabel || 'filter list'}.`;
      break;
  }
  return {...context, message};
}

function createLoadingState<T>(): LoadState<T> {
  return {status: 'loading'};
}

function createErrorState<T>(message: string): LoadState<T> {
  return {message, status: 'error'};
}

function createLoadedState<T>(data: T): LoadState<T> {
  return {data, status: 'loaded'};
}

function useMountedRef(): React.MutableRefObject<boolean> {
  const mountedRef = useRef(true);

  useEffect(() => {
    return () => {
      mountedRef.current = false;
    };
  }, []);

  return mountedRef;
}

function createTransientId(prefix: string): string {
  const randomUUID = globalThis.crypto &&
          typeof globalThis.crypto.randomUUID === 'function' ?
      globalThis.crypto.randomUUID.bind(globalThis.crypto) :
      null;
  if (randomUUID) {
    return `${prefix}-${randomUUID()}`;
  }

  return `${prefix}-${Date.now()}-${Math.random().toString(16).slice(2)}`;
}


const USD_FORMATTER = new Intl.NumberFormat(undefined, {
  style: 'currency',
  currency: 'USD',
  minimumFractionDigits: 2,
  maximumFractionDigits: 2,
});

function formatPlanLabel(tier: AccountStatus['tier'] | BillingInfo['tier']): string {
  if (!tier) {
    return 'Free';
  }

  return tier.charAt(0).toUpperCase() + tier.slice(1);
}

function formatBillingDate(timestamp: bigint | number): string | null {
  const seconds = typeof timestamp === 'bigint' ? Number(timestamp) : timestamp;
  if (!seconds) {
    return null;
  }

  return new Date(seconds * 1000).toLocaleDateString(undefined, {
    month: 'short',
    day: 'numeric',
    year: 'numeric',
  });
}

function formatUsd(amount: number): string {
  return USD_FORMATTER.format(Math.max(0, amount));
}

interface ProgressProps extends React.HTMLAttributes<HTMLDivElement> {
  value?: number;
}

const Progress = React.forwardRef<HTMLDivElement, ProgressProps>(
  ({ className, value, ...props }, ref) => {
    return (
      <div
        ref={ref}
        role="progressbar"
        aria-valuemin={0}
        aria-valuemax={100}
        aria-valuenow={value}
        className={cn(
          "relative h-2.5 w-full overflow-hidden rounded-full bg-muted/80 backdrop-blur-sm border border-border/20 shadow-inner",
          className
        )}
        {...props}
      >
        <div
          className="h-full w-full flex-1 rounded-full bg-gradient-to-r from-success/80 to-success transition-all duration-500 ease-out shadow-[0_0_12px_rgba(34,197,94,0.3)]"
          style={{ transform: `translateX(-${100 - (value || 0)}%)` }}
        />
      </div>
    );
  }
);
Progress.displayName = "Progress";

function useSettingsStoreState(store: MahoSettingsStore) {
  return useSyncExternalStore(
      listener => store.subscribe(listener), () => store.getSnapshot());
}

const STANDALONE_MESSAGE_CLASS = 'rounded-xl border border-border/70 bg-background/20 px-6 py-4 text-sm leading-6 text-muted-foreground';
const RETRY_ROW_CLASS =
    'grid gap-4 rounded-xl border border-border/70 bg-background/20 px-4 py-4 sm:grid-cols-[minmax(0,1fr)_minmax(270px,344px)] sm:px-6 sm:items-start';
const SETTING_ROW_CLASS =
    'grid gap-4 border-t border-border px-4 py-4 sm:grid-cols-[minmax(0,1fr)_minmax(270px,344px)] sm:px-6 sm:items-start';
const SETTING_COPY_CLASS = 'min-w-0';
const SETTING_TITLE_CLASS = 'text-sm font-medium text-foreground';
export const SETTING_DESCRIPTION_CLASS = 'mt-1 text-xs leading-5 text-muted-foreground';
const EMPTY_DESCRIPTION_CLASS = 'border-b border-border px-6 py-4 text-sm leading-6 text-muted-foreground';
const SETTING_CONTROL_CLASS = 'flex w-full items-center justify-start gap-2 sm:justify-end';
export const SETTING_ACTIONS_CLASS = 'flex w-full flex-wrap items-center gap-2 sm:justify-end';
export const SETTING_CONTROL_STACKED_CLASS = 'flex-col items-stretch sm:justify-start';
export const SETTING_CONTROL_STACK_CLASS = 'flex w-full flex-col gap-2';
const INPUT_SHELL_CLASS = 'w-full min-w-0';
const INPUT_FIELD_CLASS = 'bg-background/70';
const SELECT_SHELL_CLASS = 'w-full min-w-0';
const SELECT_TRIGGER_CLASS = 'bg-background/70';
const ACTION_BUTTON_CLASS =
    'h-9 min-w-fit rounded-lg border-border bg-background/70 px-4 text-sm font-medium text-foreground shadow-sm hover:bg-surface-hover';
export const INLINE_STATUS_CLASS = 'inline-flex min-h-9 items-center rounded-lg border border-border bg-background/70 px-3 text-sm font-medium text-foreground';
const ICON_CLASS = 'inline-block size-4 shrink-0 align-middle';

interface PaneShellProps {
  pane: PaneDefinition;
  children: React.ReactNode;
}

export function PaneShell({pane, children}: PaneShellProps) {
  return (
    <section
      className="mx-auto w-full max-w-3xl px-4 py-6 sm:px-6 sm:py-8"
      data-pane={pane.key}
      id={`section-${pane.key}`}
    >
      <div className="mb-6 flex flex-wrap items-baseline gap-x-3 gap-y-1">
        <h2 className="text-2xl font-semibold tracking-tight">{pane.title}</h2>
        {scopeApplicabilityLabel(pane.scope) ? (
          <span className="text-xs font-medium text-muted-foreground">
            {scopeApplicabilityLabel(pane.scope)}
          </span>
        ) : null}
      </div>
      {children}
    </section>
  );
}

interface SectionCardProps {
  title: string;
  description?: string;
  action?: React.ReactNode;
  children: React.ReactNode;
  className?: string;
  padded?: boolean;
  headerBanner?: React.ReactNode;
}

export function SectionCard({title, description, action, children, className, padded, headerBanner}: SectionCardProps) {
  return (
    <div className={cn("space-y-3", className)}>
      <div className="flex flex-wrap items-start justify-between gap-4 px-1">
        <div className="space-y-1">
          <h3 className="text-base font-semibold tracking-tight">{title}</h3>
          {description ? <p className="text-sm text-muted-foreground">{description}</p> : null}
        </div>
        {action || null}
      </div>
      {(headerBanner || children) ? (
        <div className="overflow-hidden rounded-xl border border-border/60 glass">
          {headerBanner || null}
          <div className={cn(padded ? "p-6" : "[&>section:first-child]:border-t-0")}>
            {children}
          </div>
        </div>
      ) : null}
    </div>
  );
}

export function DomainPaneContent(
    {pane, settings, store}: {
      pane: PaneDefinition;
      settings: SettingValue[];
      store: MahoSettingsStore;
    }) {
  switch (pane.contentKind) {
    case 'account':
      return <AccountPane pane={pane} store={store} />;
    case 'profiles':
      return <ProfilesPane pane={pane} store={store} />;
    case 'passwords':
      return <PasswordsPane pane={pane} settings={settings} store={store} />;
    case 'saved-passwords':
      return <SavedPasswordsPane pane={pane} settings={settings} store={store} />;
    case 'mail':
      return <MailAccountsPane pane={pane} store={store} />;
    case 'mail-overview':
      return <MailPane pane={pane} settings={settings} store={store} />;
    case 'autofill':
      return <AutofillPane pane={pane} store={store} />;
    case 'shortcuts':
      return <ShortcutsPane store={store} />;
    case 'ai':
      return <AiPane pane={pane} store={store} />;
    case 'ai-developers':
      return <AiDevelopersPane pane={pane} />;
    case 'billing':
      return <BillingPane pane={pane} store={store} />;
    case 'content-blocker':
      return <ContentBlockerPane pane={pane} settings={settings} store={store} />;
    case 'maho-mini':
      return <MahoMiniPane pane={pane} store={store} />;
    case 'mail-signatures':
      return <MailSignaturesPane pane={pane} store={store} />;
    case 'mail-rules':
      return <MailRulesPane pane={pane} store={store} />;
    case 'mail-calendar':
      return <MailCalendarPane pane={pane} store={store} />;
    case 'mail-behavior':
      return <MailBehaviorPane pane={pane} store={store} />;
    case 'mail-security':
      return <MailSecurityPane pane={pane} store={store} />;
    default:
      return (
        <PaneShell pane={pane}>
          <p className={STANDALONE_MESSAGE_CLASS}>This pane is not available.</p>
        </PaneShell>
      );
  }
}

function AccountPane({pane, store}: {pane: PaneDefinition; store: MahoSettingsStore}) {
  const mountedRef = useMountedRef();
  const state = useSettingsStoreState(store);
  const account = state.accountStatus;
  const error = state.accountError;
  const loading = state.accountLoading;
  const [email, setEmail] = useState('');
  const [password, setPassword] = useState('');
  const [submitting, setSubmitting] = useState(false);

  useEffect(() => {
    void store.refreshAccountStatus();
  }, [store]);

  let accountSection: React.ReactNode;
  if (loading && !submitting && account === null) {
    accountSection = <PaneLoading message="Loading account…" />;
  } else if (!account?.signedIn) {
    accountSection = (
      <SectionCard title="Sign in to Maho" description="Access your subscription, sync, and billing." className="max-w-md" padded>
          <div className="mb-4 flex size-10 items-center justify-center rounded-full bg-primary/10 text-primary">
            <svg aria-hidden="true" className="size-4" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
              <path d="M15 3h4a2 2 0 0 1 2 2v14a2 2 0 0 1-2 2h-4" />
              <polyline points="10 17 15 12 10 7" />
              <line x1="15" x2="3" y1="12" y2="12" />
            </svg>
          </div>
          <form
            className="grid gap-4"
            onSubmit={async (event) => {
              event.preventDefault();
              if (submitting) {
                return;
              }

              setSubmitting(true);
              const result = await store.signIn(email.trim(), password);
              if (!mountedRef.current) {
                return;
              }

              setSubmitting(false);
              if (result.success) {
                setEmail('');
                setPassword('');
              }
            }}>
            <div className="grid gap-2">
              <label className="text-sm font-medium leading-none" htmlFor="account-email">Email</label>
              <Input
                autoComplete="email"
                disabled={submitting}
                id="account-email"
                required
                type="email"
                value={email}
                onChange={event => setEmail(event.currentTarget.value)}
              />
            </div>
            <div className="grid gap-2">
              <label className="text-sm font-medium leading-none" htmlFor="account-password">Password</label>
              <Input
                autoComplete="current-password"
                disabled={submitting}
                id="account-password"
                required
                type="password"
                value={password}
                onChange={event => setPassword(event.currentTarget.value)}
              />
            </div>
            {error ? (
              <Alert className="border-destructive/60 bg-destructive/10 text-destructive shadow-sm" variant="destructive">
                <AlertCircle className="size-4" />
                <AlertTitle>Sign in failed</AlertTitle>
                <AlertDescription>{error}</AlertDescription>
              </Alert>
            ) : null}
            <Button disabled={submitting || !email.trim() || !password} type="submit">
              {submitting ? 'Signing in…' : 'Sign in'}
            </Button>
            <div className="flex items-center gap-3">
              <span className="h-px flex-1 bg-border" />
              <span className="text-xs text-muted-foreground">or</span>
              <span className="h-px flex-1 bg-border" />
            </div>
            <Button
              disabled={submitting}
              type="button"
              variant="outline"
              onClick={async () => {
                if (submitting) {
                  return;
                }
                setSubmitting(true);
                await store.signInWithGoogle();
                if (!mountedRef.current) {
                  return;
                }
                setSubmitting(false);
              }}>
              <svg aria-hidden="true" className="size-5 shrink-0" viewBox="0 0 48 48">
                <path fill="#EA4335" d="M24 9.5c3.54 0 6.71 1.22 9.21 3.6l6.85-6.85C35.9 2.38 30.47 0 24 0 14.62 0 6.51 5.38 2.56 13.22l7.98 6.19C12.43 13.72 17.74 9.5 24 9.5z" />
                <path fill="#4285F4" d="M46.98 24.55c0-1.57-.15-3.09-.38-4.55H24v9.02h12.94c-.58 2.96-2.26 5.48-4.78 7.18l7.73 6c4.51-4.18 7.09-10.36 7.09-17.65z" />
                <path fill="#FBBC05" d="M10.53 28.59c-.48-1.45-.76-2.99-.76-4.59s.27-3.14.76-4.59l-7.98-6.19C.92 16.46 0 20.12 0 24c0 3.88.92 7.54 2.56 10.78l7.97-6.19z" />
                <path fill="#34A853" d="M24 48c6.48 0 11.93-2.13 15.89-5.81l-7.73-6c-2.15 1.45-4.92 2.3-8.16 2.3-6.26 0-11.57-4.22-13.47-9.91l-7.98 6.19C6.51 42.62 14.62 48 24 48z" />
              </svg>
              Continue with Google
            </Button>
          </form>
        </SectionCard>
    );
  } else {
    const tierLabel = formatPlanLabel(account.tier);
    const renewDate = formatBillingDate(account.subscriptionExpiresAt);
    accountSection = (
      <SectionCard
        title={account.email}
        description={[
          tierLabel + ' plan',
          account.displayName || '',
          account.subscriptionStatus ? `Status: ${account.subscriptionStatus}` : ''
        ].filter(Boolean).join(' · ')}
        action={
          <Button disabled={loading} type="button" variant="outline" onClick={() => void store.signOut()}>
            Sign out
          </Button>
        }
        padded
      >
        {renewDate ? (
          <div className="text-sm text-muted-foreground space-y-1">
            <p>Next renewal on {renewDate}</p>
          </div>
        ) : null}
      </SectionCard>
    );
  }

  return (
    <PaneShell pane={pane}>
      {accountSection}
      <div className="mt-6">
        <SyncConnectionSection store={store} />
      </div>
    </PaneShell>
  );
}



export function PaneLoading({message}: {message: string}) {
  return <p className={STANDALONE_MESSAGE_CLASS}>{message}</p>;
}

export function PaneRetry(
    {message, onRetry, title}: {
      message: string;
      onRetry: () => void;
      title: string;
    }) {
  return (
    <section className={RETRY_ROW_CLASS}>
      <div className={SETTING_COPY_CLASS}>
        <h4 className={SETTING_TITLE_CLASS}>{title}</h4>
        <p className={SETTING_DESCRIPTION_CLASS}>{message}</p>
      </div>
      <div className={SETTING_ACTIONS_CLASS}>
        <ActionButton label="Retry" onClick={onRetry} />
      </div>
    </section>
  );
}

export function EmptyDescription({message}: {message: string}) {
  return <p className={EMPTY_DESCRIPTION_CLASS}>{message}</p>;
}



export function ActionButton(
    {ariaLabel, className, disabled, label, onClick, style}: {
      ariaLabel?: string;
      className?: string;
      disabled?: boolean;
      label: React.ReactNode;
      onClick: () => void | Promise<void>;
      style?: React.CSSProperties;
    }) {
  const [isBusy, setIsBusy] = useState(false);
  const mountedRef = useMountedRef();

  return (
    <Button
      aria-label={ariaLabel}
      className={cn(ACTION_BUTTON_CLASS, className)}
      disabled={disabled || isBusy}
      style={style}
      type="button"
      variant="outline"
      onClick={async () => {
        if (mountedRef.current) {
          setIsBusy(true);
        }
        try {
          await onClick();
        } finally {
          if (mountedRef.current) {
            setIsBusy(false);
          }
        }
      }}>
      {label}
    </Button>
  );
}

export function Toggle(
    {ariaDescribedBy, disabled, enabled, label, onToggle}: {
      ariaDescribedBy?: string;
      disabled?: boolean;
      enabled: boolean;
      label: string;
      onToggle: (nextEnabled: boolean) => Promise<void> | void;
    }) {
  const [isBusy, setIsBusy] = useState(false);
  const mountedRef = useMountedRef();

  return (
    <div className="flex items-center space-x-2">
      <Switch
        checked={enabled}
        disabled={disabled || isBusy}
        aria-label={label}
        aria-describedby={ariaDescribedBy}
        onCheckedChange={async (checked) => {
          if (isBusy) return;
          if (mountedRef.current) {
            setIsBusy(true);
          }
          try {
            await onToggle(checked);
          } finally {
            if (mountedRef.current) {
              setIsBusy(false);
            }
          }
        }}
      />
      <span className="text-sm text-muted-foreground">{enabled ? 'On' : 'Off'}</span>
    </div>
  );
}

export function ManagedSettingRow(
    {children, className, controlClassName, description, title}: {
      children?: React.ReactNode;
      className?: string;
      controlClassName?: string;
      description: React.ReactNode;
      title: string;
    }) {
  return (
    <section className={cn(SETTING_ROW_CLASS, className)}>
      <div className={SETTING_COPY_CLASS}>
        <h4 className={SETTING_TITLE_CLASS}>{title}</h4>
        <p className={SETTING_DESCRIPTION_CLASS}>{description}</p>
      </div>
      <div className={cn(SETTING_CONTROL_CLASS, controlClassName)}>{children}</div>
    </section>
  );
}

export function AiMailReadConsentSetting(
    {allowed, onSave}: {
      allowed: boolean;
      onSave: (allowed: boolean) => Promise<boolean>;
    }) {
  const descriptionId = 'ai-mail-read-consent-description';
  const [displayedAllowed, setDisplayedAllowed] = useState(allowed);
  const [saveError, setSaveError] = useState<string | null>(null);

  useEffect(() => {
    setDisplayedAllowed(allowed);
  }, [allowed]);

  return (
    <ManagedSettingRow
      description={
        <span id={descriptionId}>
          Permit AI reads of Mail accounts, folders, message metadata, and message bodies.
          This permission is off by default and can be revoked at any time.
        </span>
      }
      title="Allow AI to read Mail">
      <div className="flex flex-col items-end gap-2">
        <Toggle
          ariaDescribedBy={descriptionId}
          enabled={displayedAllowed}
          label="Allow AI to read Mail"
          onToggle={async nextAllowed => {
            setSaveError(null);
            setDisplayedAllowed(nextAllowed);
            const saved = await onSave(nextAllowed);
            if (!saved) {
              setDisplayedAllowed(allowed);
              setSaveError('Could not save Mail access. Your previous setting was restored.');
            }
          }}
        />
        {saveError ? (
          <p className="max-w-72 text-right text-xs text-destructive" role="alert">
            {saveError}
          </p>
        ) : null}
      </div>
    </ManagedSettingRow>
  );
}

function ReadOnlySettingRow(
    {description, title}: {
      description: React.ReactNode;
      title: string;
    }) {
  return (
    <section className={SETTING_ROW_CLASS}>
      <div className={SETTING_COPY_CLASS}>
        <h4 className={SETTING_TITLE_CLASS}>{title}</h4>
        <p className={SETTING_DESCRIPTION_CLASS}>{description}</p>
      </div>
    </section>
  );
}

export function TextFieldShell(
    {ariaLabel, disabled, onChange, onKeyDown, placeholder, type = 'text', value}: {
      ariaLabel: string;
      disabled?: boolean;
      onChange: (value: string) => void;
      onKeyDown?: (event: React.KeyboardEvent<HTMLInputElement>) => void;
      placeholder: string;
      type?: string;
      value: string;
    }) {
  return (
    <div className={INPUT_SHELL_CLASS}>
      <Input
        aria-label={ariaLabel}
        className={INPUT_FIELD_CLASS}
        disabled={disabled}
        placeholder={placeholder}
        type={type}
        value={value}
        onChange={event => onChange(event.currentTarget.value)}
        onKeyDown={onKeyDown}
      />
    </div>
  );
}

export function SelectShell<T extends string>(
    {ariaLabel, onChange, options, value}: {
      ariaLabel: string;
      onChange: (value: T) => void;
      options: Array<{label: string; value: T; disabled?: boolean; description?: string}>;
      value: T;
    }) {
  const unknownValue = value !== '' && !options.some(option => option.value === value);

  return (
    <div className={SELECT_SHELL_CLASS}>
      <Select value={value} onValueChange={nextValue => onChange(nextValue as T)}>
        <SelectTrigger aria-label={ariaLabel} className={SELECT_TRIGGER_CLASS}>
          <SelectValue placeholder={ariaLabel}>
            {unknownValue ? value : undefined}
          </SelectValue>
        </SelectTrigger>
        <SelectContent>
          {options.map(option => (
            <SelectItem key={option.value} value={option.value} disabled={option.disabled}>
              <div className="flex flex-col text-left">
                <span className="font-medium">{option.label}</span>
                {option.description && (
                  <span className="text-xs text-muted-foreground">{option.description}</span>
                )}
              </div>
            </SelectItem>
          ))}
        </SelectContent>
      </Select>
    </div>
  );
}

function ProfilesPane({pane, store}: {pane: PaneDefinition; store: MahoSettingsStore}) {
  return (
    <PaneShell pane={pane}>
      <ProfilesEditor store={store} />
    </PaneShell>
  );
}

function MahoMiniPane({pane, store}: {pane: PaneDefinition; store: MahoSettingsStore}) {
  const handler = store.getHandler();
  const settingsState = useSettingsStoreState(store);
  const settings = settingsState.settings;
  const [pattern, setPattern] = useState('');
  const [rulesState, setRulesState] = useState<LoadState<ATCRule[]>>(createLoadingState());
  const [spacesState, setSpacesState] = useState<LoadState<SpaceBasicInfo[]>>(createLoadingState());
  const [spaceId, setSpaceId] = useState('');
  const [addError, setAddError] = useState('');

  const loadRules = useCallback(async () => {
    setRulesState(createLoadingState());
    try {
      const {rules} = await handler.getATCRules();
      setRulesState(createLoadedState(rules));
    } catch {
      setRulesState(createErrorState('Failed to load rules.'));
    }
  }, [handler]);

  const loadSpaces = useCallback(async () => {
    setSpacesState(createLoadingState());
    try {
      const {spaces} = await handler.getSpaces();
      setSpacesState(createLoadedState(spaces));
    } catch {
      setSpacesState(createErrorState('Failed to load spaces.'));
    }
  }, [handler]);

  useEffect(() => {
    void loadRules();
    void loadSpaces();
  }, [loadRules, loadSpaces]);

  useEffect(() => {
    if (spacesState.status === 'loaded' && spacesState.data.length > 0 && !spaceId) {
      setSpaceId(spacesState.data[0]!.id);
    }
  }, [spacesState, spaceId]);

  const isLoaded = rulesState.status === 'loaded' && spacesState.status === 'loaded';
  const isAddDisabled = !pattern || !spaceId || (spacesState.status === 'loaded' && spacesState.data.length === 0);

  return (
    <PaneShell pane={pane}>
      {rulesState.status === 'loading' || spacesState.status === 'loading' ? (
        <PaneLoading message="Loading routing rules and spaces…" />
      ) : null}
      {rulesState.status === 'error' ? (
        <PaneRetry message={rulesState.message} title="Rules unavailable" onRetry={() => void loadRules()} />
      ) : null}
      {spacesState.status === 'error' ? (
        <PaneRetry message={spacesState.message} title="Spaces unavailable" onRetry={() => void loadSpaces()} />
      ) : null}
      {isLoaded ? (
        <>
          <SectionCard
            title="Maho Mini"
            description="Maho Mini is a smaller, simpler window — perfect for quick lookups and reading links friends send you."
            padded
          >
            <div className="divide-y divide-border -mx-6 -my-6">
              <ManagedSettingRow
                description="Open a blank Maho Mini window with Option+Command+N."
                title="Option+Command+N shortcut"
                className="px-6 py-4">
                <Toggle
                  enabled={settings.find(s => s.key === 'atc.maho_mini_global_shortcut_enabled')?.value !== 'false'}
                  label="Option+Command+N shortcut"
                  onToggle={async nextEnabled => {
                    await store.commitSettingValue('atc.maho_mini_global_shortcut_enabled', nextEnabled ? 'true' : 'false');
                  }}
                />
              </ManagedSettingRow>
              <ManagedSettingRow
                description="Command+Option+Click on links to open them in Maho Mini instead of a new tab."
                title="Command+Option+Click override"
                className="px-6 py-4">
                <Toggle
                  enabled={settings.find(s => s.key === 'atc.maho_mini_click_override_enabled')?.value !== 'false'}
                  label="Command+Option+Click override"
                  onToggle={async nextEnabled => {
                    await store.commitSettingValue('atc.maho_mini_click_override_enabled', nextEnabled ? 'true' : 'false');
                  }}
                />
              </ManagedSettingRow>
              <ManagedSettingRow
                description="Open external links in Maho Mini from other applications."
                title="Open external links in Maho Mini"
                className="px-6 py-4">
                <Toggle
                  enabled={settings.find(s => s.key === 'atc.open_external_links_in_maho_mini')?.value === 'true'}
                  label="Open external links in Maho Mini"
                  onToggle={async nextEnabled => {
                    await store.commitSettingValue('atc.open_external_links_in_maho_mini', nextEnabled ? 'true' : 'false');
                  }}
                />
              </ManagedSettingRow>
            </div>
          </SectionCard>

          <SectionCard
            title="Peek"
            description="Preview supported links and popups without leaving your current page."
            padded
          >
            <div className="divide-y divide-border -mx-6 -my-6">
              <ManagedSettingRow
                description="Show supported links and popups in an in-window preview."
                title="Enable Peek"
                className="px-6 py-4">
                <Toggle
                  enabled={settings.find(s => s.key === 'atc.peek_enabled')?.value !== 'false'}
                  label="Enable Peek"
                  onToggle={async nextEnabled => {
                    await store.commitSettingValue('atc.peek_enabled', nextEnabled ? 'true' : 'false');
                  }}
                />
              </ManagedSettingRow>
              <ManagedSettingRow
                description="Open supported links from pinned and favorite tabs in Peek."
                title="Open links in Peek"
                className="px-6 py-4">
                <Toggle
                  enabled={settings.find(s => s.key === 'atc.peek_link_routing_enabled')?.value !== 'false'}
                  label="Open links in Peek"
                  onToggle={async nextEnabled => {
                    await store.commitSettingValue('atc.peek_link_routing_enabled', nextEnabled ? 'true' : 'false');
                  }}
                />
              </ManagedSettingRow>
              <ManagedSettingRow
                description="Open supported web popups, including sign-in flows, in Peek."
                title="Open popups in Peek"
                className="px-6 py-4">
                <Toggle
                  enabled={settings.find(s => s.key === 'atc.peek_popup_routing_enabled')?.value !== 'false'}
                  label="Open popups in Peek"
                  onToggle={async nextEnabled => {
                    await store.commitSettingValue('atc.peek_popup_routing_enabled', nextEnabled ? 'true' : 'false');
                  }}
                />
              </ManagedSettingRow>
            </div>
          </SectionCard>

          <SectionCard title="URL routing rules">
            {rulesState.status === 'loaded' && spacesState.status === 'loaded' && rulesState.data.map(rule => (
              <ATCRuleRow
                key={rule.id}
                rule={rule}
                spaces={spacesState.data}
                onRemove={async () => {
                  await handler.removeATCRule(rule.id);
                  setRulesState(current => current.status === 'loaded' ?
                      createLoadedState(current.data.filter(item => item.id !== rule.id)) :
                      current);
                }}
                onToggle={async nextEnabled => {
                  await handler.toggleATCRule(rule.id, nextEnabled);
                  setRulesState(current => current.status === 'loaded' ?
                      createLoadedState(current.data.map(item => item.id === rule.id ? {...item, enabled: nextEnabled} : item)) :
                      current);
                }}
              />
            ))}
            {spacesState.status === 'loaded' && spacesState.data.length === 0 && (
              <div className="text-sm text-destructive px-6 py-2">
                No spaces available. You must create at least one space to add routing rules.
              </div>
            )}
            <ManagedSettingRow
              controlClassName={SETTING_ACTIONS_CLASS}
              description={
                <div>
                  <div>Route matching URLs into a target space. Use * as a wildcard (e.g. *.example.com).</div>
                  {addError && <ActionError message={addError} />}
                </div>
              }
              title="Add rule">
              <TextFieldShell
                ariaLabel="URL pattern"
                placeholder="URL pattern (e.g. *.example.com)"
                value={pattern}
                onChange={setPattern}
              />
              {spacesState.status === 'loaded' && (
                <SelectShell
                  ariaLabel="Target space"
                  options={spacesState.data.length > 0 ? spacesState.data.map(s => ({
                    label: s.name || `Unnamed Space (${s.id})`,
                    value: s.id,
                  })) : [{label: 'No spaces available', value: ''}]}
                  value={spaceId}
                  onChange={setSpaceId}
                />
              )}
              <ActionButton
                disabled={isAddDisabled}
                label="Add"
                onClick={async () => {
                  if (!pattern || !spaceId) {
                    return;
                  }
                  const {rule} = await handler.addATCRule(pattern, spaceId);
                  if (rule) {
                    setRulesState(current => current.status === 'loaded' ?
                        createLoadedState([...current.data, rule]) :
                        current);
                    setPattern('');
                    setAddError('');
                  } else {
                    setAddError('Failed to add rule. The pattern may be invalid or already exist.');
                  }
                }}
              />
            </ManagedSettingRow>
          </SectionCard>
        </>
      ) : null}
    </PaneShell>
  );
}

function ATCRuleRow(
    {onRemove, onToggle, rule, spaces}: {
      onRemove: () => Promise<void>;
      onToggle: (nextEnabled: boolean) => Promise<void>;
      rule: ATCRule;
      spaces: SpaceBasicInfo[];
    }) {
  const getSpaceDisplayName = (targetId: string, spacesList: SpaceBasicInfo[]) => {
    const space = spacesList.find(s => s.id === targetId);
    if (space) {
      return space.name || `Unnamed Space (${targetId})`;
    }
    return `Unknown Space (${targetId})`;
  };

  const displayName = getSpaceDisplayName(rule.targetSpaceId, spaces);

  return (
    <ManagedSettingRow
      controlClassName={SETTING_ACTIONS_CLASS}
      description={<><img alt="" aria-hidden="true" className={ICON_CLASS} src="chrome://maho-settings/icons/arrow-right.svg" /> {displayName}</>}
      title={rule.urlPattern}>
      <Toggle enabled={rule.enabled} label={rule.urlPattern} onToggle={onToggle} />
      <ActionButton ariaLabel="Remove rule" label={<img alt="" aria-hidden="true" className={ICON_CLASS} src="chrome://maho-settings/icons/x.svg" />} onClick={onRemove} />
    </ManagedSettingRow>
  );
}

function useProviderModels(store: MahoSettingsStore, providerId: string) {
  const handler = store.getHandler();
  const callbackRouter = store.getCallbackRouter();
  const [models, setModels] = useState<string[]>([]);
  const [loading, setLoading] = useState(false);
  const [fromCache, setFromCache] = useState(true);
  const [refreshing, setRefreshing] = useState(false);
  const modelRequest = useRef(0);

  const fetchModels = useCallback(async () => {
    const request = ++modelRequest.current;
    setModels([]);
    setFromCache(true);
    setRefreshing(false);
    setLoading(Boolean(providerId));
    if (!providerId || typeof handler.fetchProviderModels !== 'function') return;
    try {
      const res = await handler.fetchProviderModels(providerId);
      if (request !== modelRequest.current) return;
      setModels(res.modelIds);
      setFromCache(res.fromCache);
    } catch (e) {
      if (request === modelRequest.current) console.error('Failed to fetch provider models', e);
    } finally {
      if (request === modelRequest.current) setLoading(false);
    }
  }, [handler, providerId]);

  const refresh = useCallback(async () => {
    if (!providerId || typeof handler.refreshProviderModels !== 'function') return;
    const request = ++modelRequest.current;
    setLoading(false);
    setRefreshing(true);
    try {
      const res = await handler.refreshProviderModels(providerId);
      if (request !== modelRequest.current) return;
      setModels(res.modelIds);
      setFromCache(false);
    } catch (e) {
      if (request === modelRequest.current) console.error('Failed to refresh provider models', e);
    } finally {
      if (request === modelRequest.current) setRefreshing(false);
    }
  }, [handler, providerId]);

  useEffect(() => {
    void fetchModels();
    return () => { modelRequest.current += 1; };
  }, [fetchModels]);

  useEffect(() => {
    let cancelled = false;
    const onRefreshed = (pId: string, modelIds: string[]) => {
      if (cancelled) return;
      if (pId === providerId) {
        setModels(modelIds);
        setFromCache(false);
      }
    };
    const listenerId = callbackRouter.providerModelsRefreshed.addListener(onRefreshed);
    return () => {
      cancelled = true;
      callbackRouter.removeListener(listenerId);
    };
  }, [callbackRouter, providerId]);

  return { models, loading, fromCache, refresh, refreshing };
}

function ModelPicker({
  ariaLabel,
  modelValue,
  providerModels,
  modelsLoading,
  onCommitModel,
  allowAuto,
  customPlaceholder = "Enter model identifier",
  emptyErrorMsg
}: {
  ariaLabel: string;
  modelValue: string;
  providerModels: string[];
  modelsLoading: boolean;
  onCommitModel: (value: string) => Promise<void>;
  allowAuto?: boolean;
  customPlaceholder?: string;
  emptyErrorMsg?: string;
}) {
  const hasModels = providerModels.length > 0;
  const modelInList = hasModels && providerModels.includes(modelValue);
  const isAuto = allowAuto === true && modelValue === '';

  const [userChoseCustom, setUserChoseCustom] = useState(false);
  const [customDraft, setCustomDraft] = useState('');
  const [saveError, setSaveError] = useState<string | null>(null);

  useEffect(() => {
    if (modelInList) {
      setUserChoseCustom(false);
    } else if (!isAuto && modelValue !== '') {
      setCustomDraft(modelValue);
    }
  }, [modelInList, modelValue, isAuto]);

  if (modelsLoading) {
    return (
      <div className="text-xs text-muted-foreground flex items-center gap-1">
        <Loader2 className="size-3 animate-spin" />
        Loading models...
      </div>
    );
  }

  const showCustomInput =
      !hasModels || userChoseCustom || (!isAuto && hasModels && !modelInList);

  const options: Array<{value: string; label: string}> = [];
  if (allowAuto) {
    options.push({value: '', label: 'Auto — Best model for your prompt'});
  }
  options.push(...providerModels.map(m => ({value: m, label: m})));
  if (hasModels) {
    options.push({value: 'custom', label: 'Other...'});
  }

  let selectedValue: string;
  if (isAuto) {
    selectedValue = '';
  } else if (showCustomInput) {
    selectedValue = 'custom';
  } else {
    selectedValue = modelValue;
  }

  const handleSaveCustom = async () => {
    const trimmed = customDraft.trim();
    if (!trimmed) {
      return;
    }
    setSaveError(null);
    try {
      await onCommitModel(trimmed);
    } catch (err) {
      setSaveError('Failed to save custom model.');
    }
  };

  return (
    <div className="flex flex-col gap-2 w-full">
      {hasModels ? (
        <SelectShell
          ariaLabel={ariaLabel}
          value={selectedValue}
          options={options}
          onChange={value => {
            if (value === 'custom') {
              setUserChoseCustom(true);
            } else {
              setUserChoseCustom(false);
              void onCommitModel(value);
            }
          }}
        />
      ) : emptyErrorMsg ? (
        <div className="text-xs text-destructive flex items-center gap-1 mb-1">
          <AlertCircle className="size-3.5" />
          {emptyErrorMsg}
        </div>
      ) : null}
      {showCustomInput && (
        <div className="flex flex-col gap-1.5 w-full">
          <div className="flex items-center gap-2 w-full">
            <TextFieldShell
              ariaLabel={ariaLabel + " Text Input"}
              placeholder={customPlaceholder}
              value={customDraft}
              onChange={setCustomDraft}
              onKeyDown={e => {
                if (e.key === 'Enter') {
                  e.preventDefault();
                  void handleSaveCustom();
                }
              }}
            />
            <ActionButton
              label="Save"
              onClick={handleSaveCustom}
              disabled={!customDraft.trim()}
            />
          </div>
          {saveError ? (
            <p className="text-xs text-destructive mt-0.5">{saveError}</p>
          ) : null}
        </div>
      )}
    </div>
  );
}

const STATE_CONNECTED = 1;
const STATE_DISCONNECTED = 0;
const STATE_OFFLINE = 3;

interface AiPaneState {
  models: AIModelsSettings;
  legacy: AISettings | null;
}

const TASK_DEFINITIONS = [
  {
    id: 'tab_tidy',
    name: 'Tab Tidy',
    description: 'Groups tabs into folders when you run Tab Tidy.',
  },
  {
    id: 'inline_edit',
    name: 'Inline Edit',
    description: 'Rewrites selected text with your instruction.',
  },
  {
    id: 'page_preview',
    name: 'Page previews',
    description: 'Generates AI page preview text when requested.',
  },
  {
    id: 'tab_title',
    name: 'Pinned tab titles',
    description: 'Shortens titles for pinned tabs when title tidying is enabled.',
  },
  {
    id: 'download_tidy',
    name: 'Download naming',
    description: 'Suggests cleaner names for completed downloads when download tidying is enabled.',
  },
  {
    id: 'memory',
    name: 'Memory & summaries',
    description: 'Extracts durable facts and summarizes browsing activity in the background.',
  },
];

function getProviderIcon(id: string) {
  switch (id) {
    case 'maho-managed':
      return <Sparkles className="size-4 text-primary" />;
    case 'openai':
    case 'anthropic':
      return <Zap className="size-4 text-warning" />;
    case 'openai-compatible':
      return <Globe className="size-4 text-primary" />;
    case 'local-server':
      return <Database className="size-4 text-success" />;
    default:
      return <Sparkles className="size-4" />;
  }
}

function AiPane({pane, store}: {pane: PaneDefinition; store: MahoSettingsStore}) {
  const handler = store.getHandler();
  const settingsState = useSettingsStoreState(store);
  const conversationAutoArchiveDays =
      settingsState.settings.find(
          setting => setting.key === 'conversation.auto_archive_after_days')?.value ?? '-1';
  const [actionError, setActionError] = useState<string | null>(null);
  const [state, setState] = useState<LoadState<AiPaneState>>(createLoadingState());
  const [activeModal, setActiveModal] = useState<{
    type: 'configure' | 'disconnect';
    providerId: string;
  } | null>(null);
  const [testResults, setTestResults] = useState<Record<string, {ok: boolean; message: string}>>({});
  const [testingProviders, setTestingProviders] = useState<Record<string, boolean>>({});

  const loadSettings = useCallback(async () => {
    setState(current =>
      current.status === 'loaded' ? current : createLoadingState());
    try {
      let modelsData: AIModelsSettings | null = null;
      let aiData: AISettings | null = null;

      if (typeof handler.getAIModelsSettings === 'function') {
        try {
          const res = await handler.getAIModelsSettings();
          modelsData = res.settings;
        } catch {
          // fallback to legacy
        }
      }

      if (typeof handler.getAISettings === 'function') {
        try {
          const res = await handler.getAISettings();
          aiData = res.settings;
        } catch {
          // ignore
        }
      }

      if (!modelsData && aiData) {
        const hasManaged = aiData.hasRelaySession;
        const isManagedDefault = aiData.provider === 'maho-managed';
        modelsData = {
          defaultProviderId: aiData.provider || (hasManaged ? 'maho-managed' : 'openai'),
          defaultModelId: aiData.model || '',
          hasRelaySession: hasManaged,
          providers: [
            {
              id: 'maho-managed',
              label: 'Maho Managed',
              authLabel: 'Maho account',
              state: hasManaged ? STATE_CONNECTED : STATE_DISCONNECTED,
              isDefaultProvider: isManagedDefault,
              models: [],
              modelsLoading: false,
            },
            {
              id: 'openai',
              label: 'OpenAI',
              authLabel: aiData.hasOauthOpenai ? 'OAuth' : 'API key',
              state: (aiData.hasOauthOpenai || aiData.hasByokOpenai) ? STATE_CONNECTED : STATE_DISCONNECTED,
              isDefaultProvider: aiData.provider === 'openai',
              models: [],
              modelsLoading: false,
            },
            {
              id: 'anthropic',
              label: 'Anthropic',
              authLabel: aiData.hasOauthAnthropic ? 'OAuth' : 'API key',
              state: (aiData.hasOauthAnthropic || aiData.hasByokAnthropic) ? STATE_CONNECTED : STATE_DISCONNECTED,
              isDefaultProvider: aiData.provider === 'anthropic',
              models: [],
              modelsLoading: false,
            },
            {
              id: 'openai-compatible',
              label: 'OpenAI-compatible',
              authLabel: 'Custom API',
              state: (aiData.provider === 'openai-compatible' && !!aiData.baseUrl) ? STATE_CONNECTED : STATE_DISCONNECTED,
              isDefaultProvider: aiData.provider === 'openai-compatible',
              baseUrl: aiData.baseUrl,
              models: [],
              modelsLoading: false,
            },
            {
              id: 'local-server',
              label: 'Local Server (Ollama)',
              authLabel: 'Ollama',
              state: aiData.provider === 'local-server' ? STATE_CONNECTED : STATE_DISCONNECTED,
              isDefaultProvider: aiData.provider === 'local-server',
              baseUrl: aiData.baseUrl || 'http://localhost:11434',
              models: [],
              modelsLoading: false,
            },
          ],
          taskRoutes: [
            { taskId: 'tab_tidy', inheritsDefault: true, effectiveProviderId: aiData.provider, effectiveModelId: aiData.model, available: true },
            { taskId: 'inline_edit', inheritsDefault: true, effectiveProviderId: aiData.provider, effectiveModelId: aiData.model, available: true },
            { taskId: 'page_preview', inheritsDefault: true, effectiveProviderId: aiData.provider, effectiveModelId: aiData.model, available: true },
            { taskId: 'tab_title', inheritsDefault: true, effectiveProviderId: aiData.provider, effectiveModelId: aiData.model, available: true },
            { taskId: 'download_tidy', inheritsDefault: true, effectiveProviderId: aiData.provider, effectiveModelId: aiData.model, available: true },
            { taskId: 'memory', inheritsDefault: true, effectiveProviderId: aiData.provider, effectiveModelId: aiData.model, available: true },
          ],
        };
      }

      if (!modelsData) {
        throw new Error('Failed to load models settings');
      }

      setState(createLoadedState({ models: modelsData, legacy: aiData }));
    } catch (err) {
      console.error("LOAD SETTINGS ERROR:", err);
      setState(createErrorState('Failed to load Maho AI settings.'));
    }
  }, [handler]);

  useEffect(() => {
    void loadSettings();
  }, [loadSettings]);

  // Live sync: the chrome://maho-ai quick switcher writes the same Default
  // pair; the browser pushes OnAIModelsSettingsChanged so this page re-renders
  // the Providers radio, Chat & Agent row, and inherited task rows immediately.
  useEffect(() => {
    const router = store.getCallbackRouter?.();
    const event = (router as {onAIModelsSettingsChanged?: {addListener: (l: (s: AIModelsSettings) => void) => number}} | undefined)?.onAIModelsSettingsChanged;
    if (!event || typeof event.addListener !== 'function') {
      return;
    }
    const id = event.addListener((settings: AIModelsSettings) => {
      setState(current => {
        const legacy = current.status === 'loaded' ? current.data.legacy : null;
        return createLoadedState({models: settings, legacy});
      });
    });
    return () => {
      if (typeof (router as {removeListener?: (id: number) => void}).removeListener === 'function') {
        (router as {removeListener: (id: number) => void}).removeListener(id);
      }
    };
  }, [store]);

  const lastState = useRef<SettingsState>(store.getSnapshot());
  useEffect(() => {
    const unsub = store.subscribe(() => {
      const current = store.getSnapshot();
      const prev = lastState.current;
      lastState.current = current;
      const accountChanged = prev.accountStatus !== current.accountStatus;
      const settingsChanged = prev.settings !== current.settings;
      if (accountChanged || settingsChanged) {
        void loadSettings();
      }
    });
    return unsub;
  }, [store, loadSettings]);

  const commit: (action: () => Promise<void>) => Promise<void> = useCallback(async (action: () => Promise<void>): Promise<void> => {
    setActionError(null);
    try {
      await action();
    } catch {
      setActionError('Failed to save AI settings.');
    } finally {
      await loadSettings();
    }
  }, [loadSettings]);

  const currentProviderId = state.status === 'loaded' ? state.data.models.defaultProviderId : '';
  const { models: providerModels, loading: modelsLoading } = useProviderModels(store, currentProviderId);

  const isLegacyFallback = typeof handler.getAIModelsSettings !== 'function';

  const handleSetDefaultProvider = async (providerId: string) => {
    await commit(async () => {
      if (!isLegacyFallback && typeof handler.setDefaultAIModel === 'function' && state.status === 'loaded') {
        const p = state.data.models.providers.find(prov => prov.id === providerId);
        const currentDefaultModel = state.data.models.defaultModelId;
        const isLastModelSelectable = Boolean(p?.lastModelId && p.models.some(m => m.id === p.lastModelId));
        const isCurrentDefaultSelectable = Boolean(currentDefaultModel && p?.models.some(m => m.id === currentDefaultModel));
        const modelId = (isLastModelSelectable ? p?.lastModelId : undefined) ||
                        (isCurrentDefaultSelectable ? currentDefaultModel : undefined) ||
                        p?.models[0]?.id;
        if (!modelId) {
          setActiveModal({type: 'configure', providerId});
          return;
        }
        await handler.setDefaultAIModel(providerId, modelId);
      } else if (typeof handler.setAIProvider === 'function') {
        await handler.setAIProvider(providerId);
      }
    });
  };

  const handleTestProvider = async (providerId: string) => {
    setTestingProviders(prev => ({...prev, [providerId]: true}));
    try {
      if (typeof handler.testAIProvider === 'function') {
        const {result} = await handler.testAIProvider(providerId);
        setTestResults(prev => ({...prev, [providerId]: result}));
      } else {
        setTestResults(prev => ({...prev, [providerId]: {ok: true, message: 'Provider connection test passed.'}}));
      }
    } catch (e: unknown) {
      setTestResults(prev => ({...prev, [providerId]: {ok: false, message: e instanceof Error ? e.message : 'Connection failed.'}}));
    } finally {
      setTestingProviders(prev => ({...prev, [providerId]: false}));
    }
  };

  return (
    <section className="mx-auto w-full max-w-3xl px-6 py-8" data-pane={pane.key} id={`section-${pane.key}`}>
      <h2 className="mb-6 text-2xl font-semibold tracking-tight">{pane.title}</h2>

      {actionError ? <p className={STANDALONE_MESSAGE_CLASS}>{actionError}</p> : null}
      {state.status === 'loading' ? <PaneLoading message="Loading Maho AI settings…" /> : null}
      {state.status === 'error' ? (
        <PaneRetry message={state.message} title="Maho AI settings unavailable" onRetry={() => void loadSettings()} />
      ) : null}
      {state.status === 'loaded' ? (
        <>
          {/* Providers Card */}
          <div className="mb-6">
            <SectionCard
              title="Providers"
              description="Connect multiple AI providers. The active radio button marks the global Default provider."
            >
              {!state.data.models.hasRelaySession && (
                <div className="mx-6 my-4 rounded-xl border border-border/70 bg-card/50 p-5 flex flex-col gap-3">
                  <div className="flex items-start gap-3">
                    <div className="flex h-8 w-8 shrink-0 items-center justify-center rounded-lg bg-primary/10 text-primary">
                      <Sparkles className="size-4" />
                    </div>
                    <div>
                      <p className="text-sm font-semibold text-foreground">Maho Managed AI requires a Maho account</p>
                      <p className="mt-1 text-xs text-muted-foreground">
                        Sign in to access the managed AI service with no API keys needed.
                        Free tier includes generous monthly usage.
                      </p>
                    </div>
                  </div>
                  <div className="flex gap-2">
                    <Button
                      size="sm"
                      onClick={() => { store.selectPane('account'); }}
                      className="h-8 rounded-lg px-3.5 text-xs font-semibold"
                    >
                      Sign In
                    </Button>
                  </div>
                </div>
              )}

              <div className="divide-y divide-border/40">
                {state.data.models.providers.map(provider => {
                  const isDefault = provider.isDefaultProvider;
                  const isConnected = isLegacyFallback || provider.state === AIConnectionState.kConnected;
                  const isOffline = !isLegacyFallback && (provider.state === AIConnectionState.kOffline);
                  const isTesting = testingProviders[provider.id] || false;
                  const testRes = testResults[provider.id];

                  return (
                    <div
                      key={provider.id}
                      className="flex items-center justify-between px-6 py-4 hover:bg-muted/10 transition-colors"
                      data-provider-row={provider.id}
                    >
                      <div className="flex items-center gap-3">
                        <input
                          type="radio"
                          name="default_ai_provider"
                          aria-label={`Set ${provider.label} as Default`}
                          checked={isDefault}
                          disabled={!isConnected}
                          onChange={() => { void handleSetDefaultProvider(provider.id); }}
                          className="size-4 cursor-pointer text-primary focus:ring-primary disabled:opacity-40"
                        />
                        <div className="flex size-8 shrink-0 items-center justify-center rounded-lg bg-muted/30">
                          {getProviderIcon(provider.id)}
                        </div>
                        <div>
                          <div className="flex items-center gap-2">
                            <span className="text-sm font-medium text-foreground">{provider.label}</span>
                            {isDefault && (
                              <Badge variant="secondary" className="text-[10px] px-1.5 py-0 font-medium">
                                Default
                              </Badge>
                            )}
                          </div>
                          <span className="text-xs text-muted-foreground">{provider.authLabel}</span>
                        </div>
                      </div>

                      <div className="flex items-center gap-3">
                        {testRes && (
                          <span className={cn("text-xs", testRes.ok ? "text-success font-medium" : "text-destructive")}>
                            {testRes.message}
                          </span>
                        )}

                        <Badge
                          variant={
                            isConnected
                              ? "success"
                              : isOffline
                              ? "warning"
                              : "secondary"
                          }
                          className="text-xs"
                        >
                          {isConnected ? "Connected" : isOffline ? "Offline" : "Not connected"}
                        </Badge>

                        {isConnected ? (
                          <div className="flex items-center gap-1.5">
                            <Button
                              size="sm"
                              variant="ghost"
                              className="h-8 px-2.5 text-xs"
                              disabled={isTesting}
                              onClick={() => { void handleTestProvider(provider.id); }}
                            >
                              {isTesting ? <Loader2 className="size-3 animate-spin mr-1" /> : null}
                              Test
                            </Button>
                            <Button
                              size="sm"
                              variant="outline"
                              className="h-8 px-2.5 text-xs"
                              onClick={() => setActiveModal({type: 'configure', providerId: provider.id})}
                            >
                              Configure
                            </Button>
                            <Button
                              size="sm"
                              variant="ghost"
                              className="h-8 px-2 text-xs text-muted-foreground hover:text-destructive"
                              onClick={() => setActiveModal({type: 'disconnect', providerId: provider.id})}
                            >
                              Disconnect
                            </Button>
                          </div>
                        ) : (
                          <Button
                            size="sm"
                            variant="outline"
                            className="h-8 px-3 text-xs font-medium"
                            onClick={() => {
                              if (provider.id === 'maho-managed') {
                                store.selectPane('account');
                              } else {
                                setActiveModal({type: 'configure', providerId: provider.id});
                              }
                            }}
                          >
                            Connect
                          </Button>
                        )}
                      </div>
                    </div>
                  );
                })}
              </div>
            </SectionCard>
          </div>

          {/* Task Models Card */}
          <div className="mb-6">
            <SectionCard
              title="Task Models"
              description="Assign models to specific features. Features set to 'Use Default' inherit the Chat & Agent model."
            >
              {/* Row 0: Chat & Agent — Default */}
              <div className="border-b border-border/40 px-6 py-4 bg-muted/5">
                <div className="flex items-start justify-between gap-4">
                  <div>
                    <div className="flex items-center gap-2">
                      <span className="text-sm font-semibold text-foreground">Chat & Agent</span>
                      <Badge variant="secondary" className="text-[10px] px-1.5 py-0 font-semibold bg-primary/10 text-primary">
                        Default
                      </Badge>
                    </div>
                    <p className="mt-1 text-xs text-muted-foreground leading-relaxed">
                      Conversations and agent work in Aside. Also used by Memory, Tab Tidy, and other background features unless they have their own model override.
                    </p>
                  </div>

                  <div className="shrink-0 w-64">
                    <ModelPicker
                      ariaLabel="Model"
                      modelValue={state.data.models.defaultModelId}
                      providerModels={providerModels}
                      modelsLoading={modelsLoading}
                      onCommitModel={async value => {
                        await commit(async () => {
                          if (typeof handler.setAIModel === 'function') {
                            await handler.setAIModel(value);
                          }
                          if (typeof handler.setDefaultAIModel === 'function') {
                            await handler.setDefaultAIModel(state.data.models.defaultProviderId, value);
                          }
                        });
                      }}
                      customPlaceholder="Select or enter model"
                    />
                  </div>
                </div>
              </div>

              {/* Task Rows */}
              <div className="divide-y divide-border/30">
                {TASK_DEFINITIONS.map(taskDef => {
                  const taskRoute = state.data.models.taskRoutes.find(r => r.taskId === taskDef.id);
                  const isInheriting = taskRoute ? taskRoute.inheritsDefault : true;
                  const effectiveModel = taskRoute?.effectiveModelId || state.data.models.defaultModelId || 'Default';

                  // Build model options combining Use Default and connected provider models
                  const options: Array<{value: string; label: string}> = [
                    {value: '', label: `Use Default (${state.data.models.defaultModelId || 'Default'})`},
                  ];

                  for (const prov of state.data.models.providers) {
                    if (prov.state === AIConnectionState.kConnected) {
                      for (const m of prov.models) {
                        options.push({
                          value: `${prov.id}::${m.id}`,
                          label: `${prov.label}: ${m.label || m.id}`,
                        });
                      }
                    }
                  }

                  const currentValue = isInheriting ? '' : `${taskRoute?.configuredProviderId || taskRoute?.effectiveProviderId}::${taskRoute?.configuredModelId || taskRoute?.effectiveModelId}`;

                  return (
                    <div key={taskDef.id} className="flex items-start justify-between px-6 py-4 gap-4" data-task-row={taskDef.id}>
                      <div>
                        <div className="flex items-center gap-2">
                          <span className="text-sm font-medium text-foreground">{taskDef.name}</span>
                          {!isInheriting && (
                            <Badge variant="outline" className="text-[10px] px-1.5 py-0">
                              Override
                            </Badge>
                          )}
                        </div>
                        <p className="mt-0.5 text-xs text-muted-foreground">{taskDef.description}</p>
                        <span className="mt-1 block text-[11px] text-muted-foreground/80">
                          Current: <strong className="font-medium text-foreground/90">{effectiveModel}</strong>
                        </span>
                      </div>

                      <div className="shrink-0 w-64">
                        <SelectShell
                          ariaLabel={`${taskDef.name} Model`}
                          value={currentValue}
                          options={options}
                          onChange={async val => {
                            await commit(async () => {
                              if (!val) {
                                if (typeof handler.setTaskAIModel === 'function') {
                                  await handler.setTaskAIModel(taskDef.id, undefined, undefined);
                                }
                              } else {
                                const [pid, mid] = val.split('::');
                                if (typeof handler.setTaskAIModel === 'function') {
                                  await handler.setTaskAIModel(taskDef.id, pid, mid);
                                }
                              }
                            });
                          }}
                        />
                      </div>
                    </div>
                  );
                })}
              </div>
            </SectionCard>
          </div>

          {/* Permissions Card */}
          <div className="mb-6">
            <SectionCard title="Permissions">
              {[
                'ai.permission_tier',
                'ai.final_confirm',
                'ai.proactive_mode',
                'ai.approval_policy',
              ].map(key => {
                const setting = settingsState.settings.find(item => item.key === key);
                const meta = SETTING_METADATA[key];
                if (!setting) return null;
                const save = async (value: string) => {
                  await commit(async () => {
                    if (!await store.commitSettingValue(key, value)) {
                      throw new Error(`Failed to save ${key}`);
                    }
                  });
                };
                return (
                  <ManagedSettingRow key={key} title={meta.label} description={meta.description}>
                    {meta.control === 'select' ? (
                      <SelectShell
                        ariaLabel={meta.label}
                        value={setting.value}
                        options={meta.options ?? []}
                        onChange={value => { void save(value); }}
                      />
                    ) : (
                      <Toggle
                        label={meta.label}
                        enabled={setting.value === 'true'}
                        onToggle={enabled => save(enabled ? 'true' : 'false')}
                      />
                    )}
                  </ManagedSettingRow>
                );
              })}
              {state.data.legacy && (
                <AiMailReadConsentSetting
                  allowed={state.data.legacy.mailReadAllowed}
                  onSave={async allowed => {
                    try {
                      const {success} = await handler.setAIMailReadAllowed(allowed);
                      if (success) {
                        await loadSettings();
                      }
                      return success;
                    } catch {
                      return false;
                    }
                  }}
                />
              )}
            </SectionCard>
          </div>

          <McpAgentGuidanceSettingsSection store={store} />

          <div className="mb-6">
            <SectionCard title="Conversations">
              <AiSelectRow
                label="Auto-archive inactive conversations after"
                initialValue={conversationAutoArchiveDays}
                options={SETTING_METADATA['conversation.auto_archive_after_days']?.options ?? []}
                onCommit={async value => {
                  await commit(async () => {
                    const saved = await store.commitSettingValue(
                        'conversation.auto_archive_after_days', value);
                    if (!saved) throw new Error('Failed to save auto-archive policy');
                  });
                }}
              />
            </SectionCard>
          </div>

          <SectionCard title="Workspace">
            <ManagedSettingRow
              description="Enable or disable AI session persistence."
              title="Session persistence">
              <Toggle
                enabled={state.data.legacy?.sessionPersistenceEnabled ?? false}
                label="Session persistence"
                onToggle={async enabled => { await commit(async () => { await handler.setAISessionPersistence(enabled); }); }}
              />
            </ManagedSettingRow>
          </SectionCard>

          {/* Configure Dialog */}
          {activeModal?.type === 'configure' && (
            <ProviderConfigDialog
              providerId={activeModal.providerId}
              provider={state.data.models.providers.find(p => p.id === activeModal.providerId)!}
              handler={handler}
              onClose={() => setActiveModal(null)}
              onSaved={async () => {
                setActiveModal(null);
                await loadSettings();
              }}
            />
          )}

          {/* Disconnect Dialog */}
          {activeModal?.type === 'disconnect' && (
            <ProviderDisconnectDialog
              providerId={activeModal.providerId}
              modelsData={state.data.models}
              handler={handler}
              onClose={() => setActiveModal(null)}
              onDisconnected={async () => {
                setActiveModal(null);
                await loadSettings();
              }}
            />
          )}
        </>
      ) : null}
    </section>
  );
}

function ProviderConfigDialog({
  providerId,
  provider,
  handler,
  onClose,
  onSaved,
}: {
  providerId: string;
  provider: AIProviderDescriptor;
  handler: PageHandlerRemote;
  onClose: () => void;
  onSaved: () => Promise<void>;
}) {
  const [apiKey, setApiKey] = useState('');
  const [baseUrl, setBaseUrl] = useState(provider.baseUrl || '');
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const handleSave = async () => {
    setSaving(true);
    setError(null);
    try {
      if (providerId === 'openai' || providerId === 'anthropic' || providerId === 'openai-compatible') {
        if (apiKey) {
          const {success} = await handler.setBYOKKey(providerId, apiKey);
          if (!success) throw new Error(`Failed to save ${provider.label} key`);
        }
        if (providerId === 'openai-compatible' && baseUrl && typeof handler.setAIProviderBaseUrl === 'function') {
          await handler.setAIProviderBaseUrl(providerId, baseUrl);
        }
      } else if (providerId === 'local-server') {
        if (baseUrl && typeof handler.setAIProviderBaseUrl === 'function') {
          await handler.setAIProviderBaseUrl(providerId, baseUrl);
        }
      }
      await onSaved();
    } catch (err: unknown) {
      setError(err instanceof Error ? err.message : String(err));
    } finally {
      setSaving(false);
    }
  };

  return (
    <Dialog open onOpenChange={open => { if (!open) onClose(); }}>
      <DialogContent className="sm:max-w-md">
        <DialogHeader>
          <DialogTitle>Configure {provider.label}</DialogTitle>
          <DialogDescription>
            Update credentials and endpoint settings for this provider.
          </DialogDescription>
        </DialogHeader>

        <div className="space-y-4 py-2">
          {error && (
            <Alert variant="destructive">
              <AlertCircle className="size-4" />
              <AlertDescription>{error}</AlertDescription>
            </Alert>
          )}

          {(providerId === 'openai-compatible' || providerId === 'local-server') && (
            <div className="space-y-1.5">
              <label className="text-xs font-semibold text-foreground">Base URL</label>
              <Input
                value={baseUrl}
                placeholder={providerId === 'local-server' ? 'http://localhost:11434' : 'https://api.openai.com/v1'}
                onChange={e => setBaseUrl(e.target.value)}
              />
            </div>
          )}

          {providerId !== 'local-server' && providerId !== 'maho-managed' && (
            <div className="space-y-1.5">
              <label className="text-xs font-semibold text-foreground">API Key</label>
              <Input
                type="password"
                value={apiKey}
                placeholder={provider.state === AIConnectionState.kConnected ? '•••••••• (stored encrypted)' : 'Enter API key'}
                onChange={e => setApiKey(e.target.value)}
              />
            </div>
          )}
        </div>

        <DialogFooter className="flex gap-2">
          <Button variant="outline" onClick={onClose} disabled={saving}>
            Cancel
          </Button>
          <Button onClick={handleSave} disabled={saving}>
            {saving ? <Loader2 className="size-3.5 animate-spin mr-1.5" /> : null}
            Save
          </Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  );
}

function ProviderDisconnectDialog({
  providerId,
  modelsData,
  handler,
  onClose,
  onDisconnected,
}: {
  providerId: string;
  modelsData: AIModelsSettings;
  handler: PageHandlerRemote;
  onClose: () => void;
  onDisconnected: () => Promise<void>;
}) {
  const provider = modelsData.providers.find(p => p.id === providerId);
  const isDefault = modelsData.defaultProviderId === providerId;
  const otherConnected = modelsData.providers.filter(
    p => p.id !== providerId && (p.state === AIConnectionState.kConnected)
  );

  const affectedTasks = (modelsData.taskRoutes || []).filter(
    r => !r.inheritsDefault && (r.configuredProviderId === providerId || r.effectiveProviderId === providerId)
  );

  const [taskReplacements, setTaskReplacements] = useState<Record<string, { inheritDefault: boolean; providerId?: string; modelId?: string }>>(() => {
    const init: Record<string, { inheritDefault: boolean; providerId?: string; modelId?: string }> = {};
    for (const t of (modelsData.taskRoutes || [])) {
      if (!t.inheritsDefault && (t.configuredProviderId === providerId || t.effectiveProviderId === providerId)) {
        init[t.taskId] = { inheritDefault: true };
      }
    }
    return init;
  });

  const [replacementDefaultProvider, setReplacementDefaultProvider] = useState(
    otherConnected[0]?.id || ''
  );
  const [replacementDefaultModel, setReplacementDefaultModel] = useState(
    otherConnected[0]?.lastModelId || otherConnected[0]?.models[0]?.id || ''
  );
  const [disconnecting, setDisconnecting] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const handleConfirm = async () => {
    setDisconnecting(true);
    setError(null);
    try {
      if (typeof handler.disconnectAIProvider === 'function') {
        const replacementsPayload = affectedTasks.map(t => {
          const rep = taskReplacements[t.taskId];
          if (rep && !rep.inheritDefault && rep.providerId) {
            return {
              taskId: t.taskId,
              inheritDefault: false,
              providerId: rep.providerId,
              modelId: rep.modelId || '',
            };
          }
          return {
            taskId: t.taskId,
            inheritDefault: true,
            providerId: undefined,
            modelId: undefined,
          };
        });

        const {accepted, error: err} = await handler.disconnectAIProvider(
          providerId,
          isDefault ? replacementDefaultProvider : undefined,
          isDefault ? replacementDefaultModel : undefined,
          replacementsPayload
        );
        if (!accepted) {
          throw new Error(err || 'Failed to disconnect provider');
        }
      }
      await onDisconnected();
    } catch (err: unknown) {
      setError(err instanceof Error ? err.message : String(err));
    } finally {
      setDisconnecting(false);
    }
  };

  return (
    <Dialog open onOpenChange={open => { if (!open) onClose(); }}>
      <DialogContent className="sm:max-w-md">
        <DialogHeader>
          <DialogTitle>Disconnect {provider?.label}</DialogTitle>
          <DialogDescription>
            Disconnecting this provider will clear its saved credentials and endpoint configuration.
          </DialogDescription>
        </DialogHeader>

        <div className="space-y-4 py-2">
          {error && (
            <Alert variant="destructive">
              <AlertCircle className="size-4" />
              <AlertDescription>{error}</AlertDescription>
            </Alert>
          )}

          {isDefault && (
            <div className="rounded-lg border border-warning/30 bg-warning/10 p-3 text-xs text-warning space-y-2">
              <p className="font-semibold">This is your current Default provider.</p>
              <p>You must assign another connected provider as Default before disconnecting.</p>

              {otherConnected.length === 0 ? (
                <p className="font-medium text-destructive">
                  No other connected providers available. Please connect another provider first.
                </p>
              ) : (
                <div className="space-y-2 pt-1 text-foreground">
                  <div>
                    <label className="text-[11px] font-semibold text-muted-foreground block mb-1">Replacement Default Provider</label>
                    <SelectShell
                      ariaLabel="Replacement Provider"
                      value={replacementDefaultProvider}
                      options={otherConnected.map(p => ({value: p.id, label: p.label}))}
                      onChange={val => {
                        setReplacementDefaultProvider(val);
                        const prov = otherConnected.find(p => p.id === val);
                        setReplacementDefaultModel(prov?.lastModelId || prov?.models[0]?.id || '');
                      }}
                    />
                  </div>
                </div>
              )}
            </div>
          )}

          {affectedTasks.length > 0 && (
            <div className="rounded-lg border border-border/60 bg-muted/30 p-3 text-xs space-y-2">
              <p className="font-semibold text-foreground">Assigned Tasks ({affectedTasks.length})</p>
              <p className="text-muted-foreground">The following tasks are configured to use this provider and must be reassigned.</p>
              <div className="space-y-2 pt-1">
                {affectedTasks.map(t => {
                  const taskDef = TASK_DEFINITIONS.find(td => td.id === t.taskId);
                  const currentRep = taskReplacements[t.taskId] || { inheritDefault: true };
                  return (
                    <div key={t.taskId} className="flex items-center justify-between gap-2 border-t border-border/40 pt-2">
                      <span className="font-medium text-foreground">{taskDef?.name || t.taskId}</span>
                      <SelectShell
                        ariaLabel={`Reassign ${taskDef?.name || t.taskId}`}
                        value={currentRep.inheritDefault ? '' : `${currentRep.providerId}::${currentRep.modelId}`}
                        options={[
                          { value: '', label: 'Use Default' },
                          ...otherConnected.flatMap(p =>
                            p.models.map(m => ({
                              value: `${p.id}::${m.id}`,
                              label: `${p.label}: ${m.label || m.id}`
                            }))
                          )
                        ]}
                        onChange={val => {
                          if (!val) {
                            setTaskReplacements(prev => ({ ...prev, [t.taskId]: { inheritDefault: true } }));
                          } else {
                            const [pid, mid] = val.split('::');
                            setTaskReplacements(prev => ({ ...prev, [t.taskId]: { inheritDefault: false, providerId: pid, modelId: mid } }));
                          }
                        }}
                      />
                    </div>
                  );
                })}
              </div>
            </div>
          )}
        </div>

        <DialogFooter className="flex gap-2">
          <Button variant="outline" onClick={onClose} disabled={disconnecting}>
            Cancel
          </Button>
          <Button
            variant="destructive"
            onClick={handleConfirm}
            disabled={disconnecting || (isDefault && otherConnected.length === 0)}
          >
            {disconnecting ? <Loader2 className="size-3.5 animate-spin mr-1.5" /> : null}
            Disconnect
          </Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  );
}

function BillingPane({pane, store}: {pane: PaneDefinition; store: MahoSettingsStore}) {
  const state = useSettingsStoreState(store);
  const account = state.accountStatus;
  const billing = state.billingInfo;
  const error = state.billingError;
  const loading = state.billingLoading;
  const invoices = state.invoices;
  const [purchaseError, setPurchaseError] = useState<string | null>(null);
  const [purchasing, setPurchasing] = useState(false);
  const [subscriptionError, setSubscriptionError] = useState<string | null>(null);
  const [purchasingTier, setPurchasingTier] = useState<PlanTier | null>(null);

  useEffect(() => {
    void store.refreshAccountStatus();
  }, [store]);

  useEffect(() => {
    if (account?.signedIn) {
      void store.refreshBilling();
      void store.refreshInvoices();
    }
  }, [account?.signedIn, store]);

  useEffect(() => {
    if (!account?.signedIn) {
      return undefined;
    }
    const handleVisibility = () => {
      if (document.visibilityState === 'visible') {
        void store.refreshBilling();
      }
    };
    document.addEventListener('visibilitychange', handleVisibility);
    return () => document.removeEventListener('visibilitychange', handleVisibility);
  }, [account?.signedIn, store]);

  const handleBuyCredits = async () => {
    setPurchaseError(null);
    setPurchasing(true);
    try {
      const checkoutUrl = await store.getBuyCreditsUrl();
      if (!checkoutUrl) {
        setPurchaseError('Unable to generate a checkout URL. Please try again.');
        return;
      }
      await openCheckoutOverlay({
        checkoutUrl,
        onSuccess: () => {
          void store.refreshBilling();
          void store.refreshInvoices();
        },
        onError: (err) => {
          console.warn('[maho-settings] LemonSqueezy overlay failed, using fallback', err);
        },
      });
    } catch (err) {
      const message = err instanceof Error ? err.message : String(err);
      setPurchaseError(`Purchase failed: ${message}`);
    } finally {
      setPurchasing(false);
    }
  };

  const openBillingPortal = async () => {
    try {
      const {url} = await store.getHandler().getBillingPortalUrl();
      if (url) {
        window.open(url, '_blank', 'noopener');
      } else {
        window.open('https://app.lemonsqueezy.com/my-orders', '_blank', 'noopener');
      }
    } catch {
      window.open('https://app.lemonsqueezy.com/my-orders', '_blank', 'noopener');
    }
  };

  if (!account?.signedIn) {
    return (
      <PaneShell pane={pane}>
        <SectionCard
          title="Sign in to manage your subscription"
          description="You'll see plan details, credits, and invoices here after signing in."
          className="max-w-xl"
          padded
        >
          <Button type="button" onClick={() => store.selectPane('account')}>
            Go to sign in
          </Button>
        </SectionCard>
      </PaneShell>
    );
  }

  if (loading && !billing) {
    return (
      <PaneShell pane={pane}>
        <PaneLoading message="Loading billing details…" />
      </PaneShell>
    );
  }

  if (error) {
    return (
      <PaneShell pane={pane}>
        <PaneRetry message={error} title="Connection lost" onRetry={() => void store.refreshBilling()} />
      </PaneShell>
    );
  }

  if (!billing) {
    return (
      <PaneShell pane={pane}>
        <PaneLoading message="Loading billing details…" />
      </PaneShell>
    );
  }

  const tier = billing.tier || account.tier;
  const tierLabel = formatPlanLabel(tier);
  const renewDate = formatBillingDate(
      billing.subscriptionExpiresAt || account.subscriptionExpiresAt);
  const creditLimit = billing.tierCeilingUsd;
  const creditBalance = billing.creditBalanceUsd;
  const remainingPct = creditLimit > 0 ?
      Math.max(0, Math.min(100, Math.round((creditBalance / creditLimit) * 1000) / 10)) :
      0;
  const progressValue = creditLimit > 0 ? remainingPct : creditBalance > 0 ? 100 : 0;
  const statusBadgeVariant: 'success' | 'secondary' =
      billing.subscriptionStatus === 'active' ? 'success' : 'secondary';
  const currentPlanTier: PlanTier =
      tier === 'pro' || tier === 'max' ? tier : 'free';

  const handlePlanSelect = async (selectedTier: PlanTier) => {
    if (selectedTier === currentPlanTier) {
      return;
    }
    if ((currentPlanTier === 'max' && selectedTier !== 'max') ||
        (currentPlanTier === 'pro' && selectedTier === 'free')) {
      await openBillingPortal();
      return;
    }
    if (selectedTier === 'free') {
      return;
    }

    setSubscriptionError(null);
    setPurchasingTier(selectedTier);
    try {
      const checkoutUrl = await store.getSubscriptionCheckoutUrl(selectedTier);
      if (!checkoutUrl) {
        setSubscriptionError('Unable to generate a checkout URL. Please try again.');
        return;
      }
      await openCheckoutOverlay({
        checkoutUrl,
        onSuccess: () => {
          void store.refreshAccountStatus();
          void store.refreshBilling();
          void store.refreshInvoices();
        },
        onError: (err) => {
          console.warn('[maho-settings] LemonSqueezy overlay failed, using fallback', err);
        },
      });
    } catch (err) {
      const message = err instanceof Error ? err.message : String(err);
      setSubscriptionError(`Plan checkout failed: ${message}`);
    } finally {
      setPurchasingTier(null);
    }
  };

  return (
    <PaneShell pane={pane}>
      <SectionCard
        title="Overview"
        description="Plan details, renewal timing, and payment status for your Maho account."
        action={
          <div className="flex items-center gap-2">
            <Badge variant={statusBadgeVariant}>{billing.subscriptionStatus || 'No subscription'}</Badge>
            <Button size="sm" type="button" variant="outline" onClick={openBillingPortal}>
              Adjust plan
              <ArrowUpRight className="size-3.5" />
            </Button>
          </div>
        }
        className="mb-6"
        padded
      >
        <div className="grid gap-3 sm:grid-cols-2">
          <div className="rounded-xl border border-border/70 bg-background/60 p-4">
            <p className="text-sm font-medium text-muted-foreground">Plan</p>
            <div className="mt-3 flex flex-wrap items-center gap-2">
              <p className="text-lg font-semibold tracking-tight">{tierLabel}</p>
              <Badge variant={tier && tier !== 'free' ? 'default' : 'secondary'}>{tierLabel} plan</Badge>
            </div>
            <p className="mt-2 text-sm text-muted-foreground">
              {renewDate ? `Auto-renews on ${renewDate}.` : 'Renewal details will appear once your subscription is active.'}
            </p>
          </div>
          <div className="rounded-xl border border-border/70 bg-background/60 p-4">
            <p className="text-sm font-medium text-muted-foreground">Payment method</p>
            <p className="mt-3 text-lg font-semibold tracking-tight">
              {billing.hasPaymentMethod ? 'Payment method on file' : 'No payment method'}
            </p>
            <p className="mt-2 text-sm text-muted-foreground">Update cards, receipts, and billing details via our payment provider.</p>
            <Button className="mt-4" size="sm" type="button" variant="outline" onClick={openBillingPortal}>
              Manage
              <ArrowUpRight className="size-3.5" />
            </Button>
          </div>
        </div>
      </SectionCard>

      <SectionCard
        title="Managed AI plans"
        description="Choose the monthly Managed AI plan that fits your usage."
        className="mb-6"
        padded
      >
        <PlanPicker
          currentTier={currentPlanTier}
          busyTier={purchasingTier}
          freeLabel="Free plan"
          onSelect={tier => { void handlePlanSelect(tier); }}
        />
        {subscriptionError ? (
          <Alert variant="destructive" className="mt-4">
            <AlertTitle>Plan checkout failed</AlertTitle>
            <AlertDescription>{subscriptionError}</AlertDescription>
          </Alert>
        ) : null}
      </SectionCard>

      <SectionCard
        title="Credits"
        description={creditLimit > 0 ? `${remainingPct}% remaining` : 'Current available balance'}
        action={
          <Badge variant={progressValue > 20 ? 'success' : progressValue > 5 ? 'warning' : 'destructive'}>
            {formatUsd(creditBalance)}
          </Badge>
        }
        className="mb-6"
        padded
      >
        <div className="grid gap-4 rounded-xl border border-border/70 bg-background/60 p-4">
          <div className="flex flex-wrap items-end justify-between gap-3">
            <div>
              <p className="text-3xl font-semibold tracking-tight">
                {creditLimit > 0 ? `${remainingPct}%` : formatUsd(creditBalance)}
              </p>
              <p className="mt-1 text-sm text-muted-foreground">
                {creditLimit > 0 ? `${formatUsd(creditBalance)} of ${formatUsd(creditLimit)} remaining.` : 'Your current balance is available for usage.'}
              </p>
            </div>
            {loading ? <Badge variant="secondary">Refreshing…</Badge> : null}
          </div>
          <Progress value={progressValue} />
          <div className="flex flex-wrap items-center justify-between gap-3 text-sm text-muted-foreground">
            <span>
              {renewDate ? `Resets to ${formatUsd(creditLimit)} on ${renewDate}.` : 'Your next reset date will appear here.'}
            </span>
            {billing.lifetimePurchasedUsd > 0 ? (
              <span>Purchased {formatUsd(billing.lifetimePurchasedUsd)} total</span>
            ) : null}
          </div>
        </div>
      </SectionCard>

      <SectionCard
        title="Buy Credits"
        description="Pay-as-you-go credits that never expire. Choose any amount at checkout; credits cover overflow when your subscription ceiling is reached, or work standalone with no subscription."
        className="mb-6"
        padded
      >
        <button
          type="button"
          disabled={purchasing}
          onClick={() => void handleBuyCredits()}
          className="flex w-full flex-col items-start gap-1 rounded-xl border border-foreground bg-foreground p-4 text-left text-background transition-colors hover:opacity-90 disabled:cursor-not-allowed disabled:opacity-60"
        >
          <span className="text-2xl font-semibold tracking-tight">Buy credits</span>
          <span className="text-xs opacity-80">
            Pay what you want — enter any amount (min $10) at checkout.
          </span>
          {purchasing ? (
            <span className="text-xs opacity-70">Opening…</span>
          ) : null}
        </button>
        {purchaseError ? (
          <div className="mt-4">
            <Alert className="border-destructive/60 bg-destructive/10 text-destructive shadow-sm" variant="destructive">
              <AlertCircle className="size-4" />
              <AlertTitle>Purchase failed</AlertTitle>
              <AlertDescription>{purchaseError}</AlertDescription>
            </Alert>
          </div>
        ) : null}
        <p className="text-xs text-muted-foreground mt-4">
          Credits are consumed at a 30% markup over usage cost. Credits never expire. See ADR 0010 for details.
        </p>
      </SectionCard>

      <SectionCard
        title="Invoices"
        description="Receipts and invoices will appear here after your first paid charge."
        action={state.invoicesLoading && <Badge variant="secondary">Loading…</Badge>}
        padded
      >
        {state.invoicesError ? (
          <Alert className="border-destructive/60 bg-destructive/10 text-destructive shadow-sm" variant="destructive">
            <AlertCircle className="size-4" />
            <AlertTitle>Failed to load invoices</AlertTitle>
            <AlertDescription>{state.invoicesError}</AlertDescription>
          </Alert>
        ) : invoices.length === 0 ? (
          <div className="grid min-h-36 place-items-center rounded-xl border border-dashed border-border/70 bg-background/40 px-6 py-10 text-center">
            <div className="space-y-1">
              <p className="text-base font-semibold tracking-tight">No invoices yet</p>
              <p className="text-sm text-muted-foreground">Invoices will appear after your first payment.</p>
            </div>
          </div>
        ) : (
          <div className="overflow-x-auto rounded-xl border border-border/70 bg-background/60">
            <table className="w-full text-left border-collapse text-sm">
              <thead>
                <tr className="border-b border-border/60 bg-muted/30 text-muted-foreground font-medium">
                  <th className="p-3">Date</th>
                  <th className="p-3">Amount</th>
                  <th className="p-3">Status</th>
                  <th className="p-3 text-right">PDF</th>
                </tr>
              </thead>
              <tbody className="divide-y divide-border/40">
                {invoices.map(invoice => (
                  <tr key={invoice.id} className="hover:bg-muted/10 transition-colors">
                    <td className="p-3 font-medium whitespace-nowrap">
                      {formatBillingDate(invoice.createdAt) || 'N/A'}
                    </td>
                    <td className="p-3 whitespace-nowrap">
                      {formatUsd(invoice.amountUsd)} {invoice.currency?.toUpperCase() || 'USD'}
                    </td>
                    <td className="p-3 whitespace-nowrap">
                      <Badge
                        variant={
                          invoice.status === 'paid'
                            ? 'success'
                            : invoice.status === 'void' || invoice.status === 'failed'
                            ? 'destructive'
                            : 'warning'
                        }
                      >
                        {invoice.status}
                      </Badge>
                    </td>
                    <td className="p-3 text-right whitespace-nowrap">
                      {invoice.pdfUrl ? (
                        <Button
                          size="sm"
                          variant="ghost"
                          type="button"
                          onClick={() => window.open(invoice.pdfUrl, '_blank', 'noopener')}
                          title="Download PDF Invoice"
                        >
                          <Download className="size-3.5 mr-1" />
                          PDF
                        </Button>
                      ) : (
                        <span className="text-muted-foreground text-xs">—</span>
                      )}
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        )}
      </SectionCard>
    </PaneShell>
  );
}

// Sentinel value used by GetAISettings to indicate "a key is stored but its
// value is intentionally hidden from the WebUI". If the user clicks Save
// without editing the field, we must NOT submit this literal value, or it
function AiSelectRow(
    {initialValue, label, onCommit, options}: {
      initialValue: string;
      label: string;
      onCommit: (value: string) => Promise<void>;
      options: Array<{value: string; label: string}>;
    }) {
  const [draft, setDraft] = useState(initialValue);

  useEffect(() => {
    setDraft(initialValue);
  }, [initialValue]);
  return (
    <ManagedSettingRow description="" title={label}>
      <SelectShell
        ariaLabel={label}
        options={options}
        value={draft}
        onChange={value => {
          setDraft(value);
          void onCommit(value);
        }}
      />
    </ManagedSettingRow>
  );
}

function RemoteDeviceRow({
  device,
  editing,
  onStartEdit,
  onCancelEdit,
  onSaveRename,
  confirmingDisconnect,
  onStartDisconnect,
  onCancelDisconnect,
  onConfirmDisconnect,
  renameValue,
  onRenameValueChange,
  renameError,
}: {
  device: SyncDeviceInfo;
  editing: boolean;
  onStartEdit: () => void;
  onCancelEdit: () => void;
  onSaveRename: () => Promise<void>;
  confirmingDisconnect: boolean;
  onStartDisconnect: () => void;
  onCancelDisconnect: () => void;
  onConfirmDisconnect: () => Promise<void>;
  renameValue: string;
  onRenameValueChange: (val: string) => void;
  renameError: string | null;
}) {
  if (editing) {
    return (
      <ManagedSettingRow
        controlClassName={SETTING_ACTIONS_CLASS}
        description={
          <div className="flex flex-col gap-1 w-full">
            <div className="flex gap-2 items-center">
              <TextFieldShell
                ariaLabel="New device name"
                placeholder="New name"
                value={renameValue}
                onChange={onRenameValueChange}
              />
              <ActionButton label="Save" onClick={onSaveRename} />
              <ActionButton label="Cancel" onClick={onCancelEdit} />
            </div>
            {renameError && <span className="text-xs text-destructive">{renameError}</span>}
          </div>
        }
        title="Rename Device"
      />
    );
  }

  if (confirmingDisconnect) {
    return (
      <ManagedSettingRow
        controlClassName={SETTING_ACTIONS_CLASS}
        description="Are you sure you want to disconnect this device?"
        title={`Disconnect ${device.name}?`}
      >
        <ActionButton label="Confirm" onClick={onConfirmDisconnect} />
        <ActionButton label="Cancel" onClick={onCancelDisconnect} />
      </ManagedSettingRow>
    );
  }

  return (
    <ManagedSettingRow
      controlClassName={SETTING_ACTIONS_CLASS}
      description={`${device.deviceType} · ${device.isOnline ? 'Online' : 'Offline'}`}
      title={device.name}
    >
      <ActionButton label="Rename" onClick={onStartEdit} />
      <ActionButton label="Disconnect" onClick={onStartDisconnect} />
    </ManagedSettingRow>
  );
}

function SyncConnectionSection({store}: {store: MahoSettingsStore}) {
  const handler = store.getHandler();
  const [joinPhrase, setJoinPhrase] = useState('');
  const [pendingKeyInfo, setPendingKeyInfo] = useState<SyncKeyInfo | null>(null);
  const [state, setState] = useState<LoadState<SyncPaneData>>(createLoadingState());

  const [editingDeviceId, setEditingDeviceId] = useState<string | null>(null);
  const [renameName, setRenameName] = useState('');
  const [renameError, setRenameError] = useState<string | null>(null);
  const [disconnectingDeviceId, setDisconnectingDeviceId] = useState<string | null>(null);
  const [syncOperationError, setSyncOperationError] = useState<string | null>(null);

  const loadStatus = useCallback(async () => {
    setState(createLoadingState());
    try {
      const {status} = await handler.getSyncStatus();
      let devices: SyncDeviceInfo[] = [];
      let devicesError: string | null = null;
      let devicesStatus: 'idle' | 'loaded' = 'idle';
      if (status.isSyncing) {
        try {
          const result = await handler.getSyncDevices();
          devices = result.devices;
          devicesStatus = 'loaded';
        } catch {
          devicesError = 'Failed to load connected devices.';
          devicesStatus = 'loaded';
        }
      }

      setPendingKeyInfo(null);
      setState(createLoadedState({devices, devicesError, devicesStatus, status}));
    } catch {
      setState(createErrorState('Failed to load sync status.'));
    }
  }, [handler]);

  useEffect(() => {
    void loadStatus();
  }, [loadStatus]);

  const handleStartRename = (device: SyncDeviceInfo) => {
    setEditingDeviceId(device.id);
    setRenameName(device.name);
    setRenameError(null);
    setSyncOperationError(null);
  };

  const handleSaveRename = async (device: SyncDeviceInfo) => {
    const trimmed = renameName.trim();
    if (!trimmed) {
      setRenameError("Device name cannot be empty.");
      return;
    }
    try {
      const {success} = await handler.renameSyncDevice(device.id, trimmed);
      if (success) {
        setEditingDeviceId(null);
        await loadStatus();
      } else {
        setRenameError("Failed to rename device.");
      }
    } catch {
      setRenameError("Failed to rename device.");
    }
  };

  const handleConfirmDisconnect = async (device: SyncDeviceInfo) => {
    try {
      const {success} = await handler.disconnectSyncDevice(device.id);
      if (success) {
        setDisconnectingDeviceId(null);
        await loadStatus();
      } else {
        setSyncOperationError("Failed to disconnect device.");
      }
    } catch {
      setSyncOperationError("Failed to disconnect device.");
    }
  };

  return (
    <>
      {state.status === 'loading' ? <PaneLoading message="Loading sync status…" /> : null}
      {state.status === 'error' ? (
        <PaneRetry message={state.message} title="Sync unavailable" onRetry={() => void loadStatus()} />
      ) : null}
      {state.status === 'loaded' ? (
        <SectionCard title="Sync" description="Sync your data across devices with an end-to-end encrypted recovery phrase.">
          {syncOperationError && (
            <div className="text-sm text-destructive px-4 py-2 border-b bg-destructive/10">{syncOperationError}</div>
          )}
          <ReadOnlySettingRow
            description={state.data.status.isSyncing ?
              `Syncing · ${state.data.status.statusLabel}` :
              `Not connected · ${state.data.status.statusLabel || 'idle'}`}
            title="Status"
          />
          {state.data.status.errorMessage ? (
            <ReadOnlySettingRow description={state.data.status.errorMessage} title="Error" />
          ) : null}
          {state.data.status.isSyncing ? (
            <>
              {state.data.devices.length > 0 ? (
                <ReadOnlySettingRow description="" title="Connected devices" />
              ) : null}
              {state.data.devices.map(device => (
                <RemoteDeviceRow
                  key={device.id}
                  device={device}
                  editing={editingDeviceId === device.id}
                  onStartEdit={() => handleStartRename(device)}
                  onCancelEdit={() => setEditingDeviceId(null)}
                  onSaveRename={() => handleSaveRename(device)}
                  confirmingDisconnect={disconnectingDeviceId === device.id}
                  onStartDisconnect={() => {
                    setDisconnectingDeviceId(device.id);
                    setSyncOperationError(null);
                  }}
                  onCancelDisconnect={() => setDisconnectingDeviceId(null)}
                  onConfirmDisconnect={() => handleConfirmDisconnect(device)}
                  renameValue={renameName}
                  onRenameValueChange={setRenameName}
                  renameError={renameError}
                />
              ))}
              {state.data.devices.length > 0 && (
                <p className="text-xs text-muted-foreground mt-2 px-4 pb-4">
                  Note: Remote rename and disconnect are live-session best-effort and may reset after reconnect.
                </p>
              )}
              {state.data.devicesError ? (
                <ReadOnlySettingRow description={state.data.devicesError} title="Devices" />
              ) : null}
              <ManagedSettingRow controlClassName={SETTING_ACTIONS_CLASS} description="Disconnect this browser from sync." title="Stop Sync">
                <ActionButton label="Stop Sync" onClick={async () => {
                  await handler.stopSync();
                  await loadStatus();
                }} />
              </ManagedSettingRow>
            </>
          ) : (
            <>
              <ManagedSettingRow
                controlClassName={SETTING_ACTIONS_CLASS}
                description="Generate a recovery phrase for a new sync session."
                title="Start new sync">
                <ActionButton label={pendingKeyInfo ? 'Generate new phrase' : 'Generate recovery phrase'} onClick={async () => {
                  const {keyInfo} = await handler.generateSyncKey();
                  setPendingKeyInfo(keyInfo);
                }} />
              </ManagedSettingRow>
              {pendingKeyInfo ? (
                <SyncRecoveryPhraseRow keyInfo={pendingKeyInfo} onContinue={async () => {
                  await handler.configureSyncEncryption(pendingKeyInfo.recoveryPhrase);
                  await loadStatus();
                }} />
              ) : null}
              <ManagedSettingRow controlClassName={SETTING_ACTIONS_CLASS} description="Join an existing sync session with a recovery phrase." title="Join existing sync">
                <TextFieldShell
                  ariaLabel="Recovery phrase"
                  placeholder="Recovery phrase"
                  value={joinPhrase}
                  onChange={setJoinPhrase}
                />
                <ActionButton label="Join" onClick={async () => {
                  const phrase = joinPhrase.trim();
                  if (!phrase) {
                    return;
                  }
                  await handler.joinSync(phrase);
                  setJoinPhrase('');
                  await loadStatus();
                }} />
              </ManagedSettingRow>
            </>
          )}
        </SectionCard>
      ) : null}
    </>
  );
}

function SyncRecoveryPhraseRow(
    {keyInfo, onContinue}: {
      keyInfo: SyncKeyInfo;
      onContinue: () => Promise<void>;
    }) {
  const [copyLabel, setCopyLabel] = useState('Copy');
  const resetTimeoutRef = useRef<number | null>(null);
  const mountedRef = useMountedRef();

  useEffect(() => {
    return () => {
      if (resetTimeoutRef.current !== null) {
        window.clearTimeout(resetTimeoutRef.current);
      }
    };
  }, []);

  return (
    <ManagedSettingRow controlClassName={SETTING_ACTIONS_CLASS} description={keyInfo.recoveryPhrase} title="Recovery Phrase">
      {navigator.clipboard?.writeText ? (
        <ActionButton label={copyLabel} onClick={async () => {
          await navigator.clipboard.writeText(keyInfo.recoveryPhrase);
          if (mountedRef.current) {
            setCopyLabel('Copied');
          }
          if (resetTimeoutRef.current !== null) {
            window.clearTimeout(resetTimeoutRef.current);
          }
          resetTimeoutRef.current = window.setTimeout(() => {
            resetTimeoutRef.current = null;
            if (mountedRef.current) {
              setCopyLabel('Copy');
            }
          }, 1200);
        }} />
      ) : null}
      <ActionButton label="Continue" onClick={onContinue} />
    </ManagedSettingRow>
  );
}


function AutofillPane({pane, store}: {pane: PaneDefinition; store: MahoSettingsStore}) {
  const handler = store.getHandler();
  const [addressForm, setAddressForm] = useState({
    addressLine1: '',
    addressLine2: '',
    city: '',
    country: '',
    email: '',
    name: '',
    phone: '',
    postalCode: '',
    state: '',
  });
  const [paymentForm, setPaymentForm] = useState({
    cardNetwork: '',
    cardholderName: '',
    expiration: '',
    lastFour: '',
  });
  const [state, setState] = useState<LoadState<AutofillPaneData>>(createLoadingState());

  const loadAutofill = useCallback(async () => {
    setState(createLoadingState());
    try {
      const [{addresses}, {payments}] = await Promise.all([
        handler.getAutofillAddresses(),
        handler.getAutofillPayments(),
      ]);
      setState(createLoadedState({addresses, payments}));
    } catch {
      setState(createErrorState('Failed to load autofill data.'));
    }
  }, [handler]);

  useEffect(() => {
    void loadAutofill();
  }, [loadAutofill]);

  return (
    <PaneShell pane={pane}>
      {state.status === 'loading' ? <PaneLoading message="Loading autofill data…" /> : null}
      {state.status === 'error' ? (
        <PaneRetry message={state.message} title="Autofill unavailable" onRetry={() => void loadAutofill()} />
      ) : null}
      {state.status === 'loaded' ? (
        <>
          <SectionCard title="Saved addresses" className="mb-6">
            {state.data.addresses.length === 0 ? <EmptyDescription message="No saved addresses." /> : null}
            {state.data.addresses.map((address) => {
              const locationParts = [
                address.addressLine1,
                address.addressLine2,
                [address.city, address.state, address.postalCode].filter(Boolean).join(' '),
                address.country,
              ].filter(Boolean);
              const contactParts = [address.phone, address.email].filter(Boolean);
              const descriptionParts: string[] = [];
              if (locationParts.length > 0) {
                descriptionParts.push(locationParts.join(', '));
              }
              if (contactParts.length > 0) {
                descriptionParts.push(contactParts.join(' · '));
              }

              return (
                <ManagedSettingRow
                  key={address.id}
                  controlClassName={SETTING_ACTIONS_CLASS}
                  description={descriptionParts.join(' · ') || 'No address details'}
                  title={address.name || address.email || address.addressLine1 || 'Unnamed address'}>
                  <ActionButton
                    ariaLabel={`Delete address ${address.name || address.addressLine1 || address.id}`}
                    label="Delete"
                    onClick={async () => {
                      await handler.deleteAutofillAddress(address.id);
                      await loadAutofill();
                    }}
                  />
                </ManagedSettingRow>
              );
            })}
            <ManagedSettingRow
              controlClassName={SETTING_CONTROL_STACKED_CLASS}
              description="Create a new saved address for form autofill. Fields left blank stay empty."
              title="Add address">
              <div className={SETTING_CONTROL_STACK_CLASS}>
                <TextFieldShell ariaLabel="Address full name" placeholder="Full name" value={addressForm.name} onChange={value => setAddressForm(current => ({...current, name: value}))} />
                <TextFieldShell ariaLabel="Address line 1" placeholder="Address line 1" value={addressForm.addressLine1} onChange={value => setAddressForm(current => ({...current, addressLine1: value}))} />
                <TextFieldShell ariaLabel="Address line 2" placeholder="Address line 2" value={addressForm.addressLine2} onChange={value => setAddressForm(current => ({...current, addressLine2: value}))} />
                <TextFieldShell ariaLabel="Address city" placeholder="City" value={addressForm.city} onChange={value => setAddressForm(current => ({...current, city: value}))} />
                <TextFieldShell ariaLabel="Address state or region" placeholder="State or region" value={addressForm.state} onChange={value => setAddressForm(current => ({...current, state: value}))} />
                <TextFieldShell ariaLabel="Address postal code" placeholder="Postal code" value={addressForm.postalCode} onChange={value => setAddressForm(current => ({...current, postalCode: value}))} />
                <TextFieldShell ariaLabel="Address country" placeholder="Country" value={addressForm.country} onChange={value => setAddressForm(current => ({...current, country: value}))} />
                <TextFieldShell ariaLabel="Address phone number" placeholder="Phone" value={addressForm.phone} onChange={value => setAddressForm(current => ({...current, phone: value}))} />
                <TextFieldShell ariaLabel="Address email" placeholder="Email" value={addressForm.email} onChange={value => setAddressForm(current => ({...current, email: value}))} />
              </div>
              <div className={SETTING_ACTIONS_CLASS}>
                <ActionButton label="Add address" onClick={async () => {
                  const values = {
                    addressLine1: addressForm.addressLine1.trim(),
                    addressLine2: addressForm.addressLine2.trim(),
                    city: addressForm.city.trim(),
                    country: addressForm.country.trim(),
                    email: addressForm.email.trim(),
                    name: addressForm.name.trim(),
                    phone: addressForm.phone.trim(),
                    postalCode: addressForm.postalCode.trim(),
                    state: addressForm.state.trim(),
                  };
                  if (!Object.values(values).some(Boolean)) {
                    return;
                  }
                  await handler.addAutofillAddress(
                      createTransientId('autofill-address'), values.name,
                      values.addressLine1, values.addressLine2, values.city,
                      values.state, values.postalCode, values.country, values.phone,
                      values.email);
                  setAddressForm({addressLine1: '', addressLine2: '', city: '', country: '', email: '', name: '', phone: '', postalCode: '', state: ''});
                  await loadAutofill();
                }} />
              </div>
            </ManagedSettingRow>
          </SectionCard>

          <SectionCard title="Payment methods">
            {state.data.payments.length === 0 ? <EmptyDescription message="No saved payment methods." /> : null}
            {state.data.payments.map((payment) => {
              const descriptionParts = [payment.cardholderName, payment.expiration ? `Exp ${payment.expiration}` : ''].filter(Boolean);
              return (
                <ManagedSettingRow
                  key={payment.id}
                  controlClassName={SETTING_ACTIONS_CLASS}
                  description={descriptionParts.join(' · ') || 'No details'}
                  title={`${payment.cardNetwork || 'Card'} •••• ${payment.lastFour || '????'}`}>
                  <ActionButton
                    ariaLabel={`Delete payment method ${payment.cardholderName || payment.lastFour || payment.id}`}
                    label="Delete"
                    onClick={async () => {
                      await handler.deleteAutofillPayment(payment.id);
                      await loadAutofill();
                    }}
                  />
                </ManagedSettingRow>
              );
            })}
            <ManagedSettingRow
              controlClassName={SETTING_CONTROL_STACKED_CLASS}
              description="Store a payment method for faster checkout autofill."
              title="Add payment method">
              <div className={SETTING_CONTROL_STACK_CLASS}>
                <TextFieldShell ariaLabel="Payment card network" placeholder="Card network" value={paymentForm.cardNetwork} onChange={value => setPaymentForm(current => ({...current, cardNetwork: value}))} />
                <TextFieldShell ariaLabel="Payment card last four digits" placeholder="Last four digits" value={paymentForm.lastFour} onChange={value => setPaymentForm(current => ({...current, lastFour: value}))} />
                <TextFieldShell ariaLabel="Payment card expiration" placeholder="Expiration (MM/YY)" value={paymentForm.expiration} onChange={value => setPaymentForm(current => ({...current, expiration: value}))} />
                <TextFieldShell ariaLabel="Payment cardholder name" placeholder="Cardholder name" value={paymentForm.cardholderName} onChange={value => setPaymentForm(current => ({...current, cardholderName: value}))} />
              </div>
              <div className={SETTING_ACTIONS_CLASS}>
                <ActionButton label="Add payment" onClick={async () => {
                  const values = {
                    cardNetwork: paymentForm.cardNetwork.trim(),
                    cardholderName: paymentForm.cardholderName.trim(),
                    expiration: paymentForm.expiration.trim(),
                    lastFour: paymentForm.lastFour.trim(),
                  };
                  if (!Object.values(values).some(Boolean)) {
                    return;
                  }
                  await handler.addAutofillPayment(
                      createTransientId('autofill-payment'), values.cardNetwork,
                      values.lastFour, values.expiration, values.cardholderName);
                  setPaymentForm({cardNetwork: '', cardholderName: '', expiration: '', lastFour: ''});
                  await loadAutofill();
                }} />
              </div>
            </ManagedSettingRow>
          </SectionCard>
        </>
      ) : null}
    </PaneShell>
  );
}

interface ContentBlockingModeOption {
  readonly value: number;
  readonly title: string;
  readonly description: string;
  readonly icon: typeof Zap;
}

const CONTENT_BLOCKING_MODE_OPTIONS: ContentBlockingModeOption[] = [
  {value: 0, title: 'Native', description: 'High-performance built-in Rust adblock engine.', icon: Zap},
  {value: 1, title: 'Extension', description: 'Third-party extension DNR blocking (e.g. uBlock Origin Lite).', icon: Puzzle},
  {value: 2, title: 'Disabled', description: 'Content blocking turned off completely.', icon: ShieldOff},
];

function ContentBlockingModeOptionCard(
    {option, selected, disabled, onSelect}: {
      option: ContentBlockingModeOption;
      selected: boolean;
      disabled: boolean;
      onSelect: () => void;
    }) {
  const Icon = option.icon;
  return (
    <label
      data-setting-scope="process"
      className={cn(
        'flex w-full items-start gap-3 rounded-lg border p-3 text-left transition-colors',
        disabled ? 'cursor-not-allowed opacity-60' : 'cursor-pointer',
        selected
          ? 'border-primary bg-primary/5 ring-1 ring-primary'
          : 'border-border/60 hover:bg-surface-hover',
      )}>
      <input
        type="radio"
        name="contentBlockingMode"
        className="sr-only"
        checked={selected}
        disabled={disabled}
        onChange={onSelect}
      />
      <span className={cn(
        'mt-0.5 flex size-8 shrink-0 items-center justify-center rounded-md',
        selected ? 'bg-primary/10 text-primary' : 'bg-muted text-muted-foreground',
      )}>
        <Icon className="size-4" />
      </span>
      <span className="min-w-0 flex-1">
        <span className="flex items-center gap-2">
          <span className="text-sm font-medium text-foreground">{option.title}</span>
          {selected ? <Check className="size-4 text-primary" /> : null}
        </span>
        <span className="mt-0.5 block text-xs leading-5 text-muted-foreground">{option.description}</span>
      </span>
    </label>
  );
}

function ContentBlockerPrivacyCard(
    {settings, store}: {settings: SettingValue[]; store: MahoSettingsStore}) {
  const rows = PRIVACY_SETTING_KEYS.flatMap((key) => {
    const setting = settings.find(item => item.key === key);
    const meta = SETTING_METADATA[key] ?? null;
    if (!setting || !meta) {
      return [];
    }
    return [{key, setting, meta}];
  });

  if (rows.length === 0) {
    return null;
  }

  return (
    <SectionCard title="Privacy" description="Tracking and cookie protections." className="mb-6">
      {rows.map(({key, setting, meta}) => (
        <ManagedSettingRow key={key} description={meta.description} title={meta.label}>
          <Toggle
            enabled={setting.value === 'true'}
            label={meta.label}
            onToggle={async (next) => {
              await store.commitSettingValue(key, next ? 'true' : 'false');
            }}
          />
        </ManagedSettingRow>
      ))}
    </SectionCard>
  );
}

function ContentBlockerPane({pane, settings, store}: {pane: PaneDefinition; settings: SettingValue[]; store: MahoSettingsStore}) {
  const handler = store.getHandler();
  const settingsState = useSettingsStoreState(store);
  const gatedProfileTarget = !!settingsState.selectedProfileTarget &&
      settingsState.selectedProfileContext?.isHostProfile !== true;
  const mountedRef = useMountedRef();
  const [form, setForm] = useState({name: '', url: ''});
  const [actionError, setActionError] = useState<ContentBlockerActionError | null>(null);
  const [busyActions, setBusyActions] = useState<ReadonlySet<ContentBlockerMutationAction>>(
      new Set());
  const busyActionsRef = useRef(new Set<ContentBlockerMutationAction>());
  const [state, setState] = useState<LoadState<ContentBlockerPaneData>>(createLoadingState());
  const reqSeqRef = useRef(0);

  const loadContentBlocker = useCallback(async () => {
    const currentReq = ++reqSeqRef.current;
    if (mountedRef.current) {
      setState(createLoadingState());
    }
    try {
      const [{lists}, {stats}] = await Promise.all([
        handler.getFilterLists(),
        handler.getContentBlockerStats(),
      ]);
      if (mountedRef.current && reqSeqRef.current === currentReq) {
        setState(createLoadedState({lists, stats}));
      }
    } catch {
      if (mountedRef.current && reqSeqRef.current === currentReq) {
        setState(createErrorState('Failed to load content blocker.'));
      }
    }
  }, [handler, mountedRef]);

  const runMutation = useCallback(async (
      context: ContentBlockerMutationContext,
      mutation: () => Promise<boolean>): Promise<boolean> => {
    if (busyActionsRef.current.has(context.action)) {
      return false;
    }

    busyActionsRef.current.add(context.action);
    if (mountedRef.current) {
      setBusyActions(new Set(busyActionsRef.current));
      setActionError(null);
    }

    try {
      const success = await mutation();
      if (!success) {
        if (mountedRef.current) {
          setActionError(createContentBlockerActionError(context));
        }
        return false;
      }
      await loadContentBlocker();
      return true;
    } catch {
      if (mountedRef.current) {
        setActionError(createContentBlockerActionError(context));
      }
      return false;
    } finally {
      busyActionsRef.current.delete(context.action);
      if (mountedRef.current) {
        setBusyActions(new Set(busyActionsRef.current));
      }
    }
  }, [loadContentBlocker, mountedRef]);

  useEffect(() => {
    void loadContentBlocker();
    return () => {
      reqSeqRef.current++;
    };
  }, [loadContentBlocker]);

  return (
    <PaneShell pane={pane}>
      {state.status === 'loading' ? <PaneLoading message="Loading content blocker…" /> : null}
      {state.status === 'error' ? (
        <PaneRetry message={state.message} title="Content blocker unavailable" onRetry={() => void loadContentBlocker()} />
      ) : null}
      {state.status === 'loaded' ? (
        <>
          <SectionCard title="Content Blocking Mode" className="mb-6">
            <ManagedSettingRow
              controlClassName={SETTING_CONTROL_STACKED_CLASS}
              description={
                <div>
                  <div>Select how ad blocking and privacy protection are handled in Maho.</div>
                  {actionError?.action === 'mode' ? <ActionError message={actionError.message} /> : null}
                </div>
              }
              title="Blocking Mode">
              <div className="mt-2 flex flex-col gap-2" role="radiogroup" aria-label="Content blocking mode">
                {CONTENT_BLOCKING_MODE_OPTIONS.map(option => (
                  <ContentBlockingModeOptionCard
                    key={option.value}
                    option={option}
                    selected={state.data.stats.mode === option.value}
                    disabled={busyActions.has('mode')}
                    onSelect={() => {
                      if (state.data.stats.mode === option.value) {
                        return;
                      }
                      void runMutation(
                          {action: 'mode', targetId: null, targetLabel: null},
                          async () => (await handler.setContentBlockingMode(option.value)).success);
                    }}
                  />
                ))}
              </div>
              {state.data.stats.mode === 1 ? (
                <div className="mt-3 p-3 bg-muted/40 rounded-md border text-xs text-muted-foreground">
                  <p className="mb-2">Extensions manage content blocking via Declarative Net Request. Open extension management to configure installed blockers.</p>
                  <ActionButton label="Manage Extensions" onClick={() => handler.openExtensionsPage()} />
                </div>
              ) : null}
              {state.data.stats.mode === 2 ? (
                <div className="mt-3 p-3 bg-muted/40 rounded-md border text-xs text-muted-foreground">
                  <p>Native content blocking is disabled. Web requests and cosmetic resources pass through unfiltered.</p>
                </div>
              ) : null}
            </ManagedSettingRow>
          </SectionCard>

          {gatedProfileTarget ? (
            <Alert className="mb-6" data-selected-profile-limitation="true">
              <AlertCircle className="size-4" />
              <AlertTitle>
                {settingsState.selectedProfileLoading ? 'Confirming selected profile context' : 'Privacy controls are managed in Profiles'}
              </AlertTitle>
              <AlertDescription>
                {settingsState.selectedProfileLoading ?
                  'Host-bound privacy controls remain unavailable until the selected profile context is confirmed.' :
                  'The legacy privacy rows in this pane are bound to the profile hosting this Settings window. Use the selected profile editor for Do Not Track, third-party cookies, and Safe Browsing.'}
              </AlertDescription>
            </Alert>
          ) : <ContentBlockerPrivacyCard settings={settings} store={store} />}

          <SectionCard title="Engine Health & Status" className="mb-6">
            <ManagedSettingRow
              controlClassName={SETTING_ACTIONS_CLASS}
              description={
                <div>
                  <div>{state.data.stats.totalRuleCount.toLocaleString()} rules active · {state.data.stats.filterListCount} filter list(s) · Gen #{state.data.stats.engineGeneration}</div>
                  {state.data.stats.healthStatus ? (
                    <div className="text-xs text-muted-foreground mt-1">Health: {state.data.stats.healthStatus}</div>
                  ) : null}
                  {state.data.stats.overallError ? (
                    <div className="text-xs text-destructive mt-1">Error: {state.data.stats.overallError}</div>
                  ) : null}
                  {actionError?.action === 'update' || actionError?.action === 'rebuild' ? (
                    <ActionError message={actionError.message} />
                  ) : null}
                </div>
              }
              title="Engine status">
              <div className="flex gap-2">
                <ActionButton
                  label={busyActions.has('update') ? "Updating..." : "Check for Updates"}
                  disabled={state.data.stats.mode !== 0 || busyActions.has('update')}
                  onClick={() => runMutation(
                      {action: 'update', targetId: null, targetLabel: null},
                      async () => (await handler.triggerFilterUpdate(null)).success)
                      .then(() => undefined)}
                />
                <ActionButton
                  label="Rebuild Engine"
                   disabled={state.data.stats.mode !== 0 || busyActions.has('rebuild')}
                   onClick={() => runMutation(
                       {action: 'rebuild', targetId: null, targetLabel: null},
                       async () => (await handler.rebuildContentRules()).success)
                       .then(() => undefined)}
                />
              </div>
            </ManagedSettingRow>
          </SectionCard>

          <SectionCard title="Filter lists">
            {state.data.stats.mode !== 0 ? (
              <div className="p-3 mb-3 bg-muted/30 rounded border text-xs text-muted-foreground">
                Filter list management is disabled outside Native Mode. Select Native Mode above to enable list controls.
              </div>
            ) : null}
            {state.data.lists.length === 0 ? <EmptyDescription message="No filter lists configured." /> : null}
            {state.data.lists.map((filterList) => {
              const filterListLabel = filterList.name || filterList.id;
              const hasListError = actionError?.targetId === filterList.id &&
                  (actionError.action === 'toggle' || actionError.action === 'remove');
              return (
                <ManagedSettingRow
                  key={filterList.id}
                  controlClassName={SETTING_ACTIONS_CLASS}
                  description={
                    <div>
                      <div>{[filterList.url, `${filterList.ruleCount.toLocaleString()} rules`, filterList.enabled ? 'Enabled' : 'Disabled'].filter(Boolean).join(' · ')}</div>
                      {hasListError ? <ActionError message={actionError.message} /> : null}
                    </div>
                  }
                  title={filterListLabel}>
                  <Toggle
                    disabled={state.data.stats.mode !== 0 || busyActions.has('toggle')}
                    enabled={filterList.enabled}
                     label={`Enable ${filterListLabel}`}
                     onToggle={nextEnabled => runMutation(
                         {action: 'toggle', targetId: filterList.id, targetLabel: filterListLabel},
                         async () => (await handler.toggleFilterList(
                             filterList.id, nextEnabled)).success)
                         .then(() => undefined)}
                  />
                  <ActionButton
                    ariaLabel={`Remove filter list ${filterListLabel}`}
                    disabled={state.data.stats.mode !== 0 || busyActions.has('remove')}
                     label="Remove"
                     onClick={() => runMutation(
                         {action: 'remove', targetId: filterList.id, targetLabel: filterListLabel},
                         async () => (await handler.removeFilterList(filterList.id)).success)
                         .then(() => undefined)}
                  />
                </ManagedSettingRow>
              );
            })}
            <ManagedSettingRow
              controlClassName={SETTING_CONTROL_STACKED_CLASS}
              description={
                  <div>
                    <div>Add a custom filter list by name and source URL.</div>
                    {actionError?.action === 'add' ? <ActionError message={actionError.message} /> : null}
                  </div>
              }
              title="Add filter list">
              <div className={SETTING_CONTROL_STACK_CLASS}>
                <TextFieldShell ariaLabel="Filter list name" disabled={state.data.stats.mode !== 0} placeholder="Filter list name" value={form.name} onChange={value => setForm(current => ({...current, name: value}))} />
                <TextFieldShell ariaLabel="Filter list URL" disabled={state.data.stats.mode !== 0} placeholder="https://example.com/filters.txt" value={form.url} onChange={value => setForm(current => ({...current, url: value}))} />
              </div>
              <div className={SETTING_ACTIONS_CLASS}>
                <ActionButton disabled={state.data.stats.mode !== 0 || busyActions.has('add')} label="Add filter list" onClick={async () => {
                  const name = form.name.trim();
                  const url = form.url.trim();
                  if (!name || !url) {
                    return;
                  }
                  const success = await runMutation(
                      {action: 'add', targetId: null, targetLabel: name},
                      async () => (await handler.addFilterList(
                          createTransientId('filter-list'), name, url)).success);
                  if (success) {
                    setForm({name: '', url: ''});
                  }
                }} />
              </div>
            </ManagedSettingRow>
          </SectionCard>
        </>
      ) : null}
    </PaneShell>
  );
}

function ActionError({message}: {message: string}) {
  return (
    <div className="flex items-center gap-1.5 text-destructive mt-1.5 text-xs">
      <AlertCircle className="h-3.5 w-3.5" />
      <span>{message}</span>
    </div>
  );
}

export function BrowserDataImportCard({handler}: {handler: PageHandlerRemote}) {
  return (
    <SectionCard
      title="Import Data"
      description="Import bookmarks, history, passwords, and Arc/Zen spaces and tabs from other browsers."
    >
      <div className="flex items-center justify-between border-t border-border px-6 py-4">
        <div>
          <div className="text-sm font-medium text-foreground">Import from another browser</div>
          <div className="text-xs text-muted-foreground">
            Transfer your spaces, open tabs, bookmarks, and passwords seamlessly into Maho.
          </div>
        </div>
        <ActionButton
          label="Import Browser Data…"
          onClick={() => handler.openMigrationDialog()}
        />
      </div>
    </SectionCard>
  );
}


