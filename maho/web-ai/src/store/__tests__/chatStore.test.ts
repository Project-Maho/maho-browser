import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import type { ChatEvent } from '../../bridge/types';
import {
  StreamCadenceBuffer,
  coalesceChatEventBatch,
  createChatStore,
} from '../chatStore';

describe('StreamCadenceBuffer', () => {
  beforeEach(() => {
    vi.useFakeTimers();
  });

  afterEach(() => {
    vi.restoreAllMocks();
  });

  it('buffers high-frequency streaming tokens and flushes on cadence', () => {
    const onTokenFlush = vi.fn();
    const cadence = new StreamCadenceBuffer({
      frameWindowMs: 16,
      onTokenFlush,
    });

    cadence.pushToken('Hello');
    cadence.pushToken(' ');
    cadence.pushToken('world');
    cadence.pushToken('!');

    expect(onTokenFlush).not.toHaveBeenCalled();
    expect(cadence.hasPending).toBe(true);

    vi.advanceTimersByTime(16);

    expect(onTokenFlush).toHaveBeenCalledTimes(1);
    expect(onTokenFlush).toHaveBeenCalledWith('Hello world!');
    expect(cadence.hasPending).toBe(false);
  });

  it('buffers thinking chunks and flushes on cadence', () => {
    const onThinkingFlush = vi.fn();
    const cadence = new StreamCadenceBuffer({
      frameWindowMs: 16,
      onThinkingFlush,
    });

    cadence.pushThinking('Thinking part 1. ');
    cadence.pushThinking('Thinking part 2.');

    expect(onThinkingFlush).not.toHaveBeenCalled();
    expect(cadence.hasPending).toBe(true);

    vi.advanceTimersByTime(16);

    expect(onThinkingFlush).toHaveBeenCalledTimes(1);
    expect(onThinkingFlush).toHaveBeenCalledWith('Thinking part 1. Thinking part 2.');
    expect(cadence.hasPending).toBe(false);
  });

  it('synchronously flushes immediately when flush() is called', () => {
    const onTokenFlush = vi.fn();
    const cadence = new StreamCadenceBuffer({
      frameWindowMs: 16,
      onTokenFlush,
    });

    cadence.pushToken('Instant token');
    expect(onTokenFlush).not.toHaveBeenCalled();

    cadence.flush();
    expect(onTokenFlush).toHaveBeenCalledTimes(1);
    expect(onTokenFlush).toHaveBeenCalledWith('Instant token');
    expect(cadence.hasPending).toBe(false);

    // Subsequent timer tick should not double-flush
    vi.advanceTimersByTime(32);
    expect(onTokenFlush).toHaveBeenCalledTimes(1);
  });

  it('cancels pending buffers without emitting when cancel() is called', () => {
    const onTokenFlush = vi.fn();
    const cadence = new StreamCadenceBuffer({
      frameWindowMs: 16,
      onTokenFlush,
    });

    cadence.pushToken('Dropped token');
    cadence.cancel();

    expect(cadence.hasPending).toBe(false);
    vi.advanceTimersByTime(32);
    expect(onTokenFlush).not.toHaveBeenCalled();
  });
});

