import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { _resetBridgeStateForTest, call, isBridgeRpcError } from '../rpc';

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

// Protocol v2 sends JSON-RPC immediately, without a legacy version field or handshake.
describe('version field in outgoing requests', () => {
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

  it('uses JSON-RPC 2.0 without a legacy version field', async () => {
    const promise = call<string[]>('byokGetProviders');

    expect(rpcSpy).toHaveBeenCalledOnce();
    const sent = JSON.parse(rpcSpy.mock.calls[0]![0]) as {
      jsonrpc: string;
      version: string;
      method: string;
      id: string;
    };

    expect(sent.version).toBeUndefined();
    expect(sent.jsonrpc).toBe('2.0');

    respondTo(sent.id, ['openai']);
    await promise;
  });
});

describe('version mismatch response', () => {
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

  it('rejects when native returns unsupported_version error', async () => {
    const promise = call<string[]>('byokGetProviders');

    const sent = JSON.parse(rpcSpy.mock.calls[0]![0]) as { id: string };

    bridgeWindow().__mahoBridgeResponse(
      JSON.stringify({
        jsonrpc: '2.0',
        id: sent.id,
        error: { kind: 'unsupported_version', message: 'Expected version 1.0, got 0.9' },
      }),
    );

    try {
      await promise;
      expect.fail('should have rejected');
    } catch (err: unknown) {
      expect(isBridgeRpcError(err)).toBe(true);
      if (isBridgeRpcError(err)) {
        expect(err.bridgeError.kind).toBe('unsupported_version');
      }
    }
  });
});

describe('requests without a legacy ready handshake', () => {
  const rpcSpy = vi.fn<(json: string) => void>();

  beforeEach(() => {
    _resetBridgeStateForTest();
    Object.defineProperty(window, 'MahoBridgeAndroid', {
      value: { rpc: rpcSpy },
      writable: true,
      configurable: true,
    });
    rpcSpy.mockClear();
  });

  afterEach(() => {
    delete (window as unknown as { MahoBridgeAndroid?: unknown }).MahoBridgeAndroid;
  });

  it('sends calls immediately in FIFO order and does not replay them on ready', async () => {
    const p1 = call<string[]>('byokGetProviders');
    const p2 = call<boolean>('byokValidateKey', { provider: 'openai', key: 'sk-test' });

    expect(rpcSpy).toHaveBeenCalledTimes(2);

    window.dispatchEvent(new CustomEvent('__mahoBridgeReady'));

    expect(rpcSpy).toHaveBeenCalledTimes(2);

    const id1 = (JSON.parse(rpcSpy.mock.calls[0]![0]) as { id: string }).id;
    const id2 = (JSON.parse(rpcSpy.mock.calls[1]![0]) as { id: string }).id;

    respondTo(id1, ['openai', 'anthropic']);
    respondTo(id2, true);

    await expect(p1).resolves.toEqual(['openai', 'anthropic']);
    await expect(p2).resolves.toBe(true);
  });
});

describe('request deadline without a legacy ready handshake', () => {
  const rpcSpy = vi.fn<(json: string) => void>();

  beforeEach(() => {
    _resetBridgeStateForTest();
    Object.defineProperty(window, 'MahoBridgeAndroid', {
      value: { rpc: rpcSpy },
      writable: true,
      configurable: true,
    });
    rpcSpy.mockClear();
    vi.useFakeTimers();
  });

  afterEach(() => {
    delete (window as unknown as { MahoBridgeAndroid?: unknown }).MahoBridgeAndroid;
    vi.useRealTimers();
    _resetBridgeStateForTest();
  });

  it('rejects sent requests at their individual deadlines if native never responds', async () => {
    const p1 = call<string[]>('byokGetProviders', undefined, { timeoutMs: 5000 });
    const p2 = call<boolean>('byokDeleteKey', { provider: 'anthropic' }, { timeoutMs: 5000 });
    const firstFailure = expect(p1).rejects.toThrow('rpc_timeout');
    const secondFailure = expect(p2).rejects.toThrow('rpc_timeout');

    expect(rpcSpy).toHaveBeenCalledTimes(2);
    vi.advanceTimersByTime(5000);

    await Promise.all([firstFailure, secondFailure]);
  });
});
