// Copyright 2026 Maho Browser. All rights reserved.

import React, {act} from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import {ALL_PANE_DEFINITIONS} from '../../schema/panes.js';
import {BrowserDataExportCard, BrowserDataImportCard} from '../domain_panes.js';
import {DataExportKind} from '../../maho_settings.mojom-webui.js';

Object.defineProperty(globalThis, 'IS_REACT_ACT_ENVIRONMENT', {
  configurable: true,
  value: true,
});

describe('Maho Settings - browser data import', () => {
  let container: HTMLDivElement;
  let root: Root;

  beforeEach(() => {
    container = document.createElement('div');
    document.body.appendChild(container);
    root = createRoot(container);
  });

  afterEach(() => {
    act(() => {
      root.unmount();
    });
    container.remove();
  });

  it('declares an import pane in the productivity domain', () => {
    const importPane =
        ALL_PANE_DEFINITIONS.find(pane => pane.key === 'import');
    expect(importPane).toBeDefined();
    expect(importPane?.domain).toBe('productivity');
    expect(importPane?.navTitle).toBe('Import data');
    expect(importPane?.externalAction).toBeUndefined();
  });

  it('exposes no bookmark pane and no bookmark external action', () => {
    expect(ALL_PANE_DEFINITIONS.some(pane => pane.key === 'bookmarks'))
        .toBe(false);
    expect(ALL_PANE_DEFINITIONS.some(
               pane => pane.contentKind === ('bookmarks' as never) ||
                   pane.externalAction === ('openBookmarks' as never)))
        .toBe(false);
    const paneText = ALL_PANE_DEFINITIONS
                         .map(pane => `${pane.navTitle} ${pane.title}`)
                         .join(' ')
                         .toLowerCase();
    expect(paneText).not.toContain('bookmark');
  });

  it('renders the import action and calls openMigrationDialog()', async () => {
    const openMigrationDialog = vi.fn().mockResolvedValue(undefined);
    const handler = {openMigrationDialog} as never;

    await act(async () => {
      root.render(<BrowserDataImportCard handler={handler} />);
    });

    const importButton =
        Array.from(container.querySelectorAll('button'))
            .find(
                button => /Import Browser Data|Import from another browser/i
                              .test(button.textContent || ''));
    expect(importButton).toBeDefined();

    await act(async () => {
      importButton?.click();
    });

    expect(openMigrationDialog).toHaveBeenCalledTimes(1);
  });

  function findButton(pattern: RegExp): HTMLButtonElement | undefined {
    return Array.from(container.querySelectorAll('button'))
        .find(button => pattern.test(button.textContent || ''));
  }

  it('exports each data kind through exportData() and shows the saved path', async () => {
    const exportData = vi.fn().mockResolvedValue(
        {success: true, path: '/tmp/maho-bookmarks.html', errorCode: ''});
    const handler = {exportData} as never;

    await act(async () => {
      root.render(<BrowserDataExportCard handler={handler} />);
    });

    for (const [pattern, kind] of [
      [/Export Bookmarks/, DataExportKind.kBookmarks],
      [/Export History/, DataExportKind.kHistory],
      [/Export Conversations/, DataExportKind.kConversations],
      [/Export Spaces/, DataExportKind.kSpaces],
      [/Export Backup/, DataExportKind.kFullBackup],
    ] as const) {
      await act(async () => {
        findButton(pattern)?.click();
      });
      expect(exportData).toHaveBeenLastCalledWith(kind, null);
    }
    expect(container.textContent).toContain('Saved to /tmp/maho-bookmarks.html');
  });

  it('surfaces vault_locked for password export without asking for a passphrase', async () => {
    const exportData = vi.fn().mockResolvedValue(
        {success: false, path: '', errorCode: 'vault_locked'});
    const handler = {exportData} as never;

    await act(async () => {
      root.render(<BrowserDataExportCard handler={handler} />);
    });
    await act(async () => {
      findButton(/Export Passwords/)?.click();
    });

    expect(exportData).toHaveBeenCalledWith(DataExportKind.kPasswords, null);
    expect(container.textContent).toContain('Unlock your passwords vault first');
    expect(container.querySelector('input[type="password"]')).toBeNull();
  });

  it('asks for the vault passphrase when OS auth is unavailable and retries with it', async () => {
    const exportData = vi.fn()
        .mockResolvedValueOnce(
            {success: false, path: '', errorCode: 'passphrase_required'})
        .mockResolvedValueOnce(
            {success: true, path: '/tmp/maho-passwords.csv', errorCode: ''});
    const handler = {exportData} as never;

    await act(async () => {
      root.render(<BrowserDataExportCard handler={handler} />);
    });
    await act(async () => {
      findButton(/Export Passwords/)?.click();
    });

    const input = container.querySelector<HTMLInputElement>('input[type="password"]');
    expect(input).not.toBeNull();
    await act(async () => {
      const setter = Object.getOwnPropertyDescriptor(
          HTMLInputElement.prototype, 'value')!.set!;
      setter.call(input, 'hunter2');
      input!.dispatchEvent(new Event('input', {bubbles: true}));
    });
    await act(async () => {
      findButton(/^Confirm$/)?.click();
    });

    expect(exportData).toHaveBeenLastCalledWith(DataExportKind.kPasswords, 'hunter2');
    expect(container.textContent).toContain('Saved to /tmp/maho-passwords.csv');
    expect(container.querySelector('input[type="password"]')).toBeNull();
  });

  it('treats a canceled save dialog as no-op', async () => {
    const exportData = vi.fn().mockResolvedValue(
        {success: false, path: '', errorCode: 'canceled'});
    const handler = {exportData} as never;

    await act(async () => {
      root.render(<BrowserDataExportCard handler={handler} />);
    });
    await act(async () => {
      findButton(/Export History/)?.click();
    });
    expect(container.textContent).not.toContain('Export failed');
  });
});
