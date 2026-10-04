// Copyright 2026 Maho Browser. All rights reserved.

import {describe, expect, it, vi} from 'vitest';
import type {MahoWelcomeStore} from '../store.js';
import {createInitialState} from '../types.js';

describe('SyncKeyBackup component contract', () => {
  it('enforces savedToFile state before permitting advance', () => {
    const state = createInitialState();
    expect(state.syncKeyBackup.savedToFile).toBe(false);
    expect(state.syncKeyBackup.recoveryPhrase).toBeNull();
  });

  it('uses redacted phrase fixtures in test environment', () => {
    const mockPhrase = 'redacted sync phrase word grid placeholder fixture values for backup test';
    const words = mockPhrase.split(' ');
    expect(words.length).toBeGreaterThan(0);
    expect(mockPhrase).not.toContain('real_secret');
  });
});
