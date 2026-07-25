import React, {useCallback, useEffect, useRef, useState, useSyncExternalStore} from 'react';

import {Alert, AlertDescription, AlertTitle} from '@ui/alert';
import {Badge} from '@ui/badge';
import {Button} from '@ui/button';
import {ShortcutsPane} from './shortcuts/index.js';
import {PasswordsPane} from './passwords_settings.js';
import {SavedPasswordsPane} from './passwords_library.js';
import {MailAccountsPane} from './mail_accounts.js';
import {MailSignaturesPane} from './mail_signatures.js';
import {MailRulesPane} from './mail_rules.js';
import {MailCalendarPane} from './mail_calendar.js';
import {MailBehaviorPane} from './mail_behavior.js';
import {MailAiPane} from './mail_ai.js';
import {MailSecurityPane} from './mail_security.js';
import {openCheckoutOverlay} from '../../maho_common/react/lemonsqueezy.js';
import {Input} from '@ui/input';
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from '@ui/select';
import {Switch} from '@ui/switch';
import {AlertCircle, ArrowUpRight, Sparkles, Download, ExternalLink, Check, Loader2} from '@icons/lucide';
import {cn} from '@lib/utils';
import type {AccountStatus, BillingInfo, PageHandlerRemote} from '../maho_settings.mojom-webui.js';
import type {
  AISettings,
  ATCRule,
  AutofillAddress,
  AutofillPayment,
  ContentBlockerStats,
  FilterListInfo,
  ProfileInfo,
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
      <h2 className="mb-6 text-2xl font-semibold tracking-tight">{pane.title}</h2>
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
      <div className="overflow-hidden rounded-xl border border-border/60 glass">
        {headerBanner || null}
        <div className={cn(padded ? "p-6" : "[&>section:first-child]:border-t-0")}>
          {children}
        </div>
      </div>
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
    case 'sync':
      return <SyncPane pane={pane} store={store} />;
    case 'passwords':
      return <PasswordsPane pane={pane} settings={settings} store={store} />;
    case 'saved-passwords':
      return <SavedPasswordsPane pane={pane} settings={settings} store={store} />;
    case 'mail':
      return <MailAccountsPane pane={pane} store={store} />;
    case 'autofill':
      return <AutofillPane pane={pane} store={store} />;
    case 'shortcuts':
      return <ShortcutsPane store={store} />;
    case 'ai':
      return <AiPane pane={pane} store={store} />;
    case 'billing':
      return <BillingPane pane={pane} store={store} />;
    case 'content-blocker':
      return <ContentBlockerPane pane={pane} store={store} />;
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
    case 'mail-ai':
      return <MailAiPane pane={pane} store={store} />;
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

  if (loading && !submitting && account === null) {
    return (
      <PaneShell pane={pane}>
        <PaneLoading message="Loading account…" />
      </PaneShell>
    );
  }

  if (!account?.signedIn) {
    return (
      <PaneShell pane={pane}>
        <SectionCard title="Sign in to Maho" description="Access your subscription, sync, and billing." className="max-w-md" padded>
          <div className="mb-4 flex size-10 items-center justify-center rounded-full bg-primary/10 text-primary">
            <Sparkles className="size-4" />
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
          </form>
        </SectionCard>
      </PaneShell>
    );
  }

  const tierLabel = formatPlanLabel(account.tier);

  const renewDate = formatBillingDate(account.subscriptionExpiresAt);

  return (
    <PaneShell pane={pane}>
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
        <div className="text-sm text-muted-foreground space-y-1">
          {account.userId ? <p>Account ID: {account.userId}</p> : null}
          {renewDate ? <p>Next renewal on {renewDate}</p> : null}
        </div>
      </SectionCard>
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
    {disabled, enabled, label, onToggle}: {
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
  const placeholder = options[0]?.label || ariaLabel;

  return (
    <div className={SELECT_SHELL_CLASS}>
      <Select value={value} onValueChange={nextValue => onChange(nextValue as T)}>
        <SelectTrigger aria-label={ariaLabel} className={SELECT_TRIGGER_CLASS}>
          <SelectValue placeholder={placeholder} />
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
  const handler = store.getHandler();
  const [createName, setCreateName] = useState('');
  const [createError, setCreateError] = useState('');
  const [state, setState] = useState<LoadState<ProfileInfo[]>>(createLoadingState());

  const loadProfiles = useCallback(async () => {
    setState(createLoadingState());
    try {
      const {profiles} = await handler.getProfiles();
      setState(createLoadedState(profiles));
    } catch {
      setState(createErrorState('Failed to load profiles.'));
    }
  }, [handler]);

  useEffect(() => {
    void loadProfiles();
  }, [loadProfiles]);

  return (
    <PaneShell pane={pane}>
      {state.status === 'loading' ? <PaneLoading message="Loading profiles…" /> : null}
      {state.status === 'error' ? (
        <PaneRetry message={state.message} title="Profiles unavailable" onRetry={() => void loadProfiles()} />
      ) : null}
      {state.status === 'loaded' ? (
        <SectionCard title="Browser profiles">
          {state.data.length === 0 ? <EmptyDescription message="No profiles configured." /> : null}
          {state.data.map(profile => (
            <ProfileRow key={profile.id} profile={profile} onReload={loadProfiles} store={store} />
          ))}
          <ManagedSettingRow
            controlClassName={SETTING_ACTIONS_CLASS}
            description={
              <div>
                <div>Create a new browser profile.</div>
                {createError && <ActionError message={createError} />}
              </div>
            }
            title="Add profile">
            <TextFieldShell
              ariaLabel="New profile name"
              placeholder="New profile name"
              value={createName}
              onChange={setCreateName}
            />
            <ActionButton
              label="Create"
              onClick={async () => {
                const name = createName.trim();
                if (!name) {
                  return;
                }
                const {profile} = await handler.createProfile(name);
                if (profile) {
                  setCreateName('');
                  setCreateError('');
                } else {
                  setCreateError('Failed to create profile. The name may be taken or invalid.');
                }
                await loadProfiles();
              }}
            />
          </ManagedSettingRow>
        </SectionCard>
      ) : null}
    </PaneShell>
  );
}

function ProfileRow(
    {profile, onReload, store}: {
      onReload: () => Promise<void>;
      profile: ProfileInfo;
      store: MahoSettingsStore;
    }) {
  const handler = store.getHandler();
  const [confirmingDelete, setConfirmingDelete] = useState(false);
  const parts: string[] = [`ID: ${profile.id}`];
  if (profile.isDefault) {
    parts.push('Default');
  }
  if (profile.isActive) {
    parts.push('Active');
  }
  if (profile.spaceIds.length > 0) {
    parts.push(`Spaces: ${profile.spaceIds.join(', ')}`);
  }

  if (confirmingDelete) {
    return (
      <ManagedSettingRow
        controlClassName={SETTING_ACTIONS_CLASS}
        description="Are you sure you want to delete this profile? This action cannot be undone."
        title={`Delete profile ${profile.name}?`}
      >
        <ActionButton
          label="Confirm"
          onClick={async () => {
            await handler.deleteProfile(profile.id);
            await onReload();
          }}
        />
        <ActionButton
          label="Cancel"
          onClick={() => setConfirmingDelete(false)}
        />
      </ManagedSettingRow>
    );
  }

  return (
    <ManagedSettingRow
      controlClassName={SETTING_ACTIONS_CLASS}
      description={parts.join(' · ')}
      title={profile.name}>
      {!profile.isActive ? (
        <ActionButton
          label="Switch"
          onClick={async () => {
            await handler.switchProfile(profile.id);
            await onReload();
          }}
        />
      ) : null}
      {!profile.isDefault && !profile.isActive ? (
        <ActionButton
          ariaLabel="Delete profile"
          label={<img alt="" aria-hidden="true" className={ICON_CLASS} src="chrome://maho-settings/icons/x.svg" />}
          onClick={() => setConfirmingDelete(true)}
        />
      ) : null}
    </ManagedSettingRow>
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

  const fetchModels = useCallback(async () => {
    if (!providerId) {
      setModels([]);
      return;
    }
    setLoading(true);
    try {
      const res = await handler.fetchProviderModels(providerId);
      setModels(res.modelIds);
      setFromCache(res.fromCache);
    } catch (e) {
      console.error('Failed to fetch provider models', e);
    } finally {
      setLoading(false);
    }
  }, [handler, providerId]);

  const refresh = useCallback(async () => {
    if (!providerId) return;
    setRefreshing(true);
    try {
      const res = await handler.refreshProviderModels(providerId);
      setModels(res.modelIds);
      setFromCache(false);
    } catch (e) {
      console.error('Failed to refresh provider models', e);
    } finally {
      setRefreshing(false);
    }
  }, [handler, providerId]);

  useEffect(() => {
    void fetchModels();
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

function TestConnectionButton({ handler }: { handler: PageHandlerRemote }) {
  const [testing, setTesting] = useState(false);
  const [result, setResult] = useState<{ success: boolean; message: string } | null>(null);

  const handleTest = async () => {
    setTesting(true);
    setResult(null);
    try {
      const res = await handler.testManagedConnection();
      setResult({ success: res.success, message: res.message });
    } catch {
      setResult({ success: false, message: 'Failed to communicate with connection tester.' });
    } finally {
      setTesting(false);
    }
  };

  useEffect(() => {
    if (result) {
      const timer = setTimeout(() => setResult(null), 5000);
      return () => clearTimeout(timer);
    }
    return undefined;
  }, [result]);

  return (
    <div className="flex items-center gap-2 mt-4">
      <Button
        type="button"
        variant="outline"
        size="sm"
        disabled={testing}
        onClick={handleTest}
      >
        {testing ? (
          <span className="flex items-center gap-1">
            <Loader2 className="size-3.5 animate-spin" />
            Testing...
          </span>
        ) : (
          'Test connection'
        )}
      </Button>
      {result ? (
        <span className={cn(
          "text-xs flex items-center gap-1 font-medium",
          result.success ? "text-emerald-600" : "text-destructive"
        )}>
          {result.success ? <Check className="size-3.5" /> : <AlertCircle className="size-3.5" />}
          {result.message}
        </span>
      ) : null}
    </div>
  );
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

function ProviderModeSelector({
  provider,
  onProviderChange
}: {
  provider: string;
  onProviderChange: (mode: 'maho-managed' | 'byok') => void;
}) {
  return (
    <ManagedSettingRow
      title="AI Provider Mode"
      description="Choose between Maho Managed AI service and bringing your own API keys."
    >
      <div className="flex flex-col gap-2">
        <label className="flex items-center gap-2 cursor-pointer text-sm font-medium">
          <input
            type="radio"
            name="ai_provider_mode"
            checked={provider === 'maho-managed'}
            onChange={() => onProviderChange('maho-managed')}
          />
          <span>Maho Managed AI Service</span>
        </label>
        <label className="flex items-center gap-2 cursor-pointer text-sm font-medium">
          <input
            type="radio"
            name="ai_provider_mode"
            checked={provider !== 'maho-managed'}
            onChange={() => onProviderChange('byok')}
          />
          <span>Use my own API key</span>
        </label>
      </div>
    </ManagedSettingRow>
  );
}

function MahoManagedStatus({
  data,
  modelsLoading,
  providerModels,
  handler,
  commit
}: {
  data: AISettings;
  modelsLoading: boolean;
  providerModels: string[];
  handler: PageHandlerRemote;
  commit: (cb: () => Promise<void>) => void;
}) {
  return (
    <div className="p-4 border border-border/70 rounded-xl bg-card/50 flex flex-col gap-3">
      <div className="flex items-center justify-between">
        <div className="flex items-center gap-2">
          <Sparkles className="size-4 text-primary animate-pulse" />
          <span className="font-semibold text-sm">Maho AI Status</span>
        </div>
        <Badge variant="secondary" className="capitalize font-semibold text-xs">
          {data.relayTier || 'free'} Tier
        </Badge>
      </div>
      <div className="text-xs text-muted-foreground flex flex-col gap-1">
        <div>Subscription Status: <span className="font-medium text-foreground capitalize">{data.relaySubscriptionStatus || 'none'}</span></div>
        {data.tierCeilingUsd > 0 ? (
          <div>Monthly Limit: <span className="font-medium text-foreground">${data.tierCeilingUsd.toFixed(2)}</span></div>
        ) : null}
        {data.creditBalanceUsd >= 0 ? (
          <div>Remaining Balance: <span className="font-medium text-foreground">${data.creditBalanceUsd.toFixed(2)}</span></div>
        ) : null}
      </div>
      
      {data.relayTier === 'max' ? (
        <div className="mt-2">
          <div className="text-xs font-semibold mb-1">Model Select (Max Tier)</div>
          <ModelPicker
            ariaLabel="Max Model"
            modelValue={data.model}
            providerModels={providerModels}
            modelsLoading={modelsLoading}
            onCommitModel={async value => { await commit(async () => { await handler.setAIModel(value); }); }}
            allowAuto={true}
          />
        </div>
      ) : null}

      <TestConnectionButton handler={handler} />
    </div>
  );
}

function ByokFields({
  data,
  handler,
  commit
}: {
  data: AISettings;
  handler: PageHandlerRemote;
  commit: (cb: () => Promise<void>) => void;
}) {
  const provider = data.provider;
  if (provider === 'openai') {
    return (
      <AiFieldRow
        initialValue={data.hasByokOpenai ? '••••••••' : ''}
        label="OpenAI BYOK Key"
        placeholder="Enter OpenAI API key (encrypted at rest)"
        type="password"
        onCommit={async (value) => {
          await commit(async () => {
            const {success} = await handler.setBYOKKey('openai', value);
            if (!success) throw new Error('Failed to save OpenAI BYOK key');
          });
        }}
        onClear={async () => {
          await commit(async () => {
            const {success} = await handler.clearBYOKKey('openai');
            if (!success) throw new Error('Failed to clear OpenAI BYOK key');
          });
        }}
      />
    );
  }
  if (provider === 'anthropic') {
    return (
      <AiFieldRow
        initialValue={data.hasByokAnthropic ? '••••••••' : ''}
        label="Anthropic BYOK Key"
        placeholder="Enter Anthropic API key (encrypted at rest)"
        type="password"
        onCommit={async (value) => {
          await commit(async () => {
            const {success} = await handler.setBYOKKey('anthropic', value);
            if (!success) throw new Error('Failed to save Anthropic BYOK key');
          });
        }}
        onClear={async () => {
          await commit(async () => {
            const {success} = await handler.clearBYOKKey('anthropic');
            if (!success) throw new Error('Failed to clear Anthropic BYOK key');
          });
        }}
      />
    );
  }
  if (provider === 'openai-compatible') {
    return (
      <>
        <AiFieldRow
          initialValue={data.hasApiKey ? '••••••••' : ''}
          label="API key"
          placeholder="Enter API key"
          type="password"
          onCommit={async value => { await commit(async () => { await handler.setAIApiKey(value); }); }}
          onClear={async () => {
            await commit(async () => {
              await handler.setAIApiKey("");
            });
          }}
        />
        <AiFieldRow
          initialValue={data.baseUrl}
          label="Base URL"
          placeholder="e.g. https://api.together.xyz"
          onCommit={async value => { await commit(async () => { await handler.setAIBaseUrl(value); }); }}
        />
      </>
    );
  }
  return null;
}

function ByokModelPickerRow({
  data,
  modelsLoading,
  providerModels,
  handler,
  commit
}: {
  data: AISettings;
  modelsLoading: boolean;
  providerModels: string[];
  handler: PageHandlerRemote;
  commit: (cb: () => Promise<void>) => void;
}) {
  const provider = data.provider;
  const isByokProvider = provider === 'openai' || provider === 'anthropic' || provider === 'openai-compatible';
  if (!isByokProvider) return null;

  const emptyErrorMsg = (() => {
    if (provider === 'openai') return 'Add your OpenAI API key above to fetch available models.';
    if (provider === 'anthropic') return 'Add your Anthropic API key above to fetch available models.';
    if (provider === 'openai-compatible') {
      if (!data.baseUrl) return 'Enter a Base URL above to fetch available models.';
      return 'No models returned. Verify the server exposes /v1/models and the API key is correct.';
    }
    return undefined;
  })();

  return (
    <ManagedSettingRow title="Model" description="Choose which model identifier to request.">
      <ModelPicker
        ariaLabel="Model"
        modelValue={data.model}
        providerModels={providerModels}
        modelsLoading={modelsLoading}
        onCommitModel={async value => { await commit(async () => { await handler.setAIModel(value); }); }}
        customPlaceholder="Enter model identifier"
        emptyErrorMsg={emptyErrorMsg}
      />
    </ManagedSettingRow>
  );
}

function LocalServerModelPickerRow({
  data,
  modelsLoading,
  providerModels,
  handler,
  commit
}: {
  data: AISettings;
  modelsLoading: boolean;
  providerModels: string[];
  handler: PageHandlerRemote;
  commit: (cb: () => Promise<void>) => void;
}) {
  if (data.provider !== 'local-server') return null;

  return (
    <ManagedSettingRow title="Model" description="Local Ollama model listing.">
      <ModelPicker
        ariaLabel="Local Model"
        modelValue={data.model}
        providerModels={providerModels}
        modelsLoading={modelsLoading}
        onCommitModel={async value => { await commit(async () => { await handler.setAIModel(value); }); }}
        customPlaceholder="e.g. llama3.1:8b"
        emptyErrorMsg="Ollama not detected or no models found."
      />
    </ManagedSettingRow>
  );
}

function AiPane({pane, store}: {pane: PaneDefinition; store: MahoSettingsStore}) {
  const handler = store.getHandler();
  const [actionError, setActionError] = useState<string | null>(null);
  const [state, setState] = useState<LoadState<AISettings>>(createLoadingState());

  const loadSettings = useCallback(async () => {
    setState(createLoadingState());
    setActionError(null);
    try {
      const {settings} = await handler.getAISettings();
      setState(createLoadedState(settings));
    } catch {
      setState(createErrorState('Failed to load Maho AI settings.'));
    }
  }, [handler]);

  useEffect(() => {
    void loadSettings();
  }, [loadSettings]);

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

  const providerId = state.status === 'loaded' ? state.data.provider : '';
  const { models: providerModels, loading: modelsLoading, refresh: refreshModels, refreshing: modelsRefreshing } = useProviderModels(store, providerId);

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
          <div className="mb-6">
            <SectionCard title="Providers">
              <ProviderModeSelector
                provider={state.data.provider}
                onProviderChange={async mode => {
                  await commit(async () => {
                    const nextProvider = mode === 'maho-managed' ? 'maho-managed' : 'openai';
                    await handler.setAIProvider(nextProvider);
                  });
                }}
              />

              {!state.data.hasRelaySession && state.data.provider === 'maho-managed' ? (
                <div className="mx-6 my-4 p-4 text-sm text-muted-foreground bg-accent/20 rounded-xl border border-border">
                  Maho Managed AI Service is disabled because you are not connected to Sync. Please go to Sync settings to sign in or enable Sync.
                </div>
              ) : null}

              {state.data.hasRelaySession && state.data.provider === 'maho-managed' ? (
                <MahoManagedStatus
                  data={state.data}
                  modelsLoading={modelsLoading}
                  providerModels={providerModels}
                  handler={handler}
                  commit={commit}
                />
              ) : null}

              {state.data.provider !== 'maho-managed' ? (
                <>
                  <AiSelectRow
                    label="Provider"
                    initialValue={state.data.provider}
                    options={[
                      {value: '', label: '(not set)'},
                      {value: 'anthropic', label: 'Anthropic'},
                      {value: 'openai', label: 'OpenAI'},
                      {value: 'openai-compatible', label: 'OpenAI-compatible (custom URL)'},
                      {value: 'local-server', label: 'Local Server (Ollama)'},
                    ]}
                    onCommit={async (value) => { await commit(async () => { await handler.setAIProvider(value); }); }}
                  />

                  <ByokFields
                    data={state.data}
                    handler={handler}
                    commit={commit}
                  />
                </>
              ) : null}
            </SectionCard>
          </div>

          <div className="mb-6">
            <SectionCard
              title="Model"
              action={state.data.provider !== 'maho-managed' && state.data.provider !== '' ? (
                <button
                  type="button"
                  className="inline-flex items-center gap-1 rounded-md border border-border bg-background/70 px-2.5 py-1 text-xs font-medium text-muted-foreground hover:bg-surface-hover disabled:opacity-50"
                  disabled={modelsRefreshing}
                  onClick={() => void refreshModels()}
                  aria-label="Refresh model list"
                >
                  <Loader2 className={cn("size-3", modelsRefreshing ? "animate-spin" : "hidden")} />
                  {modelsRefreshing ? 'Refreshing…' : 'Refresh'}
                </button>
              ) : null}
            >
              {state.data.provider !== 'maho-managed' ? (
                <>
                  <ByokModelPickerRow
                    data={state.data}
                    modelsLoading={modelsLoading}
                    providerModels={providerModels}
                    handler={handler}
                    commit={commit}
                  />

                  <LocalServerModelPickerRow
                    data={state.data}
                    modelsLoading={modelsLoading}
                    providerModels={providerModels}
                    handler={handler}
                    commit={commit}
                  />
                </>
              ) : null}

              <AiSelectRow
                label="Approval policy"
                initialValue={state.data.approvalPolicy}
                options={[
                  {value: 'prompt', label: 'Ask before each tool call'},
                  {value: 'allow', label: 'Auto-approve all tool calls'},
                  {value: 'deny', label: 'Block all tool calls'},
                ]}
                onCommit={async (value) => { await commit(async () => { await handler.setAIApprovalPolicy(value); }); }}
              />
            </SectionCard>
          </div>

          <SectionCard title="Workspace">
            <ManagedSettingRow
              description="Enable or disable AI session persistence."
              title="Session persistence">
              <Toggle
                enabled={state.data.sessionPersistenceEnabled}
                label="Session persistence"
                onToggle={async enabled => { await commit(async () => { await handler.setAISessionPersistence(enabled); }); }}
              />
            </ManagedSettingRow>
          </SectionCard>
        </>
      ) : null}
    </section>
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
  const [purchasingSize, setPurchasingSize] = useState<10 | 50 | 100 | null>(null);

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

  const handleBuyCredits = async (packSize: 10 | 50 | 100) => {
    setPurchaseError(null);
    setPurchasingSize(packSize);
    try {
      const checkoutUrl = await store.getBuyCreditsUrl(packSize);
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
      setPurchasingSize(null);
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
        description="Prepaid credit packs that never expire. Overflow when your subscription ceiling is reached, or standalone if you have no subscription."
        className="mb-6"
        padded
      >
        <div className="grid gap-3 sm:grid-cols-3">
          {([10, 50, 100] as const).map((packSize) => {
            const isFeatured = packSize === 50;
            const isPurchasing = purchasingSize === packSize;
            const isDisabled = purchasingSize !== null;
            return (
              <button
                key={packSize}
                type="button"
                disabled={isDisabled}
                onClick={() => void handleBuyCredits(packSize)}
                className={cn(
                  'flex flex-col items-start gap-1 rounded-xl border p-4 text-left transition-colors disabled:cursor-not-allowed disabled:opacity-60',
                  isFeatured
                    ? 'border-foreground bg-foreground text-background hover:opacity-90'
                    : 'border-border/70 bg-background/60 hover:border-foreground',
                )}
              >
                <span className="text-2xl font-semibold tracking-tight">
                  ${packSize}
                </span>
                <span className="text-xs opacity-80">
                  {packSize === 10
                    ? 'Starter pack'
                    : packSize === 50
                    ? 'Best value'
                    : 'Power-user pack'}
                </span>
                {isPurchasing ? (
                  <span className="text-xs opacity-70">Opening…</span>
                ) : null}
              </button>
            );
          })}
        </div>
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
// would overwrite the real stored key with the placeholder characters.
const MASKED_SECRET_PLACEHOLDER = '\u2022\u2022\u2022\u2022\u2022\u2022\u2022\u2022';

function AiFieldRow(
    {initialValue, label, onCommit, onClear, placeholder, type = 'text'}: {
      initialValue: string;
      label: string;
      onCommit: (value: string) => Promise<void>;
      onClear?: () => Promise<void>;
      placeholder?: string;
      type?: string;
    }) {
  const [draft, setDraft] = useState(initialValue);

  useEffect(() => {
    setDraft(initialValue);
  }, [initialValue]);

  const isUnchangedMaskedSecret =
      draft === MASKED_SECRET_PLACEHOLDER &&
      initialValue === MASKED_SECRET_PLACEHOLDER;

  const hasStoredKey = initialValue !== '';

  return (
    <ManagedSettingRow description="" title={label}>
      <TextFieldShell ariaLabel={label} placeholder={placeholder ?? label} type={type} value={draft} onChange={setDraft} />
      <ActionButton
          disabled={isUnchangedMaskedSecret}
          label="Save"
          onClick={() => {
            if (isUnchangedMaskedSecret) {
              // No-op: user has not edited a masked secret. Submitting would
              // overwrite the real stored key with the placeholder bullets.
              return;
            }
            return onCommit(draft);
          }}
      />
      {onClear && hasStoredKey && (
        <ActionButton
            label="Clear"
            onClick={async () => {
              if (window.confirm(`Are you sure you want to clear the stored ${label}?`)) {
                await onClear();
                setDraft('');
              }
            }}
        />
      )}
    </ManagedSettingRow>
  );
}

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

function SyncPane({pane, store}: {pane: PaneDefinition; store: MahoSettingsStore}) {
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
    <PaneShell pane={pane}>
      {state.status === 'loading' ? <PaneLoading message="Loading sync status…" /> : null}
      {state.status === 'error' ? (
        <PaneRetry message={state.message} title="Sync unavailable" onRetry={() => void loadStatus()} />
      ) : null}
      {state.status === 'loaded' ? (
        <SectionCard title="Connection">
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
    </PaneShell>
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

function ContentBlockerPane({pane, store}: {pane: PaneDefinition; store: MahoSettingsStore}) {
  const handler = store.getHandler();
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
              <div className="flex flex-col gap-2 mt-2">
                <label className="flex items-center gap-2 text-sm cursor-pointer">
                  <input
                    type="radio"
                    name="contentBlockingMode"
                    value="native"
                    checked={state.data.stats.mode === 0}
                    disabled={busyActions.has('mode')}
                    onChange={() => {
                      void runMutation(
                          {action: 'mode', targetId: null, targetLabel: null},
                          async () => (await handler.setContentBlockingMode(0)).success);
                    }}
                  />
                  <span className="font-medium">Native Mode</span> — High-performance built-in Rust adblock engine
                </label>
                <label className="flex items-center gap-2 text-sm cursor-pointer">
                  <input
                    type="radio"
                    name="contentBlockingMode"
                    value="extension"
                    checked={state.data.stats.mode === 1}
                    disabled={busyActions.has('mode')}
                    onChange={() => {
                      void runMutation(
                          {action: 'mode', targetId: null, targetLabel: null},
                          async () => (await handler.setContentBlockingMode(1)).success);
                    }}
                  />
                  <span className="font-medium">Extension Mode</span> — Third-party extension DNR blocking (e.g. uBlock Origin Lite)
                </label>
                <label className="flex items-center gap-2 text-sm cursor-pointer">
                  <input
                    type="radio"
                    name="contentBlockingMode"
                    value="disabled"
                    checked={state.data.stats.mode === 2}
                    disabled={busyActions.has('mode')}
                    onChange={() => {
                      void runMutation(
                          {action: 'mode', targetId: null, targetLabel: null},
                          async () => (await handler.setContentBlockingMode(2)).success);
                    }}
                  />
                  <span className="font-medium">Disabled</span> — Content blocking turned off completely
                </label>
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
