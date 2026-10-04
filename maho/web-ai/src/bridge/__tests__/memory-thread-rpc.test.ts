import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import {
  _getPendingCountForTest,
  _resetBridgeStateForTest,
  call,
  disposeDocumentBridge,
} from '../rpc';

describe('Bridge RPC lost-response and disposal regressions (U15)', () => {
  let nativeRpcSpy: ReturnType<typeof vi.fn>;

  beforeEach(() => {
    vi.useFakeTimers();
    _resetBridgeStateForTest();
    nativeRpcSpy = vi.fn();
    Object.defineProperty(window, 'MahoBridgeAndroid', {
      value: { rpc: nativeRpcSpy },
      writable: true,
      configurable: true,
    });
  });

  afterEach(() => {
    vi.restoreAllMocks();
    vi.useRealTimers();
    _resetBridgeStateForTest();
    delete (window as unknown as { MahoBridgeAndroid?: unknown }).MahoBridgeAndroid;
  });

  it('memory-thread: lost response times out and releases capacity', async () => {
    // Given: an RPC call where native transport accepts the request but never responds
    const promise = call<string>('agentPollEvent', { handle: 'h1' }, { timeoutMs: 100 });
    expect(_getPendingCountForTest()).toBe(1);

    // When: virtual time advances past the timeout deadline
    vi.advanceTimersByTime(150);

    // Then: the promise must reject with typed error rpc_timeout and release pending capacity
    // Production baseline bug: call has no timeout handling; promise stays pending forever,
    // and _pending Map retains the entry indefinitely (leak).
    let caughtError: Error | null = null;
    try {
      await promise;
    } catch (err) {
      caughtError = err instanceof Error ? err : new Error(String(err));
    }

    expect(caughtError).not.toBeNull();
    expect(caughtError?.message).toContain('rpc_timeout');
    expect(_getPendingCountForTest()).toBe(0);
  });

  it('memory-thread: disposal settles every pending call once', async () => {
    // Given: multiple calls in flight
    const p1 = call<string>('methodA');
    const p2 = call<string>('methodB');
    expect(_getPendingCountForTest()).toBe(2);

    // When: document bridge disposal is triggered (e.g. pagehide or explicit disposal)
    disposeDocumentBridge();

    // Then: every pending call must reject exactly once with bridge_disposed
    // Production baseline bug: disposeDocumentBridge is an inert no-op; pending calls hang forever.
    const results = await Promise.allSettled([p1, p2]);
    expect(results[0]?.status).toBe('rejected');
    expect((results[0] as PromiseRejectedResult)?.reason?.message).toContain('bridge_disposed');
    expect(results[1]?.status).toBe('rejected');
    expect((results[1] as PromiseRejectedResult)?.reason?.message).toContain('bridge_disposed');
    expect(_getPendingCountForTest()).toBe(0);
  });

  it('memory-thread: abort response race has one settlement', async () => {
    // Given: an in-flight call with an AbortSignal
    const controller = new AbortController();
    const promise = call<string>('testMethod', {}, { signal: controller.signal });
    expect(_getPendingCountForTest()).toBe(1);

    // When: caller aborts the operation
    controller.abort();

    // And Then: promise rejects with rpc_aborted, and capacity is released
    // Production baseline bug: call does not listen to signal; promise never rejects.
    let caughtError: Error | null = null;
    try {
      await promise;
    } catch (err) {
      caughtError = err instanceof Error ? err : new Error(String(err));
    }
    expect(caughtError).not.toBeNull();
    expect(caughtError?.message).toContain('rpc_aborted');
    expect(_getPendingCountForTest()).toBe(0);
  });

  it('memory-thread: serialization failure retains nothing', async () => {
    // Given: an object that cannot be serialized to JSON (circular structure)
    const circular: Record<string, unknown> = {};
    circular.self = circular;

    // When: call is made with nonserializable parameters
    // Then: call must reject synchronously or immediately, and _pending must retain NOTHING (0 entries).
    // Production baseline bug: _pending.set(id, ...) is called BEFORE JSON.stringify.
    // When JSON.stringify throws, _pending retains the orphaned entry forever!
    let threw = false;
    try {
      await call('testMethod', circular);
    } catch {
      threw = true;
    }

    expect(threw).toBe(true);
    expect(_getPendingCountForTest()).toBe(0);
  });

  it('memory-thread: late resource response is released by native owner', async () => {
    // Given: a faithful native host fixture implementing the U15 request-ledger contract
    interface NativeLedgerEntry {
      requestId: string;
      handle: string;
      abandoned: boolean;
      released: boolean;
    }
    const nativeLedger = new Map<string, NativeLedgerEntry>();
    nativeRpcSpy.mockImplementation((json: string) => {
      const parsed = JSON.parse(json) as {
        method: string;
        id: string;
        params: Record<string, unknown>;
      };
      if (parsed.method === 'agentCreateSession') {
        nativeLedger.set(parsed.id, {
          requestId: parsed.id,
          handle: `native-handle-${parsed.id}`,
          abandoned: false,
          released: false,
        });
      } else if (parsed.method === 'bridgeRequestAbandon') {
        const reqId = typeof parsed.params?.requestId === 'string' ? parsed.params.requestId : '';
        const entry = nativeLedger.get(reqId);
        if (entry) {
          entry.abandoned = true;
          // Native request ledger immediately frees abandoned session upon completion:
          entry.released = true;
        }
      }
    });

    // When: web-ai initiates a resource creation with a short deadline
    const createPromise = call<string>(
      'agentCreateSession',
      { sessionId: 'session-late-1' },
      { timeoutMs: 50 },
    );

    expect(nativeLedger.size).toBe(1);
    const entry = Array.from(nativeLedger.values())[0]!;

    // When: deadline expires and web-ai times out / abandons the create wait
    vi.advanceTimersByTime(100);

    try {
      await createPromise;
    } catch {
      // Expected timeout
    }

    // Then: U15 requires web-ai to notify native via internal control message bridgeRequestAbandon
    // so that the native request ledger immediately releases the abandoned late-completing session.
    // Production baseline bug: web-ai sends no bridgeRequestAbandon notification;
    // native ledger is never marked abandoned, and late resource leaks unreleased in native memory.
    expect(entry.abandoned).toBe(true);
    expect(entry.released).toBe(true);
  });
});
