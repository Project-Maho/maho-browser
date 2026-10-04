// Copyright 2026 Maho Browser. All rights reserved.

import React, {act} from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import type {SettingValue} from '../../mojo.js';
import {MailBehaviorUpdateStatus, MailNotificationPermission} from '../../mojo.js';
import type {PaneDefinition} from '../../models.js';
import {
  ALL_PANE_DEFINITIONS,
  LIVE_PANE_SECTION_MANIFEST,
  panesForSettings,
  resolveReachablePaneKey,
} from '../../schema/panes.js';
import {DEFAULT_BEHAVIOR_PREFS, type BehaviorPrefs} from '../mail_behavior_prefs.js';
import {MAIL_APP_URL, MailPane} from '../mail_overview.js';

Object.defineProperty(globalThis, 'IS_REACT_ACT_ENVIRONMENT', {
  configurable: true,
  value: true,
});

const MAIL_PANE = ALL_PANE_DEFINITIONS.find(pane => pane.key === 'mail')!;

class TestRoute {
  private nextId = 1;
  private readonly listeners = new Map<number, () => void>();

  addListener(listener: () => void): number {
    const id = this.nextId++;
    this.listeners.set(id, listener);
    return id;
  }

  remove(id: number): void {
    this.listeners.delete(id);
  }

  emit(): void {
    for (const listener of [...this.listeners.values()]) listener();
  }
}

function behaviorSnapshot(prefs: Partial<BehaviorPrefs> = {}) {
  const merged: BehaviorPrefs = {...DEFAULT_BEHAVIOR_PREFS, revision: 4, ...prefs};
  return {
    version: 1,
    revision: BigInt(merged.revision),
    valueJson: JSON.stringify(merged),
    notificationPermission: MailNotificationPermission.kGranted,
  };
}

function createHarness() {
  const behaviorRoute = new TestRoute();
  const handler = {
    mailGetBehaviorPrefs: vi.fn().mockResolvedValue({
      ok: true,
      snapshot: behaviorSnapshot(),
    }),
    mailSetBehaviorPref: vi.fn().mockResolvedValue({
      result: {
        status: MailBehaviorUpdateStatus.kApplied,
        snapshot: behaviorSnapshot({revision: 5}),
      },
    }),
    mailRequestNotificationPermission: vi.fn().mockResolvedValue({
      permission: MailNotificationPermission.kGranted,
    }),
  };
  const commitSettingValue = vi.fn().mockResolvedValue(true);
  const store = {
    getHandler: () => handler,
    getCallbackRouter: () => ({
      onMailBehaviorChanged: behaviorRoute,
      removeListener: (id: number) => behaviorRoute.remove(id),
    }),
    commitSettingValue,
  } as never;
  return {behaviorRoute, commitSettingValue, handler, store};
}

function settings(overrides: Record<string, string> = {}): SettingValue[] {
  const values: Record<string, string> = {
    'mail.enabled': 'true',
    'ai.mail_read_allowed': 'false',
    ...overrides,
  };
  return Object.entries(values).map(([key, value]) => ({key, value} as SettingValue));
}

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

function renderPane(
    harness: ReturnType<typeof createHarness>,
    settingValues: SettingValue[] = settings(),
    pane: PaneDefinition = MAIL_PANE): void {
  act(() => {
    root.render(
        <MailPane pane={pane} settings={settingValues} store={harness.store} />);
  });
}

async function flush(): Promise<void> {
  await act(async () => {
    await Promise.resolve();
    await Promise.resolve();
  });
}

function switchFor(label: string): HTMLButtonElement {
  const control = container.querySelector<HTMLButtonElement>(
      `[role="switch"][aria-label="${label}"]`);
  if (!control) throw new Error(`missing switch: ${label}`);
  return control;
}

function selectTrigger(label: string): HTMLButtonElement {
  const control = container.querySelector<HTMLButtonElement>(
      `[aria-label="${label}"]`);
  if (!control) throw new Error(`missing select: ${label}`);
  return control;
}

function buttonWithText(text: string): HTMLButtonElement {
  const candidate = [...container.querySelectorAll('button')].find(
      element => element.textContent?.trim().includes(text));
  if (!candidate) throw new Error(`missing button: ${text}`);
  return candidate;
}

async function click(element: HTMLElement): Promise<void> {
  await act(async () => {
    element.dispatchEvent(new MouseEvent('click', {bubbles: true}));
    await Promise.resolve();
  });
}

describe('Mail as a top-level settings pane', () => {
  it('is a first-class nav entry that no longer hides behind Features', () => {
    const disabledKeys = panesForSettings(false).map(pane => pane.key);

    expect(disabledKeys).toContain('mail');
    expect(disabledKeys).not.toContain('features');
    expect(MAIL_PANE.navTitle).toBe('Mail');
    expect(MAIL_PANE.contentKind).toBe('mail-overview');
    expect(MAIL_PANE.groupKeys).toBeUndefined();
    expect(LIVE_PANE_SECTION_MANIFEST.features).toBeUndefined();
  });

  it('keeps legacy ?pane=features deep links working', () => {
    expect(resolveReachablePaneKey('features', false)).toBe('mail');
    expect(resolveReachablePaneKey('features', true)).toBe('mail');
  });
});

