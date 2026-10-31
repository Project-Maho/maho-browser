// Copyright 2026 Maho Browser. All rights reserved.

import React, {useCallback, useEffect, useRef, useState} from 'react';

import {PasswordProviderKind, VaultAgentPolicy} from '../mojo.js';
import type {SettingValue} from '../mojo.js';
import type {PaneDefinition} from '../models.js';
import {VAULT_AUTO_LOCK_OPTIONS} from '../schema/setting_schema.js';
import {
  ActionButton,
  ManagedSettingRow,
  PaneLoading,
  PaneRetry,
  PaneShell,
  SectionCard,
  SelectShell,
  Toggle,
} from './domain_panes.js';
import {ImportPasswordsSection} from './passwords_import_flow.js';
import {
  createPasswordErrorState,
  createPasswordLoadedState,
  createPasswordLoadingState,
  getPasswordProviderBackendValue,
  getPasswordsEnabled,
  getVaultAutoLockMinutes,
  getVaultDeviceAuthRequired,
} from './passwords_model.js';
import type {
  PasswordSettingsPaneData,
  PasswordProviderBackendValue,
  PasswordsLoadState,
} from './passwords_model.js';
import type {MahoSettingsStore} from './store.js';

type LoadPasswordSettings = (showLoading?: boolean) => Promise<void>;

type VaultPolicyBackendValue =
    | 'deny'
    | 'ask_every_use'
    | 'allow_for_task'
    | 'while_unlocked'
    | 'always_allow';

const VAULT_POLICY_VALUES = {
  [VaultAgentPolicy.kDeny]: 'deny',
  [VaultAgentPolicy.kAskEveryUse]: 'ask_every_use',
  [VaultAgentPolicy.kAllowForTask]: 'allow_for_task',
  [VaultAgentPolicy.kWhileUnlocked]: 'while_unlocked',
  [VaultAgentPolicy.kAlwaysAllow]: 'always_allow',
} as const satisfies Record<VaultAgentPolicy, VaultPolicyBackendValue>;

const VAULT_POLICY_FROM_VALUE = {
  deny: VaultAgentPolicy.kDeny,
  ask_every_use: VaultAgentPolicy.kAskEveryUse,
  allow_for_task: VaultAgentPolicy.kAllowForTask,
  while_unlocked: VaultAgentPolicy.kWhileUnlocked,
  always_allow: VaultAgentPolicy.kAlwaysAllow,
} as const satisfies Record<VaultPolicyBackendValue, VaultAgentPolicy>;

const VAULT_POLICY_OPTIONS: readonly {
  readonly description: string;
  readonly label: string;
  readonly value: VaultPolicyBackendValue;
}[] = [
  {label: 'Deny', value: 'deny', description: 'Default fail-closed policy.'},
  {label: 'Ask every use', value: 'ask_every_use', description: 'Require a fresh decision for each request.'},
  {label: 'Allow for task', value: 'allow_for_task', description: 'Persist the task-scoped preference only.'},
  {label: 'While unlocked', value: 'while_unlocked', description: 'Permit while this Vault session stays unlocked.'},
  {label: 'Always allow', value: 'always_allow', description: 'Persist the broadest default policy.'},
];