describe('coalesceChatEventBatch', () => {
  it('merges contiguous token events in a batch into a single token event', () => {
    const input: ChatEvent[] = [
      { kind: 'token', token: 'H' },
      { kind: 'token', token: 'e' },
      { kind: 'token', token: 'llo ' },
      { kind: 'token', token: 'world' },
    ];

    const result = coalesceChatEventBatch(input);
    expect(result).toHaveLength(1);
    expect(result[0]).toEqual({ kind: 'token', token: 'Hello world' });
  });

  it('merges contiguous thinking events', () => {
    const input: ChatEvent[] = [
      { kind: 'thinking', thinking: 'Step 1. ' },
      { kind: 'thinking', thinking: 'Step 2.' },
    ];

    const result = coalesceChatEventBatch(input);
    expect(result).toHaveLength(1);
    expect(result[0]).toEqual({ kind: 'thinking', thinking: 'Step 1. Step 2.' });
  });

  it('preserves ordering when interrupted by non-token/thinking events', () => {
    const input: ChatEvent[] = [
      { kind: 'token', token: 'Before tool ' },
      { kind: 'token', token: 'call' },
      { kind: 'tool_call', toolCallId: 't1', name: 'search', args: { query: 'test' } },
      { kind: 'token', token: 'After ' },
      { kind: 'token', token: 'tool' },
      { kind: 'complete', content: 'Final' },
    ];

    const result = coalesceChatEventBatch(input);
    expect(result).toEqual([
      { kind: 'token', token: 'Before tool call' },
      { kind: 'tool_call', toolCallId: 't1', name: 'search', args: { query: 'test' } },
      { kind: 'token', token: 'After tool' },
      { kind: 'complete', content: 'Final' },
    ]);
  });

  it('handles empty or single item arrays cleanly', () => {
    expect(coalesceChatEventBatch([])).toEqual([]);
    expect(coalesceChatEventBatch([{ kind: 'token', token: 'solo' }])).toEqual([
      { kind: 'token', token: 'solo' },
    ]);
  });
});

describe('createChatStore', () => {
  beforeEach(() => {
    vi.useFakeTimers();
  });

  afterEach(() => {
    vi.restoreAllMocks();
  });

  it('initializes with default state and supports subscription updates', () => {
    const store = createChatStore();
    expect(store.getState().booting).toBe(true);
    expect(store.getState().messages).toHaveLength(0);

    const listener = vi.fn();
    const unsub = store.subscribe(listener);

    store.dispatch({
      type: 'BOOT_SUCCESS',
      conversationId: 'c1',
      conversationPersisted: true,
      messages: [],
      provider: { id: 'openai', label: 'OpenAI' },
      sessionHandle: 'h1',
    });

    expect(store.getState().sessionHandle).toBe('h1');
    expect(store.getState().booting).toBe(false);
    expect(listener).toHaveBeenCalledTimes(1);

    unsub();
    store.dispatch({ type: 'SET_DRAFT', value: 'hello' });
    expect(listener).toHaveBeenCalledTimes(1);
    expect(store.getState().draft).toBe('hello');
  });

  it('buffers streaming tokens via pushStreamingToken and flushes on cadence', () => {
    const store = createChatStore({
      booting: false,
      sessionHandle: 'h1',
      messages: [
        { kind: 'chat', id: 'u1', role: 'user', content: { kind: 'text', text: 'Hi' } },
        { kind: 'chat', id: 'a1', role: 'assistant', content: '', images: [], isStreaming: true },
      ],
    });

    store.pushStreamingToken('A');
    store.pushStreamingToken('B');
    store.pushStreamingToken('C');

    // Before timer advances, state has not yet updated
    expect((store.getState().messages[1] as any).content).toBe('');

    vi.advanceTimersByTime(16);

    expect((store.getState().messages[1] as any).content).toBe('ABC');
  });

  it('flushes pending streaming tokens synchronously before processing complete action', () => {
    const store = createChatStore({
      booting: false,
      sessionHandle: 'h1',
      messages: [
        { kind: 'chat', id: 'u1', role: 'user', content: { kind: 'text', text: 'Hi' } },
        { kind: 'chat', id: 'a1', role: 'assistant', content: 'Initial ', images: [], isStreaming: true },
      ],
    });

    store.pushStreamingToken('buffered text');
    // Dispatch complete directly while tokens are buffered
    store.dispatch({ type: 'STREAM_COMPLETE', finalMessage: 'Initial buffered text' });

    expect(store.getState().isStreaming).toBe(false);
    expect((store.getState().messages[1] as any).content).toBe('Initial buffered text');
    expect((store.getState().messages[1] as any).isStreaming).toBe(false);
  });
});
