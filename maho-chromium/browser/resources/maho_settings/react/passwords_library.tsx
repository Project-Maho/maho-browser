// Copyright 2026 Maho Browser. All rights reserved.

import React, {useCallback, useEffect, useRef, useState} from 'react';

import {
  PasswordProviderKind,
  SecretAction,
  VaultItemKind,
  VaultLockState,
} from '../mojo.js';
import type {SettingValue, VaultItem, VaultItemListResult, VaultOperationResult} from '../mojo.js';
import type {PaneDefinition} from '../models.js';
import {
  PaneLoading,
  PaneRetry,
  PaneShell,
} from './domain_panes.js';
import {getPasswordsEnabled, isMahoNativeProvider} from './passwords_model.js';
import {
  createSavedPasswordErrorState,
  createSavedPasswordLoadedState,
  createSavedPasswordLoadingState,
  getPrimaryOrigin,
  normalizeCredentialOrigin,
  operationResetsTransientState,
  vaultOperationMessage,
} from './saved_passwords_model.js';
import type {
  AddSavedPasswordInput,
  AddSecureNoteInput,
  EditSavedPasswordInput,
  EditSecureNoteInput,
  LoadSavedPasswords,
  SavedPasswordActionStatus,
  SavedPasswordDrawerState,
  SavedPasswordLoadState,
} from './saved_passwords_model.js';
import {SavedPasswordsContent} from './saved_passwords_content.js';
import type {MahoSettingsStore} from './store.js';

