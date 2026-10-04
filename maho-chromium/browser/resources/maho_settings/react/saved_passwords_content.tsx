// Copyright 2026 Maho Browser. All rights reserved.

import React, {useCallback, useEffect, useMemo, useRef, useState} from 'react';

import {Alert, AlertDescription, AlertTitle} from '@ui/alert';
import {Badge} from '@ui/badge';
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
  DropdownMenuSeparator,
  DropdownMenuTrigger,
} from '@ui/dropdown-menu';
import {Input} from '@ui/input';
import {
  AlertCircle,
  ArrowUpDown,
  Check,
  Clock,
  Copy,
  Database,
  Edit,
  ExternalLink,
  Eye,
  FileText,
  Globe,
  Grid,
  Key,
  KeyRound,
  Lock,
  Plus,
  RefreshCw,
  RotateCcw,
  Search,
  Shield,
  ShieldAlert,
  ShieldCheck,
  Star,
  Trash2,
  Undo2,
  X,
} from 'lucide-react';
import {
  SecretAction,
  VaultItemKind,
  VaultLockState,
  VaultPreflightState,
} from '../mojo.js';
import type {
  VaultHealthReport,
  VaultItem,
  VaultTotpCodeResult,
} from '../mojo.js';
import {
  ManagedSettingRow,
  SectionCard,
} from './domain_panes.js';
import {isMahoNativeProvider} from './passwords_model.js';
import {VaultAccessGate} from './passwords_vault_gate.js';
import {
  filterByCategory,
  filterSavedPasswordItems,
  getDisplayName,
  getPrimaryOrigin,
  sortSavedPasswordItems,
} from './saved_passwords_model.js';
import type {
  AddSavedPasswordInput,
  AddSecureNoteInput,
  EditSavedPasswordInput,
  EditSecureNoteInput,
  LoadSavedPasswords,
  SavedPasswordActionStatus,
  SavedPasswordDrawerState,
  SavedPasswordsPaneData,
  SavedPasswordSortDirection,
  SavedPasswordSortField,
  VaultFilterCategory,
} from './saved_passwords_model.js';
import {
  AddSavedPasswordDrawer,
  EditSavedPasswordDrawer,
  StandalonePasswordGeneratorDialog,
} from './saved_passwords_drawer.js';
import type {MahoSettingsStore} from './store.js';

const ICON_CLASS = 'size-4';
const VAULT_SHELL_CLASS = 'overflow-hidden rounded-xl border border-border/60 glass';
const VAULT_HEADER_CLASS = 'flex flex-wrap items-center justify-between gap-3 border-b border-border bg-background/40 px-5 py-3.5';
const VAULT_BODY_CLASS = 'grid min-h-[460px] md:grid-cols-[180px_minmax(0,1fr)]';
const VAULT_FILTER_CLASS = 'border-b border-border bg-background/60 p-3 md:border-r md:border-b-0';
const VAULT_FILTER_BUTTON_CLASS = 'h-9 w-full justify-start gap-2.5 rounded-md px-2.5 text-xs font-medium';
const VAULT_MAIN_CLASS = 'min-w-0 bg-background/60';
const VAULT_CONTROLS_WRAP_CLASS = 'flex flex-wrap items-center justify-between gap-3 border-b border-border bg-background/40 p-3';
const VAULT_SEARCH_CLASS = 'h-9 w-full min-w-[200px] border-border bg-background/40 pl-9 text-xs text-foreground shadow-none placeholder:text-muted-foreground focus-visible:ring-ring';
const VAULT_TABLE_HEADER_CLASS = 'grid grid-cols-[36px_minmax(0,1fr)_120px] items-center gap-2 border-b border-border bg-background/40 px-3 py-2 text-xs font-semibold text-muted-foreground';
const VAULT_ROW_CLASS = 'grid grid-cols-[36px_minmax(0,1fr)_120px] items-center gap-2 border-b border-border px-3 py-2.5 last:border-b-0 hover:bg-surface-hover';
const VAULT_ICON_BUTTON_CLASS = 'size-8 rounded-md text-muted-foreground hover:bg-surface-hover hover:text-foreground';
const STATUS_CLASS = 'mt-1 flex items-start gap-1.5 text-[11px] leading-4';
const CONFIRM_DETAIL_CLASS = 'grid gap-1 rounded-lg border border-border/60 bg-muted/20 p-3 text-xs leading-5';
const CONFIRM_ERROR_CLASS = 'text-xs leading-5 text-destructive';
const DRAWER_CONTENT_CLASS =
    'left-auto right-0 top-0 h-full max-w-md translate-x-0 translate-y-0 rounded-none border-y-0 border-r-0 glass-strong sm:rounded-none data-[state=closed]:slide-out-to-right data-[state=open]:slide-in-from-right overflow-y-auto';

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
  readonly onAddSecureNote?: (input: AddSecureNoteInput) => Promise<string | null>;
  readonly onCloseDrawer: () => void;
  readonly onDelete: (item: VaultItem) => Promise<void>;
  readonly onEdit: (item: VaultItem, input: EditSavedPasswordInput) => Promise<string | null>;
  readonly onEditSecureNote?: (item: VaultItem, input: EditSecureNoteInput) => Promise<string | null>;
  readonly onEmptyTrash?: () => Promise<void>;
  readonly onLock?: () => Promise<void>;
  readonly onOpenAdd: (kind?: VaultItemKind) => void;
  readonly onOpenEdit: (item: VaultItem) => void;
  readonly onRestore?: (item: VaultItem) => Promise<void>;
  readonly onSearchChange: (value: string) => void;
  readonly onSecretAction: (item: VaultItem, action: SecretAction) => Promise<void>;
  readonly onToggleFavorite?: (item: VaultItem) => Promise<void>;
  readonly onTrash?: (item: VaultItem) => Promise<void>;
  readonly passwordsEnabled: boolean;
  readonly searchInputValue: string;
  readonly searchQuery: string;
  readonly store: MahoSettingsStore;
  readonly transientResetToken: number;
};

