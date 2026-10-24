// Copyright 2026 Maho Browser. All rights reserved.

import {describe, expect, it} from 'vitest';

import {getAccountSessionNotice} from '../domain_panes.js';

describe('Account session notice', () => {
  it('shows a non-modal signed-out notice when the stored session expired', () => {
    expect(getAccountSessionNotice({signedIn: true, sessionValid: false})?.title)
        .toBe('Signed out - sync paused');
  });

  it('stays hidden for an active session or a never-signed-in profile', () => {
    expect(getAccountSessionNotice({signedIn: true, sessionValid: true})).toBeNull();
    expect(getAccountSessionNotice({signedIn: false, sessionValid: false})).toBeNull();
    expect(getAccountSessionNotice(null)).toBeNull();
  });
});
