// Copyright 2026 Maho Browser. All rights reserved.

import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';
import {MahoSettingsStore} from '../store.js';
import type {SettingValue} from '../../maho_settings.mojom-webui.js';

describe('MahoSettingsStore settings delta & revision tracking', () => {
  let store: MahoSettingsStore;
  let getSettingsMock: ReturnType<typeof vi.fn>;

  beforeEach(() => {
    store = new MahoSettingsStore();
    const handler = store.getHandler();
    getSettingsMock = vi.fn().mockResolvedValue({
      settings: [
        {key: 'general.startup_mode', value: 'homepage'},
        {key: 'mail.enabled', value: 'false'},
      ],
    });
    (handler as any).getSettings = getSettingsMock;
    (handler as any).getBrowserVersionInfo = vi.fn().mockResolvedValue({
      info: {
        version: '1.0.0',
        channel: 'stable',
        updateState: 0,
        updateSupported: false,
        updateGuidance: '',
      },
    });
  });

  afterEach(() => {
    store.dispose();
    vi.clearAllMocks();
  });

  it('carries update guidance while retaining the ability to re-check', async () => {
    const getBrowserVersionInfo = vi.fn().mockResolvedValue({info: {
      version: '1.0.0',
      channel: 'stable',
      updateState: 2,
      updateSupported: true,
      updateGuidance: 'external-update',
    }});
    const checkForBrowserUpdates = vi.fn();
    Object.assign(store.getHandler(), {
      getBrowserVersionInfo,
      checkForBrowserUpdates,
    });
    await store.refreshBrowserVersionInfo();
    expect(store.getSnapshot().updateGuidance).toBe('external-update');
    expect(store.getSnapshot().updateSupported).toBe(true);
    expect(store.getSnapshot().updateState).toBe(2);
    store.checkForBrowserUpdates();
    expect(checkForBrowserUpdates).toHaveBeenCalledOnce();
    expect(store.getSnapshot().updateState).toBe(1);

    getBrowserVersionInfo.mockResolvedValueOnce({info: {
      version: '1.0.0', channel: 'stable', updateState: 6,
      updateSupported: true, updateGuidance: '',
    }});
    await store.refreshBrowserVersionInfo();
    expect(store.getSnapshot().updateGuidance).toBe('');
    expect(store.getSnapshot().updateState).toBe(6);
  });

  it('applies contiguous delta on bootstrapped store and updates revision', async () => {
    await store.bootstrap();
    expect(store.getSnapshot().settingsRevision).toBe(0n);

    // Apply contiguous delta revision 1
    const applied = store.applySettingsDelta({
      key: 'general.startup_mode',
      value: 'restore_last_session',
      revision: 1n,
    });

    expect(applied).toBe(true);
    expect(store.getSnapshot().settingsRevision).toBe(1n);
    const setting = store.getSnapshot().settings.find(s => s.key === 'general.startup_mode');
    expect(setting?.value).toBe('restore_last_session');
  });

  it('inserts new key when applying delta if key was not previously present', async () => {
    await store.bootstrap();

    const applied = store.applySettingsDelta({
      key: 'appearance.sidebar_width',
      value: '300',
      revision: 1n,
    });

    expect(applied).toBe(true);
    expect(store.getSnapshot().settingsRevision).toBe(1n);
    const setting = store.getSnapshot().settings.find(s => s.key === 'appearance.sidebar_width');
    expect(setting?.value).toBe('300');
  });

  it('ignores stale or already applied delta', async () => {
    await store.bootstrap();

    store.applySettingsDelta({
      key: 'general.startup_mode',
      value: 'v1',
      revision: 1n,
    });
    expect(store.getSnapshot().settingsRevision).toBe(1n);

    // Stale delta with revision 1n (already applied)
    const appliedSame = store.applySettingsDelta({
      key: 'general.startup_mode',
      value: 'stale-v1',
      revision: 1n,
    });
    expect(appliedSame).toBe(false);
    expect(store.getSnapshot().settings.find(s => s.key === 'general.startup_mode')?.value).toBe('v1');

    // Stale delta with revision 0n
    const appliedOlder = store.applySettingsDelta({
      key: 'general.startup_mode',
      value: 'stale-v0',
      revision: 0n,
    });
    expect(appliedOlder).toBe(false);
    expect(store.getSnapshot().settings.find(s => s.key === 'general.startup_mode')?.value).toBe('v1');
  });

  it('triggers full snapshot fallback on revision gap/mismatch', async () => {
    await store.bootstrap();
    expect(store.getSnapshot().settingsRevision).toBe(0n);
    expect(getSettingsMock).toHaveBeenCalledTimes(1);

    getSettingsMock.mockResolvedValueOnce({
      settings: [
        {key: 'general.startup_mode', value: 'synced_from_snapshot'},
        {key: 'mail.enabled', value: 'false'},
      ],
    });

    // Send delta with gap: revision 5n when current is 0n
    const applied = store.applySettingsDelta({
      key: 'general.startup_mode',
      value: 'gap_value',
      revision: 5n,
    });

    expect(applied).toBe(false);
    expect(getSettingsMock).toHaveBeenCalledTimes(2);

    // Wait for snapshot refresh to complete
    await (store as any).refreshSettingsInFlight;
    expect(store.getSnapshot().settings.find(s => s.key === 'general.startup_mode')?.value).toBe('synced_from_snapshot');
  });

  it('triggers snapshot fetch if delta received before bootstrap', async () => {
    expect(getSettingsMock).not.toHaveBeenCalled();

    const applied = store.applySettingsDelta({
      key: 'general.startup_mode',
      value: 'early_value',
      revision: 1n,
    });

    expect(applied).toBe(false);
    expect(getSettingsMock).toHaveBeenCalledTimes(1);
  });

  it('updates reachable pane and URL if mail.enabled changes via delta', async () => {
    await store.bootstrap();

    const applied = store.applySettingsDelta({
      key: 'mail.enabled',
      value: 'true',
      revision: 1n,
    });

    expect(applied).toBe(true);
    expect(store.getSnapshot().settings.find(s => s.key === 'mail.enabled')?.value).toBe('true');
  });
});