export function SavedPasswordsContent(props: SavedPasswordsContentProps) {
  const [pendingDelete, setPendingDelete] = useState<VaultItem | null>(null);
  const [showEmptyTrashConfirm, setShowEmptyTrashConfirm] = useState(false);
  const [selectedCategory, setSelectedCategory] = useState<VaultFilterCategory>('all');
  const [sortField, setSortField] = useState<SavedPasswordSortField>('name');
  const [sortDirection, setSortDirection] = useState<SavedPasswordSortDirection>('asc');
  const [detailItem, setDetailItem] = useState<VaultItem | null>(null);
  const [showGenerator, setShowGenerator] = useState(false);
  const [showHealth, setShowHealth] = useState(false);
  const [lastResetToken, setLastResetToken] = useState(props.transientResetToken);

  if (lastResetToken !== props.transientResetToken) {
    setLastResetToken(props.transientResetToken);
    setPendingDelete(null);
    setShowEmptyTrashConfirm(false);
    setDetailItem(null);
    setShowGenerator(false);
    setShowHealth(false);
  }

  const trashItems = useMemo(
      () => props.data.trashItems ?? props.data.items.filter(i => Boolean(i.trashedAt)),
      [props.data.trashItems, props.data.items]);
  // The drawer must act on the latest revision: secret use bumps the item
  // revision and reloads the list, so a stored snapshot would go stale and
  // every later mutation would fail with revision_conflict.
  const currentDetailItem = detailItem
      ? props.data.items.find(i => i.id === detailItem.id) ??
        trashItems.find(i => i.id === detailItem.id) ?? detailItem
      : null;

  const activeItems = useMemo(
      () => props.data.items.filter(i => !i.trashedAt),
      [props.data.items]);

  const categoryItems = useMemo(
      () => filterByCategory(activeItems, trashItems, selectedCategory),
      [activeItems, trashItems, selectedCategory]);

  const filteredItems = useMemo(
      () => filterSavedPasswordItems(categoryItems, props.searchQuery),
      [categoryItems, props.searchQuery]);

  const sortedItems = useMemo(
      () => sortSavedPasswordItems(filteredItems, sortField, sortDirection),
      [filteredItems, sortField, sortDirection]);

  const toggleSort = (field: SavedPasswordSortField) => {
    if (sortField === field) {
      setSortDirection(prev => prev === 'asc' ? 'desc' : 'asc');
    } else {
      setSortField(field);
      setSortDirection('asc');
    }
  };

  const lockState = props.data.vaultStatus.status?.lockState ?? VaultLockState.kUninitialized;
  const isNativeProvider = isMahoNativeProvider(props.data.providerStatus.provider);
  const fatalState = isFatalVaultPreflightState(
      props.data.vaultPreflightState) ? props.data.vaultPreflightState : null;

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

  const handleToggleFavorite = async (item: VaultItem) => {
    if (props.onToggleFavorite) {
      await props.onToggleFavorite(item);
    } else {
      const handler = props.store.getHandler();
      await handler.setVaultItemFavorite(item.id, item.revision, !item.favorite);
      await props.loadSavedPasswords(false);
    }
  };

  const handleTrash = async (item: VaultItem) => {
    if (props.onTrash) {
      await props.onTrash(item);
    } else {
      const handler = props.store.getHandler();
      await handler.trashVaultItem(item.id, item.revision);
      await props.loadSavedPasswords(false);
    }
    if (detailItem?.id === item.id) {
      setDetailItem(null);
    }
  };

  const handleRestore = async (item: VaultItem) => {
    if (props.onRestore) {
      await props.onRestore(item);
    } else {
      const handler = props.store.getHandler();
      await handler.restoreVaultItem(item.id, item.revision);
      await props.loadSavedPasswords(false);
    }
    if (detailItem?.id === item.id) {
      setDetailItem(null);
    }
  };

  const handleEmptyTrash = async () => {
    if (props.onEmptyTrash) {
      await props.onEmptyTrash();
    } else {
      const handler = props.store.getHandler();
      await handler.emptyVaultTrash();
      await props.loadSavedPasswords(false);
    }
    setShowEmptyTrashConfirm(false);
  };

  const handleLockNow = async () => {
    if (props.onLock) {
      await props.onLock();
    } else {
      const handler = props.store.getHandler();
      await handler.lockVault();
      await props.loadSavedPasswords(true);
    }
  };

  return (
    <>
      <section className={VAULT_SHELL_CLASS} data-testid="vault-library">
        <header className={VAULT_HEADER_CLASS}>
          <div className="flex items-center gap-3">
            <h3 className="text-base font-semibold tracking-tight text-foreground">Vault</h3>
            <Badge className="border-border bg-surface-selected text-foreground" variant="outline">
              {activeItems.length} items
            </Badge>
          </div>

          <div className="flex flex-wrap items-center gap-2">
            <Button
              aria-label="Lock Vault now"
              className="h-8 gap-1.5 border-border bg-background/70 px-2.5 text-xs text-foreground hover:bg-surface-hover"
              size="sm"
              type="button"
              variant="outline"
              onClick={() => void handleLockNow()}>
              <Lock className="size-3.5" />
              Lock now
            </Button>

            <Button
              aria-label="Password health"
              className={`h-8 gap-1.5 px-2.5 text-xs ${showHealth ? 'bg-primary text-primary-foreground' : 'border-border bg-background/70 text-foreground hover:bg-surface-hover'}`}
              size="sm"
              type="button"
              variant={showHealth ? 'default' : 'outline'}
              onClick={() => setShowHealth(prev => !prev)}>
              <ShieldCheck className="size-3.5" />
              Health
            </Button>

            <Button
              aria-label="Password generator"
              className="h-8 gap-1.5 border-border bg-background/70 px-2.5 text-xs text-foreground hover:bg-surface-hover"
              size="sm"
              type="button"
              variant="outline"
              onClick={() => setShowGenerator(true)}>
              <KeyRound className="size-3.5" />
              Generator
            </Button>

            <Button
              aria-label="Add password"
              className="h-8 gap-1.5 bg-primary px-3 text-xs font-semibold text-primary-foreground shadow-sm hover:bg-primary/90"
              size="sm"
              type="button"
              onClick={() => props.onOpenAdd()}>
              <Plus className={ICON_CLASS} />
              New
            </Button>
          </div>
        </header>

        <div className={VAULT_BODY_CLASS}>
          <VaultCategoryRail
            activeCategory={selectedCategory}
            authenticatorCount={activeItems.filter(i => i.hasTotp).length}
            favoritesCount={activeItems.filter(i => i.favorite).length}
            loginsCount={activeItems.filter(i => i.itemKind === VaultItemKind.kLogin).length}
            secureNotesCount={activeItems.filter(i => i.itemKind === VaultItemKind.kSecureItem).length}
            totalCount={activeItems.length}
            trashCount={trashItems.length}
            onSelectCategory={setSelectedCategory}
          />

          <div className={VAULT_MAIN_CLASS}>
            {showHealth ? (
              <PasswordHealthView
                items={activeItems}
                store={props.store}
                onClose={() => setShowHealth(false)}
                onSelectItem={item => {
                  setShowHealth(false);
                  setDetailItem(item);
                }}
              />
            ) : (
              <>
                <div className={VAULT_CONTROLS_WRAP_CLASS}>
                  <div className="relative flex-1">
                    <Search className="pointer-events-none absolute left-3 top-1/2 size-3.5 -translate-y-1/2 text-muted-foreground" />
                    <Input
                      aria-label="Search login"
                      className={VAULT_SEARCH_CLASS}
                      placeholder="Search Vault items…"
                      type="search"
                      value={props.searchInputValue}
                      onChange={event => props.onSearchChange(event.currentTarget.value)}
                    />
                  </div>

                  <div className="flex items-center gap-2">
                    {selectedCategory === 'trash' ? (
                      <Button
                        aria-label="Empty trash"
                        className="h-8 gap-1.5 text-xs text-destructive hover:bg-destructive/10"
                        disabled={trashItems.length === 0}
                        size="sm"
                        type="button"
                        variant="outline"
                        onClick={() => setShowEmptyTrashConfirm(true)}>
                        <Trash2 className="size-3.5" />
                        Empty trash
                      </Button>
                    ) : null}

                    <DropdownMenu>
                      <DropdownMenuTrigger asChild>
                        <Button
                          aria-label="Sort items"
                          className="h-8 gap-1.5 border-border bg-background/40 px-2.5 text-xs text-foreground hover:bg-surface-hover"
                          size="sm"
                          type="button"
                          variant="outline">
                          <ArrowUpDown className="size-3.5 text-muted-foreground" />
                          <span>Sort</span>
                        </Button>
                      </DropdownMenuTrigger>
                      <DropdownMenuContent align="end" className="w-48 border-border bg-background/40 p-1 text-foreground shadow-lg">
                        <DropdownMenuItem
                          className="cursor-pointer justify-between rounded px-2 py-1.5 text-xs hover:bg-surface-hover"
                          onClick={() => toggleSort('name')}>
                          <span>Name ({sortDirection === 'asc' ? 'A–Z' : 'Z–A'})</span>
                          {sortField === 'name' ? <Check className="size-3 text-primary" /> : null}
                        </DropdownMenuItem>
                        <DropdownMenuItem
                          className="cursor-pointer justify-between rounded px-2 py-1.5 text-xs hover:bg-surface-hover"
                          onClick={() => toggleSort('recently-used')}>
                          <span>Recently used</span>
                          {sortField === 'recently-used' ? <Check className="size-3 text-primary" /> : null}
                        </DropdownMenuItem>
                        <DropdownMenuItem
                          className="cursor-pointer justify-between rounded px-2 py-1.5 text-xs hover:bg-surface-hover"
                          onClick={() => toggleSort('date-created')}>
                          <span>Date added</span>
                          {sortField === 'date-created' ? <Check className="size-3 text-primary" /> : null}
                        </DropdownMenuItem>
                        <DropdownMenuItem
                          className="cursor-pointer justify-between rounded px-2 py-1.5 text-xs hover:bg-surface-hover"
                          onClick={() => toggleSort('date-updated')}>
                          <span>Date modified</span>
                          {sortField === 'date-updated' ? <Check className="size-3 text-primary" /> : null}
                        </DropdownMenuItem>
                      </DropdownMenuContent>
                    </DropdownMenu>
                  </div>
                </div>

                <div role="table" aria-label="Vault login items">
                  <div className={VAULT_TABLE_HEADER_CLASS} role="row">
                    <div role="columnheader">
                      <span className="sr-only">Favorite</span>
                    </div>
                    <div
                      aria-sort={sortField === 'name' ? (sortDirection === 'asc' ? 'ascending' : 'descending') : 'none'}
                      className="cursor-pointer select-none text-foreground hover:text-foreground"
                      role="columnheader"
                      onClick={() => toggleSort('name')}>
                      Name {sortField === 'name' ? (sortDirection === 'asc' ? '↑' : '↓') : '↕'}
                    </div>
                    <div className="text-right" role="columnheader">Options</div>
                  </div>

                  <div role="rowgroup">
                    {sortedItems.map(item => (
                      <SavedPasswordRow
                        actionStatus={props.actionStatus}
                        isTrash={selectedCategory === 'trash'}
                        item={item}
                        key={item.id}
                        onDelete={setPendingDelete}
                        onEdit={props.onOpenEdit}
                        onRestore={handleRestore}
                        onSecretAction={props.onSecretAction}
                        onSelectDetail={setDetailItem}
                        onToggleFavorite={handleToggleFavorite}
                        onTrash={handleTrash}
                      />
                    ))}

                    {sortedItems.length === 0 ? (
                      <VaultEmptyState
                        category={selectedCategory}
                        hasQuery={Boolean(props.searchQuery)}
                        totalItemsCount={activeItems.length}
                        onAddLogin={() => props.onOpenAdd(VaultItemKind.kLogin)}
                        onAddNote={() => props.onOpenAdd(VaultItemKind.kSecureItem)}
                        onClearSearch={() => props.onSearchChange('')}
                        onImport={() => props.store.selectPane('passwords')}
                      />
                    ) : null}
                  </div>
                </div>
              </>
            )}
          </div>
        </div>
      </section>

      {props.drawer?.kind === 'add' ? (
        <AddSavedPasswordDrawer
          initialKind={props.drawer.initialKind}
          onAdd={props.onAdd}
          onAddSecureNote={props.onAddSecureNote}
          onClose={props.onCloseDrawer}
          store={props.store}
        />
      ) : null}

      {props.drawer?.kind === 'edit' ? (
        <EditSavedPasswordDrawer
          item={props.drawer.item}
          onClose={props.onCloseDrawer}
          onSave={props.onEdit}
          onSaveSecureNote={props.onEditSecureNote}
          store={props.store}
        />
      ) : null}

      {currentDetailItem ? (
        <ItemDetailDrawer
          isTrash={selectedCategory === 'trash'}
          item={currentDetailItem}
          store={props.store}
          onClose={() => setDetailItem(null)}
          onDeletePermanently={item => {
            setDetailItem(null);
            setPendingDelete(item);
          }}
          onEdit={item => {
            setDetailItem(null);
            props.onOpenEdit(item);
          }}
          onRestore={handleRestore}
          onSecretAction={props.onSecretAction}
          onToggleFavorite={handleToggleFavorite}
          onTrash={handleTrash}
        />
      ) : null}

      {showGenerator ? (
        <StandalonePasswordGeneratorDialog
          store={props.store}
          onClose={() => setShowGenerator(false)}
        />
      ) : null}

      {pendingDelete ? (
        <DeleteSavedPasswordDialog
          item={pendingDelete}
          onCancel={() => setPendingDelete(null)}
          onConfirm={props.onDelete}
        />
      ) : null}

      {showEmptyTrashConfirm ? (
        <EmptyTrashDialog
          onCancel={() => setShowEmptyTrashConfirm(false)}
          onConfirm={handleEmptyTrash}
        />
      ) : null}
    </>
  );
}

