// Copyright 2026 Maho Browser. All rights reserved.

import React from 'react';

import {Button} from '@ui/button';
import {Select, SelectContent, SelectItem, SelectTrigger, SelectValue} from '@ui/select';
import {AlertCircle, CheckCircle, Database, Download, Loader2, ShieldAlert} from '@icons/lucide';
import type {PasswordImportPreview} from '../mojo.js';
import {
  ManagedSettingRow,
  SETTING_ACTIONS_CLASS,
  SETTING_CONTROL_STACKED_CLASS,
  SETTING_DESCRIPTION_CLASS,
} from './domain_panes.js';
import type {
  PasswordImportFormatOption,
  PasswordImportProvider,
} from './passwords_import_model.js';
import type {PasswordImportFormatId} from './passwords_model.js';

export type ImportPreviewState = {
  readonly fileName: string;
  readonly formatLabel: string;
  readonly preview: PasswordImportPreview;
};

export type ImportSummaryState = {
  readonly committed: number;
  readonly failed: number;
  readonly sourceLabel: string;
  readonly terminalResultCount: number;
};

export type ImportStatus =
    | {readonly tone: 'danger'; readonly message: string}
    | {readonly tone: 'success'; readonly message: string};

const ICON_CLASS = 'size-4 shrink-0';
const COUNT_GRID_CLASS = 'grid gap-3 rounded-xl border border-border/70 bg-background/25 p-4 sm:grid-cols-3';
const COUNT_VALUE_CLASS = 'text-xl font-semibold tracking-tight text-foreground';
const COUNT_LABEL_CLASS = 'text-xs leading-5 text-muted-foreground';
const PROVIDER_PANEL_CLASS = 'grid gap-4 rounded-xl border border-border/70 bg-background/25 p-4';
const PROVIDER_META_CLASS = 'grid gap-1';
const PROVIDER_SELECT_CLASS = 'w-full sm:max-w-sm';

export function ImportProviderPanel(
    {busy, onChooseFile, onFormatChange, provider, selectedFormat}: {
      readonly busy: boolean;
      readonly onChooseFile: (option: PasswordImportFormatOption) => Promise<void>;
      readonly onFormatChange: (formatId: PasswordImportFormatId) => void;
      readonly provider: PasswordImportProvider;
      readonly selectedFormat: PasswordImportFormatOption;
    }) {
  return (
    <div className={PROVIDER_PANEL_CLASS} data-testid="password-import-provider-guide">
      <div className="flex items-start gap-3">
        <div className="mt-0.5 rounded-lg border border-border bg-background/70 p-2 text-muted-foreground">
          <Database className={ICON_CLASS} />
        </div>
        <div className={PROVIDER_META_CLASS}>
          <h4 className="text-sm font-semibold text-foreground">{provider.displayName}</h4>
          <p className="text-xs leading-5 text-muted-foreground">{provider.description}</p>
          <p className="text-xs font-medium text-foreground">Accepted: {provider.acceptedFormats}</p>
        </div>
      </div>

      <ol className="grid gap-2 pl-4 text-xs leading-5 text-muted-foreground">
        {provider.instructions.map(instruction => (
          <li className="list-decimal" key={instruction}>{instruction}</li>
        ))}
      </ol>

      <p className="flex items-start gap-2 rounded-lg border border-warning/30 bg-warning/10 px-3 py-2 text-xs leading-5 text-warning">
        <ShieldAlert className={ICON_CLASS} />
        {provider.warning}
      </p>

      <div className="grid gap-3 sm:grid-cols-[minmax(0,1fr)_auto] sm:items-end">
        <div className="grid gap-1.5">
          <label className="text-xs font-medium text-foreground" htmlFor="password-import-format">
            Export format
          </label>
          {provider.formats.length > 1 ? (
            <Select
              disabled={busy}
              value={selectedFormat.descriptor.formatId}
              onValueChange={value => onFormatChange(value as PasswordImportFormatId)}>
              <SelectTrigger aria-label="Password export format" className={PROVIDER_SELECT_CLASS} id="password-import-format">
                <SelectValue />
              </SelectTrigger>
              <SelectContent>
                {provider.formats.map(option => (
                  <SelectItem
                    key={option.descriptor.formatId}
                    value={option.descriptor.formatId}>
                    {option.descriptor.label}
                  </SelectItem>
                ))}
              </SelectContent>
            </Select>
          ) : (
            <p className="flex min-h-9 items-center rounded-md border border-border/70 bg-background/70 px-3 text-sm text-foreground">
              {selectedFormat.descriptor.label}
            </p>
          )}
        </div>
        <Button
          disabled={busy}
          type="button"
          onClick={() => void onChooseFile(selectedFormat)}>
          {busy ? <Loader2 className={`${ICON_CLASS} animate-spin`} /> : <Download className={ICON_CLASS} />}
          {busy ? 'Opening…' : 'Choose File'}
        </Button>
      </div>
    </div>
  );
}

