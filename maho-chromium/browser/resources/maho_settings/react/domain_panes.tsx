import React, {useCallback, useEffect, useMemo, useRef, useState, useSyncExternalStore} from 'react';

import {Alert, AlertDescription, AlertTitle} from '@ui/alert';
import {Badge} from '@ui/badge';
import {Button} from '@ui/button';
import {
  Dialog,
  DialogContent,
  DialogHeader,
  DialogTitle,
  DialogDescription,
  DialogFooter,
} from '@ui/dialog';
import {useShortcutInterceptor} from './use_shortcut_interceptor.js';
import {ShortcutsPane} from './shortcuts/index.js';
import {openCheckoutOverlay} from '../../maho_common/react/lemonsqueezy.js';
import {
  Card,
  CardContent,
  CardDescription,
  CardHeader,
  CardTitle,
} from '@ui/card';
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
import {PasswordProviderKind} from '../mojo.js';
import type {AccountStatus, BillingInfo} from '../maho_settings.mojom-webui.js';
import type {
  AISettings,
  ATCRule,
  AutofillAddress,
  AutofillPayment,
  ContentBlockerStats,
  FilterListInfo,
  InstalledExtension,
  PasswordProviderStatus,
  ProfileInfo,
  SavedPassword,
  SettingValue,
  ShortcutBinding,
  SpaceBasicInfo,
  SyncDeviceInfo,
  SyncKeyInfo,
  SyncStatus,
} from '../mojo.js';
import type {PaneDefinition} from '../models.js';
import type {MahoSettingsStore} from './store.js';

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

interface PasswordsPaneData {
  passwords: SavedPassword[];
  providerStatus: PasswordProviderStatus;
}

interface AutofillPaneData {
  addresses: AutofillAddress[];
  payments: AutofillPayment[];
}

interface ContentBlockerPaneData {
  lists: FilterListInfo[];
  stats: ContentBlockerStats;
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

const SHORTCUT_ACRONYMS = new Set(['URL', 'JS', 'HC', 'AI', 'API', 'OS', 'CSS', 'HTML', 'JSON', 'RSS', 'ID', 'UI', 'UX', 'IP', 'DNS', 'TLS', 'SSL', 'VPN', 'CPU', 'GPU', 'RAM']);

const USD_FORMATTER = new Intl.NumberFormat(undefined, {
  style: 'currency',
  currency: 'USD',
  minimumFractionDigits: 2,
  maximumFractionDigits: 2,
});

function formatShortcutAction(action: string): string {
  return action.split('_').map((word, index) => {
    const upper = word.toUpperCase();
    if (SHORTCUT_ACRONYMS.has(upper)) {
      return upper;
    }
    if (index === 0 && word.length > 0) {
      return word.charAt(0).toUpperCase() + word.slice(1);
    }
    return word;
  }).join(' ');
}

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

function getPasswordsEnabled(settings: SettingValue[]): boolean {
  const item = settings.find(setting => setting.key === 'autofill.passwords_enabled');
  return item?.value !== 'false';
}

function isMahoNativeProvider(provider: PasswordProviderStatus['provider']): boolean {
  return provider === PasswordProviderKind.kMahoNative;
}

function normalizeSavedPasswords(passwords: SavedPassword[]): SavedPassword[] {
  return passwords.map(password => ({
    ...password,
    lastUsed: password.lastUsed || null,
    password: password.password || null,
  }));
}

const PANE_SECTION_CLASS = 'mb-6 scroll-mt-8';
const PANE_SHELL_CLASS = 'overflow-hidden rounded-xl border border-border bg-card shadow-sm';
const PANE_HEADER_CLASS = 'border-b border-border px-6 py-4';
const PANE_TITLE_CLASS = 'text-2xl font-semibold tracking-tight';
const PANE_GRID_CLASS = 'grid gap-4 p-4 md:grid-cols-2 md:p-5';
const PANE_CONTENT_CLASS = 'min-w-0 space-y-4 md:col-span-2';
const SECTION_CARD_CLASS = 'overflow-hidden rounded-xl border border-border/70 bg-background/20';
const STANDALONE_MESSAGE_CLASS = 'rounded-xl border border-border/70 bg-background/20 px-6 py-4 text-sm leading-6 text-muted-foreground';
const RETRY_ROW_CLASS =
    'grid gap-4 rounded-xl border border-border/70 bg-background/20 px-6 py-4 sm:grid-cols-[minmax(0,1fr)_minmax(270px,344px)] sm:items-start';
const SECTION_HEADER_CLASS =
    'flex flex-col gap-3 rounded-xl border border-border/70 bg-background/20 px-6 py-4 sm:flex-row sm:items-center sm:justify-between';
const SECTION_HEADER_SPACED_CLASS = 'mt-6';
const SECTION_TITLE_CLASS = 'text-base font-semibold tracking-tight';
const ROWS_CLASS = SECTION_CARD_CLASS;
const SETTING_ROW_CLASS =
    'grid gap-4 border-t border-border px-6 py-4 sm:grid-cols-[minmax(0,1fr)_minmax(270px,344px)] sm:items-start';
const SETTING_ROW_FIRST_CLASS = 'border-t-0 pt-0';
const SETTING_COPY_CLASS = 'min-w-0';
const SETTING_TITLE_CLASS = 'text-sm font-medium text-foreground';
const SETTING_DESCRIPTION_CLASS = 'mt-1 text-xs leading-5 text-muted-foreground';
const EMPTY_DESCRIPTION_CLASS = 'border-b border-border px-6 py-4 text-sm leading-6 text-muted-foreground';
const SETTING_CONTROL_CLASS = 'flex w-full items-center justify-start gap-2 sm:justify-end';
const SETTING_ACTIONS_CLASS = 'flex w-full flex-wrap items-center gap-2 sm:justify-end';
const SETTING_CONTROL_STACKED_CLASS = 'flex-col items-stretch sm:justify-start';
const SETTING_CONTROL_STACK_CLASS = 'flex w-full flex-col gap-2';
const INPUT_SHELL_CLASS = 'w-full min-w-0';
const INPUT_FIELD_CLASS = 'bg-background/70';
const SELECT_SHELL_CLASS = 'w-full min-w-0';
const SELECT_TRIGGER_CLASS = 'bg-background/70';
const ACTION_BUTTON_CLASS =
    'h-9 min-w-fit rounded-lg border-border bg-background/70 px-4 text-sm font-medium text-foreground shadow-sm hover:bg-accent hover:text-accent-foreground';
const INLINE_STATUS_CLASS = 'inline-flex min-h-9 items-center rounded-lg border border-border bg-background/70 px-3 text-sm font-medium text-foreground';
const CONTENT_STACKED_HEADER_CLASS = 'rounded-xl border border-border/70 bg-background/20 px-6 py-4';
const CONTENT_TITLE_GROUP_CLASS = 'min-w-0 space-y-1';
const CONTENT_COPY_CLASS = 'text-sm leading-6 text-muted-foreground';
const PROVIDER_CARD_CLASS = 'grid gap-0 overflow-hidden rounded-lg border border-border/70 bg-background/40';
const CAPABILITIES_COPY_CLASS = 'px-6 py-4 text-xs leading-5 text-muted-foreground';
const BROWSER_SHELL_CLASS = 'overflow-hidden rounded-xl border border-border/70 bg-background/20';
const ICON_CLASS = 'inline-block size-4 shrink-0 align-middle';

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
    case 'extensions':
      return <ExtensionsPane pane={pane} store={store} />;
    case 'atc':
      return <ATCPane pane={pane} store={store} />;
    default:
      return (
        <DomainPaneSection pane={pane}>
          <p className={STANDALONE_MESSAGE_CLASS}>This pane is not available.</p>
        </DomainPaneSection>
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
      <DomainPaneSection pane={pane}>
        <PaneLoading message="Loading account…" />
      </DomainPaneSection>
    );
  }

  if (!account?.signedIn) {
    return (
      <DomainPaneSection pane={pane}>
        <Card className="max-w-md border-border/70 bg-card/95 shadow-sm">
          <CardHeader className="space-y-4">
            <div className="flex size-10 items-center justify-center rounded-full bg-primary/10 text-primary">
              <Sparkles className="size-4" />
            </div>
            <div className="space-y-1">
              <CardTitle>Sign in to Maho</CardTitle>
              <CardDescription>Access your subscription, sync, and billing.</CardDescription>
            </div>
          </CardHeader>
          <CardContent>
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
          </CardContent>
        </Card>
      </DomainPaneSection>
    );
  }

  const tierLabel = formatPlanLabel(account.tier);
  const planBadgeVariant: 'default' | 'secondary' =
      account.tier && account.tier !== 'free' ? 'default' : 'secondary';
  const renewDate = formatBillingDate(account.subscriptionExpiresAt);

  return (
    <DomainPaneSection pane={pane}>
      <Card className="max-w-2xl border-border/70 bg-card/95 shadow-sm">
        <CardHeader className="gap-4 sm:flex-row sm:items-start sm:justify-between">
          <div className="space-y-3">
            <div className="space-y-1">
              <CardTitle className="text-2xl tracking-tight">{account.email}</CardTitle>
              <CardDescription className="flex flex-wrap items-center gap-2">
                <Badge variant={planBadgeVariant}>{tierLabel} plan</Badge>
                {account.displayName ? <span>{account.displayName}</span> : null}
                {account.subscriptionStatus ? <span>· {account.subscriptionStatus}</span> : null}
              </CardDescription>
            </div>
            <div className="grid gap-1 text-sm text-muted-foreground">
              {account.userId ? <p>Account ID: {account.userId}</p> : null}
              {renewDate ? <p>Next renewal on {renewDate}</p> : null}
            </div>
          </div>
          <Button disabled={loading} type="button" variant="outline" onClick={() => void store.signOut()}>
            Sign out
          </Button>
        </CardHeader>
      </Card>
    </DomainPaneSection>
  );
}

