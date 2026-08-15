// Copyright 2026 Maho Browser. All rights reserved.

import React, {useMemo, useState} from 'react';

import {Alert, AlertDescription, AlertTitle} from '@ui/alert';
import {Button} from '@ui/button';
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
  AlertCircle,
  Check,
  Copy,
  Database,
  Edit,
  Lock,
  Plus,
  RefreshCw,
  ShieldAlert,
  Trash2,
} from '@icons/lucide';
import {
  SecretAction,
  VaultLockState,
  VaultPreflightState,
} from '../mojo.js';
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
const CONFIRM_DETAIL_CLASS = 'grid gap-1 rounded-lg border border-border/60 bg-muted/20 p-3 text-xs leading-5';
const CONFIRM_ERROR_CLASS = 'text-xs leading-5 text-destructive';

export function describeDeleteTarget(item: VaultItem): {origin: string; username: string} {
  return {
    origin: getPrimaryOrigin(item) || 'No origin',
    username: item.usernameHint || 'No username saved',
  };
}

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
  /**
   * Monotonic counter the pane bumps whenever transient credential state is
   * reset (most importantly on the canonical Vault lock-state event). A change
   * discards the pending delete confirmation during this render, so a locked
   * Vault can never leave a stale confirmation dialog behind.
   */
  readonly transientResetToken: number;
};

export function SavedPasswordsContent(props: SavedPasswordsContentProps) {
  const [pendingDelete, setPendingDelete] = useState<VaultItem | null>(null);
  const [lastResetToken, setLastResetToken] = useState(props.transientResetToken);
  if (lastResetToken !== props.transientResetToken) {
    setLastResetToken(props.transientResetToken);
    setPendingDelete(null);
  }
  const filteredItems = useMemo(
      () => filterSavedPasswordItems(props.data.items, props.searchQuery),
      [props.data.items, props.searchQuery]);
  const lockState = props.data.vaultStatus.status?.lockState ?? VaultLockState.kUninitialized;
  const isNativeProvider = isMahoNativeProvider(props.data.providerStatus.provider);
  const fatalState = isFatalVaultPreflightState(
      props.data.vaultPreflightState) ? props.data.vaultPreflightState : null;

  // Fatal preflight diagnosis must win over the generic disabled-provider
  // redirect: the provider gate intentionally disables Maho Native for these
  // states, but the Saved Passwords pane still needs to explain why.
  if (fatalState !== null) {
    return <VaultPreflightNotice state={fatalState} />;
  }
  if (!isNativeProvider) {
    return <SettingsRedirectCard store={props.store} providerName={props.data.providerStatus.displayName} />;
  }
  if (!props.passwordsEnabled) {
    return <SettingsRedirectCard store={props.store} providerName="Maho Native" />;
  }
  if (lockState !== VaultLockState.kUnlocked) {
    return <LockedVaultNotice
      loadSavedPasswords={props.loadSavedPasswords}
      store={props.store}
      vaultResult={props.data.vaultStatus}
    />;
  }

  return (
    <>
      <SectionCard
        title="Saved Passwords"
        description="Search and manage native Vault logins. The copy action returns status only; passwords are never displayed."
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
          onDelete={setPendingDelete}
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
      {pendingDelete ? (
        <DeleteSavedPasswordDialog
          item={pendingDelete}
          onCancel={() => setPendingDelete(null)}
          onConfirm={props.onDelete}
        />
      ) : null}
    </>
  );
}

type FatalVaultPreflightState =
    VaultPreflightState.kPlaintextResidue|
    VaultPreflightState.kStructuralCorruption|
    VaultPreflightState.kUnrecoverableKey;

function isFatalVaultPreflightState(
    state: VaultPreflightState): state is FatalVaultPreflightState {
  return state === VaultPreflightState.kUnrecoverableKey ||
      state === VaultPreflightState.kStructuralCorruption ||
      state === VaultPreflightState.kPlaintextResidue;
}

function LockedVaultNotice(
    {loadSavedPasswords, store, vaultResult}: {
      readonly loadSavedPasswords: LoadSavedPasswords;
      readonly store: MahoSettingsStore;
      readonly vaultResult: SavedPasswordsPaneData['vaultStatus'];
    }) {
  return <SectionCard
    title="Vault access"
    description="Unlock the Vault before managing saved passwords."
    headerBanner={
      <Alert className="rounded-none border-0 border-b border-border/60 bg-muted/20">
        <Lock className={ICON_CLASS} />
        <AlertTitle>Maho Vault is locked</AlertTitle>
        <AlertDescription>
          Enter your master passphrase below. The Local Vault stores encrypted
          password data locally on this device; your passphrase is not sent to
          Maho or any server.
        </AlertDescription>
      </Alert>
    }>
    <VaultAccessGate
      loadPasswords={() => loadSavedPasswords(true)}
      searchQuery=""
      store={store}
      vaultResult={vaultResult}
    />
  </SectionCard>;
}