function VaultEmptyState({
  category,
  hasQuery,
  totalItemsCount,
  onAddLogin,
  onAddNote,
  onClearSearch,
  onImport,
}: {
  readonly category: VaultFilterCategory;
  readonly hasQuery: boolean;
  readonly totalItemsCount: number;
  readonly onAddLogin: () => void;
  readonly onAddNote: () => void;
  readonly onClearSearch: () => void;
  readonly onImport: () => void;
}) {
  if (hasQuery) {
    return (
      <div className="grid min-h-40 place-items-center px-6 py-10 text-center text-xs text-muted-foreground">
        <p>No items match this search.</p>
        <Button className="mt-2 text-xs text-primary" size="sm" type="button" variant="ghost" onClick={onClearSearch}>
          Clear search
        </Button>
      </div>
    );
  }

  if (category === 'trash') {
    return (
      <div className="grid min-h-40 place-items-center px-6 py-10 text-center text-xs text-muted-foreground">
        <Trash2 className="size-8 text-muted-foreground/60 mb-2" />
        <p>Trash is empty.</p>
      </div>
    );
  }

  if (totalItemsCount > 0) {
    return (
      <div className="grid min-h-40 place-items-center px-6 py-10 text-center text-xs text-muted-foreground">
        <p>No items in this category.</p>
      </div>
    );
  }

  return (
    <div className="grid place-items-center px-6 py-12 text-center" data-testid="vault-empty-state">
      <div className="flex size-12 items-center justify-center rounded-full border border-border bg-background/40 text-primary mb-3 shadow-inner">
        <Shield className="size-6" />
      </div>
      <h4 className="text-sm font-semibold text-foreground">Your Maho Vault is empty</h4>
      <p className="mt-1 max-w-sm text-xs leading-5 text-muted-foreground">
        No logins in your Vault yet. Store logins, passwords, and secure notes locally on your device with hardware-backed encryption.
      </p>
      <div className="mt-5 flex flex-wrap items-center justify-center gap-2.5">
        <Button
          className="h-8 gap-1.5 bg-primary px-3 text-xs font-semibold text-primary-foreground shadow-sm hover:bg-primary/90"
          type="button"
          onClick={onAddLogin}>
          <Plus className="size-3.5" />
          Add login
        </Button>
        <Button
          className="h-8 gap-1.5 border-border bg-background/40 px-3 text-xs text-foreground hover:bg-surface-hover"
          type="button"
          variant="outline"
          onClick={onAddNote}>
          <FileText className="size-3.5 text-success" />
          Add secure note
        </Button>
        <Button
          className="h-8 gap-1.5 border-border bg-background/40 px-3 text-xs text-foreground hover:bg-surface-hover"
          type="button"
          variant="outline"
          onClick={onImport}>
          Import passwords
        </Button>
      </div>
    </div>
  );
}

