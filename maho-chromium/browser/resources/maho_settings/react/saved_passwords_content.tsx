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
import {
  DropdownMenu,
  DropdownMenuContent,
  DropdownMenuItem,
  DropdownMenuTrigger,
} from '@ui/dropdown-menu';
import {Input} from '@ui/input';
import {
  AlertCircle,
  Check,
  Copy,
  CreditCard,
  Database,
  Edit,
  ExternalLink,
  Globe,
  Grid,
  Key,
  Lock,
  Plus,
  RefreshCw,
  Search,
  ShieldAlert,
  Trash2,
  User,
} from '@icons/lucide';
import {
  SecretAction,
  VaultLockState,
  VaultPreflightState,
} from '../mojo.js';
import type {VaultItem} from '../mojo.js';
import {
  ManagedSettingRow,
  SectionCard,
} from './domain_panes.js';
import {isMahoNativeProvider} from './passwords_model.js';
import {VaultAccessGate} from './passwords_vault_gate.js';
import {
  filterSavedPasswordItems,
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
const VAULT_SHELL_CLASS = 'overflow-hidden rounded-xl border border-slate-700/70 bg-slate-950/70 shadow-sm';
const VAULT_HEADER_CLASS = 'flex items-center justify-between gap-4 border-b border-slate-700/60 bg-slate-900/70 px-5 py-4';
const VAULT_BODY_CLASS = 'grid min-h-[440px] md:grid-cols-[168px_minmax(0,1fr)]';
const VAULT_FILTER_CLASS = 'border-b border-slate-700/60 bg-slate-950/45 p-3 md:border-r md:border-b-0';
const VAULT_FILTER_BUTTON_CLASS = 'h-9 w-full justify-start gap-2 rounded-md px-2.5 text-xs font-medium';
const VAULT_MAIN_CLASS = 'min-w-0 bg-slate-950/40';
const VAULT_SEARCH_WRAP_CLASS = 'border-b border-slate-700/60 bg-slate-900/45 p-4';
const VAULT_SEARCH_CLASS = 'h-10 border-slate-700 bg-slate-900/60 pl-9 text-slate-100 shadow-none placeholder:text-slate-500 focus-visible:ring-blue-500/70';
const VAULT_TABLE_HEADER_CLASS = 'grid grid-cols-[minmax(0,1fr)_88px_116px] items-center gap-3 border-b border-slate-700/60 bg-slate-900/40 px-4 py-2.5 text-xs font-semibold text-slate-400';
const VAULT_ROW_CLASS = 'grid grid-cols-[minmax(0,1fr)_88px_116px] items-center gap-3 border-b border-slate-800/80 px-4 py-3 last:border-b-0 hover:bg-slate-800/35';
const VAULT_OWNER_BADGE_CLASS = 'inline-flex w-fit items-center rounded-full border border-blue-400/25 bg-blue-500/10 px-2.5 py-1 text-[11px] font-semibold text-blue-300';
const VAULT_ICON_BUTTON_CLASS = 'size-8 rounded-md text-slate-400 hover:bg-slate-700/60 hover:text-slate-100';
const STATUS_CLASS = 'mt-1 flex items-start gap-1.5 text-[11px] leading-4';
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
    return <VaultAccessNotice
      loadSavedPasswords={props.loadSavedPasswords}
      store={props.store}
      vaultResult={props.data.vaultStatus}
    />;
  }

  return (
    <>
      <section className={VAULT_SHELL_CLASS} data-testid="vault-library">
        <header className={VAULT_HEADER_CLASS}>
          <h3 className="text-xl font-semibold tracking-tight text-slate-100">Vault</h3>
          <Button
            aria-label="Add password"
            className="h-9 gap-1.5 bg-blue-600 px-3.5 font-semibold text-white shadow-sm hover:bg-blue-500"
            type="button"
            onClick={props.onOpenAdd}>
            <Plus className={ICON_CLASS} />
            New
          </Button>
        </header>

        <div className={VAULT_BODY_CLASS}>
          <VaultCategoryRail itemCount={props.data.items.length} />
          <div className={VAULT_MAIN_CLASS}>
            <div className={VAULT_SEARCH_WRAP_CLASS}>
              <div className="relative">
                <Search className="pointer-events-none absolute left-3 top-1/2 size-4 -translate-y-1/2 text-slate-400" />
                <Input
                  aria-label="Search login"
                  className={VAULT_SEARCH_CLASS}
                  placeholder="Search login"
                  type="search"
                  value={props.searchInputValue}
                  onChange={event => props.onSearchChange(event.currentTarget.value)}
                />
              </div>
            </div>

            <div role="table" aria-label="Vault login items">
              <div className={VAULT_TABLE_HEADER_CLASS} role="row">
                <div role="columnheader">Name ↕</div>
                <div role="columnheader">Owner ↕</div>
                <div className="text-right" role="columnheader">Options</div>
              </div>
              <div role="rowgroup">
                {filteredItems.map(item => <SavedPasswordRow
                  actionStatus={props.actionStatus}
                  item={item}
                  key={item.id}
                  onDelete={setPendingDelete}
                  onEdit={props.onOpenEdit}
                  onSecretAction={props.onSecretAction}
                />)}
                {filteredItems.length === 0 ? (
                  <div className="grid min-h-40 place-items-center px-6 py-10 text-center text-sm text-slate-400">
                    {props.searchQuery ? 'No logins match this search.' : 'No logins in your Vault yet.'}
                  </div>
                ) : null}
              </div>
            </div>
          </div>
        </div>
      </section>

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

type VaultCategory = {
  readonly count?: number;
  readonly disabled?: boolean;
  readonly icon: React.ComponentType<{className?: string}>;
  readonly label: string;
};

function VaultCategoryRail({itemCount}: {readonly itemCount: number}) {
  const categories: readonly VaultCategory[] = [
    {label: 'All items', icon: Grid, count: itemCount},
    {label: 'Login', icon: Key, count: itemCount},
    {label: 'Card', icon: CreditCard, disabled: true},
    {label: 'Identity', icon: User, disabled: true},
    {label: 'Secure note', icon: Database, disabled: true},
    {label: 'Trash', icon: Trash2, disabled: true},
  ];

  return (
    <aside aria-label="Vault categories" className={VAULT_FILTER_CLASS}>
      <div className="mb-2 px-2 text-[11px] font-semibold uppercase tracking-[0.12em] text-slate-500">
        Types
      </div>
      <div className="grid gap-1">
        {categories.map((category, index) => {
          const Icon = category.icon;
          return (
            <Button
              aria-current={index === 0 ? 'page' : undefined}
              className={`${VAULT_FILTER_BUTTON_CLASS} ${index === 0 ? 'bg-blue-500/10 text-blue-300 hover:bg-blue-500/15' : 'text-slate-400 hover:bg-slate-800/60 hover:text-slate-200'} ${category.disabled ? 'opacity-50' : ''}`}
              disabled={category.disabled}
              key={category.label}
              type="button"
              variant="ghost">
              <Icon className="size-3.5 shrink-0" />
              <span className="min-w-0 flex-1 truncate text-left">{category.label}</span>
              {category.count !== undefined ? <span className="text-[10px] tabular-nums">{category.count}</span> : null}
            </Button>
          );
        })}
      </div>
    </aside>
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

function VaultAccessNotice(
    {loadSavedPasswords, store, vaultResult}: {
      readonly loadSavedPasswords: LoadSavedPasswords;
      readonly store: MahoSettingsStore;
      readonly vaultResult: SavedPasswordsPaneData['vaultStatus'];
    }) {
  const lockState = vaultResult.status?.lockState ?? VaultLockState.kUninitialized;
  const needsInitialize = lockState === VaultLockState.kUninitialized;
  const BannerIcon = needsInitialize ? Database : Lock;
  const bannerTitle = needsInitialize ? 'Set up Maho Vault' : 'Maho Vault is locked';
  const sectionDescription = needsInitialize ?
      'Initialize the local Vault before saving native passwords.' :
      'Unlock the Vault before managing saved passwords.';
  const bannerDescription = needsInitialize ?
      'Maho Vault has not been initialized on this device. Create a master passphrase and recovery secret below before storing credentials.' :
      'Enter your master passphrase below. The Vault stores encrypted password data locally on this device; your passphrase is not sent to Maho or any server.';

  return <SectionCard
    title="Vault access"
    description={sectionDescription}
    headerBanner={
      <Alert
        className="rounded-none border-0 border-b border-border/60 bg-muted/20"
        data-vault-state={needsInitialize ? 'uninitialized' : 'locked'}>
        <BannerIcon className={ICON_CLASS} />
        <AlertTitle>{bannerTitle}</AlertTitle>
        <AlertDescription>{bannerDescription}</AlertDescription>
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
  const origin = getPrimaryOrigin(item);
  const domain = getCredentialDomain(origin);
  const status = actionStatus?.itemId === item.id ? actionStatus : null;
  const faviconUrl = origin ?
      `chrome://favicon2/?size=32&scale_factor=1x&page_url=${encodeURIComponent(origin)}` : null;
  const launchUrl = () => {
    if (!origin) {
      return;
    }
    window.open(origin, '_blank', 'noopener,noreferrer');
  };

  return (
    <div className={VAULT_ROW_CLASS} data-testid={`saved-password-${item.id}`} role="row">
      <div className="flex min-w-0 items-center gap-3" role="cell">
        <div className="relative flex size-9 shrink-0 items-center justify-center overflow-hidden rounded-full border border-slate-700/70 bg-slate-800/70 text-slate-400">
          <Globe className="size-4" />
          {faviconUrl ? (
            <img
              alt=""
              className="absolute inset-1 size-7 rounded-full object-contain"
              src={faviconUrl}
              onError={event => {
                event.currentTarget.style.display = 'none';
              }}
            />
          ) : null}
        </div>
        <div className="min-w-0">
          <button
            className="block max-w-full truncate text-left text-sm font-semibold text-blue-400 hover:text-blue-300 hover:underline"
            title={origin || domain}
            type="button"
            onClick={() => onEdit(item)}>
            {domain}
          </button>
          <div className="truncate text-xs text-slate-400" title={item.usernameHint || undefined}>
            {item.usernameHint || 'No username saved'}
          </div>
          {status ? (
            <p className={`${STATUS_CLASS} ${status.status === 'success' ? 'text-success' : 'text-destructive'}`}>
              {status.status === 'success' ? <Check className="size-3.5" /> : <AlertCircle className="size-3.5" />}
              {status.message}
            </p>
          ) : null}
        </div>
      </div>

      <div role="cell">
        <span className={VAULT_OWNER_BADGE_CLASS}>Me</span>
      </div>

      <div className="flex items-center justify-end gap-1" role="cell">
        <Button
          aria-label={`Launch ${domain}`}
          className={VAULT_ICON_BUTTON_CLASS}
          disabled={!origin}
          size="icon"
          title="Launch URL"
          type="button"
          variant="ghost"
          onClick={launchUrl}>
          <ExternalLink className="size-4" />
          <span className="sr-only">Launch URL</span>
        </Button>
        <Button
          aria-label={`Copy password for ${domain}`}
          className={VAULT_ICON_BUTTON_CLASS}
          size="icon"
          title="Copy"
          type="button"
          variant="ghost"
          onClick={() => void onSecretAction(item, SecretAction.kCopy)}>
          <Copy className="size-4" />
          <span className="sr-only">Copy</span>
        </Button>
        <DropdownMenu>
          <DropdownMenuTrigger asChild>
            <Button
              aria-label={`More options for ${domain}`}
              className={VAULT_ICON_BUTTON_CLASS}
              size="icon"
              title="More Options"
              type="button"
              variant="ghost">
              <span aria-hidden="true" className="text-lg leading-none">⋮</span>
              <span className="sr-only">More Options</span>
            </Button>
          </DropdownMenuTrigger>
          <DropdownMenuContent align="end" className="w-40 bg-card p-1 shadow-md">
            <DropdownMenuItem
              className="cursor-pointer gap-2 rounded-sm px-2 py-1.5 text-sm hover:bg-surface-hover"
              onClick={() => onEdit(item)}>
              <Edit className="size-4" />
              Edit
            </DropdownMenuItem>
            <DropdownMenuItem
              className="cursor-pointer gap-2 rounded-sm px-2 py-1.5 text-sm text-destructive hover:bg-destructive/10"
              onClick={() => onDelete(item)}>
              <Trash2 className="size-4" />
              Delete
            </DropdownMenuItem>
          </DropdownMenuContent>
        </DropdownMenu>
      </div>
    </div>
  );
}

function getCredentialDomain(origin: string): string {
  if (!origin) {
    return 'Unknown site';
  }
  try {
    return new URL(origin).hostname || origin;
  } catch {
    return origin;
  }
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
