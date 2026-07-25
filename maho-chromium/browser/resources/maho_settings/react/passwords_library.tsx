// Copyright 2026 Maho Browser. All rights reserved.

import React, {useCallback, useEffect, useRef, useState} from 'react';

import {PasswordProviderKind, SecretAction, VaultItemKind, VaultLockState} from '../mojo.js';
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
  EditSavedPasswordInput,
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
  const hasLoadedOnceRef = useRef(false);
  const requestIdRef = useRef(0);
  const passwordsEnabled = getPasswordsEnabled(settings);

  const clearTransientState = useCallback(() => {
    setActionStatus(null);
    setDrawer(null);
  }, []);

  const loadSavedPasswords: LoadSavedPasswords = useCallback(async (showLoading = true) => {
    const requestId = ++requestIdRef.current;
    if (showLoading) {
      setLoadState(createSavedPasswordLoadingState());
    }

    try {
      const [providerResult, vaultStatusResult] = await Promise.all([
        handler.getPasswordProviderStatus(),
        handler.getVaultStatus(),
      ]);
      if (requestId !== requestIdRef.current) {
        return;
      }

      const providerStatus = providerResult.status;
      const vaultStatus = vaultStatusResult.result;
      const isUnlocked = vaultStatus.status?.lockState === VaultLockState.kUnlocked;
      let items: VaultItem[] = [];
      if (passwordsEnabled && isMahoNativeProvider(providerStatus.provider) && isUnlocked) {
        const {result} = await handler.listVaultItems(
            PasswordProviderKind.kMahoNative, [VaultItemKind.kLogin], null, 0);
        if (requestId !== requestIdRef.current) {
          return;
        }
        if (!result.success) {
          hasLoadedOnceRef.current = true;
          setLoadState(createSavedPasswordErrorState(
              vaultOperationMessage(listFailureAsOperation(result), 'Failed to load saved passwords.')));
          return;
        }
        items = result.items;
      } else {
        clearTransientState();
      }

      hasLoadedOnceRef.current = true;
      setLoadState(createSavedPasswordLoadedState({items, providerStatus, vaultStatus}));
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
    return () => {
      requestIdRef.current += 1;
      clearTransientState();
    };
  }, [clearTransientState]);

  const handleAddPassword = async (input: AddSavedPasswordInput): Promise<string | null> => {
    const normalizedOrigin = normalizeCredentialOrigin(input.origin);
    if (!normalizedOrigin) {
      return 'Enter a valid site origin.';
    }
    if (!input.password) {
      return 'Enter a password to save.';
    }
    const {result} = await handler.addVaultLogin(
        normalizedOrigin, [normalizedOrigin], input.username.trim(), input.password);
    if (operationResetsTransientState(result)) {
      clearTransientState();
      await loadSavedPasswords(true);
      return vaultOperationMessage(result, 'Vault locked before the password could be saved.');
    }
    if (!result.success) {
      return vaultOperationMessage(result, 'Failed to save password.');
    }
    await loadSavedPasswords(false);
    return null;
  };

  const handleEditPassword = async (
      item: VaultItem, input: EditSavedPasswordInput): Promise<string | null> => {
    const normalizedOrigin = normalizeCredentialOrigin(input.origin);
    if (!normalizedOrigin) {
      return 'Enter a valid site origin.';
    }
    const primaryOrigin = getPrimaryOrigin(item);
    const title = item.title && item.title !== primaryOrigin ? item.title : normalizedOrigin;
    const {result} = await handler.updateVaultLogin(
        item.id, item.revision, title, [normalizedOrigin], input.username.trim(), null);
    if (operationResetsTransientState(result)) {
      clearTransientState();
      await loadSavedPasswords(true);
      return vaultOperationMessage(result, 'Vault locked before changes could be saved.');
    }
    if (!result.success) {
      return vaultOperationMessage(result, 'Failed to update saved password.');
    }
    await loadSavedPasswords(false);
    return null;
  };

  const handleDeletePassword = async (item: VaultItem) => {
    const {result} = await handler.deleteVaultItem(item.id, item.revision);
    clearTransientState();
    if (!result.success && !operationResetsTransientState(result)) {
      setActionStatus({itemId: item.id, message: vaultOperationMessage(result, 'Failed to delete saved password.'), status: 'failed'});
      return;
    }
    await loadSavedPasswords(operationResetsTransientState(result));
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
    setActionStatus({
      itemId: item.id,
      message: action === SecretAction.kCopy ?
          'Copied by the browser process. No password was returned to this page.' :
          'Fill request sent through the status-only path. If no compatible field is active, no page field changes.',
      status: 'success',
    });
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
          onCloseDrawer={() => setDrawer(null)}
          onDelete={handleDeletePassword}
          onEdit={handleEditPassword}
          onOpenAdd={() => setDrawer({kind: 'add'})}
          onOpenEdit={item => setDrawer({item, kind: 'edit'})}
          onSecretAction={handleSecretAction}
          onSearchChange={setSearchInputValue}
          passwordsEnabled={passwordsEnabled}
          searchInputValue={searchInputValue}
          searchQuery={searchQuery}
          store={store}
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
