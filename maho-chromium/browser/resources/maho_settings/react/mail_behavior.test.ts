import {describe, expect, it} from 'vitest';

import {parseBehaviorPrefs} from './mail_behavior_prefs.js';

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
});
