// Copyright 2026 Maho Browser. All rights reserved.

import React, {act} from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import type {PaneDefinition} from '../../models.js';
import {MailBehaviorUpdateStatus, MailNotificationPermission} from '../../mojo.js';
import {MailAccountsPane} from '../mail_accounts.js';
import {MailBehaviorPane} from '../mail_behavior.js';
import {DEFAULT_BEHAVIOR_PREFS, type BehaviorPrefs} from '../mail_behavior_prefs.js';
import {MailCalendarPane} from '../mail_calendar.js';
import {MailRulesPane} from '../mail_rules.js';
import {MailSecurityPane} from '../mail_security.js';
import {MailSignaturesPane} from '../mail_signatures.js';

Object.defineProperty(globalThis, 'IS_REACT_ACT_ENVIRONMENT', {
  configurable: true,
  value: true,
});

interface Deferred<T> {
  promise: Promise<T>;
  resolve(value: T): void;
  reject(reason: unknown): void;
}

function deferred<T>(): Deferred<T> {
  let resolve!: (value: T) => void;
  let reject!: (reason: unknown) => void;
  const promise = new Promise<T>((resolvePromise, rejectPromise) => {
    resolve = resolvePromise;
    reject = rejectPromise;
  });
  return {promise, resolve, reject};
}

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

function behaviorPrefs(overrides: Partial<BehaviorPrefs> = {}): BehaviorPrefs {
  return {
    ...DEFAULT_BEHAVIOR_PREFS,
    revision: 7,
    ...overrides,
  };
}

function behaviorSnapshot(prefs: BehaviorPrefs) {
  return {
    version: 1,
    revision: BigInt(prefs.revision),
    valueJson: JSON.stringify(prefs),
    notificationPermission: MailNotificationPermission.kGranted,
  };
}

function createHarness() {
  const routes = {
    onMailAccountsChanged: new TestRoute(),
    onMailBehaviorChanged: new TestRoute(),
    onMailCalendarChanged: new TestRoute(),
    onMailRulesChanged: new TestRoute(),
    onMailSecurityChanged: new TestRoute(),
    onMailSignaturesChanged: new TestRoute(),
  };
  const handler = {
    mailListAccounts: vi.fn().mockResolvedValue({ok: true, resultJson: '[]'}),
    mailDeleteAccount: vi.fn().mockResolvedValue({ok: true, resultJson: ''}),
    mailReconnectAccount: vi.fn().mockResolvedValue({ok: true, resultJson: ''}),
    mailBeginOAuth: vi.fn().mockResolvedValue({ok: false, resultJson: ''}),
    mailOAuthCancel: vi.fn().mockResolvedValue(undefined),
    mailAddAccount: vi.fn().mockResolvedValue({ok: true, resultJson: ''}),
    mailGetBehaviorPrefs: vi.fn().mockResolvedValue({
      ok: true,
      snapshot: behaviorSnapshot(behaviorPrefs()),
    }),
    mailSetBehaviorPref: vi.fn().mockResolvedValue({
      result: {
        status: MailBehaviorUpdateStatus.kApplied,
        snapshot: behaviorSnapshot(behaviorPrefs({revision: 8})),
      },
    }),
    mailRequestNotificationPermission: vi.fn().mockResolvedValue({
      permission: MailNotificationPermission.kGranted,
    }),
    mailGetCalendarPrefs: vi.fn().mockResolvedValue({
      ok: true,
      resultJson: JSON.stringify({
        hide_weekends: false,
        week_start: 1,
        working_hours_start: '09:00',
        working_hours_end: '17:00',
        default_reminder_minutes: 15,
      }),
    }),
    mailListCalendarCategories: vi.fn().mockResolvedValue({ok: true, resultJson: '[]'}),
    mailListAccountCalendars: vi.fn().mockResolvedValue({ok: true, resultJson: '[]'}),
    mailSetCalendarPrefs: vi.fn().mockResolvedValue({ok: true}),
    mailSetCalendarVisibility: vi.fn().mockResolvedValue({ok: true}),
    mailListRules: vi.fn().mockResolvedValue({ok: true, resultJson: '[]'}),
    mailUpsertRule: vi.fn().mockResolvedValue({ok: true, error: ''}),
    mailDeleteRule: vi.fn().mockResolvedValue({ok: true}),
    mailReorderRules: vi.fn().mockResolvedValue({ok: true}),
    mailListSignatures: vi.fn().mockResolvedValue({ok: true, resultJson: '[]'}),
    mailUpsertSignature: vi.fn().mockResolvedValue({ok: true, error: ''}),
    mailDeleteSignature: vi.fn().mockResolvedValue({ok: true}),
    mailListPgpKeys: vi.fn().mockResolvedValue({ok: true, resultJson: '[]'}),
    mailListSmimeIdentities: vi.fn().mockResolvedValue({ok: true, resultJson: '[]'}),
    mailGeneratePgpKey: vi.fn().mockResolvedValue({ok: true, error: ''}),
    mailImportPgpKey: vi.fn().mockResolvedValue({ok: true, error: ''}),
    mailImportSmimeIdentity: vi.fn().mockResolvedValue({ok: true, error: ''}),
    mailDeletePgpKey: vi.fn().mockResolvedValue({ok: true}),
    mailDeleteSmimeIdentity: vi.fn().mockResolvedValue({ok: true}),
  };
  const router = {
    ...routes,
    removeListener(id: number) {
      for (const route of Object.values(routes)) route.remove(id);
    },
  };
  return {
    handler,
    routes,
    store: {
      getHandler: () => handler,
      getCallbackRouter: () => router,
    } as never,
  };
}

