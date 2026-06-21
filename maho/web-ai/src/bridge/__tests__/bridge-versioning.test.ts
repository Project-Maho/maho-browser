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

  it('includes version: "1.0" in every outgoing JSON-RPC envelope', async () => {
    const promise = call<string[]>('byokGetProviders');

    expect(rpcSpy).toHaveBeenCalledOnce();
    const sent = JSON.parse(rpcSpy.mock.calls[0]![0]) as {
      jsonrpc: string;
      version: string;
      method: string;
      id: string;
    };

    expect(sent.version).toBe('1.0');
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

describe('bridge ready handshake — queue drain', () => {
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

  it('queues calls made before __mahoBridgeReady and drains them in FIFO order', async () => {
    const p1 = call<string[]>('byokGetProviders');
    const p2 = call<boolean>('byokValidateKey', { provider: 'openai', key: 'sk-test' });

    expect(rpcSpy).not.toHaveBeenCalled();

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

describe('bridge ready handshake — timeout', () => {
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

  it('rejects all queued promises after 5s if __mahoBridgeReady never fires', async () => {
    const p1 = call<string[]>('byokGetProviders');
    const p2 = call<boolean>('byokDeleteKey', { provider: 'anthropic' });

    expect(rpcSpy).not.toHaveBeenCalled();

    vi.advanceTimersByTime(5001);

    await expect(p1).rejects.toThrow('bridge ready timeout');
    await expect(p2).rejects.toThrow('bridge ready timeout');
  });
});
