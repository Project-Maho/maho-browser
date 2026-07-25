// Copyright 2026 Maho Browser. All rights reserved.

import React, {useState} from 'react';

import {Button} from '@ui/button';
import {Database, Download, Loader2, ShieldAlert} from '@icons/lucide';
import type {
  PasswordImportOperationResult,
  PasswordImportPreview,
} from '../mojo.js';
import {
  EmptyDescription,
  SectionCard,
} from './domain_panes.js';
import {
  ImportPreviewPanel,
  ImportStatusMessage,
  ImportSummaryPanel,
  type ImportPreviewState,
  type ImportStatus,
  type ImportSummaryState,
} from './passwords_import_panels.js';
import {
  getPasswordImportSourceCards,
  summarizeImportFilePath,
  type PasswordImportFormatOption,
} from './passwords_import_model.js';
import type {MahoSettingsStore} from './store.js';

type ImportFlowState =
    | {readonly kind: 'idle'}
    | {readonly kind: 'preview'; readonly state: ImportPreviewState}
    | {readonly kind: 'summary'; readonly state: ImportSummaryState};

type GuidedPasswordImportFlowProps = {
  readonly onCommitted: () => Promise<void>;
  readonly store: MahoSettingsStore;
};

const ICON_CLASS = 'size-4 shrink-0';
const CARD_GRID_CLASS = 'grid gap-4 p-6 md:grid-cols-2';
const SOURCE_CARD_CLASS = 'flex h-full flex-col gap-4 rounded-xl border border-border/70 bg-background/25 p-4';
const FORMAT_BUTTON_CLASS = 'justify-start bg-background/70';

export function GuidedPasswordImportFlow({onCommitted, store}: GuidedPasswordImportFlowProps) {
  const handler = store.getHandler();
  const [busyFormatLabel, setBusyFormatLabel] = useState<string | null>(null);
  const [confirmed, setConfirmed] = useState(false);
  const [flowState, setFlowState] = useState<ImportFlowState>({kind: 'idle'});
  const [status, setStatus] = useState<ImportStatus | null>(null);

  const resetPreview = () => {
    setConfirmed(false);
    setFlowState({kind: 'idle'});
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
      description="Choose a user-exported file, preview safe counts first, then confirm before Maho writes anything to the Vault."
      className="mt-6"
    >
      <div className={CARD_GRID_CLASS}>
        {getPasswordImportSourceCards().map(card => (
          <article className={SOURCE_CARD_CLASS} key={card.cardId}>
            <div className="flex items-start gap-3">
              <div className="mt-1 rounded-lg border border-border bg-background/70 p-2 text-muted-foreground">
                <Database className={ICON_CLASS} />
              </div>
              <div className="min-w-0 space-y-1">
                <h4 className="text-sm font-semibold text-foreground">{card.displayName}</h4>
                <p className="text-xs leading-5 text-muted-foreground">{card.description}</p>
                <p className="text-xs font-medium text-foreground">Accepted: {card.acceptedFormats}</p>
              </div>
            </div>
            <ol className="grid gap-2 pl-4 text-xs leading-5 text-muted-foreground">
              {card.instructions.map(instruction => <li className="list-decimal" key={instruction}>{instruction}</li>)}
            </ol>
            <p className="flex items-start gap-2 rounded-lg border border-warning/30 bg-warning/10 px-3 py-2 text-xs leading-5 text-warning">
              <ShieldAlert className={ICON_CLASS} />
              {card.warning}
            </p>
            <div className="mt-auto flex flex-wrap gap-2">
              {card.formats.map(option => (
                <Button
                  className={FORMAT_BUTTON_CLASS}
                  disabled={busyFormatLabel !== null}
                  key={option.descriptor.formatId}
                  type="button"
                  variant="outline"
                  onClick={() => void chooseFile(option)}
                >
                  {busyFormatLabel === option.descriptor.label ? <Loader2 className={`${ICON_CLASS} animate-spin`} /> : <Download className={ICON_CLASS} />}
                  Choose {option.descriptor.label}
                </Button>
              ))}
            </div>
          </article>
        ))}
      </div>
      {flowState.kind === 'idle' ? <EmptyDescription message="No import file selected yet." /> : null}
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
