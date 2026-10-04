// Copyright 2026 Maho Browser. All rights reserved.

import React, {act} from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import {ALL_PANE_DEFINITIONS} from '../../schema/panes.js';
import {BrowserDataImportCard} from '../domain_panes.js';

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
});
