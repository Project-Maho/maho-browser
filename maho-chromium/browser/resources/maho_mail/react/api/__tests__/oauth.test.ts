// Copyright 2026 Maho Browser. All rights reserved.

import {beforeEach, describe, expect, it, vi} from 'vitest';

const mojoMocks = vi.hoisted(() => ({
  handler: {
    beginOAuth: vi.fn<(provider: string, reauthorizeAccountId: string) => Promise<{readonly ok: boolean; readonly errorJson: string; readonly state: string}>>(),
  },
}));

vi.mock('../../mojo_client.js', () => mojoMocks);

import {startOAuth2} from '../index';

describe('startOAuth2', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it('returns the OAuth state from browser-driven BeginOAuth', async () => {
    mojoMocks.handler.beginOAuth.mockResolvedValueOnce({
      ok: true,
      errorJson: '',
      state: 'state_from_cpp',
    });

    await expect(startOAuth2('gmail')).resolves.toBe('state_from_cpp');
    expect(mojoMocks.handler.beginOAuth).toHaveBeenCalledWith('gmail', '');
  });

  it('passes an existing account ID for OAuth reauthorization', async () => {
    mojoMocks.handler.beginOAuth.mockResolvedValueOnce({
      ok: true,
      errorJson: '',
      state: 'reauthorization_state',
    });

    await expect(startOAuth2('gmail', 'existing-account')).resolves.toBe(
      'reauthorization_state',
    );
    expect(mojoMocks.handler.beginOAuth).toHaveBeenCalledWith(
      'gmail',
      'existing-account',
    );
  });

  it('preserves the legacy rejection path when BeginOAuth fails', async () => {
    mojoMocks.handler.beginOAuth.mockResolvedValueOnce({
      ok: false,
      errorJson: 'OAuth service unavailable',
      state: '',
    });

    await expect(startOAuth2('outlook')).rejects.toThrow('OAuth service unavailable');
  });
});
