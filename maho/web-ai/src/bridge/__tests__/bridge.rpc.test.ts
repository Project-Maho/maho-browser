import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { _resetBridgeStateForTest } from '../rpc';

type WindowWithBridge = Window & {
  __mahoBridgeResponse: (responseJson: string) => void;
  MahoBridgeAndroid?: { rpc: (json: string) => void };
  webkit?: { messageHandlers?: { mahoBridge?: { postMessage: (msg: string) => void } } };
};

function bridgeWindow(): WindowWithBridge {
  return window as unknown as WindowWithBridge;
}

function respondTo(id: string, result: unknown) {
  bridgeWindow().__mahoBridgeResponse(
    JSON.stringify({ jsonrpc: '2.0', id, result }),
  );
}

function respondError(id: string, error: { kind: string; [key: string]: unknown }) {
  bridgeWindow().__mahoBridgeResponse(
    JSON.stringify({ jsonrpc: '2.0', id, error }),
  );
}

// ─── Android transport tests ────────────────────────────────────────────────

describe('RPC via Android transport', () => {
  const rpcSpy = vi.fn<(json: string) => void>();

  beforeEach(() => {
    _resetBridgeStateForTest();
    Object.defineProperty(window, 'MahoBridgeAndroid', {
      value: { rpc: rpcSpy },
      writable: true,
      configurable: true,
    });
    rpcSpy.mockClear();
    window.dispatchEvent(new CustomEvent('__mahoBridgeReady'));
  });

  afterEach(() => {
    delete (window as unknown as { MahoBridgeAndroid?: unknown }).MahoBridgeAndroid;
  });

  it('sends a JSON-RPC 2.0 request with string UUID id', async () => {
    // Import fresh to pick up window mock
    const { call } = await import('../rpc');

    const promise = call<string[]>('byokGetProviders');

    expect(rpcSpy).toHaveBeenCalledOnce();
    const sent = JSON.parse(rpcSpy.mock.calls[0]![0]) as {
      jsonrpc: string;
      method: string;
      id: string;
      params?: Record<string, unknown>;
    };
    expect(sent.jsonrpc).toBe('2.0');
    expect(sent.method).toBe('byokGetProviders');
    expect(typeof sent.id).toBe('string');
    // UUID-v4 format check (loose)
    expect(sent.id.length).toBeGreaterThanOrEqual(32);

    respondTo(sent.id, ['openai', 'anthropic']);
    await expect(promise).resolves.toEqual(['openai', 'anthropic']);
  });

  it('passes named params in request', async () => {
    const { call } = await import('../rpc');

    const promise = call<void>('byokSetKey', { provider: 'openai', key: 'sk-abc' });

    const sent = JSON.parse(rpcSpy.mock.calls[0]![0]) as {
      id: string;
      params?: Record<string, unknown>;
    };
    expect(sent.params).toEqual({ provider: 'openai', key: 'sk-abc' });

    respondTo(sent.id, undefined);
    await promise;
  });

  it('resolves multiple in-flight calls correctly (out-of-order)', async () => {
    const { call } = await import('../rpc');

    const p1 = call<string | null>('byokGetKey', { provider: 'openai' });
    const p2 = call<string | null>('byokGetKey', { provider: 'anthropic' });

    const id1 = (JSON.parse(rpcSpy.mock.calls[0]![0]) as { id: string }).id;
    const id2 = (JSON.parse(rpcSpy.mock.calls[1]![0]) as { id: string }).id;

    // Respond out of order
    respondTo(id2, 'sk-ant-key');
    respondTo(id1, 'sk-oai-key');

    await expect(p1).resolves.toBe('sk-oai-key');
    await expect(p2).resolves.toBe('sk-ant-key');
  });

  it('large session handle string (>2^53) survives round-trip exactly', async () => {
    const { call } = await import('../rpc');

    const largeHandle = '9007199254740993'; // 2^53 + 1 — would truncate if number
    const promise = call<string>('chatSessionStart', { opts: {} });

    const sent = JSON.parse(rpcSpy.mock.calls[0]![0]) as { id: string };
    respondTo(sent.id, largeHandle);

    const result = await promise;
    expect(result).toBe(largeHandle);
    expect(result).toBe('9007199254740993');
    // Verify it's still exactly the same string (no numeric truncation)
    expect(result).not.toBe('9007199254740992');
  });

  it('rejects with BridgeError on session_invalid', async () => {
    const { call, isBridgeRpcError } = await import('../rpc');

    const promise = call<void>('chatSessionFree', { handle: 'abc-123' });

    const sent = JSON.parse(rpcSpy.mock.calls[0]![0]) as { id: string };
    respondError(sent.id, { kind: 'session_invalid', id: 'abc-123' });

    try {
      await promise;
      expect.fail('should have rejected');
    } catch (err: unknown) {
      expect(isBridgeRpcError(err)).toBe(true);
      if (isBridgeRpcError(err)) {
        expect(err.bridgeError.kind).toBe('session_invalid');
        if (err.bridgeError.kind === 'session_invalid') {
          expect(err.bridgeError.id).toBe('abc-123');
        }
      }
    }
  });

  it('rejects with BridgeError on native_error', async () => {
    const { call, isBridgeRpcError } = await import('../rpc');

    const promise = call<void>('byokSetKey', { provider: 'openai', key: 'sk-test' });

    const sent = JSON.parse(rpcSpy.mock.calls[0]![0]) as { id: string };
    respondError(sent.id, {
      kind: 'native_error',
      method: 'byokSetKey',
      reason: 'disk full',
    });

    try {
      await promise;
      expect.fail('should have rejected');
    } catch (err: unknown) {
      expect(isBridgeRpcError(err)).toBe(true);
      if (isBridgeRpcError(err)) {
        expect(err.bridgeError.kind).toBe('native_error');
      }
    }
  });
});

