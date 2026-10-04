import { act, renderHook } from '@testing-library/preact';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import type { AgentEvent, MahoBridge } from '../../bridge/types';
import { useAgentSession, wakeAgentSession } from '../use-agent-session';

describe('useAgentSession polling regressions (U12w)', () => {
  beforeEach(() => {
    vi.useFakeTimers();
  });

  afterEach(() => {
    vi.restoreAllMocks();
    vi.useRealTimers();
  });

  it('memory-thread: idle backoff and local wake', async () => {
    // Given: an active agent session attached to a quiet/idle queue (returning null)
    let pollCount = 0;
    const bridge: Partial<MahoBridge> = {
      agentPollEvent: vi.fn().mockImplementation(async () => {
        pollCount++;
        return null;
      }),
    };
    const onEvent = vi.fn();
    const wakeRef: { current: (() => void) | null } = { current: null };

    renderHook(() =>
      useAgentSession({
        bridge: bridge as MahoBridge,
        handle: 'test-handle',
        onEvent,
        wakeRef,
      }),
    );

    // When: 60 virtual seconds pass while completely idle
    await act(async () => {
      vi.advanceTimersByTime(60_000);
    });

    // Then: empty results must back off (50 -> 100 -> 250 -> 500 -> 1000ms),
    // bounding 60s idle polls to at most 65 calls (<=60 ceiling fallback + initial ramp).
    // Production baseline bug: fixed 50ms interval produces 1,200 polls per 60 seconds.
    expect(pollCount).toBeLessThanOrEqual(65);

    // And When: local wake is triggered (via wakeAgentSession or wakeRef)
    const countBeforeWake = pollCount;
    await act(async () => {
      if (wakeRef.current) {
        wakeRef.current();
      } else {
        wakeAgentSession('test-handle');
      }
    });

    // Then: local wake must trigger an immediate poll without waiting for fallback
    expect(pollCount).toBe(countBeforeWake + 1);
  });

  it('memory-thread: terminal retains replay delivery', async () => {
    // Given: an agent session where the active turn completes
    const completeEvent = JSON.stringify({
      type: 'complete',
      data: { full_text: 'Turn finished successfully.' },
    });
    const replayedInteraction = JSON.stringify({
      type: 'interaction_request',
      data: {
        id: 'ir-replayed-1',
        question: 'Confirm action?',
        options: [{ id: 'yes', label: 'Yes' }],
      },
    });

    const queue: (string | null)[] = [completeEvent];
    let pollCount = 0;
    const bridge: Partial<MahoBridge> = {
      agentPollEvent: vi.fn().mockImplementation(async () => {
        pollCount++;
        return queue.length > 0 ? (queue.shift() ?? null) : null;
      }),
    };
    const events: AgentEvent[] = [];
    const onDone = vi.fn();

    renderHook(() =>
      useAgentSession({
        bridge: bridge as MahoBridge,
        handle: 'test-handle',
        onEvent: (ev) => events.push(ev),
        onDone,
      }),
    );

    // First tick delivers complete
    await act(async () => {
      vi.advanceTimersByTime(50);
    });
    expect(onDone).toHaveBeenCalledOnce();
    expect(events.some((e) => e.kind === 'complete')).toBe(true);

    // Turn is done; session enters idle fallback cadence.
    // Over 30 virtual seconds, poll count should be bounded to fallback rate (~30 polls, <=35).
    const pollsAfterComplete = pollCount;
    await act(async () => {
      vi.advanceTimersByTime(30_000);
    });
    const idlePolls = pollCount - pollsAfterComplete;
    expect(idlePolls).toBeLessThanOrEqual(35);

    // When: native enqueues a historical replay / interaction request while idle
    queue.push(replayedInteraction);

    // Within at most 1,000ms fallback interval, the event is picked up
    await act(async () => {
      vi.advanceTimersByTime(1_000);
    });

    // Then: the replayed event is delivered
    expect(
      events.some(
        (e) => e.kind === 'interaction_request' && e.id === 'ir-replayed-1',
      ),
    ).toBe(true);
  });

  it('memory-thread: hidden visible transition has one poll', async () => {
    // Given: an idle session on a hidden document
    let pollCount = 0;
    const bridge: Partial<MahoBridge> = {
      agentPollEvent: vi.fn().mockImplementation(async () => {
        pollCount++;
        return null;
      }),
    };
    const onEvent = vi.fn();

    Object.defineProperty(document, 'visibilityState', {
      value: 'hidden',
      writable: true,
      configurable: true,
    });
    document.dispatchEvent(new Event('visibilitychange'));

    renderHook(() =>
      useAgentSession({
        bridge: bridge as MahoBridge,
        handle: 'test-handle',
        onEvent,
      }),
    );

    // When: 60 virtual seconds pass while document is hidden
    await act(async () => {
      vi.advanceTimersByTime(60_000);
    });

    // Then: hidden documents must use 5,000ms fallback, so 60s yields at most 13 polls (60s/5s = 12 + 1).
    // Production baseline bug: hidden state is ignored; fixed 50ms interval produces 1,200 polls.
    expect(pollCount).toBeLessThanOrEqual(13);

    // When: document transitions back to visible
    const countBeforeVisible = pollCount;
    Object.defineProperty(document, 'visibilityState', {
      value: 'visible',
      writable: true,
      configurable: true,
    });
    document.dispatchEvent(new Event('visibilitychange'));

    // Then: visible-page transition must trigger exactly one immediate poll without overlap
    expect(pollCount).toBe(countBeforeVisible + 1);
  });

  it('memory-thread: bulk drain yields without overlap', async () => {
    // Given: 64 agent token events buffered in native queue
    const queued: string[] = [];
    for (let i = 0; i < 64; i++) {
      queued.push(JSON.stringify({ type: 'token', data: `t${i}` }));
    }

    let activePolls = 0;
    let maxInFlight = 0;
    const bridge: Partial<MahoBridge> = {
      agentPollEvent: vi.fn().mockImplementation(async () => {
        activePolls++;
        maxInFlight = Math.max(maxInFlight, activePolls);
        const item = queued.shift() ?? null;
        activePolls--;
        return item;
      }),
    };
    const received: AgentEvent[] = [];

    renderHook(() =>
      useAgentSession({
        bridge: bridge as MahoBridge,
        handle: 'test-handle',
        onEvent: (ev) => received.push(ev),
      }),
    );

    // When: first tick runs (advance 50ms to start first task)
    // DELTA FIX (recorded): preact test-utils act schedules its finish() as a
    // single microtask after the callback promise resolves, so a timer-entered
    // async drain cascade advances only ~1 checkpoint per act. The assertions
    // below are unchanged; we pump the timer until the drain task reaches its
    // 64-event yield point instead of assuming a single-act full flush.
    await act(async () => {
      vi.advanceTimersByTime(50);
    });
    for (let pump = 0; pump < 64 && received.length < 64; pump++) {
      await act(async () => {
        vi.advanceTimersByTime(50);
      });
    }

    // Then: while data exists, up to 64 events must be drained in that single task
    // without a 50ms delay per individual token, and strictly without overlapping calls.
    // Production baseline bug: only 1 event is drained per 50ms tick (received.length is 1).
    expect(maxInFlight).toBe(1);
    expect(received.length).toBe(64);
  });
});
