import {describe, expect, it} from 'vitest';

import {
  applyBehaviorUpdate,
  DEFAULT_BEHAVIOR_PREFS,
  parseBehaviorPrefs,
  parseBehaviorSnapshot,
  SYNC_INTERVAL_OPTIONS,
} from './mail_behavior_prefs.js';

describe('parseBehaviorPrefs', () => {
  it('preserves boolean false and numeric values from canonical JSON', () => {
    const prefs = parseBehaviorPrefs(JSON.stringify({
      block_remote_images: false,
      block_trackers: true,
      sync_interval: 900,
    }));

    expect(prefs?.block_remote_images).toBe(false);
    expect(prefs?.block_trackers).toBe(true);
    expect(prefs?.sync_interval).toBe(900);
  });

  it('coerces legacy string values without making false truthy', () => {
    const prefs = parseBehaviorPrefs(JSON.stringify({
      block_remote_images: 'false',
      desktop_notifications: 'true',
      sync_interval: '300',
    }));

    expect(prefs?.block_remote_images).toBe(false);
    expect(prefs?.desktop_notifications).toBe(true);
    expect(prefs?.sync_interval).toBe(300);
  });

  it('defaults the unread badge on and preserves an explicit disable', () => {
    expect(parseBehaviorPrefs('{}')?.unread_badge_enabled).toBe(true);
    expect(parseBehaviorPrefs(JSON.stringify({
      unread_badge_enabled: false,
    }))?.unread_badge_enabled).toBe(false);
  });

  it('uses the canonical fresh-profile defaults and a selectable sync interval', () => {
    const prefs = parseBehaviorPrefs('{}');
    expect(prefs).toEqual(DEFAULT_BEHAVIOR_PREFS);
    expect(prefs?.sync_interval).toBe(15);
    expect(SYNC_INTERVAL_OPTIONS.map(option => option.value))
        .toContain(prefs?.sync_interval);
  });

  it('migrates missing notification fields to the versioned defaults', () => {
    const snapshot = parseBehaviorSnapshot('{}', 7);
    expect(snapshot).toMatchObject({
      version: 1,
      revision: 7,
      desktop_notifications: true,
      notification_preview: 'sender_subject',
      unread_badge_enabled: true,
    });
  });

  it.each(['sender_subject', 'sender_only', 'generic'] as const)(
      'accepts the %s notification preview mode', (notificationPreview) => {
        expect(parseBehaviorSnapshot(JSON.stringify({
          notification_preview: notificationPreview,
        }), 1)?.notification_preview).toBe(notificationPreview);
      });

  it('preserves explicit false values during migration', () => {
    const snapshot = parseBehaviorSnapshot(JSON.stringify({
      desktop_notifications: false,
      unread_badge_enabled: false,
    }), 4);
    expect(snapshot?.desktop_notifications).toBe(false);
    expect(snapshot?.unread_badge_enabled).toBe(false);
  });

  it('rejects stale revisions without changing the authoritative snapshot', () => {
    const snapshot = parseBehaviorSnapshot('{}', 9)!;
    expect(applyBehaviorUpdate(snapshot, 8, 'desktop_notifications', false))
        .toEqual({status: 'conflict', snapshot});
  });
});