type VaultCategoryRailProps = {
  readonly activeCategory: VaultFilterCategory;
  readonly authenticatorCount: number;
  readonly favoritesCount: number;
  readonly loginsCount: number;
  readonly secureNotesCount: number;
  readonly totalCount: number;
  readonly trashCount: number;
  readonly onSelectCategory: (category: VaultFilterCategory) => void;
};

function VaultCategoryRail(props: VaultCategoryRailProps) {
  const categories: readonly {
    readonly id: VaultFilterCategory;
    readonly label: string;
    readonly icon: React.ComponentType<{className?: string}>;
    readonly count: number;
  }[] = [
    {id: 'all', label: 'All items', icon: Grid, count: props.totalCount},
    {id: 'favorites', label: 'Favorites', icon: Star, count: props.favoritesCount},
    {id: 'logins', label: 'Logins', icon: Key, count: props.loginsCount},
    {id: 'authenticator', label: 'Authenticator', icon: ShieldCheck, count: props.authenticatorCount},
    {id: 'secure-notes', label: 'Secure notes', icon: FileText, count: props.secureNotesCount},
    {id: 'trash', label: 'Trash', icon: Trash2, count: props.trashCount},
  ];

  return (
    <aside aria-label="Vault categories" className={VAULT_FILTER_CLASS}>
      <div className="mb-2 px-2 text-[11px] font-semibold uppercase tracking-[0.12em] text-muted-foreground">
        Categories
      </div>
      <div className="grid gap-1">
        {categories.map(category => {
          const Icon = category.icon;
          const isActive = props.activeCategory === category.id;
          return (
            <Button
              aria-current={isActive ? 'page' : undefined}
              data-category={category.id}
              className={`${VAULT_FILTER_BUTTON_CLASS} ${isActive ? 'bg-surface-selected text-foreground' : 'text-muted-foreground hover:bg-surface-hover hover:text-foreground'}`}
              key={category.id}
              type="button"
              variant="ghost"
              onClick={() => props.onSelectCategory(category.id)}>
              <Icon className="size-3.5 shrink-0" />
              <span className="min-w-0 flex-1 truncate text-left">{category.label}</span>
              <span className="text-[10px] tabular-nums text-muted-foreground">{category.count}</span>
            </Button>
          );
        })}
      </div>
    </aside>
  );
}

type SavedPasswordRowProps = {
  readonly actionStatus: SavedPasswordActionStatus | null;
  readonly isTrash?: boolean;
  readonly item: VaultItem;
  readonly onDelete: (item: VaultItem) => void;
  readonly onEdit: (item: VaultItem) => void;
  readonly onRestore?: (item: VaultItem) => Promise<void>;
  readonly onSecretAction: (item: VaultItem, action: SecretAction) => Promise<void>;
  readonly onSelectDetail: (item: VaultItem) => void;
  readonly onToggleFavorite?: (item: VaultItem) => Promise<void>;
  readonly onTrash?: (item: VaultItem) => Promise<void>;
};

