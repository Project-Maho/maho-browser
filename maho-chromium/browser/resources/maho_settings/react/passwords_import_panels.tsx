// Copyright 2026 Maho Browser. All rights reserved.

import React from 'react';

import {Button} from '@ui/button';
import {AlertCircle, CheckCircle} from '@icons/lucide';
import type {PasswordImportPreview} from '../mojo.js';
import {
  ManagedSettingRow,
  SETTING_ACTIONS_CLASS,
  SETTING_CONTROL_STACKED_CLASS,
  SETTING_DESCRIPTION_CLASS,
} from './domain_panes.js';

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