function DomainPaneSection(
    {children, pane}: {children: React.ReactNode; pane: PaneDefinition}) {
  return (
    <section className={PANE_SECTION_CLASS} data-pane={pane.key} id={`section-${pane.key}`}>
      <div className={PANE_SHELL_CLASS}>
        <header className={PANE_HEADER_CLASS}>
          <h2 className={PANE_TITLE_CLASS}>{pane.title}</h2>
        </header>
        <div className={PANE_GRID_CLASS}>
          <article className={PANE_CONTENT_CLASS}>{children}</article>
        </div>
      </div>
    </section>
  );
}

function PaneLoading({message}: {message: string}) {
  return <p className={STANDALONE_MESSAGE_CLASS}>{message}</p>;
}

function PaneRetry(
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

function EmptyDescription({message}: {message: string}) {
  return <p className={EMPTY_DESCRIPTION_CLASS}>{message}</p>;
}

function SectionHeader(
    {action, spaced = false, title}: {
      action?: React.ReactNode;
      spaced?: boolean;
      title: string;
    }) {
  return (
    <div className={cn(SECTION_HEADER_CLASS, spaced ? SECTION_HEADER_SPACED_CLASS : undefined)}>
      <h3 className={SECTION_TITLE_CLASS}>{title}</h3>
      {action ?? null}
    </div>
  );
}

function ActionButton(
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
    {enabled, label, onToggle}: {
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
        disabled={isBusy}
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

function ManagedSettingRow(
    {children, controlClassName, description, isFirstRow = false, title}: {
      children?: React.ReactNode;
      controlClassName?: string;
      description: React.ReactNode;
      isFirstRow?: boolean;
      title: string;
    }) {
  return (
    <section className={cn(SETTING_ROW_CLASS, isFirstRow ? SETTING_ROW_FIRST_CLASS : undefined)}>
      <div className={SETTING_COPY_CLASS}>
        <h4 className={SETTING_TITLE_CLASS}>{title}</h4>
        <p className={SETTING_DESCRIPTION_CLASS}>{description}</p>
      </div>
      <div className={cn(SETTING_CONTROL_CLASS, controlClassName)}>{children}</div>
    </section>
  );
}

function TextFieldShell(
    {ariaLabel, onChange, placeholder, type = 'text', value}: {
      ariaLabel: string;
      onChange: (value: string) => void;
      placeholder: string;
      type?: string;
      value: string;
    }) {
  return (
    <div className={INPUT_SHELL_CLASS}>
      <Input
        aria-label={ariaLabel}
        className={INPUT_FIELD_CLASS}
        placeholder={placeholder}
        type={type}
        value={value}
        onChange={event => onChange(event.currentTarget.value)}
      />
    </div>
  );
}

function SelectShell<T extends string>(
    {ariaLabel, onChange, options, value}: {
      ariaLabel: string;
      onChange: (value: T) => void;
      options: Array<{label: string; value: T}>;
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
            <SelectItem key={option.value} value={option.value}>{option.label}</SelectItem>
          ))}
        </SelectContent>
      </Select>
    </div>
  );
}