function SavedPasswordRow({
  actionStatus,
  isTrash,
  item,
  onDelete,
  onEdit,
  onRestore,
  onSecretAction,
  onSelectDetail,
  onToggleFavorite,
  onTrash,
}: SavedPasswordRowProps) {
  const origin = getPrimaryOrigin(item);
  const domain = getCredentialDomain(origin);
  const status = actionStatus?.itemId === item.id ? actionStatus : null;
  const isSecureNote = item.itemKind === VaultItemKind.kSecureItem;
  const faviconUrl = origin ?
      `chrome://favicon2/?size=32&scale_factor=1x&page_url=${encodeURIComponent(origin)}` : null;

  const launchUrl = () => {
    if (!origin) return;
    window.open(origin, '_blank', 'noopener,noreferrer');
  };

  const displayName = item.title || domain || item.usernameHint || (isSecureNote ? 'Secure Note' : 'Login');

  return (
    <div className={VAULT_ROW_CLASS} data-testid={`saved-password-${item.id}`} role="row">
      <div className="flex items-center justify-center" role="cell">
        <Button
          aria-label={item.favorite ? `Remove ${displayName} from favorites` : `Add ${displayName} to favorites`}
          className="size-7 p-0 text-muted-foreground hover:text-warning"
          size="icon"
          type="button"
          variant="ghost"
          onClick={() => void onToggleFavorite?.(item)}>
          <Star className={`size-3.5 ${item.favorite ? 'fill-warning text-warning' : ''}`} />
        </Button>
      </div>

      <div className="flex min-w-0 items-center gap-2.5" role="cell">
        <div className="relative flex size-8 shrink-0 items-center justify-center overflow-hidden rounded-full border border-border bg-surface-selected text-muted-foreground">
          {isSecureNote ? (
            <FileText className="size-3.5 text-success" />
          ) : (
            <>
              <Globe className="size-3.5" />
              {faviconUrl ? (
                <img
                  alt=""
                  className="absolute inset-1 size-6 rounded-full object-contain"
                  src={faviconUrl}
                  onError={event => {
                    event.currentTarget.style.display = 'none';
                  }}
                />
              ) : null}
            </>
          )}
        </div>

        <div className="min-w-0 flex-1">
          <div className="flex flex-wrap items-center gap-1.5">
            <button
              className="truncate text-left text-sm font-medium text-foreground hover:underline"
              title={displayName}
              type="button"
              onClick={() => onSelectDetail(item)}>
              {displayName}
            </button>
            {item.hasPasskey ? (
              <span className="rounded border border-border bg-secondary px-1.5 py-0.2 text-[10px] font-medium text-secondary-foreground">
                Passkey
              </span>
            ) : null}
            {item.hasTotp ? (
              <span className="rounded border border-border bg-secondary px-1.5 py-0.2 text-[10px] font-medium text-primary">
                TOTP
              </span>
            ) : null}
          </div>

          <div className="truncate text-[11px] text-muted-foreground" title={item.usernameHint || undefined}>
            {isSecureNote ? 'Secure Note' : (item.usernameHint || 'No username saved')}
          </div>

          {status ? (
            <p className={`${STATUS_CLASS} ${status.status === 'success' ? 'text-success' : 'text-destructive'}`}>
              {status.status === 'success' ? <Check className="size-3.5" /> : <AlertCircle className="size-3.5" />}
              {status.message}
            </p>
          ) : null}
        </div>
      </div>

      <div className="flex items-center justify-end gap-1" role="cell">
        {isTrash ? (
          <>
            <Button
              aria-label={`Restore ${displayName}`}
              className={VAULT_ICON_BUTTON_CLASS}
              size="icon"
              title="Restore"
              type="button"
              variant="ghost"
              onClick={() => void onRestore?.(item)}>
              <RotateCcw className="size-3.5" />
            </Button>
            <Button
              aria-label={`Delete ${displayName} permanently`}
              className={VAULT_ICON_BUTTON_CLASS}
              size="icon"
              title="Delete permanently"
              type="button"
              variant="ghost"
              onClick={() => onDelete(item)}>
              <Trash2 className="size-3.5 text-destructive" />
            </Button>
          </>
        ) : (
          <>
            {!isSecureNote && origin ? (
              <Button
                aria-label={`Launch ${domain}`}
                className={VAULT_ICON_BUTTON_CLASS}
                size="icon"
                title="Launch URL"
                type="button"
                variant="ghost"
                onClick={launchUrl}>
                <ExternalLink className="size-3.5" />
              </Button>
            ) : null}

            {!isSecureNote ? (
              <Button
                aria-label={`Copy password for ${domain}`}
                className={VAULT_ICON_BUTTON_CLASS}
                size="icon"
                title="Copy"
                type="button"
                variant="ghost"
                onClick={() => void onSecretAction(item, SecretAction.kCopy)}>
                <Copy className="size-3.5" />
                <span className="sr-only">Copy</span>
              </Button>
            ) : null}

            <DropdownMenu>
              <DropdownMenuTrigger asChild>
                <Button
                  aria-label={`More options for ${domain}`}
                  className={VAULT_ICON_BUTTON_CLASS}
                  size="icon"
                  title="More Options"
                  type="button"
                  variant="ghost">
                  <span aria-hidden="true" className="text-base leading-none">⋮</span>
                  <span className="sr-only">More Options</span>
                </Button>
              </DropdownMenuTrigger>
              <DropdownMenuContent align="end" className="w-40 border-border bg-background/40 p-1 text-foreground shadow-md">
                <DropdownMenuItem
                  className="cursor-pointer gap-2 rounded px-2 py-1.5 text-xs hover:bg-surface-hover"
                  onClick={() => onSelectDetail(item)}>
                  <Eye className="size-3.5 text-muted-foreground" />
                  View details
                </DropdownMenuItem>
                <DropdownMenuItem
                  className="cursor-pointer gap-2 rounded px-2 py-1.5 text-xs hover:bg-surface-hover"
                  onClick={() => onEdit(item)}>
                  <Edit className="size-3.5 text-muted-foreground" />
                  Edit
                </DropdownMenuItem>
                <DropdownMenuItem
                  className="cursor-pointer gap-2 rounded px-2 py-1.5 text-xs text-warning hover:bg-warning/10"
                  data-action="move-to-trash"
                  onClick={() => void onTrash?.(item)}>
                  <Trash2 className="size-3.5" />
                  Move to trash
                </DropdownMenuItem>
                <DropdownMenuSeparator className="bg-surface-selected" />
                <DropdownMenuItem
                  className="cursor-pointer gap-2 rounded px-2 py-1.5 text-xs text-destructive hover:bg-destructive/10"
                  onClick={() => onDelete(item)}>
                  <Trash2 className="size-3.5" />
                  Delete
                </DropdownMenuItem>
              </DropdownMenuContent>
            </DropdownMenu>
          </>
        )}
      </div>
    </div>
  );
}