type PaneCase = {
  key: string;
  title: string;
  component: React.ComponentType<{pane: PaneDefinition; store: never}>;
  loadMethod: keyof ReturnType<typeof createHarness>['handler'];
  route: keyof ReturnType<typeof createHarness>['routes'];
  readyText: string;
  emptyText: string;
  configureReady(handler: ReturnType<typeof createHarness>['handler']): void;
};

const ACCOUNT = JSON.stringify([{id: 'account-1', email: 'mail@example.test', display_name: 'Work', provider: 'gmail', status: 'connected'}]);
const RULE = JSON.stringify([{id: 'rule-1', account_id: '', name: 'Archive newsletters', conditions_json: '{}', actions_json: '{}', priority: 1, enabled: true}]);
const SIGNATURE = JSON.stringify([{id: 'sig-1', account_id: 'account-1', name: 'Work signature', content_html: 'Regards', is_default: true}]);

const CASES: PaneCase[] = [
  {
    key: 'mail-accounts', title: 'Mail Accounts', component: MailAccountsPane,
    loadMethod: 'mailListAccounts', route: 'onMailAccountsChanged',
    readyText: 'mail@example.test', emptyText: 'No mail accounts connected yet.',
    configureReady: handler => handler.mailListAccounts.mockResolvedValue({ok: true, resultJson: ACCOUNT}),
  },
  {
    key: 'mail-signatures', title: 'Signatures', component: MailSignaturesPane,
    loadMethod: 'mailListSignatures', route: 'onMailSignaturesChanged',
    readyText: 'Work signature', emptyText: 'No signatures found.',
    configureReady: handler => handler.mailListSignatures.mockResolvedValue({ok: true, resultJson: SIGNATURE}),
  },
  {
    key: 'mail-rules', title: 'Rules', component: MailRulesPane,
    loadMethod: 'mailListRules', route: 'onMailRulesChanged',
    readyText: 'Archive newsletters', emptyText: 'No rules found.',
    configureReady: handler => handler.mailListRules.mockResolvedValue({ok: true, resultJson: RULE}),
  },
  {
    key: 'mail-calendar', title: 'Calendar', component: MailCalendarPane,
    loadMethod: 'mailGetCalendarPrefs', route: 'onMailCalendarChanged',
    readyText: 'Calendar Preferences', emptyText: 'No accounts calendars registered.',
    configureReady: () => undefined,
  },
  {
    key: 'mail-behavior', title: 'Behavior', component: MailBehaviorPane,
    loadMethod: 'mailGetBehaviorPrefs', route: 'onMailBehaviorChanged',
    readyText: 'Mail Composition & Layout', emptyText: '',
    configureReady: () => undefined,
  },
  {
    key: 'mail-security', title: 'Security', component: MailSecurityPane,
    loadMethod: 'mailListAccounts', route: 'onMailAccountsChanged',
    readyText: 'PGP Encryption Keys', emptyText: 'Connect a mail account to manage security keys.',
    configureReady: handler => handler.mailListAccounts.mockResolvedValue({ok: true, resultJson: ACCOUNT}),
  },
];