function ProfilesPane({pane, store}: {pane: PaneDefinition; store: MahoSettingsStore}) {
  const handler = store.getHandler();
  const [createName, setCreateName] = useState('');
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
    <DomainPaneSection pane={pane}>
      <SectionHeader title="Browser profiles" />
      {state.status === 'loading' ? <PaneLoading message="Loading profiles…" /> : null}
      {state.status === 'error' ? (
        <PaneRetry message={state.message} title="Profiles unavailable" onRetry={() => void loadProfiles()} />
      ) : null}
      {state.status === 'loaded' ? (
        <div className={ROWS_CLASS}>
          {state.data.length === 0 ? <EmptyDescription message="No profiles configured." /> : null}
          {state.data.map(profile => (
            <ProfileRow key={profile.id} profile={profile} onReload={loadProfiles} store={store} />
          ))}
          <ManagedSettingRow
            controlClassName={SETTING_ACTIONS_CLASS}
            description="Create a new browser profile."
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
                }
                await loadProfiles();
              }}
            />
          </ManagedSettingRow>
        </div>
      ) : null}
    </DomainPaneSection>
  );
}

function ProfileRow(
    {profile, onReload, store}: {
      onReload: () => Promise<void>;
      profile: ProfileInfo;
      store: MahoSettingsStore;
    }) {
  const handler = store.getHandler();
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
          onClick={async () => {
            await handler.deleteProfile(profile.id);
            await onReload();
          }}
        />
      ) : null}
    </ManagedSettingRow>
  );
}

function ATCPane({pane, store}: {pane: PaneDefinition; store: MahoSettingsStore}) {
  const handler = store.getHandler();
  const [pattern, setPattern] = useState('');
  const [rulesState, setRulesState] = useState<LoadState<ATCRule[]>>(createLoadingState());
  const [spaceId, setSpaceId] = useState('');

  const loadRules = useCallback(async () => {
    setRulesState(createLoadingState());
    try {
      const {rules} = await handler.getATCRules();
      setRulesState(createLoadedState(rules));
    } catch {
      setRulesState(createErrorState('Failed to load rules.'));
    }
  }, [handler]);

  useEffect(() => {
    void loadRules();
  }, [loadRules]);

  return (
    <DomainPaneSection pane={pane}>
      <SectionHeader title="URL routing rules" />
      {rulesState.status === 'loading' ? <PaneLoading message="Loading rules…" /> : null}
      {rulesState.status === 'error' ? (
        <PaneRetry message={rulesState.message} title="Rules unavailable" onRetry={() => void loadRules()} />
      ) : null}
      {rulesState.status === 'loaded' ? (
        <div className={ROWS_CLASS}>
          {rulesState.data.map(rule => (
            <ATCRuleRow
              key={rule.id}
              rule={rule}
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
          <ManagedSettingRow
            controlClassName={SETTING_ACTIONS_CLASS}
            description="Route matching URLs into a target space."
            title="Add rule">
            <TextFieldShell
              ariaLabel="URL pattern"
              placeholder="URL pattern (e.g. *.example.com)"
              value={pattern}
              onChange={setPattern}
            />
            <TextFieldShell
              ariaLabel="Target space ID"
              placeholder="Target space ID"
              value={spaceId}
              onChange={setSpaceId}
            />
            <ActionButton
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
                  setSpaceId('');
                }
              }}
            />
          </ManagedSettingRow>
        </div>
      ) : null}
    </DomainPaneSection>
  );
}

