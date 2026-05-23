import React, {useCallback, useEffect, useMemo, useRef, useState} from 'react';

import {Button, TextInput, joinClassNames} from '../../maho_common/react/primitives/index.js';
import {PasswordProviderKind} from '../mojo.js';
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
  ThemeStoreEntry,
} from '../mojo.js';
import type {PaneDefinition} from '../models.js';
import type {MahoSettingsStore} from './store.js';

type LoadState<T> =
    | {status: 'loading'}
    | {status: 'error'; message: string}
    | {data: T; status: 'loaded'};

interface ThemeStoreData {
  spaces: SpaceBasicInfo[];
  success: boolean;
  themes: ThemeStoreEntry[];
}

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

function formatShortcutAction(action: string): string {
  return action.replace(/_/g, ' ').replace(/\b\w/g, letter => letter.toUpperCase());
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

export function DomainPaneContent(
    {pane, settings, store}: {
      pane: PaneDefinition;
      settings: SettingValue[];
      store: MahoSettingsStore;
    }) {
  switch (pane.contentKind) {
    case 'profiles':
      return <ProfilesPane pane={pane} store={store} />;
    case 'sync':
      return <SyncPane pane={pane} store={store} />;
    case 'passwords':
      return <PasswordsPane pane={pane} settings={settings} store={store} />;
    case 'autofill':
      return <AutofillPane pane={pane} store={store} />;
    case 'theme-store':
      return <ThemeStorePane pane={pane} store={store} />;
    case 'shortcuts':
      return <ShortcutsPane pane={pane} store={store} />;
    case 'ai':
      return <AiPane pane={pane} store={store} />;
    case 'content-blocker':
      return <ContentBlockerPane pane={pane} store={store} />;
    case 'extensions':
      return <ExtensionsPane pane={pane} store={store} />;
    case 'atc':
      return <ATCPane pane={pane} store={store} />;
    default:
      return (
        <DomainPaneSection pane={pane}>
          <p className="setting-row__description">This pane is not available.</p>
        </DomainPaneSection>
      );
  }
}

function DomainPaneSection(
    {children, pane}: {children: React.ReactNode; pane: PaneDefinition}) {
  return (
    <section className="preferences-section" data-pane={pane.key} id={`section-${pane.key}`}>
      <div className="preferences-card preferences-card--pane-shell">
        <header className="preferences-section__header">
          <h2 className="preferences-section__title">{pane.title}</h2>
        </header>
        <div className="cards-grid">
          <article className="content-card content-card--wide">{children}</article>
        </div>
      </div>
    </section>
  );
}

function PaneLoading({message}: {message: string}) {
  return <p className="setting-row__description">{message}</p>;
}

function PaneRetry(
    {message, onRetry, title}: {
      message: string;
      onRetry: () => void;
      title: string;
    }) {
  return (
    <section className="setting-row setting-row--first">
      <div className="setting-row__copy">
        <h4 className="setting-row__title">{title}</h4>
        <p className="setting-row__description">{message}</p>
      </div>
      <div className="setting-row__control setting-actions">
        <ActionButton label="Retry" onClick={onRetry} />
      </div>
    </section>
  );
}

function EmptyDescription({message}: {message: string}) {
  return <p className="setting-row__description">{message}</p>;
}

function SectionHeader(
    {action, spaced = false, title}: {
      action?: React.ReactNode;
      spaced?: boolean;
      title: string;
    }) {
  return (
    <div className={spaced ? 'content-card__header content-card__header--spaced' : 'content-card__header'}>
      <h3 className="content-card__title">{title}</h3>
      {action ?? null}
    </div>
  );
}

function ActionButton(
    {ariaLabel, className, disabled, label, onClick, style}: {
      ariaLabel?: string;
      className?: string;
      disabled?: boolean;
      label: string;
      onClick: () => void | Promise<void>;
      style?: React.CSSProperties;
    }) {
  const [isBusy, setIsBusy] = useState(false);
  const mountedRef = useMountedRef();

  return (
    <Button
      aria-label={ariaLabel}
      className={joinClassNames('setting-switch', className)}
      disabled={disabled || isBusy}
      style={style}
      type="button"
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

function AsyncToggleButton(
    {enabled, label, onToggle}: {
      enabled: boolean;
      label: string;
      onToggle: (nextEnabled: boolean) => Promise<void> | void;
    }) {
  const [isBusy, setIsBusy] = useState(false);
  const mountedRef = useMountedRef();

  return (
    <Button
      aria-checked={enabled}
      aria-label={label}
      className={joinClassNames('setting-switch', enabled ? 'is-on' : undefined)}
      disabled={isBusy}
      role="switch"
      type="button"
      onClick={async () => {
        if (isBusy) {
          return;
        }
        if (mountedRef.current) {
          setIsBusy(true);
        }
        try {
          await onToggle(!enabled);
        } finally {
          if (mountedRef.current) {
            setIsBusy(false);
          }
        }
      }}>
      <span className="setting-switch__track">
        <span className="setting-switch__thumb" />
      </span>
      <span className="setting-switch__status">{enabled ? 'On' : 'Off'}</span>
    </Button>
  );
}

function ManagedSettingRow(
    {children, controlClassName, description, isFirstRow = false, title}: {
      children?: React.ReactNode;
      controlClassName?: string;
      description: string;
      isFirstRow?: boolean;
      title: string;
    }) {
  return (
    <section className={joinClassNames('setting-row', isFirstRow ? 'setting-row--first' : undefined)}>
      <div className="setting-row__copy">
        <h4 className="setting-row__title">{title}</h4>
        <p className="setting-row__description">{description}</p>
      </div>
      <div className={joinClassNames('setting-row__control', controlClassName)}>{children}</div>
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
    <label className="setting-input-shell">
      <TextInput
        aria-label={ariaLabel}
        className="setting-input"
        placeholder={placeholder}
        type={type}
        value={value}
        onChange={event => onChange(event.currentTarget.value)}
      />
    </label>
  );
}

function SelectShell<T extends string>(
    {ariaLabel, onChange, options, value}: {
      ariaLabel: string;
      onChange: (value: T) => void;
      options: Array<{label: string; value: T}>;
      value: T;
    }) {
  return (
    <div className="setting-input-shell setting-input-shell--select">
      <select
        aria-label={ariaLabel}
        className="setting-input setting-select"
        value={value}
        onChange={event => onChange(event.currentTarget.value as T)}>
        {options.map(option => (
          <option key={option.value} value={option.value}>{option.label}</option>
        ))}
      </select>
      <span aria-hidden="true" className="setting-input-shell__suffix setting-input-shell__suffix--icon">▾</span>
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
      <SectionHeader title="Browser Profiles" />
      {state.status === 'loading' ? <PaneLoading message="Loading profiles…" /> : null}
      {state.status === 'error' ? (
        <PaneRetry message={state.message} title="Profiles unavailable" onRetry={() => void loadProfiles()} />
      ) : null}
      {state.status === 'loaded' ? (
        <div className="preferences-card__rows">
          {state.data.length === 0 ? <EmptyDescription message="No profiles configured." /> : null}
          {state.data.map(profile => (
            <ProfileRow key={profile.id} profile={profile} onReload={loadProfiles} store={store} />
          ))}
          <ManagedSettingRow
            controlClassName="setting-actions"
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
      controlClassName="setting-actions"
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
          label="×"
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
      <SectionHeader title="URL Routing Rules" />
      {rulesState.status === 'loading' ? <PaneLoading message="Loading rules…" /> : null}
      {rulesState.status === 'error' ? (
        <PaneRetry message={rulesState.message} title="Rules unavailable" onRetry={() => void loadRules()} />
      ) : null}
      {rulesState.status === 'loaded' ? (
        <div className="preferences-card__rows">
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
            controlClassName="setting-actions"
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
      controlClassName="setting-actions"
      description={`→ ${rule.targetSpaceId}`}
      title={rule.urlPattern}>
      <AsyncToggleButton enabled={rule.enabled} label={rule.urlPattern} onToggle={onToggle} />
      <ActionButton ariaLabel="Remove rule" label="×" onClick={onRemove} />
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
      setState(createErrorState('Failed to load AI settings.'));
    }
  }, [handler]);

  useEffect(() => {
    void loadSettings();
  }, [loadSettings]);

  const commit = useCallback(async (action: () => Promise<void>) => {
    try {
      setActionError(null);
      await action();
      await loadSettings();
    } catch {
      setActionError('Failed to save AI settings.');
    }
  }, [loadSettings]);

  return (
    <DomainPaneSection pane={pane}>
      <SectionHeader title="AI Configuration" />
      {actionError ? <p className="setting-row__description">{actionError}</p> : null}
      {state.status === 'loading' ? <PaneLoading message="Loading AI settings…" /> : null}
      {state.status === 'error' ? (
        <PaneRetry message={state.message} title="AI settings unavailable" onRetry={() => void loadSettings()} />
      ) : null}
      {state.status === 'loaded' ? (
        <div className="preferences-card__rows">
          <AiSelectRow
            label="Provider"
            initialValue={state.data.provider}
            options={[
              {value: '', label: '(not set)'},
              {value: 'anthropic', label: 'Anthropic'},
              {value: 'openai', label: 'OpenAI'},
              {value: 'openai-compatible', label: 'OpenAI-compatible (custom URL)'},
              {value: 'google', label: 'Google (Gemini)'},
            ]}
            onCommit={value => commit(async () => handler.setAIProvider(value))}
          />
          <AiFieldRow
            initialValue={state.data.baseUrl}
            label="Base URL"
            placeholder="(provider default)"
            onCommit={value => commit(async () => handler.setAIBaseUrl(value))}
          />
          <AiFieldRow
            initialValue={state.data.hasApiKey ? '••••••••' : ''}
            label="API Key"
            placeholder="Paste your API key"
            type="password"
            onCommit={value => commit(async () => handler.setAIApiKey(value))}
          />
          <AiFieldRow
            initialValue={state.data.model}
            label="Model"
            placeholder="e.g. claude-sonnet-4-20250514 or gpt-4o"
            onCommit={value => commit(async () => handler.setAIModel(value))}
          />
          <AiFieldRow
            initialValue={state.data.approvalPolicy}
            label="Approval Policy"
            onCommit={value => commit(async () => handler.setAIApprovalPolicy(value))}
          />
          <ManagedSettingRow
            description="Enable or disable AI session persistence."
            title="Session Persistence">
            <AsyncToggleButton
              enabled={state.data.sessionPersistenceEnabled}
              label="Session Persistence"
              onToggle={enabled => commit(async () => handler.setAISessionPersistence(enabled))}
            />
          </ManagedSettingRow>
        </div>
      ) : null}
    </DomainPaneSection>
  );
}

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

  return (
    <ManagedSettingRow description="" title={label}>
      <TextFieldShell ariaLabel={label} placeholder={placeholder ?? label} type={type} value={draft} onChange={setDraft} />
      <ActionButton label="Save" onClick={() => onCommit(draft)} />
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
      <select
        aria-label={label}
        className="text-field-shell"
        value={draft}
        onChange={e => {
          setDraft(e.target.value);
          void onCommit(e.target.value);
        }}>
        {options.map(o => (
          <option key={o.value} value={o.value}>{o.label}</option>
        ))}
      </select>
    </ManagedSettingRow>
  );
}

function ShortcutsPane({pane, store}: {pane: PaneDefinition; store: MahoSettingsStore}) {
  const handler = store.getHandler();
  const [state, setState] = useState<LoadState<ShortcutBinding[]>>(createLoadingState());

  const loadShortcuts = useCallback(async () => {
    setState(createLoadingState());
    try {
      const {shortcuts} = await handler.getShortcuts();
      setState(createLoadedState(shortcuts));
    } catch {
      setState(createErrorState('Failed to load shortcuts.'));
    }
  }, [handler]);

  useEffect(() => {
    void loadShortcuts();
  }, [loadShortcuts]);

  return (
    <DomainPaneSection pane={pane}>
      <SectionHeader
        action={<ActionButton label="Reset All" onClick={async () => {
          await handler.resetAllShortcuts();
          await loadShortcuts();
        }} />}
        title="Keyboard Shortcuts"
      />
      {state.status === 'loading' ? <PaneLoading message="Loading shortcuts…" /> : null}
      {state.status === 'error' ? (
        <PaneRetry message={state.message} title="Shortcuts unavailable" onRetry={() => void loadShortcuts()} />
      ) : null}
      {state.status === 'loaded' ? (
        <div className="preferences-card__rows">
          {state.data.map(binding => (
            <ShortcutRow key={binding.action} binding={binding} onReload={loadShortcuts} store={store} />
          ))}
        </div>
      ) : null}
    </DomainPaneSection>
  );
}

function ShortcutRow(
    {binding, onReload, store}: {
      binding: ShortcutBinding;
      onReload: () => Promise<void>;
      store: MahoSettingsStore;
    }) {
  const handler = store.getHandler();
  const [draft, setDraft] = useState(binding.keyCombo);

  useEffect(() => {
    setDraft(binding.keyCombo);
  }, [binding.keyCombo]);

  return (
    <ManagedSettingRow
      controlClassName="setting-actions"
      description={!binding.isDefault ? 'Custom binding' : 'Default'}
      title={formatShortcutAction(binding.action)}>
      <AsyncToggleButton
        enabled={binding.enabled}
        label={`Enable ${binding.action}`}
        onToggle={async nextEnabled => {
          await handler.toggleShortcut(binding.action, nextEnabled);
          await onReload();
        }}
      />
      <TextFieldShell
        ariaLabel={`Shortcut for ${binding.action}`}
        placeholder="Shortcut"
        value={draft}
        onChange={setDraft}
      />
      <ActionButton
        label="Save"
        onClick={async () => {
          await handler.setShortcut(binding.action, draft);
          await onReload();
        }}
      />
      <ActionButton
        disabled={binding.isDefault}
        label="Reset"
        onClick={async () => {
          await handler.resetShortcut(binding.action);
          await onReload();
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
      <SectionHeader title="Account & Sync" />
      {state.status === 'loading' ? <PaneLoading message="Loading sync status…" /> : null}
      {state.status === 'error' ? (
        <PaneRetry message={state.message} title="Sync unavailable" onRetry={() => void loadStatus()} />
      ) : null}
      {state.status === 'loaded' ? (
        <div className="preferences-card__rows">
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
                <ManagedSettingRow description="" title="Connected Devices" />
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
              <ManagedSettingRow controlClassName="setting-actions" description="Disconnect this browser from sync." title="Stop Sync">
                <ActionButton label="Stop Sync" onClick={async () => {
                  await handler.stopSync();
                  await loadStatus();
                }} />
              </ManagedSettingRow>
            </>
          ) : (
            <>
              <ManagedSettingRow
                controlClassName="setting-actions"
                description="Generate a recovery phrase for a new sync session."
                title="Start New Sync">
                <ActionButton label={pendingKeyInfo ? 'Generate New Phrase' : 'Generate Recovery Phrase'} onClick={async () => {
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
              <ManagedSettingRow controlClassName="setting-actions" description="Join an existing sync session with a recovery phrase." title="Join Existing Sync">
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
    <ManagedSettingRow controlClassName="setting-actions" description={keyInfo.recoveryPhrase} title="Recovery Phrase">
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

function ThemeStorePane({pane, store}: {pane: PaneDefinition; store: MahoSettingsStore}) {
  const handler = store.getHandler();
  const [selectedSpaceId, setSelectedSpaceId] = useState('');
  const [state, setState] = useState<LoadState<ThemeStoreData>>(createLoadingState());

  const loadThemeStore = useCallback(async () => {
    setState(createLoadingState());
    const [themeStoreResult, spacesResult] = await Promise.allSettled([
      handler.fetchThemeStore(),
      handler.getSpaces(),
    ]);
    const themeStore = themeStoreResult.status === 'fulfilled' ? themeStoreResult.value : null;
    const spaces = spacesResult.status === 'fulfilled' ? spacesResult.value.spaces : [];
    const themes = themeStore?.themes || [];
    const success = themeStore?.success || false;
    setSelectedSpaceId(spaces[0]?.id || '');
    setState(createLoadedState({spaces, success, themes}));
  }, [handler]);

  useEffect(() => {
    void loadThemeStore();
  }, [loadThemeStore]);

  return (
    <DomainPaneSection pane={pane}>
      {state.status === 'loading' ? <PaneLoading message="Loading themes…" /> : null}
      {state.status === 'loaded' && state.data.themes.length === 0 && !state.data.success ? (
        <PaneRetry
          message="We could not load the theme catalog right now. Check your connection and try again."
          title="Theme Store unavailable"
          onRetry={() => void loadThemeStore()}
        />
      ) : null}
      {state.status === 'loaded' && !(state.data.themes.length === 0 && !state.data.success) ? (
        <>
          <SectionHeader
            action={<ActionButton label="Refresh" onClick={() => loadThemeStore()} />}
            title="Community Themes"
          />
          {state.data.themes.length === 0 ? <EmptyDescription message="No themes available." /> : null}
          {state.data.success ? null : (
            <p className="setting-row__description">Showing cached themes while the remote catalog is unavailable.</p>
          )}
          {state.data.spaces.length > 0 ? (
            <ManagedSettingRow controlClassName="setting-actions" description="Select which space should receive the theme." isFirstRow title="Apply to space">
              <SelectShell
                ariaLabel="Target space"
                options={state.data.spaces.map(space => ({label: space.name, value: space.id}))}
                value={selectedSpaceId}
                onChange={setSelectedSpaceId}
              />
            </ManagedSettingRow>
          ) : null}
          <div className="preferences-card__rows">
            {state.data.themes.map(theme => (
              <ThemeRow
                key={theme.id}
                selectedSpaceId={selectedSpaceId}
                store={store}
                theme={theme}
              />
            ))}
          </div>
        </>
      ) : null}
    </DomainPaneSection>
  );
}

function ThemeRow(
    {selectedSpaceId, store, theme}: {
      selectedSpaceId: string;
      store: MahoSettingsStore;
      theme: ThemeStoreEntry;
    }) {
  const handler = store.getHandler();
  const [label, setLabel] = useState('Apply');

  const parts: string[] = [];
  if (theme.author) {
    parts.push(`by ${theme.author}`);
  }
  if (theme.description) {
    parts.push(theme.description);
  }

  return (
    <ManagedSettingRow
      controlClassName="setting-actions"
      description={parts.join(' · ') || 'Community theme'}
      title={theme.name}>
      <ActionButton label={label} onClick={async () => {
        const spaceId = selectedSpaceId;
        if (!spaceId || !theme.themeJson) {
          return;
        }
        setLabel('Applying…');
        const {success} = await handler.applyThemeToSpace(spaceId, theme.themeJson);
        setLabel(success ? 'Applied' : 'Failed');
      }} />
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
          <SectionHeader title="Saved Addresses" />
          <div className="preferences-card__rows">
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
                  controlClassName="setting-actions"
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
              controlClassName="setting-row__control--stacked"
              description="Create a new saved address for form autofill. Fields left blank stay empty."
              isFirstRow={state.data.addresses.length === 0}
              title="Add address">
              <div className="setting-control-stack">
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
              <div className="setting-actions">
                <ActionButton label="Add Address" onClick={async () => {
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
          <SectionHeader spaced title="Payment Methods" />
          <div className="preferences-card__rows">
            {state.data.payments.length === 0 ? <EmptyDescription message="No saved payment methods." /> : null}
            {state.data.payments.map((payment, index) => {
              const descriptionParts = [payment.cardholderName, payment.expiration ? `Exp ${payment.expiration}` : ''].filter(Boolean);
              return (
                <ManagedSettingRow
                  key={payment.id}
                  controlClassName="setting-actions"
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
              controlClassName="setting-row__control--stacked"
              description="Store a payment method for faster checkout autofill."
              isFirstRow={state.data.payments.length === 0}
              title="Add payment method">
              <div className="setting-control-stack">
                <TextFieldShell ariaLabel="Payment card network" placeholder="Card network" value={paymentForm.cardNetwork} onChange={value => setPaymentForm(current => ({...current, cardNetwork: value}))} />
                <TextFieldShell ariaLabel="Payment card last four digits" placeholder="Last four digits" value={paymentForm.lastFour} onChange={value => setPaymentForm(current => ({...current, lastFour: value}))} />
                <TextFieldShell ariaLabel="Payment card expiration" placeholder="Expiration (MM/YY)" value={paymentForm.expiration} onChange={value => setPaymentForm(current => ({...current, expiration: value}))} />
                <TextFieldShell ariaLabel="Payment cardholder name" placeholder="Cardholder name" value={paymentForm.cardholderName} onChange={value => setPaymentForm(current => ({...current, cardholderName: value}))} />
              </div>
              <div className="setting-actions">
                <ActionButton label="Add Payment" onClick={async () => {
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
          <div className="content-card__header">
            <div className="content-card__title-group">
              <h3 className="content-card__title">Saved Passwords</h3>
              <p className="content-card__copy">{`Passwords provider: ${loadState.data.providerStatus.displayName}.`}</p>
            </div>
            <div className="content-card__copy passwords-provider-card">
              <ManagedSettingRow
                description="Enable password management to show saved passwords."
                isFirstRow
                title="Passwords">
                <AsyncToggleButton
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
                <span className="setting-switch">
                  {loadState.data.providerStatus.provider === PasswordProviderKind.kMahoNative ?
                    'Maho Native' :
                    loadState.data.providerStatus.provider === PasswordProviderKind.kBitwarden ?
                      'Bitwarden' :
                      '1Password'}
                </span>
              </ManagedSettingRow>
              <p className="content-card__copy">
                {`Capabilities: ${loadState.data.providerStatus.capabilities.canListSavedPasswords ? 'list' : 'no list'}, ${loadState.data.providerStatus.capabilities.canSearchSavedPasswords ? 'search' : 'no search'}, ${loadState.data.providerStatus.capabilities.canAddSavedPasswords ? 'add' : 'no add'}, ${loadState.data.providerStatus.capabilities.canEditSavedPasswords ? 'edit' : 'no edit'}, ${loadState.data.providerStatus.capabilities.canRevealPasswords ? 'reveal' : 'no reveal'}, ${loadState.data.providerStatus.capabilities.canDeleteSavedPasswords ? 'delete' : 'no delete'}.`}
              </p>
            </div>
          </div>
          <div className="browser-settings-shell browser-settings-shell--wide">
            {!isMahoNativeProvider(loadState.data.providerStatus.provider) ? (
              <>
                <ManagedSettingRow
                  description={`${loadState.data.providerStatus.displayName} handles save, fill, and autosave through its extension. Chromium's built-in password manager is suppressed in this mode, and Maho Native passwords are not manageable here.`}
                  title={`${loadState.data.providerStatus.displayName} is managing passwords`}>
                  <span className="setting-switch">Extension-managed</span>
                </ManagedSettingRow>
                {loadState.data.passwords.length > 0 ? (
                  <p className="setting-row__description">
                    {`${loadState.data.passwords.length} Maho Native password${loadState.data.passwords.length === 1 ? '' : 's'} remain stored locally and can only be managed after switching back to Maho Native.`}
                  </p>
                ) : null}
              </>
            ) : !passwordsEnabled ? (
              <p className="setting-row__description">Passwords is turned off. Turn it on to show saved passwords.</p>
            ) : (
              <div className="cards-grid">
                <div>
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
                </div>
              </div>
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
      <div className="preferences-card__rows">
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
      controlClassName="setting-row__control--stacked"
      description="Store a new native password entry."
      title="Add password">
      <div className="setting-control-stack">
        <TextFieldShell ariaLabel="Password domain" placeholder="Domain" value={domain} onChange={setDomain} />
        <TextFieldShell ariaLabel="Password username" placeholder="Username" value={username} onChange={setUsername} />
        <TextFieldShell ariaLabel="Password value" placeholder="Password" type="password" value={password} onChange={setPassword} />
      </div>
      <div className="setting-actions">
        <ActionButton label="Add Password" onClick={async () => {
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
      controlClassName="setting-row__control--stacked"
      description={description || 'No usage history'}
      title={`${password.username || 'Unknown user'} @ ${password.domain || 'Unknown domain'}`}>
      <p className="setting-row__description">
        {password.password ? (isRevealed ? `Password: ${password.password}` : 'Password hidden') : 'No password stored.'}
      </p>
      <div className="setting-actions">
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
          <SectionHeader title="Content Blocker" />
          <ManagedSettingRow
            controlClassName="setting-actions"
            description={`${state.data.stats.totalRuleCount.toLocaleString()} rules active · ${state.data.stats.filterListCount} filter list(s)`}
            isFirstRow
            title="Engine Status">
            <ActionButton label="Rebuild" onClick={async () => {
              await handler.rebuildContentRules();
              await loadContentBlocker();
            }} />
          </ManagedSettingRow>
          <SectionHeader spaced title="Filter Lists" />
          <div className="preferences-card__rows">
            {state.data.lists.length === 0 ? <EmptyDescription message="No filter lists configured." /> : null}
            {state.data.lists.map((filterList, index) => (
              <ManagedSettingRow
                key={filterList.id}
                controlClassName="setting-actions"
                description={[filterList.url, `${filterList.ruleCount.toLocaleString()} rules`, filterList.enabled ? 'Enabled' : 'Disabled'].filter(Boolean).join(' · ')}
                isFirstRow={index === 0}
                title={filterList.name || filterList.id}>
                <AsyncToggleButton
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
              controlClassName="setting-row__control--stacked"
              description="Add a custom filter list by name and source URL."
              isFirstRow={state.data.lists.length === 0}
              title="Add filter list">
              <div className="setting-control-stack">
                <TextFieldShell ariaLabel="Filter list name" placeholder="Filter list name" value={form.name} onChange={value => setForm(current => ({...current, name: value}))} />
                <TextFieldShell ariaLabel="Filter list URL" placeholder="https://example.com/filters.txt" value={form.url} onChange={value => setForm(current => ({...current, url: value}))} />
              </div>
              <div className="setting-actions">
                <ActionButton label="Add Filter List" onClick={async () => {
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
  const [state, setState] = useState<LoadState<InstalledExtension[]>>(createLoadingState());

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

  return (
    <DomainPaneSection pane={pane}>
      <SectionHeader title="Installed Extensions" />
      {state.status === 'loading' ? <PaneLoading message="Loading extensions…" /> : null}
      {state.status === 'error' ? (
        <PaneRetry message={state.message} title="Extensions unavailable" onRetry={() => void loadExtensions()} />
      ) : null}
      {state.status === 'loaded' ? (
        <div className="preferences-card__rows">
          {state.data.length === 0 ? <EmptyDescription message="No extensions installed." /> : null}
          {state.data.map((extension, index) => {
            const descriptionParts = [
              extension.version ? `v${extension.version}` : '',
              extension.enabled ? 'Enabled' : 'Disabled',
              extension.description || '',
            ].filter(Boolean);

            return (
              <ManagedSettingRow
                key={extension.id}
                controlClassName="setting-actions"
                description={descriptionParts.join(' · ')}
                isFirstRow={index === 0}
                title={extension.name || extension.id}>
                <AsyncToggleButton
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
        </div>
      ) : null}
    </DomainPaneSection>
  );
}
