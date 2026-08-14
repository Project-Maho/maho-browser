import { cleanup, fireEvent, render, screen } from '@testing-library/preact';
import { h } from 'preact';
import { afterEach, describe, expect, it } from 'vitest';
import {
  getCredentialErrorPresentation,
  parseCredentialErrorCode,
} from '../credential-error';
import { CredentialErrorCard } from '../credential-error-card';

afterEach(cleanup);

describe('parseCredentialErrorCode', () => {
  it('parses the typed credential envelope into its code', () => {
    const envelope = JSON.stringify({
      version: 1,
      kind: 'credential_error',
      code: 'secure_store_unavailable',
    });
    expect(parseCredentialErrorCode(envelope)).toBe('secure_store_unavailable');
  });

  it('parses the managed-auth code used for the subscription/account path', () => {
    const envelope = JSON.stringify({
      version: 1,
      kind: 'credential_error',
      code: 'managed_auth_unavailable',
    });
    expect(parseCredentialErrorCode(envelope)).toBe('managed_auth_unavailable');
  });

  it('returns null for a plain raw diagnostic string', () => {
    expect(
      parseCredentialErrorCode('Execution error: Secure storage callback is missing'),
    ).toBeNull();
  });

  it('returns null for non-credential JSON and garbage', () => {
    expect(parseCredentialErrorCode('{"kind":"other","code":"x"}')).toBeNull();
    expect(parseCredentialErrorCode('{"kind":"credential_error","code":"nope"}')).toBeNull();
    expect(parseCredentialErrorCode('not json')).toBeNull();
    expect(parseCredentialErrorCode(null)).toBeNull();
  });

  it('rejects noncanonical credential envelopes', () => {
    expect(parseCredentialErrorCode(JSON.stringify({
      version: 2,
      kind: 'credential_error',
      code: 'managed_auth_unavailable',
    }))).toBeNull();
    expect(parseCredentialErrorCode(JSON.stringify({
      version: 1,
      kind: 'credential_error',
      code: 'managed_auth_unavailable',
      diagnostic: 'must not cross the UI boundary',
    }))).toBeNull();
  });
});

describe('getCredentialErrorPresentation', () => {
  it('routes BYOK/key failures to AI settings', () => {
    expect(getCredentialErrorPresentation('secure_store_unavailable')).toMatchObject({
      target: 'ai-settings',
      actionLabel: 'Open AI settings',
    });
  });

  it('routes managed auth to account (subscription) settings', () => {
    expect(getCredentialErrorPresentation('managed_auth_unavailable')).toMatchObject({
      target: 'account',
      actionLabel: 'Open account settings',
    });
  });

  it('never echoes the raw diagnostic into the presentation copy', () => {
    const p = getCredentialErrorPresentation('secure_store_unavailable');
    expect(JSON.stringify(p)).not.toContain('Secure storage callback');
  });

  it('sends managed account recovery directly to the authentication step', () => {
    window.location.hash = '#chat';
    render(h(CredentialErrorCard, { code: 'managed_auth_unavailable' }));

    fireEvent.click(screen.getByTestId('credential-error-action'));

    expect(window.location.hash).toBe('#onboarding?step=auth');
  });
});