function paneDefinition(entry: PaneCase): PaneDefinition {
  return {
    key: entry.key,
    scope: 'account',
    selectedProfileSupport: 'not-applicable',
    symbol: 'mail',
    navTitle: entry.title,
    title: entry.title,
    kind: 'live',
    domain: 'mail',
  };
}

let container: HTMLDivElement;
let root: Root;

beforeEach(() => {
  container = document.createElement('div');
  container.id = 'app';
  document.body.appendChild(container);
  root = createRoot(container);
});

afterEach(() => {
  act(() => root.unmount());
  container.remove();
  vi.restoreAllMocks();
});

function renderPane(entry: PaneCase, harness: ReturnType<typeof createHarness>): void {
  const Component = entry.component;
  act(() => root.render(<Component pane={paneDefinition(entry)} store={harness.store} />));
}

async function flush(): Promise<void> {
  await act(async () => {
    await Promise.resolve();
    await Promise.resolve();
  });
}

function state(): string | undefined {
  return container.querySelector<HTMLElement>('[data-mail-settings-state]')?.dataset.mailSettingsState;
}

function button(label: string): HTMLButtonElement {
  const candidate = [...container.querySelectorAll('button')]
      .find(element => element.textContent?.trim().includes(label));
  if (!candidate) throw new Error(`missing button: ${label}`);
  return candidate;
}

function switchFor(label: string): HTMLButtonElement {
  const heading = [...container.querySelectorAll('h4')]
      .find(element => element.textContent?.trim() === label);
  const candidate = heading?.parentElement?.parentElement?.querySelector<HTMLButtonElement>('[role="switch"]');
  if (!candidate) throw new Error(`missing switch: ${label}`);
  return candidate;
}

async function click(element: HTMLElement): Promise<void> {
  await act(async () => {
    element.dispatchEvent(new MouseEvent('click', {bubbles: true}));
    await Promise.resolve();
  });
}

describe.each(CASES)('$title rendered acceptance states', entry => {
  it('renders loading, ready, and account scope copy from the real component', async () => {
    const harness = createHarness();
    entry.configureReady(harness.handler);
    const pending = deferred<never>();
    const load = harness.handler[entry.loadMethod] as ReturnType<typeof vi.fn>;
    const ready = load.getMockImplementation();
    load.mockImplementationOnce(() => pending.promise);

    renderPane(entry, harness);
    expect(state()).toBe('loading');
    expect(container.textContent).toContain('Loading');
    expect(container.textContent).toContain('Applies to all profiles.');

    load.mockImplementation(ready!);
    pending.reject(new Error('superseded by helper restart'));
    await flush();
    expect(state()).toBe('error');

    await click(button('Retry'));
    await flush();
    expect(state()).toBe('ready');
    expect(container.textContent).toContain(entry.readyText);
  });

  it('renders unavailable and recovers on the exact helper callback event', async () => {
    const harness = createHarness();
    const load = harness.handler[entry.loadMethod] as ReturnType<typeof vi.fn>;
    load.mockResolvedValueOnce(entry.key === 'mail-behavior' ?
      {ok: false, snapshot: null} : {ok: false, resultJson: 'mail helper unavailable'});
    entry.configureReady(harness.handler);

    renderPane(entry, harness);
    await flush();
    expect(state()).toBe('unavailable');
    expect(container.textContent?.toLowerCase()).toContain('unavailable');

    await act(async () => {
      harness.routes[entry.route].emit();
      await Promise.resolve();
      await Promise.resolve();
    });
    expect(state()).toBe('ready');
    expect(container.textContent).toContain(entry.readyText);
  });

  if (entry.key === 'mail-behavior') {
    it('classifies a rejected behavior load as unavailable through the shared state contract', async () => {
      const harness = createHarness();
      harness.handler.mailGetBehaviorPrefs.mockResolvedValueOnce({
        ok: false,
        snapshot: null,
      });
      renderPane(entry, harness);
      await flush();

      expect(state()).toBe('unavailable');
      expect(container.textContent).toContain('Mail service unavailable');
    });
  }

  it('renders a rejected/invalid payload as error and offers deterministic retry', async () => {
    const harness = createHarness();
    const load = harness.handler[entry.loadMethod] as ReturnType<typeof vi.fn>;
    if (entry.key === 'mail-behavior') {
      load.mockResolvedValueOnce({ok: true, snapshot: {...behaviorSnapshot(behaviorPrefs()), valueJson: '{'}});
    } else if (entry.key === 'mail-calendar') {
      load.mockRejectedValueOnce(new Error('broken transport'));
    } else {
      load.mockResolvedValueOnce({ok: true, resultJson: '{'});
    }
    renderPane(entry, harness);
    await flush();
    expect(state()).toBe('error');
    expect(button('Retry')).toBeTruthy();
  });

  if (entry.emptyText) {
    it('renders the pane-specific empty state', async () => {
      const harness = createHarness();
      renderPane(entry, harness);
      await flush();
      expect(state()).toBe(entry.key === 'mail-calendar' ? 'ready' : 'empty');
      expect(container.textContent).toContain(entry.emptyText);
    });
  }
});