function ATCRuleRow(
    {onRemove, onToggle, rule}: {
      onRemove: () => Promise<void>;
      onToggle: (nextEnabled: boolean) => Promise<void>;
      rule: ATCRule;
    }) {
  return (
    <ManagedSettingRow
      controlClassName={SETTING_ACTIONS_CLASS}
      description={<><img alt="" aria-hidden="true" className={ICON_CLASS} src="chrome://maho-settings/icons/arrow-right.svg" /> {rule.targetSpaceId}</>}
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

  useEffect(() => {
    void fetchModels();
  }, [fetchModels]);

  useEffect(() => {
    const onRefreshed = (pId: string, modelIds: string[]) => {
      if (pId === providerId) {
        setModels(modelIds);
        setFromCache(false);
      }
    };
    callbackRouter.providerModelsRefreshed.addListener(onRefreshed);
    return () => {
      callbackRouter.providerModelsRefreshed.removeListener(onRefreshed);
    };
  }, [callbackRouter, providerId]);

  return { models, loading, fromCache };
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
  if (modelsLoading) {
    return (
      <div className="text-xs text-muted-foreground flex items-center gap-1">
        <Loader2 className="size-3 animate-spin" />
        Loading models...
      </div>
    );
  }

  const hasModels = providerModels.length > 0;
  const isCustom = hasModels && !providerModels.includes(modelValue) && (!allowAuto || modelValue !== '');

  const options: Array<{value: string; label: string}> = [];
  if (allowAuto) {
    options.push({value: '', label: 'Auto — Best model for your prompt'});
  }
  options.push(...providerModels.map(m => ({value: m, label: m})));
  if (hasModels) {
    options.push({value: 'custom', label: 'Other...'});
  }

  const selectedValue = isCustom ? 'custom' : modelValue;

  return (
    <div className="flex flex-col gap-2 w-full">
      {hasModels ? (
        <SelectShell
          ariaLabel={ariaLabel}
          value={selectedValue}
          options={options}
          onChange={value => {
            if (value !== 'custom') {
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
      {(!hasModels || isCustom) && (
        <TextFieldShell
          ariaLabel={ariaLabel + " Text Input"}
          placeholder={customPlaceholder}
          value={modelValue}
          onChange={onCommitModel}
        />
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
      isFirstRow={true}
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
  handler: any;
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
            onCommitModel={value => commit(async () => handler.setAIModel(value))}
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
  handler: any;
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
        onCommit={value => commit(async () => {
          const {success} = await handler.setBYOKKey('openai', value);
          if (!success) throw new Error('Failed to save OpenAI BYOK key');
        })}
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
        onCommit={value => commit(async () => {
          const {success} = await handler.setBYOKKey('anthropic', value);
          if (!success) throw new Error('Failed to save Anthropic BYOK key');
        })}
      />
    );
  }
  if (provider === 'openai-compatible') {
    return (
      <>
        <AiFieldRow
          initialValue={data.apiKey}
          label="API key"
          placeholder="Enter API key"
          type="password"
          onCommit={value => commit(async () => handler.setAIApiKey(value))}
        />
        <AiFieldRow
          initialValue={data.baseUrl}
          label="Base URL"
          placeholder="e.g. https://api.together.xyz"
          onCommit={value => commit(async () => handler.setAIBaseUrl(value))}
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
  handler: any;
  commit: (cb: () => Promise<void>) => void;
}) {
  const provider = data.provider;
  const isByokProvider = provider === 'openai' || provider === 'anthropic' || provider === 'openai-compatible';
  if (!isByokProvider) return null;

  return (
    <ManagedSettingRow title="Model" description="Choose which model identifier to request.">
      <ModelPicker
        ariaLabel="Model"
        modelValue={data.model}
        providerModels={providerModels}
        modelsLoading={modelsLoading}
        onCommitModel={value => commit(async () => handler.setAIModel(value))}
        customPlaceholder="Enter model identifier"
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
  handler: any;
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
        onCommitModel={value => commit(async () => handler.setAIModel(value))}
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

  useEffect(() => {
    const unsub = store.subscribe(() => {
      void loadSettings();
    });
    return unsub;
  }, [store, loadSettings]);

  const commit = useCallback(async (action: () => Promise<void>) => {
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
  const { models: providerModels, loading: modelsLoading } = useProviderModels(store, providerId);

  return (
    <DomainPaneSection pane={pane}>
      <SectionHeader title="Maho AI configuration" />
      {actionError ? <p className={STANDALONE_MESSAGE_CLASS}>{actionError}</p> : null}
      {state.status === 'loading' ? <PaneLoading message="Loading Maho AI settings…" /> : null}
      {state.status === 'error' ? (
        <PaneRetry message={state.message} title="Maho AI settings unavailable" onRetry={() => void loadSettings()} />
      ) : null}
      {state.status === 'loaded' ? (
        <div className={ROWS_CLASS}>
          {state.data.hasRelaySession ? (
            <ProviderModeSelector
              provider={state.data.provider}
              onProviderChange={mode => commit(async () => {
                const nextProvider = mode === 'maho-managed' ? 'maho-managed' : 'openai';
                await handler.setAIProvider(nextProvider);
              })}
            />
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
                onCommit={value => commit(async () => handler.setAIProvider(value))}
              />

              <ByokFields
                data={state.data}
                handler={handler}
                commit={commit}
              />

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
            onCommit={value => commit(async () => handler.setAIApprovalPolicy(value))}
          />

          <ManagedSettingRow
            description="Enable or disable AI session persistence."
            title="Session persistence">
            <Toggle
              enabled={state.data.sessionPersistenceEnabled}
              label="Session persistence"
              onToggle={enabled => commit(async () => handler.setAISessionPersistence(enabled))}
            />
          </ManagedSettingRow>
        </div>
      ) : null}
    </DomainPaneSection>
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
      <DomainPaneSection pane={pane}>
        <Card className="max-w-xl border-border/70 bg-card/95 shadow-sm">
          <CardHeader>
            <CardTitle>Sign in to manage your subscription</CardTitle>
            <CardDescription>You'll see plan details, credits, and invoices here after signing in.</CardDescription>
          </CardHeader>
          <CardContent>
            <Button type="button" onClick={() => store.selectPane('account')}>
              Go to sign in
            </Button>
          </CardContent>
        </Card>
      </DomainPaneSection>
    );
  }

  if (loading && !billing) {
    return (
      <DomainPaneSection pane={pane}>
        <PaneLoading message="Loading billing details…" />
      </DomainPaneSection>
    );
  }

  if (error) {
    return (
      <DomainPaneSection pane={pane}>
        <div className="grid max-w-xl gap-3">
          <Alert className="border-destructive/60 bg-destructive/10 text-destructive shadow-sm" variant="destructive">
            <AlertCircle className="size-4" />
            <AlertTitle>Connection lost</AlertTitle>
            <AlertDescription>{error}</AlertDescription>
          </Alert>
          <div>
            <Button type="button" variant="outline" onClick={() => void store.refreshBilling()}>
              Retry
            </Button>
          </div>
        </div>
      </DomainPaneSection>
    );
  }

  if (!billing) {
    return (
      <DomainPaneSection pane={pane}>
        <PaneLoading message="Loading billing details…" />
      </DomainPaneSection>
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
    <DomainPaneSection pane={pane}>
      <div className="grid max-w-3xl gap-6">
        <Card className="border-border/70 bg-card/95 shadow-sm">
          <CardHeader className="gap-4 pb-4 sm:flex-row sm:items-start sm:justify-between">
            <div className="space-y-2">
              <div className="flex flex-wrap items-center gap-2">
                <CardTitle>Overview</CardTitle>
                <Badge variant={statusBadgeVariant}>{billing.subscriptionStatus || 'No subscription'}</Badge>
              </div>
              <CardDescription>Plan details, renewal timing, and payment status for your Maho account.</CardDescription>
            </div>
            <Button size="sm" type="button" variant="outline" onClick={openBillingPortal}>
              Adjust plan
              <ArrowUpRight className="size-3.5" />
            </Button>
          </CardHeader>
          <CardContent className="grid gap-3 sm:grid-cols-2">
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
              <p className="mt-2 text-sm text-muted-foreground">Update cards, receipts, and billing details in Stripe.</p>
              <Button className="mt-4" size="sm" type="button" variant="outline" onClick={openBillingPortal}>
                Manage
                <ArrowUpRight className="size-3.5" />
              </Button>
            </div>
          </CardContent>
        </Card>

        <Card className="border-border/70 bg-card/95 shadow-sm">
          <CardHeader className="pb-4">
            <div className="flex flex-wrap items-start justify-between gap-3">
              <div className="space-y-1">
                <CardTitle>Credits</CardTitle>
                <CardDescription>
                  {creditLimit > 0 ? `${remainingPct}% remaining` : 'Current available balance'}
                </CardDescription>
              </div>
              <Badge variant={progressValue > 20 ? 'success' : progressValue > 5 ? 'warning' : 'destructive'}>
                {formatUsd(creditBalance)}
              </Badge>
            </div>
          </CardHeader>
          <CardContent>
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
          </CardContent>
        </Card>

        <Card className="border-border/70 bg-card/95 shadow-sm">
          <CardHeader className="pb-4">
            <CardTitle>Buy Credits</CardTitle>
            <CardDescription>
              Prepaid credit packs that never expire. Overflow when your subscription ceiling is reached, or standalone if you have no subscription.
            </CardDescription>
          </CardHeader>
          <CardContent className="space-y-4">
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
              <Alert className="border-destructive/60 bg-destructive/10 text-destructive shadow-sm" variant="destructive">
                <AlertCircle className="size-4" />
                <AlertTitle>Purchase failed</AlertTitle>
                <AlertDescription>{purchaseError}</AlertDescription>
              </Alert>
            ) : null}
            <p className="text-xs text-muted-foreground">
              Credits are consumed at a 30% markup over usage cost. Credits never expire. See ADR 0010 for details.
            </p>
          </CardContent>
        </Card>

        <Card className="border-border/70 bg-card/95 shadow-sm">
          <CardHeader className="flex-row items-center justify-between">
            <div className="space-y-1">
              <CardTitle>Invoices</CardTitle>
              <CardDescription>Receipts and invoices will appear here after your first paid charge.</CardDescription>
            </div>
            {state.invoicesLoading && <Badge variant="secondary">Loading…</Badge>}
          </CardHeader>
          <CardContent>
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
          </CardContent>
        </Card>
      </div>
    </DomainPaneSection>
  );
}

// Sentinel value used by GetAISettings to indicate "a key is stored but its
// value is intentionally hidden from the WebUI". If the user clicks Save
// without editing the field, we must NOT submit this literal value, or it
// would overwrite the real stored key with the placeholder characters.
const MASKED_SECRET_PLACEHOLDER = '\u2022\u2022\u2022\u2022\u2022\u2022\u2022\u2022';

function AiFieldRow(
    {initialValue, label, onCommit, placeholder, type = 'text'}: {
      initialValue: string;
      label: string;
      onCommit: (value: string) => Promise<void>;
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

function SyncPane({pane, store}: {pane: PaneDefinition; store: MahoSettingsStore}) {
  const handler = store.getHandler();
  const [joinPhrase, setJoinPhrase] = useState('');
  const [pendingKeyInfo, setPendingKeyInfo] = useState<SyncKeyInfo | null>(null);
  const [state, setState] = useState<LoadState<SyncPaneData>>(createLoadingState());

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

  return (
    <DomainPaneSection pane={pane}>
      <SectionHeader title="Account & sync" />
      {state.status === 'loading' ? <PaneLoading message="Loading sync status…" /> : null}
      {state.status === 'error' ? (
        <PaneRetry message={state.message} title="Sync unavailable" onRetry={() => void loadStatus()} />
      ) : null}
      {state.status === 'loaded' ? (
        <div className={ROWS_CLASS}>
          <ManagedSettingRow
            description={state.data.status.isSyncing ?
              `Syncing · ${state.data.status.statusLabel}` :
              `Not connected · ${state.data.status.statusLabel || 'idle'}`}
            isFirstRow
            title="Status"
          />
          {state.data.status.errorMessage ? (
            <ManagedSettingRow description={state.data.status.errorMessage} title="Error" />
          ) : null}
          {state.data.status.isSyncing ? (
            <>
              {state.data.devices.length > 0 ? (
                <ManagedSettingRow description="" title="Connected devices" />
              ) : null}
              {state.data.devices.map(device => (
                <ManagedSettingRow
                  key={device.id}
                  description={`${device.deviceType} · ${device.isOnline ? 'Online' : 'Offline'}`}
                  title={device.name}
                />
              ))}
              {state.data.devicesError ? (
                <ManagedSettingRow description={state.data.devicesError} title="Devices" />
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
                  await handler.startSync(pendingKeyInfo.syncKey);
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
        </div>
      ) : null}
    </DomainPaneSection>
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
    <DomainPaneSection pane={pane}>
      {state.status === 'loading' ? <PaneLoading message="Loading autofill data…" /> : null}
      {state.status === 'error' ? (
        <PaneRetry message={state.message} title="Autofill unavailable" onRetry={() => void loadAutofill()} />
      ) : null}
      {state.status === 'loaded' ? (
        <>
          <SectionHeader title="Saved addresses" />
          <div className={ROWS_CLASS}>
            {state.data.addresses.length === 0 ? <EmptyDescription message="No saved addresses." /> : null}
            {state.data.addresses.map((address, index) => {
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
                  isFirstRow={index === 0}
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
              isFirstRow={state.data.addresses.length === 0}
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
          </div>
          <SectionHeader spaced title="Payment methods" />
          <div className={ROWS_CLASS}>
            {state.data.payments.length === 0 ? <EmptyDescription message="No saved payment methods." /> : null}
            {state.data.payments.map((payment, index) => {
              const descriptionParts = [payment.cardholderName, payment.expiration ? `Exp ${payment.expiration}` : ''].filter(Boolean);
              return (
                <ManagedSettingRow
                  key={payment.id}
                  controlClassName={SETTING_ACTIONS_CLASS}
                  description={descriptionParts.join(' · ') || 'No details'}
                  isFirstRow={index === 0}
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
              isFirstRow={state.data.payments.length === 0}
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
          </div>
        </>
      ) : null}
    </DomainPaneSection>
  );
}

function PasswordsPane(
    {pane, settings, store}: {
      pane: PaneDefinition;
      settings: SettingValue[];
      store: MahoSettingsStore;
    }) {
  const handler = store.getHandler();
  const [loadState, setLoadState] = useState<LoadState<PasswordsPaneData>>(createLoadingState());
  const [passwordsEnabledOverride, setPasswordsEnabledOverride] = useState<boolean | null>(null);
  const [searchInputValue, setSearchInputValue] = useState('');
  const [searchQuery, setSearchQuery] = useState('');
  const [revealedIds, setRevealedIds] = useState<Set<string>>(new Set());
  const hasLoadedOnceRef = useRef(false);
  const passwordsRequestIdRef = useRef(0);
  const passwordsEnabled = passwordsEnabledOverride ?? getPasswordsEnabled(settings);

  useEffect(() => {
    setPasswordsEnabledOverride(null);
  }, [settings]);

  useEffect(() => {
    const timeoutId = window.setTimeout(() => {
      setSearchQuery(searchInputValue.trim());
    }, 200);

    return () => {
      window.clearTimeout(timeoutId);
    };
  }, [searchInputValue]);

  const loadPasswords = useCallback(async (nextSearchQuery: string, showLoading = true) => {
    const requestId = ++passwordsRequestIdRef.current;
    if (showLoading) {
      setLoadState(createLoadingState());
    }

    try {
      const [providerResult, passwordsResult] = await Promise.all([
        handler.getPasswordProviderStatus(),
        nextSearchQuery ? handler.searchPasswords(nextSearchQuery) : handler.getSavedPasswords(),
      ]);

      if (requestId !== passwordsRequestIdRef.current) {
        return;
      }

      hasLoadedOnceRef.current = true;
      setLoadState(createLoadedState({
        passwords: normalizeSavedPasswords(passwordsResult.passwords),
        providerStatus: providerResult.status,
      }));
    } catch {
      if (requestId !== passwordsRequestIdRef.current) {
        return;
      }

      hasLoadedOnceRef.current = true;
      setLoadState(createErrorState('Failed to load passwords.'));
    }
  }, [handler]);

  useEffect(() => {
    void loadPasswords(searchQuery, !hasLoadedOnceRef.current);
  }, [loadPasswords, searchQuery]);

  return (
    <DomainPaneSection pane={pane}>
      {loadState.status === 'loading' ? <PaneLoading message="Loading password provider…" /> : null}
      {loadState.status === 'error' ? (
        <PaneRetry message={loadState.message} title="Passwords unavailable" onRetry={() => void loadPasswords(searchQuery)} />
      ) : null}
      {loadState.status === 'loaded' ? (
        <>
          <div className={CONTENT_STACKED_HEADER_CLASS}>
            <div className={CONTENT_TITLE_GROUP_CLASS}>
                <h3 className={SECTION_TITLE_CLASS}>Saved passwords</h3>
              <p className={CONTENT_COPY_CLASS}>{`Passwords provider: ${loadState.data.providerStatus.displayName}.`}</p>
            </div>
            <div className={PROVIDER_CARD_CLASS}>
              <ManagedSettingRow
                description="Enable password management to show saved passwords."
                isFirstRow
                title="Passwords">
                <Toggle
                  enabled={passwordsEnabled}
                  label="Passwords"
                  onToggle={async nextEnabled => {
                    const success = await store.commitSettingValue(
                        'autofill.passwords_enabled', nextEnabled ? 'true' : 'false');
                    if (success) {
                      setPasswordsEnabledOverride(nextEnabled);
                    }
                  }}
                />
              </ManagedSettingRow>
              <ManagedSettingRow
                description={loadState.data.providerStatus.description}
                title={`Password provider · ${loadState.data.providerStatus.displayName}`}>
                <span className={INLINE_STATUS_CLASS}>
                  {loadState.data.providerStatus.provider === PasswordProviderKind.kMahoNative ?
                    'Maho Native' :
                    loadState.data.providerStatus.provider === PasswordProviderKind.kBitwarden ?
                      'Bitwarden' :
                      '1Password'}
                </span>
              </ManagedSettingRow>
              <p className={CAPABILITIES_COPY_CLASS}>
                {`Capabilities: ${loadState.data.providerStatus.capabilities.canListSavedPasswords ? 'list' : 'no list'}, ${loadState.data.providerStatus.capabilities.canSearchSavedPasswords ? 'search' : 'no search'}, ${loadState.data.providerStatus.capabilities.canAddSavedPasswords ? 'add' : 'no add'}, ${loadState.data.providerStatus.capabilities.canEditSavedPasswords ? 'edit' : 'no edit'}, ${loadState.data.providerStatus.capabilities.canRevealPasswords ? 'reveal' : 'no reveal'}, ${loadState.data.providerStatus.capabilities.canDeleteSavedPasswords ? 'delete' : 'no delete'}.`}
              </p>
            </div>
          </div>
          <div className={BROWSER_SHELL_CLASS}>
            {!isMahoNativeProvider(loadState.data.providerStatus.provider) ? (
              <>
                <ManagedSettingRow
                  description={`${loadState.data.providerStatus.displayName} handles save, fill, and autosave through its extension. Chromium's built-in password manager is suppressed in this mode, and Maho Native passwords are not manageable here.`}
                  title={`${loadState.data.providerStatus.displayName} is managing passwords`}>
                  <span className={INLINE_STATUS_CLASS}>Extension-managed</span>
                </ManagedSettingRow>
                {loadState.data.passwords.length > 0 ? (
                  <p className={STANDALONE_MESSAGE_CLASS}>
                    {`${loadState.data.passwords.length} Maho Native password${loadState.data.passwords.length === 1 ? '' : 's'} remain stored locally and can only be managed after switching back to Maho Native.`}
                  </p>
                ) : null}
              </>
            ) : !passwordsEnabled ? (
              <p className={STANDALONE_MESSAGE_CLASS}>Passwords is turned off. Turn it on to show saved passwords.</p>
            ) : (
              <NativePasswordsContent
                data={loadState.data}
                loadPasswords={loadPasswords}
                revealedIds={revealedIds}
                searchInputValue={searchInputValue}
                searchQuery={searchQuery}
                setRevealedIds={setRevealedIds}
                setSearchInputValue={setSearchInputValue}
                store={store}
              />
            )}
          </div>
        </>
      ) : null}
    </DomainPaneSection>
  );
}

function NativePasswordsContent(
    {data, loadPasswords, revealedIds, searchInputValue, searchQuery, setRevealedIds, setSearchInputValue, store}: {
      data: PasswordsPaneData;
      loadPasswords: (searchQuery: string, showLoading?: boolean) => Promise<void>;
      revealedIds: Set<string>;
      searchInputValue: string;
      searchQuery: string;
      setRevealedIds: React.Dispatch<React.SetStateAction<Set<string>>>;
      setSearchInputValue: (value: string) => void;
      store: MahoSettingsStore;
    }) {
  const handler = store.getHandler();
  const providerStatus = data.providerStatus;

  return (
    <>
      <ManagedSettingRow description="Filter the native password list below." isFirstRow title="Search saved passwords">
        <TextFieldShell
          ariaLabel="Search passwords"
          placeholder="Search passwords"
          type="search"
          value={searchInputValue}
          onChange={setSearchInputValue}
        />
      </ManagedSettingRow>
      {providerStatus.capabilities.canAddSavedPasswords ? (
        <AddPasswordRow loadPasswords={loadPasswords} query={searchQuery} store={store} />
      ) : null}
      <div className={ROWS_CLASS}>
        {data.passwords.length === 0 ? <EmptyDescription message="No saved passwords." /> : null}
        {data.passwords.map(savedPassword => (
          <PasswordRow
            key={savedPassword.id}
            isRevealed={revealedIds.has(savedPassword.id)}
            loadPasswords={loadPasswords}
            password={savedPassword}
            providerStatus={providerStatus}
            query={searchQuery}
            setRevealedIds={setRevealedIds}
            store={store}
          />
        ))}
      </div>
    </>
  );
}

function AddPasswordRow(
    {loadPasswords, query, store}: {
      loadPasswords: (searchQuery: string) => Promise<void>;
      query: string;
      store: MahoSettingsStore;
    }) {
  const handler = store.getHandler();
  const [domain, setDomain] = useState('');
  const [password, setPassword] = useState('');
  const [username, setUsername] = useState('');

  return (
    <ManagedSettingRow
      controlClassName={SETTING_CONTROL_STACKED_CLASS}
      description="Store a new native password entry."
      title="Add password">
      <div className={SETTING_CONTROL_STACK_CLASS}>
        <TextFieldShell ariaLabel="Password domain" placeholder="Domain" value={domain} onChange={setDomain} />
        <TextFieldShell ariaLabel="Password username" placeholder="Username" value={username} onChange={setUsername} />
        <TextFieldShell ariaLabel="Password value" placeholder="Password" type="password" value={password} onChange={setPassword} />
      </div>
      <div className={SETTING_ACTIONS_CLASS}>
        <ActionButton label="Add password" onClick={async () => {
          const trimmedDomain = domain.trim();
          const trimmedUsername = username.trim();
          if (!trimmedDomain) {
            return;
          }
          const success = await handler.addPassword(trimmedDomain, trimmedUsername, password);
          if (!success.success) {
            return;
          }
          setDomain('');
          setUsername('');
          setPassword('');
          await loadPasswords(query.trim());
        }} />
      </div>
    </ManagedSettingRow>
  );
}

function PasswordRow(
    {isRevealed, loadPasswords, password, providerStatus, query, setRevealedIds, store}: {
      isRevealed: boolean;
      loadPasswords: (searchQuery: string) => Promise<void>;
      password: SavedPassword;
      providerStatus: PasswordProviderStatus;
      query: string;
      setRevealedIds: React.Dispatch<React.SetStateAction<Set<string>>>;
      store: MahoSettingsStore;
    }) {
  const handler = store.getHandler();
  const [draftUsername, setDraftUsername] = useState(password.username);

  useEffect(() => {
    setDraftUsername(password.username);
  }, [password.username]);

  const description = [
    password.createdAt ? `Created ${password.createdAt}` : '',
    password.lastUsed ? `Last used ${password.lastUsed}` : '',
  ].filter(Boolean).join(' · ');

  return (
    <ManagedSettingRow
      controlClassName={SETTING_CONTROL_STACKED_CLASS}
      description={description || 'No usage history'}
      title={`${password.username || 'Unknown user'} @ ${password.domain || 'Unknown domain'}`}>
      <p className={SETTING_DESCRIPTION_CLASS}>
        {password.password ? (isRevealed ? `Password: ${password.password}` : 'Password hidden') : 'No password stored.'}
      </p>
      <div className={SETTING_ACTIONS_CLASS}>
        {providerStatus.capabilities.canEditSavedPasswords ? (
          <>
            <TextFieldShell
              ariaLabel={`Username for ${password.domain}`}
              placeholder="Username"
              value={draftUsername}
              onChange={setDraftUsername}
            />
            <ActionButton label="Save Username" onClick={async () => {
              const success = await handler.updatePasswordUsername(password.id, draftUsername.trim());
              if (!success.success) {
                return;
              }
              await loadPasswords(query.trim());
            }} />
          </>
        ) : null}
        {providerStatus.capabilities.canRevealPasswords && password.password ? (
          <ActionButton label={isRevealed ? 'Hide' : 'Reveal'} onClick={async () => {
            setRevealedIds(current => {
              const next = new Set(current);
              if (next.has(password.id)) {
                next.delete(password.id);
              } else {
                next.add(password.id);
              }
              return next;
            });
          }} />
        ) : null}
        <ActionButton label="Delete" onClick={async () => {
          const success = await handler.deletePassword(password.id);
          if (!success.success) {
            return;
          }
          setRevealedIds(current => {
            const next = new Set(current);
            next.delete(password.id);
            return next;
          });
          await loadPasswords(query.trim());
        }} />
      </div>
    </ManagedSettingRow>
  );
}

function ContentBlockerPane({pane, store}: {pane: PaneDefinition; store: MahoSettingsStore}) {
  const handler = store.getHandler();
  const [form, setForm] = useState({name: '', url: ''});
  const [state, setState] = useState<LoadState<ContentBlockerPaneData>>(createLoadingState());

  const loadContentBlocker = useCallback(async () => {
    setState(createLoadingState());
    try {
      const [{lists}, {stats}] = await Promise.all([
        handler.getFilterLists(),
        handler.getContentBlockerStats(),
      ]);
      setState(createLoadedState({lists, stats}));
    } catch {
      setState(createErrorState('Failed to load content blocker.'));
    }
  }, [handler]);

  useEffect(() => {
    void loadContentBlocker();
  }, [loadContentBlocker]);

  return (
    <DomainPaneSection pane={pane}>
      {state.status === 'loading' ? <PaneLoading message="Loading content blocker…" /> : null}
      {state.status === 'error' ? (
        <PaneRetry message={state.message} title="Content blocker unavailable" onRetry={() => void loadContentBlocker()} />
      ) : null}
      {state.status === 'loaded' ? (
        <>
          <SectionHeader title="Content blocker" />
          <ManagedSettingRow
            controlClassName={SETTING_ACTIONS_CLASS}
            description={`${state.data.stats.totalRuleCount.toLocaleString()} rules active · ${state.data.stats.filterListCount} filter list(s)`}
            isFirstRow
            title="Engine status">
            <ActionButton label="Rebuild" onClick={async () => {
              await handler.rebuildContentRules();
              await loadContentBlocker();
            }} />
          </ManagedSettingRow>
          <SectionHeader spaced title="Filter lists" />
          <div className={ROWS_CLASS}>
            {state.data.lists.length === 0 ? <EmptyDescription message="No filter lists configured." /> : null}
            {state.data.lists.map((filterList, index) => (
              <ManagedSettingRow
                key={filterList.id}
                controlClassName={SETTING_ACTIONS_CLASS}
                description={[filterList.url, `${filterList.ruleCount.toLocaleString()} rules`, filterList.enabled ? 'Enabled' : 'Disabled'].filter(Boolean).join(' · ')}
                isFirstRow={index === 0}
                title={filterList.name || filterList.id}>
                <Toggle
                  enabled={filterList.enabled}
                  label={`Enable ${filterList.name || filterList.id}`}
                  onToggle={async nextEnabled => {
                    await handler.toggleFilterList(filterList.id, nextEnabled);
                    await loadContentBlocker();
                  }}
                />
                <ActionButton
                  ariaLabel={`Remove filter list ${filterList.name || filterList.id}`}
                  label="Remove"
                  onClick={async () => {
                    await handler.removeFilterList(filterList.id);
                    await loadContentBlocker();
                  }}
                />
              </ManagedSettingRow>
            ))}
            <ManagedSettingRow
              controlClassName={SETTING_CONTROL_STACKED_CLASS}
              description="Add a custom filter list by name and source URL."
              isFirstRow={state.data.lists.length === 0}
              title="Add filter list">
              <div className={SETTING_CONTROL_STACK_CLASS}>
                <TextFieldShell ariaLabel="Filter list name" placeholder="Filter list name" value={form.name} onChange={value => setForm(current => ({...current, name: value}))} />
                <TextFieldShell ariaLabel="Filter list URL" placeholder="https://example.com/filters.txt" value={form.url} onChange={value => setForm(current => ({...current, url: value}))} />
              </div>
              <div className={SETTING_ACTIONS_CLASS}>
                <ActionButton label="Add filter list" onClick={async () => {
                  const name = form.name.trim();
                  const url = form.url.trim();
                  if (!name || !url) {
                    return;
                  }
                  await handler.addFilterList(createTransientId('filter-list'), name, url);
                  setForm({name: '', url: ''});
                  await loadContentBlocker();
                }} />
              </div>
            </ManagedSettingRow>
          </div>
        </>
      ) : null}
    </DomainPaneSection>
  );
}

function ExtensionsPane({pane, store}: {pane: PaneDefinition; store: MahoSettingsStore}) {
  const handler = store.getHandler();
  const callbackRouter = store.getCallbackRouter();
  const [state, setState] = useState<LoadState<InstalledExtension[]>>(createLoadingState());
  const [bannerDismissed, setBannerDismissed] = useState(false);

  const loadExtensions = useCallback(async () => {
    setState(createLoadingState());
    try {
      const {extensions} = await handler.getInstalledExtensions();
      setState(createLoadedState(extensions));
    } catch {
      setState(createErrorState('Failed to load extensions.'));
    }
  }, [handler]);

  useEffect(() => {
    void loadExtensions();
  }, [loadExtensions]);

  useEffect(() => {
    const onExtensionsChanged = (extensions: InstalledExtension[]) => {
      setState(createLoadedState(extensions));
    };
    callbackRouter.installedExtensionsChanged.addListener(onExtensionsChanged);
    return () => {
      callbackRouter.installedExtensionsChanged.removeListener(onExtensionsChanged);
    };
  }, [callbackRouter]);

  const hasMv2Extensions = state.status === 'loaded' && state.data.some(e => e.manifestVersion === 2);

  return (
    <DomainPaneSection pane={pane}>
      <SectionHeader title="Installed extensions" />
      {state.status === 'loading' ? <PaneLoading message="Loading extensions…" /> : null}
      {state.status === 'error' ? (
        <PaneRetry message={state.message} title="Extensions unavailable" onRetry={() => void loadExtensions()} />
      ) : null}
      {state.status === 'loaded' ? (
        <div className={ROWS_CLASS}>
          {hasMv2Extensions && !bannerDismissed ? (
            <Alert className="mb-4 flex items-start gap-3 border-amber-200/50 bg-amber-500/5 text-amber-900 dark:text-amber-200">
              <AlertCircle className="h-5 w-5 mt-0.5 text-amber-500" />
              <div className="flex-1">
                <AlertTitle className="font-semibold text-amber-800 dark:text-amber-300">Manifest V2 Support Ending</AlertTitle>
                <AlertDescription className="text-sm mt-1 leading-relaxed opacity-90">
                  Some of your installed extensions are using Manifest V2, which will soon be deprecated. Please migrate to Manifest V3 versions to ensure they continue working.{' '}
                  <a
                    href="https://developer.chrome.com/docs/extensions/develop/migrate"
                    target="_blank"
                    rel="noreferrer"
                    className="underline font-semibold hover:opacity-80 inline-flex items-center gap-0.5"
                  >
                    Learn more <ExternalLink className="h-3 w-3 inline" />
                  </a>
                </AlertDescription>
              </div>
              <Button
                variant="ghost"
                size="sm"
                className="h-8 px-2 text-amber-700 hover:text-amber-800 dark:text-amber-400 dark:hover:text-amber-300 hover:bg-amber-500/10"
                onClick={() => setBannerDismissed(true)}
              >
                Dismiss
              </Button>
            </Alert>
          ) : null}
          {state.data.length === 0 ? <EmptyDescription message="No extensions installed." /> : null}
          {state.data.map((extension, index) => {
            const isMv2 = extension.manifestVersion === 2;
            const descriptionParts = [
              extension.version ? `v${extension.version}` : '',
              extension.enabled ? 'Enabled' : 'Disabled',
              extension.description || '',
            ].filter(Boolean);

            const descriptionNode = (
              <span className="flex flex-col gap-1.5">
                <span>{descriptionParts.join(' · ')}</span>
                {isMv2 ? (
                  <span className="inline-flex">
                    <Badge variant="warning" className="text-[10px] py-0 px-1.5 h-4 font-semibold uppercase tracking-wider bg-amber-500/15 text-amber-600 dark:text-amber-400 border border-amber-500/20">
                      Manifest V2 (deprecated)
                    </Badge>
                  </span>
                ) : null}
              </span>
            );

            return (
              <ManagedSettingRow
                key={extension.id}
                controlClassName={SETTING_ACTIONS_CLASS}
                description={descriptionNode}
                isFirstRow={index === 0}
                title={extension.name || extension.id}>
                <Toggle
                  enabled={extension.enabled}
                  label={`Enable ${extension.name || extension.id}`}
                  onToggle={async () => {
                    await handler.toggleExtension(extension.id);
                    await loadExtensions();
                  }}
                />
                <ActionButton
                  ariaLabel={`Remove extension ${extension.name || extension.id}`}
                  label="Remove"
                  onClick={async () => {
                    await handler.removeExtension(extension.id);
                    await loadExtensions();
                  }}
                />
              </ManagedSettingRow>
            );
          })}
          <ManagedSettingRow
            controlClassName={SETTING_ACTIONS_CLASS}
            isFirstRow={state.data.length === 0}
            title="Advanced extension controls"
            description="Access full extension settings and developer features.">
            <ActionButton
              ariaLabel="Open extensions page"
              label="Manage"
              onClick={() => {
                handler.openExtensionsPage();
              }}
            />
          </ManagedSettingRow>
        </div>
      ) : null}
    </DomainPaneSection>
  );
}
