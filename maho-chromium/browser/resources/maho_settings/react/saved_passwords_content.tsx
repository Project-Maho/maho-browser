// Copyright 2026 Maho Browser. All rights reserved.

import React, {useMemo} from 'react';

import {Button} from '@ui/button';
import {Input} from '@ui/input';
import {AlertCircle, Check, Copy, Edit, Key, Plus, Trash2} from '@icons/lucide';
import {SecretAction, VaultLockState} from '../mojo.js';
import type {VaultItem} from '../mojo.js';
import {
  EmptyDescription,
  ManagedSettingRow,
  SectionCard,
  SETTING_ACTIONS_CLASS,
  SETTING_CONTROL_STACKED_CLASS,
  SETTING_DESCRIPTION_CLASS,
} from './domain_panes.js';
import {isMahoNativeProvider} from './passwords_model.js';
import {GuidedPasswordImportFlow} from './passwords_import_flow.js';
import {VaultAccessGate} from './passwords_vault_gate.js';
import {
  filterSavedPasswordItems,
  getDisplayName,
  getPrimaryOrigin,
} from './saved_passwords_model.js';
import type {
  AddSavedPasswordInput,
  EditSavedPasswordInput,
  LoadSavedPasswords,
  SavedPasswordActionStatus,
  SavedPasswordDrawerState,
  SavedPasswordsPaneData,
} from './saved_passwords_model.js';
import {
  AddSavedPasswordDrawer,
  EditSavedPasswordDrawer,
} from './saved_passwords_drawer.js';
import type {MahoSettingsStore} from './store.js';

const ICON_CLASS = 'size-4';
const SEARCH_INPUT_CLASS = 'bg-background/70';
const STATUS_CLASS = 'mt-3 flex items-start gap-2 text-xs leading-5';

export type SavedPasswordsContentProps = {
  readonly actionStatus: SavedPasswordActionStatus | null;
  readonly data: SavedPasswordsPaneData;
  readonly drawer: SavedPasswordDrawerState;
  readonly loadSavedPasswords: LoadSavedPasswords;
  readonly onAdd: (input: AddSavedPasswordInput) => Promise<string | null>;
  readonly onCloseDrawer: () => void;
  readonly onDelete: (item: VaultItem) => Promise<void>;
  readonly onEdit: (item: VaultItem, input: EditSavedPasswordInput) => Promise<string | null>;
  readonly onOpenAdd: () => void;
  readonly onOpenEdit: (item: VaultItem) => void;
  readonly onSearchChange: (value: string) => void;
  readonly onSecretAction: (item: VaultItem, action: SecretAction) => Promise<void>;
  readonly passwordsEnabled: boolean;
  readonly searchInputValue: string;
  readonly searchQuery: string;
  readonly store: MahoSettingsStore;
};