export type ItemDetailDrawerProps = {
  readonly isTrash?: boolean;
  readonly item: VaultItem;
  readonly onClose: () => void;
  readonly onDeletePermanently?: (item: VaultItem) => void;
  readonly onEdit: (item: VaultItem) => void;
  readonly onRestore?: (item: VaultItem) => Promise<void>;
  readonly onSecretAction: (item: VaultItem, action: SecretAction) => Promise<void>;
  readonly onToggleFavorite?: (item: VaultItem) => Promise<void>;
  readonly onTrash?: (item: VaultItem) => Promise<void>;
  readonly store: MahoSettingsStore;
};

export function ItemDetailDrawer({
  isTrash,
  item,
  onClose,
  onDeletePermanently,
  onEdit,
  onRestore,
  onSecretAction,
  onToggleFavorite,
  onTrash,
  store,
}: ItemDetailDrawerProps) {
  const [notes, setNotes] = useState<string | null>(null);
  // Stored username; item.usernameHint is masked and only used until it loads.
  const [username, setUsername] = useState<string | null>(null);
  const [loadingNotes, setLoadingNotes] = useState(false);
  const [totpCode, setTotpCode] = useState<string | null>(null);
  const [secondsRemaining, setSecondsRemaining] = useState(0);
  const [period, setPeriod] = useState(30);
  const [copiedField, setCopiedField] = useState<string | null>(null);
  const mountedRef = useRef(true);

  useEffect(() => {
    return () => {
      mountedRef.current = false;
      setNotes(null);
      setTotpCode(null);
    };
  }, []);

  useEffect(() => {
    setNotes(null);
    setUsername(null);
    setTotpCode(null);
    setLoadingNotes(true);

    const handler = store.getHandler();
    if (typeof handler.getVaultItemNotes === 'function') {
      void handler.getVaultItemNotes(item.id).then(res => {
        if (!mountedRef.current) return;
        setLoadingNotes(false);
        if (res.success && res.notes) {
          setNotes(res.notes);
        }
        if (res.success) {
          setUsername(res.username ?? '');
        }
      }).catch(() => {
        if (mountedRef.current) setLoadingNotes(false);
      });
    } else {
      setLoadingNotes(false);
    }
  }, [item.id, store]);

  useEffect(() => {
    if (!item.hasTotp) {
      setTotpCode(null);
      return;
    }

    const handler = store.getHandler();
    let timerId: number | null = null;

    const fetchTotp = async () => {
      if (typeof handler.getVaultTotpCode !== 'function') return;
      try {
        const {result} = await handler.getVaultTotpCode(item.id);
        if (!mountedRef.current) return;
        if (result.success && result.code) {
          setTotpCode(result.code);
          setSecondsRemaining(result.secondsRemaining);
          setPeriod(result.period || 30);
        }
      } catch {
      }
    };

    void fetchTotp();
    timerId = window.setInterval(fetchTotp, 1000);

    return () => {
      if (timerId !== null) window.clearInterval(timerId);
    };
  }, [item.id, item.hasTotp, store]);

  const copyToClipboard = async (text: string, field: string) => {
    try {
      await navigator.clipboard.writeText(text);
      setCopiedField(field);
      window.setTimeout(() => {
        if (mountedRef.current) setCopiedField(null);
      }, 2000);
    } catch {
    }
  };

  const isSecureNote = item.itemKind === VaultItemKind.kSecureItem;
  const primaryOrigin = getPrimaryOrigin(item);
  const displayName = item.title || getCredentialDomain(primaryOrigin) || item.usernameHint || (isSecureNote ? 'Secure Note' : 'Login');

  return (
    <Dialog open onOpenChange={open => { if (!open) onClose(); }}>
      <DialogContent className={DRAWER_CONTENT_CLASS}>
        <DialogHeader className="border-b border-border pb-3">
          <div className="flex items-start justify-between gap-3">
            <div className="min-w-0 flex-1">
              <DialogTitle className="truncate text-base font-semibold text-foreground">{displayName}</DialogTitle>
              <div className="mt-1 flex flex-wrap items-center gap-1.5">
                <Badge className="border-border bg-surface-selected text-[10px] text-foreground" variant="outline">
                  {isSecureNote ? 'Secure Note' : 'Login'}
                </Badge>
                {item.hasPasskey ? (
                  <Badge className="border-border bg-secondary text-[10px] text-secondary-foreground" variant="outline">
                    Passkey
                  </Badge>
                ) : null}
              </div>
            </div>
            <Button
              aria-label={item.favorite ? 'Remove from favorites' : 'Add to favorites'}
              className="size-8 p-0 text-muted-foreground hover:text-warning"
              size="icon"
              type="button"
              variant="ghost"
              onClick={() => void onToggleFavorite?.(item)}>
              <Star className={`size-4 ${item.favorite ? 'fill-warning text-warning' : ''}`} />
            </Button>
          </div>
        </DialogHeader>

        <div className="grid gap-4 py-3 text-xs">
          {!isSecureNote ? (
            <>
              <div className="grid gap-1 rounded-lg border border-border bg-background/40 p-3">
                <span className="text-[11px] font-medium text-muted-foreground">Username</span>
                <div className="flex items-center justify-between gap-2">
                  <span className="truncate font-mono text-xs text-foreground" data-testid="detail-username">
                    {(username ?? item.usernameHint) || 'No username'}
                  </span>
                  {username ? (
                    <Button
                      aria-label="Copy username"
                      className="h-7 gap-1 px-2 text-xs text-foreground hover:bg-surface-hover"
                      size="sm"
                      type="button"
                      variant="ghost"
                      onClick={() => void copyToClipboard(username, 'username')}>
                      {copiedField === 'username' ? <Check className="size-3 text-success" /> : <Copy className="size-3" />}
                      <span>Copy</span>
                    </Button>
                  ) : null}
                </div>
              </div>

              <div className="grid gap-1 rounded-lg border border-border bg-background/40 p-3">
                <span className="text-[11px] font-medium text-muted-foreground">Password</span>
                <div className="flex items-center justify-between gap-2">
                  <span className="font-mono text-sm tracking-widest text-muted-foreground">••••••••</span>
                  <div className="flex items-center gap-1">
                    <Button
                      aria-label="Reveal password"
                      className="h-7 gap-1 px-2 text-xs text-primary hover:bg-surface-hover"
                      size="sm"
                      type="button"
                      variant="ghost"
                      onClick={() => void onSecretAction(item, SecretAction.kReveal)}>
                      <Eye className="size-3" />
                      <span>Reveal</span>
                    </Button>
                    <Button
                      aria-label="Copy password"
                      className="h-7 gap-1 px-2 text-xs text-foreground hover:bg-surface-hover"
                      size="sm"
                      type="button"
                      variant="ghost"
                      onClick={() => void onSecretAction(item, SecretAction.kCopy)}>
                      <Copy className="size-3" />
                      <span>Copy</span>
                    </Button>
                  </div>
                </div>
              </div>

              {item.hasTotp ? (
                <div className="grid gap-1 rounded-lg border border-border/60 bg-background/40 p-3" data-testid="detail-totp-card">
                  <div className="flex items-center justify-between">
                    <span className="text-[11px] font-medium text-primary">One-Time Code (TOTP)</span>
                    <span className="text-[10px] tabular-nums text-muted-foreground">{secondsRemaining}s</span>
                  </div>
                  <div className="flex items-center justify-between gap-2 pt-1">
                    <span className="font-mono text-base font-semibold tracking-widest text-primary" data-testid="detail-totp-code">
                      {totpCode || '••••••'}
                    </span>
                    <Button
                      aria-label="Copy one-time code"
                      className="h-7 gap-1 px-2 text-xs text-primary hover:bg-surface-hover"
                      disabled={!totpCode}
                      size="sm"
                      type="button"
                      variant="ghost"
                      onClick={() => totpCode && void copyToClipboard(totpCode, 'totp')}>
                      {copiedField === 'totp' ? <Check className="size-3 text-success" /> : <Copy className="size-3" />}
                      <span>Copy</span>
                    </Button>
                  </div>
                  <div className="h-1 w-full overflow-hidden rounded-full bg-surface-selected mt-1">
                    <div
                      className="h-full bg-primary transition-all duration-1000 ease-linear"
                      style={{width: `${Math.max(0, Math.min(100, (secondsRemaining / (period || 30)) * 100))}%`}}
                    />
                  </div>
                </div>
              ) : null}

              {item.origins.length > 0 ? (
                <div className="grid gap-1.5 rounded-lg border border-border bg-background/40 p-3">
                  <span className="text-[11px] font-medium text-muted-foreground">Websites</span>
                  <div className="grid gap-1">
                    {item.origins.map((origin, index) => (
                      <div key={index} className="flex items-center justify-between gap-2 py-0.5">
                        <span className="truncate text-xs text-foreground" title={origin}>{origin}</span>
                        <div className="flex items-center gap-1">
                          <Button
                            aria-label={`Copy website ${index}`}
                            className="size-6 p-0 text-muted-foreground hover:text-foreground"
                            size="icon"
                            type="button"
                            variant="ghost"
                            onClick={() => void copyToClipboard(origin, `url-${index}`)}>
                            {copiedField === `url-${index}` ? <Check className="size-3 text-success" /> : <Copy className="size-3" />}
                          </Button>
                          <Button
                            aria-label={`Launch website ${index}`}
                            className="size-6 p-0 text-muted-foreground hover:text-foreground"
                            size="icon"
                            type="button"
                            variant="ghost"
                            onClick={() => window.open(origin, '_blank', 'noopener,noreferrer')}>
                            <ExternalLink className="size-3" />
                          </Button>
                        </div>
                      </div>
                    ))}
                  </div>
                </div>
              ) : null}
            </>
          ) : null}

          <div className="grid gap-1.5 rounded-lg border border-border bg-background/40 p-3">
            <span className="text-[11px] font-medium text-muted-foreground">Notes</span>
            {loadingNotes ? (
              <span className="text-xs text-muted-foreground">Loading notes…</span>
            ) : notes ? (
              <div className="whitespace-pre-wrap rounded border border-border bg-background/60 p-2.5 font-sans text-xs text-foreground" data-testid="detail-notes">
                {notes}
              </div>
            ) : (
              <span className="text-xs text-muted-foreground">No notes</span>
            )}
          </div>

          <div className="grid gap-1 px-1 text-[11px] text-muted-foreground">
            <div>Added: {new Date(item.createdAt).toLocaleDateString()}</div>
            <div>Updated: {new Date(item.updatedAt).toLocaleDateString()}</div>
            {item.lastUsedAt ? <div>Last used: {new Date(item.lastUsedAt).toLocaleDateString()}</div> : null}
          </div>
        </div>

        <DialogFooter className="border-t border-border pt-3">
          {isTrash ? (
            <div className="flex w-full items-center justify-between gap-2">
              <Button
                className="gap-1.5 text-xs text-foreground"
                size="sm"
                type="button"
                variant="outline"
                onClick={() => void onRestore?.(item)}>
                <RotateCcw className="size-3.5" />
                Restore
              </Button>
              <Button
                className="gap-1.5 text-xs text-destructive hover:bg-destructive/10"
                size="sm"
                type="button"
                variant="outline"
                onClick={() => onDeletePermanently?.(item)}>
                <Trash2 className="size-3.5" />
                Delete forever
              </Button>
            </div>
          ) : (
            <div className="flex w-full items-center justify-between gap-2">
              <Button
                className="gap-1.5 text-xs text-warning hover:bg-warning/10"
                size="sm"
                type="button"
                variant="outline"
                onClick={() => void onTrash?.(item)}>
                <Trash2 className="size-3.5" />
                Move to trash
              </Button>
              <div className="flex items-center gap-2">
                <Button size="sm" type="button" variant="outline" onClick={onClose}>
                  Close
                </Button>
                <Button
                  className="gap-1.5 bg-primary text-xs font-semibold text-primary-foreground hover:bg-primary/90"
                  size="sm"
                  type="button"
                  onClick={() => onEdit(item)}>
                  <Edit className="size-3.5" />
                  Edit
                </Button>
              </div>
            </div>
          )}
        </DialogFooter>
      </DialogContent>
    </Dialog>
  );
}