// ─── iOS transport tests ────────────────────────────────────────────────────

describe('RPC via iOS transport', () => {
  const postMessageSpy = vi.fn<(json: string) => void>();

  beforeEach(() => {
    _resetBridgeStateForTest();
    Object.defineProperty(window, 'webkit', {
      value: { messageHandlers: { mahoBridge: { postMessage: postMessageSpy } } },
      writable: true,
      configurable: true,
    });
    postMessageSpy.mockClear();
    window.dispatchEvent(new CustomEvent('__mahoBridgeReady'));
  });

  afterEach(() => {
    delete (window as unknown as { webkit?: unknown }).webkit;
  });

  it('sends via webkit messageHandlers on iOS', async () => {
    const { call } = await import('../rpc');

    const promise = call<string[]>('byokGetProviders');

    expect(postMessageSpy).toHaveBeenCalledOnce();
    const sent = JSON.parse(postMessageSpy.mock.calls[0]![0]) as {
      jsonrpc: string;
      id: string;
      method: string;
    };
    expect(sent.jsonrpc).toBe('2.0');
    expect(sent.method).toBe('byokGetProviders');
    expect(typeof sent.id).toBe('string');

    respondTo(sent.id, ['openai']);
    await expect(promise).resolves.toEqual(['openai']);
  });
});

// ─── Platform detection tests ───────────────────────────────────────────────

describe('detectPlatform', () => {
  afterEach(() => {
    delete (window as unknown as { MahoBridgeAndroid?: unknown }).MahoBridgeAndroid;
    delete (window as unknown as { webkit?: unknown }).webkit;
  });

  it('returns "android" when MahoBridgeAndroid exists', async () => {
    Object.defineProperty(window, 'MahoBridgeAndroid', {
      value: { rpc: vi.fn() },
      writable: true,
      configurable: true,
    });
    const { detectPlatform } = await import('../rpc');
    expect(detectPlatform()).toBe('android');
  });

  it('returns "ios" when webkit.messageHandlers.mahoBridge exists', async () => {
    Object.defineProperty(window, 'webkit', {
      value: { messageHandlers: { mahoBridge: { postMessage: vi.fn() } } },
      writable: true,
      configurable: true,
    });
    const { detectPlatform } = await import('../rpc');
    expect(detectPlatform()).toBe('ios');
  });

  it('returns "unknown" when no bridge is present', async () => {
    const { detectPlatform } = await import('../rpc');
    expect(detectPlatform()).toBe('unknown');
  });
});

// ─── No transport available ─────────────────────────────────────────────────

describe('no transport available', () => {
  beforeEach(() => {
    _resetBridgeStateForTest();
    delete (window as unknown as { MahoBridgeAndroid?: unknown }).MahoBridgeAndroid;
    delete (window as unknown as { webkit?: unknown }).webkit;
    window.dispatchEvent(new CustomEvent('__mahoBridgeReady'));
  });

  it('rejects immediately with descriptive error', async () => {
    const { call } = await import('../rpc');

    await expect(call('byokGetProviders')).rejects.toThrow(
      'no native transport available',
    );
  });

  it('includes method name in rejection message', async () => {
    const { call } = await import('../rpc');

    await expect(call('chatSessionStart', { opts: {} })).rejects.toThrow(
      'chatSessionStart',
    );
  });
});