export function SavedPasswordsContent(props: SavedPasswordsContentProps) {
  const filteredItems = useMemo(
      () => filterSavedPasswordItems(props.data.items, props.searchQuery),
      [props.data.items, props.searchQuery]);
  const lockState = props.data.vaultStatus.status?.lockState ?? VaultLockState.kUninitialized;
  const isNativeProvider = isMahoNativeProvider(props.data.providerStatus.provider);

  if (!isNativeProvider) {
    return <SettingsRedirectCard store={props.store} providerName={props.data.providerStatus.displayName} />;
  }
  if (!props.passwordsEnabled) {
    return <SettingsRedirectCard store={props.store} providerName="Maho Native" />;
  }
  if (lockState !== VaultLockState.kUnlocked) {
    return <SectionCard title="Vault access" description="Unlock the Vault before managing saved passwords.">
      <VaultAccessGate
        loadPasswords={() => props.loadSavedPasswords(true)}
        searchQuery=""
        store={props.store}
        vaultResult={props.data.vaultStatus}
      />
    </SectionCard>;
  }

  return (
    <>
      <SectionCard
        title="Saved Passwords"
        description="Search and manage native Vault logins. Copy and fill actions return status only; passwords are never displayed."
        action={<Button type="button" onClick={props.onOpenAdd}><Plus className={ICON_CLASS} />Add password</Button>}
      >
        <ManagedSettingRow description="Filter by site, title, or username." title="Search saved passwords">
          <Input
            aria-label="Search saved passwords"
            className={SEARCH_INPUT_CLASS}
            placeholder="Search saved passwords"
            type="search"
            value={props.searchInputValue}
            onChange={event => props.onSearchChange(event.currentTarget.value)}
          />
        </ManagedSettingRow>
        {filteredItems.map(item => <SavedPasswordRow
          actionStatus={props.actionStatus}
          item={item}
          key={item.id}
          onDelete={props.onDelete}
          onEdit={props.onOpenEdit}
          onSecretAction={props.onSecretAction}
        />)}
        {filteredItems.length === 0 ? (
          <EmptyDescription message={props.searchQuery ? 'No saved passwords match this search.' : 'No saved passwords yet.'} />
        ) : null}
      </SectionCard>
      <GuidedPasswordImportFlow
        store={props.store}
        onCommitted={() => props.loadSavedPasswords(false)}
      />
      {props.drawer?.kind === 'add' ? (
        <AddSavedPasswordDrawer onAdd={props.onAdd} onClose={props.onCloseDrawer} />
      ) : null}
      {props.drawer?.kind === 'edit' ? (
        <EditSavedPasswordDrawer item={props.drawer.item} onClose={props.onCloseDrawer} onSave={props.onEdit} />
      ) : null}
    </>
  );
}

function SettingsRedirectCard({providerName, store}: {readonly providerName: string; readonly store: MahoSettingsStore}) {
  return <SectionCard title="Saved Passwords" description="Credential management is available when Maho Native passwords are enabled.">
    <ManagedSettingRow
      description={`${providerName} is currently selected. Open password settings to switch providers or enable password management.`}
      title="Password settings required">
      <Button type="button" variant="outline" onClick={() => store.selectPane('passwords')}>Open Passwords</Button>
    </ManagedSettingRow>
  </SectionCard>;
}

type SavedPasswordRowProps = {
  readonly actionStatus: SavedPasswordActionStatus | null;
  readonly item: VaultItem;
  readonly onDelete: (item: VaultItem) => Promise<void>;
  readonly onEdit: (item: VaultItem) => void;
  readonly onSecretAction: (item: VaultItem, action: SecretAction) => Promise<void>;
};

function SavedPasswordRow({actionStatus, item, onDelete, onEdit, onSecretAction}: SavedPasswordRowProps) {
  const origin = getPrimaryOrigin(item) || 'No origin';
  const status = actionStatus?.itemId === item.id ? actionStatus : null;
  return (
    <ManagedSettingRow
      controlClassName={SETTING_CONTROL_STACKED_CLASS}
      description={`Origin: ${origin}`}
      title={getDisplayName(item)}>
      <p className={SETTING_DESCRIPTION_CLASS}>
        {item.usernameHint ? `Username: ${item.usernameHint}` : 'No username saved'}
        {item.updatedAt ? ` · Updated ${item.updatedAt}` : ''}
      </p>
      <div className={SETTING_ACTIONS_CLASS}>
        <Button type="button" variant="outline" onClick={() => void onSecretAction(item, SecretAction.kCopy)}><Copy className={ICON_CLASS} />Copy</Button>
        <Button type="button" variant="outline" onClick={() => void onSecretAction(item, SecretAction.kFill)}><Key className={ICON_CLASS} />Fill</Button>
        <Button type="button" variant="outline" onClick={() => onEdit(item)}><Edit className={ICON_CLASS} />Edit</Button>
        <Button type="button" variant="outline" className="text-destructive hover:bg-destructive/10" onClick={() => void onDelete(item)}><Trash2 className={ICON_CLASS} />Delete</Button>
      </div>
      {status ? (
        <p className={`${STATUS_CLASS} ${status.status === 'success' ? 'text-success' : 'text-destructive'}`}>
          {status.status === 'success' ? <Check className={ICON_CLASS} /> : <AlertCircle className={ICON_CLASS} />}
          {status.message}
        </p>
      ) : null}
    </ManagedSettingRow>
  );
}
