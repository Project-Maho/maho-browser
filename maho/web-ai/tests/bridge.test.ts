/**
 * Unit tests for the RPC dispatcher (src/bridge/rpc.ts).
 *
 * We simulate the native response callback (__mahoBridgeResponse) directly
 * so these tests run entirely in jsdom without a real WebView.
 */
import { beforeEach, describe, expect, it, vi } from 'vitest';

// Set up a fake Android bridge on window before importing rpc.ts
const rpcSpy = vi.fn<(json: string) => void>();
Object.defineProperty(window, 'MahoBridgeAndroid', {
  value: { rpc: rpcSpy },
  writable: true,
  configurable: true,
});

// Dynamic import so the module picks up the window mock
const { callBridge } = await import('../src/bridge/rpc');

type WindowWithBridge = Window & {
  __mahoBridgeResponse: (responseJson: string) => void;
  MahoBridgeAndroid: { rpc: (json: string) => void };
};

function bridgeWindow(): WindowWithBridge {
  return window as unknown as WindowWithBridge;
}

function respondTo(id: string, result: unknown) {
  bridgeWindow().__mahoBridgeResponse(JSON.stringify({ id, result }));
}

function respondError(id: string, kind: string) {
  bridgeWindow().__mahoBridgeResponse(JSON.stringify({ id, error: { kind } }));
}

describe('callBridge', () => {
  beforeEach(() => {
    rpcSpy.mockClear();
  });

  it('sends a JSON-RPC 2.0 request through the Android transport', async () => {
    const promise = callBridge<string[]>('byokGetProviders');

    expect(rpcSpy).toHaveBeenCalledOnce();
    const sent = JSON.parse(rpcSpy.mock.calls[0]![0]) as { jsonrpc: string; method: string; params: unknown[]; id: string };
    expect(sent.jsonrpc).toBe('2.0');
    expect(sent.method).toBe('byokGetProviders');
    expect(Array.isArray(sent.params)).toBe(true);
    expect(typeof sent.id).toBe('string');

    respondTo(sent.id, ['openai', 'anthropic']);
    await expect(promise).resolves.toEqual(['openai', 'anthropic']);
  });

  it('resolves with the correct result for multiple in-flight calls', async () => {
    const p1 = callBridge<boolean>('byokSetKey', ['openai', 'sk-test']);
    const p2 = callBridge<boolean>('byokValidateKey', ['anthropic', 'sk-ant-test']);

    const id1 = (JSON.parse(rpcSpy.mock.calls[0]![0]) as { id: string }).id;
    const id2 = (JSON.parse(rpcSpy.mock.calls[1]![0]) as { id: string }).id;

    // Respond out of order
    respondTo(id2, false);
    respondTo(id1, true);

    await expect(p1).resolves.toBe(true);
    await expect(p2).resolves.toBe(false);
  });

  it('rejects when native returns an error object', async () => {
    const promise = callBridge<string | null>('byokGetKey', ['openai']);
    const id = (JSON.parse(rpcSpy.mock.calls[0]![0]) as { id: string }).id;

    respondError(id, 'Core not initialised');
    await expect(promise).rejects.toThrow('Core not initialised');
  });

  it('passes params correctly', async () => {
    const promise = callBridge<boolean>('byokSetKey', ['openai', 'sk-abc123']);
    const sent = JSON.parse(rpcSpy.mock.calls[0]![0]) as { params: unknown[]; id: string };
    expect(sent.params).toEqual(['openai', 'sk-abc123']);

    respondTo(sent.id, true);
    await promise;
  });

  it('rejects immediately when no transport is available', async () => {
    // Temporarily remove the Android bridge
    const original = (window as unknown as WindowWithBridge).MahoBridgeAndroid;
    delete (window as unknown as { MahoBridgeAndroid?: unknown }).MahoBridgeAndroid;

    await expect(callBridge('byokGetProviders')).rejects.toThrow('no native transport available');

    (window as unknown as WindowWithBridge).MahoBridgeAndroid = original;
  });
});