describe('Mail behavior rendered mutation semantics', () => {
  const entry = CASES.find(candidate => candidate.key === 'mail-behavior')!;

  it('renders the canonical fresh sync interval as a valid option', async () => {
    const harness = createHarness();
    harness.handler.mailGetBehaviorPrefs.mockResolvedValueOnce({
      ok: true,
      snapshot: behaviorSnapshot({...DEFAULT_BEHAVIOR_PREFS, revision: 0}),
    });
    renderPane(entry, harness);
    await flush();

    expect(state()).toBe('ready');
    expect(button('Every 15 minutes')).toBeTruthy();
  });

  it('ignores an older load that completes after a newer snapshot', async () => {
    const harness = createHarness();
    const older = deferred<ReturnType<typeof harness.handler.mailGetBehaviorPrefs>>();
    const newer = deferred<ReturnType<typeof harness.handler.mailGetBehaviorPrefs>>();
    harness.handler.mailGetBehaviorPrefs
        .mockImplementationOnce(() => older.promise)
        .mockImplementationOnce(() => newer.promise);
    renderPane(entry, harness);

    await act(async () => {
      harness.routes.onMailBehaviorChanged.emit();
      await Promise.resolve();
    });
    newer.resolve({
      ok: true,
      snapshot: behaviorSnapshot(behaviorPrefs({revision: 9, block_remote_images: false})),
    });
    await flush();
    expect(state()).toBe('ready');
    expect(switchFor('Block Remote Images').getAttribute('data-state')).toBe('unchecked');

    older.resolve({
      ok: true,
      snapshot: behaviorSnapshot(behaviorPrefs({revision: 7, block_remote_images: true})),
    });
    await flush();
    expect(state()).toBe('ready');
    expect(switchFor('Block Remote Images').getAttribute('data-state')).toBe('unchecked');
  });

  it('does not let a stale load overwrite an optimistic edit', async () => {
    const harness = createHarness();
    const staleLoad = deferred<ReturnType<typeof harness.handler.mailGetBehaviorPrefs>>();
    const save = deferred<ReturnType<typeof harness.handler.mailSetBehaviorPref>>();
    harness.handler.mailGetBehaviorPrefs
        .mockResolvedValueOnce({
          ok: true,
          snapshot: behaviorSnapshot(behaviorPrefs({revision: 8})),
        })
        .mockImplementationOnce(() => staleLoad.promise);
    harness.handler.mailSetBehaviorPref.mockImplementationOnce(() => save.promise);
    renderPane(entry, harness);
    await flush();

    await act(async () => {
      harness.routes.onMailBehaviorChanged.emit();
      await Promise.resolve();
    });
    await click(switchFor('Block Remote Images'));
    expect(switchFor('Block Remote Images').getAttribute('data-state')).toBe('unchecked');

    staleLoad.resolve({
      ok: true,
      snapshot: behaviorSnapshot(behaviorPrefs({revision: 7, block_remote_images: true})),
    });
    await flush();
    expect(switchFor('Block Remote Images').getAttribute('data-state')).toBe('unchecked');

    save.resolve({
      result: {
        status: MailBehaviorUpdateStatus.kApplied,
        snapshot: behaviorSnapshot(behaviorPrefs({revision: 9, block_remote_images: false})),
      },
    });
    await flush();
    expect(switchFor('Block Remote Images').getAttribute('data-state')).toBe('unchecked');
  });

  it('renders conflict with the authoritative snapshot', async () => {
    const harness = createHarness();
    const conflictPrefs = behaviorPrefs({revision: 9, block_remote_images: true});
    harness.handler.mailSetBehaviorPref.mockResolvedValueOnce({
      result: {
        status: MailBehaviorUpdateStatus.kConflict,
        snapshot: behaviorSnapshot(conflictPrefs),
      },
    });
    renderPane(entry, harness);
    await flush();

    await click(switchFor('Block Remote Images'));
    await flush();

    expect(state()).toBe('conflict');
    const conflict = container.querySelector<HTMLElement>('[role="alert"]');
    expect(conflict?.textContent).toContain('changed in another tab');
    expect(conflict?.textContent).toContain('latest values');
    expect(switchFor('Block Remote Images').getAttribute('data-state')).toBe('checked');
  });

  it('rolls back only the failed key while a rapid independent edit remains optimistic', async () => {
    const harness = createHarness();
    const first = deferred<never>();
    const second = deferred<{result: {status: MailBehaviorUpdateStatus; snapshot: ReturnType<typeof behaviorSnapshot>}}>();
    harness.handler.mailSetBehaviorPref
        .mockImplementationOnce(() => first.promise)
        .mockImplementationOnce(() => second.promise);
    renderPane(entry, harness);
    await flush();

    const images = switchFor('Block Remote Images');
    const trackers = switchFor('Block Email Trackers');
    await click(images);
    expect(images.getAttribute('data-state')).toBe('unchecked');
    expect(trackers.disabled).toBe(false);
    await click(trackers);
    expect(trackers.getAttribute('data-state')).toBe('unchecked');
    expect(harness.handler.mailSetBehaviorPref).toHaveBeenCalledTimes(1);

    await act(async () => {
      first.reject(new Error('save failed'));
      await Promise.resolve();
      await Promise.resolve();
    });
    expect(harness.handler.mailSetBehaviorPref).toHaveBeenCalledTimes(2);
    expect(switchFor('Block Remote Images').getAttribute('data-state')).toBe('checked');
    expect(switchFor('Block Email Trackers').getAttribute('data-state')).toBe('unchecked');

    second.resolve({
      result: {
        status: MailBehaviorUpdateStatus.kApplied,
        snapshot: behaviorSnapshot(behaviorPrefs({revision: 8, block_trackers: false})),
      },
    });
    await flush();
    expect(switchFor('Block Email Trackers').getAttribute('data-state')).toBe('unchecked');
    expect(container.textContent).toContain('Failed to update behavior preference');
  });
});

describe('Mail calendar rendered failed-save rollback', () => {
  const entry = CASES.find(candidate => candidate.key === 'mail-calendar')!;

  it('restores the previous preference when persistence fails', async () => {
    const harness = createHarness();
    harness.handler.mailSetCalendarPrefs.mockResolvedValueOnce({ok: false});
    renderPane(entry, harness);
    await flush();

    const weekends = switchFor('Hide Weekends');
    expect(weekends.getAttribute('data-state')).toBe('unchecked');
    await click(weekends);
    await flush();

    expect(switchFor('Hide Weekends').getAttribute('data-state')).toBe('unchecked');
    expect(container.textContent).toContain('Failed to update calendar preferences');
  });
});
