import React, {act} from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import {ALL_PANE_DEFINITIONS, panesForSettings} from '../../schema/panes.js';
import {GeneralPane, BookmarksPane} from '../domain_panes.js';

Object.defineProperty(globalThis, 'IS_REACT_ACT_ENVIRONMENT', {
  configurable: true,
  value: true,
});

describe('Maho Settings - Bookmarks & Browser Data Import', () => {
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

  it('declares bookmarks in PRODUCTIVITY_PANES with externalAction openBookmarks', () => {
    const bookmarksPane = ALL_PANE_DEFINITIONS.find(pane => pane.key === 'bookmarks');
    expect(bookmarksPane).toBeDefined();
    expect(bookmarksPane?.domain).toBe('productivity');
    expect(bookmarksPane?.externalAction).toBe('openBookmarks');
    expect(bookmarksPane?.navTitle).toBe('Bookmarks');
  });

  it('renders BookmarksPane with action button that calls handler.openBookmarksPage()', async () => {
    const openBookmarksPage = vi.fn().mockResolvedValue(undefined);
    const handler = {
      openBookmarksPage,
    } as any;

    await act(async () => {
      root.render(<BookmarksPane handler={handler} />);
    });

    const button = Array.from(container.querySelectorAll('button')).find(
      btn => /Bookmark Manager|Open Bookmarks/i.test(btn.textContent || '')
    );
    expect(button).toBeDefined();

    await act(async () => {
      button?.click();
    });

    expect(openBookmarksPage).toHaveBeenCalledTimes(1);
  });

  it('renders Import Browser Data action in GeneralPane and calls handler.openMigrationDialog()', async () => {
    const openMigrationDialog = vi.fn().mockResolvedValue(undefined);
    const handler = {
      openMigrationDialog,
    } as any;
    const store = {
      getHandler: () => handler,
      getSettings: () => ({}),
      getSnapshot: () => ({}),
      subscribe: () => () => {},
    } as any;

    await act(async () => {
      root.render(<GeneralPane store={store} handler={handler} />);
    });

    const importButton = Array.from(container.querySelectorAll('button')).find(
      btn => /Import Browser Data|Import from another browser/i.test(btn.textContent || '')
    );
    expect(importButton).toBeDefined();

    await act(async () => {
      importButton?.click();
    });

    expect(openMigrationDialog).toHaveBeenCalledTimes(1);
  });
});
