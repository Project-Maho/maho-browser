import React, {act} from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import type {SettingValue} from '../../mojo.js';
import {ALL_PANE_DEFINITIONS} from '../../schema/panes.js';
import {DomainPaneContent} from '../domain_panes.js';
import type {MahoSettingsStore} from '../store.js';

import {SETTING_METADATA} from '../../schema/setting_schema.js';

describe('AI permission metadata', () => {
  it.each([
    ['ai.permission_tier', 'select'],
    ['ai.final_confirm', 'toggle'],
    ['ai.proactive_mode', 'toggle'],
    ['ai.approval_policy', 'select'],
  ])('exposes %s as a profile setting', (key, control) => {
    expect(SETTING_METADATA[key]).toMatchObject({
      control,
      scope: 'profile',
      selectedProfile: false,
    });
  });

  it.each([
    ['ai.permission_tier', ['read_only', 'guard', 'full_access']],
    ['ai.approval_policy', ['prompt', 'allow', 'deny']],
  ] as const)('limits %s choices to its supported values', (key, values) => {
    expect(SETTING_METADATA[key]?.options?.map(option => option.value)).toEqual(values);
  });
});

Object.defineProperty(globalThis, 'IS_REACT_ACT_ENVIRONMENT', {
  configurable: true,
  value: true,
});

let container: HTMLDivElement;
let root: Root;

beforeEach(() => {
  container = document.createElement('div');
  document.body.appendChild(container);
  root = createRoot(container);
  Element.prototype.scrollIntoView = vi.fn();
  Element.prototype.hasPointerCapture = vi.fn().mockReturnValue(false);
});

afterEach(() => {
  act(() => root.unmount());
  container.remove();
  vi.restoreAllMocks();
});

async function renderPermissions() {
  let snapshot = {
    settings: Object.entries({
      'ai.permission_tier': 'guard',
      'ai.final_confirm': 'true',
      'ai.proactive_mode': 'false',
      'ai.approval_policy': 'prompt',
    }).map(([key, value]) => ({key, value} as SettingValue)),
  };
  const listeners = new Set<() => void>();
  const handler = {
    getAISettings: vi.fn().mockResolvedValue({settings: {
      provider: '', mailReadAllowed: false, sessionPersistenceEnabled: true,
      approvalPolicy: 'deny',
    }}),
  };
  const router = {
    providerModelsRefreshed: {addListener: () => 1},
    removeListener: vi.fn(),
  };
  const commitSettingValue = vi.fn(async (key: string, value: string) => {
    snapshot = {...snapshot, settings: snapshot.settings.map(setting =>
      setting.key === key ? {...setting, value} : setting)};
    for (const listener of listeners) listener();
    return true;
  });
  const store = {
    getSnapshot: () => snapshot,
    subscribe: (listener: () => void) => {
      listeners.add(listener);
      return () => { listeners.delete(listener); };
    },
    getHandler: () => handler,
    getCallbackRouter: () => router,
    commitSettingValue,
  } as unknown as MahoSettingsStore;
  await act(async () => {
    root.render(<DomainPaneContent
      pane={ALL_PANE_DEFINITIONS.find(pane => pane.contentKind === 'ai')!}
      settings={snapshot.settings} store={store} />);
  });
  return {commitSettingValue};
}

function controlFor(key: string): HTMLButtonElement {
  const control = container.querySelector<HTMLButtonElement>(
      `button[aria-label="${SETTING_METADATA[key].label}"]`);
  if (!control) throw new Error(`Missing control for ${key}`);
  return control;
}

describe('AI pane permission controls', () => {
  it.each([
    ['ai.final_confirm', 'true', 'false'],
    ['ai.proactive_mode', 'false', 'true'],
  ])('reads and writes %s through the setting store', async (key, initial, next) => {
    const harness = await renderPermissions();
    expect(controlFor(key).getAttribute('aria-checked')).toBe(initial);
    await act(async () => { controlFor(key).click(); });
    expect(harness.commitSettingValue).toHaveBeenCalledWith(key, next);
    expect(controlFor(key).getAttribute('aria-checked')).toBe(next);
  });

  it.each([
    ['ai.permission_tier', 'guard', 'full_access'],
    ['ai.approval_policy', 'prompt', 'deny'],
  ])('offers only supported choices and saves %s through the store', async (key, initial, next) => {
    const harness = await renderPermissions();
    const options = SETTING_METADATA[key].options!;
    expect(controlFor(key).textContent).toBe(options.find(option => option.value === initial)!.label);
    await act(async () => {
      controlFor(key).dispatchEvent(new KeyboardEvent('keydown', {key: 'ArrowDown', bubbles: true}));
    });
    const rendered = [...document.querySelectorAll<HTMLElement>('[role="option"]')];
    expect(rendered.map(option => option.textContent)).toEqual(options.map(option => option.label));
    await act(async () => { rendered[options.findIndex(option => option.value === next)].click(); });
    expect(harness.commitSettingValue).toHaveBeenCalledWith(key, next);
    expect(controlFor(key).textContent).toBe(options.find(option => option.value === next)!.label);
  });
});