export function ImportPreviewPanel(
    {busy, confirmed, onCancel, onCommit, onConfirmChange, state}: {
      readonly busy: boolean;
      readonly confirmed: boolean;
      readonly onCancel: () => Promise<void>;
      readonly onCommit: (preview: PasswordImportPreview) => Promise<void>;
      readonly onConfirmChange: (checked: boolean) => void;
      readonly state: ImportPreviewState;
    }) {
  const preview = state.preview;
  return (
    <ManagedSettingRow
      controlClassName={SETTING_CONTROL_STACKED_CLASS}
      description={`${state.formatLabel} selected: ${state.fileName}. Review this sanitized preview before writing to the Vault.`}
      title="Preview import">
      <div className="grid w-full gap-4">
        <div className={COUNT_GRID_CLASS}>
          <CountTile label="Ready to import" value={preview.imported} />
          <CountTile label="Skipped" value={preview.skipped} />
          <CountTile label="Duplicates" value={preview.duplicates} />
          <CountTile label="Blank passwords" value={preview.blankPasswords} />
          <CountTile label="Unsupported fields" value={preview.unsupportedFields} />
          <CountTile label="Source" value={preview.sourceLabel} />
        </div>
        <SafePreviewMessages preview={preview} />
        <label className="flex items-start gap-2 text-sm text-foreground">
          <input
            checked={confirmed}
            className="mt-1"
            type="checkbox"
            onChange={event => onConfirmChange(event.currentTarget.checked)}
          />
          I reviewed the preview counts and want to import these records into the Vault.
        </label>
        <div className={SETTING_ACTIONS_CLASS}>
          <Button type="button" variant="outline" disabled={busy} onClick={() => void onCancel()}>Cancel preview</Button>
          <Button type="button" disabled={busy || !confirmed} onClick={() => void onCommit(preview)}>Confirm import</Button>
        </div>
      </div>
    </ManagedSettingRow>
  );
}

export function ImportSummaryPanel(
    {onDone, state}: {readonly onDone: () => void; readonly state: ImportSummaryState}) {
  return (
    <ManagedSettingRow
      controlClassName={SETTING_CONTROL_STACKED_CLASS}
      description={`${state.sourceLabel} finished with one terminal import result.`}
      title="Imported passwords">
      <div className="grid w-full gap-4">
        <div className={COUNT_GRID_CLASS}>
          <CountTile label="Committed" value={state.committed} />
          <CountTile label="Failed" value={state.failed} />
          <CountTile label="Terminal results" value={state.terminalResultCount} />
        </div>
        <div className={SETTING_ACTIONS_CLASS}>
          <Button type="button" onClick={onDone}>Done</Button>
        </div>
      </div>
    </ManagedSettingRow>
  );
}

export function ImportStatusMessage({status}: {readonly status: ImportStatus}) {
  const className = status.tone === 'success' ? 'text-success' : 'text-destructive';
  return <p className={`mx-6 mb-4 flex items-start gap-2 text-sm leading-6 ${className}`}>
    {status.tone === 'success' ? <CheckCircle className={ICON_CLASS} /> : <AlertCircle className={ICON_CLASS} />}
    {status.message}
  </p>;
}

function SafePreviewMessages({preview}: {readonly preview: PasswordImportPreview}) {
  const messages = [...preview.safeMessages, ...preview.safeErrors];
  if (messages.length === 0) {
    return <p className={SETTING_DESCRIPTION_CLASS}>No parser warnings were reported. Preview details never include password values or TOTP seeds.</p>;
  }
  return (
    <ul className="grid gap-2 rounded-xl border border-border/70 bg-background/25 p-4 text-xs leading-5 text-muted-foreground">
      {messages.map(message => <li className="flex items-start gap-2" key={message}><AlertCircle className={ICON_CLASS} />{message}</li>)}
    </ul>
  );
}

function CountTile({label, value}: {readonly label: string; readonly value: number | string}) {
  return <div><div className={COUNT_VALUE_CLASS}>{value}</div><div className={COUNT_LABEL_CLASS}>{label}</div></div>;
}
