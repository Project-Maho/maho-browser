// Copyright 2026 Maho Browser. All rights reserved.

import React, {act} from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import type {PaneDefinition} from '../../models.js';
import {
  DEFAULT_CALENDAR_PREFS,
  MailCalendarPane,
  normalizeCalendarPrefs,
} from '../mail_calendar.js';

Object.defineProperty(globalThis, 'IS_REACT_ACT_ENVIRONMENT', {
  configurable: true,
  value: true,
});

const PANE: PaneDefinition = {
  key: 'mail-calendar',
  scope: 'account',
  selectedProfileSupport: 'not-applicable',
  symbol: 'mail',
  navTitle: 'Calendar',
  title: 'Calendar',
  kind: 'live',
  domain: 'mail',
};

function createStore(prefsResultJson: string) {
  const handler = {
    mailGetCalendarPrefs:
        vi.fn().mockResolvedValue({ok: true, resultJson: prefsResultJson}),
    mailListCalendarCategories:
        vi.fn().mockResolvedValue({ok: true, resultJson: '[]'}),
    mailListAccountCalendars:
        vi.fn().mockResolvedValue({ok: true, resultJson: '[]'}),
    mailSetCalendarPrefs: vi.fn().mockResolvedValue({ok: true}),
    mailSetCalendarVisibility: vi.fn().mockResolvedValue({ok: true}),
  };
  const router = {
    onMailCalendarChanged: {addListener: () => 1},
    removeListener: () => {},
  };
  return {
    handler,
    store: {
      getHandler: () => handler,
      getCallbackRouter: () => router,
    } as never,
  };
}

describe('normalizeCalendarPrefs', () => {
  it('falls back to defaults for an unset backend row', () => {
    // mail-core answers a missing app-settings row with JSON `null`; parsing it
    // straight into state used to make the pane read `hide_weekends` off null.
    expect(normalizeCalendarPrefs('null')).toEqual(DEFAULT_CALENDAR_PREFS);
  });

  it('falls back to defaults for empty, non-object, and invalid payloads', () => {
    expect(normalizeCalendarPrefs('')).toEqual(DEFAULT_CALENDAR_PREFS);
    expect(normalizeCalendarPrefs('"{}"')).toEqual(DEFAULT_CALENDAR_PREFS);
    expect(normalizeCalendarPrefs('[]')).toEqual(DEFAULT_CALENDAR_PREFS);
    expect(normalizeCalendarPrefs('not json')).toEqual(DEFAULT_CALENDAR_PREFS);
  });

  it('keeps well-typed fields and defaults the rest', () => {
    expect(normalizeCalendarPrefs(JSON.stringify({
      hide_weekends: true,
      week_start: 1,
      working_hours_start: 42,
    }))).toEqual({
      ...DEFAULT_CALENDAR_PREFS,
      hide_weekends: true,
      week_start: 1,
    });
  });
});

describe('MailCalendarPane with a null preferences payload', () => {
  let container: HTMLDivElement;
  let root: Root;

  beforeEach(() => {
    container = document.createElement('div');
    document.body.appendChild(container);
    root = createRoot(container);
  });

  afterEach(() => {
    act(() => root.unmount());
    container.remove();
    vi.restoreAllMocks();
  });

  it('renders defaults instead of throwing during render', async () => {
    const {store} = createStore('null');
    await act(async () => {
      root.render(<MailCalendarPane pane={PANE} store={store} />);
    });

    const pane = container.querySelector('[data-mail-settings-state]');
    expect(pane?.getAttribute('data-mail-settings-state')).toBe('ready');
    expect(container.textContent).toContain('Hide Weekends');
    const weekendSwitch = container.querySelector('[role="switch"]');
    expect(weekendSwitch?.getAttribute('data-state')).toBe('unchecked');
  });
});