export function PasswordsPane(
    {pane, settings, store}: {
      readonly pane: PaneDefinition;
      readonly settings: readonly SettingValue[];
      readonly store: MahoSettingsStore;
    }) {
  const handler = store.getHandler();
  const [loadState, setLoadState] = useState<PasswordsLoadState<PasswordSettingsPaneData>>(
      createPasswordLoadingState());
  const [passwordsEnabledOverride, setPasswordsEnabledOverride] = useState<boolean | null>(null);
  const [vaultAutoLockOverride, setVaultAutoLockOverride] = useState<string | null>(null);
  const [vaultDeviceAuthOverride, setVaultDeviceAuthOverride] = useState<boolean | null>(null);
  const hasLoadedOnceRef = useRef(false);
  const settingsRequestIdRef = useRef(0);
  const passwordsEnabled = passwordsEnabledOverride ?? getPasswordsEnabled(settings);
  const vaultAutoLockMinutes = vaultAutoLockOverride ?? getVaultAutoLockMinutes(settings);
  const vaultDeviceAuthRequired = vaultDeviceAuthOverride ?? getVaultDeviceAuthRequired(settings);

  useEffect(() => {
    setPasswordsEnabledOverride(null);
    setVaultAutoLockOverride(null);
    setVaultDeviceAuthOverride(null);
  }, [settings]);

  const loadPasswordSettings: LoadPasswordSettings = useCallback(async (showLoading = true) => {
    const requestId = ++settingsRequestIdRef.current;
    if (showLoading) {
      setLoadState(createPasswordLoadingState());
    }

    try {
      const [providerResult, optionsResult, policyResult] = await Promise.all([
        handler.getPasswordProviderStatus(),
        handler.getPasswordProviderOptions(),
        handler.getVaultPolicyStatus(),
      ]);

      if (requestId !== settingsRequestIdRef.current) {
        return;
      }

      hasLoadedOnceRef.current = true;
      setLoadState(createPasswordLoadedState({
        providerStatus: providerResult.status,
        providerOptions: optionsResult.options,
        policyStatus: policyResult.status,
      }));
    } catch (error) {
      if (requestId !== settingsRequestIdRef.current) {
        return;
      }

      hasLoadedOnceRef.current = true;
      const message = error instanceof Error ? error.message : 'Failed to load password settings.';
      setLoadState(createPasswordErrorState(message));
    }
  }, [handler]);

  useEffect(() => {
    void loadPasswordSettings(!hasLoadedOnceRef.current);
  }, [loadPasswordSettings]);

  return (
    <PaneShell pane={pane}>
      {loadState.status === 'loading' ? <PaneLoading message="Loading password provider…" /> : null}
      {loadState.status === 'error' ? (
        <PaneRetry message={loadState.message} title="Passwords unavailable" onRetry={() => void loadPasswordSettings(true)} />
      ) : null}
      {loadState.status === 'loaded' ? (
        <>
          <PasswordSettingsCard
            data={loadState.data}
            loadPasswordSettings={loadPasswordSettings}
            passwordsEnabled={passwordsEnabled}
            setPasswordsEnabledOverride={setPasswordsEnabledOverride}
            vaultAutoLockMinutes={vaultAutoLockMinutes}
            vaultDeviceAuthRequired={vaultDeviceAuthRequired}
            setVaultAutoLockOverride={setVaultAutoLockOverride}
            setVaultDeviceAuthOverride={setVaultDeviceAuthOverride}
            store={store}
          />
          <ImportPasswordsSection
            store={store}
            onCommitted={() => loadPasswordSettings(false)}
          />
        </>
      ) : null}
    </PaneShell>
  );
}

type PasswordSettingsCardProps = {
  readonly data: PasswordSettingsPaneData;
  readonly loadPasswordSettings: LoadPasswordSettings;
  readonly passwordsEnabled: boolean;
  readonly setPasswordsEnabledOverride: (value: boolean) => void;
  readonly vaultAutoLockMinutes: string;
  readonly vaultDeviceAuthRequired: boolean;
  readonly setVaultAutoLockOverride: (value: string) => void;
  readonly setVaultDeviceAuthOverride: (value: boolean) => void;
  readonly store: MahoSettingsStore;
};

