// Copyright 2026 Maho Browser. All rights reserved.

import {describe, expect, it} from 'vitest';

import {getAccountSignInCopy, getSyncAccountRequiredNotice} from '../domain_panes.js';

describe('Sync account-required status', () => {
  it('shows account-required to a local-only profile opening Sync settings', () => {
    const notice = getSyncAccountRequiredNotice({signedIn: false, localOnly: true});
    expect(notice?.title).toBe('Account required for sync');
  });

  it('shows account-required to any signed-out profile', () => {
    expect(getSyncAccountRequiredNotice({signedIn: false, localOnly: false})?.title)
        .toBe('Account required for sync');
  });

  it('shows sync controls once signed in or while the status is loading', () => {
    expect(getSyncAccountRequiredNotice({signedIn: true, localOnly: false})).toBeNull();
    expect(getSyncAccountRequiredNotice(null)).toBeNull();
  });

  it('offers Sign in later to a local-only profile on the Account pane', () => {
    expect(getAccountSignInCopy({localOnly: true}).title)
        .toBe('Using Maho without an account');
    expect(getAccountSignInCopy({localOnly: false}).title).toBe('Sign in to Maho');
    expect(getAccountSignInCopy(null).title).toBe('Sign in to Maho');
  });
});