export function SavedPasswordsPane(
    {pane, settings, store}: {
      readonly pane: PaneDefinition;
      readonly settings: readonly SettingValue[];
      readonly store: MahoSettingsStore;
    }) {
  const handler = store.getHandler();
  const [actionStatus, setActionStatus] = useState<SavedPasswordActionStatus | null>(null);
  const [drawer, setDrawer] = useState<SavedPasswordDrawerState>(null);
  const [loadState, setLoadState] = useState<SavedPasswordLoadState>(
      createSavedPasswordLoadingState());
  const [searchInputValue, setSearchInputValue] = useState('');
  const [searchQuery, setSearchQuery] = useState('');
  const [transientResetToken, setTransientResetToken] = useState(0);
  const hasLoadedOnceRef = useRef(false);
  const requestIdRef = useRef(0);
  const passwordsEnabled = getPasswordsEnabled(settings);

  const clearTransientState = useCallback(() => {
    setActionStatus(null);
    setDrawer(null);
    setTransientResetToken(token => token + 1);
  }, []);

  const loadSavedPasswords: LoadSavedPasswords = useCallback(async (showLoading = true) => {
    const requestId = ++requestIdRef.current;
    if (showLoading) {
      setLoadState(createSavedPasswordLoadingState());
    }

    try {
      const [providerResult, vaultStatusResult, vaultPreflightResult] =
          await Promise.all([
            handler.getPasswordProviderStatus(),
            handler.getVaultStatus(),
            handler.getVaultPreflightState(),
          ]);
      if (requestId !== requestIdRef.current) {
        return;
      }

      const providerStatus = providerResult.status;
      const vaultStatus = vaultStatusResult.result;
      const vaultPreflightState = vaultPreflightResult.state;
      const isUnlocked = vaultStatus.status?.lockState === VaultLockState.kUnlocked;
      let items: VaultItem[] = [];
      let trashItems: VaultItem[] = [];
      if (passwordsEnabled && isMahoNativeProvider(providerStatus.provider) && isUnlocked) {
        // The core excludes trashed items unless trash_only is set, so the
        // library and the Trash view need one list request each.
        const kinds = [VaultItemKind.kLogin, VaultItemKind.kSecureItem];
        const [normalResult, trashResult] = await Promise.all([
          handler.listVaultItems(PasswordProviderKind.kMahoNative, kinds, null, 0, false, false),
          handler.listVaultItems(PasswordProviderKind.kMahoNative, kinds, null, 0, true, false),
        ]);
        if (requestId !== requestIdRef.current) {
          return;
        }
        const failed = !normalResult.result.success ? normalResult.result :
            !trashResult.result.success ? trashResult.result : null;
        if (failed) {
          hasLoadedOnceRef.current = true;
          setLoadState(createSavedPasswordErrorState(
              vaultOperationMessage(listFailureAsOperation(failed), 'Failed to load saved passwords.')));
          return;
        }
        items = normalResult.result.items.filter(i => !i.trashedAt);
        trashItems = trashResult.result.items.filter(i => Boolean(i.trashedAt));
      } else {
        clearTransientState();
      }

      hasLoadedOnceRef.current = true;
      setLoadState(createSavedPasswordLoadedState({
        items,
        trashItems,
        providerStatus,
        vaultPreflightState,
        vaultStatus,
      }));
    } catch (error) {
      if (requestId !== requestIdRef.current) {
        return;
      }
      hasLoadedOnceRef.current = true;
      const message = error instanceof Error ? error.message : 'Failed to load saved passwords.';
      setLoadState(createSavedPasswordErrorState(message));
    }
  }, [clearTransientState, handler, passwordsEnabled]);

  useEffect(() => {
    const timeoutId = window.setTimeout(() => {
      setSearchQuery(searchInputValue.trim());
    }, 200);
    return () => window.clearTimeout(timeoutId);
  }, [searchInputValue]);

  useEffect(() => {
    clearTransientState();
    void loadSavedPasswords(!hasLoadedOnceRef.current);
  }, [clearTransientState, loadSavedPasswords, settings]);

  useEffect(() => {
    const router = store.getCallbackRouter();
    const listenerId = router.onVaultLockStateChanged.addListener(() => {
      clearTransientState();
      void loadSavedPasswords(false);
    });
    return () => {
      router.removeListener(listenerId);
    };
  }, [clearTransientState, loadSavedPasswords, store]);

  useEffect(() => {
    return () => {
      requestIdRef.current += 1;
      clearTransientState();
    };
  }, [clearTransientState]);

  const handleAddPassword = async (input: AddSavedPasswordInput): Promise<string | null> => {
    const primaryOrigin = normalizeCredentialOrigin(input.origin);
    if (!primaryOrigin) {
      return 'Enter a valid site origin.';
    }
    if (!input.password) {
      return 'Enter a password to save.';
    }
    const origins = input.origins && input.origins.length > 0
        ? input.origins.map(o => normalizeCredentialOrigin(o) || o).filter(Boolean)
        : [primaryOrigin];
    const title = input.title?.trim() || primaryOrigin;
    const {result} = await handler.addVaultLogin(
        title, origins, input.username.trim(), input.password, input.notes || null);
    if (operationResetsTransientState(result)) {
      clearTransientState();
      await loadSavedPasswords(true);
      return vaultOperationMessage(result, 'Vault locked before the password could be saved.');
    }
    if (!result.success) {
      return vaultOperationMessage(result, 'Failed to save password.');
    }
    if (input.totpSecret && result.item) {
      try {
        await handler.setVaultLoginTotp(result.item.id, result.item.revision, input.totpSecret.trim());
      } catch {
      }
    }
    await loadSavedPasswords(false);
    return null;
  };

  const handleAddSecureNote = async (input: AddSecureNoteInput): Promise<string | null> => {
    if (!input.title.trim()) {
      return 'Enter a title for the secure note.';
    }
    const {result} = await handler.addVaultSecureNote(input.title.trim(), input.notes);
    if (operationResetsTransientState(result)) {
      clearTransientState();
      await loadSavedPasswords(true);
      return vaultOperationMessage(result, 'Vault locked before the note could be saved.');
    }
    if (!result.success) {
      return vaultOperationMessage(result, 'Failed to save secure note.');
    }
    await loadSavedPasswords(false);
    return null;
  };

  const handleEditPassword = async (
      item: VaultItem, input: EditSavedPasswordInput): Promise<string | null> => {
    const primaryOrigin = normalizeCredentialOrigin(input.origin);
    if (!primaryOrigin) {
      return 'Enter a valid site origin.';
    }
    const origins = input.origins && input.origins.length > 0
        ? input.origins.map(o => normalizeCredentialOrigin(o) || o).filter(Boolean)
        : [primaryOrigin];
    const title = input.title?.trim() || item.title || primaryOrigin;
    const {result} = await handler.updateVaultLogin(
        item.id, item.revision, title, origins, input.username.trim(), input.password, input.notes ?? null);
    if (operationResetsTransientState(result)) {
      clearTransientState();
      await loadSavedPasswords(true);
      return vaultOperationMessage(result, 'Vault locked before changes could be saved.');
    }
    if (!result.success) {
      return vaultOperationMessage(result, 'Failed to update saved password.');
    }
    if (input.totpSecret) {
      const nextRev = result.item?.revision ?? item.revision;
      try {
        await handler.setVaultLoginTotp(item.id, nextRev, input.totpSecret.trim());
      } catch {
      }
    }
    await loadSavedPasswords(false);
    return null;
  };

  const handleEditSecureNote = async (
      item: VaultItem, input: EditSecureNoteInput): Promise<string | null> => {
    if (!input.title.trim()) {
      return 'Enter a title for the secure note.';
    }
    const {result} = await handler.updateVaultSecureNote(
        item.id, item.revision, input.title.trim(), input.notes);
    if (operationResetsTransientState(result)) {
      clearTransientState();
      await loadSavedPasswords(true);
      return vaultOperationMessage(result, 'Vault locked before changes could be saved.');
    }
    if (!result.success) {
      return vaultOperationMessage(result, 'Failed to update secure note.');
    }
    await loadSavedPasswords(false);
    return null;
  };

  // Item-state mutations (trash, restore, favorite, delete) do not carry
  // content, so a revision_conflict only means the row's revision went stale
  // (e.g. a secret use bumped it). Re-resolve the current revision and retry
  // once. Content edits deliberately do NOT go through this path: they must
  // surface the conflict instead of overwriting a newer version.
  const withCurrentRevision = async (
      item: VaultItem,
      mutate: (revision: bigint) => Promise<{result: VaultOperationResult}>):
      Promise<VaultOperationResult> => {
    const first = (await mutate(item.revision)).result;
    if (first.success || first.errorCode !== 'revision_conflict') {
      return first;
    }
    const kinds = [VaultItemKind.kLogin, VaultItemKind.kSecureItem];
    const lists = await Promise.all([
      handler.listVaultItems(PasswordProviderKind.kMahoNative, kinds, null, 0, false, false),
      handler.listVaultItems(PasswordProviderKind.kMahoNative, kinds, null, 0, true, false),
    ]);
    const current = lists.flatMap(l => (l.result.success ? l.result.items : []))
        .find(candidate => candidate.id === item.id);
    if (!current || current.revision === item.revision) {
      return first;
    }
    return (await mutate(current.revision)).result;
  };

  const handleDeletePassword = async (item: VaultItem) => {
    const result = await withCurrentRevision(
        item, revision => handler.deleteVaultItem(item.id, revision));
    clearTransientState();
    if (!result.success && !operationResetsTransientState(result)) {
      setActionStatus({itemId: item.id, message: vaultOperationMessage(result, 'Failed to delete saved password.'), status: 'failed'});
      return;
    }
    await loadSavedPasswords(operationResetsTransientState(result));
  };

  const handleTrash = async (item: VaultItem) => {
    const result = await withCurrentRevision(
        item, revision => handler.trashVaultItem(item.id, revision));
    clearTransientState();
    if (!result.success && !operationResetsTransientState(result)) {
      setActionStatus({itemId: item.id, message: vaultOperationMessage(result, 'Failed to move to trash.'), status: 'failed'});
      return;
    }
    await loadSavedPasswords(operationResetsTransientState(result));
  };

  const handleRestore = async (item: VaultItem) => {
    const result = await withCurrentRevision(
        item, revision => handler.restoreVaultItem(item.id, revision));
    clearTransientState();
    if (!result.success && !operationResetsTransientState(result)) {
      setActionStatus({itemId: item.id, message: vaultOperationMessage(result, 'Failed to restore item.'), status: 'failed'});
      return;
    }
    await loadSavedPasswords(operationResetsTransientState(result));
  };

  const handleEmptyTrash = async () => {
    const {result} = await handler.emptyVaultTrash();
    clearTransientState();
    if (!result.success && !operationResetsTransientState(result)) {
      setActionStatus({itemId: '', message: vaultOperationMessage(result, 'Failed to empty trash.'), status: 'failed'});
      return;
    }
    await loadSavedPasswords(operationResetsTransientState(result));
  };

  const handleToggleFavorite = async (item: VaultItem) => {
    const nextFav = !item.favorite;
    const result = await withCurrentRevision(
        item, revision => handler.setVaultItemFavorite(item.id, revision, nextFav));
    if (operationResetsTransientState(result)) {
      clearTransientState();
      await loadSavedPasswords(true);
      return;
    }
    if (!result.success) {
      setActionStatus({itemId: item.id, message: vaultOperationMessage(result, 'Failed to update favorite status.'), status: 'failed'});
      return;
    }
    await loadSavedPasswords(false);
  };

  const handleLock = async () => {
    clearTransientState();
    await handler.lockVault();
    await loadSavedPasswords(true);
  };

  const handleSecretAction = async (item: VaultItem, action: SecretAction) => {
    setActionStatus(null);
    const {result} = await handler.useVaultSecret(item.id, item.revision, action);
    if (operationResetsTransientState(result)) {
      clearTransientState();
      await loadSavedPasswords(true);
      setActionStatus({itemId: item.id, message: vaultOperationMessage(result, 'Vault locked before the action completed.'), status: 'failed'});
      return;
    }
    if (!result.success) {
      setActionStatus({itemId: item.id, message: vaultOperationMessage(result, 'Secret action failed.'), status: 'failed'});
      return;
    }
    if (action === SecretAction.kCopy) {
      setActionStatus({
        itemId: item.id,
        message: 'Copied by the browser process. No password was returned to this page.',
        status: 'success',
      });
    } else if (action === SecretAction.kReveal) {
      setActionStatus({
        itemId: item.id,
        message: 'Password revealed in native dialog. No password was returned to this page.',
        status: 'success',
      });
    }
    await loadSavedPasswords(false);
  };

  return (
    <PaneShell pane={pane}>
      {loadState.status === 'loading' ? <PaneLoading message="Loading saved passwords…" /> : null}
      {loadState.status === 'error' ? (
        <PaneRetry message={loadState.message} title="Saved Passwords unavailable" onRetry={() => void loadSavedPasswords(true)} />
      ) : null}
      {loadState.status === 'loaded' ? (
        <SavedPasswordsContent
          actionStatus={actionStatus}
          data={loadState.data}
          drawer={drawer}
          onAdd={handleAddPassword}
          onAddSecureNote={handleAddSecureNote}
          onCloseDrawer={() => setDrawer(null)}
          onDelete={handleDeletePassword}
          onEdit={handleEditPassword}
          onEditSecureNote={handleEditSecureNote}
          onEmptyTrash={handleEmptyTrash}
          onLock={handleLock}
          onOpenAdd={kind => setDrawer({kind: 'add', initialKind: kind})}
          onOpenEdit={item => setDrawer({item, kind: 'edit'})}
          onRestore={handleRestore}
          onSecretAction={handleSecretAction}
          onSearchChange={setSearchInputValue}
          onToggleFavorite={handleToggleFavorite}
          onTrash={handleTrash}
          passwordsEnabled={passwordsEnabled}
          searchInputValue={searchInputValue}
          searchQuery={searchQuery}
          store={store}
          transientResetToken={transientResetToken}
          loadSavedPasswords={loadSavedPasswords}
        />
      ) : null}
    </PaneShell>
  );
}

function listFailureAsOperation(result: VaultItemListResult): VaultOperationResult {
  return {
    success: false,
    errorCode: result.errorCode,
    errorMessage: result.errorMessage,
    status: null,
    item: null,
  };
}