function PasswordSettingsCard(
    {data, loadPasswordSettings, passwordsEnabled, setPasswordsEnabledOverride,
     vaultAutoLockMinutes, vaultDeviceAuthRequired, setVaultAutoLockOverride,
     setVaultDeviceAuthOverride, store}:
        PasswordSettingsCardProps) {
  const handler = store.getHandler();

  return (
    <SectionCard
      title="Password settings"
      description={`Current provider: ${data.providerStatus.displayName}. Saved logins are managed in the Saved Passwords pane.`}
    >
      <ManagedSettingRow
        description="Enable password management to show saved passwords."
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
        description={data.providerStatus.description}
        title="Password provider">
        <SelectShell<PasswordProviderBackendValue>
          ariaLabel="Password provider"
          options={data.providerOptions.map(opt => ({
            label: opt.displayName,
            value: getPasswordProviderBackendValue(opt.provider),
            description: opt.isAvailable ? opt.description :
                `${opt.description} Extension not installed or disabled.`,
          }))}
          value={getPasswordProviderBackendValue(data.providerStatus.provider)}
          onChange={async nextValue => {
            const success = await store.commitSettingValue('autofill.password_provider', nextValue);
            if (success) {
              void loadPasswordSettings(false);
            }
          }}
        />
      </ManagedSettingRow>
      <ManagedSettingRow
        description="Lock the Vault automatically after this much inactivity."
        title="Lock Maho Vault">
        <SelectShell<string>
          ariaLabel="Lock Maho Vault after"
          options={[...VAULT_AUTO_LOCK_OPTIONS]}
          value={vaultAutoLockMinutes}
          onChange={async nextValue => {
            const success = await store.commitSettingValue(
                'autofill.vault_auto_lock_minutes', nextValue);
            if (success) {
              setVaultAutoLockOverride(nextValue);
            }
          }}
        />
      </ManagedSettingRow>
      <ManagedSettingRow
        description="Ask for Touch ID (or your device passcode) before filling, copying, or changing saved passwords."
        title="Require device authentication">
        <Toggle
          enabled={vaultDeviceAuthRequired}
          label="Require device authentication"
          onToggle={async nextEnabled => {
            const success = await store.commitSettingValue(
                'autofill.vault_require_device_auth', nextEnabled ? 'true' : 'false');
            if (success) {
              setVaultDeviceAuthOverride(nextEnabled);
            }
          }}
        />
      </ManagedSettingRow>
      <ManagedSettingRow
        description={data.policyStatus.isAvailable
            ? 'Default policy for future Vault agent requests. Agent grants are not implemented in this build.'
            : data.policyStatus.unavailableReason || 'Vault policy backend unavailable.'}
        title="Vault agent policy">
        {data.policyStatus.isAvailable ? (
          <SelectShell<VaultPolicyBackendValue>
            ariaLabel="Vault agent policy"
            options={[...VAULT_POLICY_OPTIONS]}
            value={VAULT_POLICY_VALUES[data.policyStatus.policy]}
            onChange={async nextValue => {
              const {status} = await handler.setVaultPolicy(
                  VAULT_POLICY_FROM_VALUE[nextValue], null, null, null);
              if (status.isAvailable) {
                void loadPasswordSettings(false);
              }
            }}
          />
        ) : (
          <div className="text-sm text-muted-foreground">Policy controls unavailable.</div>
        )}
      </ManagedSettingRow>
      {data.providerStatus.provider !== PasswordProviderKind.kMahoNative &&
      !data.providerStatus.isAvailable ? (
        <div className="mx-6 my-4 p-4 rounded-lg bg-destructive/10 border border-destructive/20 text-destructive flex flex-col gap-2">
          <div className="text-sm font-medium">
            Selected provider extension ({data.providerStatus.displayName}) is not installed or disabled.
          </div>
          <div className="flex gap-2">
            <ActionButton
              label="Manage Extensions"
              onClick={() => {
                void handler.openExtensionsPage();
              }}
            />
          </div>
        </div>
      ) : null}
      <div className="px-6 py-4 text-xs leading-5 text-muted-foreground border-t border-border">
        {`Provider metadata: ${data.providerStatus.capabilities.canListSavedPasswords ? 'listing supported' : 'listing unavailable'}, ${data.providerStatus.capabilities.canSearchSavedPasswords ? 'lookup supported' : 'lookup unavailable'}, ${data.providerStatus.capabilities.canEditSavedPasswords ? 'metadata updates supported' : 'metadata updates unavailable'}.`}
      </div>
      <ManagedSettingRow
        description="Open the dedicated Identity pane for stored login management."
        title="Saved Passwords management">
        <ActionButton label="Open Saved Passwords" onClick={() => store.selectPane('saved-passwords')} />
      </ManagedSettingRow>
    </SectionCard>
  );
}