const FATAL_VAULT_NOTICES: Record<FatalVaultPreflightState, {
  readonly actionLabel: string;
  readonly actionUnavailable: string;
  readonly description: string;
  readonly icon: React.ComponentType<{className?: string}>;
  readonly title: string;
  readonly variant: 'destructive'|'warning';
}> = {
  [VaultPreflightState.kUnrecoverableKey]: {
    actionLabel: 'Recreate or delete Vault',
    actionUnavailable:
        'Vault recreation and deletion are not available in Settings yet.',
    description:
        'Maho found encrypted Vault data, but its local database key is missing or unusable. The saved passwords cannot be decrypted. If you have a backup, restore the matching Vault database and key together. Otherwise, delete and recreate the Vault when that recovery action becomes available.',
    icon: ShieldAlert,
    title: 'Vault keys are unavailable',
    variant: 'destructive',
  },
  [VaultPreflightState.kStructuralCorruption]: {
    actionLabel: 'Recover or restore Vault',
    actionUnavailable:
        'Vault recovery and restore are not available in Settings yet.',
    description:
        'Maho could not validate the encrypted Vault database or its schema. Keep the existing Vault files intact. Restore a known-good backup, or use a supported recovery tool when one becomes available.',
    icon: Database,
    title: 'Vault database needs recovery',
    variant: 'destructive',
  },
  [VaultPreflightState.kPlaintextResidue]: {
    actionLabel: 'Scrub plaintext residue',
    actionUnavailable:
        'A safe scrub action is not available in Settings yet.',
    description:
        'Maho detected plaintext Vault data or a leftover plaintext migration backup. Native password saving is disabled to prevent further exposure. Keep the existing encrypted Vault intact and remove the residue with a supported scrub tool when one becomes available.',
    icon: ShieldAlert,
    title: 'Plaintext Vault residue detected',
    variant: 'warning',
  },
};

function VaultPreflightNotice({state}: {readonly state: FatalVaultPreflightState}) {
  const notice = FATAL_VAULT_NOTICES[state];
  const Icon = notice.icon;
  const stateToken = state === VaultPreflightState.kUnrecoverableKey ?
      'unrecoverableKey' :
      state === VaultPreflightState.kStructuralCorruption ?
      'structuralCorruption' : 'plaintextResidue';
  return <SectionCard
    title="Saved Passwords"
    description="Maho Native password management is paused until this Vault issue is resolved."
    padded>
    <Alert
      className={notice.variant === 'warning' ? 'bg-warning/10' : 'bg-destructive/10'}
      data-testid="vault-preflight-notice"
      data-vault-preflight-state={stateToken}
      variant={notice.variant}>
      <Icon className={ICON_CLASS} />
      <AlertTitle>{notice.title}</AlertTitle>
      <AlertDescription className="space-y-3">
        <p>{notice.description}</p>
        <div className="flex flex-wrap items-center gap-3">
          <Button disabled type="button" variant="outline">
            <RefreshCw className={ICON_CLASS} />
            {notice.actionLabel}
          </Button>
          <span>{notice.actionUnavailable}</span>
        </div>
      </AlertDescription>
    </Alert>
  </SectionCard>;
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
  readonly onDelete: (item: VaultItem) => void;
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
        <Button type="button" variant="outline" onClick={() => onEdit(item)}><Edit className={ICON_CLASS} />Edit</Button>
        <Button type="button" variant="outline" className="text-destructive hover:bg-destructive/10" onClick={() => onDelete(item)}><Trash2 className={ICON_CLASS} />Delete</Button>
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

type DeleteSavedPasswordDialogProps = {
  readonly item: VaultItem;
  readonly onCancel: () => void;
  readonly onConfirm: (item: VaultItem) => Promise<void>;
};

/**
 * Destructive deletes require an explicit confirmation that echoes the exact
 * origin of the credential being removed; a mismatch blocks the delete.
 */
function DeleteSavedPasswordDialog(
    {item, onCancel, onConfirm}: DeleteSavedPasswordDialogProps) {
  const {origin, username} = describeDeleteTarget(item);
  const confirmInputRef = React.useRef<HTMLInputElement | null>(null);
  const [confirmation, setConfirmation] = useState('');
  const [errorMessage, setErrorMessage] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  const matches = confirmation.trim() === origin;

  const confirmDelete = async () => {
    if (busy) {
      return;
    }
    if (!matches) {
      setErrorMessage(`Type ${origin} exactly to confirm this deletion.`);
      confirmInputRef.current?.focus();
      return;
    }
    setBusy(true);
    setErrorMessage(null);
    await onConfirm(item);
    onCancel();
  };

  return (
    <Dialog open onOpenChange={open => {
      if (!open) {
        onCancel();
      }
    }}>
      <DialogContent>
        <DialogHeader>
          <DialogTitle>Delete saved password?</DialogTitle>
          <DialogDescription>
            This permanently removes the credential from the local Vault. It cannot be undone.
          </DialogDescription>
        </DialogHeader>
        <div className={CONFIRM_DETAIL_CLASS}>
          <span data-testid="delete-confirm-origin">Origin: {origin}</span>
          <span data-testid="delete-confirm-username">Username: {username}</span>
        </div>
        <div className="grid gap-1.5">
          <label className="text-xs font-medium text-foreground" htmlFor="delete-confirm-input">
            Type the origin to confirm
          </label>
          <Input
            aria-label="Confirm deletion by typing the origin"
            id="delete-confirm-input"
            placeholder={origin}
            ref={confirmInputRef}
            value={confirmation}
            onChange={event => {
              setConfirmation(event.currentTarget.value);
              setErrorMessage(null);
            }}
          />
        </div>
        <p
          aria-live="assertive"
          className={errorMessage ? CONFIRM_ERROR_CLASS : 'sr-only'}
          data-testid="delete-confirm-error"
          role="alert">
          {errorMessage ?? ''}
        </p>
        <DialogFooter>
          <Button type="button" variant="outline" onClick={onCancel}>Cancel</Button>
          <Button
            type="button"
            className="text-destructive hover:bg-destructive/10"
            disabled={busy}
            variant="outline"
            onClick={() => void confirmDelete()}>
            {busy ? 'Deleting…' : 'Delete password'}
          </Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  );
}