export type PasswordHealthViewProps = {
  readonly items: readonly VaultItem[];
  readonly onClose: () => void;
  readonly onSelectItem: (item: VaultItem) => void;
  readonly store: MahoSettingsStore;
};

export function PasswordHealthView({items, onClose, onSelectItem, store}: PasswordHealthViewProps) {
  const [report, setReport] = useState<VaultHealthReport | null>(null);
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    let mounted = true;
    const handler = store.getHandler();
    if (typeof handler.getVaultHealthReport === 'function') {
      void handler.getVaultHealthReport().then(res => {
        if (!mounted) return;
        setReport(res.report);
        setLoading(false);
      }).catch(() => {
        if (mounted) setLoading(false);
      });
    } else {
      setLoading(false);
    }
    return () => {
      mounted = false;
    };
  }, [store]);

  const itemsById = useMemo(() => {
    const map = new Map<string, VaultItem>();
    for (const item of items) {
      map.set(item.id, item);
    }
    return map;
  }, [items]);

  const weakItems = useMemo(() => {
    if (!report) return [];
    const weakIds = report.weakItemIds ?? report.weak ?? [];
    return weakIds.map(id => itemsById.get(id)).filter(Boolean) as VaultItem[];
  }, [report, itemsById]);

  const reusedItemGroups = useMemo(() => {
    if (!report) return [];
    const reusedRaw = report.reusedGroups ?? report.reused ?? [];
    return reusedRaw.map(group =>
      group.map(id => itemsById.get(id)).filter(Boolean) as VaultItem[]
    ).filter(group => group.length > 0);
  }, [report, itemsById]);

  return (
    <div className="p-4 text-xs" data-testid="password-health-view">
      <div className="mb-4 flex items-center justify-between border-b border-border pb-3">
        <div className="flex items-center gap-2">
          <ShieldCheck className="size-5 text-primary" />
          <h4 className="text-sm font-semibold text-foreground">Password Health Report</h4>
        </div>
        <Button size="sm" type="button" variant="outline" onClick={onClose}>
          Back to Vault
        </Button>
      </div>

      {loading ? (
        <div className="grid min-h-40 place-items-center text-muted-foreground">
          <RefreshCw className="size-5 animate-spin text-muted-foreground mb-2" />
          <span>Analyzing password health…</span>
        </div>
      ) : report ? (
        <div className="grid gap-5">
          <div className="grid grid-cols-3 gap-3">
            <div className="rounded-lg border border-border bg-background/40 p-3">
              <span className="text-[11px] text-muted-foreground">Logins checked</span>
              <div className="mt-1 text-lg font-bold text-foreground">{report.totalLogins}</div>
            </div>
            <div className="rounded-lg border border-border bg-background/40 p-3">
              <span className="text-[11px] text-muted-foreground">Weak passwords</span>
              <div className={`mt-1 text-lg font-bold ${weakItems.length > 0 ? 'text-warning' : 'text-success'}`}>
                {weakItems.length}
              </div>
            </div>
            <div className="rounded-lg border border-border bg-background/40 p-3">
              <span className="text-[11px] text-muted-foreground">Reused passwords</span>
              <div className={`mt-1 text-lg font-bold ${reusedItemGroups.length > 0 ? 'text-destructive' : 'text-success'}`}>
                {reusedItemGroups.reduce((acc, g) => acc + g.length, 0)}
              </div>
            </div>
          </div>

          {weakItems.length === 0 && reusedItemGroups.length === 0 ? (
            <div className="grid min-h-32 place-items-center rounded-lg border border-success/20 bg-success/5 p-6 text-center text-success">
              <ShieldCheck className="size-8 mb-2" />
              <p className="font-semibold">All passwords are in good health!</p>
              <p className="text-[11px] text-muted-foreground">No weak or reused passwords detected across your Vault.</p>
            </div>
          ) : null}

          {weakItems.length > 0 ? (
            <div className="grid gap-2">
              <div className="flex items-center gap-1.5 text-xs font-semibold text-warning">
                <AlertCircle className="size-3.5" />
                <span>Weak Passwords ({weakItems.length})</span>
              </div>
              <div className="grid gap-1 rounded-lg border border-border bg-background/40 p-2">
                {weakItems.map(item => (
                  <button
                    key={item.id}
                    className="flex w-full items-center justify-between rounded p-2 text-left hover:bg-surface-hover"
                    type="button"
                    onClick={() => onSelectItem(item)}>
                    <div className="min-w-0 flex-1">
                      <span className="truncate font-semibold text-foreground">{item.title || getPrimaryOrigin(item)}</span>
                      <span className="ml-2 text-muted-foreground">{item.usernameHint}</span>
                    </div>
                    <span className="text-[10px] text-warning">Review & Update</span>
                  </button>
                ))}
              </div>
            </div>
          ) : null}

          {reusedItemGroups.length > 0 ? (
            <div className="grid gap-2">
              <div className="flex items-center gap-1.5 text-xs font-semibold text-destructive">
                <ShieldAlert className="size-3.5" />
                <span>Reused Passwords ({reusedItemGroups.length} groups)</span>
              </div>
              <div className="grid gap-2.5">
                {reusedItemGroups.map((group, groupIdx) => (
                  <div key={groupIdx} className="rounded-lg border border-destructive/20 bg-background/40 p-2.5">
                    <span className="text-[11px] font-medium text-destructive">Shared by {group.length} accounts:</span>
                    <div className="mt-1.5 grid gap-1">
                      {group.map(item => (
                        <button
                          key={item.id}
                          className="flex w-full items-center justify-between rounded p-1.5 text-left hover:bg-surface-hover"
                          type="button"
                          onClick={() => onSelectItem(item)}>
                          <span className="truncate text-xs text-foreground">{item.title || getPrimaryOrigin(item)}</span>
                          <span className="text-[10px] text-muted-foreground">{item.usernameHint}</span>
                        </button>
                      ))}
                    </div>
                  </div>
                ))}
              </div>
            </div>
          ) : null}
        </div>
      ) : (
        <div className="grid min-h-32 place-items-center text-muted-foreground">
          <p>Health report is unavailable at this time.</p>
        </div>
      )}
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

function EmptyTrashDialog({
  onCancel,
  onConfirm,
}: {
  readonly onCancel: () => void;
  readonly onConfirm: () => Promise<void>;
}) {
  const [busy, setBusy] = useState(false);

  return (
    <Dialog open onOpenChange={open => { if (!open) onCancel(); }}>
      <DialogContent>
        <DialogHeader>
          <DialogTitle>Empty Trash?</DialogTitle>
          <DialogDescription>
            Permanently delete all items currently in the trash. This action cannot be undone.
          </DialogDescription>
        </DialogHeader>
        <DialogFooter>
          <Button type="button" variant="outline" onClick={onCancel}>Cancel</Button>
          <Button
            className="bg-destructive text-destructive-foreground hover:bg-destructive/90"
            disabled={busy}
            type="button"
            onClick={async () => {
              setBusy(true);
              await onConfirm();
              setBusy(false);
            }}>
            {busy ? 'Emptying…' : 'Empty trash'}
          </Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
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
