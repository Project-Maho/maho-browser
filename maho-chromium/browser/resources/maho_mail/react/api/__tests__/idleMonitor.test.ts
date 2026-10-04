// Copyright 2026 Maho Browser. All rights reserved.

import {beforeEach, describe, expect, it, vi} from 'vitest';

// R3: StartIdleMonitor/StopIdleMonitor must not fake success. The C++ arms
// either invoke the real sync-worker primitive (Start) or return ok:false with
// an explicit error (Stop, no per-account idle-stop FFI exists). callBackend
// rejects on !ok, so these tests prove the rejection contract and the
// account_id forwarding without pinning any error prose.
const mojoMocks = vi.hoisted(() => ({
  handler: {
    callBackend: vi.fn<
      (command: string, argsJson: string) => Promise<{readonly ok: boolean; readonly resultJson: string}>
    >(),
  },
}));

vi.mock('../../mojo_client.js', () => mojoMocks);

import {startIdleMonitor, stopIdleMonitor} from '../index';

describe('idleMonitor backend contract', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it('startIdleMonitor forwards the account id and resolves on backend ok', async () => {
    mojoMocks.handler.callBackend.mockResolvedValueOnce({ok: true, resultJson: '{}'});
    await expect(startIdleMonitor('acc-1')).resolves.toEqual({});
    expect(mojoMocks.handler.callBackend).toHaveBeenCalledWith(
      'StartIdleMonitor',
      JSON.stringify({account_id: 'acc-1'}),
    );
  });

  it('startIdleMonitor rejects instead of fake-resolving when the backend reports failure', async () => {
    mojoMocks.handler.callBackend.mockResolvedValueOnce({
      ok: false,
      resultJson: '{"error":"Failed to start idle monitor"}',
    });
    await expect(startIdleMonitor('acc-1')).rejects.toThrow();
  });

  it('stopIdleMonitor surfaces the backend not-supported error as a rejection', async () => {
    mojoMocks.handler.callBackend.mockResolvedValueOnce({
      ok: false,
      resultJson: '{"error":"StopIdleMonitor not supported"}',
    });
    await expect(stopIdleMonitor('acc-1')).rejects.toThrow();
    expect(mojoMocks.handler.callBackend).toHaveBeenCalledWith(
      'StopIdleMonitor',
      JSON.stringify({account_id: 'acc-1'}),
    );
  });

  it('stopIdleMonitor passes a future backend ok through without throwing', async () => {
    mojoMocks.handler.callBackend.mockResolvedValueOnce({ok: true, resultJson: '{}'});
    await expect(stopIdleMonitor('acc-1')).resolves.toEqual({});
  });
});