describe('Mail pane contents', () => {
  it('commits the Mail enable toggle to mail.enabled', async () => {
    const harness = createHarness();
    renderPane(harness, settings({'mail.enabled': 'false'}));
    await flush();

    const control = switchFor('Enable Maho Mail (Beta)');
    expect(control.getAttribute('aria-checked')).toBe('false');

    await click(control);
    expect(harness.commitSettingValue).toHaveBeenCalledWith('mail.enabled', 'true');
  });

  it('commits the AI read permission to ai.mail_read_allowed', async () => {
    const harness = createHarness();
    renderPane(harness);
    await flush();

    const control = switchFor('Allow AI to read Mail');
    expect(control.getAttribute('aria-checked')).toBe('false');

    await click(control);
    expect(harness.commitSettingValue).toHaveBeenCalledWith(
        'ai.mail_read_allowed', 'true');
  });

  it('restores the AI permission when the browser rejects the save', async () => {
    const harness = createHarness();
    harness.commitSettingValue.mockResolvedValueOnce(false);
    renderPane(harness);
    await flush();

    await click(switchFor('Allow AI to read Mail'));
    await flush();

    expect(switchFor('Allow AI to read Mail').getAttribute('aria-checked'))
        .toBe('false');
    expect(container.querySelector('[role="alert"]')?.textContent)
        .toContain('Could not save Mail access');
  });

  it('offers every notification, badge, and sync control on one pane', async () => {
    const harness = createHarness();
    renderPane(harness);
    await flush();

    expect(switchFor('Desktop notifications').getAttribute('data-state'))
        .toBe('checked');
    expect(switchFor('Unread badge').getAttribute('data-state')).toBe('checked');
    expect(selectTrigger('Notification preview').textContent)
        .toContain('Sender and subject');
    expect(selectTrigger('Sync interval').textContent)
        .toContain('Every 15 minutes');
  });

  it('persists the unread badge toggle through the behavior pref API', async () => {
    const harness = createHarness();
    renderPane(harness);
    await flush();

    await click(switchFor('Unread badge'));
    await flush();

    expect(harness.handler.mailSetBehaviorPref).toHaveBeenCalledWith(
        4n, 'unread_badge_enabled', 'false');
  });

  it('rolls back a rejected notification change and reports the failure', async () => {
    const harness = createHarness();
    harness.handler.mailSetBehaviorPref.mockResolvedValueOnce({
      result: {
        status: MailBehaviorUpdateStatus.kRejected,
        snapshot: behaviorSnapshot({revision: 4}),
      },
    });
    renderPane(harness);
    await flush();

    await click(switchFor('Desktop notifications'));
    await flush();

    expect(switchFor('Desktop notifications').getAttribute('data-state'))
        .toBe('checked');
    expect(container.textContent)
        .toContain('Failed to update mail delivery preference');
  });

  it('surfaces a conflicting snapshot from another tab', async () => {
    const harness = createHarness();
    harness.handler.mailSetBehaviorPref.mockResolvedValueOnce({
      result: {
        status: MailBehaviorUpdateStatus.kConflict,
        snapshot: behaviorSnapshot({revision: 9, unread_badge_enabled: true}),
      },
    });
    renderPane(harness);
    await flush();

    await click(switchFor('Unread badge'));
    await flush();

    expect(container.querySelector('[data-mail-settings-state]')?.getAttribute(
        'data-mail-settings-state')).toBe('conflict');
    expect(container.querySelector('[role="alert"]')?.textContent)
        .toContain('changed in another tab');
    expect(switchFor('Unread badge').getAttribute('data-state')).toBe('checked');
  });

  it('recovers from an unavailable mail helper on the change event', async () => {
    const harness = createHarness();
    harness.handler.mailGetBehaviorPrefs.mockResolvedValueOnce(
        {ok: false, snapshot: null});
    renderPane(harness);
    await flush();

    expect(container.querySelector('[data-mail-settings-state]')?.getAttribute(
        'data-mail-settings-state')).toBe('unavailable');

    await act(async () => {
      harness.behaviorRoute.emit();
      await Promise.resolve();
      await Promise.resolve();
    });

    expect(container.querySelector('[data-mail-settings-state]')?.getAttribute(
        'data-mail-settings-state')).toBe('ready');
    expect(switchFor('Unread badge')).toBeTruthy();
  });

  it('links out to the Mail app for account management', async () => {
    const harness = createHarness();
    const open = vi.fn();
    vi.stubGlobal('open', open);
    renderPane(harness);
    await flush();

    await click(buttonWithText('Open Maho Mail'));
    expect(open).toHaveBeenCalledWith(
        MAIL_APP_URL, '_blank', 'noopener,noreferrer');
    vi.unstubAllGlobals();
  });

  it('hides delivery controls and the app link while Mail is disabled', async () => {
    const harness = createHarness();
    renderPane(harness, settings({'mail.enabled': 'false'}));
    await flush();

    expect(harness.handler.mailGetBehaviorPrefs).not.toHaveBeenCalled();
    expect(container.querySelector('[role="switch"][aria-label="Unread badge"]'))
        .toBeNull();
    expect(buttonWithText('Open Maho Mail').disabled).toBe(true);
    expect(container.textContent).toContain('Enable Maho Mail to choose');
  });
});
