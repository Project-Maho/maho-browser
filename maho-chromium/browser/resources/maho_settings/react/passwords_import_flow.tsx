// Copyright 2026 Maho Browser. All rights reserved.

import React, {useMemo, useState} from 'react';

import {Select, SelectContent, SelectItem, SelectTrigger, SelectValue} from '@ui/select';
import type {
  PasswordImportOperationResult,
  PasswordImportPreview,
} from '../mojo.js';
import {SectionCard} from './domain_panes.js';
import {
  ImportPreviewPanel,
  ImportProviderPanel,
  ImportStatusMessage,
  ImportSummaryPanel,
  type ImportPreviewState,
  type ImportStatus,
  type ImportSummaryState,
} from './passwords_import_panels.js';
import {
  getDefaultPasswordImportFormat,
  getPasswordImportFormat,
  getPasswordImportProvider,
  getPasswordImportProviders,
  summarizeImportFilePath,
  type PasswordImportFormatOption,
  type PasswordImportProviderId,
} from './passwords_import_model.js';
import type {PasswordImportFormatId} from './passwords_model.js';
import type {MahoSettingsStore} from './store.js';

type ImportFlowState =
    | {readonly kind: 'idle'}
    | {readonly kind: 'preview'; readonly state: ImportPreviewState}
    | {readonly kind: 'summary'; readonly state: ImportSummaryState};

type ImportPasswordsSectionProps = {
  readonly onCommitted: () => Promise<void>;
  readonly store: MahoSettingsStore;
};

const PROVIDER_SELECT_CLASS = 'w-full sm:max-w-sm';

/**
 * Password import belongs to the general Passwords settings pane rather than
 * the Saved Passwords Vault browser. Keeping the whole guided flow behind this
 * section makes that information-architecture boundary explicit at the call
 * site in passwords_settings.tsx.
 */
export function ImportPasswordsSection({onCommitted, store}: ImportPasswordsSectionProps) {
  const handler = store.getHandler();
  const providers = getPasswordImportProviders();
  const [busyFormatLabel, setBusyFormatLabel] = useState<string | null>(null);
  const [confirmed, setConfirmed] = useState(false);
  const [flowState, setFlowState] = useState<ImportFlowState>({kind: 'idle'});
  const [selectedProviderId, setSelectedProviderId] =
      useState<PasswordImportProviderId>('onepassword');
  const selectedProvider = useMemo(
      () => getPasswordImportProvider(selectedProviderId), [selectedProviderId]);
  const [selectedFormatId, setSelectedFormatId] = useState<PasswordImportFormatId>(
      () => getDefaultPasswordImportFormat(getPasswordImportProvider('onepassword')).descriptor.formatId);
  const selectedFormat = useMemo(
      () => getPasswordImportFormat(selectedProvider, selectedFormatId),
      [selectedFormatId, selectedProvider]);
  const [status, setStatus] = useState<ImportStatus | null>(null);

  const resetPreview = () => {
    setConfirmed(false);
    setFlowState({kind: 'idle'});
    setStatus(null);
  };

  const selectProvider = (providerId: PasswordImportProviderId) => {
    const provider = getPasswordImportProvider(providerId);
    setSelectedProviderId(providerId);
    setSelectedFormatId(getDefaultPasswordImportFormat(provider).descriptor.formatId);
    setStatus(null);
  };

  const chooseFile = async (option: PasswordImportFormatOption) => {
    setBusyFormatLabel(option.descriptor.label);
    setConfirmed(false);
    setStatus(null);
    try {
      const {filePath} = await handler.selectPasswordImportFile(option.sourceFormat);
      if (!filePath) {
        setStatus({message: 'File selection was cancelled.', tone: 'danger'});
        return;
      }
      const {result} = await handler.previewPasswordImport(option.sourceFormat, filePath);
      if (!result.success || !result.preview) {
        setStatus({message: importFailureMessage(result, 'Could not preview this export file.'), tone: 'danger'});
        return;
      }
      setFlowState({
        kind: 'preview',
        state: {
          fileName: summarizeImportFilePath(filePath),
          formatLabel: option.descriptor.label,
          preview: result.preview,
        },
      });
    } finally {
      setBusyFormatLabel(null);
    }
  };

  const cancelPreview = async () => {
    setBusyFormatLabel('Cancelling');
    try {
      await handler.cancelPasswordImport();
      resetPreview();
    } finally {
      setBusyFormatLabel(null);
    }
  };

  const commitPreview = async (preview: PasswordImportPreview) => {
    if (!confirmed) {
      setStatus({message: 'Confirm the preview before importing.', tone: 'danger'});
      return;
    }
    setBusyFormatLabel('Importing');
    setStatus(null);
    try {
      const {result} = await handler.commitPasswordImport(preview.previewToken);
      if (!result.success) {
        setStatus({message: importFailureMessage(result, 'Import failed before anything was written.'), tone: 'danger'});
        return;
      }
      setFlowState({
        kind: 'summary',
        state: {
          committed: result.committed,
          failed: result.failed,
          sourceLabel: preview.sourceLabel,
          terminalResultCount: result.terminalResultCount,
        },
      });
      setConfirmed(false);
      setStatus({message: 'Import committed to the Vault.', tone: 'success'});
      await onCommitted();
    } finally {
      setBusyFormatLabel(null);
    }
  };

  return (
    <SectionCard
      title="Import passwords"
      description="Import logins from another password manager here. Maho previews sanitized counts before writing anything to the local Vault."
      className="mt-6">
      {flowState.kind === 'idle' ? (
        <div className="grid gap-4 p-6">
          <div className="grid gap-1.5">
            <label className="text-xs font-medium text-foreground" htmlFor="password-import-provider">
              Password manager
            </label>
            <Select
              disabled={busyFormatLabel !== null}
              value={selectedProviderId}
              onValueChange={value => selectProvider(value as PasswordImportProviderId)}>
              <SelectTrigger
                aria-label="Password import provider"
                className={PROVIDER_SELECT_CLASS}
                id="password-import-provider">
                <SelectValue />
              </SelectTrigger>
              <SelectContent>
                {providers.map(provider => (
                  <SelectItem key={provider.providerId} value={provider.providerId}>
                    {provider.displayName}
                  </SelectItem>
                ))}
              </SelectContent>
            </Select>
          </div>
          <ImportProviderPanel
            busy={busyFormatLabel !== null}
            provider={selectedProvider}
            selectedFormat={selectedFormat}
            onChooseFile={chooseFile}
            onFormatChange={setSelectedFormatId}
          />
        </div>
      ) : null}
      {flowState.kind === 'preview' ? (
        <ImportPreviewPanel
          busy={busyFormatLabel !== null}
          confirmed={confirmed}
          state={flowState.state}
          onCancel={cancelPreview}
          onConfirmChange={setConfirmed}
          onCommit={commitPreview}
        />
      ) : null}
      {flowState.kind === 'summary' ? <ImportSummaryPanel state={flowState.state} onDone={resetPreview} /> : null}
      {status ? <ImportStatusMessage status={status} /> : null}
    </SectionCard>
  );
}

function importFailureMessage(result: PasswordImportOperationResult, fallback: string): string {
  if (result.errorMessage) {
    return result.errorMessage;
  }
  if (result.errorCode === 'locked' || result.errorCode === 'vault_locked') {
    return 'Vault locked before import could commit. Unlock the Vault and preview the file again; Maho did not write through any legacy plaintext path.';
  }
  return fallback;
}
